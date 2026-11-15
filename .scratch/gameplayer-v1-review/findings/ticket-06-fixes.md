# Fixes: ticket 06, Video Area states and automatic recovery

- Reports checked: `ticket-06-claude.md` (Claude F1-F7), `ticket-06-astra.md` (Astra F1-F9, outside the repo).
- Code checked at 9f99ac5 plus these fixes.
- Unit tests: `test.bat` passed, 249 cases, 4809 assertions (was 239 / 4676).
- Build: `build.bat` clean.
- Smoke run: `GamePlayer.exe --run 15` with a temporary settings file. Loading at 51.189, Playing at 51.568, sound playing at 51.640, 60 frames a second, first open 93 ms, exited cleanly.

Files changed: `src/core/video_area_state.h`, `src/core/video_area_state.cpp`, `src/platform/feed_keeper.h`, `src/platform/feed_keeper.cpp`, `tests/video_area_state_test.cpp`. No shared file of another ticket was touched.

## Per finding

### Claude F1 + Astra F1: a pending retry survives into Playing, the Feed restarts and the sound stays off
- Verdict: accepted (Blocker).
- Why: proven by failing tests before the fix. No signal at 3 s plans a reopen at 8 s; frames at 4 s give Playing; the tick at 8 s asked for `restartFeed` in Playing. The machine stayed Playing through the restart, so the new Source's first frame did not ask for the sound.
- Change, `video_area_state.cpp`: `FramesArrived` drops the pending retry. `FeedRestarted` leaves Playing for Loading, so the first frame after any restart starts the sound again (second guard).
- Tests: "a reopen planned during No signal is dropped once frames arrive", "a retry planned by a device notification is dropped once frames arrive", "any restart leaves Playing, so the new Source's first frame starts the sound again". All three failed before, pass now.

### Astra F8: a frame delivered just before a read failure undoes the failure state
- Verdict: accepted (Should fix; worse than rated, it fed F1).
- Why: traced. At 60 frames a second `frameSinceTick_` is almost always set when a read failure is handled. The next tick then counted that old frame: back to Playing, sound started on the dead Source, then the 500 ms retry restarted the Feed in Playing and the sound stayed off. `mf_source.cpp` `Fail` requests no further read, so a Failed event really ends the frames of that start.
- Change, `feed_keeper.cpp` `HandleMessage`: a Failed event clears `frameSinceTick_` before it is applied. Platform code, no unit test; the machine-side guard of F1 covers the silent-sound outcome.

### Claude F2 + Astra F6: the Error or Card busy text keeps the old code when the failure changes
- Verdict: accepted (Should fix; not a Blocker, needs two different repeating failures in a row).
- Why: traced. `ShowState` compared the state only.
- Change, `feed_keeper.*`: `shownFailure_` kept beside `shown_`; redraw and log when either differs.
- Test: "Error follows the failure: another one repeating replaces the code shown" pins the machine's side (passed before; the bug was in the platform part).

### Claude F3: tests miss timers and failures that outlive their state
- Verdict: accepted.
- Change, `tests/video_area_state_test.cpp`: ten new cases. Besides the ones above: a first-read failure repeated with Opened in between reaches Error; Card busy to No Capture Card; a device notification while open and waiting is ignored; one during No signal reopens after 500 ms; the second No signal reopen; two resume cases.

### Claude F4: the open-and-waiting window ignores resume and can show No signal wrongly
- Verdict: accepted for resume, rejected for device removal.
- Why: traced. Resume went through `DevicesChanged`, which does nothing while a device is open and waiting, so a PC that slept then woke to No signal and a further 5 s wait. Removal while open and waiting stays ignored: opening the card can raise device notifications of its own, and the machine cannot tell which device left; the next reopen says No Capture Card.
- Change: new `VideoAreaStateMachine::Resumed` (reopen after 500 ms in every non-playing state; Playing is left to the stall rule). `feed_keeper.cpp` calls it for `PBT_APMRESUME*`.
- Tests: "resume reopens a device that was open and waiting when the PC slept", "resume leaves a Feed alone whose frames still arrive"; the old sleep test now uses `Resumed`.

