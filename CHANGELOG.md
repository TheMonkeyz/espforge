# Changelog

What changed in each release. The display shows these notes on its update screen, and the settings page shows them
before you install; only the entries newer than the version you have are shown. The flasher site publishes them as
`notes.json`, built by `tools/make_flasher_site.py`.

How to write an entry: add a `## vX.Y.Z - YYYY-MM-DD` section at the top **before** tagging the release, with one
`- ` line per change, written for the person holding the display. Release candidates get their own
`## vX.Y.Z-rc.N - YYYY-MM-DD` section, shown only to Beta users; the final release's section lists everything again.

## v0.1.0-rc.1 - 2026-10-04
- Three screens to swipe through: a welcome screen, a system screen with the firmware version and network status, and the Wi-Fi setup screen.
- Wi-Fi setup from a phone: join the display's own setup network (its password is on the screen), or scan the Easy Connect code with an Android phone.
- If the Wi-Fi is unreachable at start-up, the setup network opens by itself for a while.
- A settings page on your network at https://<the display's address>/: Wi-Fi, language and updates.
- English and French (Canada), on the display and on the settings page.
- Updates over Wi-Fi from the Stable or Beta channel, with release notes before you install. A new version that fails to start goes back to the previous one by itself.
