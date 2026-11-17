# Frame Generation stays outside the Viewer for now

Status: superseded by ADR 0003 (Frame Generation inside the Viewer). Its consequences still apply whenever in-app Frame Generation is off.

Frame Generation on a video Feed has no game motion vectors or depth, so DLSS and FSR frame generation cannot be used, and matching the quality of the existing Lossless Scaling app in-house would be a large effort with uncertain results. The Viewer therefore does no Frame Generation itself: Lossless Scaling runs on top of the Viewer's window, configured once by the user, and the Viewer never controls it.

## Consequences

- The Viewer must present only Unique Frames. Lossless Scaling counts presented frames, so Repeated Frames (a 30 fps Game Rate inside a 60 Hz Signal Rate) would make it see 60 and generate nothing useful.
- Fullscreen is a borderless window, never exclusive fullscreen, because Lossless Scaling cannot work on exclusive fullscreen.
- The pipeline is split into Source, Processor and Presenter. Today the Processor only drops Repeated Frames; in-app Frame Generation or sharpening can later be added there without a rewrite.
- Frame Generation adds Latency (it must hold back a real frame), so it remains a per-game choice made by the user in Lossless Scaling.
