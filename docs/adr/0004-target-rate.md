# Target Rate: one frame per refresh, the nearest of the frames 4x makes

Replaces two points of ADR 0003, for Target only. Everything else in ADR 0003 stands, and 2x and 4x keep both points unchanged.

Frame Generation gets a fourth choice beside off, 2x and 4x: Target. Instead of a fixed Generation Factor the Viewer shows a steady Target Rate, the refresh rate of the monitor the window is on (144 on the user's), so a 30 fps game is shown at 144 and a drop to 26 fps still shows 144. G cycles off, 2x, 4x, Target.

Three facts shape how. The user's monitor (BenQ ZOWIE XL2536) has a fixed refresh: no FreeSync or Adaptive-Sync, so a frame can only appear at a vsync. AMD's FRC only makes the frame exactly halfway between two pictures; 4x halves twice, so the frames available for Unique Frame N are at 1/4, 2/4, 3/4 of the interval since N-1, and N itself (the ladder), never at an arbitrary time. And 144 is not a multiple of a 30 or 26 fps Game Rate. So Target presents once per vsync (sync interval 1, no tearing, the existing waitable swap chain with maximum frame latency 1) and, for each vsync, shows the ladder frame nearest in time to when that vsync is displayed, or the previous frame again when nothing nearer exists. The times follow 4x: the first ladder frame is due when the ladder is ready, the others a step apart after it, so Target holds frames back by the same amount as 4x, plus the wait for the next vsync (up to one refresh, about 7 ms). If the rate in use is not above the Game Rate, Target shows real frames only. The pure logic is `core/target_schedule.h`. It sits beside `FrameSchedule`, which is unchanged. The vblank times come from the swap chain's frame statistics (`core/vsync_clock.h`).

## Replaced points of ADR 0003 (Target only)

- "The Generation Factor is the user's choice alone." In Target the Viewer chooses how many Generated Frames each Unique Frame gets: as many as the Game Rate leaves room for at the Target Rate. The user chooses the rate, not the factor. Generation Factor stays the word for 2x and 4x.
- "V-sync is ignored while Frame Generation is on." Target forces V-sync on: on a fixed-refresh monitor only a vsync-aligned present avoids tearing and uneven pacing (the monitor's DyAc strobing makes uneven pacing easy to see). The V-sync menu item shows on and greyed in Target; the user's own V-sync setting is not changed or saved, and applies again when Target ends.

## Considered options

- **A fixed x6 factor.** 144 / 24 or so, but FRC cannot make sixths, and picking 6 of 8 eighths is uneven. Target covers the same use and adapts when the Game Rate drops.
- **Timed presents with tearing, as 2x and 4x do.** Exact due times, but on a fixed-refresh monitor each present tears or waits anyway, and at 144 per second the tear lines would be constant.
- **Variable refresh (FreeSync).** Would let frames appear at any time and make nearest-frame picking unnecessary. The monitor has none.

## Consequences

- Target adds up to one refresh of Latency over 4x (accepted, Q5 in the sprint plan).
- Judder remains where the Game Rate is uneven: a 26 fps game on a 60 Hz signal arrives 33 and 50 ms apart, and the schedule can only use the last interval. It is softened, not removed. With evenly spaced Unique Frames every ladder frame is shown.
- Seen on the user's PC: on the second monitor (59 Hz) the windowed swap chain still accepted one present per 144 Hz vblank (its frame statistics say so too). The rate in use follows the window's monitor, but the present cadence is the one DWM gives. Target follows the measured cadence, so the picking stays right, but more frames are presented than that monitor shows.
- Target needs the same AMD runtime as 2x and 4x. If the second FRC fails, Target stays on, picking from the 2x frames.
