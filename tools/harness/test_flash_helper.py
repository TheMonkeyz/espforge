"""tools/devloop/flash_helper.py (the helper for macOS / Linux) without a board: the file protocol the harness relies on
against pyserial's loopback port (serial_live.txt line by line, serial.send echoed as "> line", stop.request,
serial_log.txt at the end), a board coming back under another port name, the restart through the test console (and
esptool when no console answers), and what it flashes (the staged copies, md5-checked). Skipped where pyserial is
missing; ESP-IDF's Python has it (CI installs it).

    python -m unittest discover -s tools/harness -p "test_*.py"
"""
import json
import os
import shutil
import subprocess
import sys
import tempfile
import threading
import time
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..'))
sys.path.insert(0, os.path.join(HERE, '..', 'devloop'))
try:
    import serial  # noqa: F401
    import flash_helper as fh
    import stage
except ImportError:
    fh = None


class TempDev(unittest.TestCase):
    """A repository root in a temporary folder: forge.json values from the real one, .devloop/ and the build here."""

    def setUp(self):
        self.root = tempfile.mkdtemp()
        self.old_cfg, self.old_dev = fh.CFG, fh.DEV
        fh.CFG = {**fh.CFG, '_root': self.root, 'build_dir': 'build/v55', 'port': ''}
        fh.DEV = fh.forgecfg.devloop(fh.CFG)

    def tearDown(self):
        fh.CFG, fh.DEV = self.old_cfg, self.old_dev
        shutil.rmtree(self.root, ignore_errors=True)

    def path(self, name):
        return os.path.join(fh.DEV, name)

    def read(self, name):
        with open(self.path(name), encoding='utf-8') as f:
            return f.read()


@unittest.skipIf(fh is None, 'pyserial missing (run from an ESP-IDF shell)')
class Monitor(TempDev):
    def run_monitor(self, seconds, during):
        res = {}
        t = threading.Thread(target=lambda: res.update(r=fh.monitor('loop://', seconds, url=True, echo=False)))
        t.start()
        during()
        t.join(seconds + 5)
        return res['r']

    def live(self):
        return self.read('serial_live.txt').splitlines()

    def wait_for(self, line, timeout=3):
        end = time.time() + timeout
        while time.time() < end:
            if os.path.exists(self.path('serial_live.txt')) and line in self.live():
                return True
            time.sleep(0.05)
        return False

    def test_command_is_sent_and_logged(self):
        def during():
            time.sleep(0.3)
            with open(self.path('serial.send'), 'w') as f:
                f.write('ping\n\nscreen\n')
            self.assertTrue(self.wait_for('> ping'), 'the command was not echoed in serial_live.txt')
            self.assertTrue(self.wait_for('ping'), 'the loopback did not return the bytes sent')
        lines, early, err = self.run_monitor(1.5, during)
        self.assertIsNone(err)
        self.assertFalse(early)
        self.assertIn('> ping', lines)
        self.assertIn('> screen', lines)
        self.assertNotIn('> ', lines)                 # the empty line is skipped
        self.assertFalse(os.path.exists(self.path('serial.send')), 'serial.send was not consumed')
        self.assertEqual(self.read('serial_log.txt').splitlines(), lines)

    def test_stop_request_ends_the_window_early(self):
        def during():
            time.sleep(0.3)
            open(self.path('stop.request'), 'w').close()
        t0 = time.time()
        _, early, err = self.run_monitor(20, during)
        self.assertIsNone(err)
        self.assertTrue(early)
        self.assertLess(time.time() - t0, 5)
        self.assertFalse(os.path.exists(self.path('stop.request')))

    def test_stale_serial_send_is_dropped(self):
        with open(self.path('serial.send'), 'w') as f:
            f.write('reboot\n')
        lines, _, _ = self.run_monitor(0.5, lambda: None)
        self.assertNotIn('> reboot', lines)


