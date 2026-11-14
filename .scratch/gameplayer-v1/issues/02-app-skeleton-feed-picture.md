# 02: App skeleton showing the Feed picture in a window

**What to build:** The real start of GamePlayer. When the user launches the app with the Capture Card connected and the Console on, a normal resizable window titled "GamePlayer" opens and shows the Feed picture with no configuration. There is no sound, menu or fullscreen yet. This ticket also establishes the project's build setup, unit-test harness and internal structure that every later ticket builds on, so structure matters here far more than it did in the prototype.

**Blocked by:** 01 (Throwaway prototype proving the pipeline), and the plan review that follows it.

**Status:** needs-plan-review

**Gate:** Do not start this ticket until the user has reviewed the findings note from ticket 01 (the throwaway prototype) and has confirmed or revised the plan. This ticket may be rewritten at that review; if its status still reads needs-plan-review, stop and ask the user rather than beginning work.

## Context

GamePlayer is a small personal Windows 11 app (the "Viewer") that shows a game Console's picture and sound from a USB Capture Card on the PC monitor, replacing OBS for that single job. Its top priority is low Latency; it does video and audio only. It is for one user, never sold or distributed, shipped as a plain executable with no installer.

Read these first:

- `GLOSSARY.md` (repo root): project vocabulary. Use these terms for names in code and comments.
- `docs/adr/0001-native-cpp-direct3d11-stack.md` and `docs/adr/0002-frame-generation-stays-outside-the-viewer.md`: settled architecture decisions.
- The prototype's findings note (in the prototype folder created by ticket 01). It records what the Capture Card really exposes and anything that contradicted our assumptions. Where it disagrees with this ticket, the findings win; say so in your report.
- `docs/research/capture-viewer-research.md`: background; consult as needed.

Hardware: Xbox Series S (1080p, 60 Hz, HDR off, VRR off); AVerMedia Live Gamer Ultra S GC553 Pro over USB; AMD Radeon RX 9070; 1080p 144 Hz FreeSync SDR monitor; Windows 11.

The prototype folder is reference only. You may read it to see what worked, but do not copy its structure or move its files into the app; write the app code fresh.

## Decisions already made

- **Stack** (ADR 0001): C++ (C++20), Direct3D 11, Media Foundation Source Reader for capture, plain Win32 window. No Qt, WinUI, Electron or other UI framework: they put a compositing layer between the swap chain and the screen.
- **Build**: CMake with the MSVC toolchain, producing one executable named GamePlayer. One documented command to build and one to run the unit tests.
- **Unit-test harness**: set one up now (a small, common, header-only or easily vendored framework is preferred so the build has no heavy external dependency). The testing policy for this project is: unit tests for pure logic, code review for the rest, and the user checks picture and sound by hand because an agent cannot see the monitor.
- **Three-stage pipeline** (ADR 0002): Source, Processor, Presenter.
  - *Source* produces captured frames with an arrival timestamp (QueryPerformanceCounter). It sits behind a small interface so that a DirectShow implementation could replace Media Foundation later without touching the rest. Only the Media Foundation implementation is built, unless the prototype findings say Media Foundation is unusable on this card.
  - *Processor* takes frames in and emits zero or more frames out, each with the time it should be shown. In this ticket it is a pass-through. Ticket 04 makes it drop Repeated Frames; later, in-app Frame Generation or sharpening would live here. Design the interface with that future in mind: it must be able to emit more frames than it receives, and to see the previous two or three Unique Frames.
  - *Presenter* owns the Direct3D 11 device and swap chain and shows frames. It runs on its own thread and is woken when a frame is ready; it is not called directly from the capture callback. Reason: a later Processor that generates frames needs to schedule presents at times that do not coincide with capture callbacks.
