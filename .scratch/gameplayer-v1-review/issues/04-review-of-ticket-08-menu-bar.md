# 04: Review of ticket 08: Menu bar

**What to do:** Review the native menu bar: its structure and labels, how it reflects live state when it opens, what each command does, the filtering of offered resolutions, V-sync and always on top, and the Help texts.

**Blocked by:** 03 (Review of ticket 09).

**Status:** ready-for-agent

## Context

GamePlayer is a personal Windows 11 C++ / Direct3D 11 app (the "Viewer") that shows a game Console's picture and sound from a USB Capture Card with low Latency. Ticket 08 added a VLC-style Win32 menu bar (Device, Audio, View, Help) for overrides; zero configuration stays the default. It is hidden in fullscreen.

## Rules for this review

1. **Self-contained, tool-neutral.** This ticket is all you are given. Open the repository at `S:\GamePlayer` yourself, on branch `gameplayer-v1` (stay on it; there is no remote). Paths are relative to the repository root.
2. **Blind.** Another reviewer does this ticket independently. Do not open or search any file in `.scratch/gameplayer-v1-review/findings/` except the one you create.
3. **Read-only.** Change no code, test, build file, spec, ticket or document; make no git changes. The only file you write is your findings file. Build output in `build\` is fine.
4. **What you may run.** `test.bat` (preferred), `build.bat`, read-only git, file reading and searching. Never run `GamePlayer.exe` in any form: the Capture Card is exclusive and the user may be using it. Never stop or kill a process. Never change Windows settings. If a build fails because a file is locked or another build runs, wait, try once more, then record "not built".
5. **The tip, not history.** Review the files as they are now; commits are a map (verify with `git log`). Where a later decision changed the original ticket, judge against the later decision.
6. **Scope.** Owned files as a whole; shared files only for the listed parts. Anything else: one line in "Noticed outside this ticket's scope".
7. **Known items** below are not discoveries; comment on them in their template section.
8. **Ignore** `.claude\`, `build\`, `third_party\`, `prototype\` code (`prototype/FINDINGS.md` has the card's real mode list).
9. **Severity.** Blocker: wrong behaviour, crash, data race, or an acceptance criterion unmet. Should fix: a real weakness worth fixing. Nit: style, naming. Every finding gives severity, axis, path:line, what is wrong, why it matters, suggested fix in words, confidence. Use the glossary's terms.

## Read first

- `GLOSSARY.md`, `docs/adr/0001-*.md`, `docs/adr/0002-*.md`, `docs/DEVELOPING.md`, `prototype/FINDINGS.md` (mode list).
- The original ticket: `.scratch/gameplayer-v1/issues/08-menu-bar.md`.

## Commits (map only)

`51414aa`, merge `81a368f`. Later: `855cfac` (remembered), `f81b83f` (View > FPS counter), `47d5efc` (About shows the presented rate).

## Later decisions

- View has an FPS counter item (reviewed with ticket 04).
- Help > About also shows the presented rate, and a `Shown:` rate while a Frame rate override is set.
- All menu choices are remembered (ticket 09).

## Scope

Owned: `src/core/menu_model.*`, `src/platform/menu_bar.*`, `src/core/viewer_settings.h`, `tests/menu_model_test.cpp`; in `src/app/main.cpp` `HandleMenuCommand`, `MenuOpening`, `ShowAbout` and the `WM_INITMENUPOPUP`, `WM_COMMAND`, `WM_ENTERMENULOOP` branches.

Shared, only these parts:

- `src/core/capture_choice.*`: the offered resolutions and their tests (owner: ticket 02 review).
- `src/platform/mf_source.*`, `src/core/source.h`: the mode list and switching modes (ticket 02 review).
- `src/platform/feed_keeper.*`: `SetChoice`, the resolution fallback, `Modes` (ticket 06 review); `src/core/video_area_state.*`: what ticket 08 added (ticket 06 review).
- `src/platform/fullscreen.*`: menu bar hidden and restored, always on top dropped in fullscreen (ticket 05 review).
- `src/platform/presenter.*`: `SetVsync` (ticket 02 review). `src/core/audio_controls.*`: the delay reset (ticket 07 review).

## Axes and emphasis

1. **Spec (main):** the menu structure; only uncompressed modes at 60 fps or below, never MJPEG, never NV12 1080p120; no 120 or 144 entry; the eight hotkeys; live check marks and values; everything applies at once.
2. **Standards:** labels and check states in pure logic with tests.
3. **Correctness:** command IDs for dynamic lists (stale if the device list changes between opening and clicking); modal menu loop and message boxes versus the Feed; V-sync and override changes across threads.
4. **Latency:** V-sync off by default (tearing allowed, sync interval 0); nothing stalls presenting while a menu or message box is open.

## Known and not yet verified by hand

- Whether a windowed swap chain with a menu bar gets independent flip (and in fullscreen PresentMon showed "Composed: Flip"; an always-on-top overlay from another app is suspected).
- Resolutions other than 1080p are untested on the card.
- The default mode requires 1080p60; a device without it shows Error.
- Always on top versus Lossless Scaling.
- The Feed keeping on while a menu is open is logged but its hand check is pending; the user checklist of ticket 08 is pending.

## Acceptance criteria

- [ ] Each acceptance criterion of ticket 08 has a verdict.
- [ ] The mode filter is checked against the card's real mode list in `prototype/FINDINGS.md`.
- [ ] Dynamic menu IDs and the modal loops are judged.
- [ ] The findings file is written per the template.

## Where to write the findings

Copy `.scratch/gameplayer-v1-review/FINDINGS-TEMPLATE.md` to `.scratch/gameplayer-v1-review/findings/ticket-08-<reviewer>.md` (`<reviewer>`: your short lowercase name). If that file exists, pick another name and do not open it.
