# 04: Unique Frames and Game Rate detection

**What to build:** GamePlayer shows each distinct picture from the game exactly once. When the user plays a 30 fps game, the Viewer detects that the Game Rate is 30 even though the Console's Signal Rate is 60, drops the Repeated Frames, and presents only the Unique Frames. When the game runs at 60, every frame is shown. The user does nothing; it adapts by itself as the game's rate changes.

**Blocked by:** 02 (App skeleton showing the Feed picture in a window).

**Status:** done (user hand checks pending)

**Gate outcome:** The user could not find a way to run a game at 30 fps, so bit-identical Repeated Frames at 30 fps remain unverified. This ticket is built with a tunable tolerance and diagnostic logging instead. The user will judge Lossless Scaling by taking a 60 fps game to 120 on their 144 Hz monitor (ticket 10).

## Context

GamePlayer is a small personal Windows 11 app (the "Viewer") that shows a game Console's picture and sound from a USB Capture Card on the PC monitor. Read `GLOSSARY.md` (especially Signal Rate, Game Rate, Unique Frame, Repeated Frame), `docs/adr/0002-frame-generation-stays-outside-the-viewer.md`, the developer note from ticket 02, and the prototype's findings note, `prototype/FINDINGS.md`, which records what comparison worked on a static picture and, once the user has run a 30 fps game through the prototype, whether Repeated Frames arrive bit-identical.

Hardware: Xbox Series S; AVerMedia Live Gamer Ultra S GC553 Pro capturing uncompressed 1080p60; AMD Radeon RX 9070.

### Why this exists

The Console always sends 60 frames per second. A 30 fps game therefore arrives as each picture sent twice. Frame Generation is done by a separate app, Lossless Scaling, running on top of the Viewer's window, and it works out the base frame rate by counting the frames the Viewer presents. If the Viewer presented all 60 frames, Lossless Scaling would believe the game runs at 60 and its generated in-between frames would sit between identical pictures, which looks choppy rather than smooth. Presenting only Unique Frames lets it see the true base of 30 and generate up to 60 properly. This requirement is the main consequence recorded in ADR 0002. The same detection will be the foundation for any Frame Generation later built into the Viewer.

## Decisions already made

- This logic lives in the **Processor** stage of the Source / Processor / Presenter pipeline. The Processor stops being a pass-through: it receives every captured frame and emits only Unique Frames.
- **Capture stays at 60 always.** 30 is never a capture mode. Detection of the Game Rate happens in the Viewer.
- **Automatic by default.** The Viewer decides per frame whether it is a Repeated Frame, and derives the current Game Rate from the recent pattern of Unique Frames (typically 60, 30, sometimes other values such as 40 or unstable rates when a game drops frames).
- **An override will exist** with three values: Auto, 30, 60. Ticket 08 puts it in the menu; this ticket provides the setting in the Processor's interface and makes it work. Define the meanings as follows:
  - *Auto*: drop Repeated Frames as detected. This is the default.
  - *60*: no dropping at all; every captured frame is presented. This is the escape hatch if detection misbehaves on some game.
  - *30*: present at most one frame per two captured frames, preferring Unique Frames, for the case where detection is fooled (for example by film grain or a flickering HUD element making repeats look different).
