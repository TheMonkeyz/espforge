"""Unit tests of the harness's own logic (no board):   python tools/harness/test_harness.py
(or python -m unittest discover -s tools/harness -p "test_*.py")

The board is faked: FakeLog (lines a test appends to), FakeCtx, a Board whose send() is stubbed (and whose .devloop/
is a temp folder, so nothing reaches a flash helper that may be running), FakeClock for the tests that wait."""
import base64
import contextlib
import io
import json
import os
import re
import shutil
import struct
import sys
import time
import tempfile
import unittest
import zlib
from unittest import mock

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, '..'))
import forgecfg  # noqa: E402
import board  # noqa: E402
from board import CFG, SUITES, Board, english_netsh, kv, Fail  # noqa: E402
import core_suites  # noqa: E402
import app_suites  # noqa: E402
import harness  # noqa: E402
import shot  # noqa: E402  (tools/devloop/shot.py, on the path through board.py)
from harness import BAD, BASELINE, ORDER, compare, parse_args, propose, suite_of  # noqa: E402

BASE = {
    '_comment': 'not a metric',
    'swipe_fps.hello_to_system': {'min': 20, 'ref': 45, 'note': 'kept'},
    'internal_min_kb': {'min': 8, 'ref': 9},
    'snapshot_ms.hello': {'max': 5000, 'ref': 2300},
    'swipe_gap_max_ms.hello_to_system': {'max': 200, 'ref': 60, 'dio': {'max': 260}},
}


def load_json(path):
    with open(path, encoding='utf-8') as f:
        return json.load(f)


def write(path, data):
    with open(path, 'wb' if isinstance(data, bytes) else 'w') as f:
        f.write(data if isinstance(data, (bytes, str)) else json.dumps(data))


def verdicts(rows):
    return {k: v for k, _, _, v in rows}


def snapshot_screens(base):
    """The screens the baseline has snapshot times for; None for a new project's empty baseline."""
    if not base:
        return None
    return {k.split('.', 1)[1] for k in base if k.startswith('snapshot_ms.')}


class Compare(unittest.TestCase):
    def test_within_and_outside_limits(self):
        v = verdicts(compare({'swipe_fps.hello_to_system': 30, 'swipe_gap_max_ms.hello_to_system': 250,
                              'internal_min_kb': 7}, BASE, ['perf', 'memory']))
        self.assertEqual(v['swipe_fps.hello_to_system'], 'ok')
        self.assertEqual(v['swipe_gap_max_ms.hello_to_system'], 'REGRESSION')
        self.assertEqual(v['internal_min_kb'], 'REGRESSION')

    def test_dio_limits(self):
        v = verdicts(compare({'swipe_fps.hello_to_system': 30, 'swipe_gap_max_ms.hello_to_system': 250}, BASE,
                             ['perf'], mode='DIO'))
        self.assertEqual(v['swipe_gap_max_ms.hello_to_system'], 'ok')

    def test_missing_metric_fails(self):
        # in the baseline, its suite ran, not measured: a regex drift or a reused log window, not a pass
        v = verdicts(compare({'swipe_fps.hello_to_system': 40}, BASE, ['perf', 'memory']))
        self.assertEqual(v['internal_min_kb'], 'MISSING')
        self.assertIn('MISSING', BAD)

    def test_missing_from_a_suite_that_did_not_run(self):
        v = verdicts(compare({'internal_min_kb': 9}, BASE, ['memory']))
        self.assertNotIn('snapshot_ms.hello', v)              # screens didn't run
        self.assertNotIn('_comment', v)
        v = verdicts(compare({}, BASE, ['screens']))
        self.assertEqual(v, {'snapshot_ms.hello': 'MISSING'})

    def test_metric_without_a_limit_fails(self):
        v = verdicts(compare({'internal_min_kb': 9, 'swipe_fps.new': 20}, BASE, ['memory']))
        self.assertEqual(v['swipe_fps.new'], 'NEW')
        self.assertIn('NEW', BAD)

    def test_explicit_skip(self):
        v = verdicts(compare({'swipe_fps.hello_to_system': 40, 'swipe_gap_max_ms.hello_to_system': 20}, BASE,
                             ['perf', 'memory'], {'internal_*': 'not today'}))
        self.assertEqual(v['internal_min_kb'], 'skipped: not today')
        self.assertNotIn(v['internal_min_kb'], BAD)

    def test_propose_keeps_limits_and_notes(self):
        out = propose({'swipe_fps.hello_to_system': 47, 'page_kb': 53, 'swipe_fps.x': 60, 'idle_internal_drop_kb': 2},
                      BASE)
        self.assertEqual(out['swipe_fps.hello_to_system'], {'min': 20, 'ref': 47, 'note': 'kept'})
        self.assertIn('max', out['page_kb'])                  # bigger is worse, though it ends in _kb
        self.assertIn('max', out['idle_internal_drop_kb'])
        self.assertIn('min', out['swipe_fps.x'])
        self.assertEqual(BASE['swipe_fps.hello_to_system']['ref'], 45)  # the input is not changed

    def test_propose_dio(self):
        out = propose({'swipe_gap_max_ms.hello_to_system': 90}, BASE, mode='DIO')
        self.assertEqual(out['swipe_gap_max_ms.hello_to_system']['dio']['ref'], 90)
        self.assertEqual(out['swipe_gap_max_ms.hello_to_system']['ref'], 60)


class Baseline(unittest.TestCase):
    def test_every_baseline_metric_has_a_suite(self):
        # a metric no suite claims would never be MISSING: it could disappear unnoticed
        base = load_json(BASELINE)
        for k in base:
            if not k.startswith('_'):
                self.assertIn(suite_of(k), SUITES, f'{k}: no suite produces it (METRIC_SUITES)')

    def test_every_suite_is_ordered(self):
        for s in SUITES:
            self.assertIn(s, ORDER)

    def test_snapshot_metrics_follow_forge_screens(self):
        snaps = snapshot_screens(load_json(BASELINE))
        if snaps is None:
            self.skipTest('baseline.json is {} (a new project): the first harness run writes it (docs/NEW-PROJECT.md)')
        self.assertEqual(snaps, set(CFG['screens']))

    def test_a_new_projects_empty_baseline_is_skipped(self):
        # NEW-PROJECT resets the baseline to {}: CI failed on it until the first harness run (esp32-s3-rtcquebec)
        self.assertIsNone(snapshot_screens({}))
        self.assertEqual(snapshot_screens({'snapshot_ms.home': 90, 'fps.x': 1}), {'home'})


