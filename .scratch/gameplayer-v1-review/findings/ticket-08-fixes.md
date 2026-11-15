# Fixes: ticket 08, Menu bar

- Reports checked: `ticket-08-claude.md` (Claude F1-F7 and outside-scope notes), `ticket-08-astra.md` (Astra F1-F6, outside the repo), and the items passed on by tickets 02, 04 and 06.
- Code checked at e7148ad plus these fixes.
- Unit tests: `test.bat` passed, 277 cases, 5709 assertions (was 273 / 5692).
- Build: `build.bat` clean, no warnings.
- Evidence for the mode filter: the card's real list, `build\GamePlayer.log` (`Source: mode #0` to `#124`).
- Smoke runs (no other GamePlayer running; temporary settings files):
  - `--run 15`, remembered `resolution=1280x800`: opened mode #20 YUY2 1280x800 60, Loading -> Playing in 0.4 s, 60 arrived/s, sound found, exited cleanly. So at least one non-1080p mode streams on this card.
  - `--run 8`, remembered `resolution=1000x1000`: `Feed: resolution 1000x1000 is not offered by "Live Gamer ULTRA S GC553Pro"; the default mode`, played 1080p60, the file kept `resolution=1000x1000` (this run only, as designed), exited cleanly.

Files changed: `src/core/capture_choice.h`, `src/core/capture_choice.cpp`, `src/core/menu_model.h`, `src/core/menu_model.cpp`, `src/platform/menu_bar.h`, `src/platform/menu_bar.cpp`, `src/platform/mf_source.h`, `src/platform/mf_source.cpp` (`ListCaptureDevices` only), `src/platform/feed_keeper.h`, `src/platform/feed_keeper.cpp`, `src/app/main.cpp`, `tests/menu_model_test.cpp`, `tests/capture_choice_test.cpp`, `docs/DEVELOPING.md`.

Frame path: nothing added between the Source and the Presenter. The new checks run on the window's thread, once per Feed restart or menu click.

## Per finding

### Astra F1 + Claude F5: two Capture Cards of the same name
- Verdict: accepted (Nit by likelihood, not a Blocker: needs two cards of one model).
- Why: traced. The menu held names only; both rows were checked, and a click cleared the identifier, so the name's first match opened.
- Change: `ListCaptureDevices` returns name and identifier (`CaptureCardEntry`, `capture_choice.h`). `CaptureCardItems` checks by identifier (by name when one has none) and numbers duplicates in list order ("USB Video (2)"). `HandleMenuCommand` passes the clicked identifier to the Source, which picks by identifier first.
- Tests: "two Capture Cards of the same name: numbered, and only the one in use is checked", "a Capture Card with no identifier is checked by its name".

### Astra F2 (+ Claude outside note, `main.cpp:159`): choosing the automatic card already playing
- Verdict: accepted (Should fix, not a Blocker: needs a non-GC553 card on automatic, then a GC553 plugged in, then a recovery).
- Why: traced. The early return stored the choice in the settings but never told the Source, so a later restart could pick another card. The same path copied this run's resolution (maybe a fallback) over the remembered one.
- Change: new `FeedKeeper::KeepDeviceInUse` updates the Source's choice with no restart; `HandleMenuCommand` calls it and no longer touches the remembered resolution.

### Astra F3: modes just above 60 fps passed the 60.5 ceiling
- Verdict: accepted for the menu (Nit: no such rate on the real list; its highest at or below 60 is 60/1).
- Why: arithmetic. 241/4 = 60.25 passed `fps > 60.5`.
- Change: `kMaxOfferedFps = 60.01` (`capture_choice.h`), room only for a 60 written as a long fraction.
- Test: "the Resolution menu stops at 60 fps: a rate just above is never offered" (60.25 and 60.5 out, 60.0002 and 59.94 in). Failed before (the 60.25 and 60.5 modes were offered).
- Not changed: the default 1080p choice still accepts within 1 fps of 60. That is ticket 02's part of the file, and nothing on the card sits there.

### Astra F4: equal rates as different fractions slip past the twin check
- Verdict: rejected, already fixed by ticket 02 (cross-multiplied, tested: "an MJPEG twin is recognised when its rate is the same number written as another fraction").

### Astra F5: a resolution the card no longer offers falls back silently
- Verdict: accepted (Should fix, not a Blocker: needs a remembered size on another card, or a changed mode list).
- Why: traced. `ChooseCaptureMode` gave the default; only a log line said so; the run kept the stale choice.
- Change, `FeedKeeper::DropUnofferedResolution` (after each restart): if the open mode's size is not the chosen one, the choice is dropped for this run (Source too, no restart), and the fallback callback is told `offered = false`. `main.cpp` shows "WxH is not offered by this card: 1080p". Remembered value kept, as for the other fallback. Checked in the second smoke run.

### Astra F6: lists cut at 99 entries
- Verdict: rejected.
- Why: true, but the card offers 12 sizes and a PC has a handful of cameras. Not worth wider id ranges.

### Claude F1: a chosen resolution that opens but sends no frames never falls back
- Verdict: deferred to the user.
- Why: true as traced: the no-frames timeout gives No signal and reopens in the chosen mode. But "no frames" is also a Console that is off. Treating it as a failed mode would drop a remembered resolution every time the Viewer starts before the Console. The smoke run shows 1280x800 streams on this card, so this may never happen. See "Decisions for the user".

