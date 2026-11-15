# Fixes: ticket 03, Feed sound

- Reports checked: `ticket-03-claude.md` (Claude F1-F6), `ticket-03-astra.md` (Astra F1-F6, outside the repo), and the item passed on by `ticket-06-fixes.md`.
- Code checked at 55b7a82 plus these fixes.
- Unit tests: `test.bat` passed, 273 cases, 5692 assertions (was 261 / 4847). Before the fixes, 11 of the new or tightened cases failed and one crashed (SIGSEGV).
- Build: `build.bat` clean, no warnings.
- Smoke runs: `GamePlayer.exe --run 20` (and two of `--run 12`) with temporary settings files; no other GamePlayer was running. Output: the monitor's HDMI audio, 48 kHz, 10 ms periods both sides (no low-latency period offered), buffers 22 ms.
  - Before: fill 15.0 ms (min 15.0, max 15.0), rate +0 ppm, underruns 0, overruns 0, every second.
  - After, runs 2 and 3: the same, to the digit.
  - After, run 1: fill avg 14.2 to 14.7 ms, min 5.0 to 5.5, max 15.1 to 15.5, rate -31 to -68 ppm, underruns 0, overruns 0. This run started with the capture packets and the output periods almost at the same instant, so about one period in twelve was served just before its packet came (5 ms left instead of 15). Which start a run gets is chance, not the change: nothing in the priming changed, and the next two runs were flat. The drift correction was slowly raising the fill in answer (max 15.1 to 15.5 over ten seconds). See hand check 2.
  - No "runs without Pro Audio priority" line: both audio threads got MMCSS.

Files changed: `src/core/drift_correction.h`, `src/core/drift_correction.cpp`, `src/core/resampler.cpp`, `src/core/sound_playout.h`, `src/core/sound_playout.cpp`, `src/platform/sound.cpp`, `tests/drift_correction_test.cpp`, `tests/resampler_test.cpp`, `tests/sound_playout_test.cpp`. No file of another ticket was touched.

Sound Latency: the target fill is still 15 ms. Nothing waits, allocates or logs inside the capture or render loops that did not before (one log line per audio thread at its start, only if Windows refuses the priority). The overrun limit is unchanged for ordinary outputs (pinned by a test).

## Per finding

### Astra F1: a 48 kHz Capture Card and an 8 kHz output can corrupt memory
- Verdict: accepted (Blocker as rated, though only for an output more than four times slower than the card: 8 kHz or 11.025 kHz from 48 kHz, such as a hands-free Bluetooth headset).
- Why: proven. The new test crashed with SIGSEGV before the fix. For a step above 4, `Process` erased more frames than `InputNeeded` had asked for.
- Change, `resampler.cpp` `InputNeeded`: also asks for the frames skipped on the way to the next period, so one frame before the next position is always there. Steps of 4 or less are unchanged.
- Tests: "a large step (48 kHz to 8 kHz) asks for all the input it skips, period after period" (also checks the output values), "a large uneven step (48 kHz to 11.025 kHz) consumes input at exactly its step".

