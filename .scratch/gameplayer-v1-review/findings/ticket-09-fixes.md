# Fixes: ticket 09, Remembered settings

- Reports checked: `ticket-09-claude.md` (Claude F1-F5, outside note), `ticket-09-sol.md` (Sol F1-F3, outside the repo), items passed on by tickets 02, 07, 08.
- Code checked at 114ff7e plus these fixes.
- Unit tests: `test.bat` passed, 290 cases, 6310 assertions (was 287 / 6241; with the new tests before the fixes 3 cases red, 47 assertions).
- Build: `build.bat` clean, no warnings.
- Smoke runs: four `--run 10` runs, temporary settings files in the session scratchpad, no other GamePlayer running. Details at the end.

Files changed: `src/core/settings_file.h/.cpp`, `src/core/window_shape.h/.cpp`, `src/platform/settings_store.h/.cpp`, `src/app/main.cpp` (remembered-settings section, `ApplyAllSettings`, start-up show), `tests/settings_file_test.cpp`, `tests/window_shape_test.cpp`, `docs/DEVELOPING.md`.

The file format is unchanged (`version=1`, same keys). Every file that read before reads the same now, except a card name or identifier with overlong UTF-8 (Windows never writes that).

## Per finding

### Sol F1: restoring a snapped window changes its saved size
- Verdict: accepted. Should fix (Sol: Blocker; the window still opens at the right place, only the height changes).
- Why: traced. `WindowNow` saves the current rectangle on purpose ("snapped or not"), then `RestoredWindowRect` reshaped it to 16:9 from its width, and `RestoreWindow` reshaped it again after the move. The ticket says same size; `DEVELOPING.md` already says snaps keep their shape with black bars. The old test "is reshaped from its width" locked the bug in. Claude missed it (said the snap was kept).
- Change: `window_shape.cpp` `RestoredWindowRect` keeps the saved size; only a Video Area below the minimum is shaped to the minimum. `main.cpp` `RestoreWindow` no longer reshapes after the move: on the monitor it was saved on the frame is the one it had, so a 16:9 window comes back 16:9 (the ticket: no custom DPI handling unless a problem is seen).
- Cost: if the user changes a monitor's scale between runs, the window may come back a few pixels off 16:9 (thin black bars until an edge is dragged). Hand check 3.
- Tests: "a Video Area that is not 16:9 (a snap) keeps its size" (red before), plus a left-half snap.
- Smoke: `window=100,100,1100,1000` reopened at exactly that, Video Area 984x841, written back unchanged.

### Sol F2: rejected placement still opens maximised
- Verdict: accepted. Should fix (Sol: Blocker; the window is fully visible, but the ticket says default size).
- Why: traced. `RestoreWindow` returned nothing; `openMaximised` read `loaded.window->maximised` even after the rejection.
- Change: `RestoreWindow` returns whether the position was restored; maximised only then.
- Smoke: off-screen `maximised=true` file: log "not fully on a connected monitor (2); default size", file written back `maximised=false`.

### Claude F1: a failing save is retried every second, forever
- Verdict: accepted, Should fix.
- Why: traced. Only `Written` changed state; each second ran create, write, flush, rename on the window thread. Sol listed it under "fine"; the flush every second for a whole session is still a real weakness.
- Change: `SettingsSaveTiming::Failed(text)`: the same text is tried again only after `kRetrySeconds` (30); a new change is tried once it settles, as before; exit and session end still try (`Differs` unchanged). `main.cpp` `WriteSettings` calls it.
- Tests: "a failed write is tried again only after a while, not every second" (red before: 29 extra tries), "after a failed write, a new change is tried once it settles". The old test that locked in every-second retries was replaced.

### Claude F2: logged error code may be the wrong one
- Verdict: accepted, Nit, cheap.
- Change: `WriteSettingsFile(path, text, error)` takes the error right after the step that failed, before `CloseHandle` / `DeleteFileW`; `main.cpp` logs it.

### Claude F3: same-name stand-in learnt as the chosen card
- Verdict: rejected.
- Why: Source picks the same-name card when the identifier is absent (`capture_choice.cpp` id, then name). That is the designed fallback for a card whose identifier changed (other USB port). With the remembered identifier absent, "same card, new identifier" and "other identical card" cannot be told apart; learning keeps the next start on the card now in use. Needs two identical cards, one user, one card.

