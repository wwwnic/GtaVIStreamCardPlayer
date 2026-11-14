# 06: Video Area states and automatic recovery

**What to build:** GamePlayer always tells the user what is going on, inside the Video Area, and fixes itself. On launch it shows a loading message until the first picture arrives. If no Capture Card is connected, if the card has no signal because the Console is off, or if another program is holding the card, a plain message says so. The Viewer keeps retrying on its own, and the moment the problem goes away (card plugged back in, Console switched on, OBS closed) the picture and sound return without the user touching GamePlayer.

**Blocked by:** 02 (App skeleton showing the Feed picture in a window) and 03 (Feed sound).

**Status:** ready-for-agent

## Context

GamePlayer is a small personal Windows 11 app (the "Viewer") that shows a game Console's picture and sound from a USB Capture Card on the PC monitor. The user is replacing OBS partly because, when something is wrong, it takes them fifteen minutes to work out what; clear states and self-recovery are a core reason this app exists. The user wants zero configuration: start the app and the Feed appears, or a message says why not. Read `GLOSSARY.md`, the ADRs in `docs/adr/`, the developer note from ticket 02, and the prototype's findings note, `prototype/FINDINGS.md` (it records how the card behaves with a static picture; it does not record the error seen when another program holds the card, which is still unknown).

Hardware: Xbox Series S; AVerMedia Live Gamer Ultra S GC553 Pro over USB; Windows 11. Other programs that may hold the card: OBS and AVerMedia's own software (Streaming Center, Assist Central).

## Decisions already made

- **Messages appear in the Video Area**, drawn by the Viewer itself into its Direct3D surface (DirectWrite / Direct2D text rendered into the swap chain is the intended approach). No message boxes, no separate dialogs, no status bar.
- **States** (name them clearly in code; this is a small state machine and must be unit tested):
  - *Loading*: the app has started or is reconnecting and no picture has arrived yet.
  - *No Capture Card*: no video capture device is present.
  - *Card busy*: a device is present but cannot be opened because another program holds it. The message should name the likely culprits ("another app is using the capture card, for example OBS or AVerMedia software").
  - *No signal*: the card is open but the Console is off or disconnected.
  - *Playing*: the Feed is showing. No message.
  - *Error*: anything unexpected, with a short human sentence and, in smaller text, the technical code, so the user can report it.
- **Self-recovery**: in every non-playing state the Viewer retries automatically at a modest interval (about once a second is fine; do not spin). It also reacts immediately to device arrival and removal notifications (`WM_DEVICECHANGE` / device notification registration) rather than waiting for the next retry. No "retry" button.
- **Both picture and sound recover.** Audio from ticket 03 must be stopped and restarted together with video so they come back together and in step. If the picture is fine but the sound cannot start (for example no output device), keep the picture and show a small, unobtrusive note; do not block the Feed.
- **Robustness targets**: unplugging and replugging the card's USB cable while running; switching the Console off and on; starting GamePlayer before the card is plugged in; starting GamePlayer while OBS holds the card and then closing OBS; PC sleep and resume. None of these may crash or hang the app, and each must end in Playing without a restart.
- **Still pictures are not "no signal".** Ticket 04 (which may or may not be finished when you start) makes the Viewer present only Unique Frames, so a paused game produces no new pictures while frames are still arriving from the card. Never base signal detection on frames presented, and never on the Game Rate.
- **"No signal" cannot be detected by frames stopping.** The prototype showed that the card keeps delivering a steady 60 frames per second of a static picture while the Console is idle. It may well keep delivering 60 frames per second of its own placeholder picture when no HDMI signal is present at all. So "frames stopped arriving" is at best a secondary symptom (a stalled or removed device), not the definition of no signal. Detection has to come from something else, and you have to find out what.
- **Wording**: short, plain English, large enough to read from a sofa, centred, light text on black. One line saying what is wrong, optionally one smaller line saying what to do.
- **Sleep prevention** from ticket 05 (if present) should be held only in the Playing state.
- **Structure**: the state machine is pure logic (events in, state out, with timers injected) so it can be unit tested without a device. The Source reports events (opened, frame arrived, failed with code, device removed); it does not draw anything.

