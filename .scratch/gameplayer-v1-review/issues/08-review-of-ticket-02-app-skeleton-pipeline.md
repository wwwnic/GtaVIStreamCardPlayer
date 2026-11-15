# 08: Review of ticket 02: App skeleton and the Source / Processor / Presenter pipeline

**What to do:** Review the app's foundation as it stands now: the build and test setup, the window's start-up and shutdown, and the whole picture pipeline from the Media Foundation capture callback through the Processor interface to the Presenter's thread and the swap chain.

**Blocked by:** 07 (Review of ticket 03).

**Status:** ready-for-agent

## Context

GamePlayer is a personal Windows 11 C++ / Direct3D 11 app (the "Viewer") that shows a game Console's picture and sound from a USB Capture Card with low Latency. Ticket 02 set up the structure every later ticket builds on: Source, Processor and Presenter (ADR 0002), a flip-model swap chain presenting on arrival, colour conversion in the app's own shader. Later tickets added much to these files; this review owns them as a whole.

## Rules for this review

1. **Self-contained, tool-neutral.** This ticket is all you are given. Open the repository at `S:\GamePlayer` yourself, on branch `gameplayer-v1` (stay on it; there is no remote). Paths are relative to the repository root.
2. **Blind.** Another reviewer does this ticket independently. Do not open or search any file in `.scratch/gameplayer-v1-review/findings/` except the one you create.
3. **Read-only.** Change no code, test, build file, spec, ticket or document; make no git changes. The only file you write is your findings file. Build output in `build\` is fine.
4. **What you may run.** `test.bat` (preferred), `build.bat`, read-only git, file reading and searching. Never run `GamePlayer.exe` in any form: the Capture Card is exclusive and the user may be using it. Never stop or kill a process. Never change Windows settings. If a build fails because a file is locked or another build runs, wait, try once more, then record "not built".
5. **The tip, not history.** Review the files as they are now; commits are a map (verify with `git log`). Where a later decision changed the original ticket, judge against the later decision.
6. **Scope.** Owned files as a whole, including parts other reviews also look at for their feature. Anything outside: one line in "Noticed outside this ticket's scope".
7. **Known items** below are not discoveries; comment on them in their template section.
8. **Ignore** `.claude\`, `build\`, `third_party\`, `prototype\` code.
9. **Severity.** Blocker: wrong behaviour, crash, data race, or an acceptance criterion unmet. Should fix: a real weakness worth fixing. Nit: style, naming. Every finding gives severity, axis, path:line, what is wrong, why it matters, suggested fix in words, confidence. Use the glossary's terms.

## Read first

- `GLOSSARY.md`, `docs/adr/0001-*.md`, `docs/adr/0002-*.md`, `docs/DEVELOPING.md` (all of it), `prototype/FINDINGS.md`.
- The original ticket: `.scratch/gameplayer-v1/issues/02-app-skeleton-feed-picture.md`.

## Commits (map only)

`ad95828`, merge `d443483`. Later commits that changed owned files: `c55d06d` (Presenter texts, Source events), `9af8099` (text with the next frame), `51414aa` (V-sync, mode switch), `855cfac` (device identifier), `8bb41fe` (window).

## Later decisions

- Menu bar, 16:9 window shape, fullscreen and remembered settings now live in `main.cpp`; judge `main.cpp` here for structure, start-up and shutdown order, App member order and lifetimes, COM and DPI set-up, the message loop and `WindowProc` dispatch order.
- The Presenter presents for a new frame, a resize, a device rebuild and a text that appears or ends; `ShowTextWithNextFrame` never presents. `showAtQpc` is not waited for yet (documented limit in `presenter.h`).

## Scope

Owned: `src/core/frame.*`, `src/core/source.h`, `src/core/processor.*`, `src/core/latest_slot.h`, `src/core/colour.*`, `src/core/picture_fit.*`, `src/core/capture_choice.*`, `src/platform/mf_source.*`, `src/platform/presenter.*`, `src/platform/log.*`, `src/app/main.cpp`, `CMakeLists.txt`, `build.bat`, `test.bat`, `tests/test_main.cpp`, the tests `frame_test`, `latest_slot_test`, `colour_test`, `picture_fit_test`, `processor_test`, `capture_choice_test`, and `docs/DEVELOPING.md` (each feature's own lines are also checked by its review).

## Axes and emphasis

1. **Correctness (main):** Presenter thread and its wake-up; the latest-frame slot; frame pool lifetimes across threads; COM in Media Foundation callbacks; async Source Reader shutdown and flush; device-loss rebuild; swap chain resize; Direct2D on a Direct3D device used from more than one thread; shutdown order and leaks.
2. **Latency:** capture callback to present: copies, upload, waitable object, frame latency, tearing and sync interval; no queueing; the Source's thread never waits on the GPU.
3. **Spec:** each criterion; colour range and matrix from the media type for any mode, logged.
4. **Standards:** deep modules with small interfaces; `src/core/` pure and tested; capture behind the `Source` interface; DEVELOPING.md accurate.

## Known and not yet verified by hand

- Independent flip unconfirmed: PresentMon showed "Composed: Flip" in fullscreen (an always-on-top overlay from another app is suspected); windowed with a menu bar also unverified.
- The device-loss rebuild was never hand tested.
- The default mode requires 1080p60; a device without it shows Error.
- State and feedback texts cause extra presents (the mechanism is here; whether acceptable is for tickets 06 and 07).
- The user checklist of ticket 02 (colours, responsiveness) is pending.

## Acceptance criteria

- [ ] Each acceptance criterion of ticket 02 has a verdict.
- [ ] The frame's path from capture callback to present is traced with its threads and every copy.
- [ ] Start-up and shutdown order in `main.cpp` and the Presenter is traced.
- [ ] The findings file is written per the template.

## Where to write the findings

Copy `.scratch/gameplayer-v1-review/FINDINGS-TEMPLATE.md` to `.scratch/gameplayer-v1-review/findings/ticket-02-<reviewer>.md` (`<reviewer>`: your short lowercase name). If that file exists, pick another name and do not open it.
