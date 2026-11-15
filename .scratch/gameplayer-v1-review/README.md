# GamePlayer v1 review tickets (sprint 2)

GamePlayer is a personal Windows 11 C++ / Direct3D 11 app (the "Viewer") that shows a game Console's picture and sound from a USB Capture Card on the PC monitor with low Latency. Version 1 was built from ten tickets in `.scratch/gameplayer-v1/issues/` (overview in `.scratch/gameplayer-v1/README.md`). All of them are built and merged on branch `gameplayer-v1`.

The project has no pull requests and no issue tracker, so this sprint gives every built ticket a code review written as markdown. Ticket 01 was a throwaway prototype and is not reviewed. Tickets 02 to 10 each get one review ticket here.

## What is in this folder

- `README.md`: this file.
- `FINDINGS-TEMPLATE.md`: the template every reviewer copies and fills in.
- `issues/`: the nine review tickets. Each is self-contained and written for a reviewer with no prior context.
- `findings/`: where reviewers write. Empty at the start (it holds only `.gitkeep`). One file per reviewer per reviewed ticket: `findings/ticket-<MM>-<reviewer>.md`, where `<MM>` is the number of the reviewed (original) ticket and `<reviewer>` a short lowercase name such as `claude` or `astra`.

Review ticket numbers (01 to 09) are not the numbers of the tickets they review (02 to 10). The file names say both, for example `issues/01-review-of-ticket-10-lossless-scaling-guide.md`. Findings files use the reviewed ticket's number only.

## Rules every review follows

Each review ticket repeats these rules in full, so a reviewer needs only its own ticket.

1. **Two independent reviewers.** Every ticket is done twice, once by a Claude model and once by a non-Claude model (ChatGPT). The tickets are therefore tool-neutral: no tool, skill or phrasing specific to one assistant. The reviewer opens the repository at `S:\GamePlayer` itself; tickets give paths, never pasted code.
2. **Blind.** A reviewer must not read anything in `findings/` written by anyone else.
3. **Read-only.** A reviewer changes no code, test, build file, spec, ticket or document and makes no git changes. Its only output is its own findings file, copied from `FINDINGS-TEMPLATE.md`.
4. **What may run.** The unit tests (`test.bat`) and the build (`build.bat`), see `docs/DEVELOPING.md`. Never `GamePlayer.exe`: the Capture Card can be held by one program at a time and the user may be using it. Never kill or stop a process. Never change Windows settings.
5. **The tip, not history.** The feature is reviewed as it stands at the tip of `gameplayer-v1`. Later tickets rewrote earlier code. Each ticket lists its commits as a map only, and lists the later decisions that changed the original ticket, so the reviewer judges against what was decided last.
6. **Owned files and shared files.** Each ticket lists the files that belong to its feature (reviewed as a whole) and the parts of shared files that serve the feature (reviewed only for those parts), so the same finding is not reported nine times. The shared-file owners are in the table below.
7. **Four axes, no security axis** (single-user, offline, personal app): Spec, Standards, Correctness, Latency. Each ticket says where the emphasis lies.
8. **Known and not yet verified by hand.** Each ticket lists what is already flagged, so reviewers do not report it as new. They may comment on the code behind it.
9. **Severity.** Blocker (wrong behaviour, crash, data race, an acceptance criterion unmet), Should fix (a real weakness worth fixing), Nit (style, naming; normally skipped).

## Who owns each shared file as a whole

The owning review covers the whole file, including the parts other reviews also look at for their own feature. Other reviews look only at their parts.