## Known unknowns

- **What the card does with HDMI unplugged is unknown, and you cannot test it yourself.** You cannot unplug the HDMI cable or switch the Console off. Find out what you can without that: inspect what the Source delivers and any signals Media Foundation or the device offers (for example media type change events, stream errors or tick events, sample flags, frame contents such as a uniform black or a fixed vendor picture, the audio stream going silent or stalling). Then design the detection so that a user check can confirm it: log once per second, in a form the user can send back, every value your detection looks at, so one run with the HDMI cable pulled and replugged (and one with the Console switched off) tells us what the card really does. Say clearly in your report which parts of no-signal detection are verified and which are unverified guesses. If the card turns out to send its own "no signal" picture as ordinary frames and nothing reliable distinguishes it, it is acceptable to simply show that picture and treat the state as Playing; say so in your report rather than guessing at a heuristic that could fire during a dark or paused game.
- **The failure when another app holds the card is still unknown.** The prototype did not record it. It may be a sharing-violation style HRESULT from Media Foundation on activation, on the first read, or in the read callback, or the device may open but deliver nothing. Determine it yourself, for example by opening the device twice (a second instance of a small test program, or two readers in one process), and log the exact HRESULT and where it appeared. Record what you found in your report. If busy cannot be distinguished from other failures, show a combined message ("can't open the capture card; another app may be using it") rather than guessing wrongly.
- Whether the card re-enumerates with the same identity after a replug. Re-run device selection from scratch on every reconnect rather than holding on to an old device handle.

## Out of scope

Menu bar and device picker (08); remembered settings (09); volume and Audio Delay feedback (07, which may reuse the text drawing built here); logging UI; notifications outside the window; diagnosing Console-side problems such as HDCP beyond a generic no-signal or error message.

## Acceptance criteria

- [ ] On launch, a loading message is visible in the Video Area until the first picture arrives.
- [ ] With no Capture Card connected, the Video Area says so; plugging the card in brings the Feed up without restarting.
- [ ] With the card connected and the Console off, the Video Area says there is no signal (or shows the card's own no-signal picture, if that is what the card sends); switching the Console on brings the Feed up without restarting.
- [ ] With the card held by another program, the Video Area says the card is in use by another app; closing that program brings the Feed up without restarting.
- [ ] Unplugging the card while playing switches to the no-card message without a crash or hang; replugging restores picture and sound.
- [ ] After PC sleep and resume, the Feed returns by itself.
- [ ] Picture and sound always recover together; a sound-only failure does not block the picture.
- [ ] A paused or static game is never reported as no signal.
- [ ] Unexpected failures show a short readable sentence plus the technical code.
- [ ] Messages are readable in windowed and fullscreen modes and at small window sizes.
- [ ] No-signal detection does not rely on frames stopping or on presented frames; the values it uses are logged once per second, and your report states which parts are verified and which await the user's HDMI-unplug check.
- [ ] The HRESULT (and where it appears) when another app holds the card has been determined, for example by opening the device twice, and is recorded in your report.
- [ ] Unit tests cover the state machine: every state, every transition listed above, retry timing, and the rule that a static picture or a Game Rate near zero never means no signal.

## How to verify

Yourself: build, run the unit tests, and run the app with whatever hardware state is available. You can exercise the no-card path by running without the card and, if the card is present, the busy path by opening the device in a second small test program or OBS if installed. Report honestly which states you could not exercise.

For the user. End your work by reporting this checklist:

1. Start GamePlayer with the card unplugged. Is there a clear message? Plug the card in. Does the game appear by itself?
2. Switch the Xbox off. What does the window show? Switch it on. Does the game come back with sound?
3. While playing, pull the HDMI cable out of the card for ten seconds, then put it back. What does the window show while it is out? Does the game come back? Send the log from this run.
4. Open OBS so it uses the card, then start GamePlayer. Does it say another app is using the card? Close OBS. Does the game appear?
5. While playing, pull the card's USB cable and put it back. Does everything return?
6. Pause a game, or leave the Xbox idle on a still screen, for a minute. Does the picture stay, with no "no signal" message?
7. Put the PC to sleep and wake it. Does the game come back?
