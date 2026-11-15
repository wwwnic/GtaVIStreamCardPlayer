# 02: Review of ticket 07: Audio controls (volume, mute, Audio Delay)

**What to do:** Review the audio controls as they stand at the tip: the keys Up, Down, M, J, K, the logic that steps and clamps volume and Audio Delay, the gain and Audio Delay inside the sound playout, and the short feedback line in the Video Area.

**Blocked by:** 01 (Review of ticket 10).

**Status:** ready-for-agent

## Context

GamePlayer is a personal Windows 11 C++ / Direct3D 11 app (the "Viewer") that shows a game Console's picture and sound from a USB Capture Card with low Latency. Feed sound is captured with WASAPI, buffered in a small drift-corrected ring and played on the Windows default output (ticket 03). Ticket 07 added the app's own volume, mute and an Audio Delay (extra buffered sound, never a picture delay), with a brief on-screen confirmation.

## Rules for this review

1. **Self-contained, tool-neutral.** This ticket is all you are given. Open the repository at `S:\GamePlayer` yourself, on branch `gameplayer-v1` (stay on it; there is no remote). Paths are relative to the repository root.
2. **Blind.** Another reviewer does this ticket independently. Do not open or search any file in `.scratch/gameplayer-v1-review/findings/` except the one you create.
3. **Read-only.** Change no code, test, build file, spec, ticket or document; make no git changes. The only file you write is your findings file. Build output in the git-ignored `build\` folder is fine.
4. **What you may run.** `test.bat` (preferred), `build.bat`, read-only git, file reading and searching. Never run `GamePlayer.exe` in any form: the Capture Card is exclusive and the user may be using it. Never stop or kill a process. Never change Windows settings. If a build fails because a file is locked or another build runs, wait, try once more, then record "not built".
5. **The tip, not history.** Review the files as they are now; commits are a map (verify with `git log`). Where a later decision changed the original ticket, judge against the later decision.
6. **Scope.** Owned files as a whole; shared files only for the listed parts. Anything else: one line in "Noticed outside this ticket's scope".
7. **Known items** below are not discoveries; comment on them in their template section.
8. **Ignore** `.claude\`, `build\`, `third_party\`, `prototype\` code.
9. **Severity.** Blocker: wrong behaviour, crash, data race, or an acceptance criterion unmet. Should fix: a real weakness worth fixing. Nit: style, naming. Every finding gives severity, axis, path:line, what is wrong, why it matters, suggested fix in words, confidence. Use the glossary's terms.

## Read first

- `GLOSSARY.md` (Audio Delay, Video Area), `docs/adr/0001-*.md`, `docs/adr/0002-*.md`, `docs/DEVELOPING.md`.
- The original ticket: `.scratch/gameplayer-v1/issues/07-audio-controls.md`.

## Commits (map only)

`aabe482` (logic), `91558a0` (gain and Audio Delay in the playout), `a57ae0c` (keys, feedback). Later: `8bb41fe` rewrote the feedback on `Presenter::ShowText`; `51414aa` added the Audio Delay reset for the menu.

## Later decisions

- The feedback line is drawn through `Presenter::ShowText(TextPlace::Top, ..., 1500)`; the Presenter presents when it appears and when it ends.
- The Audio menu (ticket 08) adds a reset to 0; the values are remembered (ticket 09).

## Scope

Owned: `src/core/audio_controls.*`, `src/core/feedback_line.*`, `src/platform/feedback.*`, `tests/audio_controls_test.cpp`, `tests/feedback_line_test.cpp`.

Shared, only these parts:

- `src/core/sound_playout.*`: gain, mute and Audio Delay, and how drift correction follows the shifted target; their cases in `tests/sound_playout_test.cpp` (owner: ticket 03 review).
- `src/platform/sound.*`: the volume, mute and Audio Delay get / set and how they reach the render thread (ticket 03 review).
- `src/app/main.cpp`: `PressAudio`, `HandleAudioKey`, the `WM_KEYDOWN` branch (ticket 02 review).
- `src/platform/presenter.*`: `ShowText` with a duration, as used by the feedback (ticket 02 review).

## Axes and emphasis

1. **Spec:** keys, 5-point steps, 0-500 ms in 50 ms steps, the minimum message, mute keeps the volume, a corner that is not top-left, about 1.5 s, the app's own volume only.
2. **Standards:** pure logic in `src/core/` with tests; is `feedback_line` still the thing that decides timing, or dead weight now?
3. **Correctness (main):** thread safety of values set on the window thread and read on the render thread; no click on a delay change; drift correction after the change; key repeat handling.
4. **Latency:** Audio Delay 0 adds nothing; the gain adds no buffering; the window thread is not blocked; the feedback causes no more presents than needed.

## Known and not yet verified by hand

- The feedback text causes extra presents (one when it appears, one when it ends); accepted by ticket 07.
- Whether a useful Audio Delay is ever needed with this card.
- The user checklist of ticket 07 is pending.

## Acceptance criteria

- [ ] Each acceptance criterion of ticket 07 has a verdict.
- [ ] The value hand-off between threads is examined and judged.
- [ ] The click-free delay change and drift correction after it are examined in code and tests.
- [ ] The findings file is written per the template.

## Where to write the findings

Copy `.scratch/gameplayer-v1-review/FINDINGS-TEMPLATE.md` to `.scratch/gameplayer-v1-review/findings/ticket-07-<reviewer>.md` (`<reviewer>`: your short lowercase name, for example `claude`, `astra`). If that file exists, pick another name and do not open it.
