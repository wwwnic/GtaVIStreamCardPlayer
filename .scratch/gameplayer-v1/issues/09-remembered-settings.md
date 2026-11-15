# 09: Remembered settings

**What to build:** GamePlayer behaves like a normal desktop app between runs. It reopens where the user closed it, at the same size, on the same monitor, and with the sound level, mute state, Audio Delay and menu choices they last used. The user never sees a settings page: the app simply keeps whatever is current. If the saved information is missing or damaged, the app quietly starts with defaults.

**Blocked by:** 08 (Menu bar).

**Status:** done (user hand checks pending)

## Context

GamePlayer is a small personal Windows 11 app (the "Viewer") that shows a game Console's picture and sound from a USB Capture Card on the PC monitor. It is for one user, shipped as a plain executable with no installer. One of the user's reasons for leaving OBS is that its configuration keeps getting disturbed; GamePlayer's settings must be few, robust and invisible. Read `GLOSSARY.md`, the ADRs in `docs/adr/`, the developer note from ticket 02 and the settings structure introduced in ticket 08.

## Decisions already made

- **What is remembered:**
  - Window position, size and maximised state.
  - Volume, mute, Audio Delay.
  - Menu overrides: chosen Capture Card, resolution override, frame rate handling (Auto / 30 / 60).
  - View options: V-sync, always on top.
- **What is not remembered:** fullscreen. The app always opens windowed, even if it was closed in fullscreen; in that case it reopens with the windowed position it had before going fullscreen.
- **Where:** one small human-readable file in the user's roaming app-data folder, in a GamePlayer subfolder (obtain the folder from the system with `SHGetKnownFolderPath(FOLDERID_RoamingAppData)`; never hard-code a path). Not the registry, not next to the executable.
- **Format:** a simple text format that a person could read and fix by hand (INI-style key=value or a small JSON document). Do not add a heavy dependency for this; a small hand-written parser is fine. Include a format version number.
- **When saved:** on a clean exit, and also shortly after any change (debounced, so dragging the window does not write the file many times a second), so a crash or power loss loses nothing important. Write to a temporary file and rename it over the old one so a half-written file can never be left behind.
- **Failure behaviour:** a missing file, an unreadable file, an unknown version, or any invalid or out-of-range value falls back to the default for that value, silently, without an error message, and the app carries on. A damaged file is simply replaced on the next save. Unknown keys are ignored.
- **Window placement:** normal Windows behaviour. Use `GetWindowPlacement` / `SetWindowPlacement` so the restore position is right even when the window is closed maximised. On start-up, check that the saved position is still on a connected monitor; if the monitor it was on has gone, or the window would be off-screen, put it on the primary monitor at the default size.
- **Remembered Capture Card:** store a stable identifier for the device (its symbolic link or equivalent) together with its friendly name. On start-up, use the remembered device if it is present; if not, fall back to the automatic choice (prefer the GC553 Pro, otherwise the first device) without changing the saved preference just because the card is temporarily unplugged.
- **Remembered resolution override:** if the remembered mode is not offered by the device at start-up, use the default (1080p60) for this run.
- **Defaults** (must match the earlier tickets): default window size on the primary monitor, volume 100%, not muted, Audio Delay 0 ms, automatic device, default resolution 1080p60, frame rate Auto, V-sync off, always on top off.
- **Structure:** loading, validating, defaulting and serialising the settings are pure logic operating on text in and a settings structure out, with the file system access kept at the edge. That pure part must be unit tested thoroughly; this ticket is one of the places where the project's "unit tests for pure logic" policy carries the most weight.

## Known unknowns

- Whether the Capture Card's device identifier is stable across replugs and USB ports. If it changes, matching by friendly name as a second attempt is the fallback; implement both and note what you saw.
- Per-monitor DPI differences can make a restored window land at a slightly different size on a monitor with a different scale factor. Windows placement APIs handle the common case; do not build custom DPI handling unless you observe a real problem.

## Out of scope

A settings dialog or settings page; profiles; import or export; a "reset to defaults" menu item (deleting the file does it); remembering fullscreen; storing anything about Lossless Scaling (the Viewer never controls it); cloud sync; registry use.

## Acceptance criteria

- [ ] Closing and reopening the app restores window position, size and maximised state on the same monitor.
- [ ] If the saved position is no longer on a connected monitor, the window opens fully visible on the primary monitor.
- [ ] The app always opens windowed, including after being closed in fullscreen, and then uses the pre-fullscreen windowed position.
- [ ] Volume, mute, Audio Delay, chosen Capture Card, resolution override, frame rate handling, V-sync and always on top are restored on the next start and reflected in the menu.
- [ ] Settings are stored in one small human-readable file under the user's roaming app-data folder, found via the system API, with a format version.
- [ ] Settings are saved on exit and shortly after changes, using write-to-temporary-then-rename.
- [ ] A missing, empty, corrupt, truncated or wrong-version file, and any invalid or out-of-range value, results in defaults for the affected values with no error shown and no crash.
- [ ] A remembered Capture Card that is not present falls back to the automatic choice without overwriting the saved preference; a remembered resolution the device no longer offers falls back to the default for that run.
- [ ] Unit tests cover: round-trip of every setting; defaults when the input is empty; each kind of damage listed above; out-of-range clamping or defaulting per value; unknown keys ignored; unknown version handled; and the on-screen check for a saved window rectangle against a set of monitor rectangles.

## How to verify

Yourself: build, run the unit tests, then run the app, change settings, exit, inspect the file, restart and confirm from your debug log that the values were loaded. Corrupt the file by hand and confirm the app starts with defaults and rewrites it.

For the user. End your work by reporting this checklist:

1. Move and resize the window, close GamePlayer, open it again. Is it where you left it?
2. If you have two monitors: leave it on the second one, close, reopen. Same monitor?
3. Change the volume, mute, and the Audio Delay; close and reopen. Are they as you left them?
4. Turn on View > Always on top; close and reopen. Still checked?
5. Close GamePlayer while in fullscreen and reopen. Does it open as a normal window?
