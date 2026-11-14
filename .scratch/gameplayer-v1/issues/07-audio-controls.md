# 07: Audio controls: volume, mute and Audio Delay

**What to build:** The user controls the Feed's sound from the keyboard while the GamePlayer window has focus. Up and Down change the volume, M mutes and unmutes, and J and K shift the sound earlier or later relative to the picture in 50 ms steps (the Audio Delay), the same idea and the same keys as VLC. Each key press shows a brief confirmation in the Video Area (for example "Volume 80%", "Muted", "Audio delay +100 ms") that fades away on its own.

**Blocked by:** 03 (Feed sound).

**Status:** needs-plan-review

**Gate:** Do not start this ticket until the user has reviewed the findings note from ticket 01 (the throwaway prototype) and has confirmed or revised the plan. This ticket may be rewritten at that review; if its status still reads needs-plan-review, stop and ask the user rather than beginning work.

## Context

GamePlayer is a small personal Windows 11 app (the "Viewer") that shows a game Console's picture and sound from a USB Capture Card on the PC monitor. Read `GLOSSARY.md` (see Audio Delay and Video Area), the ADRs in `docs/adr/`, the developer note from ticket 02 and the audio module built in ticket 03.

The audio path from ticket 03: WASAPI capture from the Capture Card's endpoint, a small ring buffer with automatic drift correction, WASAPI render to the Windows default output. Sound and picture are not timestamp-synchronised; they both run as fast as possible.

## Decisions already made

- **Keys** (active when the GamePlayer window has focus, in windowed and fullscreen modes):
  - Up / Down: volume up / down in steps of 5 percentage points, range 0-100%.
  - M: toggle mute. Muting keeps the volume value; unmuting restores it.
  - J: Audio Delay minus 50 ms. K: Audio Delay plus 50 ms. (This mirrors VLC, which the user knows.)
  - The complete v1 hotkey set is F11, Esc, M, Up, Down, J, K. Add no others.
- **Volume is the app's own level**, applied to the Viewer's audio stream (the app's sample gain or its per-app session volume), not the Windows master volume. Default 100%.
- **Audio Delay semantics**: a positive value makes the sound play later. Default 0 ms. Range 0 to +500 ms in 50 ms steps.
  - Negative values are not possible in a meaningful way: the sound cannot be played before it arrives, and the Viewer deliberately never delays the picture (delaying video would add Latency, which is the one thing this app must not do). So J at 0 ms stays at 0 ms and the feedback says so (for example "Audio delay 0 ms (minimum)"). This is a judgement recorded here so nobody implements a video delay to make negative values work.
- **Implementation of Audio Delay**: extra buffered audio added on top of the drift-corrected target fill from ticket 03. Changing the delay must not produce a loud click; a brief, quiet transition (short fade or a moment of silence while the buffer adjusts) is acceptable. Drift correction keeps working around the new target.
- **On-screen feedback**: one short line of text in the Video Area, visible for about 1.5 seconds after the last key press, then gone. Repeated presses update the same line rather than stacking. It must not cover the centre of the picture; a corner is right. If ticket 06 has already built text drawing for the Video Area, reuse it; otherwise build the minimal text drawing here (DirectWrite / Direct2D into the swap chain) in a way ticket 06 can reuse.
- **Persistence is not part of this ticket.** Ticket 09 saves volume, mute and Audio Delay. Expose them through the audio module's interface as plain get / set values so ticket 08 (menu) and ticket 09 (settings) can use them.
- **Pure logic is unit tested**: stepping and clamping of volume and Audio Delay, mute toggling that preserves volume, and the feedback line's show / hide timing (with the clock injected).

## Known unknowns

- Whether the feedback text drawn over the Feed interferes with Lossless Scaling's frame counting. It should not matter in practice (it is brief), but drawing the overlay causes extra presents if the Viewer redraws while the picture is unchanged. Keep overlay-driven redraws to the minimum needed to show and hide the text, and do not animate a long fade frame by frame.
- Whether a useful Audio Delay is ever needed with this card. The user suspects not; keep the implementation small.

## Out of scope

Menu entries for these controls (08); saving them between runs (09); an output device picker (not in v1); per-channel balance; equaliser; any picture delay; global hotkeys that work when the window does not have focus.

## Acceptance criteria

- [ ] Up and Down change the volume in 5-point steps between 0% and 100%; the change is audible immediately.
- [ ] M mutes and unmutes; unmuting returns to the previous volume.
- [ ] K increases and J decreases the Audio Delay in 50 ms steps, clamped to 0-500 ms, and the sound shifts accordingly without a loud click.
- [ ] The volume applies to GamePlayer's own sound only, not the Windows master volume.
- [ ] Each key press shows a short confirmation line in a corner of the Video Area that disappears after about 1.5 seconds; repeated presses update it in place.
- [ ] The keys work in both windowed and fullscreen modes when the window has focus, and do nothing when another app has focus.
- [ ] Volume, mute and Audio Delay are available as get / set values on the audio module's interface for the menu and settings tickets.
- [ ] Drift correction from ticket 03 still holds the buffer at its (shifted) target after the Audio Delay changes.
- [ ] Unit tests cover stepping, clamping, mute behaviour and feedback timing.

## How to verify

Yourself: build, run the unit tests, run the app and confirm via your debug log that key presses change the values and the buffer target.

For the user. End your work by reporting this checklist:

1. Press Up and Down. Does the volume change, with a small message on screen?
2. Press M twice. Does the sound stop and come back at the same level?
3. Press K a few times. Does the sound clearly lag the picture more each time? Press J back to zero. Is it back in step?
4. Does the message disappear by itself after a moment, and stay out of the way of the game?
5. Does all of that work in fullscreen too?
