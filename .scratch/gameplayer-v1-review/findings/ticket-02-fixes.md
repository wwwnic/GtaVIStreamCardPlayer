# Fixes: ticket 02, App skeleton and the Source / Processor / Presenter pipeline

- Reports checked: `ticket-02-claude.md` (Claude F1-F4, N1-N6), `ticket-02-astra.md` (Astra F1-F14, outside the repo), and the items passed on by `ticket-06-fixes.md`.
- Code checked at 99104d7 plus these fixes.
- Unit tests: `test.bat` passed, 254 cases, 4818 assertions (was 249 / 4809).
- Build: `build.bat` clean.
- Smoke runs: `GamePlayer.exe --run 15` with a temporary settings file, before and after. The Console showed a still picture both times, so only two frames were presented per run. Before: 60 frames a second arrived, arrival-to-present 4.39 ms (first frame) and 1.91 ms, close to stop 48 ms, device references left 0. After: 60 a second, 4.57 ms and 1.88 ms, 40 ms, 0. No difference beyond noise. A moving picture was not measured (hand check 1).

Files changed: `src/platform/presenter.cpp`, `src/platform/presenter.h`, `src/platform/mf_source.cpp`, `src/core/source.h`, `src/core/capture_choice.cpp`, `src/core/capture_choice.h`, `src/core/video_area_state.cpp` (one function moved out), `src/app/main.cpp`, `tests/capture_choice_test.cpp`, `tests/picture_fit_test.cpp`.

Nothing was added between the Source and the Presenter. The frame path gained one integer compare per frame (`CopySample`); everything else is on failure, start or stop paths.

## Per finding

### Claude F1 + Astra F1: a failed ResizeBuffers leaves the Video Area frozen for good
- Verdict: accepted (Should fix; not a Blocker, it needs a device loss that lands on a resize).
- Why: traced. `ResizeBackBuffer` dropped the back buffer view, only logged a failed `ResizeBuffers`, and `Run` then skipped every draw; only `Present` could set the device-lost flag, and it never ran again.
- Change, `presenter.cpp`: `ResizeBackBuffer` returns false when `ResizeBuffers` fails or no back buffer view can be made; `Run` then sets the device-lost flag and rebuilds (paced, see next). The frame stays in the slot meanwhile. Platform code, no unit test; hand check 3.

### Claude F2 + Astra F12: the device rebuild is retried at the frame rate
- Verdict: accepted (Should fix).
- Why: traced. The 1000 ms was only a timeout on the wake event, which every Unique Frame and text change sets.
- Change, `presenter.cpp`: `rebuildAt_` (tick count). A rebuild attempt, successful or not, forbids the next one for a second (`kRebuildEveryMs`); wakes in between do nothing. The first attempt after a loss is still immediate.

