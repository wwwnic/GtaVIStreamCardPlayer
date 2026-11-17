# 01: Presenter shows several timed frames per Unique Frame

**What to build:** The Presenter can show more than one frame for each Unique Frame, each at an exact time, evenly spaced, and it keeps that timing whatever else happens (a text appearing, a resize, a menu). Nothing the user sees changes yet: with no Frame Generation the Viewer behaves exactly as before. A developer-only switch makes the new timing visible in the log so it can be checked before any generator exists.

**Blocked by:** None (can start immediately).

**Status:** done

## Validation recorded at integration

The final MSVC build and unit suite passed after the Feed restart change. The user confirmed normal playback and the visual checks with the volume keys, FPS counter and fullscreen toggle.

The 1080p60 smoke run preceded the final Feed restart change. Steady full seconds showed 60 presents with the switch off, 120 at 2x and 240 at 4x. Maximum measured lateness was 0.539 ms at 2x and 0.556 ms at 4x. At 4x the log counted three early drops during startup and zero late skips.

A 30 fps game and timing during interactive resize, menu use or overlay changes with the developer switch on remain unverified. The visual checks above were made during normal playback.

## Context

GamePlayer (the "Viewer") shows a game Console's picture from a Capture Card. Its pipeline is Source, Processor, Presenter. Today the Presenter shows each Unique Frame the moment it arrives and holds one frame at most. ADR 0003 puts Frame Generation on the GPU side next to the Presenter; that needs the Presenter to show a real frame and its Generated Frames as a timed sequence. This ticket is that prefactor. Read `GLOSSARY.md`, `docs/adr/0003-frame-generation-inside-the-viewer.md`, `docs/DEVELOPING.md` and `.scratch/gameplayer-frame-generation/README.md`.

## Decisions already made

- **Order shown.** For a new Unique Frame N with k Generated Frames between N-1 and N: the k Generated Frames first, in order, then N itself. The gaps are equal: one step is the interval divided by k + 1. k is 0, 1 or 3.
- **The interval** is the time between the arrivals of the last two Unique Frames, so it follows the Game Rate by itself (about 33 ms at 30, about 17 ms at 60).
- **By the clock.** Each frame of the sequence has its own due time counted from the first present of that sequence. A late present must not push the later ones back.
- **Never queue, never go back.** If the next Unique Frame arrives before the current sequence has finished, the rest of the old sequence is dropped and the new one starts. The picture never steps back in time.
- **Other wake-ups do not disturb it.** A text that appears or ends, a resize, a repaint: none of these may end a wait early or bunch frames together. This is the prototype's known weakness; fix it by design.
- **When not to generate (k = 0, show N at once):** the first Unique Frame after a start, a Feed restart, a device rebuild or a change of picture size; an interval under 6 ms or over 100 ms; a gap over 150 ms or a frame sequence number going backwards (both also mean: forget the earlier frame).
- **Where the rules live.** The schedule (which frame is due when, what to drop, when not to generate) is pure logic in `core`, with the clock passed in, and unit-tested. The Presenter only waits and presents.
- **The generator is a small interface on the GPU side:** given the picture of Unique Frame N as a texture, it gives back 0, 1 or 3 Generated Frames between N-1 and N, or says it cannot be used at all. This ticket ships no real generator.
- **Exact waits** use a high-resolution waitable timer.
- **Unchanged with k = 0:** the number of presents, the V-sync and tearing behaviour, and every rule in the "Lossless Scaling" section of `docs/DEVELOPING.md` (present only for a new Unique Frame, a resize, a device rebuild, or a text that appears or ends; the FPS counter never causes a present).
- **Texts stay on top.** The FPS counter, state messages and feedback line are drawn onto every frame of a sequence, never mixed into a Generated Frame.
- **Developer switch** (command line only, not in the menu, not saved): show each Unique Frame 2 or 4 times, evenly spaced, with the picture unchanged. It stands in for a generator and exercises the whole timing path.
- **Log.** Once a second while a sequence of more than one frame is in use: Unique Frames, frames presented, frames dropped because the next Unique Frame came early, and the largest lateness of a present against its due time.

## Known unknowns

- How exact the timer is on this PC. Record the lateness the log shows at 2 and at 4 frames per Unique Frame.
- Whether a present with tearing allowed returns fast enough to keep four evenly spaced frames inside a 16.7 ms interval. Report what you measure; do not add an automatic limit (the user decided against one).

## Out of scope

Any real generator; AMD code; the G key or a menu item; the Sharp Upscale; changes to the FPS counter text; an automatic limit tied to the monitor's refresh rate.

## Acceptance criteria

- [ ] With the developer switch off, the Viewer presents exactly as before this ticket: same present count per second, same V-sync and tearing behaviour.
- [ ] With the developer switch at 2 and at 4, the log shows frames presented equal to 2 and 4 times the Unique Frames per second, on a 60 fps and (if a game allows) a 30 fps Game Rate.
- [ ] Showing a feedback line (volume key held), resizing the window and opening a menu while the switch is on do not bunch frames: the lateness in the log stays small and the presented count stays right.
- [x] A Unique Frame arriving early drops the rest of the old sequence; the log counts it; nothing is shown out of order.
- [x] After a start, a Feed restart, a device rebuild, a size change, a pause over 150 ms or an unusable interval, the next Unique Frame is shown at once, alone.
- [ ] Texts in the Video Area are drawn on every frame of a sequence and appear and end on time.
- [x] Unit tests cover: due times for k = 1 and k = 3; a late present not moving later due times; early arrival of the next Unique Frame; every "when not to generate" case; the interval taken from arrival times.
- [x] `docs/DEVELOPING.md` describes the new part, its threads and the developer switch.

## How to verify

Yourself: build, run the unit tests, then a short smoke run with the developer switch (own `--settings` file) and read the log lines above.

For the user. End your work by reporting this checklist:

1. Start GamePlayer normally and play for a minute. Does everything look exactly as before?
2. Press the volume keys, F for the counter, F11 twice. Anything odd?