class Args(unittest.TestCase):
    def test_default_runs_all_but_ota(self):
        o = parse_args([])
        self.assertNotIn('ota', o.run)
        self.assertIn('boot', o.run)

    def test_quick_leaves_out_the_slow_suites(self):
        o = parse_args(['--quick'])
        self.assertNotIn('idle_stable', o.run)
        self.assertNotIn('wifi_setup', o.run)
        self.assertIn('web', o.run)                          # the web suite's device tests stay; Playwright doesn't
        self.assertEqual(parse_args(['--quick', 'idle_stable']).run, ['idle_stable'])   # asked for: run

    def test_ota_flag_adds_the_suite_and_expect(self):
        o = parse_args(['--ota', 'v1.2.0-rc.1'])
        self.assertIn('ota', o.run)
        self.assertEqual(o.expect, 'v1.2.0-rc.1')

    def test_suite_option_and_flash_default(self):
        o = parse_args(['--suite', 'boot,console', '--flash'])
        self.assertEqual(o.run, ['boot', 'console'])
        self.assertEqual(o.flash, CFG['build_dir'])
        o = parse_args(['--flash', 'boot'])                  # a suite name after --flash is a suite
        self.assertEqual((o.run, o.flash), (['boot'], CFG['build_dir']))

    def test_wifi_setup_only_by_default_on_windows(self):
        # netsh, and a PC that stays online over Ethernet while its Wi-Fi is on the setup network (docs/MACOS.md)
        old = harness.WINDOWS
        try:
            harness.WINDOWS = True
            self.assertIn('wifi_setup', parse_args([]).run)
            harness.WINDOWS = False
            self.assertNotIn('wifi_setup', parse_args([]).run)
            self.assertIn('wifi_runtime', parse_args([]).run)
            self.assertEqual(parse_args(['wifi_setup']).run, ['wifi_setup'])   # still there when asked for
        finally:
            harness.WINDOWS = old


class LogWaits(unittest.TestCase):
    def test_ready_line_found_after_start_logs_wait(self):
        # "console ready" lost in the first 2.5 s: start_log's wait stops on the ready line itself and moves the read
        # position past it; harness.py's own wait for that line must still find it (start=0), not fail 90 s later
        import board
        log = board.Log()
        lines = ['I (4895) diag: mark app ready      internal 126 KB free', 'I (4952) ota: Running v0.1.1 from ota_0']
        log.lines = lambda: lines
        log.wait(r'test: console ready|ota: Running|' + board.CFG['ready_line'], 1)
        self.assertEqual(log.pos, 1)
        self.assertTrue(log.wait(board.CFG['ready_line'], 1, start=0))
        with self.assertRaises(board.Fail):
            log.wait(board.CFG['ready_line'], 0.3)


class Helpers(unittest.TestCase):
    def test_kv(self):
        self.assertEqual(kv('connected=1 sta_ssid=Home ap=0 ap_pass=abcd1234'),
                         {'connected': '1', 'sta_ssid': 'Home', 'ap': '0', 'ap_pass': 'abcd1234'})

    def test_netsh_language(self):
        self.assertTrue(english_netsh('    Name                   : Wi-Fi\n    State                  : connected\n'))
        self.assertFalse(english_netsh('There is no wireless interface on the system.'))
        with self.assertRaises(Fail):
            english_netsh('    Nom   : Wi-Fi\n    Description : x\n    GUID : y\n    État : connecté\n')


    def test_wait_online_returns_once_connected(self):
        # perf.page_swipes ran while Wi-Fi rejoined after the Easy Connect page (46 fps once): measure() waits
        import board
        answers = iter(['connected=0 retries=1', 'connected=0 retries=2', 'connected=1 sta_ssid=Home'])
        fake = type('B', (), {'wifi': lambda self: kv(next(answers))})()
        self.assertGreaterEqual(board.Board.wait_online(fake, timeout=5, step=0), 0)
        never = type('B', (), {'wifi': lambda self: {'connected': '0'}})()
        with self.assertRaises(Fail):
            board.Board.wait_online(never, timeout=0.05, step=0)

    def test_measurements_dodge_the_first_minute_update_check(self):
        # perf.page_swipes landed ~60 s after boot in two runs: forge_ota's first check ran inside the measurement
        import app_suites
        self.assertEqual(app_suites.first_minute_wait_s(30), 0)
        self.assertEqual(app_suites.first_minute_wait_s(55), 16)
        self.assertEqual(app_suites.first_minute_wait_s(64), 7)
        self.assertEqual(app_suites.first_minute_wait_s(70), 1)
        self.assertEqual(app_suites.first_minute_wait_s(71), 0)
        self.assertEqual(app_suites.first_minute_wait_s(300), 0)

    def test_heap_numbers_settle_after_an_ota_install(self):
        # memory.heap_numbers in an --ota run read 35 KB largest right after the install, 46 at rest
        import core_suites
        readings = iter([{'internal': 110, 'largest': 35}, {'internal': 116, 'largest': 41},
                         {'internal': 121, 'largest': 46}, {'internal': 121, 'largest': 46}])
        h = core_suites.heap_settled(lambda: next(readings), settle=True, step=0)
        self.assertEqual(h['largest'], 46)                    # the third reading, confirmed by the fourth
        first = core_suites.heap_settled(lambda: {'internal': 1, 'largest': 2}, settle=False, step=0)
        self.assertEqual(first['largest'], 2)


# ---------------------------------------------------------------- fakes

class FakeLog:
    """A log whose lines a test appends to (what the monitor would write)."""

    def __init__(self, lines=None):
        self.data = list(lines or [])
        self.pos = self.mark_at = 0

    def lines(self):
        return list(self.data)

    wait = board.Log.wait
    count = board.Log.count
    mark = board.Log.mark
    since_mark = board.Log.since_mark