### Astra F2: capture can dereference a missing WASAPI service during startup
- Verdict: accepted (Should fix; a crash, but the window is the instant between opening the endpoint and the thread's first call).
- Why: traced. `CaptureLoop` ignored the result of `GetService` and called through the pointer.
- Change, `sound.cpp`: `Sound::Start` takes the capture service before anything is started and returns false with a log line if it fails (FeedKeeper then shows the "no sound" note and the picture carries on). The capture thread is given the service, the same way the render thread already was.

### Astra F3: long output periods can keep playout permanently silent
- Verdict: accepted (Should fix, not a Blocker: needs an output that takes about 40 ms or more at a time; the outputs seen here take 10 ms).
- Why: proven by a failing test: with 40 ms pulls nothing was ever heard, 799 overruns in 40 s. The overrun limit (target + 40 ms) was below what such a period needs to start (target + 40 ms + 2 frames).
- Change, `sound_playout.cpp` `Push`: the margin is the larger of 40 ms and twice what the latest `Pull` needed plus 10 ms. For periods up to about 15 ms that is still 40 ms. Never beyond the ring's capacity.
- Tests: "an output that takes 40 ms at a time still starts playing, and keeps playing", "... recovers from an underrun", and "an ordinary output keeps the overrun limit it had: 40 ms above the target".

### Astra F4: a missed notification registration leaves sound on the previous default
- Verdict: accepted (Nit by likelihood, not a Blocker: `RegisterEndpointNotificationCallback` has to fail first; the fix is three lines).
- Why: traced. With no watcher only a failed output woke the control thread.
- Change, `sound.cpp` `ControlLoop`: when there is no watcher, the control thread compares the default output with the open one every 2 s (`FollowDefaultOutput`, which does nothing when it is the same device). With a watcher nothing changes.

### Claude F1: the drift controller is proportional-only, so a steady clock difference leaves the fill off target
- Verdict: accepted (Should fix).
- Why: proven by failing tests: 3 ms off at 300 ppm, 8 ms at 800 ppm, for as long as the session lasts. On this machine and output the drift read 0 ppm in the smoke runs, so nothing was lost here; another output (a USB headset) has another clock.
- Change, `drift_correction.*`: a slow trim (60 s) learns the steady clock difference and takes it over from the proportional correction, so the fill returns to the target itself. Same 0.1% bound on the rate. The trim does not learn while the rate is at its bound (no wind-up), and `Reset` clears it. With the 2 s smoothing and the 10 s correction all three settle without swinging (the roots are real: about 3 s, 9 s and 47 s).
- Tests: "a steady clock difference leaves no lasting fill error, either way" (under 0.5 ms after five minutes at 300 and 800 ppm, never further off than before on the way, rate equal to the clock difference), "a long spell far off the target is not remembered once the fill is back", "starting over forgets the clock difference learnt so far". The settled-error checks in the older drift and playout sessions went from 5 ms to 1 ms or less.

### Claude F2: no test checks the 15 ms target against late packets or other output periods
- Verdict: accepted.
- Change, `tests/sound_playout_test.cpp`: a second simulation (`SimulateUneven`) with late capture packets, any output period and any clock difference. New cases: a whole packet late with equal clocks; three quarters late at 50 and 300 ppm either way; 128-frame output periods, steady and with late packets.
- What the tests found, and it is NOT fixed: the target's stated reason ("outlasts one late capture packet, 5 ms of margin") holds only while the output takes 10 ms periods in step with the packets. A packet a whole 10 ms late still gives an underrun when the card's clock is the slower one (12 in seven simulated minutes at 300 ppm, 2 at 50 ppm, with a late packet every 3 s) or with 128-frame output periods (every time). Three quarters late is covered everywhere. The trim of F1 does not change this: the cause is the packets sliding against the periods, not the offset. Only a higher target would, and that is a Latency decision. The comment at `kTargetFillSeconds` now says so. See "Decisions for the user".

### Claude F6 + Astra F5: no low-pass filter when the output rate is lower than the card's
- Verdict: deferred (Nit for the outputs in use; not a Blocker).
- Why: true as arithmetic. For 48 kHz to 44.1 kHz only content above 20 kHz folds back, to above 20 kHz: not heard. It becomes audible only on outputs of 32 kHz or less (hands-free headsets, which sound poor whatever is done). A proper filter with state across periods is a new piece of the resampler, not a review fix.

### Astra F6: an overrun splices old interpolation history into the new sound
- Verdict: rejected.
- Why: true, the resampler keeps three frames (62 microseconds) from before the skip. But an overrun drops 40 ms or more of sound at that point; the skip itself is the audible event, and three frames of the old side add nothing to it. Resetting the resampler there would put silence into the window instead, no better.

### Claude F3: with no output open, every capture packet beyond the margin counts as an overrun
- Verdict: rejected (Nit, skipped).
- Why: true, about 25 a second. But the same log line already ends "(no output device)" when it happens, so the figure cannot be mistaken for glitches, and nothing is heard. Counting them apart means a change to `Push` and to the test that pins an overrun before the first pull.

### Claude F4: an output that cannot be opened is retried every 2 s with full logging
- Verdict: rejected (Nit, skipped).
- Why: true, four lines per try. A quiet retry needs a silent mode through `OpenStream` and its ten log lines; a longer pause between tries would bring the sound back later once the other program lets go. The log is rewritten at every start. Not worth either cost.

### Claude F5: the audio threads do not check COM initialisation and MMCSS
- Verdict: accepted (Nit, cheap).
- Change, `sound.cpp`: `ComScope` leaves COM only if it was entered (capture, render and control threads). `AudioThreadScope` logs once, at the thread's start, when "Pro Audio" priority is refused.

### From ticket 06 (Astra's ticket-06 F4, second half): no note in the Video Area while there is no output device
- Verdict: deferred to the user.
- Why: true as traced: `Sound::Start` returns true with no output, so `SoundMissing()` is false and nothing shows; the log says "no default output device ... waiting for one" and the stats line "(no output device)". It is the documented design of `sound.h`. A note needs a new wording ("No sound from the capture card" would be wrong), an atomic "has output" value in `sound.*`, and a second note state in `video_area_state.*` and `feed_keeper.cpp`. A PC with no output device at all is also a rare place to be playing. Wording and whether it is wanted: user decision.
- The FYI items were checked: the sound is started by the first frame after every restart, and `Apply` shows the state before `StartSound`. Nothing in `sound.cpp` depends on the old order.

### Outside scope, from Claude's report
- No fade-in after an underrun (`sound_playout.cpp`, gain): passed to ticket 07.
- `Sound::Start` blocks the window's thread while it opens two endpoints: ticket 06 already put `ShowState` before `StartSound`; moving the open to a worker is a redesign. Not done.

## Hand checks for the user

1. Play for an hour. In the log, the minute lines: `underruns 0, overruns 0`? `fill avg` near 15 ms? Note the `rate ... ppm` figure: that is the real clock difference of this card and output.
2. Look at `min` in those lines. Around 15: fine. Around 5 (as in one smoke run): the packets and the periods started almost together. Any underruns in such a run? Any crackle? Send that log.
3. Switch the Windows default output (speakers to headset) while playing. Sound moves within two seconds? Back again?
4. If there is a USB or Bluetooth headset: play ten minutes on it. Same log questions. Its `rate ppm` will differ.
5. Console off, then on: sound comes back with the picture?
6. Unplug the card's USB while playing, plug it back: no crash, sound returns?
7. Close the Viewer while playing: closes at once, no hang?
8. The ticket 03 checklist is still open.

## Decisions for the user

- The 15 ms target does not cover a capture packet a whole 10 ms late in every case (Claude F2, above). It does on this machine as measured (equal clocks, 10 ms periods). Raise the target (20 ms covers it; 5 ms more Latency), or leave it and watch `underruns` in hand checks 1, 2 and 4? Left at 15 ms.
- A "no output device" note in the Video Area: wanted, and with which words? (From ticket 06.)
- Low-pass filter for outputs below 44.1 kHz (hands-free headsets): wanted? (Claude F6, Astra F5.)

## Passed to other tickets

- Ticket 07, `src/core/sound_playout.cpp` `Pull`: after an underrun the sound resumes at full gain with no fade-in, unlike the Audio Delay re-prime (Claude, outside scope).
- Ticket 07, `src/core/drift_correction.*`: `Reset` now also clears the learnt trim. An Audio Delay change re-primes and so resets it; the fill then needs a few minutes to sit exactly on the new target again (it is within the old offset meanwhile). The Audio Delay sessions in `tests/sound_playout_test.cpp` now check 1 ms instead of 5 ms.
- Ticket 07, `src/core/sound_playout.cpp` `Push`: the overrun margin now also looks at what the latest `Pull` needed (`periodNeed_`); unchanged for 10 ms periods, Audio Delay included.
- Ticket 06 (done), `src/platform/feed_keeper.cpp`: `Sound::Start` can now also return false for a failed capture `GetService`; handled like every other false (note shown, picture carries on). Nothing to do.
