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

F11 toggles borderless fullscreen; Esc leaves it. Up / Down change the volume, M mutes, J / K move the sound 50 ms earlier / later (Audio Delay, 0-500 ms); each shows a short line centred at the top for 1.5 s (a held key at a limit repeats nothing on screen; only the time restarts). F (or View > FPS counter) toggles the FPS counter top-left (with Frame Generation off, the presented rate, e.g. "30 FPS": the Game Rate on Auto, the Frame rate override's rate otherwise; blank for the first two seconds after a start or Feed restart, until the rate window is full; hidden while a state message such as Loading or No signal shows; the Signal Rate is in Help > About and the log; off on a fresh install, then remembered; drawn only into frames presented anyway, so it never adds a present and freezes on a still picture). `--rate auto|30|60` sets the Game Rate override, `--tolerance X` the near-duplicate tolerance (e.g. 0.005, at most 0.05), for one run (neither is saved). Once a second the log has a `Frames:` line: arrived, unique, repeats caught exactly and by the tolerance, presented, dropped, the Game Rate (detected), presented rate and Signal Rate, the largest near-repeat share and the closest Unique Frames (under 1% of words differing: a sign repeats are not bit-identical), the compare time before and after presenting, and the FPS counter text (or `warming up`, `off`). With Frame Generation running the counter has two equally sized lines, `Game 30` and `Shown 120`. Game keeps the real-frame rate above; Shown measures all successful DXGI presents, including Generated Frames and repaints, over the last two seconds without a monitor cap. Starting, failed and unavailable generation retain the one-line counter. Help > About labels the override rate `Game:` during generation and shows measured total presents as `Shown:`; with generation off it keeps the old `Shown:` override line. It also reports the actual Generation Factor (or off, with the availability/startup reason), plus actual Sharp Upscale usage and Feed -> shown sizes. The Presenter publishes these actual states under its statistics mutex; the FPS counter still never wakes it. Only one copy runs: while one is open, starting another just brings it forward (so a smoke run does nothing while the user's copy is open).

The menu bar (windowed only, hidden in fullscreen) overrides the automatic choices: Capture Card, Resolution, Frame rate, the audio values, Fullscreen, Always on top, V-sync (off by default), FPS counter. Two Capture Cards of the same name are numbered and told apart by their identifier. A chosen resolution that does not start, or that the card does not offer, gives way to the default mode for that run, with a short line saying so. Each choice logs a `Menu:` line; `Menu: open` / `closed` bracket an open menu.

Remembered settings (ticket 09): the menu choices, volume, mute, Audio Delay and the windowed position, size and maximised state are kept in `%APPDATA%\GamePlayer\settings.ini` (key=value, `version=1`; delete it for the defaults), and the FPS counter. Never fullscreen: the app always opens windowed, at the position it had before fullscreen. Saved about two seconds after a change settles and at exit, through a temporary file renamed over the old one. Anything missing or invalid is the default for that value; the window reopens at the size it had (a snapped one too, with black bars); a remembered position not fully on a connected monitor gives the default size on the primary monitor, not maximised. A save that fails is tried again after 30 s, on the next change, and at exit. The Capture Card is found by its symbolic link, then its name, else the automatic choice (the preference is kept). `--settings PATH` uses another file: smoke runs should, so the user's own file is left alone. The log has `Settings:` lines (what was loaded, each save) and `Window: remembered position ...`.

`--run N` closes the app by itself after N seconds, for unattended smoke runs. Each run rewrites `GamePlayer.log` beside the executable: the capture devices found, every mode the card offers, the chosen device and mode, the colour range and matrix read from that mode, then frame counts and the arrival-to-present time (once a second for ten seconds, then once a minute). For sound: every audio capture endpoint, the chosen one, both endpoints' formats, periods and buffers, the buffer target, then (same rhythm) the fill, drift correction rate, underruns and overruns. Video Area: every state change (`Video Area: Loading -> Playing`), and once a second a `Signal:` line with every value no-signal detection could use (frames, stream ticks, type changes, discontinuities, luma of the latest frame, sound frames, silent frames, peak). Retries log their details only when the device list changed. Each audio key logs the new volume, mute, Audio Delay and buffer target.

Press G or choose View > Frame generation to cycle through off, 2x and 4x. The settings file remembers the choice as `frameGeneration=off`, `frameGeneration=2x` or `frameGeneration=4x`. Missing and unrecognised values select off. V-sync is unavailable while generating. With generation off, the Lossless Scaling present rules below apply.

