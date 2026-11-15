# 03: Review of ticket 09: Remembered settings

**What to do:** Review how GamePlayer remembers its window placement, audio values and menu choices between runs: the settings text format and its parser, when it saves, how it writes the file, and how the window and Capture Card are restored at start-up.

**Blocked by:** 02 (Review of ticket 07).

**Status:** ready-for-agent

## Context

GamePlayer is a personal Windows 11 C++ / Direct3D 11 app (the "Viewer") that shows a game Console's picture and sound from a USB Capture Card with low Latency. Ticket 09 made it reopen where it was closed, with the same settings, from one small key=value file under `%APPDATA%\GamePlayer\`. Damage of any kind gives defaults, silently.

## Rules for this review

1. **Self-contained, tool-neutral.** This ticket is all you are given. Open the repository at `S:\GamePlayer` yourself, on branch `gameplayer-v1` (stay on it; there is no remote). Paths are relative to the repository root.
2. **Blind.** Another reviewer does this ticket independently. Do not open or search any file in `.scratch/gameplayer-v1-review/findings/` except the one you create.
3. **Read-only.** Change no code, test, build file, spec, ticket or document; make no git changes. The only file you write is your findings file. Build output in `build\` is fine. Do not create, edit or delete any settings file under `%APPDATA%`.
4. **What you may run.** `test.bat` (preferred), `build.bat`, read-only git, file reading and searching. Never run `GamePlayer.exe` in any form: the Capture Card is exclusive and the user may be using it. Never stop or kill a process. Never change Windows settings. If a build fails because a file is locked or another build runs, wait, try once more, then record "not built".
5. **The tip, not history.** Review the files as they are now; commits are a map (verify with `git log`). Where a later decision changed the original ticket, judge against the later decision.
6. **Scope.** Owned files as a whole; shared files only for the listed parts. Anything else: one line in "Noticed outside this ticket's scope".
7. **Known items** below are not discoveries; comment on them in their template section.
8. **Ignore** `.claude\`, `build\`, `third_party\`, `prototype\` code.
9. **Severity.** Blocker: wrong behaviour, crash, data race, or an acceptance criterion unmet. Should fix: a real weakness worth fixing. Nit: style, naming. Every finding gives severity, axis, path:line, what is wrong, why it matters, suggested fix in words, confidence. Use the glossary's terms.

## Read first

- `GLOSSARY.md`, `docs/adr/0001-*.md`, `docs/adr/0002-*.md`, `docs/DEVELOPING.md` (the "Remembered settings" paragraph).
- The original ticket: `.scratch/gameplayer-v1/issues/09-remembered-settings.md`.

## Commits (map only)

`855cfac`. Later: `f81b83f` added the FPS counter to the file.

## Later decisions

- The FPS counter state is remembered (off on a fresh install).
- A resolution that does not start falls back for this run only; the choice stays remembered.
- `--settings PATH` uses another file; `--rate` applies for one run and is not saved.

## Scope

Owned: `src/core/settings_file.*`, `src/platform/settings_store.*`, `tests/settings_file_test.cpp`; in `src/app/main.cpp` the "Remembered settings (ticket 09)" section (`ScreenFromWorkspace` to `RestoreWindow`), `ApplyAllSettings`, the loading in `wWinMain`, `WM_ENDSESSION`, the exit save, `--settings`.

Shared, only these parts:

- `src/core/window_shape.*`: `RestoredWindowRect` and its tests (owner: ticket 05 review).
- `src/platform/fullscreen.h`: `WindowedPlacement` (ticket 05 review).
- `src/core/capture_choice.*`, `src/platform/mf_source.cpp`, `src/core/source.h`: finding the remembered card by identifier, then name (ticket 02 review).
- `src/platform/feed_keeper.*`: the choice by identifier (ticket 06 review).
- `src/core/viewer_settings.h`: defaults match the earlier tickets (ticket 08 review).

## Axes and emphasis

1. **Spec (main):** every remembered value; defaults; every kind of damage; temporary file then rename; save on exit and debounced; folder from the system API; format version; a missing card does not overwrite the saved preference.
2. **Standards:** parsing and validation are pure and well tested (this ticket carries the testing policy hardest).
3. **Correctness:** write failure paths; rename semantics; save order at exit and on session end; fullscreen, minimised and maximised placement; workspace versus screen coordinates; DPI.
4. **Latency:** file writes on the window thread (how often, how long); nothing on the frame or sound path.

## Known and not yet verified by hand

- Whether the card's identifier is stable across replugs and USB ports.
- Per-monitor DPI differences on restore.
- The user checklist of ticket 09 is pending (including two monitors).

## Acceptance criteria

- [ ] Each acceptance criterion of ticket 09 has a verdict.
- [ ] Each damage case of the ticket is checked against the tests and the code.
- [ ] The file write path and its failure handling are judged.
- [ ] The findings file is written per the template.

## Where to write the findings

Copy `.scratch/gameplayer-v1-review/FINDINGS-TEMPLATE.md` to `.scratch/gameplayer-v1-review/findings/ticket-09-<reviewer>.md` (`<reviewer>`: your short lowercase name). If that file exists, pick another name and do not open it.