@unittest.skipIf(fh is None, 'pyserial missing (run from an ESP-IDF shell)')
class Reopen(TempDev):
    """After a restart the board's USB may come back under another name (macOS numbers usbmodem ports by location)."""

    def setUp(self):
        super().setUp()
        self.old = fh.board_ports, serial.Serial

        class FakeSerial:
            def open(s):
                if s.port != '/dev/cu.usbmodem1201':
                    raise serial.SerialException('no such port')
        serial.Serial = FakeSerial

    def tearDown(self):
        fh.board_ports, serial.Serial = self.old
        super().tearDown()

    def test_board_found_again_under_a_new_name(self):
        fh.board_ports = lambda: [('/dev/cu.usbmodem1201', 'USB JTAG/serial debug unit')]
        s = fh.open_port('/dev/cu.usbmodem1101', wait=3)
        self.assertIsNotNone(s, 'the board was not found under its new name')
        self.assertEqual(s.port, '/dev/cu.usbmodem1201')
        self.assertFalse(s.dtr or s.rts, 'DTR / RTS must be low (they reset the chip)')
        self.assertEqual(s.baudrate, int(fh.CFG['monitor_baud']))

    def test_gives_up_when_the_board_stays_away(self):
        fh.board_ports = lambda: []
        t0 = time.time()
        self.assertIsNone(fh.open_port('/dev/cu.usbmodem1101', wait=1))
        self.assertLess(time.time() - t0, 3)


class FakeBoard:
    """A port whose board answers the console's `ping` and `reboot` (with a boot log) when answers=True, or ignores
    them. `first`: what it prints before (the end of the boot esptool's reset started, from ~3 s on)."""
    BOOT = (b'I (227945) testcon: ok restarting\r\nI (227950) wifi:flush txq\r\nESP-ROM:esp32s3-20210327\r\n'
            b'rst:0xc (RTC_SW_CPU_RST),boot:0x2b (SPI_FAST_FLASH_BOOT)\r\n'
            b'I (39) boot.esp32s3: SPI Mode       : QIO\r\nI (2969) test: console ready\r\n')

    PONG = b'I (3105) test: pong 0.7.0\r\n'

    def __init__(self, answers, first=b''):
        self.answers, self.pending, self.written = answers, first, b''

    def write(self, data):
        self.written += data
        if self.answers and data == b'ping\n':
            self.pending += self.PONG
        if self.answers and data == b'reboot\n':
            self.pending += self.BOOT

    def read(self, n):
        out, self.pending = self.pending[:n], self.pending[n:]
        if not out:
            time.sleep(0.05)
        return out

    def close(self):
        pass


@unittest.skipIf(fh is None, 'pyserial missing (run from an ESP-IDF shell)')
class ConsoleReboot(TempDev):
    """reboot.request restarts through the test console, so the boot's first lines are kept (the flash mode, the reset
    reason: the harness printed "flash ?" after esptool's reset, L191), and falls back to esptool without a console."""

    def setUp(self):
        super().setUp()
        self.old = fh.open_port, fh.esptool
        self.esptool_calls = []

        def fake_esptool(args):
            self.esptool_calls.append(args)
            return 1, ['A fatal error occurred: no board (test)'], None
        fh.esptool = fake_esptool

    def tearDown(self):
        fh.open_port, fh.esptool = self.old
        super().tearDown()

    def test_boot_log_kept_from_its_first_line(self):
        board = FakeBoard(answers=True)
        fh.open_port = lambda *a, **k: board
        rc = fh.run_request('reboot', 1, port_opt='COM-test', echo=False)
        self.assertEqual(rc, 0)
        self.assertEqual(self.esptool_calls, [], 'esptool reset the board although the console did')
        self.assertEqual(board.written, b'reboot\n')
        log = self.read('serial_log.txt')
        for line in ('ESP-ROM:esp32s3', 'rst:0xc', 'SPI Mode       : QIO', 'console ready'):
            self.assertIn(line, log)
        self.assertIn('SPI Mode       : QIO', self.read('serial_live.txt'))
        self.assertIn(' resets=0 ', self.read('flash.done'), 'the restart asked for counted as an unexpected reset')
        self.assertEqual(self.read('flash.status').strip(), 'idle')

    def test_falls_back_to_esptool_without_a_console(self):
        fh.open_port = lambda *a, **k: FakeBoard(answers=False)
        t0 = time.time()
        rc = fh.run_request('reboot', 1, port_opt='COM-test', echo=False)
        self.assertEqual(len(self.esptool_calls), 1, 'no esptool reset after the console did not restart the board')
        self.assertIn('chip_id', self.esptool_calls[0])
        self.assertNotEqual(rc, 0)                     # (the fake esptool fails)
        self.assertLess(time.time() - t0, 8)