### Claude F2: "MJPEG-derived" detection relies only on an exact twin
- Verdict: rejected.
- Why: the real log answers it. The two modes in question, NV12 2560x1440 at 60 (#68) and NV12 3840x2160 at 30 (#73), are flagged `nominalRange=2 yuvMatrix=1` (limited range, BT.709), like every plain uncompressed mode. Every MJPEG-paired mode is `nominalRange=1 yuvMatrix=2` (full range, BT.601) and has an exact twin. So the twin rule catches exactly the derived ones. Fixture comment updated.

### Claude F3: Audio value lines updated by position
- Verdict: accepted (Nit, cheap).
- Change: `kMenuVolumeShown`, `kMenuDelayShown` (greyed, never sent); updated by id like the rest.

### Claude F4: loose Hotkeys test; two defaults unchecked
- Verdict: accepted (Nit, cheap).
- Change: each key must start its line and end with a tab ("\nM\t", "\nUp / Down\t", ...). The defaults test also checks `fpsCounter` and `captureCardId`.

### Claude F6: Frame rate choice logs no `Menu:` line
- Verdict: accepted (Nit).
- Change: `Menu: Frame rate auto|30|60`. `DEVELOPING.md` adds the FPS counter to the menu list, and notes duplicates and the resolution fallbacks.

### Claude F7: About says "none" for a card present but not playing
- Verdict: accepted (Nit, cheap).
- Change: About shows the card opened last with "(not playing)" when no mode is open (Card busy, Error). The Capture Card list keeps its check mark on it. Clicking it while not playing still restarts it (a retry).
- Test: "About names a card that is there but not playing (busy, failed)".

### From ticket 06 (Astra F5) and Claude (outside scope): `FallBackFrom` / `choiceProven_`
- What the code does today:
  - Not yet proven (no frame since the choice): a mode refused, or a non-busy first-read failure, drops the resolution for this run.
  - Proven: never again. Later failures are retried, and shown as Error if they repeat.
  - The remembered value always stays (`main.cpp` resets only `settings`).
- Claude's "clears the user's setting": overstated. It is dropped for this run only, and the file keeps it. A card unplugged at the exact first read does give 1080p for the rest of that run.
- Astra's "never falls back again": true, by design (the mode has worked, so later failures are the card's).
- Verdict: no plain bug; deferred as a product choice (below).

### From ticket 02: default mode does not exclude MJPEG twins
- Verdict: rejected.
- Why: the card's only 1080p twin is at 240 fps, outside the default's 59-61 window. On a card whose only 1080p60 had a twin, excluding it would turn a working picture into Error.

### From ticket 02: `ListCaptureDevices` on the window's thread at each open
- Verdict: rejected.
- Why: only the Capture Card submenu waits (one enumeration, milliseconds). Presenting, capture and sound run on their own threads.

### From ticket 02: About's mode after a mid-stream type change
- Verdict: rejected (Nit, never seen).
- Why: fixing needs the callback's type read across threads. The code comment (`AdoptCurrentType`) already says About keeps the mode set at Start.

### From ticket 04: About's `Shown:` meaning; rates 0 after a restart
- Verdict: nothing to do. The wording is untouched ("frames shown" in the hotkey text, "Shown:" in About). 0 right after a restart is true: no frames yet.

## Hand checks for the user

1. Device > Capture Card: one check mark? On the GC553?
2. With the card busy (OBS open): Help > About says the card name "(not playing)"?
3. Device > Resolution > 1280x720 while playing. Picture comes? Then Default: back to 1080p?
4. Pick a resolution with the Console off. "No signal"? Switch the Console on: does the picture come in that resolution?
5. Device > Frame rate > 30: log has `Menu: Frame rate 30`?
6. Audio menu: Volume and Audio Delay lines show the right values after pressing Up, J, K?
7. The ticket 08 checklist (eight items) is still open.

## Decisions for the user

- A chosen resolution that opens but sends nothing for 3 s:
  - A) As now: "No signal", retried in that mode. Pick Default by hand.
  - B) Fall back to 1080p for the run. But a Console still off at launch would also drop a remembered resolution for that run.
  - Left at A.
- After a resolution has played once, its later start failures:
  - A) As now: retried, Error if repeated; the choice stays.
  - B) Fall back to 1080p on any first-start failure, even after it played. An unplug while playing would then drop it for the run.
  - Left at A.

## Passed to other tickets

- Ticket 07, `src/core/audio_controls.cpp`: the feedback says "Audio delay"; the glossary says "Audio Delay" (Claude, outside scope).
- Ticket 09, `src/app/main.cpp` `HandleMenuCommand`: choosing a card now saves its identifier at once (was cleared and learnt later). Choosing the card already playing no longer copies this run's resolution into the remembered one. `LearnCaptureCardId` unchanged.
- Ticket 09: the resolution fallback (failed or not offered) still resets only `settings`; the remembered value stays.
- Ticket 02 (done), `src/platform/presenter.cpp`: Astra's outside note on 720x576 shown 5:4. Already covered by ticket 02's hand check for its F3.