### Claude F3 + Astra F3: the picture is fitted to the capture mode's pixel shape, not to 16:9
- Verdict: deferred (needs the user's eyes). Not a Blocker: every 16:9 mode, the default included, is unaffected.
- Why: true as traced (`ShowAndPresent` passes the mode's width and height to `FitPicture`). Which is right depends on the card: if it scales the Console's 16:9 picture to fill a 720x576 or 2560x1080 mode, 16:9 is right; if it adds its own bars, the present code is. This card offers 640x480, 720x480, 720x576, 1280x800, 1920x1200 and 2560x1080. Nothing records what it does, and guessing could turn a right picture into a wrong one.
- Done now: the comments in `presenter.h` and `presenter.cpp` said "fitted 16:9"; they now say what the code does. `tests/picture_fit_test.cpp` covers the call the Presenter really makes (a non-16:9 shape), which no test did.
- After hand check 2: if circles are squeezed, the fix is one line (call `FitPicture` with its default 16:9 arguments).

### Claude F4 + Astra F2: a media type change mid-stream is only logged
- Verdict: accepted (Should fix; not a Blocker, never seen on this card: `type changes` is 0 in every `Signal:` line).
- Why: traced. Size, format and colour were read once in `Begin`.
- Change, `mf_source.cpp`: on either type-changed flag `AdoptCurrentType` re-reads the reader's current type and, if it differs, uses its size, format and colour from that frame on (logged). A type the Viewer cannot show reports one `Failed` at stage Read, so the Feed restarts and sets the chosen mode again. If the current type cannot be read, frames go on as before (no worse than it was). Runs only on a flagged sample. The Presenter and the Processor already handle a frame of another size.
- Left as is: Help > About keeps the mode set at Start after such a change.

### Astra F4 + Claude N4: a colour value the Viewer does not know is logged as "unspecified"
- Verdict: accepted as a log fix (Nit; not a Blocker). Converting or refusing such modes: rejected.
- Why: true, absent and unknown values share one branch. But no mode of this card has one (all are range 1 or 2, matrix 1 or 2), BT.2020 on an 8-bit SDR mode is unlikely, and refusing the mode would turn a slightly off picture into an Error.
- Change, `mf_source.cpp`: the colour line says `unsupported value N (treated as ...)` when the card gave a value, `unspecified (treated as ...)` only when it gave none.
- Skipped: names instead of numbers in the Presenter's own log line.

### Astra F5 + Claude N1: the frame is taken and uploaded before the wait for the swap chain
- Verdict: rejected (Nit as Claude rated; not a Blocker).
- Why: the wait only blocks when the previous present is still in flight. Today the upload and conversion run during that wait (cost: the longer of the two). Waiting first costs their sum, every time the wait blocks, to gain a newer frame only when one arrives inside the wait (frames are 16.7 ms apart; the wait is a few ms at most at 144 Hz). It also needs a peek at the slot, and a wait that is not followed by a present leaves the next wait blocking for 100 ms. Not a clear win, on the path where nothing may be added. Presenting after the 100 ms timeout is deliberate: the Presenter never stalls.

### Astra F6 + Claude N3: "any thread" promises that hold by call order
- Verdict: accepted as documentation (Nit; not a Blocker: no caller races today, `TakeProbe` is only called on the window's thread).
- Change: `source.h` `TakeProbe` now says "from the thread that calls Start and Stop". `presenter.h` says "any thread" means not while Start or Stop runs.

### Astra F7: reader setup ignores failed attribute calls
- Verdict: accepted (Should fix; a crash only if `MFCreateAttributes` fails, which is out of memory).
- Change, `mf_source.cpp` `StartReading`: the four calls are chained on their HRESULT and fail through the existing Reader stage.

### Astra F8: automatic selection matches the whole GC553 family
- Verdict: half accepted (Nit; not a Blocker, the user owns one card).
- Why: proven by a failing test: with "Live Gamer ULTRA GC553" listed before the Pro, the older card was chosen.
- Change, `capture_choice.cpp`: the Pro first ("gc553pro", "gc553 pro"), then any GC553, then the first device. Header comment updated.
- Rejected half: with no Pro, another GC553 is still chosen before an unrelated first device. A capture card of the same family is a better guess than a webcam. Pinned by a test.
- Tests: "the GC553 Pro is chosen over another GC553 model listed before it" (failed before), "without the Pro, another GC553 is still chosen before an unrelated first device".

### Astra F9: a failed wake event starts a spinning Presenter
- Verdict: accepted (Nit; `CreateEventW` does not fail in practice).
- Change, `presenter.cpp` `Start`: the event is created first and checked; without it Start logs and returns false, no thread.

### Astra F10: failed presents are counted as frames shown
- Verdict: accepted (Nit; one wrong count in the `Stats:` line at a device loss).
- Change, `presenter.cpp`: `ShowAndPresent` returns whether `Present` succeeded; a frame counts only if it was converted and presented.

### Astra F11 + Claude N5: the copy assumes tightly packed rows
- Verdict: accepted as a log line (Nit); copying row by row rejected.
- Why: `IMFMediaBuffer::Lock` gives the contiguous form by contract, and the default mode was verified. A row copy would change the frame path for a case nobody has seen.
- Change, `mf_source.cpp` `CopySample`: one log line, once, when the buffer is longer than the frame, so a sheared picture in some mode can be explained from the log.

### Astra F13: MJPEG twins compared by spelling; rates above 60; default mode ignores twins
- Verdict: first part accepted, the other two rejected.
- First, proven by a failing test: an MJPEG mode at 60000/1000 was not seen as the twin of 60/1. Change, `capture_choice.cpp` `HasMjpegTwin`: rates compared as fractions (cross-multiplied in 64 bits). Tests: "an MJPEG twin is recognised when its rate is the same number written as another fraction" (failed before), "an MJPEG mode of the same size at another rate is not a twin".
- Rates up to 60.5: deliberate. The card writes rates as fractions like 10000000/83333; a 60 written that way can land a hair above 60.
- Default mode and twins: the default 1080p60 YUY2 has no twin on this card, and refusing a twinned one elsewhere would give Error where a picture was possible.

### Astra F14: a failed graphics setup at launch leaves the Viewer black for good
- Verdict: accepted (Should fix).
- Why: traced. `Start` returned false, `main.cpp` logged, no thread existed to try again.
- Change, `presenter.cpp` `Start`: without a device the thread starts anyway in the device-lost state and tries once a second, on the same path as after a loss. `presenter.h` and the comment in `main.cpp` say so.
- Limit: while there is no device nothing can be drawn, so the Video Area cannot say why it is black; the log does, once a second.

### Claude N2: Presenter::Stop's message pump drops a quit
- Verdict: accepted (Nit).
- Change, `presenter.cpp` `Stop`: a `WM_QUIT` seen while waiting for the thread is posted again after the join.

### Claude N6: small items in main.cpp and the shader
- Verdict: one accepted, one passed on, rest rejected.
- Accepted, `main.cpp`: the "could not create the window" exit now balances COM and closes the log.
- `WM_DPICHANGED`: passed to ticket 05 (window size and shape).
- NV12 nearest chroma, no mip levels: rejected, nobody sees it with YUY2 and one monitor.

### From ticket 06 (Astra F9): flush wait of up to 2 s on the window's thread
- Verdict: deferred to the user.
- Why: true. But the 2 s is a ceiling, not the usual cost: in both smoke runs "Closing" to "Source: stopped" took 40-48 ms, sound included. It never delays frames. Removing it means stop and open on a worker thread, a redesign of `FeedKeeper` and the Source's threading rules. Worth it only if the window is ever seen to hang on a restart.

### From ticket 06 (Astra F7): luma summary before the frame hand-off
- Verdict: rejected.
- Why: 576 byte reads once a second. Moving it after the hand-off delays the next read request by the same microseconds instead.

### From ticket 06 (Claude F7): `NameOf(SourceStage)` defined in the state machine's file
- Verdict: accepted (Nit).
- Change: now inline in `src/core/source.h`, removed from `video_area_state.cpp`.

### From ticket 06 (FYI): `FeedKeeper::Start` posts its first tick
- Checked: `main.cpp` shows the window, then the loop runs the tick. Nothing to change.

## Hand checks for the user

1. Play a game for a minute. Does it feel as responsive as before? Send the log (`Stats:` lines with a moving picture).
2. Resolution menu: pick 720x576, then 2560x1080. Is the picture squeezed or widened (look at something round), or does it have the right shape with bars? (F3)
3. While a game plays, reset the graphics driver (Win+Ctrl+Shift+B). Does the picture come back within about a second? Again while dragging the window's edge. The log should show "device lost" or "ResizeBuffers ... failed", then one "device recreated". If the log shows neither, the key combination did not remove the device: `dxcap -forcetdr` from an administrator prompt does (it needs the Windows "Graphics Tools" optional feature). (F1, F2)
4. Close the window. Does the app exit straight away?
5. The ticket 02 checklist is still open: colours, shape while dragging, no card connected.

## Decisions for the user

- F3: after hand check 2, say whether non-16:9 modes should always be shown 16:9.
- Flush wait (ticket 06, Astra F9): leave it, unless restarts are seen to freeze the window.

## Passed to other tickets

- Ticket 05, `src/app/main.cpp` `WindowProc`: `WM_DPICHANGED` is not handled; the window keeps its pixel size on a monitor with another scale (Claude N6).
- Ticket 08, `src/core/capture_choice.cpp`: `HasMjpegTwin` now compares rates as fractions (its part of the file; already fixed and tested here, nothing to do). Open for 08 if wanted: the default mode choice does not exclude MJPEG twins (Astra F13, rejected here).
- Ticket 08, `src/platform/menu_bar.cpp`: `ListCaptureDevices` enumerates devices on the window's thread each time the menu opens (Claude, outside scope; not checked here).
- Ticket 08, About: after a mid-stream media type change `Source::CurrentMode` still gives the mode set at Start (new, minor, never seen).
- Ticket 04, `src/core/unique_frames.cpp`: the rate called "presented" counts what the Processor emitted, so it can exceed real presents when the slot replaces a frame (Astra, outside scope); `SetGameRateOverride` across threads (Claude, outside scope). Not checked here.
- Ticket 06 (done): `Presenter::Start` now returns true and retries when there is no graphics device; state texts appear once the device exists.