@unittest.skipIf(fh is None, 'pyserial missing (run from an ESP-IDF shell)')
class FlashRestart(TempDev):
    """After a flash the port is back ~3 s into the boot (esptool's reset re-enumerates the USB, L154): early lines
    (`ble: synced` at 2.5 s) were missing, so a test could not wait for them (esp32-s3-colormesh v0.1.0-rc.5). Once
    the test console answers, the helper restarts through it with the port open: the log is one whole boot (L191).
    Firmware without the console is logged as before."""
    FIRST = b'I (3050) ble: scanning\r\nI (3080) app: the end of the boot esptool started\r\n'

    def setUp(self):
        super().setUp()
        self.old = fh.open_port, fh.esptool, fh.flash_args, fh.SETTLE_S, fh.CONSOLE_ASK_S
        fh.SETTLE_S, fh.CONSOLE_ASK_S = 0, 1.5
        self.esptool_calls = []

        def fake_esptool(args):
            self.esptool_calls.append(args)
            return 0, ['Serial port COM-test', 'Hard resetting via RTS pin...'], 'COM-test'
        fh.esptool = fake_esptool
        fh.flash_args = lambda port: (['--port', port, 'write_flash', '0x10000', 'app.bin'], None)

    def tearDown(self):
        fh.open_port, fh.esptool, fh.flash_args, fh.SETTLE_S, fh.CONSOLE_ASK_S = self.old
        super().tearDown()

    def flash(self, board):
        fh.open_port = lambda *a, **k: board
        t0 = time.time()
        rc = fh.run_request('flash', 1, port_opt='COM-test', echo=False)
        self.assertEqual(rc, 0)
        self.assertLess(time.time() - t0, 8)
        self.assertEqual(len(self.esptool_calls), 1, 'esptool ran more than the flash')
        self.assertTrue(self.read('flash.done').startswith('exit=0 port=COM-test '))
        self.assertEqual(self.read('flash.status').strip(), 'idle')
        return self.read('serial_log.txt')

    def test_the_boot_log_is_whole_after_a_flash(self):
        board = FakeBoard(answers=True, first=self.FIRST)
        log = self.flash(board)
        self.assertTrue(board.written.startswith(b'ping\n'), board.written)
        self.assertTrue(board.written.endswith(b'reboot\n'), 'no restart through the console after the flash')
        for line in ('ESP-ROM:esp32s3', 'rst:0xc', 'SPI Mode       : QIO', 'console ready'):
            self.assertIn(line, log)
        first = [i for i, line in enumerate(log.splitlines()) if line.startswith('ESP-ROM:')]
        self.assertEqual(first, [1], 'the log does not start with the boot (the "ok restarting" line, then ESP-ROM)')
        self.assertNotIn('ble: scanning', log, 'the tail of the first boot kept: two boots in one log')
        self.assertIn(' resets=0 ', self.read('flash.done'), 'the restart asked for counted as an unexpected reset')

    def test_firmware_without_the_console_is_logged_as_before(self):
        board = FakeBoard(answers=False, first=self.FIRST)
        log = self.flash(board)
        self.assertNotIn(b'reboot', board.written, 'reboot sent although the console never answered')
        self.assertIn('ble: scanning', log, 'the boot esptool started was not logged')
        self.assertIn(' resets=0 ', self.read('flash.done'))

    def test_turned_off_in_forge_json(self):
        fh.CFG['flash_restart'] = False
        board = FakeBoard(answers=True, first=self.FIRST)
        log = self.flash(board)
        self.assertEqual(board.written, b'', 'the console was asked although forge.json says flash_restart false')
        self.assertIn('ble: scanning', log)


