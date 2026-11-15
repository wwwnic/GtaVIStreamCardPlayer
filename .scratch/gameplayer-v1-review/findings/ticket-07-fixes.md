# Fixes: ticket 07, Audio controls

- Reports checked: `ticket-07-claude.md` (Claude F1-F7, outside-scope notes), `ticket-07-astra.md` (Astra F1-F6, outside the repo), items passed on by tickets 03 and 08.
- Code checked at e79262a plus these fixes.
- Unit tests: `test.bat` passed, 287 cases, 6241 assertions (was 277 / 5709 per the brief; 282 / 6210 with the new tests before the fixes, 7 cases red).
- Build: `build.bat` clean, no warnings.
- Smoke run: `--run 20`, temporary settings file, no other GamePlayer running. Loading -> Playing in 0.4 s. Sound: first second fill avg 12.3 ms (min 0.0 = priming, max 20.0); then every second avg 15.0, min 15.0, max 15.0, rate +0 ppm, 0 underruns, 0 overruns. The ticket 03 "min 5 ms" dip did not show.

Files changed: `src/core/audio_controls.cpp`, `src/core/sound_playout.h/.cpp`, `src/core/drift_correction.h/.cpp`, `src/core/shown_texts.h/.cpp` (new), `src/core/feedback_line.*` (removed), `src/platform/presenter.h/.cpp`, `src/platform/text_overlay.h` (types only), `src/platform/feedback.h/.cpp`, `src/platform/sound.h/.cpp`, `src/app/main.cpp` (`PressAudio`), `CMakeLists.txt`, `tests/sound_playout_test.cpp`, `tests/drift_correction_test.cpp`, `tests/audio_controls_test.cpp`, `tests/shown_texts_test.cpp` (new), `tests/feedback_line_test.cpp` (removed), `docs/DEVELOPING.md`.

## Per finding

### Claude F1 + Astra F1: a new playout starts at full gain when muted or turned down
- Verdict: accepted. Should fix (Astra: Blocker). Real at every Feed start and restart while muted, but one period (~10 ms) fading down.
- Why: traced. `Sound::Start` builds a new `SoundPlayout` (applied gain 1.0), then `SetGain(0)` sets only the wanted gain; the first period faded 1.0 -> 0. Test "a gain set before the sound starts holds from its first sample" failed (peak 0.499 at gain 0.2, sound at gain 0).
- Change: `sound_playout.cpp` `Pull`: every start of playback sets the applied gain to 0, so the first period fades in from silence to the gain wanted now. Covers the first start, underrun resume, new output rate and the Audio Delay re-prime. Initial `appliedGain_` is 0.
- Also fixes the ticket 03 passed item (below) and Claude's outside note "first start jumps from silence in one sample".
- Tests: the new one above; "the sound fades in from silence when it starts and when it resumes after an underrun" (failed before: 0.5 at frame 0). Three older tests checked frame 0 of the first period at full level; they now check the fade-in ramp and the last frame.

### From ticket 03: after an underrun the sound resumes at full gain, no fade-in
- Verdict: accepted, same fix as F1. The cut into the underrun itself stays a step (the sound is gone before it can fade).

### From ticket 03: drift correction trim cleared by an Audio Delay change
- Verdict: accepted.
- Why: the re-prime after J / K went through `correction_.Reset`, which clears the learnt trim. The two clocks are the same, so the trim is still right. Proven: new test "right after an Audio Delay change the fill sits at the new target, not minutes later" (10 min learnt, then 0.1 / 0.25 s delay at +-300 ppm): worst 1 s fill error 2.5 / 2.7 ms in the two minutes after, before the fix. Under 1 ms after.
- Change: `DriftCorrection::Resume(fill)` restarts from the fill but keeps the trim. The playout uses it only for the re-prime that follows an Audio Delay change (`keepDrift_`); a new output rate, underrun or first start still `Reset`. The Audio Delay sessions still pass at 1 ms.
- Test: "picking up again at a new target (Audio Delay) keeps the clock difference learnt" (drift_correction_test).

### Astra F3: one volume key can publish a transient level
- Verdict: accepted. Nit by effect (Astra: Blocker).
- Why: traced. `PressAudio` called `SetMuted` then `SetVolume`, each publishing a gain and writing a log line in between. Muted 5% -> Down gave a moment of 5% before 0%. Only that case is louder than both ends, and only 5%.
- Change: `Sound::SetAudio(settings)` sets all three and publishes once, one log line; `PressAudio` uses it. The single setters stay for the menu / settings.

### Claude F2 + Astra F5: a held key at a limit presents an unchanged picture
- Verdict: accepted, Should fix.
- Why: traced. `ShowText` with a duration always repainted; key repeat at a limit sends the same line ~30 times a second.
- Change: the same brief text again only restarts its time, no present (in `ShownTexts::Show`, below). The presenting thread wakes at the old end, finds nothing due, waits again.
- Astra's second point also accepted: `repaint_.exchange(false) || EndExpiredTexts()` skipped the expiry check when a repaint was pending, so an expired line could be drawn once more, then cleared with a second present. `presenter.cpp` `Run` now evaluates both.
- Tests: "the same brief text again only moves its end: no drawing", "the same text after it has ended is drawn again".