`platform/amf_generator.cpp` loads the AMD driver's AMF runtime from the Windows system directory. A worker creates FRC on the Presenter's Direct3D 11 device at the Feed size. It holds its own device reference until setup finishes, so the Presenter can show real frames, switch off, rebuild or stop during setup. FRC uses interpolated-only mode, high quality, fallback blending and no future frame. The Presenter copies each BGRA input into a four-texture ring. After submitting N it polls for about 2 ms, discarding outputs whose timestamp differs from N. A matching intermediate frame precedes N by half the Unique Frame interval. When none is ready, N is shown alone. The Presenter draws its texts after generation. A runtime failure disables generation for the session and preserves the saved choice.

At 4x a second FRC processes the doubled stream. It receives the middle frame and then real frame N, each with its own timestamp. Both matching quarter frames must arrive to show first quarter, middle, last quarter and N, each a quarter interval apart. A missing quarter leaves only the middle and N. After missing a middle or a discontinuity, the second FRC starts its history at N. A second-stage failure leaves 2x running and updates the feedback and menu. The Viewer applies no monitor refresh cap.

`FrameGen:` logs the start result and, during playback, the factor, Generated Frames made, Unique Frame intervals attempted, quarter frames used, successful presents, discarded late outputs, and average and worst generation time per attempt on the presenting thread. GPU time is unmeasured.

The developer-only command-line switch `--frames-per-unique 2` or `--frames-per-unique 4` repeats each picture at equal intervals. The presenting thread asks the GPU-side `FrameGenerator` for intermediate pictures and draws the overlays onto each output. The pure `FrameSchedule` uses the last two Unique Frame arrival times for spacing and anchors all deadlines to the first present. A high-resolution waitable timer wakes the presenting thread. Text, resize and menu wake-ups recheck the same deadline. A new arrival supersedes an unfinished sequence. Starts, Feed restarts, device rebuilds, picture-size changes and unsuitable arrival intervals show one frame immediately. During timed sequences the Presenter uses unsynchronised presents. The once-per-second `Timed frames` log counts Unique Frames, successful presents, early drops and late skips, and records the worst lateness and Present call duration. The switch is never saved.

## Sharp Upscale

Press U or select View > Sharp upscale to toggle the remembered choice (off on first use).
For an enlarged Feed the Presenter runs FSR 1 EASU directly from each finished picture
to a texture matching the fitted picture rectangle. RCAS sharpens that texture pixel for
pixel at 0.2 stops below maximum. Both passes use the same display-encoded RGB as the
plain stretch. Generated Frames follow the same path and the text overlay is drawn last.
Native-size and smaller pictures use the smooth path. The optional D3D11 shaders require
no AMF runtime. An `Upscale: FSR 1 on/off` transition logs the Feed and shown sizes.
Compilation or resource failure logs the reason and falls back to smooth stretching.

## Lossless Scaling

Use in-app Frame Generation or Lossless Scaling, never both: Lossless Scaling would count Generated Frames as real ones. The user's one-time setup guide is `docs/lossless-scaling-setup.md`, written in the user's vertical style (very short lines, one idea per line, no bullets); keep that style when editing it. What keeps the window a good target (ticket 10): the title is set once to exactly "GamePlayer" and never changed (no `SetWindowText`; debug numbers go to the log or the FPS counter, never the title); the exe is `GamePlayer.exe`; DXGI Alt+Enter is disabled and nothing calls `SetFullscreenState`; the Presenter presents only for a new Unique Frame, a resize, a device rebuild, or a text that appears or ends (the FPS counter never presents; the Frame rate override "60 fps (every frame)" passes Repeated Frames on too, so Lossless Scaling then sees 60); fullscreen uses `HWND_TOP`, never topmost, and hides the cursor after 2 s.

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
  - `presentation_info.h`: actual generation/upscale state and rolling successful-present counts for Shown; no refresh cap.
  - `picture_fit.h`: where the 16:9 picture goes inside the Video Area.
  - `window_shape.h`: the window rectangle while an edge or corner is dragged, so the windowed Video Area stays exactly 16:9 (WM_SIZING), and whether a remembered one may be restored.
  - `monitor_choice.h`, `cursor_idle.h`, `stay_awake.h`: which monitor fullscreen covers, when the cursor hides, when to keep the PC awake.
  - `video_area_state.h`: the Video Area states (Loading, No Capture Card, Card busy, No signal, Playing, Error), their messages, and when to restart picture and sound.
  - `video_area_text.h`: text shown in the Video Area (headline and smaller detail line, three places) and its sizes for a given window.
  - `luma_summary.h`: brightness of a frame on a coarse grid, logged for no-signal investigation only.
  - `latest_slot.h`: the single-frame hand-off between threads (a newer frame replaces an unshown one).
  - `sound_playout.h`: everything between sound capture and sound output: priming to the target fill (`kTargetFillSeconds`, the one number to tune), underrun and overrun recovery. Built from `sound_ring.h` (the buffer), `drift_correction.h` (holds the fill at its target by a tiny playback rate change), `resampler.h` (rate conversion) and `pcm.h` (sample format conversion). `capture_choice.h` also picks the audio capture endpoint. The playout also applies the gain (volume, mute) and the Audio Delay (extra target fill), both without clicks; every start of the sound fades in from silence to the gain set. An Audio Delay change keeps the clock difference drift correction has learnt.
  - `audio_controls.h`: what Up, Down, M, J, K do to volume, mute and Audio Delay, and the confirmation line for each.
  - `shown_texts.h`: the texts the Video Area shows per place, when the brief ones (that line) end, and whether a change needs a present; the Presenter keeps one and passes the clock in.