# monitor.ps1 against a fake port: a function named New-Object shadows the cmdlet for SerialPort only (functions win
# over cmdlets), Start-Sleep is a no-op (the 2 s settle). $env:FAKE_ANSWERS=1: the board answers ping and reboot.
PS_RUNNER = r'''param($Monitor)
$global:FakeQ = New-Object System.Collections.Generic.Queue[string]
$global:FakeQ.Enqueue("I (3050) ble: scanning")
$global:Fake = [pscustomobject]@{ DtrEnable = $true; RtsEnable = $true; ReadTimeout = 0; IsOpen = $false; Written = "" }
$global:Fake | Add-Member ScriptMethod Open { $this.IsOpen = $true }
$global:Fake | Add-Member ScriptMethod Close { $this.IsOpen = $false }
$global:Fake | Add-Member ScriptMethod Write {
  param($s)
  $this.Written += $s
  if ($env:FAKE_ANSWERS -ne "1") { return }
  if ($s -eq "ping`n") { $global:FakeQ.Enqueue("I (3105) test: pong 0.7.0") }
  if ($s -eq "reboot`n") {
    foreach ($l in "I (227945) test: ok restarting", "I (227950) wifi:flush txq", "ESP-ROM:esp32s3-20210327",
                   "rst:0xc (RTC_SW_CPU_RST),boot:0x2b (SPI_FAST_FLASH_BOOT)",
                   "I (39) boot.esp32s3: SPI Mode       : QIO", "I (2969) test: console ready") { $global:FakeQ.Enqueue($l) }
  }
}
$global:Fake | Add-Member ScriptMethod ReadLine {
  if ($global:FakeQ.Count) { return $global:FakeQ.Dequeue() }
  [System.Threading.Thread]::Sleep(50); throw [System.TimeoutException]::new("no line")
}
function New-Object {
  param([Parameter(Position = 0)][string]$TypeName, [Parameter(Position = 1)][object[]]$ArgumentList)
  if ($TypeName -eq "System.IO.Ports.SerialPort") { return $global:Fake }
  if ($ArgumentList) { Microsoft.PowerShell.Utility\New-Object -TypeName $TypeName -ArgumentList $ArgumentList }
  else { Microsoft.PowerShell.Utility\New-Object -TypeName $TypeName }
}
function Start-Sleep {}
& $Monitor -Port COM-test -Seconds 1 -AfterFlash -AskSeconds 2
$code = $LASTEXITCODE
Set-Content -Path (Join-Path (Split-Path $Monitor) "written.txt") -Value $global:Fake.Written -NoNewline
exit $code
'''
POWERSHELL = shutil.which('powershell') if os.name == 'nt' else None


@unittest.skipIf(POWERSHELL is None, 'Windows PowerShell only')
class MonitorPs1AfterFlash(unittest.TestCase):
    """The PC's helper, monitor.ps1 -AfterFlash (the twin of restart_after_flash()): a copy in a temporary root, so
    the real .devloop is not touched."""

    def setUp(self):
        self.root = tempfile.mkdtemp()
        self.dir = os.path.join(self.root, 'tools', 'devloop')
        os.makedirs(self.dir)
        shutil.copy(os.path.join(HERE, '..', 'devloop', 'monitor.ps1'), self.dir)
        with open(os.path.join(self.root, 'forge.json'), 'w') as f:
            f.write('{}')
        with open(os.path.join(self.dir, 'runner.ps1'), 'w') as f:
            f.write(PS_RUNNER)

    def tearDown(self):
        shutil.rmtree(self.root, ignore_errors=True)

    def run_monitor(self, answers):
        r = subprocess.run([POWERSHELL, '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
                            os.path.join(self.dir, 'runner.ps1'), os.path.join(self.dir, 'monitor.ps1')],
                           capture_output=True, text=True, errors='replace', timeout=60,
                           env={**os.environ, 'FAKE_ANSWERS': '1' if answers else '0'})
        dev = os.path.join(self.root, '.devloop')

        def read(name):
            with open(os.path.join(dev, name), encoding='utf-8-sig', errors='replace') as f:
                return f.read()
        with open(os.path.join(self.dir, 'written.txt'), encoding='utf-8-sig', errors='replace') as f:
            written = f.read()
        return r.returncode, read('serial_log.txt').splitlines(), written, read('flash.status').strip(), r

    def test_the_boot_log_is_whole_after_a_flash(self):
        code, log, written, status, r = self.run_monitor(answers=True)
        self.assertEqual(code, 10, f'not "restarted" (10): {r.stdout[-800:]} {r.stderr[-800:]}')
        self.assertTrue(written.startswith('ping\n') and written.endswith('reboot\n'), repr(written))
        self.assertEqual(log[1], 'ESP-ROM:esp32s3-20210327', log)
        self.assertIn('I (39) boot.esp32s3: SPI Mode       : QIO', log)
        self.assertNotIn('I (3050) ble: scanning', log, 'the tail of the first boot kept: two boots in one log')
        self.assertEqual(status, 'logging')

    def test_firmware_without_the_console_is_logged_as_before(self):
        code, log, written, status, r = self.run_monitor(answers=False)
        self.assertEqual(code, 0, f'{r.stdout[-800:]} {r.stderr[-800:]}')
        self.assertNotIn('reboot', written)
        self.assertIn('I (3050) ble: scanning', log)
        self.assertEqual(status, 'logging')


