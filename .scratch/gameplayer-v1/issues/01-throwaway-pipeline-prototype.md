# 01: Throwaway prototype proving the pipeline

**What to build:** A bare, disposable Windows program that shows the Capture Card's Feed (picture and sound) in a plain window with the chosen technology stack, so the user can judge by hand whether the approach is good enough before the real app is started. It has no menu, no settings and no polish. It also writes a log describing what the Capture Card really offers, and you finish by writing a short findings note. Nothing from this prototype is reused as app code.

**Blocked by:** None (can start immediately).

**Status:** ready-for-agent

**Hard stop after this ticket:** This is the only ticket that may be worked on right now. When the prototype and its findings note are done, stop. Do not start ticket 02 or any other ticket, do not create the real app's project, and do not "get a head start" on anything. The user will review the whole plan against your findings before any further work is authorised, and the remaining tickets (currently marked needs-plan-review) may be rewritten at that review.

## Context

GamePlayer is a small personal Windows 11 app (the "Viewer") that shows a game Console's picture and sound from a USB Capture Card on the PC monitor, replacing OBS for that single job. Its top priority is low Latency; it does video and audio only (no recording, no streaming). It is for one user, never sold or distributed.

Read these first, in this order. They are short and they are the source of truth for vocabulary and settled decisions:

- `GLOSSARY.md` (repo root): the project vocabulary. Use these terms in code comments, logs and notes.
- `docs/adr/0001-native-cpp-direct3d11-stack.md`: why the stack is C++ / Direct3D 11 / Media Foundation / WASAPI / Win32.
- `docs/adr/0002-frame-generation-stays-outside-the-viewer.md`: why Frame Generation is done by the separate Lossless Scaling app and what that demands of the Viewer.
- `docs/research/capture-viewer-research.md`: the background research, including a hardware section and a "Lossless Scaling automation" section. Long; skim the summary, section 0 (hardware), the capture API section and the Lossless Scaling section.

Hardware and environment (fixed, do not design for anything else in this ticket):

- Console: Xbox Series S, set to 1080p, 60 Hz, HDR off, VRR off, stereo audio.
- Capture Card: AVerMedia Live Gamer Ultra S (model GC553 Pro), USB 5 Gbps.
- GPU: AMD Radeon RX 9070.
- Monitor: 1080p, 144 Hz, FreeSync, SDR (no HDR).
- OS: Windows 11 Pro.

Two facts that shape everything:

- The Console sends a 60 Hz signal (the Signal Rate) even when the game runs at 30 fps (the Game Rate). A 30 fps game is therefore expected to arrive as 60 frames per second in which every frame is sent twice (a Unique Frame followed by a Repeated Frame). This is based on user reports, not a vendor statement; this prototype must confirm it.
- The Capture Card's uncompressed ceiling is 1080p60 (NV12, YUY2 or RGB24 according to AVerMedia's FAQ). Anything above 60 fps, and 4K60, is MJPEG only. MJPEG costs quality and Latency and is not used.

## Step 0: check the build toolchain

Before writing anything, check whether a C++ toolchain is present on this machine: the MSVC compiler (Visual Studio 2022 or Build Tools with the "Desktop development with C++" workload), a Windows 10/11 SDK, and CMake (the one bundled with Visual Studio counts). Report exactly what is present and what is missing.

If something is missing, stop and report it. Do not install anything, and do not download installers, without the user explicitly saying so. Installing a toolchain changes the user's machine and is their decision.

## What the prototype does

