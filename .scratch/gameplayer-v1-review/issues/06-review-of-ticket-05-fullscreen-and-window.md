# 06: Review of ticket 05: Fullscreen and window behaviour

**What to do:** Review borderless fullscreen (F11, Esc), the target monitor, restoring the window, the hiding cursor, the 16:9 window shape, keeping the PC and display awake while a Feed plays, and the single instance.

**Blocked by:** 05 (Review of ticket 04).

**Status:** ready-for-agent

## Context

GamePlayer is a personal Windows 11 C++ / Direct3D 11 app (the "Viewer") that shows a game Console's picture and sound from a USB Capture Card with low Latency. Fullscreen must be a borderless window, never exclusive, so Lossless Scaling can work on it (ADR 0002). The user plays with a controller only, which Windows does not count as activity.

## Rules for this review

1. **Self-contained, tool-neutral.** This ticket is all you are given. Open the repository at `S:\GamePlayer` yourself, on branch `gameplayer-v1` (stay on it; there is no remote). Paths are relative to the repository root.
2. **Blind.** Another reviewer does this ticket independently. Do not open or search any file in `.scratch/gameplayer-v1-review/findings/` except the one you create.
3. **Read-only.** Change no code, test, build file, spec, ticket or document; make no git changes. The only file you write is your findings file. Build output in `build\` is fine.
4. **What you may run.** `test.bat` (preferred), `build.bat`, read-only git, file reading and searching. Never run `GamePlayer.exe` in any form: the Capture Card is exclusive and the user may be using it. Never stop or kill a process. Never change Windows settings (also: do not run `powercfg` with any option that changes something). If a build fails because a file is locked or another build runs, wait, try once more, then record "not built".
5. **The tip, not history.** Review the files as they are now; commits are a map (verify with `git log`). Where a later decision changed the original ticket, judge against the later decision.
6. **Scope.** Owned files as a whole; shared files only for the listed parts. Anything else: one line in "Noticed outside this ticket's scope".
7. **Known items** below are not discoveries; comment on them in their template section.
8. **Ignore** `.claude\`, `build\`, `third_party\`, `prototype\` code.
9. **Severity.** Blocker: wrong behaviour, crash, data race, or an acceptance criterion unmet. Should fix: a real weakness worth fixing. Nit: style, naming. Every finding gives severity, axis, path:line, what is wrong, why it matters, suggested fix in words, confidence. Use the glossary's terms.

## Read first

- `GLOSSARY.md`, `docs/adr/0001-*.md`, `docs/adr/0002-*.md`, `docs/DEVELOPING.md` (`fullscreen.*`, `keep_awake.*`, `main.cpp` in the module layout).
- The original ticket: `.scratch/gameplayer-v1/issues/05-fullscreen-and-window-behaviour.md`.

## Commits (map only)

`4457ab6`, `0e89cfa`. Later: `8bb41fe` (16:9 window; its feedback part belongs to the ticket 07 review), `d904f46` (keep-awake hardening), `dfbdf43` (windowed cursor hiding).

## Later decisions (these replace the original ticket's text)

- The cursor also hides when windowed: after 2 s still, only over the Video Area of the active window, never during a menu, move or resize. Not "windowed: always visible".
- The windowed Video Area stays exactly 16:9 while dragged (already in the ticket file).
- Keep-awake holds a power request plus the window thread's execution state, resets the idle timers each second, and logs display on, off and dimmed. It is held only in the Playing state (ticket 06).

## Scope

Owned: `src/core/monitor_choice.*`, `src/core/cursor_idle.*`, `src/core/stay_awake.*`, `src/core/window_shape.*`, `src/platform/fullscreen.*`, `src/platform/keep_awake.*`, `src/platform/single_instance.*`, their tests (`monitor_choice_test`, `cursor_idle_test`, `stay_awake_test`, `window_shape_test`); in `src/app/main.cpp` `FrameSize`, `MinVideoAreaWidth`, `KeepShapeWhileSizing`, `SetDefaultSize`, the sizing in `CreateViewerWindow`, `WM_SIZING`, `WM_GETMINMAXINFO`, `WM_SYSCOMMAND`, the `WM_POWERBROADCAST` and keep-awake wiring, the single-instance claim and the another-launch message.

Shared, only these parts:

- `src/platform/presenter.*`: Alt+Enter disabled, swap chain resize on the mode change (owner: ticket 02 review).
- `src/platform/feed_keeper.*`: `TakePlayingSecond` (ticket 06 review).

`RestoredWindowRect` and `WindowedPlacement` are reviewed for their remembered-settings use by the ticket 09 review; their code is still owned here.

## Axes and emphasis

1. **Correctness (main):** Win32 message handling; `WM_DISPLAYCHANGE`; DPI; restoring style, placement and chrome in `ShowChrome`; the single-instance race; power request and execution-state lifetime and thread; refusing screensaver commands; shutdown release.
2. **Spec:** each criterion, with the later decisions.
3. **Latency:** fullscreen prerequisites for independent flip (covers the monitor exactly, `HWND_TOP`, not topmost); cursor hiding cheap; resize never blocks the Presenter; the per-second keep-awake work cheap.
4. **Standards:** pure logic tested; window chrome in one place.

## Known and not yet verified by hand

- Independent flip unconfirmed in fullscreen: PresentMon showed "Composed: Flip"; an always-on-top overlay from another app is suspected.
- The display turned off once during a long fullscreen session (about 19 minutes, controller only) with Lossless Scaling in front; cause unproven; keep-awake was hardened in `d904f46`.
- Sleep and resume were never hand tested.
- Two monitors and `WM_DISPLAYCHANGE` handling are not known to be hand tested; the user checklist of ticket 05 is pending.

## Acceptance criteria

- [ ] Each acceptance criterion of ticket 05 has a verdict, judged against the later decisions.
- [ ] The keep-awake hold and release paths are traced for every state change and exit.
- [ ] The findings file is written per the template.

## Where to write the findings

Copy `.scratch/gameplayer-v1-review/FINDINGS-TEMPLATE.md` to `.scratch/gameplayer-v1-review/findings/ticket-05-<reviewer>.md` (`<reviewer>`: your short lowercase name). If that file exists, pick another name and do not open it.