@unittest.skipIf(fh is None, 'pyserial missing (run from an ESP-IDF shell)')
class Flash(TempDev):
    """What a flash.request writes: the parts in flasher_args.json, from the staged copies, md5-checked (L1)."""
    FILES = {'0x0': 'bootloader/bootloader.bin', '0x8000': 'partition_table/partition-table.bin',
             '0x10000': 'espforge.bin', '0xd000': 'ota_data_initial.bin'}

    def setUp(self):
        super().setUp()
        self.build = os.path.join(self.root, 'build', 'v55')
        for rel in self.FILES.values():
            os.makedirs(os.path.dirname(os.path.join(self.build, rel)), exist_ok=True)
            with open(os.path.join(self.build, rel), 'wb') as f:
                f.write(os.urandom(256))
        with open(os.path.join(self.build, 'flasher_args.json'), 'w') as f:
            json.dump({'flash_settings': {'flash_mode': 'dio', 'flash_size': '16MB', 'flash_freq': '80m'},
                       'flash_files': self.FILES,
                       'extra_esptool_args': {'after': 'hard_reset', 'before': 'default_reset', 'stub': True,
                                              'chip': 'esp32s3'}}, f)
        self.old = fh.esptool, fh.monitor, fh.open_port, fh.SETTLE_S

    def tearDown(self):
        fh.esptool, fh.monitor, fh.open_port, fh.SETTLE_S = self.old
        super().tearDown()

    def test_args_name_the_staged_copies_in_offset_order(self):
        man = stage.stage(fh.CFG)
        args, err = fh.flash_args('/dev/cu.usbmodem1101')
        self.assertIsNone(err)
        self.assertEqual(args[:2], ['--port', '/dev/cu.usbmodem1101'])
        for flag, want in (('--chip', 'esp32s3'), ('-b', str(fh.CFG['baud'])), ('--flash_mode', 'dio'),
                           ('--flash_size', '16MB'), ('--after', 'hard_reset')):
            self.assertEqual(args[args.index(flag) + 1], want, flag)
        files = args[args.index('write_flash') + 7:]
        self.assertEqual(files[0::2], ['0x0', '0x8000', '0xd000', '0x10000'])
        staged = {x['offset']: os.path.join(fh.DEV, 'stage', x['file']) for x in man['parts']}
        self.assertEqual(files[1::2], [staged[o] for o in files[0::2]], 'not the staged copies')
        for f in files[1::2]:
            self.assertNotIn(self.build, f, 'a part straight from the build folder (L1)')

    def test_nothing_staged_is_refused(self):
        rc = fh.run_request('flash', 1, port_opt='/dev/null-no-board', echo=False)
        self.assertEqual(rc, 3)
        self.assertEqual(self.read('flash.status').strip(), 'flash_failed')
        self.assertTrue(self.read('flash.done').startswith('exit=3 '))
        self.assertIn(' stage=verify ', self.read('flash.done'))
        self.assertIn('stage.py', self.read('flash_log.txt'))

    def test_a_staged_copy_that_differs_is_refused(self):
        man = stage.stage(fh.CFG)
        with open(os.path.join(fh.DEV, 'stage', man['parts'][2]['file']), 'ab') as f:
            f.write(b'\0')
        args, err = fh.flash_args(None)
        self.assertIsNone(args)
        self.assertIn('md5', err)

    def test_another_build_folder_staged_is_flashed(self):
        # stage.py --build build/forge (tools/forge_local.py's folder): checked against forge.json's build_dir, which
        # holds other files, the stage was refused (2026-10-09)
        other = os.path.join(self.root, 'build', 'forge')
        shutil.copytree(self.build, other)
        with open(os.path.join(other, 'espforge.bin'), 'wb') as f:
            f.write(os.urandom(256))
        man = stage.stage(fh.CFG, build=other)
        args, err = fh.flash_args(None)
        self.assertIsNone(err)
        app = next(x for x in man['parts'] if x['offset'] == '0x10000')
        self.assertIn(os.path.join(fh.DEV, 'stage', app['file']), args)
        with open(os.path.join(other, 'espforge.bin'), 'wb') as f:   # that folder rebuilt: refused
            f.write(os.urandom(256))
        args, err = fh.flash_args(None)
        self.assertIn('changed since it was staged', err)

    def test_a_build_newer_than_the_stage_is_refused(self):
        stage.stage(fh.CFG)
        with open(os.path.join(self.build, 'espforge.bin'), 'wb') as f:
            f.write(os.urandom(256))
        args, err = fh.flash_args(None)
        self.assertIsNone(args)
        self.assertIn('changed since it was staged', err)

    def test_a_flash_forgets_the_cached_ip_and_key(self):
        stage.stage(fh.CFG)
        for name in ('ip', 'key'):
            with open(self.path(name), 'w') as f:
                f.write('old')
        calls = []
        fh.esptool = lambda args: (calls.append(args), (0, ['Serial port COM-test'], 'COM-test'))[1]
        fh.monitor = lambda port, secs, **k: ([], False, None)
        fh.open_port, fh.SETTLE_S = (lambda *a, **k: None), 0     # (no board for the restart after the flash)
        self.assertEqual(fh.run_request('flash', 1, echo=False), 0)
        self.assertEqual(len(calls), 1)
        self.assertIn('write_flash', calls[0])
        self.assertFalse(os.path.exists(self.path('ip')) or os.path.exists(self.path('key')),
                         'ip / key of the old firmware kept after a flash (L7)')
        self.assertTrue(self.read('flash.done').startswith('exit=0 port=COM-test '))


