# 08: Menu bar

**What to build:** GamePlayer gets a classic menu bar across the top of its window, in the style of VLC: the window is the video with a menu above it. From the menu the user can choose the Capture Card, force a resolution, set the frame rate handling, control the sound, toggle fullscreen, always-on-top and V-sync, and look up the hotkeys. Everything works without the menu too (zero configuration remains the default); the menu is for overrides. In fullscreen the menu bar is hidden so only the game is visible.

**Blocked by:** 04 (Unique Frames and Game Rate detection), 05 (Fullscreen and window behaviour), 07 (Audio controls).

**Status:** done (user hand checks pending)

## Context

GamePlayer is a small personal Windows 11 app (the "Viewer") that shows a game Console's picture and sound from a USB Capture Card on the PC monitor. The user described the look they want as "like VLC: just the screen that you can resize and a menu at the top". Read `GLOSSARY.md`, the ADRs in `docs/adr/`, the developer note from ticket 02, the card's real mode list in the prototype's findings note `prototype/FINDINGS.md`, and the interfaces left by tickets 04 (frame rate override on the Processor), 05 (fullscreen switch) and 07 (volume, mute, Audio Delay values).

Hardware: Xbox Series S; AVerMedia Live Gamer Ultra S GC553 Pro over USB; 1080p 144 Hz FreeSync SDR monitor; AMD Radeon RX 9070; Windows 11.

What the card really offers, from the prototype's log: uncompressed YUY2, NV12, RGB24 and P010 modes at 1080p and 60 fps or below, plus YUY2 at several other resolutions (some only at 50 and 30 fps). Its high modes (1080p240, 1440p and 2560x1080 at 120 and 144, 3440x1440 and 4K at 60) come paired with MJPG and are flagged full range, BT.601, which suggests they are MJPEG-derived. One entry contradicts the research doc's "above 60 fps is MJPEG only": Media Foundation lists an NV12 1080p120 mode that is not paired with MJPG. It has not been tested for streaming. 120 fps is out of scope for v1 regardless, so this changes nothing below; it is recorded so nobody concludes the list is wrong.

## Decisions already made

