# Fixes: ticket 05, Fullscreen and window behaviour

- Reports checked: `ticket-05-claude.md` (Claude F1-F4, outside notes), `ticket-05-sol.md` (Sol F1-F3, outside the repo), items passed on by tickets 02 and 09.
- Code checked at 0c6d6fe plus these fixes.
- Unit tests: NOT RUN. Shell access was denied in this session (Bash disabled, Desktop Commander `start_process` refused by the permission classifier). New tests written, not executed.
- Build: NOT RUN, same reason. The code was written to compile but is unverified.
- Smoke run: not done (no shell).
- Commit: not made (no shell for git).

Files changed: `src/core/window_shape.h/.cpp`, `src/app/main.cpp` (`ScalingChanged`, `WM_DPICHANGED`), `src/platform/fullscreen.cpp` (`Enter`), `src/platform/keep_awake.cpp` (`Take`), `src/platform/single_instance.h/.cpp`, `tests/window_shape_test.cpp`, `docs/DEVELOPING.md`.

## Per finding

### Claude F1: leaving fullscreen puts a snapped window at its pre-snap place
- Verdict: accepted, Should fix.
- Why: traced. `Enter` saved only `GetWindowPlacement`; for a snapped (arranged) window `rcNormalPosition` is the pre-snap rectangle, and `Leave` restores it with `SetWindowPlacement`. `WindowNow` (main.cpp) already uses `GetWindowRect` for this reason, but in fullscreen it reads `WindowedPlacement`, so the remembered place was pre-snap too.
- Change: `fullscreen.cpp` `Enter`: when not maximised, `saved_.rcNormalPosition` is set to the current window rectangle (converted to workspace coordinates, the inverse of main.cpp's `ScreenFromWorkspace`). One root fix: `Leave` returns to the snapped rectangle, still via `SetWindowPlacement` (keeps its "back onto a monitor" behaviour), and `WindowNow` in fullscreen gets the same rectangle with no change in main.cpp. Maximised windows unchanged.
- Cost: after F11 twice the window is at its snapped place but no longer "arranged" (a later drag does not shrink it back to the pre-snap size).
- Tests: platform code, none. Hand check 1.

### Claude F2 (= ticket 02 N6): `WM_DPICHANGED` not handled
- Verdict: accepted, Should fix.
- Why: traced. The process is per-monitor v2; `DefWindowProc` does not apply the suggested rectangle, so after a move to a monitor with other scaling the frame (title bar, menu bar) changes, the outer size does not, and the 16:9 Video Area is lost until the next edge drag. The minimum (`MinVideoAreaWidth`) also changes with DPI.
- Change: `window_shape.*` new pure `AfterScalingChange`: a Video Area that was exactly 16:9 (whole blocks) is reshaped like a right-edge drag (top-left kept, minimum kept); any other shape (a snap) is left as Windows suggests. `main.cpp` `ScalingChanged` on `WM_DPICHANGED`: skipped in fullscreen (the cover stays) and minimised; applies the suggested rectangle; then, unless maximised, measures the new frame and applies `AfterScalingChange`. Logs `Window: scaling now N dpi, Video Area WxH (was WxH)`. Only once the App exists, so start-up restore (ticket 09: saved size kept, measured on its own monitor) is untouched.
- Tests: "moved to a monitor with other scaling, a 16:9 Video Area stays exactly 16:9" (with a minimum subcase), "... any other shape (a snap) is left as Windows suggests". Red before (function did not exist). Not executed (see top).
- Hand check 3.

### Claude F3 + Sol F2 (merged): another launch lost during slow start or during closing
- Verdict: accepted. Should fix (Sol: Blocker; never two copies, only a launch that does nothing visible).
- Why: traced. The second copy polled `FindWindowW` 3 s, then exited. Slow start past 3 s: nothing. More likely: launching while the first copy closes (window already hidden at `SW_HIDE`, Source and Presenter stopping): found the hidden window, focus did nothing, exited; user's launch lost.
- Change: `single_instance.cpp`: the first copy now owns the mutex (`bInitialOwner` TRUE, main thread, until exit). Another launch loops up to 10 s: if the mutex becomes free or abandoned (the old copy ended) it takes over and starts normally; if the running window is visible it brings it forward and exits; a hidden window (starting or closing) is waited on. `single_instance.h` and `DEVELOPING.md` updated.
- Hand check 4.

### Sol F3: mutex creation failure allows a second Viewer
- Verdict: accepted, Nit level (Sol: Should fix). Cheap.
- Why: `CreateMutexW` null returned true. The realistic case is `ERROR_ACCESS_DENIED`: the mutex exists, made by a copy with other rights (run as administrator).
- Change: access denied is now treated as "a copy runs" (look for its window, never start a second). Any other failure still starts (failing closed would leave the user with no Viewer at all).

### Claude F4: `PowerSetRequest` failure log may show the wrong error
- Verdict: accepted, Nit. Cheap.
- Change: `keep_awake.cpp` `Take` logs each failing call (display, system) right after it, with its own `GetLastError`.

### Sol F1: keep-awake held up to 3 s after Playing ends
- Verdict: rejected.
- Why: true that the hold is released on the 3rd quiet second (`stay_awake.cpp`), but this is the documented debounce (`stay_awake.h`: "so a brief gap does not flap the request"), e.g. Playing to Loading on a restart. The ticket 06 decision ("held only in Playing") is about not holding while a state message shows for good, which holds: `TakePlayingSecond` gives false outside Playing, release follows within 3 s. Windows idle timeouts are minutes, so the 3 s cannot change when the PC or display sleeps. Not a Blocker; no change.

## Items passed on

### From ticket 02 (N6): `WM_DPICHANGED`
- Same as Claude F2 above: accepted and fixed.

### From ticket 09: `RestoreWindow` and `window_shape.*`
- Not undone. `RestoredWindowRect` unchanged; restore runs before the App exists, so the new `WM_DPICHANGED` handler does not act on it. DPI on restore still not hand checked (hand check 3).

### FYI from ticket 02 (`ResizeBuffers` rebuild, `Presenter::Start` retries)
- Read; nothing in this ticket depends on it.

## Outside this ticket
- Claude outside note: DPI awareness set in code, not the manifest (`src/app/main.cpp`, ticket 02). Works; Nit, not routed further.
- Claude outside note: remembered position in fullscreen pre-snap (`main.cpp` `WindowNow`, ticket 09): fixed at the root in `fullscreen.cpp` (Claude F1), no change in main.cpp needed.

## Hand checks for the user
1. Snap the window (Win+Left), F11, F11: back at the snapped half? Also maximised: F11 twice comes back maximised with title and menu bar.
2. F11 covers the screen, no border or taskbar; Esc leaves.
3. Two monitors with different scaling (e.g. 100% and 150%): drag the window across. Video Area stays 16:9, no black bars; log line `Window: scaling now`. Close there and reopen: same place and size.
4. Close GamePlayer and start it again at once (within a second): it opens (does not silently do nothing). Start it twice quickly from cold: one window, brought forward.
5. Controller-only play longer than the screen-off time: screen stays on.
6. Sleep while Playing, resume: a `Stay awake` line matches the state afterwards.
