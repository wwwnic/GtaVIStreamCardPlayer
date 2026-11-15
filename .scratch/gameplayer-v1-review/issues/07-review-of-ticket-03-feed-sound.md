# 07: Review of ticket 03: Feed sound

**What to do:** Review the Feed sound path: choosing the Capture Card's audio endpoint, WASAPI capture and render, the ring buffer, drift correction, rate and format conversion, following the Windows default output, and shutdown.

**Blocked by:** 06 (Review of ticket 05).

**Status:** ready-for-agent

## Context

GamePlayer is a personal Windows 11 C++ / Direct3D 11 app (the "Viewer") that shows a game Console's picture and sound from a USB Capture Card with low Latency. Ticket 03 plays the card's sound on the Windows default output through a small ring buffer whose fill is held at a target by tiny playback-rate changes. Sound and picture are not timestamp-synchronised.

## Rules for this review

1. **Self-contained, tool-neutral.** This ticket is all you are given. Open the repository at `S:\GamePlayer` yourself, on branch `gameplayer-v1` (stay on it; there is no remote). Paths are relative to the repository root.
2. **Blind.** Another reviewer does this ticket independently. Do not open or search any file in `.scratch/gameplayer-v1-review/findings/` except the one you create.
3. **Read-only.** Change no code, test, build file, spec, ticket or document; make no git changes. The only file you write is your findings file. Build output in `build\` is fine.
4. **What you may run.** `test.bat` (preferred), `build.bat`, read-only git, file reading and searching. Never run `GamePlayer.exe` in any form: the Capture Card is exclusive and the user may be using it. Never stop or kill a process. Never change Windows settings (sound devices included). If a build fails because a file is locked or another build runs, wait, try once more, then record "not built".
5. **The tip, not history.** Review the files as they are now; commits are a map (verify with `git log`). Where a later decision changed the original ticket, judge against the later decision.
6. **Scope.** Owned files as a whole; shared files only for the listed parts. Anything else: one line in "Noticed outside this ticket's scope".
7. **Known items** below are not discoveries; comment on them in their template section.
8. **Ignore** `.claude\`, `build\`, `third_party\`, `prototype\` code (`prototype/FINDINGS.md` has the audio numbers that worked).
9. **Severity.** Blocker: wrong behaviour, crash, data race, or an acceptance criterion unmet. Should fix: a real weakness worth fixing. Nit: style, naming. Every finding gives severity, axis, path:line, what is wrong, why it matters, suggested fix in words, confidence. Use the glossary's terms.

## Read first

- `GLOSSARY.md`, `docs/adr/0001-*.md`, `docs/DEVELOPING.md` (`sound_playout.h` and `sound.*`, the threads paragraph), `prototype/FINDINGS.md`.
- The original ticket: `.scratch/gameplayer-v1/issues/03-feed-sound.md`.

## Commits (map only)

`80ebe26` (core), `8209722` (WASAPI), merge `91a748d`. Later: `91558a0` and `a57ae0c` (gain, Audio Delay; reviewed with ticket 07), `c55d06d` (sound start and failure reporting for ticket 06).

## Later decisions

- The target fill is 15 ms (`kTargetFillSeconds`), measured just after each output period has taken its share; the Audio Delay adds to it.
- Picture and sound start and stop together through the FeedKeeper (ticket 06).

## Scope

Owned: `src/core/sound_playout.*`, `src/core/sound_ring.*`, `src/core/drift_correction.*`, `src/core/resampler.*`, `src/core/pcm.*`, `src/platform/sound.*`, and the tests `sound_playout_test` (not its gain and delay cases), `sound_ring_test`, `drift_correction_test`, `resampler_test`, `pcm_test`.

Shared, only these parts:

- `src/core/capture_choice.*`: the audio endpoint choice and its test cases (owner: ticket 02 review).
- `src/platform/feed_keeper.*`: how sound is started and restarted (ticket 06 review).

Gain, mute and Audio Delay inside the owned files are reviewed by the ticket 07 review; review the rest here, including the threads and locking of `sound.cpp` as a whole.

## Axes and emphasis

1. **Correctness (main):** capture, render and control threads; the ring hand-off; MMCSS; COM initialisation per thread; endpoint-change notifications and re-opening the render side; device invalidated; shutdown with no hang.
2. **Latency:** target fill, render buffer and period size, `IAudioClient3` low-latency periods, overrun recovery; nothing queues beyond the ring.
3. **Spec:** the "Not To Use" rule and its tests; stereo; mismatched rates and formats; follows the default output.
4. **Standards:** pure parts free of Windows calls and tested.

## Known and not yet verified by hand

- The target sound fill is 15 ms (about 25 ms before a period), above the planned 10-20 ms.
- Whether the endpoint names stay the same across driver updates or replugs.
- Hour-long drift, switching the default output, and sound with the Console off are not hand checked; the user checklist of ticket 03 is pending.

## Acceptance criteria

- [ ] Each acceptance criterion of ticket 03 has a verdict.
- [ ] Every thread's start, work loop and stop is traced.
- [ ] The drift controller's bounds are checked against its tests.
- [ ] The findings file is written per the template.

## Where to write the findings

Copy `.scratch/gameplayer-v1-review/FINDINGS-TEMPLATE.md` to `.scratch/gameplayer-v1-review/findings/ticket-03-<reviewer>.md` (`<reviewer>`: your short lowercase name). If that file exists, pick another name and do not open it.
