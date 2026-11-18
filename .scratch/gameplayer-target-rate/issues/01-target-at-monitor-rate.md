# 01: Target at the monitor's rate

**What to build:** G now cycles off, 2x, 4x, Target, off. In Target the Viewer shows exactly one frame per refresh of the monitor the window is on (144 on the user's monitor), with V-sync on and no tearing. Each refresh shows the frame nearest in time, chosen from the frames 4x already makes. A 30 fps game is shown at a steady 144; a drop to 26 fps still shows 144. The choice is remembered.

**Blocked by:** None (can start immediately).

**Status:** ready-for-agent

## Context

Read `README.md` and `plan.md` in this folder first. FRC only makes midpoints, so the frames available for Unique Frame N are the 4x set: Q1, M, Q3 between N-1 and N, then N itself (positions 1/4, 2/4, 3/4, 1 of the interval). This set is the **ladder** (code word only, not in the glossary). Ticket 02 adds the x8 ladder.

## Decisions already made

- **The cycle.** G: off, 2x, 4x, Target, off. Feedback line "Frame generation Target (144)" with the rate in use. The View menu item shows the current choice.
- **One present per vsync.** Target presents with sync interval 1, no tearing flag, max frame latency 1, using the existing waitable swap chain. The rate in use is the refresh of the monitor the window is on; it follows the window to another monitor (the user also has a Samsung, possibly 60 Hz).
- **Picking a frame.** For each vsync, work out where its display time falls inside the current Unique Frame's interval and show the ladder frame nearest to that position. Never go backwards; never show an older Unique Frame's frames once a new one has started. The interval used is the last one measured, as today.
- **Pure logic in `core`.** A new schedule class beside FrameSchedule (do not change FrameSchedule). It gets vsync times and arrivals and answers which frame to show. Tested first with doctest: positions, rounding, never going backwards, a new Unique Frame arriving mid-sequence, uneven 33/50 ms gaps (a 26 fps game on a 60 Hz signal), a pause, the first frame after a start.
- **Presenter.** A new path used only in Target. The 2x/4x timed path and tearing stay exactly as they are.
- **V-sync menu item** is forced on and grayed out while Target runs. The user's own V-sync setting is not changed or saved by this.
- **Counter.** Same two-line style as today: Shown (about 144) and Game (the real rate).
- **Remembered** like 2x/4x. A value the settings file does not understand means off.
- **Below the Game Rate.** If the rate in use is not above the Game Rate, show real frames only, never drop real frames (decision Q17). Rare at 144, but handle it.
- **ADR 0004.** Write it: Target Rate replaces two points of ADR 0003 for Target only: "the Generation Factor is the user's choice alone" and "V-sync is ignored while Frame Generation is on". Record why (fixed-refresh monitor, FRC midpoints only, nearest-frame picking) and the alternatives (fixed x6, timed presents with tearing, VRR).
- **Log.** A once-a-second `Target:` line: rate in use, vsyncs, presents, how many showed a Generated Frame, how many repeated the previous frame, missed vsyncs, ladder depth.

## Known unknowns

- Which source gives reliable vsync times in a borderless window on this setup (swap chain frame statistics, waiting on the output's vblank, DWM timing). Pick one, log what you see, report it.
- Latency added by the vsync wait (up to one refresh, about 7 ms). Accepted (Q5); report what you see.

## Out of scope

x8 (ticket 02); choosing a rate other than the monitor's (ticket 03); the Custom box (ticket 04); any change to 2x/4x.

## Acceptance criteria

- [ ] G cycles off, 2x, 4x, Target, off, with the matching line each time; the menu shows the current choice.
- [ ] In Target at 30 fps (Frame rate forced to 30) the log shows about 144 presents per second, one per vsync, with almost no missed vsyncs.
- [ ] No tearing in Target; V-sync shows on and grayed out; leaving Target restores the user's V-sync setting.
- [ ] Moving the window to another monitor changes the rate in use to that monitor's refresh.
- [ ] 2x and 4x behave exactly as before (same log numbers as sprint 3).
- [ ] Target is remembered across restarts.
- [ ] Unit tests cover the cycle, the settings round trip, and the frame-picking cases listed above.
- [ ] ADR 0004 written; `docs/DEVELOPING.md` describes Target.

## How to verify

Yourself: build, run the unit tests, one short smoke run in Target (own `--settings` file) and read the `Target:` lines.

For the user. End your work by reporting this checklist:

1. A 60 fps game with Frame rate set to 30. Is Target smoother than 4x?
2. Pan the camera slowly. Any regular stutter?
3. Any tearing line in Target?
4. Does Target feel laggier than 4x?
5. Close GamePlayer in Target and start again. Still Target?
