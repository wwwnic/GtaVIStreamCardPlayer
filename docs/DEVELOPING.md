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

`--run N` closes the app by itself after N seconds, for unattended smoke runs. Each run rewrites `GamePlayer.log` beside the executable: the capture devices found, every mode the card offers, the chosen device and mode, the colour range and matrix read from that mode, then frame counts and the arrival-to-present time (once a second for ten seconds, then once a minute). For sound: every audio capture endpoint, the chosen one, both endpoints' formats, periods and buffers, the buffer target, then (same rhythm) the fill, drift correction rate, underruns and overruns.

## Module layout

The pipeline has three stages (ADR 0002): Source, Processor, Presenter. Names follow `GLOSSARY.md`.

- `src/core/`: pure logic with no window, device or GPU. All of it is unit-tested in `tests/`.
  - `frame.h`: the `Frame` that travels through the pipeline (pixels, format, colour description, arrival time) and the pool that recycles frame buffers.
  - `source.h`: the `Source` interface. Capture sits behind it so another implementation could replace Media Foundation.
  - `processor.h`: the `Processor` interface (one frame in, zero or more timed frames out) and the pass-through Processor.
  - `capture_choice.h`: which capture device and which mode to use, with no configuration.
  - `colour.h`: the YUV to RGB numbers for a frame's colour range and matrix.
  - `picture_fit.h`: where the 16:9 picture goes inside the Video Area.
  - `latest_slot.h`: the single-frame hand-off between threads (a newer frame replaces an unshown one).
  - `sound_playout.h`: everything between sound capture and sound output: priming to the target fill (`kTargetFillSeconds`, the one number to tune), underrun and overrun recovery. Built from `sound_ring.h` (the buffer), `drift_correction.h` (holds the fill at its target by a tiny playback rate change), `resampler.h` (rate conversion) and `pcm.h` (sample format conversion). `capture_choice.h` also picks the audio capture endpoint.
- `src/platform/`: the parts that talk to Windows. Not unit-tested; checked by review, the debug log and the user's eyes.
  - `mf_source.*`: the Media Foundation Source Reader implementation of `Source`.
  - `presenter.*`: the `Presenter`. Owns the Direct3D 11 device, the flip-model swap chain, the shaders and its own presenting thread.
  - `sound.*`: the Feed sound. WASAPI shared mode, event-driven, capture from the card's endpoint and render to the Windows default output, following changes to that default.
  - `log.*`: the debug log.
- `src/app/main.cpp`: the window and the wiring of Source to Processor to Presenter.
- `third_party/doctest/`: the vendored test framework (one header).
- `prototype/`: the throwaway prototype from ticket 01. Reference only; not part of the app.

Threads: the window's thread runs the message loop; a Media Foundation thread delivers frames and runs the Processor; the Presenter's thread uploads, converts and presents. The Source's thread never waits for the GPU. Sound has three more: capture and render (MMCSS "Pro Audio"), and a control thread that re-opens the output and writes the sound log lines.

Testing policy: unit tests for pure logic, code review for the rest, and the user checks picture and sound by hand.
