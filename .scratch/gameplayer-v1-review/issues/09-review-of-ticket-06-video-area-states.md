# 09: Review of ticket 06: Video Area states and automatic recovery

**What to do:** Review the Video Area state machine (Loading, No Capture Card, Card busy, No signal, Playing, Error), the FeedKeeper that follows it and restarts picture and sound together, the text drawing in the Video Area, and recovery from unplug, busy card, no signal and sleep.

**Blocked by:** 08 (Review of ticket 02).

**Status:** ready-for-agent

## Context

GamePlayer is a personal Windows 11 C++ / Direct3D 11 app (the "Viewer") that shows a game Console's picture and sound from a USB Capture Card with low Latency. Ticket 06 made it say what is wrong inside the Video Area and fix itself without the user's help. A static picture is normal and must never mean no signal.

## Rules for this review

1. **Self-contained, tool-neutral.** This ticket is all you are given. Open the repository at `S:\GamePlayer` yourself, on branch `gameplayer-v1` (stay on it; there is no remote). Paths are relative to the repository root.
2. **Blind.** Another reviewer does this ticket independently. Do not open or search any file in `.scratch/gameplayer-v1-review/findings/` except the one you create.
3. **Read-only.** Change no code, test, build file, spec, ticket or document; make no git changes. The only file you write is your findings file. Build output in `build\` is fine.
4. **What you may run.** `test.bat` (preferred), `build.bat`, read-only git, file reading and searching. Never run `GamePlayer.exe` in any form: the Capture Card is exclusive and the user may be using it. Never stop or kill a process. Never change Windows settings. If a build fails because a file is locked or another build runs, wait, try once more, then record "not built".
5. **The tip, not history.** Review the files as they are now; commits are a map (verify with `git log`). Where a later decision changed the original ticket, judge against the later decision.
6. **Scope.** Owned files as a whole; shared files only for the listed parts. Anything else: one line in "Noticed outside this ticket's scope".
7. **Known items** below are not discoveries; comment on them in their template section.
8. **Ignore** `.claude\`, `build\`, `third_party\`, `prototype\` code.
9. **Severity.** Blocker: wrong behaviour, crash, data race, or an acceptance criterion unmet. Should fix: a real weakness worth fixing. Nit: style, naming. Every finding gives severity, axis, path:line, what is wrong, why it matters, suggested fix in words, confidence. Use the glossary's terms.

## Read first

- `GLOSSARY.md`, `docs/adr/0001-*.md`, `docs/adr/0002-*.md`, `docs/DEVELOPING.md` (the `Signal:` log line, `feed_keeper.*`, `video_area_state.h`), `prototype/FINDINGS.md`.
- The original ticket: `.scratch/gameplayer-v1/issues/06-video-area-states-and-recovery.md`.

## Commits (map only)

`4dd3db1` (core), `c55d06d` (platform), merge `3f6075f`. Later: `51414aa` (choice and resolution fallback), `9af8099` (Processor wiring), `855cfac` (choice by identifier).

## Later decisions

- No signal is: open, no frame for 3 s; then reopen every 5 s. Non-playing states retry every second; a missing sound retries every 5 s.
- Card busy is recognised from a fixed list of HRESULTs in `video_area_state.cpp`.
- A resolution that does not start falls back to the default mode (ticket 08).

## Scope

Owned: `src/core/video_area_state.*`, `src/core/video_area_text.*`, `src/core/luma_summary.*`, `src/platform/feed_keeper.*`, `src/platform/text_overlay.*`, and the tests `video_area_state_test`, `video_area_text_test`, `luma_summary_test`.

Shared, only these parts:

- `src/platform/mf_source.*`, `src/core/source.h`: events, failure stages and codes, the signal probe, reopening (owner: ticket 02 review).
- `src/platform/presenter.*`: `ShowText`, `ShowPicture`, presents for state texts (ticket 02 review).
- `src/platform/sound.*`: start, stop and failure reporting (ticket 03 review).
- `src/platform/keep_awake.*`: held only in Playing (ticket 05 review).

## Axes and emphasis

1. **Correctness (main):** the state machine's transitions and timers; posted events and the generation counter; restarts on the window thread while the Source's thread delivers; device notifications and resume; picture and sound restarted together; Direct2D text on the Presenter's thread.
2. **Latency:** retries and restarts block the window thread (how long, how often); nothing in Playing adds delay; the per-second probe is cheap and off the frame path.
3. **Spec:** each criterion; never no signal from a still picture or a Game Rate near zero; readable at small sizes.
4. **Standards:** pure state machine with injected time, tested; the Source reports events and draws nothing.

## Known and not yet verified by hand

- No-signal detection with HDMI pulled is a guess (no frames for 3 s, reopen every 5 s); the HDMI-pull and Console-off checks were never done.
- Card unplug and replug, and sleep and resume, were never hand tested.
- The card-busy retry blocks the window thread 15-31 ms about every 1.25 s.
- State texts cause extra presents.
- Of the busy codes, only `0xC00D3704` is recorded in the code as seen on the card; the others are listed without an observation.
- The default mode requires 1080p60; a device without it shows Error.
- The user checklist of ticket 06 is pending.

## Acceptance criteria

- [ ] Each acceptance criterion of ticket 06 has a verdict.
- [ ] Every state and transition is traced against the tests.
- [ ] Each robustness target of the ticket is traced through the code.
- [ ] The findings file is written per the template.

## Where to write the findings

Copy `.scratch/gameplayer-v1-review/FINDINGS-TEMPLATE.md` to `.scratch/gameplayer-v1-review/findings/ticket-06-<reviewer>.md` (`<reviewer>`: your short lowercase name). If that file exists, pick another name and do not open it.
