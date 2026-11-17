# GamePlayer sprint 3: Frame Generation and Sharp Upscale inside the Viewer

Sprint 1 (`.scratch/gameplayer-v1/`) built the Viewer. Sprint 2 (`.scratch/gameplayer-v1-review/`) reviewed it. This sprint brings two things into the Viewer that a throwaway prototype proved worth having: Frame Generation (2x and 4x) and a Sharp Upscale. The decision and its reasons are in `docs/adr/0003-frame-generation-inside-the-viewer.md`, which replaces ADR 0002.

Each ticket is self-contained and written for an implementing agent with no prior context. Read first, for every ticket: `GLOSSARY.md`, the ADRs in `docs/adr/` (0003 above all), and `docs/DEVELOPING.md`.

All tickets are in `issues/`.

## The prototype

The prototype is on the local branch `prototype/frame-generation`, last commit `c04bb81`. It is reference only: nothing from it is merged, and it does not meet the project's standards (no unit tests, logic inside the Presenter's loop, timing that collapses whenever the presenting thread is woken). Read it with `git show` or `git diff gameplayer-v1...prototype/frame-generation`; its changes are marked `PROTOTYPE(framegen)` and `PROTOTYPE(upscale)`. What it taught us is written into the tickets.

Measured on the user's PC (AMD graphics card, 1080p60 Feed, 144 Hz FreeSync monitor), from the prototype's log:

- AMD's AMF frame rate conversion component (FRC) works on the Viewer's own Direct3D 11 device, with BGRA textures at the Feed's size.
- Its start took about 9 s the first time ever and 67 ms on later runs.
- 2x: 60 Unique Frames in, 60 Generated Frames, 120 presented per second, about 1 ms of CPU-side time per Unique Frame.
- 4x: 240 presented per second, about 2.3 ms of CPU-side time per Unique Frame.
- An output frame carries the timestamp of the input just submitted. No output ever arrived late (0 stale outputs).
- GPU time was not measured. A 30 fps Game Rate and a 720p Feed were tried by the user by eye only.

The user judged the picture good at 2x and 4x.

## Tickets and blocking edges

| # | Ticket | Blocked by | Status |
|---|--------|------------|--------|
| 01 | Presenter shows several timed frames per Unique Frame | None | done |
| 02 | Frame Generation 2x | 01 | done |
| 03 | Frame Generation 4x | 02 | done |
| 04 | Sharp Upscale | None | done |
| 05 | Counter on two lines, About, guides | 02, 03, 04 | done |

## Order of work

1. **01** first. It is the prefactor that makes 02 easy.
2. **04** can run at the same time as 01: it only changes how a finished picture is drawn. Both touch the Presenter, so expect a small merge.
3. **02**, then **03**.
4. **05** last.

Longest chain: 01, 02, 03, 05.

## Notes for whoever dispatches these

- Start from `gameplayer-v1` (at or after commit `dc8d715`) on a new branch. Never from the prototype branch.
- No agent can see the monitor. Every ticket ends with a short manual checklist for the user; a ticket is not finished until the user has run it.
- Testing policy, unchanged: unit tests for pure logic, code review for the rest, user checks for the picture.
- Only one program can use the Capture Card, and only one copy of GamePlayer runs. A smoke run does nothing while the user's copy is open. Never close the user's copy. Smoke runs use `--settings` with their own file.
- The user's C: drive is nearly full. Keep build output and downloads on the repo's drive.
- Excluded from this sprint everywhere: an automatic limit on the Generation Factor, frame generation on non-AMD graphics cards, a strength setting for the Sharp Upscale, a 480p mode, HDR, controlling or detecting Lossless Scaling, formal latency measurement.

## Ticket 05 validation

The counter now keeps the original real-frame rate as Game and measures all successful
presents as Shown while generation is running. Startup/failure use the original line;
warm-up, state-message hiding and no-counter-present rules remain. About reports the
actual factor and FSR 1 usage/sizes. The setup guide starts with G/U and explains using
only one generator. Pure tests cover the text, rate history, About states and minimum
480x270 layout. A private-settings 20-second AMD hardware smoke at 4x and forced 30 fps
observed Game 30 / Shown 119, adjacent generation lines 120 presents/s, 90 generated
from 30 attempts, no stale outputs or late skips, and clean exit. A second 2x smoke observed Game 30 / Shown 60 and 60 presents/s, with zero late outputs.
All 333 tests / 6572 assertions pass. The user reported that the required manual test passed on 51502c5.

## Final review fixes

Generation's Game line now counts successful real endpoint presents, excluding real
endpoints superseded before presentation. Off mode keeps its established Processor
rate; Shown still counts every successful present. Typed owned notifications carry
actual AMF call/result failure reasons into About, including continued 2x fallback.
U/menu choices are logged even when native-size playback does not use FSR 1.
The explicit 60 fps (every frame) override intentionally passes repeats; this existing
user override is preserved and clarified in ADR 0003 and DEVELOPING.

The Presenter retains the latest startup picture to prime both FRC stages as soon as
the worker finishes. A new-size driver session still has unbounded asynchronous setup.
Final live verification measured startup 98.4 ms and full 4x on input 2 at 720p; switching
to 1080p took 131.8 ms and full 4x on input 3 (first ready input). Thus the literal
within-two-input bound in ticket 03 cannot be guaranteed across size/device changes;
it is recorded as a limitation rather than achieved. Existing-session recovery still
uses the tested two-picture history rule.

Fresh MSVC build: 338 tests / 6594 assertions pass. UI verification observed both lines
readable at actual 480x270 and fullscreen, About reporting 4x and measured rates, native
U toggles logging both choices, FSR 1 switching on for 720p -> 1920x1080, and clean exit.
Hardware evidence in the ticket worktree: `build/evidence/review-resolution-ui.log` and
`review-recovery-final.log`. The final 4x counter read Game 30 / Shown 119–120.
