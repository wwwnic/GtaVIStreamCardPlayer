# Native C++ with Direct3D 11, Media Foundation, WASAPI and Win32

The Viewer's top priority is low Latency, and that is decided by the pipeline (uncompressed capture, no queuing, no hidden converters, flip-model presentation), not by the language. We chose C++ with Direct3D 11, Media Foundation Source Reader, WASAPI and a plain Win32 window and menu, because every API involved is native C++ with first-party samples, there is no garbage collector or interop layer in the audio and video paths, and no UI framework sits between the swap chain and the screen. An independent review that was not told this choice reached the same stack.

## Considered Options

- **C# / .NET with Direct3D bindings**: faster to write, but GC pauses and interop in the hot paths. Ranked second.
- **Rust (wgpu, or forking the MIT-licensed `vicash` viewer)**: equal speed, more glue work, and reaching independent flip through wgpu is unverified.
- **Building on libobs or libmpv**: inherits the overhead and configuration surface the Viewer exists to avoid.
- **Direct3D 12**: nothing a simple blit needs, and much easier to get wrong.
- **DirectShow for capture**: kept as the fallback only if Media Foundation proves slower or is missing modes on the Capture Card; capture sits behind an interface for that reason.
