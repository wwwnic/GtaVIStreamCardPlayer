# 01: Review of ticket 10: Lossless Scaling readiness and setup guide

**What to do:** Check that the user's setup guide `docs/lossless-scaling-setup.md` is accurate against the code at the tip and against the research it is based on, and that the "good Lossless Scaling target" claims ticket 10 verified in code still hold. This is mostly a documentation review; the code checks are claim checks, not full module reviews.

**Blocked by:** None (first in the chain).

**Status:** ready-for-agent

## Context

GamePlayer is a personal Windows 11 C++ / Direct3D 11 app (the "Viewer") that shows a game Console's picture and sound from a USB Capture Card with low Latency. The Viewer does no Frame Generation itself; the separate app Lossless Scaling runs on top of its window (ADR 0002). Ticket 10 confirmed the window is a dependable Lossless Scaling target and wrote a one-time setup guide for the user, who reads vertically (very short lines, one idea per line).

## Rules for this review

1. **Self-contained, tool-neutral.** This ticket is all you are given. Open the repository at `S:\GamePlayer` yourself, on branch `gameplayer-v1` (stay on it; there is no remote). Paths are relative to the repository root.
2. **Blind.** Another reviewer does this ticket independently. Do not open or search any file in `.scratch/gameplayer-v1-review/findings/` except the one you create.
3. **Read-only.** Change no code, test, build file, spec, ticket or document; make no git changes (no commit, stage, stash, checkout, reset). The only file you write is your findings file. Build output in the git-ignored `build\` folder is fine.
4. **What you may run.** `test.bat` (builds and runs only the unit tests; preferred), `build.bat`, read-only git commands, and file reading and searching. Never run `GamePlayer.exe` in any form: the Capture Card can be held by one program at a time and the user may be using it. Never stop or kill a process. Never change Windows settings. If a build fails because a file is locked or another build is running, do not force it: wait, try once more, then record "not built".
5. **The tip, not history.** Review the files as they are now. The commits below are a map; verify them with `git log`. Where a later decision changed the original ticket, judge against the later decision.
6. **Scope.** Owned files as a whole; shared files only for the parts listed. Anything else goes into "Noticed outside this ticket's scope", one line each.
7. **Known items.** Do not report the items under "Known and not yet verified by hand" as discoveries. Comment on them in their template section.
8. **Ignore** `.claude\`, `build\`, `third_party\` and the `prototype\` code (`prototype/FINDINGS.md` is background reading).
9. **Severity.** Blocker: wrong behaviour, crash, data race, or an acceptance criterion unmet. Should fix: a real weakness worth fixing. Nit: style, naming; normally skipped. Every finding gives severity, axis, path:line, what is wrong, why it matters, suggested fix in words, and your confidence. Use the glossary's terms.

## Read first

- `GLOSSARY.md`, `docs/adr/0002-frame-generation-stays-outside-the-viewer.md`.
- The original ticket: `.scratch/gameplayer-v1/issues/10-lossless-scaling-readiness-and-setup-guide.md`.
- `docs/research/capture-viewer-research.md`: the section "Lossless Scaling automation" in full (the guide's factual basis), and "Low-latency presentation".
- `prototype/FINDINGS.md`.
- `docs/DEVELOPING.md`: the "Run" and "Lossless Scaling" sections.

## Commits (map only)

`355c2cc` wrote the guide (docs only). Later edits to it: `855cfac` (remembered V-sync, menu), `7967717` (FPS counter text).

## Later decisions

- The FPS counter shows one number, the presented rate (for example "30 FPS"), blank for about two seconds after a start; it is remembered between runs.
- 60 to 120 (fixed X2 on a 60 fps game, 144 Hz monitor) is the main expected use.
- The user's 60 fps run with Lossless Scaling is recorded as confirmed ("What already works").

## Scope

Owned: `docs/lossless-scaling-setup.md`; the "Lossless Scaling" section of `docs/DEVELOPING.md`.

Claim checks in shared code (verify the claim; deeper review belongs to the owner named):

- Title exactly "GamePlayer", never changed: `src/app/main.cpp`, search `src/` for `SetWindowText`. Exe name: `CMakeLists.txt`. (Owner: ticket 02 review.)
- No exclusive fullscreen: no `SetFullscreenState`; `MakeWindowAssociation` with `DXGI_MWA_NO_ALT_ENTER` in `src/platform/presenter.cpp`. (Ticket 02 review.)
- Presents only for a new Unique Frame, a resize, a device rebuild, or a text that appears or ends; the FPS counter never presents: `src/platform/presenter.cpp`, `src/platform/feed_keeper.cpp`. (Ticket 02 and 04 reviews.)
- Cursor hides after 2 s in fullscreen; `HWND_TOP`, topmost dropped in fullscreen: `src/platform/fullscreen.cpp`. (Ticket 05 review.)
- Every statement the guide makes about GamePlayer: keys, menu names, defaults, FPS counter text, log file location, log line texts (`Display: off`, `Stay awake: still held`), the power request text (`src/platform/keep_awake.cpp`).

## Axes and emphasis

1. **Spec (main):** ticket 10's criteria; the nine topics in order; 60 to 120 covered; every unverified item tagged "(not confirmed)"; no invented setting names, defaults, hotkeys, paths or versions (trace each to the research or prototype findings, or report it).
2. **Standards:** the vertical style rule (`docs/DEVELOPING.md`); glossary terms. Style breaks are Nit unless they make a step unclear.
3. **Correctness:** is each claim about the app true at the tip?
4. **Latency:** does any advice add delay against the app's design (for example V-sync), and do the presents-only-Unique-Frames claims hold?

## Known and not yet verified by hand

- Every step tagged "(not confirmed)" in the guide.
- Lossless Scaling on a 30 fps game was never tried; bit-identical Repeated Frames at 30 fps were never observed.
- Whether auto scale re-triggers after F11 without a focus change; whether WGC capture counts the Viewer's presents.
- Independent flip unconfirmed in fullscreen: PresentMon showed "Composed: Flip"; an always-on-top overlay from another app is suspected.
- The display turned off once during a long fullscreen session with Lossless Scaling in front; cause unproven; keep-awake was hardened since.
- State and feedback texts cause extra presents (the guide calls it a blip).
- The settings used in the confirmed 60 fps run were not recorded.

## Acceptance criteria

- [ ] Each statement in the guide about GamePlayer is checked against the code at the tip; wrong or stale ones are findings.
- [ ] Each Lossless Scaling, Windows, Xbox and AMD name, default, hotkey, path and version is traced to the research or prototype findings, or reported.
- [ ] The nine topics are present in order and 60 to 120 is covered as the ticket requires.
- [ ] Every unverified item ticket 10 lists carries a "(not confirmed)" tag; missing tags are reported.
- [ ] Each readiness claim has a verdict with path:line.
- [ ] Each acceptance criterion of ticket 10 has a verdict.
- [ ] The findings file is written per the template.

## Where to write the findings

Copy `.scratch/gameplayer-v1-review/FINDINGS-TEMPLATE.md` to `.scratch/gameplayer-v1-review/findings/ticket-10-<reviewer>.md`, where `<reviewer>` is the short lowercase name you were given or pick (for example `claude`, `astra`). If that file already exists, pick another name and do not open it.
