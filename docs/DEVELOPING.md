# Developing GamePlayer

Needs Visual Studio Build Tools 2022 with the C++ workload (MSVC, Windows SDK, and the bundled CMake and Ninja). Nothing has to be on PATH; the scripts find the tools themselves.

## Build

    build.bat

Configures with CMake (Ninja, MSVC, RelWithDebInfo) and builds everything into `build\`. The app is `build\GamePlayer.exe`, a single executable with the C runtime linked in.

## Run the unit tests

    test.bat

Builds the test program and runs it (`build\gameplayer_tests.exe`, doctest). It exits non-zero if a test fails.

## Run

    build\GamePlayer.exe

F11 toggles borderless fullscreen; Esc leaves it. Up / Down change the volume, M mutes, J / K move the sound 50 ms earlier / later (Audio Delay, 0-500 ms); each shows a short line top-right. F toggles the FPS counter top-left ("Game Rate / Signal Rate", off at launch; drawn only into frames presented anyway, so it never adds a present and freezes on a still picture). `--rate auto|30|60` sets the Game Rate override, `--tolerance X` the near-duplicate tolerance (e.g. 0.005), for one run (neither is saved). Once a second the log has a `Frames:` line: arrived, unique, repeats caught exactly and by the tolerance, presented, dropped, both rates, the largest near-repeat share and the closest Unique Frames (under 1% of words differing: a sign repeats are not bit-identical), and the compare time before and after presenting. Only one copy runs: while one is open, starting another just brings it forward (so a smoke run does nothing while the user's copy is open).

The menu bar (windowed only, hidden in fullscreen) overrides the automatic choices: Capture Card, Resolution, Frame rate, the audio values, Fullscreen, Always on top, V-sync (off by default). Each choice logs a `Menu:` line; `Menu: open` / `closed` bracket an open menu.

Remembered settings (ticket 09): the menu choices, volume, mute, Audio Delay and the windowed position, size and maximised state are kept in `%APPDATA%\GamePlayer\settings.ini` (key=value, `version=1`; delete it for the defaults). Never fullscreen, never the FPS counter: the app always opens windowed, at the position it had before fullscreen. Saved about two seconds after a change settles and at exit, through a temporary file renamed over the old one. Anything missing or invalid is the default for that value; a remembered position not fully on a connected monitor gives the default size on the primary monitor. The Capture Card is found by its symbolic link, then its name, else the automatic choice (the preference is kept). `--settings PATH` uses another file: smoke runs should, so the user's own file is left alone. The log has `Settings:` lines (what was loaded, each save) and `Window: remembered position ...`.

`--run N` closes the app by itself after N seconds, for unattended smoke runs. Each run rewrites `GamePlayer.log` beside the executable: the capture devices found, every mode the card offers, the chosen device and mode, the colour range and matrix read from that mode, then frame counts and the arrival-to-present time (once a second for ten seconds, then once a minute). For sound: every audio capture endpoint, the chosen one, both endpoints' formats, periods and buffers, the buffer target, then (same rhythm) the fill, drift correction rate, underruns and overruns. Video Area: every state change (`Video Area: Loading -> Playing`), and once a second a `Signal:` line with every value no-signal detection could use (frames, stream ticks, type changes, discontinuities, luma of the latest frame, sound frames, silent frames, peak). Retries log their details only when the device list changed. Each audio key logs the new volume, mute, Audio Delay and buffer target.

## Lossless Scaling

The user's one-time setup guide is `docs/lossless-scaling-setup.md`, written in the user's vertical style (very short lines, one idea per line, no bullets); keep that style when editing it. What keeps the window a good target (ticket 10): the title is set once to exactly "GamePlayer" and never changed (no `SetWindowText`; debug numbers go to the log or the FPS counter, never the title); the exe is `GamePlayer.exe`; DXGI Alt+Enter is disabled and nothing calls `SetFullscreenState`; the Presenter presents only for a new Unique Frame, a resize, a device rebuild, or a text that appears or ends (the FPS counter never presents); fullscreen uses `HWND_TOP`, never topmost, and hides the cursor after 2 s.

## Module layout

The pipeline has three stages (ADR 0002): Source, Processor, Presenter. Names follow `GLOSSARY.md`.

- `src/core/`: pure logic with no window, device or GPU. All of it is unit-tested in `tests/`.
  - `frame.h`: the `Frame` that travels through the pipeline (pixels, format, colour description, arrival time) and the pool that recycles frame buffers.
  - `source.h`: the `Source` interface. Capture sits behind it so another implementation could replace Media Foundation.
  - `processor.h`: the `Processor` interface (one frame in, zero or more timed frames out) and the pass-through Processor.
  - `unique_frames.h`: the Processor in use. Drops Repeated Frames (exact compare; near-duplicate tolerance `kNearDuplicateTolerance`, the one number to tune, default 0 = exact only), the Auto / 30 / 60 override, Signal Rate and Game Rate, the last three Unique Frames, the FPS counter text.
  - `capture_choice.h`: which capture device and which mode to use, with no configuration, and the resolutions offered in the menu (uncompressed, at most 60 fps, no MJPEG twins).
  - `viewer_settings.h`: every menu override in one structure with its defaults. `menu_model.h`: the menu's labels, check marks, Hotkeys and About texts.
  - `settings_file.h`: the remembered settings as text and back (every damage gives defaults), and when to save (`SettingsSaveTiming`).
  - `colour.h`: the YUV to RGB numbers for a frame's colour range and matrix.
  - `picture_fit.h`: where the 16:9 picture goes inside the Video Area.
  - `window_shape.h`: the window rectangle while an edge or corner is dragged, so the windowed Video Area stays exactly 16:9 (WM_SIZING), and whether a remembered one may be restored.
  - `monitor_choice.h`, `cursor_idle.h`, `stay_awake.h`: which monitor fullscreen covers, when the cursor hides, when to keep the PC awake.
  - `video_area_state.h`: the Video Area states (Loading, No Capture Card, Card busy, No signal, Playing, Error), their messages, and when to restart picture and sound.
  - `video_area_text.h`: text shown in the Video Area (headline and smaller detail line, three places) and its sizes for a given window.
  - `luma_summary.h`: brightness of a frame on a coarse grid, logged for no-signal investigation only.
  - `latest_slot.h`: the single-frame hand-off between threads (a newer frame replaces an unshown one).
  - `sound_playout.h`: everything between sound capture and sound output: priming to the target fill (`kTargetFillSeconds`, the one number to tune), underrun and overrun recovery. Built from `sound_ring.h` (the buffer), `drift_correction.h` (holds the fill at its target by a tiny playback rate change), `resampler.h` (rate conversion) and `pcm.h` (sample format conversion). `capture_choice.h` also picks the audio capture endpoint. The playout also applies the gain (volume, mute) and the Audio Delay (extra target fill), both without clicks.
  - `audio_controls.h`: what Up, Down, M, J, K do to volume, mute and Audio Delay, and the confirmation line for each.
  - `feedback_line.h`: when that line shows and hides.
- `src/platform/`: the parts that talk to Windows. Not unit-tested; checked by review, the debug log and the user's eyes.
  - `mf_source.*`: the Media Foundation Source Reader implementation of `Source`.
  - `presenter.*`: the `Presenter`. Owns the Direct3D 11 device, the flip-model swap chain, the shaders and its own presenting thread. Recreates the device if it is lost. Text in the Video Area: `ShowText(place, text, durationMs)` from any thread (durationMs > 0 for brief feedback); `ShowPicture(false)` while a state message shows.
  - `text_overlay.*`: draws those texts with Direct2D and DirectWrite into the back buffer.
  - `feed_keeper.*`: runs Source, Processor, Presenter and sound; follows `video_area_state.h`, restarts picture and sound together, reacts to device notifications and resume from sleep.
  - `sound.*`: the Feed sound. WASAPI shared mode, event-driven, capture from the card's endpoint and render to the Windows default output, following changes to that default. Volume, mute and Audio Delay are plain get / set values here.
  - `feedback.*`: shows the confirmation line through `Presenter::ShowText(TextPlace::Top, ..., 1500)`. The Presenter presents by itself when a text appears or ends, so it shows and clears on time even with a static, black or absent picture.
  - `fullscreen.*`: borderless fullscreen (F11, Esc), the hiding cursor, monitor changes. Window chrome (menu bar and topmost flag included) is hidden and restored in one place, `ShowChrome`.
  - `menu_bar.*`: the native menu bar, refreshed from `menu_model.h` each time a menu opens.
  - `keep_awake.*`: no system or display sleep while a Feed plays (the Playing state only). Holds a power request (`powercfg /requests`, elevated, lists "GamePlayer: a Feed is playing") and the window thread's execution state, and resets the idle timers each second. Logs take, release (with reason), a minute-by-minute "still held", and every display on / off / dimmed.
  - `settings_store.*`: the settings file's place (`SHGetKnownFolderPath`), reading it, and writing it safely.
  - `single_instance.*`: a second launch brings the running window forward and exits.
  - `log.*`: the debug log.
- `src/app/main.cpp`: the window. Opens with a 16:9 Video Area (1280x720, smaller if the monitor is), minimum 480x270 at 100% scaling, and keeps 16:9 while dragged. Maximised, fullscreen, snaps and programmatic sizes may give other shapes; the picture then keeps its shape with black bars.
- `third_party/doctest/`: the vendored test framework (one header).
- `prototype/`: the throwaway prototype from ticket 01. Reference only; not part of the app.

Threads: the window's thread runs the message loop; a Media Foundation thread delivers frames and runs the Processor; the Presenter's thread uploads, converts and presents. The Source's thread never waits for the GPU. Sound has three more: capture and render (MMCSS "Pro Audio"), and a control thread that re-opens the output and writes the sound log lines.

Testing policy: unit tests for pure logic, code review for the rest, and the user checks picture and sound by hand.