### Claude F4: launching minimised loses the maximised state
- Verdict: accepted, Nit.
- Change: `main.cpp`: a minimised start with a remembered maximised window uses `SetWindowPlacement` (showCmd `SW_SHOWMINIMIZED`, `WPF_RESTORETOMAXIMIZED`), so it restores maximised and the next save keeps `maximised=true`. Hand check 5.

### Claude F5 + Sol F3: overlong UTF-8 accepted; small test gaps
- Verdict: accepted (Sol: Should fix; Claude: Nit). Nit by effect: only a hand-edited or damaged file.
- Why: proven. New test with `C0 AF`, `C1 BF`, `E0 80 AF`, `F0 80 80 AF`, `C0 80`, in both card fields: red before (decoded to '/' or NUL).
- Change: `settings_file.cpp` `WideOf` rejects a value below the shortest form for its length (covers C0 / C1 leads).
- Tests added: overlong forms; shortest forms at each length still read; lone surrogate in a name gives the automatic choice; invalid `maximised` keeps the window, not maximised; `maximised` without `window`; coordinates at +-100000 kept, 100001 rejected.
- Smoke: damaged file with an overlong name: Capture Card automatic, file rewritten with defaults.

### Claude outside note: picking the card already shown copies this run's resolution fallback
- Verdict: already fixed by ticket 08 (`HandleMenuCommand`: the "showing" path no longer touches the resolution). Checked.

### From ticket 07: `ApplyAllSettings` should use `Sound::SetAudio`
- Verdict: accepted. One call, one log line, when any of the three differs. Smoke log: `Audio: volume 20% muted, Audio Delay 150 ms (no sound playing)`.

### From ticket 07: off-step audio values snap; remembered mute is true silence
- FYI. Nothing needed on load; parser still clamps to range.

### From ticket 08: save path with card and resolution choices
- Verdict: checked, holds. The fallback callback resets only `app.settings.resolution`; `remembered` is what is written. Picking the card shown changes name and identifier only. Picking another card resets the remembered resolution (the user's choice: the list belongs to a card). `--rate` changes only `settings`.
- Smoke: unknown card plus `resolution=1280x720`: "chosen Capture Card ... is not present; automatic choice", file kept the card, identifier and 1280x720.

### From ticket 02: Pro first, then any GC553, then first
- FYI. The automatic choice leaves `captureCard` empty, so `LearnCaptureCardId` stores nothing. Checked.

### Own check: a settings file briefly held open by another program
- Verdict: accepted, small. `ReadSettingsFile` returned "" on a sharing violation, so the app started with defaults and wrote them over the user's file 2 s later.
- Change: `settings_store.cpp` retries a sharing or lock violation up to 10 times, 50 ms apart, at start-up only. A file that stays unreadable still gives defaults and is replaced (the ticket's rule).

## Smoke runs

1. Missing file (folder too): defaults, `window default`, saved 2 s after launch, folder created.
2. Damaged (`window=garbage`, `volume=loud`, overlong name, `frame_rate=45`): all defaults, rewritten.
3. Off-screen, maximised: default size centred on primary, not maximised; written `maximised=false`, volume 40 kept.
4. Valid, non-16:9 window, unknown card, 1280x720, 30, muted 20%, 150 ms, FPS counter on: window kept at 1000x900, card fell back to automatic, every value written back as loaded.

## Hand checks for the user

1. Snap the window to half the screen. Close, reopen. Same place, same size?
2. Close maximised on monitor 2. Unplug monitor 2. Reopen. Normal window on monitor 1, not maximised?
3. Change a monitor's scale between runs. Window comes back near 16:9? Thin bars are expected.
4. Shortcut set to Run: Minimised, close maximised first. Start from it, click the taskbar. Opens maximised?
5. The ticket 09 checklist (five items) is still open.

## Passed to other tickets

- Ticket 05, `src/app/main.cpp` `RestoreWindow` / `src/core/window_shape.*`: a restored window now keeps its saved size (no 16:9 reshape at start-up, before or after the move). Keep that if the window shape code changes; dragging still keeps 16:9.
- Ticket 05: per-monitor DPI on restore is untested by hand (hand check 3).
