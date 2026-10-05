# Changelog

What changed in each release. The display shows these notes on its update screen, and the settings page shows them
before you install; only the entries newer than the version you have are shown. The flasher site publishes them as
`notes.json`, built by `tools/make_flasher_site.py`.

How to write an entry: add a `## vX.Y.Z - YYYY-MM-DD` section at the top **before** tagging the release, with one
`- ` line per change, written for the person holding the display. Release candidates get their own
`## vX.Y.Z-rc.N - YYYY-MM-DD` section, shown only to Beta users; the final release's section lists everything again.

## v0.1.1-rc.1 - 2026-10-04
- Two quick swipes in a row both move the screen: the second one, landing while the first was still settling, was ignored.
- The settings page no longer tells someone on the setup network the name and address of your home network.
- Behind the scenes: crash-report cleanup no longer repeats at every start; quick setup-page switches can no longer stop Easy Connect twice at once; a smoother-swipe measurement that is no longer fooled by an unrelated screen update.

## v0.1.0 - 2026-10-04
- Two screens to swipe between, following your finger smoothly: a welcome screen with the time and date, and a system screen with the firmware version, network, memory and update status.
- Press and hold anywhere for the Wi-Fi setup screen.
- Wi-Fi setup from a phone: join the display's own setup network (its password is on the screen), or scan the Easy Connect code with an Android phone. Swipe between the two.
- If the Wi-Fi is unreachable at start-up, the setup network opens by itself for a while.
- A settings page for Wi-Fi, language and updates: scan the code on the system screen with your phone.
- English and French (Canada), on the display and on the settings page.
- Updates over Wi-Fi from the Stable or Beta channel, with release notes before you install. A new version that fails to start goes back to the previous one by itself.

## v0.1.0-rc.6 - 2026-10-04
- Easy Connect works reliably: the display used to miss the phone's last message, so the phone said "That didn't work".
- The setup network stays available while the Easy Connect page is shown.
- Swiping to the system screen shows its current details right away.

## v0.1.0-rc.5 - 2026-10-04
- Easy Connect answers the phone faster.
- A network name made only of emoji shows as "…" instead of nothing.

## v0.1.0-rc.4 - 2026-10-04
- The Easy Connect code shows as a faint placeholder until it is ready, then fades in.

## v0.1.0-rc.3 - 2026-10-04
- Swiping between screens follows your finger smoothly (about 66 frames a second instead of 24), and the Wi-Fi setup pages swipe the same way.
- The Easy Connect code appears at once instead of after about 2 seconds.
- Network names with emoji show without empty boxes.
- Fixed: switching quickly back from the Easy Connect page could restart the display.
- Fixed: the Easy Connect page title overlapped the line below it.

## v0.1.0-rc.2 - 2026-10-04
- Before the first stable release, a display on the Stable channel says it is up to date instead of showing an error.

## v0.1.0-rc.1 - 2026-10-04
- Two screens to swipe between: a welcome screen with the time and date, and a system screen with the firmware version, network, memory and update status.
- Press and hold anywhere for the Wi-Fi setup screen.
- Wi-Fi setup from a phone: join the display's own setup network (its password is on the screen), or scan the Easy Connect code with an Android phone.
- If the Wi-Fi is unreachable at start-up, the setup network opens by itself for a while.
- A settings page for Wi-Fi, language and updates: scan the code on the system screen with your phone.
- English and French (Canada), on the display and on the settings page.
- Updates over Wi-Fi from the Stable or Beta channel, with release notes before you install. A new version that fails to start goes back to the previous one by itself.