- `src/platform/`: the parts that talk to Windows. Not unit-tested; checked by review, the debug log and the user's eyes.
  - `mf_source.*`: the Media Foundation Source Reader implementation of `Source`.
  - `presenter.*`: the `Presenter`. Owns the Direct3D 11 device, the flip-model swap chain, the shaders and its own presenting thread. Recreates the device if it is lost. Text in the Video Area: `ShowText(place, text, durationMs)` from any thread (durationMs > 0 for brief feedback); `ShowPicture(false)` while a state message shows.
  - `text_overlay.*`: draws those texts with Direct2D and DirectWrite into the back buffer.
  - `feed_keeper.*`: runs Source, Processor, Presenter and sound; follows `video_area_state.h`, restarts picture and sound together, reacts to device notifications and resume from sleep.
  - `sound.*`: the Feed sound. WASAPI shared mode, event-driven, capture from the card's endpoint and render to the Windows default output, following changes to that default. Volume, mute and Audio Delay are plain get / set values here.
  - `feedback.*`: shows the confirmation line through `Presenter::ShowText(TextPlace::Top, ..., 1500)`. The Presenter presents by itself when a text appears or ends, so it shows and clears on time even with a static, black or absent picture.
  - `fullscreen.*`: borderless fullscreen (F11, Esc), the hiding cursor (after 2 s still; windowed only over the Video Area of the active window, never during a menu, move or resize), monitor changes. Window chrome (menu bar and topmost flag included) is hidden and restored in one place, `ShowChrome`.
  - `menu_bar.*`: the native menu bar, refreshed from `menu_model.h` each time a menu opens.
  - `keep_awake.*`: no system or display sleep while a Feed plays (the Playing state only). Holds a power request (`powercfg /requests`, elevated, lists "GamePlayer: a Feed is playing") and the window thread's execution state, and resets the idle timers each second. Logs take, release (with reason), a minute-by-minute "still held", and every display on / off / dimmed.
  - `settings_store.*`: the settings file's place (`SHGetKnownFolderPath`), reading it, and writing it safely.
  - `single_instance.*`: a second launch brings the running window forward once it is shown and exits (waits up to 10 s; if the running copy was closing, this one starts).
  - `log.*`: the debug log.
- `src/app/main.cpp`: the window. Opens with a 16:9 Video Area (1280x720, smaller if the monitor is), minimum 480x270 at 100% scaling, and keeps 16:9 while dragged and when moved to a monitor with other scaling (`WM_DPICHANGED`). Maximised, fullscreen, snaps and programmatic sizes may give other shapes; the picture then keeps its shape with black bars.
- `third_party/doctest/`: the vendored test framework (one header).
- `prototype/`: the throwaway prototype from ticket 01. Reference only; not part of the app.

Threads: the window's thread runs the message loop; a Media Foundation thread delivers frames and runs the Processor; the Presenter's thread uploads, converts and presents. The Source's thread never waits for the GPU. Sound has three more: capture and render (MMCSS "Pro Audio"), and a control thread that re-opens the output and writes the sound log lines.

Testing policy: unit tests for pure logic, code review for the rest, and the user checks picture and sound by hand.
