# 03: Target rate menu

**What to build:** A "Target rate" submenu lets the user pick the Target Rate: Monitor (the default, follows the monitor the window is on), 120, 72 or 60. The choice is saved. If the chosen rate cannot be shown evenly on the monitor, an on-screen notice says so once. If the Target Rate is not above the Game Rate, only real frames are shown.

**Blocked by:** 01 (Target at the monitor's rate).

**Status:** ready-for-agent

## Context

Read `README.md` and `plan.md` in this folder first. The monitor refreshes at a fixed 144 Hz (no FreeSync), and Target presents once per vsync. A Target Rate below the refresh therefore means some refreshes repeat the previous frame. Only rates that divide the refresh (144, 72, 48 on this monitor) come out even.

## Decisions already made

- **Submenu**, same style as Resolution: Monitor (shows the refresh, e.g. "Monitor (144)"), 120, 72, 60, and a "Custom…" item that ticket 04 makes work (grayed out or absent until then).
- **Rate in use = the smaller of the Target Rate and the refresh.** The user's number is kept as chosen; no cap label (Q16). On a faster monitor a higher number works.
- **Below the refresh:** spread presents as evenly as possible across vsyncs (a Bresenham-style pattern). Pure logic, tested first.
- **Uneven notice** (Q18): the existing on-screen notice, once, when an uneven rate starts applying (chosen, or the window moves to another monitor). Text like "120 on a 144 Hz monitor: motion will not be perfectly even". Not repeated while playing.
- **Below the Game Rate** (Q17): show real frames only, never drop real frames.
- **Saved** in the settings file. Unknown or missing value means Monitor.
- **Applies to Target only.** 2x/4x ignore it.

## Out of scope

The Custom box (ticket 04); rates other than these presets.

## Acceptance criteria

- [ ] The submenu shows the four presets with the current one checked; Monitor shows the current refresh.
- [ ] At 72 the log shows 72 presents per second, every second vsync, evenly.
- [ ] At 120 on 144 Hz the notice shows once; presents are spread evenly.
- [ ] At 60 with a 60 fps Game Rate, only real frames are shown.
- [ ] The choice survives a restart; a bad saved value means Monitor.
- [ ] Unit tests cover the even spreading, rate in use, the below-Game-Rate rule, when the notice fires, and the settings round trip.
- [ ] `docs/DEVELOPING.md` describes the submenu.

## How to verify

Yourself: build, run the unit tests, short smoke runs at 72 and 120 (own `--settings` file).

For the user. End your work by reporting this checklist:

1. Pick 72. Does it look even?
2. Pick 120. Does the notice show once? Visible judder?
3. Restart. Same choice?
