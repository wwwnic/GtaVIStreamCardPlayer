# Ticket 01 prototype: findings

Status: built and smoke-tested unattended. The six manual checks below are awaiting the user. This note is written from the implementing agent's report.

## How to run

- Build: `prototype\build.bat` (VS Build Tools 2022, MSVC 14.44, Windows SDK 10.0.26100.0; no CMake used).
- Run: `prototype\out\gameplayer-prototype.exe` (optional `--run N` exits after N seconds).
- Log: `prototype\out\gameplayer-prototype.log`, rewritten each run.
- Keys: F11 borderless fullscreen, Esc leaves it, D toggles Repeated Frame dropping (default on), T toggles near-duplicate tolerance (default off; frames differing in under 0.5% of 8-byte words count as repeats).
- Window title shows `arrived N unique N | drop ON/OFF | status`.
- F toggles the on-screen fps overlay (default on, top-left of the picture, text "unique / arrived", updated once a second; drawn only inside frames already being presented, so it adds no presents and goes stale on a fully static picture).

## Verified unattended (8 s run, card plugged in, static picture)

- Media Foundation opens "Live Gamer ULTRA S GC553Pro" (USB VID 07CA PID 1553) with no vendor driver.
- Source Reader, async, converters disabled, accepts native YUY2 1920x1080 at 60 fps. 59.9 to 60.2 frames/s arrived.
- Direct3D 11 runs on the Radeon RX 9070; DXGI tearing supported. Swap chain is flip-discard, 2 buffers, tearing plus waitable object, max frame latency 1.
- Chosen media type reports limited range, BT.709, progressive.
- Card audio endpoint found by name with no user input. WASAPI capture and render ran with 0 underruns and 0 overruns. Both 48 kHz float stereo, shared mode, event-driven. Render buffer 22 ms; ring 40 ms capacity, plays after 10 ms primed.
- Static picture: about 60 arrived/s, 0 to 1 unique/s, repeats byte-identical.

## Card mode list (125 native types)

- YUY2 1080p at 60, 59.94, 50, 30, 29.97 fps, plus 640x480, 720x480, 720x576, 720p, 1280x800, 1920x1200, 2560x1080, and 2560x1440 (50 and 30 only).
- NV12 1080p at 120, 60, 59.94, 50, 30, 29.97 fps.
- RGB24 1080p at 60 and below (bottom-up, negative stride).
- P010 1080p at 60 and below.
- High modes flagged full range, BT.601 (probably MJPEG-derived): NV12 and MJPG pairs at 1080p240; 2560x1080 and 2560x1440 at 144 and 120; 3440x1440 at 60; 3840x2160 at 60.
- A true 1080p30 mode exists in YUY2 and NV12.

## Audio endpoints

- Six capture endpoints found. Chosen: "HDMI (Live Gamer ULTRA S GC553Pro)".
- A second endpoint, "Not To Use (Live Gamer ULTRA S GC553Pro)", also matches the name rule and lost only on enumeration order.

## Not verified (needs the user)

1. Xbox on, start the prototype: does the picture appear by itself, and does sound play?
2. Play a few minutes: does controller response feel at least as good as the OBS preview?
3. Is motion smooth, windowed and after F11? If it stutters, set the monitor to 120 Hz and retry.
4. Start a 30 fps game: what do `arrived` and `unique` read in the title? Expected about 60 and about 30. If unique is near 60, press T and re-check.
5. Same game, Lossless Scaling (LSFG, fixed X2) on the prototype window: does it show base 30, output 60, and look smooth? Then press D to stop dropping Repeated Frames and note what changes.
6. Does sound stay in step over about 15 minutes? Check ring fill, underruns and overruns in the log.

Also unverified: the error returned when another app holds the card (ticket 06 needs that HRESULT), the shader's on-screen colours, and fullscreen and key handling (the smoke test only confirmed start and clean exit).

## Plan review

### Decisions that held

- Media Foundation Source Reader works on this card, driverless, async, converters disabled. The DirectShow fallback in ADR 0001 has not been needed.
- Uncompressed 1080p60 exists (and 1080p30).
- Own-shader YUV to RGB runs; the media type matches the limited-range BT.709 assumption.
- Flip-discard with tearing and a waitable object works on the RX 9070.
- The audio endpoint is found automatically.
- Exact byte comparison marks a static signal as fully repeated.
- Exclusive fullscreen is avoided.

### Decisions that may need revising (none proven yet)

- Ticket 04: add a near-duplicate tolerance if a 30 fps game's repeats are not bit-identical (check 4 decides).
- Ticket 03: add an explicit rule to skip the "Not To Use" audio endpoint.
- Ticket 06: "no signal" cannot be detected from frames stopping, because the card keeps sending 60 fps on a static picture. Ticket 04 must also cope with a fully static Feed.
- Tickets 02 and 08: colour flags differ per mode, so the app must read range and matrix from the chosen media type rather than assume them.
- Research doc and ticket 08: Media Foundation lists NV12 1080p120 as a type not paired with MJPG, which contradicts "above 60 fps is MJPEG only". Not tested for streaming.

### Breaking surprises

- None so far.
