# Fixes: ticket 04, Unique Frames and Game Rate, with the FPS counter

- Reports checked: `ticket-04-claude.md` (Claude F1-F6), `ticket-04-astra.md` (Astra F1-F9, outside the repo), and the items passed on by `ticket-06-fixes.md` and `ticket-02-fixes.md`.
- Code checked at eb50cc6 plus these fixes.
- Unit tests: `test.bat` passed, 261 cases, 4847 assertions (was 254 / 4818). Six of the seven new cases failed before the fixes.
- Build: `build.bat` clean.
- Smoke runs: `GamePlayer.exe --run 12..15` with temporary settings files, before and after. The Console showed a still picture every time, so on Auto nothing was presented; `--rate 60` was used to make every frame go to the Presenter. No Unique Frame timing could be measured (hand check 1).
  - Auto, still picture, compare of a Repeated Frame (a full 1080p scan): before avg 0.145 ms, worst 0.25 ms; after avg 0.135 ms, worst 0.23 ms.
  - `--rate 60`, counter off, arrival-to-present: before avg 1.71 ms; after avg 1.75 ms (worst 2 to 3 ms both). Noise: nothing on that path changed.
  - `--rate 60`, counter on: 1.80 ms with the old `text_overlay.cpp`, 1.80 ms with the new one. The kept layouts make no measurable difference.
  - `replaced before shown` was 0 in every line; `after present worst` 0.001 ms or less (still picture).

Files changed: `src/core/unique_frames.h`, `src/core/unique_frames.cpp`, `src/platform/feed_keeper.cpp`, `src/platform/text_overlay.cpp`, `tests/unique_frames_test.cpp`, `docs/DEVELOPING.md`.

Frame path: nothing added between the Source and the Presenter. With a tolerance set, work was removed from before the hand-off. No lock added or held longer.

## Per finding

### Claude F1: rate smoothing can hold a value one below the true rate
- Verdict: accepted (Should fix).
- Why: proven by a failing test. A 59.95 Hz signal, two Repeated Frames (reads 59), then all Unique Frames again: the Game Rate and the counter stayed at 59 for good.
- Change, `unique_frames.cpp` `RatesAt`: the shown figure follows once the real rate is 0.75 away (`kRateStep`), not 1.0. Up at x.75, down at x.25, so a steady game still reads steady.
- Test: "rates: a Signal Rate just under a whole number reads that number again after a dip". The uneven pacing test (steady 29) still passes.

### Claude F2 + Astra F1 (second part): with a tolerance, closeness is measured before the Unique Frame is handed on
- Verdict: accepted (Should fix).
- Why: proven by a failing test. With `--tolerance` under 1% the inline compare counted on to 1% of words, only for the log.
- Change, `unique_frames.cpp` `Process`: the inline compare stops at the tolerance, whatever it is. Closeness of a Unique Frame is always measured in `AfterSubmit`. With a tolerance of 1% or more a Unique Frame cannot be close, so nothing is measured.
- Test: "with a tolerance too, how close a Unique Frame was is measured after it is handed on".

### Astra F1 (first part): closeness after the hand-off delays the next read request
- Verdict: rejected (not a Blocker).
- Why: traced. `AfterSubmit` does run before `ReadSample` is requested again (`mf_source.cpp` `OnReadSample`). But the next frame is 16.7 ms away, and the worst case is three scans of a frame: a full scan measured 0.15 ms here, so about 0.5 ms. It is logged every second (`after present worst`). A worker thread would add a thread, a queue and cache traffic to save nothing a frame can feel.

### Astra F2 (also passed on by ticket 02): the presented rate counts frames handed to the Presenter, not real presents
- Verdict: deferred to the user.
- Why: true as traced. `Submit` can replace a frame nobody took, and the rate still counts both. It only differs when the Presenter is slower than the Feed (V-sync on with a slow monitor, or a stalled GPU); `replaced before shown` was 0 in every smoke line. A real fix needs the Presenter to report each successful present and a second rate history for it: a new piece of design, not a review fix.
- Done now: the comment on `FrameRates::presentedRate` says what it counts and where the difference shows (`Stats:` line, "replaced before shown").

### Claude F3 + Astra F3: the old FPS number shows during a Feed restart
- Verdict: accepted (Should fix; not a Blocker).
- Why: traced. `Reset` left the top-left text; the Loading repaint and the new Source's first frames drew the stopped Source's number. On a still new picture it would stay.
- Change, `feed_keeper.cpp`: `UpdateFpsCounter` right after `Reset` in `RestartFeed`, and in `ShowState` before the text that presents a state change. The counter is given only while Playing, so no state message (Loading, No signal) carries a number. Text only, never a present. Platform code, no unit test; hand check 3.
- This also settles Claude's outside-scope note (counter drawn over state screens).

### Astra F4: override 30 to Auto can strand a picture
- Verdict: accepted (Should fix; not a Blocker, it needs the switch to land one frame after a new picture that then stays still).
- Why: proven by a failing test: 30 shows A, B arrives too soon, switch to Auto, B is never shown.
- Change, `unique_frames.cpp` `Process`: Auto emits when the reference has not been shown, which is every Unique Frame plus that one held-back case.
- Test: "override 30 to Auto: a Unique Frame still waiting its turn is shown by its next copy" (shown once, then nothing more on the still picture).