### Astra F3: Loading is hidden during the first open
- Verdict: accepted (Should fix, not a Blocker: the window appeared about 0.1 s late on this card, longer on a slow open).
- Why: traced. `FeedKeeper::Start` ran the first tick, and with it the whole device open, before `main.cpp` shows the window.
- Change, `feed_keeper.cpp` `Start`: the first tick is posted, not run. `main.cpp` (ticket 02's file) is unchanged: it shows the window, then the message loop runs the tick. The Presenter draws Loading on its own thread while the open blocks the window's thread.

### Astra F4: sound start hides the first picture; a missing output shows no note
- Verdict: first half accepted, second half deferred.
- First half, traced: `Apply` opened the sound (blocking) before `ShowState` uncovered the picture. Change, `feed_keeper.cpp` `Apply`: `ShowState` before `StartSound`.
- Second half, real against the original ticket ("for example no output device"), but `sound.h` states the opposite as its design ("a missing output device is not a failure, the sound starts when one appears") and already retries every 2 s. A note needs a thread-safe "has output" value in `sound.*` (ticket 03's file) and a new wording, since "No sound from the capture card" would be wrong. Passed to ticket 03.

### Astra F2 + Claude F5: No signal reopens every 8 s, not every 5 s
- Verdict: accepted as wording only (Nit); behaviour kept.
- Why: true, the cycle is 3 s of waiting plus 5 s. Shortening it gives a reopened card less time to send its first frame, and nothing about the card's timing here is verified by hand. Not worth the risk before the HDMI check.
- Change: the comment at `kNoSignalRetryMs` says 8 s; the No signal test now checks the second reopen at that pace.
- For the user: the review ticket's "reopen every 5 s" is really "every 8 s". Say if 5 s is wanted.

### Astra F5: a resolution that worked once never falls back again
- Verdict: deferred, passed to ticket 08.
- Why: true as traced (`choiceProven_` is only reset by `SetChoice`). But it is deliberate: once a resolution has played, a failed start is taken as the card's trouble, not the mode's, so an unplug does not drop the user's choice. Claude's out-of-scope note asks for the opposite direction. This is the resolution fallback of ticket 08 and a user decision.

### Astra F7: the luma summary runs before the frame is handed on
- Verdict: rejected.
- Why: 576 byte reads once a second, microseconds, and the lock is contended only by the once-a-second probe read. Handing the frame on first would mean keeping a second reference to a pooled frame on the Source's thread, for no measurable gain. `mf_source.cpp` is ticket 02's file.

### Astra F9: a slow flush can freeze the window for 2 s per restart
- Verdict: deferred, passed to ticket 02.
- Why: true (`mf_source.cpp` `End` waits up to 2 s on the window's thread), and Claude lists the same outside scope. It never delays frames. Moving stop and open to a worker thread is a redesign, not a review fix.

### Claude F6: two different failures alternating never reach Error
- Verdict: rejected (Nit, skipped).
- Why: true, but "the same failure twice" is what keeps a card that is half removed (different failures in a row) from flashing Error. Unlikely on a real device.

### Claude F7: `NameOf(SourceStage)` lives in the state machine's file
- Verdict: rejected (Nit, skipped). `source.h` is ticket 02's and has no source file of its own.

## Hand checks for the user

1. Start GamePlayer. Does the window open at once with "Loading…", then the game with sound?
2. Switch the Xbox off, wait for "No signal", switch it on. Does the game come back with sound, and stay for 20 s without a blink or losing sound? (F1)
3. Pull the HDMI cable for ten seconds, put it back. Same question. Send the log.
4. Pull the card's USB cable while playing, put it back. Do picture and sound both return? (F8)
5. Sleep the PC while playing, wake it. Do picture and sound both return? Again while "No signal" or "No capture card found" shows. (F4)
6. Open OBS on the card, start GamePlayer, close OBS. Does the game appear with sound?
7. Pause a game for a minute. No "No signal"?

## Passed to other tickets

- Ticket 03, `src/platform/sound.cpp` `Start` / `OpenRender`: no note in the Video Area while there is no output device (Astra F4, second half). Needs a thread-safe "has output" value and a wording.
- Ticket 08, `src/platform/feed_keeper.cpp` `FallBackFrom` / `choiceProven_`: when to fall back to the default mode after a resolution has once worked (Astra F5), and the opposite case of a setting dropped on a transient failure (Claude, outside scope).
- Ticket 02, `src/platform/mf_source.cpp` `End`: flush wait of up to 2 s on the window's thread (Astra F9; Claude, outside scope).
- Ticket 02, `src/platform/mf_source.cpp` `OnReadSample`: luma summary before the frame hand-off (Astra F7), judged negligible here.
- Ticket 02, `src/core/source.h`: `NameOf(SourceStage)` is defined in `video_area_state.cpp` (Claude F7).
- Ticket 04, `feed_keeper.cpp` Processor totals and override read across threads, `text_overlay.cpp` layouts rebuilt per presented frame (Claude, outside scope; not checked here).
- Ticket 02, `src/app/main.cpp`: `FeedKeeper::Start` no longer opens the card itself; the first tick is posted and runs once the message loop starts. Nothing in `main.cpp` needed to change.
