# GamePlayer v1 tickets

GamePlayer is a personal Windows 11 app (the "Viewer") that shows a game Console's picture and sound from a USB Capture Card on the PC monitor with low Latency. These tickets break version 1 into vertical slices. Each ticket is self-contained and written for an implementing agent with no prior context; each tells the agent which repo documents to read first (`GLOSSARY.md`, `docs/adr/0001-...`, `docs/adr/0002-...`, `docs/research/capture-viewer-research.md`).

All tickets are in `issues/`.

## Plan review: outcome

Ticket 01, the throwaway prototype, is done. Its findings are in `prototype/FINDINGS.md`. The prototype code is throwaway and will not be kept; only what it taught us was carried into the tickets.

The user reviewed the plan against those findings and approved it with revisions, now written into the tickets: the shader reads colour range and matrix from the media type (02); Media Foundation works driverless, no DirectShow (02); an explicit rule to skip the card's "Not To Use" audio endpoint, plus the prototype's working audio numbers (03); a near-duplicate tolerance, static-Feed handling and an optional FPS counter on the F key (04); "no signal" cannot be detected by frames stopping, and the busy-card HRESULT is still to be found (06); the card's untested NV12 1080p120 mode is never offered (08).

All tickets, 02 to 10, are done (user hand checks pending where noted in each ticket).

Tickets 04 and 10 were released after hand tests. Outcome of the hand tests that gated them: the user could not find a way to run a game at 30 fps, so bit-identical Repeated Frames at 30 fps remain unverified; ticket 04 is built with a tunable tolerance and diagnostic logging instead. The user will judge Lossless Scaling by taking a 60 fps game to 120 on their 144 Hz monitor (ticket 10).

## Tickets and blocking edges

| # | Ticket | Blocked by | Status |
|---|--------|------------|--------|
| 01 | Throwaway prototype proving the pipeline | None | done |
| 02 | App skeleton showing the Feed picture in a window | 01 + plan review | done (user hand checks pending) |
| 03 | Feed sound | 02 | done (user hand checks pending) |
| 04 | Unique Frames and Game Rate detection | 02 | done (user hand checks pending) |
| 05 | Fullscreen and window behaviour | 02 | done (user hand checks pending) |
| 06 | Video Area states and automatic recovery | 02, 03 | done (user hand checks pending) |
| 07 | Audio controls: volume, mute and Audio Delay | 03 | done (user hand checks pending) |
| 08 | Menu bar | 04, 05, 07 | done (user hand checks pending) |
| 09 | Remembered settings | 08 | done (user hand checks pending) |
| 10 | Lossless Scaling readiness and user setup guide | 04, 05 | done (user hand checks pending) |

## Order of work

**At most two tickets are worked in parallel at any time.**

1. **01**: done.
2. **02** alone. It creates the project structure everything else builds on.
3. **03, 04, 05** once 02 is done, two at a time. They touch different parts (audio, Processor, window). 04 is now released.
4. **06** (after 03), **07** (after 03) and **10** (after 04 and 05), two at a time. 04, 06 and 07 all draw text in the Video Area (FPS counter, state messages, audio feedback); whichever runs first builds the text drawing and the later ones reuse it, so if two run at the same time, expect a small merge.
5. **08** after 04, 05 and 07.
6. **09** after 08.

Longest chain: 02, 03, 07, 08, 09. Ticket 08 also waits on 04.

## Notes for whoever dispatches these

- Parallel tickets all edit the same young codebase. Running them in separate worktrees and merging one at a time is safer than letting them write to one working copy at once.
- No agent can see the monitor or hear the audio. Every ticket ends with a short manual checklist for the user; a ticket is not finished until the user has run it.
- Testing policy for v1: unit tests for pure logic, code review for the rest, user checks for picture and sound.
- The C++ toolchain is present (VS Build Tools 2022, MSVC 14.44, Windows SDK 10.0.26100.0, per `prototype/FINDINGS.md`; the prototype did not use CMake, so ticket 02 must confirm CMake is available). Installing tools is the user's decision.
- Excluded from v1 everywhere: recording, streaming, screenshots, sharpening, in-app Frame Generation, HDR, VRR capture, 120 fps modes, an audio output picker, an installer, formal latency measurement.
