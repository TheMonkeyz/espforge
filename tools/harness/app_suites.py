"""The starter app's own tests (main/: the hello and system screens, setup on a long-press). Replace or extend them
with your app's: same registry (@test from board.py), same ctx as core_suites.py.

    APP_ORDER       the app's suites, run after the core start-up checks (harness.py)
    APP_WATCH       log lines that aren't failures but must not go unseen: (regex, what), counted per test
    METRIC_SUITES   which suite produces a metric (prefix -> suite): a baseline metric is only expected when its
                    suite ran
"""
import time

from board import CFG, SCREEN_C, check, test

APP_ORDER = ['navigation', 'perf']
APP_WATCH = [
    (r'display: .*did not finish', 'a panel transfer timed out'),
    (r'lvgl: .*(out of memory|alloc failed)', 'LVGL could not allocate'),
]
METRIC_SUITES = [('swipe_', 'perf')]

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


# ---------------------------------------------------------------- perf

def measure(ctx, name, action, settle=1.0):
    """Frame rate during `action` (console commands): fps reset, act, wait for the animation to end, read fps.
    anim_fps counts frames less than 250 ms apart; gap_max_ms is the longest wait between two of them."""
    b = ctx.board
    b.cmd('fps reset')
    for c in action:
        b.cmd(c)
    time.sleep(settle)
    line = b.cmd('fps', r'test: fps (.*)').group(1)
    v = {k: float(x) for k, x in (kv.split('=') for kv in line.split() if '=' in kv)}
    check(v.get('anim_frames', 0) > 0, f'{name}: no animation frames counted ({line})')
    ctx.metric(f'swipe_fps.{name}', round(v['anim_fps'], 1))
    ctx.metric(f'swipe_gap_max_ms.{name}', round(v['gap_max_ms']))
    ctx.metric(f'swipe_render_avg_ms.{name}', round(v['render_avg_ms'], 1))
    ctx.note(f'{name}: {v["anim_fps"]:.1f} fps, {int(v["anim_frames"])} frames, render avg {v["render_avg_ms"]:.1f} ms '
             f'max {v["render_max_ms"]:.1f} ms, worst gap {v["gap_max_ms"]:.0f} ms')


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
