# 04: Unique Frames and Game Rate detection

**What to build:** GamePlayer shows each distinct picture from the game exactly once. When the user plays a 30 fps game, the Viewer detects that the Game Rate is 30 even though the Console's Signal Rate is 60, drops the Repeated Frames, and presents only the Unique Frames. When the game runs at 60, every frame is shown. The user does nothing; it adapts by itself as the game's rate changes.

**Blocked by:** 02 (App skeleton showing the Feed picture in a window).

**Status:** needs-plan-review

**Gate:** Do not start this ticket until the user has reviewed the findings note from ticket 01 (the throwaway prototype) and has confirmed or revised the plan. This ticket may be rewritten at that review; if its status still reads needs-plan-review, stop and ask the user rather than beginning work.

## Context

GamePlayer is a small personal Windows 11 app (the "Viewer") that shows a game Console's picture and sound from a USB Capture Card on the PC monitor. Read `GLOSSARY.md` (especially Signal Rate, Game Rate, Unique Frame, Repeated Frame), `docs/adr/0002-frame-generation-stays-outside-the-viewer.md`, the developer note from ticket 02, and the prototype's findings note, which records whether Repeated Frames arrive bit-identical and what comparison worked.

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
- **Cheap comparison.** Comparing two 1080p frames must cost well under a millisecond on the capture path. Comparing a sparse sample of rows or blocks spread over the frame is fine; the prototype findings say whether exact equality is enough or a tolerance is required. Keep the tolerance, if any, a single tunable constant.
- **Keep recent Unique Frames.** Retain the last two or three Unique Frames (as the pipeline's frame objects) so a later Frame Generation or sharpening step has them. No behaviour depends on this yet.
- **Expose the numbers.** The Processor publishes the current Signal Rate (frames arriving), Game Rate (Unique Frames per second) and the count of Repeated Frames dropped, so the Help or debug display and the Lossless Scaling guide can show them. A simple way for the user to see "arriving 60 / unique 30" is required for verification; a line in the window title behind a debug switch or a debug log is enough until the menu exists.
- **Testing**: this is pure logic and must be unit tested without a Capture Card, using synthetic frame sequences.

## Known unknowns

- Whether Repeated Frames from this card are bit-identical. If the prototype found they differ slightly, use the tolerance it found; if the prototype did not settle it, implement exact comparison first with the tolerance constant at zero and say in your report that it needs a user check.
- Games with uneven pacing (for example a 30 fps game that occasionally shows a picture for three signal frames, or a 40 fps mode). Auto must simply present whatever is unique; the Game Rate figure should be a smoothed estimate that does not flicker between values every second.
- Static scenes: a paused game or a still menu produces long runs of identical frames, so nothing is presented for a while. That is correct for the picture, but make sure it causes no problem: the window must still repaint when resized or uncovered, and the Game Rate estimate must not divide by zero or report nonsense. Ticket 06 must be able to distinguish "no new pictures because the scene is still" from "no signal", so keep the count of frames arriving separate from the count of Unique Frames.
- How Lossless Scaling's WGC capture mode treats presents is unverified (research suggests DXGI mode follows the source's present rate while WGC follows the desktop compositor). Nothing to implement; ticket 10 covers the user-side setting.

## Out of scope

The menu entry for the override (08); saving the override (09); any Frame Generation, interpolation or sharpening; changing the capture mode; 120 fps.

## Acceptance criteria

- [ ] With a 30 fps game, the Viewer presents about 30 frames per second and reports Signal Rate 60, Game Rate 30.
- [ ] With a 60 fps game, the Viewer presents about 60 frames per second and reports Game Rate 60.
- [ ] A Unique Frame is forwarded as soon as it is recognised; the comparison adds no frame of delay and no measurable cost on the capture path.
- [ ] The override setting exists on the Processor with values Auto (default), 30 and 60, behaving as defined above, and can be changed while running.
- [ ] The last two or three Unique Frames are retained and available to later Processor steps.
- [ ] Signal Rate, Game Rate and dropped Repeated Frame counts are available to the rest of the app and visible to the user in some simple form.
- [ ] A still picture (paused game) does not break anything: the window still repaints correctly and the rate estimate stays sane.
- [ ] Unit tests with synthetic sequences cover: all-unique input (60), strictly doubled input (30), tripled and mixed pacing, a long still run followed by motion, a change from 30 to 60 mid-stream, each override value, and the tolerance boundary if a tolerance is used.

## How to verify

Yourself: build, run the unit tests, and run the app to confirm that it still shows the picture and that the reported numbers appear.

For the user. End your work by reporting this checklist:

1. Start a 30 fps game. What do the arriving and unique numbers read? (Expected: about 60 and about 30.)
2. Start a 60 fps game or the Xbox dashboard. What do they read? (Expected: about 60 and about 60.)
3. Does the picture look exactly as before, with no new stutter?
4. Pause the game, then resize the window. Does the picture stay visible?
5. Turn on Lossless Scaling (LSFG, fixed X2) on the window during the 30 fps game. Does it show a base of 30 and an output of 60?
