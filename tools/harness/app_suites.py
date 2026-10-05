"""The starter app's own tests (main/: the hello and system screens, setup on a long-press). Replace or extend them
with your app's: same registry (@test from board.py), same ctx as core_suites.py.

    APP_ORDER       the app's suites, run after the core start-up checks (harness.py)
    APP_WATCH       log lines that aren't failures but must not go unseen: (regex, what), counted per test
    METRIC_SUITES   which suite produces a metric (prefix -> suite): a baseline metric is only expected when its
                    suite ran
"""
import re
import time

from board import CFG, SCREEN_C, check, test

APP_ORDER = ['navigation', 'perf']
APP_WATCH = [
    (r'display: .*did not finish', 'a panel transfer timed out'),
    (r'lvgl: .*(out of memory|alloc failed)', 'LVGL could not allocate'),
]
METRIC_SUITES = [('swipe_', 'perf'), ('setup_page_', 'navigation'), ('easy_connect_', 'navigation')]

HOME, NEXT = 'hello', 'system'                         # the starter's two pages, side by side: hello | system


def go_home(ctx):
    ctx.board.show(HOME)
    ctx.board.wait_screen(HOME, 6)
    time.sleep(0.5)


# ---------------------------------------------------------------- navigation

@test('navigation')
def swipe_between_pages(ctx):
    """Swipe like a person: left to the next page, right back; no wrap-around at either end."""
    b = ctx.board
    go_home(ctx)
    route = [('swipe left', NEXT), ('swipe left', NEXT),      # the right end: bounces back
             ('swipe right', HOME), ('swipe right', HOME)]    # the left end: bounces back
    for cmd, want in route:
        b.cmd(cmd)
        time.sleep(0.8)
        b.wait_screen(want, 6)
    ctx.note(f'{HOME} <-> {NEXT} by swipes, both ends bounce')


@test('navigation')
def quick_swipes(ctx):
    """A swipe that lands while the previous move's release animation still runs is a swipe too. LVGL reads nothing
    during a move, so slide.c's read hook never saw the first press end, and took the second for it: nothing moved
    (LESSONS L181). 'swipe left right' leaves 150 ms of "up" between the two
    (70 ms was sometimes read as one of the chip's brief false "ups": one drag, left then right)."""
    b = ctx.board
    go_home(ctx)
    at = len(ctx.log.lines())
    b.cmd('swipe left right')
    time.sleep(1.0)
    drags = [l for l in ctx.log.lines()[at:] if 'slide: drag: first frame' in l]
    check(len(drags) == 2, f'two quick swipes made {len(drags)} drag(s); the second was taken for the first')
    # Where the second one ends is timing: it starts when the first's release animation ends, by then the simulated
    # finger has mostly moved on ("samples 1", "back" once on v0.1.1-rc.1, on to hello in the run before)
    second = 'back' if ' back |' in drags[1] else 'on'
    ctx.note(f'two swipes 150 ms apart: 2 drags, the second went {second}; now on {b.screen()}')
    go_home(ctx)


@test('navigation')
def long_press_opens_setup(ctx):
    """A long-press opens Wi-Fi setup; a tap closes it, back where it was."""
    b = ctx.board
    if 'setup' not in CFG['screens']:
        ctx.note('no "setup" screen in forge.json: not checked')
        return
    go_home(ctx)
    b.press()
    b.wait_screen('setup', 6)
    time.sleep(1)                                      # a tap within the long-press's own release window is ignored
    b.tap()
    b.wait_screen(HOME, 6)
    ctx.note(f'long-press at {SCREEN_C}: setup; tap: back to {HOME}')


