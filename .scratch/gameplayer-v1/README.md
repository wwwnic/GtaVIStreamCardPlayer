# GamePlayer v1 tickets

GamePlayer is a personal Windows 11 app (the "Viewer") that shows a game Console's picture and sound from a USB Capture Card on the PC monitor with low Latency. These tickets break version 1 into vertical slices. Each ticket is self-contained and written for an implementing agent with no prior context; each tells the agent which repo documents to read first (`GLOSSARY.md`, `docs/adr/0001-...`, `docs/adr/0002-...`, `docs/research/capture-viewer-research.md`).

All tickets are in `issues/`.

## Plan-review gate: only ticket 01 is ready

**Right now, ticket 01 is the only ticket that may be started.** It is the throwaway prototype, and it exists to test the plan's assumptions on the real hardware.

When ticket 01 is done, all work stops. Its findings note lists which planned decisions held, which need revising, and any breaking surprises. The user then reviews the plan against those findings and either confirms it or revises it. Tickets 02 to 10 may be rewritten, re-ordered, split or dropped at that review.

Until the user has done that review, tickets 02 to 10 carry the status **needs-plan-review** and must not be started by anyone, whatever their blocking edges say. After the review, the user (or the coordinating agent on the user's instruction) changes each surviving ticket's status to ready-for-agent. An agent handed a ticket that still reads needs-plan-review should stop and ask.

## Tickets and blocking edges

| # | Ticket | Blocked by | Status |
|---|--------|------------|--------|
| 01 | Throwaway prototype proving the pipeline | None | ready-for-agent |
| 02 | App skeleton showing the Feed picture in a window | 01 + plan review | needs-plan-review |
| 03 | Feed sound | 02 | needs-plan-review |
| 04 | Unique Frames and Game Rate detection | 02 | needs-plan-review |
| 05 | Fullscreen and window behaviour | 02 | needs-plan-review |
| 06 | Video Area states and automatic recovery | 02, 03 | needs-plan-review |
| 07 | Audio controls: volume, mute and Audio Delay | 03 | needs-plan-review |
| 08 | Menu bar | 04, 05, 07 | needs-plan-review |
| 09 | Remembered settings | 08 | needs-plan-review |
| 10 | Lossless Scaling readiness and user setup guide | 04, 05 | needs-plan-review |

## Order of work

The order below from step 2 onward is the current plan and applies only once the plan review has released the tickets.

1. **01** alone, then the hard stop and plan review described above.
2. **02** alone. It creates the project structure everything else builds on.
3. **03, 04, 05** can run in parallel once 02 is done. They touch different parts (audio, Processor, window).
4. **06** (after 03), **07** (after 03) and **10** (after 04 and 05) can run in parallel with each other. 06 and 07 both draw text in the Video Area; whichever runs second reuses the first one's text drawing, so if they run at the same time, expect a small merge.
5. **08** after 04, 05 and 07.
6. **09** after 08.

Longest chain: 01, 02, 03, 07, 08, 09.

## Notes for whoever dispatches these

- Parallel tickets all edit the same young codebase. Running them in separate worktrees and merging one at a time is safer than letting them write to one working copy at once.
- No agent can see the monitor or hear the audio. Every ticket ends with a short manual checklist for the user; a ticket is not finished until the user has run it.
- Testing policy for v1: unit tests for pure logic, code review for the rest, user checks for picture and sound.
- Ticket 01 may find the C++ toolchain missing. It must report that and wait; installing tools is the user's decision.
- Excluded from v1 everywhere: recording, streaming, screenshots, sharpening, in-app Frame Generation, HDR, VRR capture, 120 fps modes, an audio output picker, an installer, formal latency measurement.