- **No added delay.** A Unique Frame must be passed on the moment it is recognised. Do not wait for the next frame to decide; compare against the previous one only.
- **Cheap comparison.** Comparing two 1080p frames must cost well under a millisecond on the capture path. Comparing a sparse sample of rows or blocks spread over the frame is fine.
- **Exact match first, near-duplicate tolerance second.** The prototype's exact byte comparison correctly marked a static picture as fully repeated (repeats were byte-identical). Whether a 30 fps game's Repeated Frames are also bit-identical is still untested, because the card's processing could make them differ slightly. So the detector has two steps: an exact comparison as the fast path (identical means Repeated Frame), and, only if that fails, a near-duplicate check that still counts the frame as a Repeated Frame when the difference is tiny. The prototype's rule was "fewer than 0.5% of 8-byte words differ"; start from that. The tolerance must be easy to tune: one named constant in one place, and settable to zero (exact match only). Log, once per second, how many frames were judged repeated by the exact path and how many by the tolerance path, so the user's check shows whether the tolerance is doing anything.
- **Keep recent Unique Frames.** Retain the last two or three Unique Frames (as the pipeline's frame objects) so a later Frame Generation or sharpening step has them. No behaviour depends on this yet.
- **Expose the numbers.** The Processor publishes the current Signal Rate (frames arriving), Game Rate (Unique Frames per second) and the count of Repeated Frames dropped, so the Help or debug display and the Lossless Scaling guide can show them. The user sees them through the FPS counter below. Do not put them in the window title: the title must stay exactly "GamePlayer" because Lossless Scaling matches its profile on it (ticket 10).
- **On-screen FPS counter (optional, off by default).** A small text counter in the top-left corner of the Video Area showing Game Rate and Signal Rate, for example "30 / 60". The F key toggles it while the GamePlayer window has focus, in windowed and fullscreen modes; it starts off on every launch. Its value is refreshed about once a second. It is drawn only into frames that are already being presented: it must never cause a present of its own (no timer, no redraw when its value changes, no redraw when it is toggled). Reason: every present is counted by Lossless Scaling as a game frame, so extra presents would make it see a wrong base rate. Consequences to accept rather than work around: toggling it on or off shows up with the next presented frame, and on a fully static picture the counter stays frozen at its last drawn value. Occasional repaints that already exist for other reasons (resize, window uncovered) may draw it too. The text drawing should be small and reusable, since tickets 06 and 07 also draw text in the Video Area; if one of them has already built text drawing, reuse it.
- **Testing**: this is pure logic and must be unit tested without a Capture Card, using synthetic frame sequences.

## Known unknowns

- Whether Repeated Frames from this card are bit-identical during a 30 fps game. The user's hand test on the prototype should settle this before you start; check `prototype/FINDINGS.md` for the result. If it shows repeats differ slightly, tune the tolerance constant to what was observed; if it is still unsettled, keep the prototype's 0.5% starting value and say in your report that it needs the user check below.
- Games with uneven pacing (for example a 30 fps game that occasionally shows a picture for three signal frames, or a 40 fps mode). Auto must simply present whatever is unique; the Game Rate figure should be a smoothed estimate that does not flicker between values every second.
- How Lossless Scaling reacts to a long gap in presents during a static scene is unverified. Nothing to implement; note anything you observe.

## A fully static Feed is normal

This is settled, not unknown. The prototype saw it on the real card: with a static picture, about 60 frames per second keep arriving while there are 0 to 1 Unique Frames per second. Menus, pause screens and an idle Console all look like this. It must be handled as an ordinary state:

- About zero Unique Frames per second is not an error. Do not log it as a fault, do not reset the pipeline, and do not let it trip any recovery logic.
- A Game Rate of zero (or close to it) must not break anything: no division by zero, no nonsense values, and nothing downstream (the FPS counter, ticket 08's About dialog, ticket 06's states) may treat it as invalid. Report it as a low rate, or as "still" if a number makes no sense; the 30 / 60 / Auto override keeps working.
- The last picture stays on screen. Nothing is presented until the next Unique Frame, and the window must still repaint the last picture correctly when resized or uncovered.
- When motion resumes, the first Unique Frame is shown immediately and the Game Rate estimate recovers within a second or two.
- Keep the count of frames arriving separate from the count of Unique Frames. Ticket 06 needs this, and note that frames arriving does not prove a live HDMI signal either (see ticket 06).
- How Lossless Scaling's WGC capture mode treats presents is unverified (research suggests DXGI mode follows the source's present rate while WGC follows the desktop compositor). Nothing to implement; ticket 10 covers the user-side setting.

## Out of scope

The menu entry for the override (08); saving the override (09); any Frame Generation, interpolation or sharpening; changing the capture mode; 120 fps.

## Acceptance criteria

- [ ] With a 30 fps game, the Viewer presents about 30 frames per second and reports Signal Rate 60, Game Rate 30.
- [ ] With a 60 fps game, the Viewer presents about 60 frames per second and reports Game Rate 60.
- [ ] A Unique Frame is forwarded as soon as it is recognised; the comparison adds no frame of delay and no measurable cost on the capture path.
- [ ] The override setting exists on the Processor with values Auto (default), 30 and 60, behaving as defined above, and can be changed while running.
- [ ] The last two or three Unique Frames are retained and available to later Processor steps.
- [ ] Signal Rate, Game Rate and dropped Repeated Frame counts are available to the rest of the app; the window title stays exactly "GamePlayer".
- [ ] Repeated Frame detection uses exact comparison as the fast path and a near-duplicate tolerance (starting at "fewer than 0.5% of 8-byte words differ") as the second step; the tolerance is one named constant, can be set to zero, and the log shows how many repeats each path caught.
- [ ] A fully static Feed (about 60 arriving, about 0 unique per second) is treated as normal: no error, no reset, no division by zero, the last picture stays on screen and repaints on resize, and motion resuming is shown at once.
- [ ] The F key toggles a small FPS counter ("Game Rate / Signal Rate", for example "30 / 60") in the top-left of the Video Area; it is off at every launch and works windowed and fullscreen.
- [ ] The FPS counter never causes a present of its own: with it on, the present count with a 30 fps game is the same as with it off.
- [ ] Unit tests with synthetic sequences cover: all-unique input (60), strictly doubled input (30), tripled and mixed pacing, a long still run followed by motion, a fully static run (Game Rate near zero without errors), a change from 30 to 60 mid-stream, each override value, and both sides of the tolerance boundary.

## How to verify

Yourself: build, run the unit tests, and run the app to confirm that it still shows the picture and that the reported numbers appear.

For the user. End your work by reporting this checklist:

1. Start GamePlayer. Is there no counter in the corner? Press F. Does a small counter appear top-left? Press F again. Does it go away?
2. Start a 30 fps game with the counter on. What does it read? (Expected: about "30 / 60".)
3. Start a 60 fps game or the Xbox dashboard while it moves. What does it read? (Expected: about "60 / 60".)
4. Does the picture look exactly as before, with no new stutter?
5. Pause the game or sit in a still menu for a minute, then resize the window. Does the picture stay visible, with no error? (The counter may freeze while nothing moves; that is expected.)
6. Turn on Lossless Scaling (LSFG, fixed X2) on the window during the 30 fps game. Does it show a base of 30 and an output of 60? Press F to turn the counter on. Does the base stay at 30?