- Opens a plain Win32 window (no menu bar, no UI framework).
- Enumerates video capture devices with Media Foundation, picks the GC553 Pro (match on the device's friendly name; fall back to the first video capture device), and logs every native media type the device exposes: subtype (NV12, YUY2, RGB24, MJPG, ...), resolution, frame rate, and any other attributes that are cheap to print (interlace mode, nominal range, YUV matrix if present).
- Captures with the Media Foundation Source Reader in asynchronous callback mode at 1920x1080, 60 fps, uncompressed. Prefer YUY2, fall back to NV12 (either is fine; log which was selected and why). Take the system-memory buffer as the device delivers it. Do not let Media Foundation insert converters or a video processor, and do not request RGB32 output from the reader: hidden conversion stages add delay.
- Converts YUV to RGB in your own pixel shader. Assume BT.709, limited range (16-235) unless the media type says otherwise; log what the media type reports.
- Presents with Direct3D 11 on a flip-model swap chain: `DXGI_SWAP_EFFECT_FLIP_DISCARD`, 2 buffers, created with `DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING` and `DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT`. Present each frame as soon as it arrives with `Present(0, DXGI_PRESENT_ALLOW_TEARING)` (check tearing support with `IDXGIFactory5::CheckFeatureSupport` first and log the result). Never queue frames: if a newer frame arrives before the previous one was shown, the newest wins.
- Drops Repeated Frames: compare each captured frame with the previous one and present only Unique Frames. Start with the simplest possible comparison (exact byte comparison of the raw buffer, or of a sparse sample of rows). Provide a key that turns this dropping on and off at runtime, so the user can compare both behaviours under Lossless Scaling.
- Plays the Capture Card's sound: capture the card's audio endpoint with WASAPI (shared mode, event-driven) and render it to the Windows default output device through a small ring buffer (aim for roughly 10-20 ms). No drift correction is needed in the prototype, but log the ring buffer fill level once per second so drift is visible.
- Toggles borderless fullscreen with F11: a borderless window covering the monitor the window is currently on. Never use exclusive fullscreen (`SetFullscreenState`), because Lossless Scaling cannot work on an exclusive fullscreen window.
- Keeps the picture at 16:9 with black bars when the window is another shape.
- Once per second, logs: frames arrived per second, Unique Frames per second, Repeated Frames per second, frames replaced before being shown, and audio buffer fill. Also shows arrived / unique rates in the window title so the user can read them without opening the log.
- Writes the log to a text file next to the executable.

## Where it lives

Put the whole prototype in its own top-level folder clearly named as a prototype (for example a `prototype` folder), with its own minimal build setup. It must be deletable in one go. Do not create the real app's project structure here; ticket 02 does that from scratch.

## Decisions already made

Do not revisit these; they were settled with the user and independently reviewed.

- Stack: C++, Direct3D 11, Media Foundation Source Reader, WASAPI, plain Win32. Reason: Latency is decided by the pipeline, and these are the native APIs with no garbage collector, interop layer or UI compositor in the path (ADR 0001).
- Uncompressed 1080p60 capture, no MJPEG, no HDR, no VRR capture.
- Present on arrival with tearing allowed. The monitor has FreeSync, so the display follows the Viewer's pace; this gives the lowest delay and no visible tearing.
- Borderless fullscreen only.
- Frame Generation is not built here. The separate Steam app Lossless Scaling runs on top of the window (ADR 0002).
- No formal latency measurement. The user declined it; the judgement is "does it feel at least as good as OBS in my hands".
- The prototype is throwaway. Favour the shortest code that answers the questions over structure, error handling or reuse.

## Known unknowns

These were flagged as unverified in the research. The prototype exists largely to settle them. Record what you find in the findings note.

- Whether the GC553 Pro is a plain driverless UVC / UAC device that Media Foundation can open, and whether its real mode list matches AVerMedia's FAQ (1080p60 in NV12 / YUY2 / RGB24). If Media Foundation does not expose a usable uncompressed 1080p60 mode, do not switch to DirectShow on your own: report it clearly, since it changes ticket 02.
- Whether a 30 fps game really arrives as doubled frames inside a 60 Hz signal, and whether the Repeated Frame is bit-identical to the frame before it. The card's internal processing might make repeats differ slightly, in which case exact comparison fails and a small tolerance is needed. The log's unique-frame rate during a 30 fps game answers this (expected: about 30 unique, about 30 repeated). You cannot run a game yourself; make the log and window title make this obvious to the user.
- Whether the card exposes a true 1080p30 mode. Just log it; it is not used.
- Whether the card's audio endpoint can be found reliably without user input. Try matching the audio capture endpoint whose name contains the same device name as the video device; log all capture endpoints and which one was chosen.
- Whether only one program can hold the card at a time. If opening fails because OBS or AVerMedia's software holds it, log the HRESULT; ticket 06 needs it.
- Whether motion is smooth on a 144 Hz FreeSync monitor when presenting 60 (or 30) frames per second on arrival, in a window and in borderless fullscreen. If it stutters, the fallback to suggest to the user is setting the monitor to 120 Hz.
- Whether Lossless Scaling (LSFG 3, fixed X2) works on this window type (flip-model, tearing allowed) and reads a base of 30 when Repeated Frames are dropped. Research suggests Lossless Scaling counts presented frames in its DXGI capture mode and may behave differently in WGC mode.

## Out of scope

Menu bar, settings, remembered window position, volume / mute / Audio Delay controls, error screens, device hot-plug recovery, drift correction, V-sync option, unit tests, clean architecture, recording, streaming, screenshots, sharpening, in-app Frame Generation, latency measurement tooling.

## Acceptance criteria

- [ ] Toolchain check performed and reported; nothing installed without the user's explicit approval.
- [ ] The prototype builds with one documented command and produces a single executable.
- [ ] On launch with the GC553 Pro connected and the Console on, the Feed picture appears in the window with no configuration.
- [ ] The Feed sound plays through the Windows default output device.
- [ ] Capture is uncompressed 1080p60 via Media Foundation Source Reader with no inserted converter; colour conversion is done in the prototype's own shader.
- [ ] Presentation uses a flip-discard swap chain and presents on arrival with tearing allowed when supported.
- [ ] Repeated Frames are dropped by default, and a key toggles dropping on and off.
- [ ] F11 toggles borderless fullscreen on the current monitor; exclusive fullscreen is never used.
- [ ] The log lists every mode the card exposes, the mode chosen, all audio capture endpoints and the one chosen, tearing support, and per-second arrival / unique / repeated rates and audio buffer fill.
- [ ] The prototype lives entirely in its own clearly named folder.
- [ ] A findings note is written next to the prototype (see below), ending with the three plan-review lists: decisions that held, decisions that need revising, breaking surprises.
- [ ] Work stops after the findings note; no other ticket is started and no other ticket or ADR is edited.

## How to verify

What you can check yourself: it compiles without warnings you introduced, it starts without crashing, and the log shows a device opened, a mode selected and frames arriving. If the card is unplugged or the Console is off you may only be able to confirm the build and the device enumeration; say so plainly rather than claiming it works.

What only the user can check. End your work by giving the user this checklist, and ask them to send back the log file and their answers:

1. Start the prototype with the Xbox on. Does the picture appear by itself? Does the sound play?
2. Play for a few minutes. Does the controller response feel at least as good as the OBS preview you use today?
3. Is motion smooth, in the window and after pressing F11? If it stutters, set the monitor to 120 Hz in Windows display settings and try again.
4. Start a 30 fps game. What do the arrived and unique numbers in the window title read?
5. With that 30 fps game running, turn on Lossless Scaling with LSFG at fixed X2 on the prototype window. Does it show 30 as the base rate and 60 as the output? Does it look smooth? Then press the key that turns off Repeated Frame dropping and say what changes.
6. Does the sound stay in step with the picture over 15 minutes or so?

## Findings note

After the user reports back (or immediately, with the user-only items marked "awaiting user"), write a short findings note in the prototype folder containing:

- The answers to the four questions: picture and sound arrive from the card; it feels at least as good as OBS; motion is smooth on the 144 Hz FreeSync monitor; Lossless Scaling works on the window with a 30 fps game and reports a 30 base.
- The card's real mode list as logged, and the mode chosen.
- Whether Repeated Frames are bit-identical, and what comparison worked.
- Which audio endpoint was chosen and how it was matched.
- The error observed when the card is held by another program, if seen.
- Anything that contradicts the assumptions in this ticket or the ADRs (for example: Media Foundation missing modes so DirectShow is needed; the card not being driverless; presenting on arrival stuttering; Lossless Scaling misbehaving on this window type).

The note must end with a section written for the plan review, with three explicit lists:

- **Decisions that held**: each planned decision the prototype confirmed (for example: Media Foundation Source Reader works on this card; uncompressed 1080p60 is available; presenting on arrival with tearing allowed is smooth with FreeSync; borderless fullscreen works; dropping Repeated Frames gives Lossless Scaling a base of 30; the card's audio endpoint can be found automatically).
- **Decisions that need revising**: each planned decision the prototype weakened or disproved, with what you observed and what you would change. Name the tickets (02 to 10) and ADRs (0001, 0002) affected.
- **Breaking surprises**: anything that could invalidate the approach as a whole, or that nobody anticipated.

Mark every item whose answer depends on the user's manual checks as "awaiting user" until they report, then update the note. Do not edit the other tickets or the ADRs yourself; list the proposed changes and leave them for the review.

Then stop. No other ticket starts until the user has reviewed this note and confirmed or revised the plan.
