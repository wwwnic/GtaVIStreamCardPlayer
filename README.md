# GamePlayer

A small Windows 11 viewer that shows a game console's picture and sound from a USB capture card on your PC monitor, with as little delay as possible.

It does one thing: play the console on the PC screen. No recording, no streaming, no overlays.

## What it does

- Shows the capture card's picture in a window or borderless fullscreen (F11, Esc to leave).
- Plays the card's sound on the Windows default output, kept in step with the picture.
- Presents each new frame as soon as it arrives. Frames the console merely repeats are dropped, so a 30 fps game is presented at 30.
- Says what is wrong instead of staying black: no capture card, card in use by another app, no signal, loading. Recovers by itself when the card or the console comes back, and after sleep.
- Volume, mute and Audio Delay from the keyboard.
- A menu bar for capture card, resolution, frame rate, audio, always on top and V-sync.
- Remembers its settings and window position.
- Keeps the PC and the display awake while a game plays.
- A stable window for [Lossless Scaling](docs/lossless-scaling-setup.md) frame generation, which stays outside the app.

## Requirements

- Windows 11, a Direct3D 11 graphics card.
- A UVC capture card with uncompressed 1080p60 output. Built and tested with a GC553 Pro only; other cards may work but are untested.

## Run

Download `GamePlayer.exe` from the Releases page and start it. It is a single file, with nothing to install. Plug in the capture card, switch the console on.

Only one program can use a capture card at a time: close OBS or the card's own software first.

## Keys

| Key | Action |
|---|---|
| F11 | Fullscreen on / off |
| Esc | Leave fullscreen |
| Up / Down | Volume |
| M | Mute |
| J / K | Sound 50 ms earlier / later (Audio Delay) |
| G | Frame generation off / 2x / 4x / Target |
| U | Sharp upscale (also sharpens a picture shown at its own size) |
| N | Night mode off / low / high / auto (lifts dark scenes; auto follows the scene) |
| F | FPS counter |
| C | Show or hide the clock and session time |
| P | Pause or resume the session timer |

Settings are kept in `%APPDATA%\GamePlayer\settings.ini`. Delete the file to return to the defaults.

A debug log, `GamePlayer.log`, is rewritten beside the executable on each run.

## Build

Needs Visual Studio Build Tools 2022 with the C++ workload. Nothing has to be on PATH.

    build.bat
    test.bat

`build.bat` produces `build\GamePlayer.exe`; `test.bat` builds and runs the unit tests. See [docs/DEVELOPING.md](docs/DEVELOPING.md) for the module layout and the testing policy.

## Documents

- [docs/DEVELOPING.md](docs/DEVELOPING.md): build, run, module layout.
- [docs/lossless-scaling-setup.md](docs/lossless-scaling-setup.md): one-time Lossless Scaling setup.
- [GLOSSARY.md](GLOSSARY.md): the project's terms.
- [docs/adr/](docs/adr/): the two architecture decisions.
- [docs/research/capture-viewer-research.md](docs/research/capture-viewer-research.md): the research behind them.

## Limits

Not in version 1: recording, streaming, screenshots, HDR, VRR, capture above 60 fps, an audio output picker, an installer.

## License

MIT, see [LICENSE](LICENSE). The unit tests use [doctest](third_party/doctest/), under its own MIT license.