class FakeCtx:
    def __init__(self, brd, log, outdir, opts=None):
        self.board, self.log, self.dir = brd, log, outdir
        self.opts = opts or type('O', (), {'ota': None, 'phone': False})()
        self.metrics, self.notes, self.snapped, self.skips = {}, [], set(), {}
        self.reset_ok, self.version = False, ''

    def out(self, name):
        return os.path.join(self.dir, name)

    def metric(self, name, value):
        self.metrics[name] = value

    def skip(self, pattern, reason):
        self.skips[pattern] = reason

    def note(self, text):
        self.notes.append(text)


class FakeClock:
    """Stands in for core_suites' `time` module: sleep() moves the clock on at once."""

    def __init__(self):
        self.t, self.slept = 1000.0, []

    def time(self):
        return self.t

    def sleep(self, s):
        self.slept.append(s)
        self.t += s


class FakeBoard:
    """Answers console commands from a table (a value may be a function of nothing): cmd(line, expect) -> the match
    of `expect` on the reply; a command not in the table is refused as the firmware does."""

    def __init__(self, replies, no_net=False):
        self.replies, self.no_net, self.sent = replies, no_net, []

    def cmd(self, line, expect=r'test: (ok|pong|screen|heap|wifi|fps|key|where|commands)', timeout=15, **kw):
        self.sent.append(line)
        if line not in self.replies:
            raise Fail(f"{line}: test: error unknown command '{line.split()[0]}' (help lists them)")
        reply = self.replies[line]
        reply = reply() if callable(reply) else reply
        m = re.search(expect, reply)
        if not m:
            raise Fail(f'{line}: {reply} does not match {expect}')
        return m

    def version(self):
        return self.cmd('ping', r'test: pong (\S+)').group(1)

    def heap(self):
        return {k: int(v) for k, v in kv(self.cmd('heap', r'test: heap (.*)').group(1)).items()}

    def show(self, name):
        self.sent.append(f'screen {name}')

    def wait_screen(self, name, timeout, step=1.0):
        pass


class StubbedBoard(unittest.TestCase):
    """A real Board whose send() appends the echo and the answers a test scripts to a FakeLog; .devloop/ is a temp
    folder, so even the old code's own send() reaches no flash helper."""

    def setUp(self):
        self.tmp = tempfile.mkdtemp()
        self.dev = mock.patch.object(board, 'DEV', self.tmp)
        self.dev.start()
        self.b = Board('', FakeLog(['I (1) diag: mark app ready']), no_net=True)
        self.sent, self.answers = [], {}

    def tearDown(self):
        self.dev.stop()
        shutil.rmtree(self.tmp, ignore_errors=True)

    def script(self, line, *answers):
        """Each time `line` is sent, the next list of answer lines (the last one repeats)."""
        self.answers[line] = list(answers)
        def send(sent):
            at = len(self.b.log.data)
            self.sent.append(sent)
            queue = self.answers.get(sent, [[]])
            self.b.log.data += ['> ' + sent] + (queue.pop(0) if len(queue) > 1 else queue[0])
            return at
        self.b.send = send


# ---------------------------------------------------------------- no-net mode (forge.json "no_net" / --no-net)

@contextlib.contextmanager
def net_mode(no_net):
    """harness.NO_NET as a project with / without a network has it (forge.json no_net), put back after."""
    old = harness.NO_NET
    harness.NO_NET = no_net
    try:
        yield
    finally:
        harness.NO_NET = old


class NoNetArgs(unittest.TestCase):
    def test_forge_json_sets_it(self):
        # board.NO_NET is forge.json's "no_net" (absent: a project on a network), harness.py follows it
        self.assertEqual(board.NO_NET, bool(CFG.get('no_net')))
        self.assertIs(harness.NO_NET, board.NO_NET)
        with net_mode(True):
            self.assertTrue(parse_args([]).no_net)
        with net_mode(False):
            self.assertFalse(parse_args([]).no_net)
            self.assertIn('web', parse_args([]).run)

    def test_the_network_suites_never_run(self):
        with net_mode(True):
            o = parse_args([])
        self.assertFalse(harness.NET_SUITES & set(o.run), o.run)
        self.assertEqual([s for s in o.run if s in ('boot', 'console', 'memory', 'screens', 'display', 'idle_stable')],
                         ['boot', 'console', 'memory', 'screens', 'display', 'idle_stable'])
        self.assertEqual(harness.NET_SUITES, {'web', 'update', 'wifi_runtime', 'wifi_setup', 'ota'})
        with net_mode(True):
            self.assertNotIn('idle_stable', parse_args(['--quick']).run)

    def test_a_network_suite_asked_for_is_refused(self):
        with net_mode(True), contextlib.redirect_stderr(io.StringIO()) as err:
            for argv in (['web'], ['--suite', 'boot,update'], ['wifi_setup']):
                with self.assertRaises(SystemExit, msg=argv):
                    parse_args(argv)
        self.assertIn('needs the network', err.getvalue())

    def test_ip_and_ota_are_refused(self):
        with net_mode(True), contextlib.redirect_stderr(io.StringIO()):
            with self.assertRaises(SystemExit):
                parse_args(['--ip', '192.168.1.5'])
            with self.assertRaises(SystemExit):
                parse_args(['--ota', 'v0.1.0-rc.1'])

    def test_the_flag_on_a_network_project(self):
        with net_mode(False):
            o = parse_args(['--no-net'])
            self.assertTrue(o.no_net)
            self.assertFalse(harness.NET_SUITES & set(o.run))
            self.assertEqual(parse_args(['--no-net', 'boot', 'console']).run, ['boot', 'console'])

    def test_board_without_network_refuses_https(self):
        b = type('B', (), {'no_net': True})()
        for call in (lambda: Board.api(b, '/api/info'), lambda: Board.key(b), lambda: Board.request(b, 'GET', '/'),
                     lambda: Board.install(b, 'v1')):
            with self.assertRaises(Fail) as e:
                call()
            self.assertIn('no network', str(e.exception))
        self.assertEqual(Board.wait_online(b), 0)            # nothing to wait for: perf's measurements still run
        self.assertIsNone(Board.version_before_restart(b))   # no API to ask before a restart
        # a no-net firmware has no forge_ota: diag's slot line says the firmware is up after a reset
        self.assertRegex('I (4952) diag: running from ota_0, 1536 KB at 0x20000', board.BOOT_SEEN)
        self.assertRegex('I (4952) ota: Running v0.1.1 from ota_0', board.BOOT_SEEN)


