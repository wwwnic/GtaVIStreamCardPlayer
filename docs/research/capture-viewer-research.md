# Capture card viewer with upscaling and frame generation: research

Research date: 2026-11-14.
Hardware: AVerMedia Live Gamer Ultra S (GC553Pro) capture card, AMD Radeon RX 9070 GPU, Windows 11, 1080p display, Xbox Series console as source.

Conventions used here:

- Every factual claim has a source link next to it.
- **[Unverified]** means I could not confirm it from a primary source.
- **[Reasoning]** means it is my own inference, not a sourced fact.
- Some vendor pages (amd.com AFMF page and release notes, Elgato help, TechPowerUp, losslessscaling.com) blocked or timed out on direct fetch. Where I rely on search-result extracts of those pages instead, I say so.

## Summary and recommendation

**DLSS 5 on an AMD card: false.**
NVIDIA states DLSS 5 is supported on GeForce RTX 50 Series GPUs, with RTX 40 Series planned later ([NVIDIA, 2026-09-01](https://www.nvidia.com/en-us/geforce/news/dlss-5-3d-guided-neural-rendering/)).
It also needs per-game integration through NVIDIA Streamline and takes engine data (colour, motion vectors, lighting buffers) as input, so it could not run on a captured video feed even on an NVIDIA card ([same source](https://www.nvidia.com/en-us/geforce/news/dlss-5-3d-guided-neural-rendering/), [NVIDIA, 2026-03-16](https://www.nvidia.com/en-us/geforce/news/dlss5-breakthrough-in-visual-fidelity-for-games/)).

**The same engine-data problem rules out FSR 4 / FSR "Redstone" frame generation.**
The RX 9070 supports it, but it needs depth, motion vectors and camera data from a game engine ([GPUOpen FSR Frame Generation](https://gpuopen.com/amd-fsr-framegeneration/)).
A capture feed has none of that.

**What does work on a plain video feed:**

1. Lossless Scaling (LSFG 3). Works on any windowed or borderless window, any GPU vendor ([Steam store page](https://store.steampowered.com/app/993090/Lossless_Scaling/)). People already use it on console capture feeds ([Steam thread, 2024](https://steamcommunity.com/app/993090/discussions/0/4338735089007356929)).
2. AMD Fluid Motion Frames (AFMF 2.1), driver level, free with the RX 9070. Whether it engages on a custom video-viewer window is **[Unverified]** and must be tested.
3. Your own interpolation inside the app (FidelityFX Optical Flow, or RIFE via ncnn/Vulkan). Possible, but the hardest path, and RX 9070 real-time performance is **[Unverified]**.

**Recommended approach:**

1. Try first, no code: a light existing viewer (vicash, Viewfinder, or mpv) plus Lossless Scaling, and separately test AFMF on the same viewer. Details in section 6.
2. If you still want your own app: build a small C++ Win32 + Direct3D 11 viewer using Media Foundation capture, WASAPI audio, and a flip-model swap chain. Make it "frame-gen friendly" (present only unique source frames, borderless fullscreen, vsync off option) so Lossless Scaling or AFMF can sit on top.
3. Only after that, consider in-app frame generation as a stretch goal.

**Honest expectations:**

- Any interpolation must hold back at least one source frame. At 30 fps that is about 33 ms of added delay, plus processing time. **[Reasoning]**
- With a 1080p source and a 1080p display, "upscaling" adds nothing. Only sharpening or capturing 4K and downscaling changes the picture. See section 3.

---

## 0. Your hardware: what was verified

### AVerMedia Live Gamer Ultra S (GC553Pro)

Sources: [AVerMedia product page](https://www.avermedia.com/product-detail/capture-card-GC553Pro), [datasheet PDF dated 2025-02-12](https://storage.avermedia.com/web_release_www/datasheet/GC553Pro_Datasheet_v1_20250212.pdf), [AVerMedia FAQ on supported modes](https://www.avermedia.com/support/faq/3nKRpQbE).

- Interface: USB 3.2 Gen 1 Type-C (5 Gbps). HDMI 2.0 in and out.
- Max pass-through: 2160p60 HDR/VRR, 1440p144 HDR/VRR, 1080p240 HDR/VRR.
- Max capture: 2160p60, 1440p144, 1080p240. HDR capture up to 1440p30 and 1080p60.
- VRR recording: up to 1440p60, 1080p120, 1080p240, and only through AVerMedia Streaming Center.
- Pixel formats: YUY2, NV12, RGB24, P010 (HDR), MJPEG.
- Audio: 5.1 capture only via Streaming Center or its OBS plugin on Windows. LPCM is stereo only.
- No HDCP-protected sources.
- Datasheet lists "Plug & Play".

Per-format capture limits, from the FAQ text summary (the full tables are images I could not read):

- MJPEG: 3840x2160p60, 3440x1440p60, 2560x1440p144, 1920x1080p240.
- NV12: 3840x2160p30, 2560x1440p60, 2560x1080p60, 1920x1080p60.
- YUY2: 2560x1440p50, 2560x1080p60, 1920x1080p60.
- RGB24 (Windows only): 2560x1440p30, 1920x1080p60.
- P010 (HDR10, Windows only): 3840x2160p30, 2560x1440p30, 1920x1080p60.

What this means for the app:

- **1080p60 is available uncompressed** (NV12, YUY2 or RGB24). This is the mode to use. No decode step, so lowest latency. **[Reasoning]**
- **Anything above 60 fps, and 4K60, is MJPEG only.** That needs a JPEG decode per frame, which costs time and some quality. **[Reasoning]**
- 4K uncompressed tops out at 30 fps (NV12/P010). That fits a 30 fps game on paper, but see section 3.
- 1080p60 YUY2 is about 2 Gbps raw, which fits in the 5 Gbps link. **[Reasoning, arithmetic: 1920 x 1080 x 2 bytes x 60]**

Not confirmed:

- **[Unverified]** That the card is strictly UVC/UAC class compliant with no vendor driver. AVerMedia says "Plug & Play" and lists iPadOS and macOS support, which strongly suggests UVC, but I found no page that states "UVC" for this model. Check Device Manager: it should appear under Cameras using the inbox `usbvideo.sys` driver.
- **[Unverified]** Any public AVerMedia SDK for this card. I found none. AVerMedia's listed software is Assist Central Pro, Streaming Center, the OBS plugin and a firmware tool ([product page](https://www.avermedia.com/product-detail/capture-card-GC553Pro)). Plan on standard Windows capture APIs.
- **[Unverified]** Measured capture latency in milliseconds. One review reports no perceivable lag but gives no numbers ([APH Networks review](https://aphnetworks.com/reviews/avermedia-live-gamer-ultra-s-gc553pro/3)).

Known quirks from AVerMedia's own pages:

- Firmware tool v1.1.3.0 fixes brief audio dropouts with 4K60 and 5.1 HDMI audio ([product page](https://www.avermedia.com/product-detail/capture-card-GC553Pro)). Update the firmware first.
- VRR capture and 5.1 audio depend on AVerMedia's own software, so a custom app should assume fixed refresh and stereo. ([datasheet](https://storage.avermedia.com/web_release_www/datasheet/GC553Pro_Datasheet_v1_20250212.pdf))

### Suggested Xbox Series output settings for lowest latency

All **[Reasoning]** from the card limits above, unless linked.

- Resolution 1080p, refresh 60 Hz. This matches the uncompressed 1080p60 capture mode and your 1080p display.
- HDR off for a first version. HDR capture means P010 plus tone mapping or an HDR swap chain. The card does support 1080p60 HDR capture if you want it later.
- VRR off. VRR capture needs Streaming Center ([datasheet](https://storage.avermedia.com/web_release_www/datasheet/GC553Pro_Datasheet_v1_20250212.pdf)), and Microsoft itself notes devices between console and display can interfere with 120 Hz/VRR ([Xbox support](https://support.xbox.com/en-CA/help/hardware-network/display-sound/4k-gaming-at-120hz)).
- Audio: stereo uncompressed, since LPCM capture is stereo only.
- 120 Hz output is an experiment for later. A 30 fps game in a 120 Hz signal would deliver each frame over HDMI faster, but the card only captures 1080p120 as MJPEG. Whether that nets out lower is **[Unverified]**.

Important for frame generation: a 30 fps game still leaves the console in a 60 Hz signal, so the card delivers 60 frames per second where every second frame is a repeat. **[Unverified from a primary Xbox source; consistent with user reports](https://steamcommunity.com/app/993090/discussions/0/4338735089007356929)**. Any frame generator needs those repeats removed first. See section 2.

### AMD Radeon RX 9070

- RDNA 4 architecture, Radeon RX 9000 series ([GPUOpen Redstone article, 2025-12-10](https://gpuopen.com/learn/amd-fsr-redstone-developers-neural-rendering/)).
- **AFMF 2.1**: supported. AMD's 25.3.1 release notes list AFMF 2.1 support for RX 6000, 7000 and 9070 series ([AMD 25.3.1 release notes](https://www.amd.com/en/resources/support-articles/release-notes/RN-RAD-WIN-25-3-1.html), read via search extract; [CG Channel summary, 2025-03-08](https://cgchannel.com/2025/03/amd-launches-fsr-4-afmf-2-1-ris-2-0)).
- **FSR 4 / Redstone ML upscaling and ML frame generation**: supported in hardware (RX 9000 required), but only inside games that integrate it ([GPUOpen FSR Frame Generation](https://gpuopen.com/amd-fsr-framegeneration/), [GPUOpen Redstone article](https://gpuopen.com/learn/amd-fsr-redstone-developers-neural-rendering/)). Not usable on a capture window.
- **Radeon Image Sharpening 2**: supported, RX 9070 series only at launch, and it applies to desktop apps and video playback, not just games ([CG Channel, 2025-03-08](https://cgchannel.com/2025/03/amd-launches-fsr-4-afmf-2-1-ris-2-0), [AMD Adrenalin page](https://www.amd.com/adrenalin)). This is the one AMD driver feature that plainly targets video windows.
- **AMD video upscaling** (driver feature for video playback): launched for RX 7000 with Adrenalin 24.1.1 ([Tom's Hardware, 2024](https://www.tomshardware.com/pc-components/gpus/amd-video-upscaler-arrives-for-rx-7000-gpu-owners-yearning-for-nvidias-rtx-video)). RX 9070 support and whether it touches a custom capture app are **[Unverified]**. It is irrelevant at 1080p-in, 1080p-out anyway.
- **"AFMF 3"**: one third-party guide headline claims an AFMF 3 hidden in Adrenalin 26.10.2 ([framesgames](https://framesgames.com/amd-adrenalin-settings-guide/)). I could not find it in AMD release notes. **[Unverified]**. Check the current AMD release notes before relying on any AFMF version detail.

---

## 1. Existing solutions

None of the viewers below has built-in frame generation. Frame generation always comes from a second layer (Lossless Scaling or the GPU driver).

### OBS Studio (current setup)

- Link: <https://obsproject.com>. Licence GPL-2.0 ([repo](https://github.com/obsproject/obs-studio)). Open source.
- Launch parameters include `--portable`, `--multi`, `--profile`, `--collection`, `--scene`, `--minimize-to-tray` ([OBS knowledge base](https://obsproject.com/kb/launch-parameters)). There is **no** launch parameter for opening a projector.
- A separate portable copy with its own profile and `--multi` would isolate "viewer OBS" from "normal OBS". **[Reasoning]** from the documented flags.
- "Save projectors on exit" (Settings, General, Projectors) reopens a fullscreen projector at launch, per forum users, but there are repeated reports of it failing or giving a grey screen ([OBS forum](https://obsproject.com/forum/threads/save-projectors-on-exit-only-works-2-3rds-of-the-time.140019), [OBS forum](https://obsproject.com/forum/threads/obs-studio-portable-deployment-with-fullscreen-projector-give-a-gray-projector-preview-screen.144792/)).
- No native hotkey for a fullscreen projector; forum advice is the Advanced Scene Switcher plugin ([OBS forum](https://obsproject.com/forum/threads/hotkey-to-enter-exit-scene-fullscreen-projector.170855/)).
- Latency reputation: fine in your experience. One competing project claims OBS preview adds 100 to 400 ms ([vicash README](https://github.com/caaatto/vicash)), which is that author's claim and does not match your experience. **[Unverified]**
- As a base: libobs is reusable but heavy, and GPL.

### vicash

- Link: <https://github.com/caaatto/vicash>. MIT. Rust, wgpu (DX12 preferred), Media Foundation capture, NV12 zero-copy upload.
- Single exe, F11 borderless fullscreen, audio passthrough with a sync-delay slider, on-screen pipeline latency meter.
- Aimed at cheap MS2109/MS2130 cards and defaults to 720p60, so 1080p60 on the GC553Pro needs checking. No measured latency published.
- Small project (16 stars at time of reading). Good reference for a Rust build and closest to what you want.

### Viewfinder

- Link: <https://github.com/velitasali/viewfinder>. Licence not stated on the repo page **[Unverified]**.
- C++, Media Foundation `IMFSourceReader`, D3D11 video processor for colour conversion, Direct2D, WASAPI passthrough. About 135 KB binary.
- F11 or double-click fullscreen. Very new (4 commits, 0 stars at time of reading). Good reference for a minimal C++ build.

### Video Capture Card Viewer

- Link: <https://github.com/mayerwin/Video-Capture-Card-Viewer> ([site](https://mayerwin.github.io/Video-Capture-Card-Viewer/)). MIT.
- .NET 10, Avalonia UI, FlashCap capture, NAudio/WASAPI audio. F11 fullscreen, auto-detects the card, auto-reconnect.
- Renders through a `WriteableBitmap`, not a GPU swap chain, so it is unlikely to be the lowest-latency design. **[Reasoning]**. No latency figures published.

### MPV-SW-Capture and plain mpv

- MPV-SW-Capture: <https://github.com/TyRaS-SW/MPV-SW-Capture>. MIT for its own scripts; mpv itself not bundled. A preconfigured mpv setup for console play through a USB capture card, with a shader menu and WASAPI audio. Author claims lag "almost as low as connecting it directly to a TV" **[Unverified, author claim]**.
- mpv: <https://mpv.io>. GPL-2.0-or-later, optionally LGPL-2.1-or-later builds ([mpv repo](https://github.com/mpv-player/mpv)). Opens capture devices via `av://dshow:...`.
- mpv's manual says the `low-latency` profile applies options that reduce latency, and that `--untimed` outputs a captured frame immediately but "will likely break, unless the stream has no audio" ([mpv manual](https://mpv.io/manual/stable/)).
- mpv can load GLSL shaders such as FSR1/CAS ports and Anime4K ([Anime4K](https://github.com/bloc97/Anime4K)).
- An SVP forum user reported an AVerMedia GC573 in mpv and PotPlayer with "no added delay" without interpolation ([SVP forum, 2025-04](https://www.svp-team.com/forum/viewtopic.php?id=7552)).

### ffplay, VLC, PotPlayer

- ffplay (FFmpeg, LGPL/GPL): <https://ffmpeg.org/ffplay.html>. Opens DirectShow devices ([FFmpeg DirectShow wiki](https://trac.ffmpeg.org/wiki/DirectShow)). No UI. Useful as a quick test only.
- VLC (GPL-2.0): <https://www.videolan.org/vlc/>. Opens DirectShow capture devices. Latency reputation for live capture is poor because of default caching **[Unverified, general reputation]**.
- PotPlayer (freeware, closed source): <https://potplayer.daum.net/>. Reported working with a capture card without added delay in the SVP thread above. Not usable as a code base.

### Vendor apps

- AVerMedia Streaming Center: the vendor app for your card, Windows only ([product page](https://www.avermedia.com/product-detail/capture-card-GC553Pro)). It has a pop-out preview window that a reviewer played through without perceivable lag ([APH Networks](https://aphnetworks.com/reviews/avermedia-live-gamer-ultra-s-gc553pro/3)). Closed source. Worth a five-minute test since it is already built for this card.
- Elgato 4K Capture Utility / Elgato Studio: Elgato says Studio replaces 4K Capture Utility (via search extract of [Elgato help](https://help.elgato.com/hc/en-us/articles/23749168389517)). Built for Elgato hardware; support for a non-Elgato card is **[Unverified]** and unlikely. Closed source.
- Genki Arcade: free app for Genki ShadowCast hardware. One independent test measured about 207 ms with Arcade versus about 184 ms with OBS on that hardware ([ryvaeus review](https://ryvaeus.pages.dev/blog/genki-shadowcast-review)). Support for other cards is **[Unverified]**. Not a good candidate.

### Related tools (not viewers)

- Magpie: <https://github.com/Blinue/Magpie>. GPL-3.0. Open-source window scaler with FSR, Anime4K and CRT effects. No frame generation. Good open reference for how to apply effects to another app's window.
- libdshowcapture: <https://github.com/obsproject/libdshowcapture>. OBS's DirectShow capture library, a building block if you choose DirectShow.

---

## 2. Frame generation on a video feed

No motion vectors or depth exist in a capture feed. Only methods that estimate motion from the images themselves can work.

### The repeat-frame problem (applies to every option)

A 30 fps game inside a 60 Hz signal reaches the PC as 60 frames per second with every second frame repeated. A frame generator fed that stream sees no motion on half the frames and produces erratic output. A Lossless Scaling user doing exactly this fixed it by setting OBS's output to 30 fps for 30 fps games ([Steam thread, 2024-06-30](https://steamcommunity.com/app/993090/discussions/0/4338735089007356929)).

For a custom app this means: detect repeated frames and present only the unique ones. **[Reasoning]**

### Latency cost of interpolation in general

To show a frame between A and B, the generator must already have B. So the real frame B is delayed by roughly one output interval to one source interval. At 30 fps source, expect on the order of 17 to 33 ms plus processing. **[Reasoning]**

Published numbers for a game (not a capture feed): at a 40 fps base with 28.5 ms latency, LSFG 3 at 80 fps measured 41.7 ms, and LSFG 2 measured 54.3 ms ([Tom's Hardware on community benchmarks, 2025-01](https://www.tomshardware.com/video-games/pc-gaming/lossless-scaling-3-update-touts-greatly-improved-latency-and-performance-universal-frame-gen-tool-boasts-24-percent-reduced-latency)). No one has published measured end-to-end numbers for console to capture card to LSFG that I could find. **[Unverified]**

### Lossless Scaling (LSFG 3)

- Link: <https://store.steampowered.com/app/993090/Lossless_Scaling/>. Paid, closed source, a few dollars.
- LSFG 3 is its own machine-learning frame generation model, with fixed multipliers or an adaptive target frame rate ([Steam store page](https://store.steampowered.com/app/993090/Lossless_Scaling/)).
- Works on windowed and borderless windows only. Recommended GPU class includes Radeon RX 6000 series, so the RX 9070 is well above that ([Steam store page](https://store.steampowered.com/app/993090/Lossless_Scaling/)).
- It captures the target window (DXGI or WGC capture) and draws its own overlay, so it does not care what the window contains. Users report it working on VLC and browser video ([Steam discussion, 2024](https://steamcommunity.com/app/993090/discussions/0/4334231144975059487)) and on OBS projector feeds from consoles ([Steam thread](https://steamcommunity.com/app/993090/discussions/0/4338735089007356929)).
- Also bundles scalers: LS1, FSR, NIS, Anime4K, integer, xBR ([Steam store page](https://store.steampowered.com/app/993090/Lossless_Scaling/)).
- Capture adds its own step on top of the viewer, so latency is viewer latency plus LSFG latency. A press summary of the developer's notes says a queue target of 0 or 1 keeps latency lowest ([TweakTown, 2025](https://www.tweaktown.com/news/102558/lossless-scaling-3-with-fg-released-20x-perf-boost-turns-geforce-gt-1030-into-rtx-5090/index.html)).
- **Verdict: the most proven option for this exact use case.**

### AMD Fluid Motion Frames (AFMF 2.1)

- Link: <https://www.amd.com/en/products/software/adrenalin/afmf.html>. Free, in the driver.
- Supported on the RX 9070 (see section 0).
- Per AMD's page (read via search extract): works with DirectX 11, DirectX 12, Vulkan and OpenGL games; needs Adrenalin 25.3.1 or later; VSync off in driver and in the app; borderless fullscreen supported on RX 7000 series and newer.
- AFMF 2 lowered latency versus AFMF 1 according to AMD ([Tom's Hardware, 2024](https://www.tomshardware.com/pc-components/gpus/amd-fluid-motion-frames-2-lowers-latency-by-28-percent)).
- It can be enabled per application through Adrenalin profiles ([AFMF 2 preview release notes](https://www.amd.com/en/resources/support-articles/release-notes/RN-RAD-WIN-AFMF2-TECH-Preview.html)).
- **On video windows: [Unverified].** AMD markets it for games. The only documented video use I found is a 2023 hack with MPC-HC plus DXVK on a preview driver ([KitGuru, 2023](https://www.kitguru.net/gaming/joao-silva/amd-fluid-motion-frames-also-works-for-video/)).
- **[Reasoning]** AFMF hooks an app's swap chain. A custom Direct3D app that presents fullscreen-borderless with vsync off, at the true source frame rate, looks like a game to the driver. So your own app has a better chance of triggering AFMF than a generic media player. This needs a real test. The Adrenalin overlay shows whether AFMF is active.
- **Verdict: worth testing because it is free and avoids the extra window-capture step, but not guaranteed.**

### FSR 3 / FSR 4 frame generation

- Requires game integration. Inputs include camera vectors, motion vectors and swap chain access ([GPUOpen FSR Frame Generation, v4.0.2, 2026-10](https://gpuopen.com/amd-fsr-framegeneration/)).
- ML frame generation takes previous and current frames plus "known depth and motion vectors" and optical flow ([GPUOpen Redstone article](https://gpuopen.com/learn/amd-fsr-redstone-developers-neural-rendering/)).
- ML version: RX 9000 and newer. Analytical fallback: any Shader Model 6.2 GPU ([GPUOpen](https://gpuopen.com/amd-fsr-framegeneration/)).
- **Verdict: not usable on plain video as designed.**

### FidelityFX SDK components used standalone

- FidelityFX Optical Flow takes a single colour input and outputs block-based motion vectors (8x8 pixel blocks) plus a scene-change flag. It has its own create/dispatch/destroy API. It needs Shader Model 6.2 ([GPUOpen Optical Flow manual, v1.1.2](https://gpuopen.com/manuals/fidelityfx_sdk/techniques/optical-flow/)).
- FidelityFX Frame Interpolation takes two back buffers plus dilated depth, dilated motion vectors and optical flow output. Backends are DX12 and Vulkan ([GPUOpen Frame Interpolation manual, v1.1.3](https://gpuopen.com/manuals/fidelityfx_sdk/techniques/frame-interpolation/)).
- The FidelityFX SDK source is MIT licensed ([CG Channel, 2025](https://cgchannel.com/2025/03/amd-launches-fsr-4-afmf-2-1-ris-2-0); confirm in the [SDK repo](https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK)).
- **[Reasoning]** Optical Flow is the realistic standalone piece: feed it captured frames, then write your own warp/blend shader. Feeding Frame Interpolation with flat depth and zero motion vectors might also work, since AFMF is believed to be built on similar optical-flow-only ideas, but I found no documentation that this is supported. **[Unverified]**
- Note this forces Direct3D 12 or Vulkan for that part of the app.

### NVIDIA DLSS Frame Generation and Smooth Motion

- DLSS Frame Generation and DLSS 5 are GeForce RTX only and need game integration ([NVIDIA](https://www.nvidia.com/en-us/geforce/news/dlss-5-3d-guided-neural-rendering/)).
- Smooth Motion is NVIDIA's driver-level equivalent of AFMF. It launched on RTX 50 and came to RTX 40 in 2025 ([TweakTown, 2025](https://www.tweaktown.com/news/107138/nvidia-smooth-motion-now-officially-available-on-rtx-40-series-gpus/index.html), [Khronos news on Vulkan support](https://www.khronos.org/news/permalink/nvidia-adds-vulkan-support-to-smooth-motion-frame-generation-a-boost-for-many-games-and-emulators)). I could not fetch NVIDIA's own requirements page.
- **Verdict: none of it applies to an AMD GPU.**

### RIFE and other ML interpolators

- RIFE via ncnn/Vulkan runs on AMD, Intel and NVIDIA GPUs with no CUDA. MIT licence ([rife-ncnn-vulkan](https://github.com/nihui/rife-ncnn-vulkan)). That repo is a file-based tool, not a real-time pipeline.
- SVP's estimates for real-time RIFE: 1080p at 60 to 72 fps needs RTX 3080 or better on the generic (ncnn/Vulkan) path, or RTX 3060 or better on TensorRT. TensorRT is NVIDIA only and "up to 100% faster". The table says it "also works on AMD" but lists no AMD cards ([SVP wiki](https://www.svp-team.com/wiki/RIFE_AI_interpolation)).
- AMD reports are discouraging: one RX 6900 XT user reported 1 to 3 fps at 1080p, and another reported an AMD driver regression affecting ncnn/Vulkan ([SVP forum](https://www.svp-team.com/forum/viewtopic.php?id=7206)). These are single user reports.
- **RX 9070 at 1080p, 30 to 60 fps: [Unverified].** I found no benchmark. In raw power it is plausibly in the class SVP's table asks for, but AMD ncnn results have been inconsistent, so do not assume it works.
- DirectML: I found no maintained real-time RIFE pipeline on DirectML with published numbers. **[Unverified]**
- Latency is the bigger issue. A user running an AVerMedia capture card through mpv with SVP RIFE saw about 2000 ms of added delay and could not reduce it; without SVP the same setup had no added delay ([SVP forum, 2025-04](https://www.svp-team.com/forum/viewtopic.php?id=7552)). The video-player filter chain buffers frames.
- **Verdict: best image quality on paper, worst fit for low-latency play. Only viable if integrated directly with a one-frame buffer, and only if the RX 9070 can run the model in well under 16 ms per frame. Treat as an experiment.**

### SVP (SmoothVideo Project)

- Link: <https://www.svp-team.com>. Paid on Windows, closed source manager around open components. Price not checked. **[Unverified]**
- Designed for video file playback through players. Not designed for live low-latency input, as the thread above shows.
- **Verdict: not suitable.**

### Anything newer

- NVIDIA DLSS 5 (released 2026-09) is neural rendering for integrated games on RTX 50, not a video tool ([NVIDIA](https://www.nvidia.com/en-us/geforce/news/dlss-5-3d-guided-neural-rendering/)).
- A possible AFMF 3 is **[Unverified]** (see section 0).
- I found no new vendor-neutral, video-oriented real-time frame generator beyond Lossless Scaling.

---

## 3. Upscaling and image enhancement on AMD

### What upscaling can and cannot add here

- **1080p in, 1080p out: upscaling does nothing.** There are no missing pixels to fill. **[Reasoning]**
- **Sharpening can help.** Console games often render below 1080p internally and are scaled up by the console, which looks soft. A sharpen pass (RCAS/CAS) makes that look crisper but cannot restore detail. **[Reasoning]**
- **Capturing 4K and downscaling to 1080p** gives supersampling-style smoothing, but on this card uncompressed 4K is 30 fps only (NV12/P010), and 4K60 is MJPEG ([AVerMedia FAQ](https://www.avermedia.com/support/faq/3nKRpQbE)). It also multiplies USB and GPU load by four. For a 30 fps game it is technically possible; whether it looks better than 1080p plus sharpening is **[Unverified]** and I would not start there.
- **Upscaling only matters if** the window is displayed larger than the source, for example a 720p source, or a future 1440p/4K monitor.

### Options

- **FSR 1 (EASU upscale + RCAS sharpen)**: MIT licensed shader code ([FidelityFX-FSR repo](https://github.com/GPUOpen-Effects/FidelityFX-FSR)). Purely spatial, needs only the colour image, so it works on video. For 1080p to 1080p use RCAS only. **[Reasoning]** for the RCAS-only advice.
- **FSR 2/3/4 upscaling**: temporal, needs engine depth and motion vectors ([GPUOpen FSR upscaler docs](https://gpuopen.com/manuals/fsr_sdk/techniques/super-resolution-upscaler/)). Not usable on video.
- **Radeon Image Sharpening 2**: driver-level, RX 9070 series, explicitly covers desktop apps and video playback ([CG Channel, 2025-03-08](https://cgchannel.com/2025/03/amd-launches-fsr-4-afmf-2-1-ris-2-0)). Zero code. Whether it applies to a custom viewer exe is **[Unverified]** but likely, since it works desktop-wide.
- **AMD video upscaling**: driver feature for video playback, announced for RX 7000 ([Tom's Hardware, 2024](https://www.tomshardware.com/pc-components/gpus/amd-video-upscaler-arrives-for-rx-7000-gpu-owners-yearning-for-nvidias-rtx-video)). Tied to Windows video processing; not relevant at same-resolution output.
- **Anime4K**: MIT, real-time GLSL shaders, tuned for 1080p anime ([Anime4K repo](https://github.com/bloc97/Anime4K)). Wrong content type for GTA-style 3D games. **[Reasoning]**
- **NVIDIA RTX Video Super Resolution**: NVIDIA RTX only (RTX 20 series or newer per the [RTX Video SDK page](https://developer.nvidia.com/rtx-video-sdk/getting-started)). Not available on AMD.
- **Lossless Scaling's scalers** (LS1, FSR, NIS, Anime4K): available if you already use it for frame generation ([Steam store page](https://store.steampowered.com/app/993090/Lossless_Scaling/)).

**Recommendation:** treat "upscaling" as "optional sharpening". Start with driver RIS 2, or an RCAS pass in your own shader. Skip everything else until the display resolution changes.

---

## 4. Capture APIs on Windows

### Options

- **Media Foundation (`IMFSourceReader`)**: the modern Win32 capture API. Can deliver frames into Direct3D textures when given a DXGI device manager through `MF_SOURCE_READER_D3D_MANAGER` ([Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/medfound/mf-source-reader-d3d-manager)). `MF_LOW_LATENCY` exists for real-time use ([Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/medfound/mf-low-latency)). Used by both vicash and Viewfinder.
- **DirectShow**: older, still what OBS (via [libdshowcapture](https://github.com/obsproject/libdshowcapture)), mpv and ffmpeg use for capture devices ([FFmpeg DirectShow wiki](https://trac.ffmpeg.org/wiki/DirectShow)). Your current good-latency OBS setup is on this path. More boilerplate, COM graph building.
- **Windows.Media.Capture (`MediaCapture` + `MediaFrameReader`)**: WinRT wrapper over Media Foundation, easiest from C# ([Microsoft Learn](https://learn.microsoft.com/en-us/windows/uwp/audio-video-camera/process-media-frames-with-mediaframereader)). Slightly less control.
- **Vendor SDK**: none found for this card. **[Unverified]**

### Which is lowest latency

I found no primary source with a measured comparison between these APIs. **[Unverified]**

**[Reasoning]** For an uncompressed UVC stream all three end up in the same kernel driver, so the API matters less than these choices:

- Use an uncompressed format (NV12 or YUY2), not MJPEG.
- Take the newest frame and drop older ones. Never queue.
- Do the colour conversion on the GPU.
- Present immediately.

Viewfinder's author notes the hardware-accelerated Media Foundation path was unreliable for their capture source and fell back to software decode plus GPU colour conversion ([Viewfinder README](https://github.com/velitasali/viewfinder)). Expect some trial and error.

### Formats for this card

- **NV12 1080p60**: 12 bits per pixel, the native GPU video format, direct texture upload. First choice. **[Reasoning]**
- **YUY2 1080p60**: 4:2:2, slightly better colour detail, needs a small shader to convert. Good second choice.
- **RGB24 1080p60**: full colour, no conversion, 50% more data than YUY2. Worth a test for text sharpness.
- **MJPEG**: only for 1080p120/240 or 4K60. Avoid for the main path.
- **P010**: HDR only.

(Format list from the [AVerMedia FAQ](https://www.avermedia.com/support/faq/3nKRpQbE).)

### Audio capture and A/V sync

- The card's audio should appear as a normal Windows capture endpoint if it is UAC compliant. **[Unverified for this model]**
- Capture and render with WASAPI. Windows 10 and later allow shared-mode buffers under 10 ms through `IAudioClient3`, when the driver supports it; the default is 10 ms ([Microsoft Learn, low latency audio](https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/low-latency-audio)).
- **[Reasoning]** For a live viewer, do not sync video to an audio clock the way a media player does. Show video immediately, play audio with a small fixed buffer, and add a manual audio delay setting. If frame generation delays video by about 33 ms, delay audio by the same amount. vicash exposes exactly such a slider ([vicash](https://github.com/caaatto/vicash)).
- The capture clock and the sound card clock drift apart over hours. Plan for a tiny resample or an occasional sample drop. **[Reasoning]**
- AVerMedia has a FAQ on preview audio/video desync on Windows (seen in search results on [avermedia.com](https://www.avermedia.com/product-detail/capture-card-GC553Pro); not read in full).

### Low-latency presentation

- Use a flip-model swap chain (`DXGI_SWAP_EFFECT_FLIP_DISCARD`). Microsoft says a flip-model borderless window is "effectively equivalent or better" than exclusive fullscreen, and can bypass the desktop compositor (Independent Flip) when the window covers the screen ([Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/for-best-performance--use-dxgi-flip-model)).
- Add `DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT` and wait on it before rendering. This gets down to one frame of queue latency in Independent Flip ([Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/for-best-performance--use-dxgi-flip-model), [how-to](https://learn.microsoft.com/en-us/windows/uwp/gaming/reduce-latency-with-dxgi-1-3-swap-chains)).
- For the lowest latency, `DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING` plus `Present(0, DXGI_PRESENT_ALLOW_TEARING)`. This is also what enables VRR in a borderless window ([Microsoft Learn, VRR](https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/variable-refresh-rate-displays)).
- On a FreeSync monitor, tearing-allowed presents let the display follow the capture cadence instead of beating against it. On a fixed 60 Hz monitor you choose between tearing (lowest latency) and vsync (up to a frame of extra delay, plus occasional repeats or drops because the console clock and monitor clock differ). **[Reasoning]**
- "One-step fullscreen" is just resizing a borderless window to the monitor bounds on a hotkey. No mode switch needed. **[Reasoning]**
- Use [PresentMon](https://github.com/GameTechDev/PresentMon) to confirm Independent Flip is engaged (recommended by the Microsoft article above).
- Note the VRR doc's warning: XAML-driven render events can throttle presents ([Microsoft Learn, VRR](https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/variable-refresh-rate-displays)). This matters for the WinUI option below.

---

## 5. App stack options

### C++ with Win32 and Direct3D 11 (recommended)

- Direct access to Media Foundation, WASAPI, DXGI flags. Every Microsoft sample for these is C++.
- Viewfinder proves the whole thing fits in a roughly 135 KB binary ([Viewfinder](https://github.com/velitasali/viewfinder)).
- FidelityFX SDK and ncnn are C++ libraries, so later experiments plug in directly. FidelityFX needs D3D12 or Vulkan ([GPUOpen](https://gpuopen.com/manuals/fidelityfx_sdk/techniques/frame-interpolation/)), so either start on D3D12 or keep the renderer small enough to port.
- UI: a few hotkeys plus a small overlay. Dear ImGui (MIT, <https://github.com/ocornut/imgui>) is the usual choice for an in-swap-chain settings panel.
- Downside: most manual work, COM boilerplate.

### Rust with windows-rs or wgpu

- vicash is a working MIT reference: wgpu on DX12 plus Media Foundation ([vicash](https://github.com/caaatto/vicash)).
- windows-rs gives the same raw API access as C++ ([windows-rs](https://github.com/microsoft/windows-rs)).
- wgpu hides swap chain details. Whether it exposes the waitable object and tearing flags the way you want is **[Unverified]**; raw D3D via windows-rs avoids the question.
- Calling FidelityFX or ncnn needs bindings.

### C# with WinUI 3 or WPF plus a swap chain

- Nicest UI tooling. `MediaCapture` is easy from C#.
- WinUI 3 hosts a swap chain through `SwapChainPanel`, which is composition-based. XAML timing can throttle presents ([Microsoft Learn, VRR](https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/variable-refresh-rate-displays)). Whether a `SwapChainPanel` can reach Independent Flip with tearing is **[Unverified]**.
- WPF's `D3DImage` is Direct3D 9 based and composited, which is a poor fit for low latency. **[Reasoning]**
- Workable pattern: C# for a small settings window, and a plain Win32 window (HWND swap chain) for the video. That keeps the fast path clean.
- A managed runtime's garbage collector pauses are a small risk for frame pacing. **[Reasoning]**

### Building on libmpv

- Gets you a working player, shaders (FSR/CAS/Anime4K ports), audio, and device input quickly. Licence GPL-2.0+ or LGPL-2.1+ depending on build ([mpv repo](https://github.com/mpv-player/mpv)).
- mpv is a player with a timing model built around audio sync. Its own manual says the low-latency options are best effort and `--untimed` tends to break with audio ([mpv manual](https://mpv.io/manual/stable/)).
- Fastest route to "something that works"; weakest control over the last frame of latency. MPV-SW-Capture is this approach already packaged ([repo](https://github.com/TyRaS-SW/MPV-SW-Capture)).

### Building on libobs

- Proven capture and rendering with the latency you already like. GPL-2.0 ([repo](https://github.com/obsproject/obs-studio)).
- Large dependency and build system for a viewer. You would be carrying most of OBS to avoid OBS. **[Reasoning]**

### Recommendation

C++, Win32 window, Direct3D 11 (or 12 if you want FidelityFX later), Media Foundation source reader, WASAPI, Dear ImGui overlay.

If you prefer Rust, fork or study vicash instead; it is the closest existing code to the goal and MIT licensed.

---

## 6. Recommended approach

### Try this first (no code)

Do these in order. Stop when it is good enough.

1. **Update card firmware** with AVerMedia's tool ([product page](https://www.avermedia.com/product-detail/capture-card-GC553Pro)). Set the Xbox to 1080p, 60 Hz, HDR off, VRR off, stereo audio (section 0).
2. **Pick a light viewer** and confirm it shows 1080p60 uncompressed with audio and one-key fullscreen:
   - [vicash](https://github.com/caaatto/vicash) (F11, latency meter, audio delay slider), or
   - [Viewfinder](https://github.com/velitasali/viewfinder) (F11), or
   - [MPV-SW-Capture](https://github.com/TyRaS-SW/MPV-SW-Capture) / plain mpv with the `low-latency` profile.
   - AVerMedia Streaming Center's pop-out preview is a useful baseline.
3. **Make the viewer show 30 unique frames per second for 30 fps games.** Select a 30 fps capture mode if the viewer offers one. If the viewer cannot do this, frame generation will misbehave (section 2). This may be the single reason a custom app is needed.
4. **Add Lossless Scaling** on the viewer window: LSFG 3, 2x, queue target 0 or 1, scaling off or sharpen only ([Steam](https://store.steampowered.com/app/993090/Lossless_Scaling/)).
5. **Separately test AFMF 2.1**: add the viewer exe as a game in AMD Software, enable Fluid Motion Frames, run borderless fullscreen with vsync off, and check the Adrenalin overlay for AFMF status. Result is **[Unverified]** until you try.
6. **Try Radeon Image Sharpening 2** in AMD Software for a free sharpen pass.
7. **Fallback that keeps OBS:** a portable OBS copy launched with `--portable --multi --profile ... --collection ...` ([OBS KB](https://obsproject.com/kb/launch-parameters)), output fps set to the game's fps, "Save projectors on exit" on, plus Lossless Scaling. This is the setup other console users already run ([Steam thread](https://steamcommunity.com/app/993090/discussions/0/4338735089007356929)).

Judge by feel in an actual game. For numbers, film the passthrough display and the PC monitor together with a phone in slow motion. **[Reasoning]**

### If you build the app

Phase 1, the viewer:

- Media Foundation source reader, 1080p60 NV12 (then test YUY2 and RGB24).
- Newest-frame-wins, no queue.
- GPU colour conversion, optional RCAS sharpen shader ([FSR 1 source, MIT](https://github.com/GPUOpen-Effects/FidelityFX-FSR)).
- Flip-discard swap chain, waitable object, tearing allowed, borderless fullscreen on one key (section 4).
- WASAPI passthrough with an adjustable audio delay.
- Auto-select the AVerMedia device, auto-reconnect on signal loss, remember settings.

Phase 2, make it frame-generation friendly:

- Detect repeated frames and present only unique ones, so a 30 fps game is presented at 30 fps.
- Then let Lossless Scaling or AFMF do the generation on top. Add an audio delay preset to match.

Phase 3, optional experiments:

- FidelityFX Optical Flow plus your own warp shader for in-app interpolation ([manual](https://gpuopen.com/manuals/fidelityfx_sdk/techniques/optical-flow/)). Removes the extra window-capture hop that Lossless Scaling needs.
- RIFE through ncnn/Vulkan with a strict one-frame buffer, only if a benchmark on the RX 9070 shows well under 16 ms per 1080p frame ([rife-ncnn-vulkan](https://github.com/nihui/rife-ncnn-vulkan)).

### Why not go straight to in-app frame generation

- The quality bar is set by LSFG 3 and AFMF, which are mature and cost nothing or a few dollars.
- The viewer is needed either way.
- Phase 2 gives nearly the same result as phase 3 for a fraction of the effort. **[Reasoning]**

---

## Links for later

Hardware

- AVerMedia GC553Pro product page: <https://www.avermedia.com/product-detail/capture-card-GC553Pro>
- GC553Pro datasheet (2025-02-12): <https://storage.avermedia.com/web_release_www/datasheet/GC553Pro_Datasheet_v1_20250212.pdf>
- GC553Pro user guide (not machine-readable for me; read the mode tables): <https://storage.avermedia.com/web_release_www/user_manual/AVerMedia_GC553Pro_User_Guide_v1.pdf>
- GC553Pro supported modes FAQ: <https://www.avermedia.com/support/faq/3nKRpQbE>
- APH Networks GC553Pro review: <https://aphnetworks.com/reviews/avermedia-live-gamer-ultra-s-gc553pro/3>
- Xbox 120 Hz setup notes: <https://support.xbox.com/en-CA/help/hardware-network/display-sound/4k-gaming-at-120hz>

AMD

- AFMF page: <https://www.amd.com/en/products/software/adrenalin/afmf.html>
- Adrenalin 25.3.1 release notes (AFMF 2.1, RIS 2, RX 9070): <https://www.amd.com/en/resources/support-articles/release-notes/RN-RAD-WIN-25-3-1.html>
- AFMF 2 preview release notes: <https://www.amd.com/en/resources/support-articles/release-notes/RN-RAD-WIN-AFMF2-TECH-Preview.html>
- Adrenalin 24.9.1 release notes (AFMF 2): <https://www.amd.com/en/resources/support-articles/release-notes/RN-RAD-WIN-24-9-1.html>
- AMD Adrenalin overview: <https://www.amd.com/adrenalin>
- CG Channel on FSR 4, AFMF 2.1, RIS 2 (2025-03-08): <https://cgchannel.com/2025/03/amd-launches-fsr-4-afmf-2-1-ris-2-0>
- GPUOpen FSR Frame Generation: <https://gpuopen.com/amd-fsr-framegeneration/>
- GPUOpen FSR Redstone for developers (2025-12-10): <https://gpuopen.com/learn/amd-fsr-redstone-developers-neural-rendering/>
- FidelityFX Frame Interpolation manual: <https://gpuopen.com/manuals/fidelityfx_sdk/techniques/frame-interpolation/>
- FidelityFX Optical Flow manual: <https://gpuopen.com/manuals/fidelityfx_sdk/techniques/optical-flow/>
- FSR upscaler docs: <https://gpuopen.com/manuals/fsr_sdk/techniques/super-resolution-upscaler/>
- FidelityFX SDK repo: <https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK>
- FSR 1 source (MIT): <https://github.com/GPUOpen-Effects/FidelityFX-FSR>
- AFMF on video hack (2023): <https://www.kitguru.net/gaming/joao-silva/amd-fluid-motion-frames-also-works-for-video/>
- AFMF 2 latency coverage: <https://www.tomshardware.com/pc-components/gpus/amd-fluid-motion-frames-2-lowers-latency-by-28-percent>
- AMD video upscaling coverage: <https://www.tomshardware.com/pc-components/gpus/amd-video-upscaler-arrives-for-rx-7000-gpu-owners-yearning-for-nvidias-rtx-video>

NVIDIA

- DLSS 5 launch (2026-09-01): <https://www.nvidia.com/en-us/geforce/news/dlss-5-3d-guided-neural-rendering/>
- DLSS 5 announcement (2026-03-16): <https://www.nvidia.com/en-us/geforce/news/dlss5-breakthrough-in-visual-fidelity-for-games/>
- RTX Video SDK: <https://developer.nvidia.com/rtx-video-sdk/getting-started>
- Smooth Motion on RTX 40 coverage: <https://www.tweaktown.com/news/107138/nvidia-smooth-motion-now-officially-available-on-rtx-40-series-gpus/index.html>

Frame generation and scaling tools

- Lossless Scaling on Steam: <https://store.steampowered.com/app/993090/Lossless_Scaling/>
- Lossless Scaling LSFG 3 page (blocked for me): <https://losslessscaling.com/lsfg-3/>
- Steam thread, console via capture card: <https://steamcommunity.com/app/993090/discussions/0/4338735089007356929>
- Steam thread, video players: <https://steamcommunity.com/app/993090/discussions/0/4334231144975059487>
- LSFG 3 latency coverage: <https://www.tomshardware.com/video-games/pc-gaming/lossless-scaling-3-update-touts-greatly-improved-latency-and-performance-universal-frame-gen-tool-boasts-24-percent-reduced-latency>
- rife-ncnn-vulkan: <https://github.com/nihui/rife-ncnn-vulkan>
- SVP RIFE wiki: <https://www.svp-team.com/wiki/RIFE_AI_interpolation>
- SVP forum, RIFE with a capture card: <https://www.svp-team.com/forum/viewtopic.php?id=7552>
- SVP forum, RIFE performance on AMD: <https://www.svp-team.com/forum/viewtopic.php?id=7206>
- vs-mlrt: <https://github.com/AmusementClub/vs-mlrt>
- Magpie: <https://github.com/Blinue/Magpie>
- Anime4K: <https://github.com/bloc97/Anime4K>

Viewers and reference code

- vicash: <https://github.com/caaatto/vicash>
- Viewfinder: <https://github.com/velitasali/viewfinder>
- Video Capture Card Viewer: <https://github.com/mayerwin/Video-Capture-Card-Viewer>
- MPV-SW-Capture: <https://github.com/TyRaS-SW/MPV-SW-Capture>
- mpv manual: <https://mpv.io/manual/stable/>
- mpv repo: <https://github.com/mpv-player/mpv>
- FFmpeg DirectShow wiki: <https://trac.ffmpeg.org/wiki/DirectShow>
- OBS launch parameters: <https://obsproject.com/kb/launch-parameters>
- OBS repo: <https://github.com/obsproject/obs-studio>
- libdshowcapture: <https://github.com/obsproject/libdshowcapture>
- Dear ImGui: <https://github.com/ocornut/imgui>
- windows-rs: <https://github.com/microsoft/windows-rs>
- PresentMon: <https://github.com/GameTechDev/PresentMon>

Microsoft docs

- Flip model guidance: <https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/for-best-performance--use-dxgi-flip-model>
- Waitable swap chains: <https://learn.microsoft.com/en-us/windows/uwp/gaming/reduce-latency-with-dxgi-1-3-swap-chains>
- Tearing and VRR: <https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/variable-refresh-rate-displays>
- Source reader attributes: <https://learn.microsoft.com/en-us/windows/win32/medfound/source-reader-attributes>
- `MF_SOURCE_READER_D3D_MANAGER`: <https://learn.microsoft.com/en-us/windows/win32/medfound/mf-source-reader-d3d-manager>
- `MF_LOW_LATENCY`: <https://learn.microsoft.com/en-us/windows/win32/medfound/mf-low-latency>
- MediaFrameReader: <https://learn.microsoft.com/en-us/windows/uwp/audio-video-camera/process-media-frames-with-mediaframereader>
- Low latency audio: <https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/low-latency-audio>
- `SendInput` (UIPI note): <https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-sendinput>

Lossless Scaling automation

- Steam news feed for app 993090 (official announcements, machine-readable): <https://api.steampowered.com/ISteamNews/GetNewsForApp/v2/?appid=993090&count=40&maxlength=0>
- Official news page (blocked for me): <https://losslessscaling.com/news/>
- Official 1.2.8 post, command-line shortcuts (blocked for me, read through the Steam feed): <https://losslessscaling.com/1-2-8-released/>
- Official 2.0 post, profiles and auto scale (blocked for me, read through the Steam feed): <https://losslessscaling.com/2-0-released/>
- Community docs site linked to the official Discord, settings reference: <https://sageinfinity.github.io/docs/Guides/LS%20settings%20Info>
- Community docs, best settings: <https://sageinfinity.github.io/docs/Guides/bestsettings>
- Community docs, auto launch to tray: <https://sageinfinity.github.io/docs/Guides/autolaunch>
- Community docs, getting started: <https://sageinfinity.github.io/docs/Guides/start>
- Community docs, VRR: <https://sageinfinity.github.io/docs/Guides/vrr>
- Community docs, DXGI vs WGC: <https://sageinfinity.github.io/docs/FAQ/dxgiwgc>
- Community docs, MPO: <https://sageinfinity.github.io/docs/FAQ/mpo>
- Community docs, settings file location: <https://sageinfinity.github.io/docs/FAQ/settings>
- Community docs, config.ini parameters: <https://sageinfinity.github.io/docs/FAQ/config>
- Steam thread, developer gives Settings.xml path: <https://steamcommunity.com/app/993090/discussions/0/830448456536434419>
- Steam thread, how to set up profiles and filters: <https://steamcommunity.com/app/993090/discussions/0/598522105087160797/>
- Steam thread, what the profile filter field is: <https://steamcommunity.com/app/993090/discussions/0/4429939323288710347>
- Steam thread, auto scale not triggering: <https://steamcommunity.com/app/993090/discussions/0/6197594259714608604>
- Steam thread, `-StartMinimized` and Task Scheduler: <https://steamcommunity.com/app/993090/discussions/0/4418676069795443929>
- Steam thread, duplicate frames in emulators: <https://steamcommunity.com/app/993090/discussions/0/4418677017728092474>
- Steam thread, Lossless Scaling vs AFMF: <https://steamcommunity.com/app/993090/discussions/0/4418675885152347170/>
- Corsair explainer (default hotkey): <https://www.corsair.com/explorer/gamer/gaming-pcs/lossless-scaling-everything-you-need-to-know/>
- Valve Steam browser protocol wiki (blocked for me): <https://developer.valvesoftware.com/wiki/Steam_browser_protocol>

---

## Open questions

Card and GPU model are now known. What remains needs a hands-on check or a source I could not reach.

Capture card

- Is the GC553Pro pure UVC/UAC with the inbox Windows driver? Check Device Manager.
- Which modes does it actually enumerate to Media Foundation and DirectShow? In particular, does it offer a true 1080p30 mode, and are NV12, YUY2 and RGB24 all listed at 1080p60?
- What is its real capture latency in milliseconds at 1080p60 uncompressed? No published figure found.
- Does a 30 fps Xbox game arrive as clean alternating repeats in a 60 Hz signal, as assumed?
- Does 1080p120 MJPEG end up faster or slower end-to-end than 1080p60 uncompressed?

GPU and driver

- Does AFMF 2.1 engage on a non-game D3D11/D3D12 viewer window on the RX 9070? No source either way.
- Does Radeon Image Sharpening 2 apply to a custom viewer exe?
- Is there an AFMF 3 in current Adrenalin drivers, and does it change any requirement? Only an unconfirmed third-party mention found.
- Current AFMF fullscreen and vsync rules. My details come from search extracts of AMD pages that timed out on direct fetch.
- Can RIFE (ncnn/Vulkan or DirectML) run a 1080p frame in under 16 ms on the RX 9070? No benchmark found.
- Can FidelityFX Frame Interpolation run with dummy depth and motion vectors, or is Optical Flow plus a custom warp the only standalone route?

Display and setup

- Does the monitor support FreeSync, and what is its refresh rate? This decides tearing versus vsync, and whether 30 to 60 or a higher multiplier makes sense.
- Is the Xbox also connected to a second display through the card's passthrough? That affects which Xbox output settings are acceptable.

Software

- End-to-end latency of console to card to viewer to Lossless Scaling. Only anecdotes exist.
- Elgato Studio and Genki Arcade with a non-vendor card. Not confirmed, probably unsupported.
- Viewfinder's licence. Not stated on its repo page.
- Whether wgpu or WinUI 3 `SwapChainPanel` can reach Independent Flip with tearing and a waitable swap chain.
- SVP pricing and current licence terms.

---

## Lossless Scaling automation

Research date: 2026-11-14. Goal: Lossless Scaling (LS) should need no manual step per session.

Source quality used below:

- **Official**: the developer's Steam announcements, read through the public Steam news feed for app 993090 ([feed](https://api.steampowered.com/ISteamNews/GetNewsForApp/v2/?appid=993090&count=40&maxlength=0)), and the [Steam store page](https://store.steampowered.com/app/993090/Lossless_Scaling/). losslessscaling.com returned HTTP 403 on direct fetch.
- **Community docs**: <https://sageinfinity.github.io/docs/Guides/start>, a guide site that links the official Discord, Reddit and Steam hub. The site does not state that it is official, so treat it as community documentation.
- **User reports**: Steam discussion posts. Only one post cited here carries a developer tag (the Settings.xml path).

### 1. Profiles and auto scale

- LS has per-game profiles. The store page says Game Profiles apply automatically when the game executable is detected ([Steam store page](https://store.steampowered.com/app/993090/Lossless_Scaling/)).
- Official 2.0 release notes (2021-12-01): "you can still use the hotkey or scale button - game profile will be applied automatically", or "use the auto scale option to let the game window to be scaled automatically whenever it is activated" ([Steam feed](https://api.steampowered.com/ISteamNews/GetNewsForApp/v2/?appid=993090&count=40&maxlength=0)).
- So auto scale triggers when the matching window becomes the active (foreground) window. **[Reasoning]** from the "whenever it is activated" wording.
- Configuration, per a user answer marked as the solution (2025-03-24): edit the profile, fill the filter field with the full exe path, or the exe name as shown in Task Manager, or the window title; separate several entries with a semicolon; then enable "auto scale" on that profile. Without auto scale enabled, the filter still makes LS switch to that profile when you scale by hotkey ([Steam thread](https://steamcommunity.com/app/993090/discussions/0/598522105087160797/)).
- Other users confirm the filter field accepts an exe or a window name and has a tooltip explaining it ([Steam thread, 2024-08-14](https://steamcommunity.com/app/993090/discussions/0/4429939323288710347)).
- Delay: a user describes "increasing the delay by several seconds" while troubleshooting auto scale, so a delay setting exists ([Steam thread, 2024-05-20](https://steamcommunity.com/app/993090/discussions/0/6197594259714608604)). Its exact name, location, range and default are **[Unverified]**; I found no official text describing it.
- Known failure: auto scale did not trigger for a game with anti-cheat, and worked for other games ([same thread](https://steamcommunity.com/app/993090/discussions/0/6197594259714608604)). Not relevant to a self-written viewer. **[Reasoning]**
- The manual Scale button uses a 5 second countdown in which you select the target window ([community docs](https://sageinfinity.github.io/docs/Guides/start)). Auto scale avoids that.

What this means for the viewer **[Reasoning]**:

- Give the viewer a fixed exe name and a fixed window title, so one profile filter always matches.
- Have the viewer bring itself to the foreground once it is showing video, since activation is the trigger.
- Whether LS re-scales correctly if the viewer changes size (windowed to borderless) after auto scale has fired is **[Unverified]**. Safer: go borderless fullscreen first, then take focus, and use the delay setting as margin.

### 2. Controlling it from another program

Command line:

- Official 1.2.8 release notes (2019-07-01) document shortcut arguments: `-path "<absolute exe path>"` (the only required one), `-args ""`, `-auto`, `-scaleFactor`, `-alignment`, `-forceResolution`, `-antiAliasing`, `-multiDisplay`, `-clipCursor`, `-cursorSpeed`, `-hideCursor`. They start a game and scale it without opening the app UI, and only work for games started directly from their exe ([Steam feed](https://api.steampowered.com/ISteamNews/GetNewsForApp/v2/?appid=993090&count=40&maxlength=0); original post at <https://losslessscaling.com/1-2-8-released/>, blocked for me).
- Whether those 2019 arguments still work in the current 3.x app, and whether any argument selects a profile or enables LSFG, is **[Unverified]**. None of the listed arguments concerns frame generation.
- `-StartMinimized` works, per several users from 2024 to 2025 ([Steam thread](https://steamcommunity.com/app/993090/discussions/0/4418676069795443929)) and the community docs ([auto launch guide](https://sageinfinity.github.io/docs/Guides/autolaunch)). It is not in any official text I found. A developer-posted stack trace shows the main window constructor takes a `startMinimized` boolean, which is consistent with it ([Steam thread](https://steamcommunity.com/app/993090/discussions/0/830448456536434419)).
- I found no documented command, IPC or API to start or stop scaling in an already running LS instance. **[Unverified that none exists]**

Start with Windows and tray:

- LS has the settings "Start minimized at Windows startup", "Minimize to tray" and "Start as Administrator" ([community docs](https://sageinfinity.github.io/docs/Guides/autolaunch)). "Option to minimize to tray" is in the official 2.0 notes ([Steam feed](https://api.steampowered.com/ISteamNews/GetNewsForApp/v2/?appid=993090&count=40&maxlength=0)).
- Combining "Start as Administrator" with "Start minimized at Windows startup" raises a UAC prompt at every logon. The community fix is a Task Scheduler task: trigger "At log on", "Run with highest privileges", action `LosslessScaling.exe -StartMinimized`, and the in-app startup option turned off ([community docs](https://sageinfinity.github.io/docs/Guides/autolaunch)). One user adds a startup delay of about 30 seconds to the task ([Steam thread](https://steamcommunity.com/app/993090/discussions/0/4418676069795443929)).
- LS runs with administrator rights by default since 1.4.0, to set GPU scheduling priority ([Steam feed, 2021-08-01 post](https://api.steampowered.com/ISteamNews/GetNewsForApp/v2/?appid=993090&count=40&maxlength=0)). The community docs recommend running it as admin ([getting started](https://sageinfinity.github.io/docs/Guides/start)).
- Typical install path: `C:\Program Files (x86)\Steam\steamapps\common\Lossless Scaling\LosslessScaling.exe` ([community docs](https://sageinfinity.github.io/docs/Guides/autolaunch)). Check the real path on this machine.

Hotkey:

- Default scale toggle is Ctrl+Alt+S, changeable in settings, per a third-party explainer ([Corsair](https://www.corsair.com/explorer/gamer/gaming-pcs/lossless-scaling-everything-you-need-to-know/)). Not confirmed from an official text. **[Unverified from a primary source]**
- Whether a synthetic key press from another program triggers it is **[Unverified]**. I found no report either way, and it depends on how LS registers the hotkey.
- If LS runs elevated, a non-elevated program cannot inject input into it: `SendInput` "is subject to UIPI. Applications are permitted to inject input only into applications that are at an equal or lesser integrity level" ([Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-sendinput)). How that applies to a global hotkey is **[Unverified]**. Auto scale avoids the question.

Steam launch URL:

- The app id is 993090 ([Steam store page](https://store.steampowered.com/app/993090/Lossless_Scaling/)).
- `steam://rungameid/993090` is the standard Steam protocol form, documented on the Valve developer wiki ([link](https://developer.valvesoftware.com/wiki/Steam_browser_protocol)), which returned HTTP 403 for me. **[Unverified by direct read]**
- Whether LS needs Steam running, or can be started directly from its exe with Steam closed, is **[Unverified]**. The Task Scheduler method above starts the exe directly and users report it works.

Settings file:

- Path, given by a developer-tagged account (2024-12-15): `C:\Users\<user>\AppData\Local\Lossless Scaling\Settings.xml` ([Steam thread](https://steamcommunity.com/app/993090/discussions/0/830448456536434419)). The community docs say settings and profiles are both in that file ([FAQ](https://sageinfinity.github.io/docs/FAQ/settings)).
- Format: XML, loaded through .NET XML serialization, per the stack trace in the same thread ([Steam thread](https://steamcommunity.com/app/993090/discussions/0/830448456536434419)). The element names and schema are not published. **[Unverified]** Read the real file before relying on any field name.
- The file can become corrupted at boot and be reset; users keep a manual backup copy ([same thread](https://steamcommunity.com/app/993090/discussions/0/830448456536434419)). For a hands-off setup, keep a backup of a known-good Settings.xml. **[Reasoning]**
- There is also a `config.ini` for advanced values (flush, capture buffer sizes, `base_framerate_threshold = 10`, `flow_scale`, FPS counter layout). The official notes mention config.ini ([Steam feed, 2024-08-11 post](https://api.steampowered.com/ISteamNews/GetNewsForApp/v2/?appid=993090&count=40&maxlength=0)); parameters are listed in the community docs, which warn against changing them ([FAQ](https://sageinfinity.github.io/docs/FAQ/config)). Its location is **[Unverified]**.

### 3. What the target window needs

Window mode:

- "Only windowed and borderless fullscreen modes are supported." Exclusive fullscreen may work only if the LS output is set to a second display ([Steam store page](https://store.steampowered.com/app/993090/Lossless_Scaling/)).
- The viewer plan (borderless window sized to the monitor) fits. Never use exclusive fullscreen.

Capture API:

- LS offers DXGI (Desktop Duplication) and WGC (Windows Graphics Capture). GDI was removed, and WGC is only available on Windows 11 24H2 or later; on older versions it falls back to DXGI ([Steam feed, 2025-03-08 post](https://api.steampowered.com/ISteamNews/GetNewsForApp/v2/?appid=993090&count=40&maxlength=0)).
- Official 2024-11-16 post: on 24H2, DXGI depends on MPO support, because without MPO "DXGI cannot reliably distinguish between updates from game window and the LS window"; "DXGI and WGC have switched roles in this update"; MPO support can be checked with dxdiag; disconnecting one of two monitors "may force the remaining monitor to support MPO" ([Steam feed](https://api.steampowered.com/ISteamNews/GetNewsForApp/v2/?appid=993090&count=40&maxlength=0)).
- Official 2025-01-10 post: DXGI works again on 24H2 after update KB5046617 ([Steam feed](https://api.steampowered.com/ISteamNews/GetNewsForApp/v2/?appid=993090&count=40&maxlength=0)).
- Community docs: WGC is the recommended default, with better overlay and MPO handling and slightly more overhead; DXGI is "sometimes slightly faster", better for lowest latency or weak PCs ([settings reference](https://sageinfinity.github.io/docs/Guides/LS%20settings%20Info), [best settings](https://sageinfinity.github.io/docs/Guides/bestsettings), [DXGI vs WGC](https://sageinfinity.github.io/docs/FAQ/dxgiwgc)).
- Measured latency difference between DXGI and WGC: none published that I found. **[Unverified]** Test both on this machine with the LS "Draw FPS" counter on.
- This PC is on Windows 11 build 26200, which is newer than 24H2, so both APIs should be offered. **[Reasoning]**

Flip model, tearing, VRR:

- I found no source on LS problems specific to a flip-model or `ALLOW_TEARING` source window. **[Unverified]**
- **[Reasoning]** While LS is scaling, the LS output window covers the screen and is what gets presented to the display. The viewer window is captured underneath, so the viewer's own tearing or VRR presentation does not reach the monitor, and the viewer is unlikely to stay in Independent Flip. Tearing and VRR then become LS settings (Sync Mode, G-Sync Support), not viewer settings.
- LS Sync Mode options: "Off (Allow tearing)" for lowest latency, "Default" (no tearing, not V-Sync), and V-Sync variants ([community docs](https://sageinfinity.github.io/docs/Guides/LS%20settings%20Info)). Officially, "Default" means both vsync and tearing are disabled ([Steam feed, 2024-07-18 post](https://api.steampowered.com/ISteamNews/GetNewsForApp/v2/?appid=993090&count=40&maxlength=0)).
- VRR: LS added "Improved Freesync support (works only on AMD GPUs)" on 2024-07-18 and initial G-Sync support on 2024-08-11 ([Steam feed](https://api.steampowered.com/ISteamNews/GetNewsForApp/v2/?appid=993090&count=40&maxlength=0)).
- Official caveat: with WGC, "in games where a hardware cursor is displayed and VRR is enabled, WGC disables independent flip mode globally", which "can lead to issues with LS and VRR functionality" ([Steam feed, 2024-11-16 post](https://api.steampowered.com/ISteamNews/GetNewsForApp/v2/?appid=993090&count=40&maxlength=0)). A viewer that hides the cursor should avoid this. **[Reasoning]**
- Community VRR guide: cap the final frame rate a few fps under the refresh rate; with LS tearing mode on a 120 Hz display it suggests 58 x 2 ([VRR guide](https://sageinfinity.github.io/docs/Guides/vrr)). A console feed cannot be capped below its own 30 or 60 fps, so on a 60 Hz display 30 x 2 lands exactly on the refresh rate. **[Reasoning]**
- A user watching video reported an unstable LS counter with VRR on in one setup and a stable one with VRR on in another ([Steam thread](https://steamcommunity.com/app/993090/discussions/0/4334231144975059487)). Anecdotal.

Video and capture windows:

- LS frame generation works on local video players (VLC, mpv) by the same procedure as games ([community docs](https://sageinfinity.github.io/docs/Guides/start), [Steam thread](https://steamcommunity.com/app/993090/discussions/0/4334231144975059487)), and on an OBS fullscreen projector showing a console through a capture card ([Steam thread](https://steamcommunity.com/app/993090/discussions/0/4338735089007356929)).
- Scaling without frame generation is "known to cause unstable frame capture or dropped frames" with both APIs ([community FAQ](https://sageinfinity.github.io/docs/FAQ/scalers)). Not an issue here since frame generation is always on.

Multi-monitor:

- "Output Display" picks the monitor LS draws on; default is the primary display ([community docs](https://sageinfinity.github.io/docs/Guides/LS%20settings%20Info)).
- By default LS stops scaling when it loses focus. "Multi Display Mode" keeps the output active when focus or the cursor moves to another display ([community docs](https://sageinfinity.github.io/docs/Guides/LS%20settings%20Info)). For hands-off use with a second monitor, turn it on. Early official notes said this mode "generally works slower than the single-display mode" (2019) ([Steam feed](https://api.steampowered.com/ISteamNews/GetNewsForApp/v2/?appid=993090&count=40&maxlength=0)); whether that still holds is **[Unverified]**.
- A second connected monitor can remove MPO support, which matters for DXGI capture on 24H2 and later (official, see above).

Overlays:

- The community docs say to disable Discord, NVIDIA and AMD overlays, custom crosshairs, animated wallpapers and third-party recorders, and that a base FPS reading far above the real one usually means an overlay is interfering ([best settings](https://sageinfinity.github.io/docs/Guides/bestsettings)).
- **[Reasoning]** An in-viewer Dear ImGui overlay is part of the viewer's own frame, so it is not an "overlay" in this sense. But if it animates while the video frame is unchanged, it creates new presents and can inflate the base frame rate LS sees.

### 4. Current LSFG version and settings for 30 to 60

Version:

- Newest official announcement in the Steam feed today is "Performance Mode Hotfix" (2025-07-14), a fix for LSFG 3.1 Performance mode. LSFG 3.1 itself was announced 2025-06-11 ([Steam feed](https://api.steampowered.com/ISteamNews/GetNewsForApp/v2/?appid=993090&count=40&maxlength=0)).
- So the current model is **LSFG 3.1** as far as official announcements show. The app's own version number is **[Unverified]**; a Reddit thread title calls the June 2025 release "Lossless Scaling 3.2", which I could not confirm. Whether an unannounced or beta build exists after July 2025 is **[Unverified]**.

Settings that matter (official unless marked):

- **Mode**: Fixed multiplies the base frame rate; Adaptive (added 2025-03-08) "dynamically adjusts fractional multipliers to maintain a specified framerate", with more GPU load and slightly lower quality than fixed ([Steam store page](https://store.steampowered.com/app/993090/Lossless_Scaling/), [Steam feed](https://api.steampowered.com/ISteamNews/GetNewsForApp/v2/?appid=993090&count=40&maxlength=0)). For a steady 30 to 60, Fixed X2 is the direct fit. **[Reasoning]**
- **Base frame rate advice**: "it is still recommended to lock the game's framerate to half the LS refresh rate" ([Steam feed, 2024-02-09 post](https://api.steampowered.com/ISteamNews/GetNewsForApp/v2/?appid=993090&count=40&maxlength=0)). 30 unique fps on a 60 Hz display matches this exactly.
- **Queue Target** (official, 2025-03-08): 0 is "Unbuffered capture, always using the last captured frame for the lowest latency", but can suffer under high GPU load or an uncapped base frame rate; 1 is the default, buffered with a queue of 1; 2 is for unstable base frame rates and adds latency ([Steam feed](https://api.steampowered.com/ISteamNews/GetNewsForApp/v2/?appid=993090&count=40&maxlength=0)). The community docs call 1 the ideal value and warn that 0 has a high chance of stutter ([settings reference](https://sageinfinity.github.io/docs/Guides/LS%20settings%20Info)). Start at 1, then try 0, since a capture feed has a steady cadence and the GPU has headroom. **[Reasoning]** for the order.
- **Max Frame Latency** (official, 2024-07-18): "Most likely, the optimal value will be 1 for Nvidia GPUs and 3 for AMD" ([Steam feed](https://api.steampowered.com/ISteamNews/GetNewsForApp/v2/?appid=993090&count=40&maxlength=0)). Community docs: default 3 is generally fine ([settings reference](https://sageinfinity.github.io/docs/Guides/LS%20settings%20Info)).
- **Flow Scale** (formerly Resolution Scale): lower values estimate motion at reduced resolution to save GPU. Official guidance is 75% for 1440p and 50% for 4K, with nothing stated for 1080p ([Steam feed, 2025-01-10 post](https://api.steampowered.com/ISteamNews/GetNewsForApp/v2/?appid=993090&count=40&maxlength=0)). Community docs suggest 80 to 100 at 1080p ([best settings](https://sageinfinity.github.io/docs/Guides/bestsettings)). Use 100 at 1080p on an RX 9070. **[Reasoning]**
- **Performance mode** (LSFG 3.1): "up to 2x GPU load reduction ... with a slight reduction in image quality" ([Steam feed, 2025-06-11 post](https://api.steampowered.com/ISteamNews/GetNewsForApp/v2/?appid=993090&count=40&maxlength=0)). Leave off unless GPU load is a problem. **[Reasoning]**
- **Scaling type**: Off, since the source and display are both 1080p (see section 3 of the main document).
- **Minimum base rate**: "LSFG 3 will disable frame generation if the base framerate drops below 10 FPS" ([Steam feed, 2025-03-08 post](https://api.steampowered.com/ISteamNews/GetNewsForApp/v2/?appid=993090&count=40&maxlength=0)).
- **Quality expectation**: the community docs give 40 fps as the minimum base and recommend 60 or more ([best settings](https://sageinfinity.github.io/docs/Guides/bestsettings)). A 30 fps base is below that guidance, so expect more visible artefacts and more added latency than the usual PC use case.

Duplicate frames versus unique frames:

- I found no developer statement on how LSFG handles identical consecutive frames. **[Unverified]**
- User evidence is consistent: LS counts presents, not unique images. PCSX2 presenting each 30 fps frame twice made LS show 60/60 and look choppy; enabling the emulator's "Skip Presenting Duplicate Frames" made LS show 30/60 and frame generation work ([Steam thread, 2024-08](https://steamcommunity.com/app/993090/discussions/0/4418677017728092474)).
- Same result with a capture card: with OBS output at 60 fps for a 30 fps game, "LSFG upscales duplicated frames, causing erratic framerate"; setting OBS to 30 fps fixed it ([Steam thread, 2024-06-30](https://steamcommunity.com/app/993090/discussions/0/4338735089007356929)).
- The community docs say the same in general terms: duplicated frames "significantly reduce" frame generation effectiveness ([best settings](https://sageinfinity.github.io/docs/Guides/bestsettings)).
- Where a game itself bakes the duplicates into its output, skipping duplicate presents did not help one user ([Steam thread, 2024-11](https://steamcommunity.com/app/993090/discussions/0/4418677017728092474)). This is the capture-card case: the repeats arrive as real frames in the 60 Hz signal, so the viewer must detect and drop them itself. **[Reasoning]**
- Officially, LS output never exceeds the display refresh rate; extra frames are skipped ([Steam feed, 2024-06-06 post](https://api.steampowered.com/ISteamNews/GetNewsForApp/v2/?appid=993090&count=40&maxlength=0)). So a 60 fps base on a 60 Hz display at X2 leaves no room for generated frames at all. **[Reasoning]**
- Check with "Draw FPS": the counter shows base and output rates, and should read 30/60 for a 30 fps game ([community docs](https://sageinfinity.github.io/docs/Guides/start)).
- Consequence for the viewer: present only when a new unique source frame arrives. This confirms the phase 2 requirement in section 6. Whether Adaptive mode copes better with a duplicated 60 fps feed is **[Unverified]**.
- A 60 fps game on a 60 Hz display gains nothing from X2 for the same reason. A second profile is not possible if both use the same exe and title, so either the viewer changes its window title per mode (for example a "30" and a "60" title matched by two profiles) or the user accepts LS idling at 60. **[Reasoning]**; whether LS re-evaluates profiles when a window title changes while scaled is **[Unverified]**.

### 5. Conflicts with AMD AFMF and Radeon overlays

- I found no official statement, from the LS developer or AMD, about running LS together with AFMF. **[Unverified]**
- The only Steam thread I found on LS and AFMF compares them and does not discuss using both at once ([Steam thread, 2024](https://steamcommunity.com/app/993090/discussions/0/4418675885152347170/)).
- **[Reasoning]** Do not enable AFMF on the viewer exe while LS is scaling it. AFMF would raise the viewer's presented frame rate, and LS counts presents (see above), so LS would see a doubled base rate. Keep AFMF off globally, or off in the viewer's Adrenalin profile. This is inference, not a sourced fact.
- Overlays: the community docs list AMD overlays among those to disable, and say an inflated base FPS reading points to overlay interference ([best settings](https://sageinfinity.github.io/docs/Guides/bestsettings)). A search extract of a Steam post claims the AMD metrics overlay does not interfere while PresentMon-based tools do; I could not open that post. **[Unverified]**
- Anti-Lag: the community docs say to enable Anti-Lag on AMD ([best settings](https://sageinfinity.github.io/docs/Guides/bestsettings)). **[Reasoning]** Anti-Lag targets a game's input-to-render pipeline; whether it does anything for a capture viewer is **[Unverified]**.
- FreeSync: LS supports it on AMD GPUs (official, see part 3). No conflict reported in official notes.
- Radeon Image Sharpening 2, Radeon Super Resolution, Enhanced Sync and Radeon Chill together with LS: no source found. **[Unverified]** Leave them off for the first test, then add one at a time. **[Reasoning]**
- MPO on AMD: the community docs mention AMD driver black screens and resets related to MPO on RX 5000 to 7000 cards and advise keeping MPO enabled unless there are problems ([MPO FAQ](https://sageinfinity.github.io/docs/FAQ/mpo)). Nothing specific to the RX 9070. **[Unverified for RX 9070]**

### Hands-off setup to try

All **[Reasoning]**, built from the sourced facts above. Each step needs a real test.

1. One-time: install LS, set "Minimize to tray", create a Task Scheduler task "At log on, highest privileges" running `LosslessScaling.exe -StartMinimized`. Turn the in-app startup option off.
2. One-time: create a profile whose filter is the viewer's exe name or window title. Enable auto scale. Set LSFG 3.1, Fixed X2, scaling Off, Flow Scale 100, Queue Target 1, Max Frame Latency 3, Draw FPS on for testing.
3. One-time: in AMD Software, keep AFMF and the Radeon overlay off for the viewer.
4. One-time: back up `Settings.xml` once it works.
5. Each session: start the viewer only. It goes borderless fullscreen, presents unique frames only, and takes focus. LS should scale it by itself.
6. Verify with the LS counter: 30/60 for a 30 fps game.

Fallback if auto scale proves unreliable: the viewer sends the scale hotkey itself. That path is unverified end to end (hotkey injection, elevation), so test auto scale first.

### Could not verify

- Name, location, range and default of the auto scale delay setting.
- Whether the 2019 command-line arguments still work in 3.x, and any argument for profiles or LSFG.
- Any way to command a running LS instance (IPC, CLI) other than the hotkey.
- Default hotkey from an official source, and whether a synthetic key press triggers it.
- `steam://rungameid/993090` behaviour, and whether LS runs without Steam open.
- Settings.xml schema and config.ini location.
- Measured DXGI versus WGC latency.
- LS behaviour with a flip-model, tearing-enabled source window.
- Developer explanation of duplicate-frame handling; behaviour of Adaptive mode on a duplicated feed.
- Any LS release newer than the 2025-07-14 hotfix, and the app version number.
- Any official position on LS with AFMF, Anti-Lag, RIS 2 or the Radeon overlay.
- losslessscaling.com pages, which all returned HTTP 403.
