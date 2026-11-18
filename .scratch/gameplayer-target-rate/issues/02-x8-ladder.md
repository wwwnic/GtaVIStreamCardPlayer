# 02: x8 ladder for Target

**What to build:** In Target, when the Game Rate is low compared with the rate in use (a 30 fps game on 144 Hz, or drops), the Viewer makes seven Generated Frames per Unique Frame instead of three, so each refresh gets a frame much closer to its real time. Motion at 30 fps and during drops becomes more even. With a 60 fps game, Target keeps using the x4 ladder. If the extra stage cannot start, Target keeps running on x4 and says so on screen.

**Blocked by:** 01 (Target at the monitor's rate).

**Status:** ready-for-agent

## Context

Read `README.md` and `plan.md` in this folder first. 4x uses two FRC stages: the first makes M from N-1 and N; the second is fed N-1, M, N and makes Q1 and Q3 (see sprint 3 ticket 03 for the exact feeding). x8 adds a third stage fed the quarter stream (N-1, Q1, M, Q3, N) and makes the eighths. Shown positions become 1/8 ... 8/8.

## Decisions already made

- **Third stage, Target only.** Created and fed only in Target with the x8 ladder. 2x and 4x never touch it.
- **Same rules as stage two.** An output is used only if it carries the number of the input just submitted. If the stage misses a frame of its stream, its history is cleared and it starts again from the current real frame; that Unique Frame uses the x4 ladder.
- **All or fall back.** If any eighth is missing for a Unique Frame, use the x4 ladder for that one. Never a partial x8.
- **Choosing the depth.** Pure logic in `core`, tested first. From the recent Game Rate (about the last second) and the rate in use: x8 when the refresh slots per Unique Frame exceed about 2.8, x4 when they fall below about 2.5 (hysteresis so it does not flip). At 144: about 51 fps and below means x8; about 58 fps and above means x4. Starting values; report if they feel wrong. Depth changes only at a point where the third stage's history can restart cleanly.
- **If the third stage cannot be created or fails:** Target stays on, uses x4, shows the existing on-screen notice once, and the log says why. Same pattern as 4x falling back to 2x today (decision Q14).
- **Log.** The `FrameGen:` line also counts eighth frames; the `Target:` line shows the depth in use.

## Known unknowns

- Real GPU and CPU time for seven FRC calls. Estimated about 6 ms per Unique Frame at 1080p from the measured 0.8 ms per call. Report average and worst at 720p and 1080p.
- Picture quality of eighths: made from already generated frames, two steps removed from real ones. The user judges it.
- Extra latency: the real frame is held about 7/8 of the interval (about 29 ms at 30 fps) instead of 3/4. Accepted (Q5).

## Out of scope

x16; changing 2x/4x; the Target rate menu (ticket 03).

## Acceptance criteria

- [ ] In Target at 30 fps the log shows the x8 ladder, about 210 Generated Frames per second made, and about 144 presents.
- [ ] With Frame rate set to 60 the log shows the x4 ladder.
- [ ] The depth does not flip back and forth on a steady Game Rate.
- [ ] A Unique Frame with a missing eighth uses the x4 ladder; no late output is shown.
- [ ] If the third stage cannot be created, Target runs on x4, the notice shows once, the log says why.
- [ ] After a pause, a Feed restart or a size change, x8 resumes by itself.
- [ ] Unit tests cover depth choice and hysteresis, "all or fall back", and the rule that restarts the third stage's history.
- [ ] `docs/DEVELOPING.md` describes the x8 ladder.

## How to verify

Yourself: build, run the unit tests, short smoke runs in Target at forced 30 and 60 (own `--settings` file), read the `FrameGen:` and `Target:` lines.

For the user. End your work by reporting this checklist:

1. 30 fps in Target: smoother than ticket 01's result?
2. Pan the camera fast. More wobble or smearing than 4x?
3. Does it feel laggier than 4x?
4. A game with frame drops: does motion stay even through them?