BOOT = ['ESP-ROM:esp32s3-20210327', 'rst:0x15 (USB_UART_CHIP_RESET),boot:0x8 (SPI_FAST_FLASH_BOOT)',
        'I (420) diag: firmware v0.7.0-rc.0 (built Nov  1 2026 10:00:00)',
        'I (421) diag: running from ota_1, 1536 KB at 0x190000',
        'I (422) diag: boot reset=USB_UART_CHIP_RESET flash_mb=16 psram_kb=8192',
        'I (900) diag: mark board          internal 170 KB free (largest 120), DMA 150 KB, PSRAM 8000 KB',
        'I (2600) test: console ready on USB (send \'help\')',
        'I (4895) diag: mark app ready      internal 120 KB free (largest 60), DMA 100 KB, PSRAM 7000 KB']
OTA_LINE = 'I (4952) ota: Running v0.7.0-rc.0 from ota_1, channel beta'


class BootCheck(unittest.TestCase):
    def test_boot_image_lines(self):
        self.assertEqual(core_suites.boot_image(BOOT, no_net=True), ('v0.7.0-rc.0', 'ota_1', None))
        self.assertIsNone(core_suites.boot_image(BOOT, no_net=False))      # a network firmware needs forge_ota's line
        self.assertEqual(core_suites.boot_image([OTA_LINE], no_net=False), ('v0.7.0-rc.0', 'ota_1', 'beta'))
        self.assertIsNone(core_suites.boot_image([OTA_LINE], no_net=True))
        self.assertIsNone(core_suites.boot_image([l for l in BOOT if 'running from' not in l], no_net=True))

    def run_start_up(self, lines, version='v0.7.0-rc.0', no_net=True):
        ctx = FakeCtx(FakeBoard({'ping': f'I (5000) test: pong {version}'}, no_net=no_net), FakeLog(lines),
                      tempfile.gettempdir())
        core_suites.start_up(ctx)
        return ctx

    def test_start_up_accepts_the_diag_lines(self):
        ctx = self.run_start_up(BOOT)
        self.assertEqual(ctx.metrics['boot_s.ready'], 4.9)
        self.assertTrue(any('v0.7.0-rc.0 from ota_1;' in n for n in ctx.notes), ctx.notes)

    def test_start_up_wants_the_console_version_and_both_lines(self):
        with self.assertRaises(Fail) as e:
            self.run_start_up(BOOT, version='v0.6.9')
        self.assertIn('v0.6.9', str(e.exception))
        with self.assertRaises(Fail) as e:
            self.run_start_up([l for l in BOOT if 'diag: firmware' not in l])
        self.assertIn('diag: firmware', str(e.exception))

    def test_a_network_firmware_still_needs_the_ota_line(self):
        with self.assertRaises(Fail) as e:
            self.run_start_up(BOOT, no_net=False)
        self.assertIn('ota: Running', str(e.exception))
        ctx = self.run_start_up(BOOT + [OTA_LINE], no_net=False)
        self.assertTrue(any('from ota_1, channel beta' in n for n in ctx.notes), ctx.notes)


HELP = ('test: commands: ping, help, heap, where, memspeed, reboot, fps [reset], tap X Y, press X Y [ms], '
        'swipe left|right|up|down [more...], drag X1 Y1 X2 Y2 [ms], screen [name], shot [SCREEN [DIV]]')
REPLIES = {'ping': 'I (5000) test: pong v0.7.0-rc.0',
           'heap': 'I (5002) test: heap internal=120 min=110 largest=64 psram=7000 psram_min=6900 uptime_s=5 '
                   'failed_allocs=0 lvgl_fallbacks=0',
           'where': 'I (5003) test: where task_lvgl=Blocked'}


class ConsoleExpectation(unittest.TestCase):
    def test_wanted_commands(self):
        self.assertEqual(core_suites.wanted_commands(no_net=True), ('ping', 'help', 'heap', 'where', 'reboot'))
        self.assertEqual(core_suites.unwanted_commands(no_net=True), ('key', 'wifi'))
        self.assertIn('wifi', core_suites.wanted_commands(no_net=False))
        self.assertEqual(core_suites.unwanted_commands(no_net=False), ())

    def run_commands(self, help_line, no_net=True):
        b = FakeBoard({**REPLIES, 'help': 'I (5001) ' + help_line}, no_net=no_net)
        ctx = FakeCtx(b, FakeLog(), tempfile.gettempdir())
        core_suites.commands(ctx)
        return ctx

    def test_no_net_help_passes_without_key_and_wifi(self):
        self.assertTrue(any('console commands' in n for n in self.run_commands(HELP).notes))
        with self.assertRaises(Fail):                         # a network project still wants them
            self.run_commands(HELP, no_net=False)

    def test_network_commands_must_be_gone(self):
        with self.assertRaises(Fail) as e:
            self.run_commands(HELP + ', key, wifi status|offline|online')
        self.assertIn('no network', str(e.exception))

    def test_wifi_low_latency_is_not_checked(self):
        ctx = FakeCtx(FakeBoard({}, no_net=True), FakeLog(), tempfile.gettempdir())
        core_suites.wifi_low_latency(ctx)
        self.assertEqual(ctx.board.sent, [])
        self.assertIn('no network', ctx.notes[0])


# ---------------------------------------------------------------- the shot block (tools/devloop/shot.py)

def row565(*values):
    """A `S:` line: RGB565 pixels, little-endian, base64 (as forge_lvgl's screens.c writes it)."""
    return 'S:' + base64.b64encode(b''.join(struct.pack('<H', v) for v in values)).decode()


ROW0 = row565(0xF800, 0x07E0, 0x001F, 0xFFFF)          # red, green, blue, white
ROW1 = row565(0x0000, 0x8410, 0xF81F, 0x07FF)          # black, grey, magenta, cyan
RGB0 = bytes([255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255])
RGB1 = bytes([0, 0, 0, 131, 129, 131, 255, 0, 255, 0, 255, 255])    # 0x8410: 16/31, 32/63, 16/31 of 255
SHOT_LINES = ['I (3000) test: ok screen hello', '> shot hello 2', 'SHOT hello 4 2', ROW0,
              'W (3010) net: a line in between', ROW1, 'SHOTEND', 'I (3200) test: ok shot hello 4x2']


