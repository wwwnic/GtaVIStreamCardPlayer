# 05: Review of ticket 04: Unique Frames and Game Rate, with the FPS counter

**What to do:** Review the Processor that drops Repeated Frames and presents only Unique Frames, the Auto / 30 / 60 override, the Signal Rate and Game Rate estimates, the retained recent Unique Frames, and the on-screen FPS counter including its later rework.

**Blocked by:** 04 (Review of ticket 08).

**Status:** ready-for-agent

## Context

GamePlayer is a personal Windows 11 C++ / Direct3D 11 app (the "Viewer") that shows a game Console's picture and sound from a USB Capture Card with low Latency. The Console always sends 60 frames per second (the Signal Rate); a 30 fps game arrives as each picture twice. Lossless Scaling counts the Viewer's presents, so the Viewer must present only Unique Frames (ADR 0002). Ticket 04 built that in the Processor stage.

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

- `GLOSSARY.md` (Signal Rate, Game Rate, Unique Frame, Repeated Frame), `docs/adr/0002-*.md`, `docs/DEVELOPING.md` (the FPS counter and `Frames:` log text in "Run"), `prototype/FINDINGS.md`.
- The original ticket: `.scratch/gameplayer-v1/issues/04-unique-frames-and-game-rate.md`.

## Commits (map only)

`7fb9600`, `c8c2672`, merge `d8a0453`, `9af8099`, `d2c03ad`, merge `f576a18`. Later (FPS counter): `f81b83f`, `2380529`, `47d5efc`, `7967717`.

## Later decisions (these replace the original ticket's text)

- The FPS counter shows one number, the presented rate ("30 FPS"): the Game Rate on Auto, the override's rate otherwise. Not "Game Rate / Signal Rate".
- It is blank for about two seconds after a start or Feed restart, until the rate window is full.
- It is remembered between runs (off on a fresh install) and has a View menu item, not "off at every launch".
- The near-duplicate tolerance defaults to 0 (exact only), not 0.5%; `--tolerance X` sets it for one run; the log shows the closest Unique Frames.
- The Signal Rate is shown in Help > About and the log.

## Scope

Owned: `src/core/unique_frames.*`, `tests/unique_frames_test.cpp`.

Shared, only these parts:

- `src/platform/feed_keeper.*`: Processor wiring, `ProcessTiming`, `LogFrames`, `UpdateFpsCounter`, `SetFpsCounter`, `SetGameRateOverride`, `Rates` (owner: ticket 06 review).
- `src/platform/presenter.*`: `ShowTextWithNextFrame`, and that a dropped Repeated Frame causes no present (ticket 02 review). `src/core/processor.h`: what ticket 04 added (ticket 02 review).
- `src/platform/text_overlay.*`, `src/core/video_area_text.*`: the top-left place (ticket 06 review).
- `src/core/menu_model.*`, `src/platform/menu_bar.*`: the FPS counter item and About rate lines (ticket 08 review). `src/core/settings_file.*`: the FPS counter key (ticket 09 review).
- `src/app/main.cpp`: `ToggleFpsCounter`, the F key, `--tolerance`, `--rate` (ticket 02 review).

## Axes and emphasis

1. **Latency (main):** comparison cost on the Source's thread; a Unique Frame passed on the moment it is recognised; the work after handing a frame on (closeness) cannot delay the next frame; memory traffic of the compare.
2. **Correctness:** lifetimes of retained frames against the frame pool; override and FPS counter changes from the window thread while the Source's thread processes; rates at zero (no division by zero); counter wrap.
3. **Spec:** each criterion and the static-Feed rules; the listed test cases exist.
4. **Standards:** pure logic tested with synthetic sequences; one named tolerance constant.

## Known and not yet verified by hand

- The tolerance defaults to 0 because bit-identical Repeated Frames at 30 fps were never observed.
- A 30 fps game was never run, so the "about 30 presented" criteria are unconfirmed.
- The FPS counter's no-present claim was not measured with Lossless Scaling.
- How Lossless Scaling treats long gaps in presents (static scene) and its WGC capture mode.
- The user checklist of ticket 04 is pending.

## Acceptance criteria

- [ ] Each acceptance criterion of ticket 04 has a verdict, judged against the later decisions where they apply.
- [ ] The capture-path cost and the no-added-delay rule are judged from the code.
- [ ] The test list in the ticket is checked case by case.
- [ ] The findings file is written per the template.

## Where to write the findings

Copy `.scratch/gameplayer-v1-review/FINDINGS-TEMPLATE.md` to `.scratch/gameplayer-v1-review/findings/ticket-04-<reviewer>.md` (`<reviewer>`: your short lowercase name). If that file exists, pick another name and do not open it.