| Shared file | Owned by the review of | Parts other reviews look at |
|---|---|---|
| `src/app/main.cpp` | ticket 02 (review 08) | 04: F key, `ToggleFpsCounter`, `--tolerance`, `--rate`. 05: window size and shape, single instance, keep-awake wiring. 07: `PressAudio`, `HandleAudioKey`. 08: menu handlers, About. 09: the remembered-settings section, `ApplyAllSettings`, load and exit save. 10: the title. |
| `src/platform/presenter.*` | ticket 02 (review 08) | 04: `ShowTextWithNextFrame`, presenting only Unique Frames. 05: Alt+Enter, resize on mode change. 06: `ShowText`, `ShowPicture`, presents for state texts. 07: `ShowText` with a duration. 08: `SetVsync`. 10: what causes a present. |
| `src/platform/mf_source.*`, `src/core/source.h` | ticket 02 (review 08) | 06: events, failure stages and codes, signal probe. 08: mode list and mode switch. 09: finding the device by its identifier. |
| `src/core/capture_choice.*` | ticket 02 (review 08) | 03: audio endpoint choice. 08: offered resolutions. 09: remembered card lookup. |
| `src/core/processor.h` | ticket 02 (review 08) | 04: what it added. |
| `docs/DEVELOPING.md` | ticket 02 (review 08) | Each review checks the lines about its own feature. 10: the "Lossless Scaling" section. |
| `src/platform/feed_keeper.*` | ticket 06 (review 09) | 04: Processor wiring, FPS counter, `Frames:` log, timing. 05: `TakePlayingSecond`. 08: `SetChoice`, resolution fallback, `Modes`. 09: choice by identifier. |
| `src/platform/text_overlay.*`, `src/core/video_area_text.*`, `src/core/video_area_state.*` | ticket 06 (review 09) | 04: the FPS counter place and drawing. 08: what it added to the state machine. |
| `src/platform/sound.*`, `src/core/sound_playout.*` | ticket 03 (review 07) | 06: start, stop, failure reporting. 07: volume, mute, Audio Delay. |
| `src/platform/fullscreen.*`, `src/core/window_shape.*` | ticket 05 (review 06) | 08: menu bar hiding, always on top. 09: `WindowedPlacement`, `RestoredWindowRect`. |
| `src/core/viewer_settings.h`, `src/core/menu_model.*`, `src/platform/menu_bar.*` | ticket 08 (review 04) | 04: the View > FPS counter item and the About rate lines. 09: defaults. |
| `src/core/settings_file.*` | ticket 09 (review 03) | 04: the FPS counter key. |
| `src/core/audio_controls.*` | ticket 07 (review 02) | 08: the Audio Delay reset. |
| `docs/lossless-scaling-setup.md` | ticket 10 (review 01) | None. |

## Tickets and blocking edges

The order is fixed: cheapest review first, a strict linear chain. Each reviewer walks the chain on its own; "blocked by" means that reviewer has finished the previous review (its findings file for it is written).

| # | Reviews ticket | File | Blocked by | Status |
|---|---|---|---|---|
| 01 | 10: Lossless Scaling readiness and setup guide | `issues/01-review-of-ticket-10-lossless-scaling-guide.md` | None | ready-for-agent |
| 02 | 07: Audio controls | `issues/02-review-of-ticket-07-audio-controls.md` | 01 | ready-for-agent |
| 03 | 09: Remembered settings | `issues/03-review-of-ticket-09-remembered-settings.md` | 02 | ready-for-agent |
| 04 | 08: Menu bar | `issues/04-review-of-ticket-08-menu-bar.md` | 03 | ready-for-agent |
| 05 | 04: Unique Frames and Game Rate, with the FPS counter | `issues/05-review-of-ticket-04-unique-frames.md` | 04 | ready-for-agent |
| 06 | 05: Fullscreen and window behaviour | `issues/06-review-of-ticket-05-fullscreen-and-window.md` | 05 | ready-for-agent |
| 07 | 03: Feed sound | `issues/07-review-of-ticket-03-feed-sound.md` | 06 | ready-for-agent |
| 08 | 02: App skeleton and the Source / Processor / Presenter pipeline | `issues/08-review-of-ticket-02-app-skeleton-pipeline.md` | 07 | ready-for-agent |
| 09 | 06: Video Area states and automatic recovery | `issues/09-review-of-ticket-06-video-area-states.md` | 08 | ready-for-agent |

## Order of work

1. Review 01 (ticket 10).
2. Review 02 (ticket 07).
3. Review 03 (ticket 09).
4. Review 04 (ticket 08).
5. Review 05 (ticket 04).
6. Review 06 (ticket 05).
7. Review 07 (ticket 03).
8. Review 08 (ticket 02).
9. Review 09 (ticket 06).

One review at a time per reviewer. The two reviewers may run at the same time as each other; they share the working copy and the `build\` folder, so a build may find the other's build running (the tickets say what to do then).

## What happens after

Fixes are not part of this sprint. Later, in a separate session, the user compares both reviewers' findings files for each ticket and writes `findings/ticket-<MM>-fixes.md`, listing the accepted findings. Fixing them is a later job with its own tickets.

## Claude-side dispatch

For the Claude reviewer only: Fable 5.2 at medium effort for the reviews of tickets 02 and 06 (review tickets 08 and 09); Opus 5.5 at medium effort for the other seven. One review at a time, down the chain. Suggested reviewer name: `claude`.
