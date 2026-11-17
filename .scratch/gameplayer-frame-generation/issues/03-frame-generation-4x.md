# 03: Frame Generation 4x

**What to build:** G now cycles off, 2x, 4x, off. At 4x the Viewer shows three Generated Frames for every Unique Frame, evenly spaced: a 30 fps game is shown at 120. The choice is remembered like 2x.

**Blocked by:** 02 (Frame Generation 2x).

**Status:** done

## Validation recorded at integration

All 322 unit tests passed. With 4x enabled on a live 720p60 Feed, the Viewer reached 120 presents per second with the Game Rate fixed at 30 fps and 240 with it fixed at 60 fps. The user reported a successful manual test of `142e7f7` without listing individual checklist results.

## Context

GamePlayer (the "Viewer") does its own Frame Generation with AMD's AMF frame rate conversion component (FRC), which only makes the frame exactly halfway between two pictures. The user plays mostly 30 fps games on a 144 Hz monitor and wants them at 120. The prototype reached 4x by running a second FRC on the output of the first, and the user judged it good. Read `GLOSSARY.md`, `docs/adr/0003-frame-generation-inside-the-viewer.md`, `docs/DEVELOPING.md` and `.scratch/gameplayer-frame-generation/README.md`.

## Decisions already made

- **The cycle.** G: off, 2x, 4x, off. Feedback lines: "Frame generation 2x", "Frame generation 4x", "Frame generation off". The View menu item shows the current Generation Factor and the key.
- **How 4x is made.** A second FRC is fed the doubled stream: real frame N-1, the middle frame M, real frame N, and so on. Its outputs are the quarter frames: one between N-1 and M, one between M and N. Shown in order: first quarter, M, second quarter, then N. From the prototype, for precision:

  ```
  first FRC   in:  N-1, N              out: M   (between N-1 and N)
  second FRC  in:  N-1, M, N, M', N'   out: Q1 (N-1..M), Q3 (M..N), ...
  shown:      Q1, M, Q3, N             each one quarter of the interval apart
  ```

- **All three or just the middle.** If both quarter frames are ready in time, show three Generated Frames. If either is missing, show only M (as at 2x) for that Unique Frame. Never show two.
- **The same late-output rule** as in ticket 02, on both FRCs: an output is used only if it carries the number of the input just submitted.
- **Keeping the second FRC in step.** It must see every frame of the doubled stream. Whenever it misses one (2x in use, no middle frame, a pause, a restart), its history is cleared and it starts again from the current real frame; that Unique Frame is shown at 2x.
- **If the second FRC cannot be created,** 4x behaves as 2x and the log says so once; the menu and the line on screen show what really runs.
- **No automatic limit.** 4x on a 60 fps game presents 240 a second, more than the user's 144 Hz monitor shows. The user decided the Viewer does not step in: the counter tells the truth and the user picks 2x. Do not add a cap.
- **Latency.** The real frame is held for three quarters of the interval (about 25 ms at 30 fps). Accepted (ADR 0003).
- **Remembered** like 2x; a value the settings file does not understand means off.
- **Log.** The once-a-second `FrameGen:` line also counts quarter frames made, so 2x and 4x can be told apart.

## Known unknowns

- GPU time at 4x (three generations per Unique Frame). CPU-side time was about 2.3 ms at 1080p. On a 720p Feed it should be much lower; report what you see.
- Picture quality of quarter frames: they are made from an already generated frame. The user judged it good on a 60 fps game shown at 240 on a 144 Hz monitor, which is not a fair test. The fair one is a 30 fps game at 120; it is in the checklist.
- Whether four evenly spaced presents fit inside a 16.7 ms interval (60 fps game at 4x). Report the lateness from ticket 01's log line; do not change behaviour because of it.

## Out of scope

Factors other than 2x and 4x; an automatic limit; a generator that makes frames at arbitrary positions (for example 60 to 144); the Sharp Upscale; counter and guide changes (ticket 05).

## Acceptance criteria

- [ ] G cycles off, 2x, 4x, off, with the matching line each time; the menu shows the current Generation Factor.
- [ ] At 4x with a 30 fps Game Rate the log shows about 90 Generated Frames and 120 presented per second; with a 60 fps Game Rate about 180 and 240.
- [ ] The four frames of a sequence are evenly spaced and in order; no late output is shown.
- [ ] A Unique Frame where a quarter frame is missing is shown with the middle frame only.
- [ ] Switching 2x to 4x and back while playing never shows a wrong or old picture.
- [ ] After a pause, a Feed restart or a size change, 4x picks up again by itself within two Unique Frames.
- [ ] If the second FRC cannot be created, 4x runs as 2x, says so once in the log, and the screen shows what really runs.
- [ ] 4x is remembered across restarts.
- [ ] Unit tests cover: the three-step cycle; the settings round trip for 4x; "three or one, never two"; the rule that restarts the second FRC's history.
- [ ] `docs/DEVELOPING.md` describes 4x.

## How to verify

Yourself: build, run the unit tests, one short smoke run at 4x (own `--settings` file) and read the `FrameGen:` lines.

For the user. End your work by reporting this checklist:

1. On a 30 fps game: G to 2x, then 4x. Is 4x smoother than 2x?
2. Pan the camera fast at 4x. Wobble or smearing around the HUD?
3. Does the game feel too laggy at 4x? Compare with 2x.
4. Close GamePlayer at 4x and start again. Still 4x?