def png_parts(data):
    """(w, h, bit depth, colour type, decompressed IDAT) of a PNG."""
    assert data[:8] == b'\x89PNG\r\n\x1a\n'
    w, h, depth, ctype = struct.unpack('>IIBB', data[16:26])
    pos, idat = 8, b''
    while pos < len(data):
        n, kind = struct.unpack('>I4s', data[pos:pos + 8])
        if kind == b'IDAT':
            idat += data[pos + 8:pos + 8 + n]
        pos += 12 + n
    return w, h, depth, ctype, zlib.decompress(idat)


class ShotDecoder(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp()

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def test_block_to_pixels(self):
        got = list(shot.blocks(SHOT_LINES))
        self.assertEqual(len(got), 1)
        name, w, h, rgb, rows = got[0]
        self.assertEqual((name, w, h, rows), ('hello', 4, 2, 2))     # the stray log line is not a row
        self.assertEqual(rgb, RGB0 + RGB1)
        self.assertEqual(shot.rgb565(0x8410), (131, 129, 131))
        self.assertEqual(shot.last_block(SHOT_LINES, 'hello')[0], 'hello')
        self.assertIsNone(shot.last_block(SHOT_LINES, 'system'))
        self.assertIsNone(shot.last_block(SHOT_LINES[:6]))           # no SHOTEND yet: not complete

    def test_a_garbled_row_is_black_and_counted(self):
        name, w, h, rgb, rows = shot.last_block(['SHOT main 4 2', ROW0, 'S:AAA', 'SHOTEND'])   # a short row
        self.assertEqual((rows, rgb[:12], rgb[12:]), (2, RGB0, bytes(12)))
        self.assertEqual(shot.last_block(['SHOT main 4 2', ROW0, 'SHOTEND'])[4], 1)        # a row never came

    def test_png_pixel_values(self):
        _, w, h, rgb, _ = shot.last_block(SHOT_LINES)
        path = os.path.join(self.tmp, 'hello.png')
        shot.png(path, w, h, rgb)
        with open(path, 'rb') as f:
            pw, ph, depth, ctype, raw = png_parts(f.read())
        self.assertEqual((pw, ph, depth, ctype), (4, 2, 8, 2))        # 8-bit RGB
        self.assertEqual(raw, b'\0' + RGB0 + b'\0' + RGB1)            # filter 0 before each row
        # a round panel: the corner of a 4x4 picture is outside the inscribed circle (tinted red), (1,0) inside
        masked = png_parts(shot.png_bytes(4, 4, rgb + rgb, round_mask=True))[4]
        self.assertEqual(masked[1:4], bytes([90 + 255 // 2, 0, 0]))
        self.assertEqual(masked[4:7], RGB0[3:6])

    def test_distinct_colors(self):
        _, w, h, rgb, _ = shot.last_block(SHOT_LINES)
        self.assertEqual(shot.distinct_colors(rgb, w, h, step=1), 8)
        self.assertEqual(shot.distinct_colors(bytes(24), 4, 2, step=1), 1)   # blank

    def test_script_writes_a_png_per_block(self):
        log = os.path.join(self.tmp, 'serial_log.txt')
        write(log, '\n'.join(SHOT_LINES + ['I (1) x', 'SHOT system 4 2', ROW1, ROW0, 'SHOTEND']) + '\n')
        out = os.path.join(self.tmp, 'shots')
        with contextlib.redirect_stdout(io.StringIO()) as so:
            self.assertEqual(shot.main([log, out]), 2)
        self.assertEqual(sorted(os.listdir(out)), ['hello.png', 'system.png'])
        self.assertIn('4x2', so.getvalue())
        with open(os.path.join(out, 'system.png'), 'rb') as f:
            self.assertEqual(png_parts(f.read())[4], b'\0' + RGB1 + b'\0' + RGB0)
        write(log, 'I (1) nothing here\n')
        with self.assertRaises(SystemExit), contextlib.redirect_stdout(io.StringIO()):
            shot.main([log, out])


class SnapOverUsb(StubbedBoard):
    """Board.snap in no-net mode: `shot <screen> <div>` to the console, SHOTEND waited for, the block decoded."""

    def test_snap_returns_the_shot(self):
        self.script('shot hello 2', SHOT_LINES[2:])
        out = os.path.join(self.tmp, 'screen_hello.png')
        s = self.b.snap('hello', out)
        self.assertIsInstance(s, board.Shot)
        self.assertEqual(self.sent, ['shot hello 2'])
        self.assertEqual((s.w, s.h, s.rows, s.path, s.rgb), (4, 2, 2, out, RGB0 + RGB1))
        self.assertTrue(os.path.exists(out))
        self.script('shot hello 4', SHOT_LINES[2:])
        self.assertIsNone(self.b.snap('hello', None, div=4).path)
        self.assertEqual(self.sent[-1], 'shot hello 4')

    def test_an_error_line_fails_it(self):
        self.script('shot nope 2', ['W (5) test: error shot nope: no such screen'])
        with self.assertRaises(Fail) as e:
            self.b.snap('nope', None)
        self.assertIn('no such screen', str(e.exception))

    def test_no_shotend_times_out(self):
        self.script('shot hello 2', ['SHOT hello 4 2', ROW0])
        with self.assertRaises(Fail) as e:
            self.b.shot('hello', None, timeout=0.3)
        self.assertIn('SHOTEND', str(e.exception))

    def test_screens_suite_takes_every_screen_over_usb(self):
        # core_suites.every_screen in no-net mode: show, shot at half size (div 2), not blank, snapshot_ms metric
        w, h = CFG['screen']['w'] // 2, CFG['screen']['h'] // 2
        red, blue = row565(*([0xF800] * w)), row565(*([0x001F] * w))
        self.script('shot hello 2', [f'SHOT hello {w} {h}'] + [red, blue] * (h // 2) + [red] * (h % 2) + ['SHOTEND'])
        self.b.show = lambda name: self.sent.append(f'screen {name}')
        self.b.wait_screen = lambda name, t: None
        ctx = FakeCtx(self.b, self.b.log, self.tmp)
        with mock.patch.dict(core_suites.CFG, {'screens': ['hello'], 'screens_not_shown': []}), \
                mock.patch.object(core_suites, 'time', FakeClock()):
            core_suites.every_screen(ctx)
        self.assertIn('screen hello', self.sent)
        self.assertIn('snapshot_ms.hello', ctx.metrics)
        self.assertTrue(os.path.exists(os.path.join(self.tmp, 'screen_hello.png')))


# ---------------------------------------------------------------- a lost answer: sent again only when it only reads

class Resend(StubbedBoard):
    WHERE = ['I (9) test: where task_lvgl=Blocked console=idle']

    def test_a_read_only_command_is_sent_again_once(self):
        # DFS: `power` lost while the panel slept, the next command answered (espforge v0.6.0-rc.6/rc.7)
        self.script('power', [], ['I (10) test: power cpu_mhz=80 panel=asleep screen_lock=0'])   # lost, then answered
        self.script('where', self.WHERE)
        with contextlib.redirect_stdout(io.StringIO()) as so:
            got = self.b.cmd('power', r'test: power (.*)', timeout=0.3).group(1)
        self.assertEqual(got, 'cpu_mhz=80 panel=asleep screen_lock=0')
        self.assertEqual(self.sent, ['power', 'where', 'power'])
        self.assertEqual(self.b.resends, ['power'])
        self.assertIn('sent again', so.getvalue())

    def test_a_command_with_side_effects_is_never_sent_twice(self):
        for line in ('fps', 'screen hello', 'swipe left', 'wifi offline'):
            self.sent.clear()
            self.script(line, [])
            self.answers['where'] = [self.WHERE]
            with self.assertRaises(Fail) as e:
                self.b.cmd(line, timeout=0.2)
            self.assertEqual(self.sent, [line, 'where'], line)
            self.assertIn('where: task_lvgl=Blocked', str(e.exception))
        self.assertEqual(self.b.resends, [])

    def test_the_rule(self):
        for line in ('power', 'presence', 'te', 'heap', 'screen', 'wifi status', 'netstat'):
            self.assertTrue(self.b.may_resend(line), line)
        for line in ('fps', 'fps reset', 'screen hello', 'presence calibrate 5', 'te lvgl off', 'wake', 'where'):
            self.assertFalse(self.b.may_resend(line), line)

    def test_the_report_says_so(self):
        self.assertIsNone(harness.resend_note('console.commands', []))
        self.assertEqual(harness.resend_note('presence.dim_off_wake', ['power', 'presence', 'power']),
                         'presence.dim_off_wake: no answer, sent again: "power" (2x), "presence"')


# ---------------------------------------------------------------- display.te (the tear-free display)

TE_LINE = 'I (1850) display: TE 59 Hz on GPIO13 (period 16.85 ms, high 0.95 ms)'
FPS_NEW = ('test: fps frames=40 render_avg_ms=6.1 render_max_ms=9.0 anim_frames=38 anim_fps=58.9 gap_max_ms=18.0 '
           'gap_max_at_ms=420 gap_max_kind=raw>raw mpx=8.20 pushes=41 push_avg_ms=9.8 push_max_ms=12.1 '
           'te_wait_avg_ms=4.2 te_wait_max_ms=16.5 te_lag_max_ms=0.21 te_timeouts=0 te_chased=0')
FPS_OLD = ('test: fps frames=40 render_avg_ms=6.1 render_max_ms=9.0 anim_frames=38 anim_fps=58.9 gap_max_ms=18.0 '
           'gap_max_at_ms=420 gap_max_kind=raw>raw mpx=8.20')


def te_reply(clock, state=1, period=16.85, lvgl='on'):
    return lambda: (f'I (9000) test: te state={state} rises={1000 + int(59 * (clock.t - 1000))} period_ms={period} '
                    f'high_ms=0.95 level=0 lvgl={lvgl} raw=on chase=on')


class DisplayTe(unittest.TestCase):
    def test_boot_line(self):
        self.assertEqual(core_suites.te_boot(BOOT + [TE_LINE]), {'hz': 59, 'gpio': 13, 'period_ms': 16.85,
                                                                 'high_ms': 0.95})
        self.assertIsNone(core_suites.te_boot(BOOT))           # missed in the first 2.5 s, or an old firmware
        bad = core_suites.te_boot(['W (1850) display: TE 0 Hz on GPIO13: not usable, frames not synced to the panel '
                                   '(tearing possible)'])
        self.assertIn('not usable', bad['unusable'])
        self.assertIn('unusable', core_suites.te_boot(['W (300) display: TE: no pulse on GPIO13: frames not synced '
                                                       'to the panel (tearing possible)']))
        # the retry while starting is not the verdict: the 1 s count after it is
        self.assertEqual(core_suites.te_boot(['I (200) display: TE: no pulse on GPIO13 in 100 ms, sending 0x35 0x00',
                                              TE_LINE])['hz'], 59)

    def test_te_status(self):
        st = core_suites.te_status('state=1 rises=1234 period_ms=16.85 high_ms=0.95 level=0 lvgl=on raw=on chase=on')
        self.assertEqual((st['state'], st['rises'], st['period_ms'], st['lvgl']), (1, 1234, 16.85, 'on'))
        self.assertEqual(core_suites.te_problems(st), [])
        self.assertEqual(len(core_suites.te_problems({**st, 'state': 0.0})), 1)
        self.assertIn('-1', core_suites.te_problems({**st, 'state': -1.0})[0])
        self.assertIn('period 33.7', core_suites.te_problems({**st, 'period_ms': 33.7})[0])   # every other pulse
        self.assertIn('lvgl=off', core_suites.te_problems({**st, 'lvgl': 'off'})[0])
        self.assertTrue(core_suites.te_problems({}))

    def test_fps_fields(self):
        self.assertEqual(core_suites.fps_te_problems(kv(FPS_NEW)), [])
        self.assertIn('no pushes=', core_suites.fps_te_problems(kv(FPS_OLD))[0])
        self.assertIn('te_timeouts=2', core_suites.fps_te_problems(kv(FPS_NEW.replace('te_timeouts=0',
                                                                                      'te_timeouts=2')))[0])
        self.assertIn('pushes=0', core_suites.fps_te_problems(kv(FPS_NEW.replace('pushes=41', 'pushes=0')))[0])

    def run_te(self, replies, lines=None):
        b = FakeBoard(replies)
        ctx = FakeCtx(b, FakeLog(lines if lines is not None else BOOT + [TE_LINE]), tempfile.gettempdir())
        with mock.patch.object(core_suites, 'time', self.clock):
            core_suites.te(ctx)
        return ctx, b

    def setUp(self):
        self.clock = FakeClock()
        self.ok = {'te': te_reply(self.clock), 'fps reset': 'I (1) test: ok fps reset', 'swipe left': 'test: ok swipe',
                   'swipe right': 'test: ok swipe', 'fps': 'I (2) ' + FPS_NEW}

    def test_passes_on_the_tear_free_display(self):
        ctx, b = self.run_te(self.ok)
        self.assertEqual([c for c in b.sent if not c.startswith('screen')],
                         ['te', 'te', 'fps reset', 'swipe left', 'swipe right', 'fps'])
        self.assertIn('59 Hz at start-up', ctx.notes[0])
        self.assertIn('41 pushes', ctx.notes[0])
        ctx, _ = self.run_te(self.ok, lines=BOOT)                 # the start-up line missed: the console decides
        self.assertIn('start-up line not in this log', ctx.notes[0])

    def test_fails_on_the_old_firmware(self):
        with self.assertRaises(Fail) as e:                        # no `te` command
            self.run_te({k: v for k, v in self.ok.items() if k != 'te'}, lines=BOOT)
        self.assertIn('no `te` console command', str(e.exception))
        with self.assertRaises(Fail) as e:                        # a `te`, but the fps line without pushes
            self.run_te({**self.ok, 'fps': 'I (2) ' + FPS_OLD})
        self.assertIn('no pushes=', str(e.exception))

    def test_fails_on_a_bad_pulse(self):
        with self.assertRaises(Fail) as e:
            self.run_te(self.ok, lines=BOOT + [TE_LINE.replace('59 Hz', '30 Hz')])
        self.assertIn('30 Hz', str(e.exception))
        with self.assertRaises(Fail):
            self.run_te({**self.ok, 'te': te_reply(self.clock, state=-1)})
        frozen = 'I (9000) test: te state=1 rises=5000 period_ms=16.85 high_ms=0.95 level=1 lvgl=on raw=on chase=on'
        with self.assertRaises(Fail) as e:                        # the pulse stopped after the start-up
            self.run_te({**self.ok, 'te': frozen})
        self.assertIn('pulse stopped', str(e.exception))
        with self.assertRaises(Fail) as e:
            self.run_te({**self.ok, 'fps': 'I (2) ' + FPS_NEW.replace('te_timeouts=0', 'te_timeouts=3')})
        self.assertIn('te_timeouts=3', str(e.exception))

    def test_a_board_without_te_opts_out(self):
        with mock.patch.dict(core_suites.CFG, {'display_te': False}):
            ctx, b = self.run_te({})
        self.assertEqual(b.sent, [])
        self.assertIn('display_te', ctx.notes[0])


# ---------------------------------------------------------------- memory.heap_numbers in an --ota run

class HeapAfterOta(unittest.TestCase):
    def test_wait_past_the_first_minute(self):
        self.assertEqual(core_suites.ota_heap_wait_s(30), 60)
        self.assertEqual(core_suites.ota_heap_wait_s(68), 22)
        self.assertEqual(core_suites.ota_heap_wait_s(90), 0)
        self.assertEqual(core_suites.ota_heap_wait_s(400), 0)

    def heap_run(self, ota, readings):
        clock, it = FakeClock(), iter(readings)
        last = []
        def heap():
            last[:] = [next(it, last[0] if last else None)]
            return last[0]
        b = type('B', (), {'heap': staticmethod(heap)})()
        ctx = FakeCtx(b, FakeLog(), tempfile.gettempdir(), type('O', (), {'ota': ota})())
        with mock.patch.object(core_suites, 'time', clock):
            core_suites.heap_numbers(ctx)
        return ctx, clock

    def test_an_ota_run_reads_after_the_first_minute(self):
        # read at 68 s (the first update check holds its buffers: largest 35), then 90 s on: 44, twice
        h = lambda up, largest: {'internal': 120, 'min': 100, 'largest': largest, 'psram_min': 7000, 'uptime_s': up,
                                 'failed_allocs': 0, 'lvgl_fallbacks': 0}
        ctx, clock = self.heap_run('v0.7.0-rc.1', [h(68, 35), h(90, 44), h(92, 44)])
        self.assertEqual(ctx.metrics['internal_largest_kb'], 44)
        self.assertEqual(clock.slept[0], 22)
        self.assertIn('first-minute update check', ctx.notes[0])
        ctx, clock = self.heap_run(None, [h(68, 35)])             # a run on a flashed build: read at once
        self.assertEqual((ctx.metrics['internal_largest_kb'], clock.slept), (35, []))


class PerfHelpers(unittest.TestCase):
    def test_slide_drag_line(self):
        line = ('I (61234) slide: drag: first frame after 15 ms, 21 frames in 352 ms (60 fps), to next | '
                'gap max 41 ms, held reads 0 err 0 up (longest 0 ms), finger still max 0 ms, samples 30, renders 0, 1.20 px/ms')
        self.assertEqual(core_suites.slide_drag(line), {'first_ms': 15, 'frames': 21, 'ms': 352, 'fps': 60,
                                                        'to': 'to next', 'gap_max_ms': 41})
        self.assertEqual(core_suites.slide_drag(line.replace('to next', 'back'))['to'], 'back')
        self.assertIsNone(core_suites.slide_drag('I (1) slide: scroll: 20 frames in 300 ms'))


class MeasureSwipeGap(unittest.TestCase):
    """app_suites.measure(): the swipe's worst gap is the move's own (`slide: drag:` gap max), not the fps counters'
    (an LVGL redraw of the landed page within 250 ms counted there: 222 ms once, espforge v0.6.0-rc.4)."""

    def run_measure(self, drag_line):
        log = FakeLog()

        class B:
            def wait_online(self):
                return 0

            def heap(self):
                return {'uptime_s': 600}

            def cmd(self, line, expect=None, **kw):
                if line == 'swipe left' and drag_line:
                    log.data.append(drag_line)
                reply = ('test: fps frames=30 render_avg_ms=4.0 render_max_ms=9.0 anim_frames=25 anim_fps=58.0 '
                         'gap_max_ms=222.0 gap_max_at_ms=900 gap_max_kind=lvgl>lvgl mpx=5.4') if line == 'fps' else 'test: ok'
                return re.search(expect, reply) if expect else None

        ctx = FakeCtx(B(), log, tempfile.mkdtemp())
        with mock.patch.object(app_suites, 'time', FakeClock()),                 mock.patch.object(app_suites.time, 'localtime', lambda: time.struct_time((2026, 11, 1, 2, 0, 10, 0, 305, 0)),
                                  create=True):
            app_suites.measure(ctx, 'x', ['swipe left'])
        return ctx

    def test_the_moves_own_gap_is_the_metric(self):
        ctx = self.run_measure('I (61234) slide: drag: first frame after 15 ms, 21 frames in 352 ms (60 fps), to next | '
                               'gap max 41 ms, held reads 0 err 0 up (longest 0 ms), finger still max 0 ms, samples 30, '
                               'renders 0, 1.20 px/ms')
        self.assertEqual(ctx.metrics['swipe_gap_max_ms.x'], 41)
        self.assertIn('222', ' '.join(ctx.notes))

    def test_without_a_drag_line_the_fps_gap(self):
        self.assertEqual(self.run_measure(None).metrics['swipe_gap_max_ms.x'], 222)


class ForgeJson(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp()

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def write(self, obj, sub=''):
        d = os.path.join(self.tmp, sub)
        os.makedirs(d, exist_ok=True)
        with open(os.path.join(d, 'forge.json'), 'w', encoding='utf-8') as f:
            json.dump(obj, f)

    def test_found_by_walking_up(self):
        self.write({'app': 'demo', 'chip': 'esp32c3', 'screen': {'w': 240}})
        deep = os.path.join(self.tmp, 'tools', 'harness', 'x')
        os.makedirs(deep)
        cfg = forgecfg.load(deep)
        self.assertEqual(os.path.normcase(cfg['_root']), os.path.normcase(self.tmp))
        self.assertEqual((cfg['app'], cfg['chip']), ('demo', 'esp32c3'))
        self.assertEqual(cfg['screen'], {'w': 240, 'h': 466, 'shape': 'rect'})   # defaults fill the rest
        self.assertEqual(cfg['monitor_baud'], 115200)

    def test_nearest_wins(self):
        self.write({'app': 'outer'})
        self.write({'app': 'inner'}, 'sub')
        self.assertEqual(forgecfg.load(os.path.join(self.tmp, 'sub'))['app'], 'inner')

    def test_missing(self):
        with self.assertRaises(FileNotFoundError):
            forgecfg.find_root(self.tmp)                     # (a temp folder has no forge.json above it)

    def test_https_port_default_and_override(self):
        # CONFIG_FORGE_HTTPS_PORT (a co-processor board may not get 443): the tools follow forge.json https_port
        self.write({'app': 'demo'})
        cfg = forgecfg.load(self.tmp)
        self.assertEqual(cfg['https_port'], 443)
        self.assertEqual(forgecfg.https_base(cfg, '10.0.0.5'), 'https://10.0.0.5')
        self.write({'app': 'demo', 'https_port': 8443}, 'p4')
        cfg = forgecfg.load(os.path.join(self.tmp, 'p4'))
        self.assertEqual(forgecfg.https_base(cfg, '10.0.0.5'), 'https://10.0.0.5:8443')

    def test_paths_and_cache(self):
        self.write({'build_dir': 'build/v55'})
        cfg = forgecfg.load(self.tmp)
        self.assertEqual(forgecfg.path(cfg, cfg['build_dir']), os.path.join(self.tmp, 'build', 'v55'))
        self.assertEqual(forgecfg.read_cached(cfg, 'ip'), '')
        forgecfg.write_cached(cfg, 'ip', '10.0.0.7')
        self.assertEqual(forgecfg.read_cached(cfg, 'ip'), '10.0.0.7')
        self.assertTrue(os.path.isdir(os.path.join(self.tmp, '.devloop')))

    def test_the_repository_forge_json(self):
        for k in ('app', 'chip', 'build_dir', 'screens', 'ready_line', 'ip_line', 'key_env', 'setup_ssid'):
            self.assertIn(k, CFG)
        m = re.search(CFG['ip_line'], 'I (5123) net: Connected, IP 192.168.1.50')
        self.assertEqual(m.group(1), '192.168.1.50')
        self.assertTrue(re.search(CFG['ready_line'], 'I (4000) diag: mark app ready      internal 90 KB free'))


class Stage(unittest.TestCase):
    def test_stage_copies_and_manifest(self):
        sys.path.insert(0, os.path.join(HERE, '..', 'devloop'))
        import stage
        tmp = tempfile.mkdtemp()
        try:
            write(os.path.join(tmp, 'forge.json'), {'app': 'demo', 'build_dir': 'build'})
            b = os.path.join(tmp, 'build')
            os.makedirs(os.path.join(b, 'bootloader'))
            write(os.path.join(b, 'bootloader', 'bootloader.bin'), b'boot')
            write(os.path.join(b, 'demo.bin'), b'app' * 100)
            write(os.path.join(b, 'flasher_args.json'), {'flash_settings': {'flash_mode': 'dio', 'flash_freq': '80m', 'flash_size': '16MB'},
                       'flash_files': {'0x10000': 'demo.bin', '0x0': 'bootloader/bootloader.bin'},
                       'app': {'file': 'demo.bin'}, 'extra_esptool_args': {'chip': 'esp32s3'}})
            m = stage.stage(forgecfg.load(tmp))
            self.assertEqual([p['offset'] for p in m['parts']], ['0x0', '0x10000'])
            d = os.path.join(tmp, '.devloop', 'stage')
            for p in m['parts']:
                self.assertEqual(stage.md5(os.path.join(d, p['file'])), p['md5'])
                self.assertRegex(p['file'], r'^(bootloader|demo)_\d{8}-\d{6}\.bin$')
            self.assertEqual(load_json(os.path.join(d, 'manifest.json'))['chip'], 'esp32s3')
        finally:
            shutil.rmtree(tmp, ignore_errors=True)


if __name__ == '__main__':
    unittest.main()
