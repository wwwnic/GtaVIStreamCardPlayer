# 03: Feed sound

**What to build:** When GamePlayer shows the Feed, the user also hears the Console's sound through whatever output Windows is currently using, with no configuration. If the user switches the Windows default output (for example from speakers to a headset), the sound follows. The sound stays in step with the picture for hours without clicks, gaps or slowly growing delay.

**Blocked by:** 02 (App skeleton showing the Feed picture in a window).

**Status:** done (user hand checks pending)

## Context

GamePlayer is a small personal Windows 11 app (the "Viewer") that shows a game Console's picture and sound from a USB Capture Card on the PC monitor, replacing OBS for that job. Low Latency is the top priority. Read `GLOSSARY.md`, `docs/adr/0001-native-cpp-direct3d11-stack.md`, the developer note written in ticket 02, and the prototype's findings note, `prototype/FINDINGS.md` (it records which audio endpoints the card exposes and how the right one was matched). `docs/research/capture-viewer-research.md` has background.

Hardware: Xbox Series S set to stereo audio; AVerMedia Live Gamer Ultra S GC553 Pro over USB (its sound appears to Windows as an audio capture endpoint); Windows 11.

## Decisions already made

- **API**: WASAPI directly (ADR 0001). Capture from the Capture Card's audio endpoint in shared mode, event-driven. Render to the Windows default output device in shared mode, event-driven. Use `IAudioClient3` low-latency shared periods where the device offers them.
- **Stereo only.** The card delivers stereo PCM outside AVerMedia's own software; 5.1 is not available and not wanted.
- **Output device**: always the Windows default render device, and it follows changes to that default while running (register for default-device change notifications and re-open the render side). There is deliberately no output device picker in the app.
- **Input device**: found automatically. Match the audio capture endpoint whose name contains the selected Capture Card's device name. No user choice needed.
- **Two endpoints match the card's name; pick the right one.** The GC553 Pro exposes two audio capture endpoints that both contain its name: "HDMI (Live Gamer ULTRA S GC553Pro)", which carries the Feed sound and is the correct one, and "Not To Use (Live Gamer ULTRA S GC553Pro)". In the prototype the right one won only because of enumeration order, which is not reliable. Make the rule explicit: among endpoints matching the card's name, prefer one whose name contains "HDMI", and never choose one whose name contains "Not To Use", even as a fallback. Log every capture endpoint and which one was chosen. This rule is pure logic (a list of names in, a choice out) and must be unit tested, including the case where "Not To Use" is listed first.
- **Known-good starting point from the prototype.** On this machine the prototype ran with 0 underruns and 0 overruns using: capture and render both 48 kHz, 32-bit float, stereo, shared mode, event-driven; a render buffer of about 22 ms; a ring buffer of 40 ms capacity that starts playing once 10 ms is primed. Start from these numbers. The prototype had no drift correction and its test was short, so they are a starting point, not a proven long-session setting.
- **Buffering**: a small ring buffer between capture and render, targeting roughly 10-20 ms of fill. Sound and picture are not synchronised by timestamps; both paths simply run as fast as they can, which lands them within a few tens of milliseconds of each other.
- **Drift correction, automatic**: the Capture Card and the output device run on different clocks, so the buffer slowly fills or drains. Hold the fill level at its target using very small adaptive resampling (changing the playback rate by a tiny fraction). This must be inaudible. On a hard underrun or overrun, recover by resetting to the target fill rather than letting delay accumulate.
- **Format differences**: the capture and render endpoints may have different sample rates or sample formats (for example 48 kHz versus 44.1 kHz, 16-bit versus float). Handle conversion in the app; do not rely on exclusive mode.
- **Structure**: audio is its own deep module with a small interface (start, stop, and later: set volume, set mute, set Audio Delay). Keep the pure parts (ring buffer, fill-level controller, resampling ratio calculation) free of Windows calls so they can be unit tested.
- Leave room for ticket 07: volume, mute and a user-set Audio Delay will be added on top. The Audio Delay will be implemented as extra buffered delay, so do not hard-code the target fill in a way that prevents adding to it.

## Known unknowns

- Whether the card's audio endpoint names stay the same across driver updates or replugs. If no endpoint matches by name, fall back to the first active capture endpoint whose name suggests a capture device, log what was chosen, and mention it in your report; ticket 08 can add a manual choice if needed.
- How small the buffer can be on this machine without glitches over a long session. Start from the prototype's numbers above and make the target one constant that is easy to tune.
- Whether the card keeps delivering audio samples when the Console is off or the signal is lost. Handle both silence and a stalled stream without crashing; full recovery behaviour belongs to ticket 06.

## Out of scope

Volume, mute and Audio Delay controls (07); on-screen messages and recovery from the card disappearing (06); output device picker (not in v1 at all); surround sound; recording.

## Acceptance criteria

- [ ] With the card connected and the Console on, launching GamePlayer plays the Feed sound through the Windows default output with no user action.
- [ ] Changing the Windows default output device while the app runs moves the sound to the new device within a couple of seconds, without restarting the app.
- [ ] Capture and render use WASAPI shared mode, event-driven; mismatched sample rates and formats between the two endpoints are handled.
- [ ] The "HDMI (Live Gamer ULTRA S GC553Pro)" capture endpoint is chosen automatically; an endpoint named "Not To Use ..." is never chosen, whatever the enumeration order, and unit tests cover this.
- [ ] Sound is stereo.
- [ ] The buffer fill level is held near its target by automatic drift correction; delay does not grow over a long session, and underruns or overruns recover to the target.
- [ ] Unit tests cover the pure logic: endpoint choice from a list of names, ring buffer behaviour (wrap-around, underrun, overrun) and the fill-level controller (given a fill history, the correction moves the fill toward the target and stays within inaudible bounds).
- [ ] Starting the app with no card, or with the default output device missing, does not crash; the picture still works if the card is present.
- [ ] Closing the app stops audio cleanly with no hang.

## How to verify

Yourself: build, run the unit tests, start and stop the app, and check your debug log for the chosen endpoints, formats, buffer target and a periodic fill-level reading that stays near the target.

For the user. End your work by reporting this checklist:

1. Start GamePlayer with the Xbox on. Do you hear the game by itself?
2. Is the sound clean: no crackling, no gaps, no robotic tone?
3. Does the sound match the picture (try a menu click sound or a gunshot)?
4. While it plays, change the Windows output device (for example speakers to headset). Does the sound move there?
5. Leave it running for an hour. Is the sound still in step with the picture and still clean?