### Claude F3 + Astra F4: `FeedbackLine` no longer decides the timing
- Verdict: accepted, Should fix (Astra: Blocker).
- Why: confirmed by search: only `kShowMs` was used; the Presenter had its own untested bookkeeping on `GetTickCount64`.
- Change: new pure `src/core/shown_texts.*` (`ShownTexts`): the texts per place, when brief ones end, whether a change needs a present, with the clock passed in. The Presenter keeps one under its text lock instead of its two arrays. `FeedbackLine` and its tests removed; 1500 ms is `Feedback::kShowMs`. `DEVELOPING.md` module list updated.
- Tests: `tests/shown_texts_test.cpp`, 8 cases (show / expire on the boundary, replace restarts time, same text, lasting texts, several places, next-frame texts).

### Claude F4 + Astra F2: the line is top centre, not a corner
- Verdict: rejected as a code change (Astra: Blocker); doc fixed; corner deferred to the user.
- Why: the later decision names `TextPlace::Top`, which is centred. It keeps off the picture's centre and the FPS counter. `DEVELOPING.md` said "top-right": corrected to "centred at the top". `feedback.h` comment says so too.
- User decision below.

### Claude F5: straight-line volume
- Verdict: deferred to the user's ear.
- Why: arithmetic true (100 -> 95% is 0.45 dB). Whether a step near the top is heard is a hand check.

### Claude F6: steps do not return to the grid from an off-grid value
- Verdict: accepted, Nit, cheap.
- Change: `audio_controls.cpp` `NextStep`: a key goes to the next multiple of the step in its direction (42 -> 45 / 40; 30 ms -> 50 / 0). On-grid values move one step as before.
- Test: "from a value off the steps (a hand-edited settings file) the keys go to the next step" (failed before: 47, 37, 80, 430).

### Claude F7: audio keys ignore Ctrl and Shift
- Verdict: rejected. No Ctrl or Shift shortcut exists; Ctrl+M acting as M adds no hotkey. Revisit if one is added.

### Astra F6: window thread waits for logging on an audio key
- Verdict: rejected.
- Why: the window thread is not a real-time loop; a log line is a short file write. The playout lock is held by the render thread for one period's resampling (microseconds). Nothing measured. A queued logger would be a refactor. `SetAudio` halves the lines per press anyway (two, was up to three).

### From ticket 08 / Claude outside note: "Audio delay" in the feedback line
- Verdict: accepted. Now "Audio Delay ..." (glossary, menu). Tests updated.

### From ticket 03: `periodNeed_` in `Push`; ticket 08: audio menu ids
- FYI only; read, nothing to do. The overrun margin still uses the new target once the fade period has run.

### Claude outside notes, not in this ticket
- `main.cpp` resolution fallback message uses the feedback line (1.5 s, replaced by an audio key): ticket 08's choice, left.
- `sound.cpp` log line per audio change on the window thread: see Astra F6.
- `feed_keeper.cpp` `Sound::Stop` joins threads on the window thread: ticket 03 area, left.
- `sound.h` "call from one thread" relies on the FeedKeeper's thread: true, left as is.

## Hand checks for the user

1. Mute (M), then unplug and replug the card. Any tick when the sound comes back? There should be none.
2. Volume 20%, restart the app. First sound too loud? Should not be.
3. Press K a few times. Soft dip, no click? Sound lags more each time? J back to 0: in step?
4. After K, listen a minute: sound steady, no slow drift? (Log: `Sound:` fill avg near the new target at once.)
5. Hold Up at 100% for 3 s on a still picture. Log `Stats:` presented stays about 0/s? Line stays, then goes 1.5 s after release?
6. Press Up / Down one step near 100%. Is each step heard? (Claude F5.)
7. Top centre line: out of the game's way? (Claude F4.)
8. From muted 5%, press Down: silence the whole time?
9. The ticket 07 checklist (five items) is still open.

## Decisions for the user

- Feedback place:
  - A) As now: top centre.
  - B) Top-right corner (the ticket's word). Needs a new text place.
  - Left at A.
- Volume curve:
  - A) As now: straight line.
  - B) Tapered (e.g. square of the fraction), if top steps sound like nothing.
  - Left at A until the hand check.

## Passed to other tickets

- Ticket 09, `src/app/main.cpp` `ApplyAllSettings`: still calls `SetVolume`, `SetMuted`, `SetAudioDelayMs` one by one. Harmless at launch (no sound yet), but `Sound::SetAudio` now does it in one step and one log line.
- Ticket 09: off-grid volume / Audio Delay from the file is no longer a problem for the keys (they go to the next step); no rounding on load needed.
- Ticket 09: a remembered mute now gives no sound at all at start (the playout fades in to the gain set).