### Claude F4 + Astra F5: a newest arrival after the caller's "now" makes the rates read zero
- Verdict: accepted (Should fix; not a Blocker, it needs the window's thread to lose the race by a hair).
- Why: proven by a failing test: all three rates read 0 on a healthy Feed.
- Change, `unique_frames.cpp` `RatesAt`: an arrival newer than the asked time is skipped, not the end of the scan.
- Test: "rates: read at a time just before the newest arrival, they are not zero".

### Astra F6: Reset leaves the old Source's rates
- Verdict: accepted (Nit by effect: About and the log, for up to two seconds; the counter was already blank).
- Why: proven by a failing test: 60 / 30 right after `Reset`, and a mixed 45 a second later.
- Change, `unique_frames.cpp` `Reset`: the rate history and the shown rates are cleared under the lock. The running totals stay (the log subtracts them).
- Test: "rates: after Reset the stopped Source's rates are gone at once".

### Astra F7: no count of Repeated Frames that were dropped, across override changes
- Verdict: rejected (not a Blocker).
- Why: on Auto, dropped equals repeated. With an override the log line gives repeated, dropped and the override itself each second. Nothing reads the missing number.

### Astra F8: the spacing counter wraps after about 829 days
- Verdict: accepted (Nit; one line).
- Change, `unique_frames.cpp`: `sinceEmitted_` stops counting at 2, the only value compared.
- Test: "override 30 after a very long still picture still shows the first new one at once" pins the behaviour (the wrap itself cannot be reached in a test).

### Astra F9: timing averages can split one sample across two log lines
- Verdict: rejected.
- Why: true, but it needs the once-a-second log read to land between two adjacent additions, and then one average is off by one sample. A coherent snapshot needs a lock or a sequence counter on the frame path, for a debug figure. The worst values are unaffected.

### Claude F5: test pictures alias every 256
- Verdict: rejected (Nit).
- Why: the helper's comment already states the 0-255 range. Only neighbouring pictures are ever compared, and neighbours always differ.

### Claude F6: `--tolerance` has no upper bound
- Verdict: accepted (Nit).
- Change: `kMaxTolerance` = 0.05 in `unique_frames.h`; the Processor's constructor clamps to it. The value in use is in every `Frames:` line. `docs/DEVELOPING.md` says "at most 0.05".
- Test: "the tolerance has a ceiling, so a mistyped one cannot freeze the picture".

### From tickets 06 and 02 (Claude): totals and override used across threads
- Verdict: rejected, checked and fine.
- Why: `override_` is atomic, read once per frame. Totals, closeness and the rate history are under the Processor's mutex. `Reset` runs only after `source_->Stop()` returned. `fpsCounter_` and `loggedTotals_` are used on the window's thread only. The mutex is taken a few times per frame on the Source's thread and once or twice a second on the window's thread for a scan of about 120 entries: no contention worth the name.

### From tickets 06 and 02 (Claude): text layouts rebuilt on every presented frame
- Verdict: accepted (Nit).
- Why: true. With the counter on, every present made a DirectWrite layout before `Present`.
- Change, `text_overlay.cpp`: the layout of each line of each place is kept and reused while text, size, width and alignment are the same.
- Measured: no difference (1.80 ms both). Kept because it is small and removes work before the present.

### Claude, outside scope: `QpcToMs` in `feed_keeper.cpp` asks the frequency each call; F toggles with Ctrl or Shift held
- Verdict: rejected (Nits). The first is called seven times a second, on the window's thread. The second is harmless.

## Hand checks for the user

1. Play a 60 fps game for a minute with the counter on (F). Does it read 60, steady? Send the log (`Frames:` lines: `before present ms unique`, `after present worst`).
2. Play a 30 fps game. Does it read 30, steady, not 29? In the log: `presented` about 30, `repeated exact` about 30. If `close unique` is about 30 instead, the repeats are not identical: say so.
3. Counter on, game moving: change the Resolution in the menu. Is the counter gone on "Loading…", blank for two seconds, then back?
4. Counter on: switch the Console off. Is "No signal" shown without a number?
5. Frame rate 30, then back to Auto, on a moving game. Picture carries on as before?
6. Lossless Scaling: same base rate with the counter on and off?
7. V-sync on: compare the counter with `Stats: presented` in the log, and look at "replaced before shown" (Astra F2).
8. The ticket 04 checklist is still open.

## Decisions for the user

- Astra F2: should the counter count real presents? Only worth building if hand check 7 shows "replaced before shown" above 0 in normal use.

## Passed to other tickets

- Ticket 08, `src/core/menu_model.cpp` About: the `Shown:` rate has the same meaning as the counter (frames handed to the Presenter; Astra F2). Rates in About now read 0 right after a Feed restart instead of the old Source's (Astra F6).
- Ticket 06 (done), `src/platform/text_overlay.cpp`: layouts are now kept per place and line; `feed_keeper.cpp` `ShowState` calls `UpdateFpsCounter` before the state text. Nothing to do.
- Ticket 09: nothing. The `fps_counter` key was read and written correctly in the smoke runs.