- **Deep modules**: the code will be maintained mostly by AI agents. Prefer a few modules with small, stable interfaces that hide a lot behind them over many thin files. Keep pure logic (anything testable without a device, a window or a GPU) separate from the parts that talk to Windows.
- **Capture format**: 1920x1080 at 60 fps, uncompressed. Prefer YUY2, fall back to NV12 (or whatever the prototype findings showed is best). No MJPEG. No converter or video processor inserted by Media Foundation; convert YUV to RGB (BT.709, limited range unless the media type says otherwise) in the app's own pixel shader.
- **Device selection**: zero configuration. Prefer the GC553 Pro by its device name; otherwise use the first video capture device. A device picker arrives with the menu in ticket 08.
- **Presentation**: flip-model swap chain (`DXGI_SWAP_EFFECT_FLIP_DISCARD`, 2 buffers, `ALLOW_TEARING` and `FRAME_LATENCY_WAITABLE_OBJECT` flags). Present each frame as soon as it is ready with tearing allowed when the system supports it. Never queue: the newest frame replaces an unshown older one. Reason: the monitor has FreeSync, so the display follows the Viewer's pace with the lowest delay.
- **Window**: opens windowed at a sensible default size, resizable, titled exactly "GamePlayer". The exact title and executable name must stay stable because Lossless Scaling matches its profile on them (ticket 10).
- **Picture shape**: the Feed keeps its 16:9 shape at any window size; the remaining area is black. Never stretch.

## Known unknowns

- If the prototype found that Media Foundation lacks a usable uncompressed 1080p60 mode on this card, stop and report before building a DirectShow Source; that is a change of plan the user should hear about.
- Whether presenting from a dedicated thread costs any delay compared with the prototype's direct path. It should not, if the Presenter is woken immediately by an event; keep the hand-off to a single latest-frame slot, not a queue.
- Independent flip (bypassing the desktop compositor) in a window that will later have a menu bar is unverified on this GPU. Nothing to do here beyond using the flip model correctly; note it if you find evidence either way.

## Out of scope

Sound (03), dropping Repeated Frames and Game Rate detection (04), fullscreen, cursor hiding, sleep prevention, single instance (05), loading and error messages and recovery (06), volume / mute / Audio Delay (07), menu bar (08), remembered settings (09). If the card is absent in this ticket, a black window is acceptable; do not crash. Also excluded from v1 entirely: recording, streaming, screenshots, sharpening, in-app Frame Generation, HDR, VRR capture, 120 fps modes.

## Acceptance criteria

- [ ] One documented command builds the app with CMake and MSVC into a single GamePlayer executable; one documented command runs the unit tests.
- [ ] A unit-test harness exists with at least one real test of pure logic (for example the 16:9 fit calculation, or capture mode selection from a list of modes).
- [ ] Launching the app with the Capture Card connected and the Console on shows the Feed picture in a window titled "GamePlayer" with no user action.
- [ ] The picture keeps its 16:9 shape with black bars while the window is resized, including during the drag.
- [ ] Capture is uncompressed 1080p60 through Media Foundation Source Reader with no inserted converter; colour conversion happens in the app's own shader.
- [ ] The code is organised as Source, Processor and Presenter with small interfaces; capture is behind an interface; the Processor is pass-through; the Presenter runs on its own thread and holds only the latest frame.
- [ ] Presentation uses a flip-discard swap chain and presents on arrival, with tearing allowed when supported.
- [ ] The GC553 Pro is chosen automatically when present; otherwise the first video capture device is used.
- [ ] Launching without a Capture Card does not crash.
- [ ] Closing the window shuts everything down cleanly (no hang, no leaked device).
- [ ] A short developer note in the repo explains how to build, how to run the tests, and the module layout in a few sentences.

## How to verify

Yourself: the build succeeds, the unit tests pass, the app starts and exits cleanly, and with the card attached your logging (keep a simple debug log) shows the chosen device, the chosen mode and frames flowing.

For the user. End your work by reporting this checklist:

1. Start GamePlayer with the Xbox on. Does the picture appear by itself?
2. Does it look right: correct colours, not washed out, not too dark, not stretched?
3. Drag the window edges. Does the picture keep its shape with black bars?
4. Does it feel as responsive as the prototype did?
5. Close the window. Does the app exit straight away?