@unittest.skipIf(fh is None, 'pyserial missing (run from an ESP-IDF shell)')
class Requests(TempDev):
    def test_seconds_in_request(self):
        for text, want in (('300', 300), (' 45\n', 45), ('', 60), ('abc', 60), ('999999', 60)):
            with open(self.path('flash.request'), 'w') as f:
                f.write(text)
            self.assertEqual(fh.seconds_in('flash.request'), want, repr(text))

    def test_port_order(self):
        old = fh.board_ports, os.environ.pop('FORGE_PORT', None)
        try:
            fh.board_ports = lambda: [('/dev/cu.usbmodem1101', 'x')]
            self.assertEqual(fh.find_port(), '/dev/cu.usbmodem1101')
            fh.CFG['port'] = 'COM7'
            self.assertEqual(fh.find_port(), 'COM7')
            os.environ['FORGE_PORT'] = 'COM9'
            self.assertEqual(fh.find_port(), 'COM9')
            self.assertEqual(fh.find_port('COM3'), 'COM3')
        finally:
            fh.board_ports = old[0]
            os.environ.pop('FORGE_PORT', None)
            if old[1] is not None:
                os.environ['FORGE_PORT'] = old[1]


if __name__ == '__main__':
    unittest.main()


@unittest.skipIf(fh is None, 'pyserial missing (run from an ESP-IDF shell)')
class PortPick(unittest.TestCase):
    """Several boards on the PC (an ESP32-P4 beside the S3): the helper asks each chip which it is, once."""

    def test_one_port_needs_no_probe(self):
        self.assertEqual(fh.pick_port([('COM5', 'x')], 'esp32s3', lambda d, c: self.fail('probed')), 'COM5')

    def test_the_cached_port_wins_while_connected(self):
        ports = [('COM5', ''), ('COM7', '')]
        self.assertEqual(fh.pick_port(ports, 'esp32s3', lambda d, c: self.fail('probed'), cached='COM7'), 'COM7')
        asked = []
        self.assertEqual(fh.pick_port(ports, 'esp32s3', lambda d, c: asked.append(d) or d == 'COM5', cached='COM9'), 'COM5')
        self.assertEqual(asked, ['COM5'])                 # a cached port that is gone: asked again

    def test_two_boards_the_chip_that_answers(self):
        ports = [('COM5', 'S3'), ('COM7', 'P4')]
        probe = lambda d, c: (d, c) == ('COM7', 'esp32p4')
        self.assertEqual(fh.pick_port(ports, 'esp32p4', probe), 'COM7')
        self.assertIsNone(fh.pick_port(ports, 'esp32c3', probe))
        self.assertIsNone(fh.pick_port([], 'esp32s3', probe))
