# Target Rate mode — plan

Status: planning only.
Nothing built yet.

## Key constraint found

AMD FRC only makes the midpoint.
4x works by halving twice.
Stage 1: middle (1/2).
Stage 2: quarters (1/4, 3/4).
See src/platform/amf_generator.cpp.

So no frame at any timestamp.
Only 1/2, 1/4, 1/8 ... steps.
Exact x6 or x4.8 is impossible.
Not with FRC alone.

## Design

Two parts.

Part A: a frame ladder.
Generate a power-of-two set.
x2, x4, or x8 phases.
x8 = a third FRC stage.
Eighths from quarters.

Part B: phase picking.
Each output slot gets a time.
Show the nearest ladder phase.
Slots = target / Game Rate.

Example, target 144:
60fps -> 2.4 slots -> x4 ladder.
30fps -> 4.8 slots -> x8 ladder.
26fps -> 5.5 slots -> x8 ladder.

Pick the cheapest ladder
that keeps timing error small.
Rule of thumb:
ladder >= 1.5 x slots.

Never show more than target.
No wasted presents.

## Presenting: fixed 144Hz, vsync aligned

Monitor: ZOWIE XL2536.
No FreeSync (see Phase 0).
Refresh is fixed at 144.

So one frame per vsync.
Present with sync interval 1.
No tearing.
Keep the waitable swap chain.
Max frame latency 1.

Each vsync:
know its time (frame statistics).
Phase = time since anchor / gap.
Show the nearest ladder phase.

Target = refresh exactly.
144, no -3 needed.

Cost: up to 1 refresh of wait.
About 7 ms worst.

DyAc (strobing) shows judder clearly.
Even pacing matters even more here.

Today: timed presents + tearing.
That stays for x2 / x4.

## Known limits

Signal is 60Hz.
A 26fps game arrives uneven.
Gaps of 33ms and 50ms mixed.
Next gap is unknown in advance.
Schedule uses the last gap.
Some judder stays.
Can't be fixed fully.
Only softened.

x8 holds frames longer.
About 7/8 of a gap.
~29ms at 30fps.
vs ~25ms at x4.

x8 = 7 FRC calls per frame.
GPU cost unknown.
Must measure first.

## Phase 0 results so far

From old logs, 720p Feed:
x4 at 60fps: 2.3 ms per Unique Frame.
x4 at 30fps: 2.6 ms per Unique Frame.
3 FRC calls -> ~0.8 ms each.
x8 guess: 7 calls -> ~6 ms.
Fits easily in a 33 ms gap.
But adds directly to latency.

Lateness at x4: avg 0.3 ms.
Timer is precise enough.

PC:
GPU: AMD Radeon RX 9070.
Monitor: ZOWIE XL, 1920x1080, 144Hz.
Second monitor: Samsung.

FreeSync on the XL2536: no.
No option in Adrenalin.
No option in the monitor menu.
Not in ZOWIE's VRR model list.

Live run, 2026-11-17:
720p Feed, Frame rate 30, 4x.
~164 s of 4x.

x4 gen time per Unique Frame:
avg 2.45 ms.
p95 of 1-second worsts: 3.5 ms.
max 9.1 ms.
x2: avg 1.0 ms (1 call).

x8 estimate at 720p:
7 calls -> ~5.7 ms avg.
~8 ms p95.

Pacing at 30fps x4:
29.6 unique/s.
dropped early: 3 total.
skipped late: 0.
lateness avg 0.29 ms.
max 1.4 ms.
Baseline is very clean.

1080p run, ~108 s of 4x:
gen avg 2.65 ms.
p95 4.8 ms, max 9.1 ms.
Pacing: 7 dropped early, 0 late.
lateness avg 0.29, max 5.3 ms.
Part windowed, part fullscreen.
No visible difference in numbers.

1080p barely costs more than 720p.
x8 estimate at 1080p:
~6 ms avg, ~11 ms p95.
Fits a 33 ms gap.

Phase 0: done.
No more tests needed.
x8 cost will be measured for real
once the third stage exists.

Side note:
Capture card offers 1080p120.
A 120Hz signal halves
the uneven-gap problem
(8 ms steps, not 17 ms).
Worth testing later.

## Phases

0. Measure
   FRC GPU time per call.
   Your monitor: VRR or not.
   Try 30fps x4 as it is.

1. Pure logic, tested
   Slot math in core.
   Phase picking in core.
   Ladder choice in core.
   Extend frame_schedule.

2. Third FRC stage
   x8 ladder in amf_generator.
   Fallback to x4 if it fails.
   Same pattern as x4 -> x2 today.

3. Presenter
   Vsync-paced loop in Target mode.
   Read refresh and vsync times.
   One present per vsync.

4. Settings and UI
   New Generation mode: Target.
   Target value: monitor refresh.
   Maybe user-set later.
   Menu, hotkey cycle, settings file.
   FPS counter shows target.

5. Docs
   ADR 0004 replaces ADR 0003 points:
   "factor is the user's alone"
   "V-sync ignored while generating"
   Glossary: Target Rate, ladder.

## Fixed x6?

Not cleanly possible.
6 from 8 phases is uneven.
Target mode covers that use case.
Keep x2 and x4 as they are.

## Decisions (grilling, 2026-11-17)

Q1 Target = 4th Generation choice.
   off -> 2x -> 4x -> Target -> off.
Q2 Target Rate is editable.
Q3 Term: Target Rate (in GLOSSARY).
   Generation Factor = 2x/4x only.
Q4 Ladder depth from recent Game Rate,
   with hysteresis. x4 or x8.
Q5 ~14 ms more lag than x4: accepted.
Q6 Target forces V-sync on.
   Menu item grayed in Target.
   2x/4x unchanged.
Q7/14 x8 stage fails:
   stay in Target on x4 ladder.
   On-screen notice.
   FRC fails fully: as today.
Q9 Additive only, no big refactor.
   New schedule class.
   New Presenter path, Target only.
   Third FRC stage, Target only.
Q10/12 Accept 1..1000.
   Any number, no "even" rule.
Q11 Menu "Target rate" submenu:
   Monitor (default, follows screen),
   120, 72, 60, Custom... (number box).
   Saved in settings.
Q13 Monitor follows the window's screen.
Q16 No cap label.
   Save the user's number.
   Shown rate = min(target, refresh).
Q17 Target below Game Rate:
   real frames only.
Q18 Uneven-rate warning:
   existing on-screen notice.
   Once, when it starts applying.
Q15 ADR 0004 replaces two ADR 0003 points.
Q19 No fixed x6.
Q20 TDD pure logic in core (doctest).
   Then manual Xbox run, read log.
Q21 New branch target-rate
   off gameplayer-v1.

## Open questions (old)

Monitor VRR: no (XL2536).
Target fixed to refresh, or settable?
Okay with ~4ms more hold at x8?