- **Native Win32 menu** (`HMENU` attached to the window). No custom-drawn menu, no UI framework (ADR 0001).
- **Hidden in fullscreen.** Entering fullscreen removes the menu bar; leaving restores it. The hotkeys still work in fullscreen.
- **Menu structure** (exact wording may be polished; keep it this small):
  - **Device**
    - *Capture Card*: a list of the video capture devices present, with a check mark on the one in use. Selecting another switches to it immediately (video and sound restart together). The automatic choice (prefer the GC553 Pro, otherwise the first device) remains the default.
    - *Resolution*: "Default (1080p)" plus a list of the resolutions the current card offers. Selecting one switches the capture mode. Only offer modes that are uncompressed and at most 60 fps; do not list MJPEG modes, and do not list the NV12 1080p120 mode or any other mode above 60 fps even if it looks uncompressed. Colour range and matrix differ between modes, so a switched mode must use the range and matrix from its own media type (ticket 02's shader reads them; confirm it still does after a switch). The user's wish for "auto" resolution was settled as: default to 1080p60, with this list as the manual override, because capture cards do not reliably report the incoming signal's resolution. The Console's own output resolution is set on the Xbox; the Viewer cannot command it.
    - *Frame rate*: Auto (default), 30, 60. This is the Processor override from ticket 04, not a capture mode: capture always runs at 60. Auto drops Repeated Frames as detected; 60 presents every captured frame; 30 presents at most every other frame. There is no 120 or 144 entry: 120 fps and above is out of scope for v1 (the card's above-60 modes are mostly MJPEG, which costs quality and Latency, and its one uncompressed-looking 120 mode is untested).
  - **Audio**
    - *Volume up*, *Volume down*, *Mute* (checkable), showing their hotkeys (Up, Down, M).
    - *Audio Delay*: increase / decrease in 50 ms steps (K / J), the current value shown, and a reset to 0.
    - There is deliberately **no output device picker**: sound always follows the Windows default output device.
  - **View**
    - *Fullscreen* (F11).
    - *Always on top* (checkable, default off). Applies in windowed mode.
    - *V-sync* (checkable, **default off**). Off means each frame is presented the moment it is ready, with tearing allowed, so the FreeSync monitor follows the Viewer's pace: lowest delay, no visible tearing. On means presents wait for the monitor's refresh (sync interval 1, no tearing flag), which adds a little delay and is there only as a fallback if the user sees tearing or stutter. The user asked for this to be optional and otherwise left the choice to us; off is the recommended default.
  - **Help**
    - *Hotkeys*: lists F11 (fullscreen), Esc (leave fullscreen), M (mute), Up / Down (volume), J / K (Audio Delay minus / plus 50 ms), F (FPS counter, from ticket 04).
    - *About*: app name and version, and the current status in a few lines: device in use, capture mode, Signal Rate, Game Rate. This doubles as the user's way to read the arriving / unique numbers. A simple standard dialog or message box is fine for both Help items.
- **Menu items reflect live state**: check marks and the Audio Delay value are correct whenever a menu opens, including after hotkey changes.
- **Changing a setting applies immediately**, with no "apply" step and no restart. Switching device or resolution goes through the same start-up path as launch, so the Video Area states from ticket 06 (if present) show loading and errors.
- **Persistence is ticket 09.** Hold the menu overrides in one plain settings structure with defaults so ticket 09 only has to save and load it.
- **Layout**: the menu bar takes space from the client area; the Video Area is what remains, and the 16:9 fit with black bars applies to it.
- **Keyboard**: do not let menu accelerators steal the v1 hotkeys. Alt still opens the menu as usual in windowed mode.

## Known unknowns

- **Presentation while a menu is open.** Win32 runs a modal loop while a menu is dropped down or the window is being moved, which can stall rendering if presenting depends on the window's message loop. The Presenter runs on its own thread (ticket 02), so the Feed should keep playing behind an open menu; verify that it does.
- **Low-delay presentation with a menu bar.** Whether a windowed flip-model swap chain still gets independent flip when the window has a menu bar is unverified on this GPU. It does not matter in fullscreen (no menu). Nothing to implement; note anything you observe.
- **Always on top versus Lossless Scaling.** Lossless Scaling draws its own output window over the Viewer. Whether a topmost Viewer window interferes is unverified. Apply always-on-top only in windowed mode and drop the topmost flag while in fullscreen.
- **V-sync versus Lossless Scaling.** When Lossless Scaling is active, its own output window and its own sync setting decide what reaches the display; the Viewer's V-sync switch then mainly changes how the Viewer's presents are paced. Mention this in the Hotkeys or About text only if it fits in one line; the full explanation belongs to the guide in ticket 10.
- **Resolutions other than 1080p** are untested on this card. If a selected mode fails to start, fall back to the default mode and surface the failure (through the Video Area error state if ticket 06 is done, otherwise a brief message).

## Out of scope

Saving any of this between runs (09); toolbars, status bars, context menus, a settings dialog; an audio output picker; 120 fps or MJPEG modes; HDR; recording, streaming, screenshots; sharpening; in-app Frame Generation or any Lossless Scaling control (the Viewer never controls Lossless Scaling, ADR 0002).

## Acceptance criteria

- [ ] A native menu bar with Device, Audio, View and Help menus is shown in windowed mode and hidden in fullscreen; it returns when leaving fullscreen.
- [ ] Device > Capture Card lists present video capture devices, marks the active one, and switches device on selection with picture and sound restarting together.
- [ ] Device > Resolution offers a default of 1080p plus the current card's uncompressed modes at 60 fps or below (never MJPEG, never the NV12 1080p120 mode), and switching works or falls back cleanly on failure.
- [ ] Device > Frame rate offers Auto, 30 and 60 and drives the Processor override; no 120 or 144 entry exists.
- [ ] Audio menu items change volume, mute and Audio Delay and stay in agreement with the hotkeys.
- [ ] View > Fullscreen behaves exactly like F11.
- [ ] View > Always on top keeps the windowed Viewer above other windows when checked.
- [ ] View > V-sync is off by default (present on arrival, tearing allowed); checking it switches to refresh-synchronised presentation immediately, and unchecking switches back.
- [ ] Help > Hotkeys lists the eight hotkeys (including F); Help > About shows name, version, device, capture mode, Signal Rate and Game Rate.
- [ ] Check marks and displayed values are correct every time a menu opens.
- [ ] The Feed keeps playing, picture and sound, while a menu is open and while the window is dragged.
- [ ] The Video Area keeps the 16:9 fit below the menu bar.
- [ ] All overrides live in one settings structure with defaults, ready for ticket 09.
- [ ] Unit tests cover pure logic introduced here (for example filtering a device's mode list down to the offered resolutions, and mapping settings to menu check states).

## How to verify

Yourself: build, run the unit tests, start the app, open each menu, toggle each item and confirm in your debug log that the corresponding setting changed and the pipeline reacted.

For the user. End your work by reporting this checklist:

1. Is there a menu across the top, with the game below it, like VLC?
2. Open a menu and leave it open. Does the game keep moving and sounding behind it?
3. Press F11. Is the menu gone? Press Esc. Is it back?
4. Try Audio > Mute and the M key. Do they agree?
5. Try View > Always on top, then click another window. Does GamePlayer stay in front?
6. Try View > V-sync on and off while playing. Any difference in smoothness or feel? (Off is the intended setting.)
7. Open Help > About during a 30 fps game. Does it show the Signal Rate as 60 and the Game Rate as 30?
8. Try Device > Frame rate at 60 and then back to Auto. Does the game still look right?
