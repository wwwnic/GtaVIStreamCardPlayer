# GamePlayer sprint 4: Target Rate

Sprint 3 (`.scratch/gameplayer-frame-generation/`) put Frame Generation (2x, 4x) inside the Viewer. This sprint adds a fourth choice: a **Target Rate**. Instead of a fixed Generation Factor, the Viewer shows a steady number of frames per second (by default the monitor's refresh, 144) and Generated Frames fill whatever the Game Rate leaves. A 30 fps game becomes 144; a drop to 26 fps still shows 144.

Each ticket is self-contained and written for an implementing agent with no prior context. Read first, for every ticket: `GLOSSARY.md` (Target Rate is new), the ADRs in `docs/adr/` (0003, and 0004 once ticket 01 writes it), `docs/DEVELOPING.md`, and `plan.md` in this folder (the design, the measurements and every decision with its question number).

All tickets are in `issues/`.

## Facts that shape the design

- AMD's FRC only makes the frame exactly halfway between two pictures. 4x is made by halving twice (two FRC stages). Frames at arbitrary times are impossible; Target picks, for each refresh, the nearest frame from a power-of-two set (the **ladder**: x4 or x8).
- The user's monitor is a BenQ ZOWIE XL2536: 1920x1080, 144 Hz, **no FreeSync / Adaptive-Sync** (no option in AMD Adrenalin or the monitor menu; not in ZOWIE's VRR model list). Sprint 3's README says "144 Hz FreeSync monitor": that is wrong. Refresh is fixed, so Target presents once per vsync with V-sync on. It has DyAc (backlight strobing), which makes uneven pacing easy to see.
- GPU: AMD Radeon RX 9070. Capture Card: AVerMedia GC553 Pro (also offers 1080p120; not used this sprint).
- Measured 2026-11-17 on the user's PC, 30 fps (forced) at 4x: generation 2.45 ms per Unique Frame at 720p and 2.65 ms at 1080p (about 0.8 ms per FRC call), presents average 0.29 ms late, almost no drops. x8 (7 FRC calls) is estimated at about 6 ms.

## Tickets and blocking edges

| # | Ticket | Blocked by | Status |
|---|--------|------------|--------|
| 01 | Target at the monitor's rate | None | ready-for-agent |
| 02 | x8 ladder for Target | 01 | ready-for-agent |
| 03 | Target rate menu | 01 | ready-for-agent |
| 04 | Custom Target Rate box | 03 | ready-for-agent |

## Order of work

1. **01** first.
2. **02** and **03** can run at the same time. 02 touches the generator; 03 touches menu, settings and notices. Both touch the Target schedule lightly; expect a small merge.
3. **04** last.

## Notes for whoever dispatches these

- Work on the branch `target-rate`, made from `gameplayer-v1`. Never from a prototype branch.
- Additive only (decision Q9). 2x and 4x must behave exactly as today: FrameSchedule, the timed-present path and tearing stay untouched. New code sits beside the old.
- No agent can see the monitor. Every ticket ends with a short manual checklist for the user; a ticket is not finished until the user has run it.
- Testing policy, unchanged: unit tests (doctest) for pure logic in `core`, written first; code review for the rest; user checks for the picture.
- Only one program can use the Capture Card, and only one copy of GamePlayer runs. A smoke run does nothing while the user's copy is open. Never close the user's copy. Smoke runs use `--settings` with their own file.
- The user's C: drive is nearly full. Keep build output on the repo's drive.
- Out of scope everywhere: a fixed x6 factor, FreeSync/VRR paths, 1080p120 Feed, frame generation on non-AMD cards, changes to 2x/4x behaviour, formal latency measurement.
