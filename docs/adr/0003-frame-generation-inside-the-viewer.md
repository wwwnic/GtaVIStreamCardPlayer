# Frame Generation inside the Viewer, on the GPU, with AMD's frame rate conversion

Replaces ADR 0002. Two of its points (the Generation Factor is the user's alone; V-sync ignored while generating) are replaced for Target only by ADR 0004.

ADR 0002 kept Frame Generation outside the Viewer because a video Feed has no motion vectors or depth and an in-house generator looked like a large effort with uncertain results. A throwaway prototype (branch `prototype/frame-generation`) changed that: AMD's AMF frame rate conversion component (FRC) makes in-between frames from pictures alone, runs on the Viewer's own Direct3D 11 device, and the user judged the result good at 2x and 4x. The Viewer therefore does Frame Generation itself, so that playing needs no second program. The same prototype showed a Sharp Upscale (FSR 1, two shader passes) is worth having for a 720p Feed.

Frame Generation runs on the GPU side of the pipeline, next to the Presenter, after the frame has been converted to RGB. The Processor keeps its one job on CPU frames: with Auto, dropping Repeated Frames so only Unique Frames go on. The existing explicit `60 fps (every frame)` override intentionally passes every captured frame, including Repeated Frames, to the Presenter and FRC. Generation follows that chosen cadence; it does not override the user's explicit frame-rate setting. The rules for when each frame is shown are pure logic in `core`, unit-tested like the rest.

## Considered options

- **In the Processor, as ADR 0002 expected.** The Processor handles CPU byte buffers on the Source's thread. Generating there means uploading to the GPU, generating, and reading the result back to the CPU only to upload it again. That costs time on every frame, and Latency is the one thing the user asked to keep lowest.
- **FSR 3 or DLSS frame generation.** Still impossible: both need motion vectors and depth from the game.
- **AMD's driver feature (AFMF).** No code, but it is a driver setting outside the Viewer, with no key in the Viewer and no knowledge of the Game Rate.
- **An open neural interpolator (RIFE) or our own optical flow.** Works on any GPU brand, but is heavier, adds a second graphics stack or months of work, and was not needed once FRC proved good enough.

## Consequences

- In-app Frame Generation needs an AMD graphics card and driver. On any other PC the Viewer still shows the Feed; the Frame Generation choice is unavailable and says why. The Viewer runs on one PC, the user's, so this was accepted.
- Lossless Scaling stays supported whenever in-app Frame Generation is off. Everything ADR 0002 required for that still holds in that state: only Unique Frames are presented, fullscreen is a borderless window, the window title never changes. With in-app Frame Generation on, the two must not run together; that is left to the user.
- The Presenter must show several frames per Unique Frame at exact times. Before this it showed each frame the moment it arrived.
- Frame Generation holds back each real frame: by half the gap between Unique Frames at 2x, three quarters at 4x (about 17 ms and 25 ms at a 30 fps Game Rate). The user turns it off with one key when that matters.
- The Generation Factor is the user's choice alone. The Viewer does not lower it when the result exceeds the monitor's refresh rate.
- V-sync is ignored while Frame Generation is on: waiting for the monitor would break the timing of Generated Frames.
- AMD's AMF header files live in `third_party/` under their MIT licence. The runtime comes with the AMD driver; nothing is installed by the Viewer.