@test('navigation')
def setup_pages_slide(ctx):
    """Setup's two pages (setup network | Easy Connect) follow the finger like hello | system, and the Easy Connect QR
    code shows up quickly. User reports, October 4: the setup pages only switched after the swipe (and froze while
    the radio switched), and the QR code took ~2 s (a channel scan the connected device doesn't need)."""
    b = ctx.board
    if 'setup1' not in CFG['screens']:
        ctx.note('no "setup1" screen in forge.json: not checked')
        return
    b.show('setup')
    b.wait_screen('setup', 6)
    time.sleep(1)
    at = len(ctx.log.lines())                         # own position (log.mark() is the harness's crash check)
    t0 = time.time()
    b.cmd('swipe left')
    b.wait_screen('setup1', 3)
    m = ctx.log.wait(r'slide: drag: first frame after (\d+) ms, (\d+) frames in (\d+) ms \((\d+) fps\), to next', 5,
                     'the setup page following the finger', start=at)
    fps = int(m.group(4))
    check(fps >= 40, f'setup page drag at {fps} fps')
    ctx.metric('setup_page_fps', fps)
    page = ctx.log.wait(r'ui: Wi-Fi setup page 1', 5, 'Easy Connect started', start=at)
    qr = ctx.log.wait(r'net: Easy Connect: QR code ready', 10, 'the Easy Connect QR code', start=at)
    qr_s = (log_ms(qr.string) - log_ms(page.string)) / 1000
    ctx.metric('easy_connect_qr_s', round(qr_s, 2))
    b.cmd('swipe right')                                # back to the first page, then closed (online again)
    b.wait_screen('setup', 3)
    go_home(ctx)
    ctx.note(f'setup -> Easy Connect by a drag at {fps} fps, its QR code {qr_s:.2f} s after the page '
             f'({time.time() - t0:.1f} s in all, console round trips included)')


def log_ms(line):
    """The ESP-IDF timestamp of a log line, in ms ("I (12345) tag: ...")."""
    m = re.search(r'\((\d+)\)', line)
    return int(m.group(1)) if m else 0


# ---------------------------------------------------------------- perf

def measure(ctx, name, action, settle=1.0):
    """Frame rate during `action` (console commands): fps reset, act, wait for the animation to end, read fps.
    anim_fps counts frames less than 250 ms apart; gap_max_ms is the longest wait between two of them."""
    b = ctx.board
    if time.localtime().tm_sec > 55:                 # not across a minute change: the clock's redraw right after a
        time.sleep(62 - time.localtime().tm_sec)     # move counts in swipe_gap_max_ms (weather_amoled: 127 ms once)
    b.cmd('fps reset')
    for c in action:
        b.cmd(c)
    time.sleep(settle)
    line = b.cmd('fps', r'test: fps (.*)').group(1)
    kv = dict(x.split('=', 1) for x in line.split() if '=' in x)
    v = {k: float(x) for k, x in kv.items() if re.fullmatch(r'-?[\d.]+', x)}   # gap_max_kind is text
    check(v.get('anim_frames', 0) > 0, f'{name}: no animation frames counted ({line})')
    ctx.metric(f'swipe_fps.{name}', round(v['anim_fps'], 1))
    ctx.metric(f'swipe_gap_max_ms.{name}', round(v['gap_max_ms']))
    ctx.metric(f'swipe_render_avg_ms.{name}', round(v['render_avg_ms'], 1))
    ctx.note(f'{name}: {v["anim_fps"]:.1f} fps, {int(v["anim_frames"])} frames, render avg {v["render_avg_ms"]:.1f} ms '
             f'max {v["render_max_ms"]:.1f} ms, worst gap {v["gap_max_ms"]:.0f} ms' +
             (f' ({kv["gap_max_kind"]}, {kv["gap_max_at_ms"]} ms after the reset)' if 'gap_max_kind' in kv else ''))


@test('perf')
def page_swipes(ctx):
    b = ctx.board
    go_home(ctx)
    measure(ctx, f'{HOME}_to_{NEXT}', ['swipe left'])
    b.wait_screen(NEXT, 4)
    measure(ctx, f'{NEXT}_to_{HOME}', ['swipe right'])
    b.wait_screen(HOME, 4)
    w, h = CFG['screen']['w'], CFG['screen']['h']        # a slow drag, finger-following, then the snap
    measure(ctx, 'drag_slow', [f'drag {w * 3 // 4} {h // 2} {w // 4} {h // 2} 600'], settle=1.5)
    b.wait_screen(NEXT, 4)
    go_home(ctx)
