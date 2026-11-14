# 05: Fullscreen and window behaviour

**What to build:** The user presses F11 and the Feed fills the monitor the window is on; F11 or Esc brings the window back exactly as it was. In fullscreen the mouse cursor disappears after two seconds without movement. While a Feed is playing, the PC and the display never go to sleep, even though the user is only touching a controller. Starting GamePlayer a second time brings the existing window forward instead of opening another one.

**Blocked by:** 02 (App skeleton showing the Feed picture in a window).

**Status:** needs-plan-review

**Gate:** Do not start this ticket until the user has reviewed the findings note from ticket 01 (the throwaway prototype) and has confirmed or revised the plan. This ticket may be rewritten at that review; if its status still reads needs-plan-review, stop and ask the user rather than beginning work.

## Context

GamePlayer is a small personal Windows 11 app (the "Viewer") that shows a game Console's picture and sound from a USB Capture Card on the PC monitor. The user's main complaint about their current tool (OBS) is that going fullscreen takes a right-click and a monitor choice every time; one key is the whole point of this ticket. Read `GLOSSARY.md`, `docs/adr/0001-native-cpp-direct3d11-stack.md`, `docs/adr/0002-frame-generation-stays-outside-the-viewer.md` and the developer note from ticket 02.

Hardware: 1080p 144 Hz FreeSync SDR monitor (the user may have more than one monitor); AMD Radeon RX 9070; Windows 11.

## Decisions already made

- **Fullscreen is a borderless window, never exclusive fullscreen.** Do not call `SetFullscreenState` and make sure DXGI's Alt+Enter handling is disabled (`MakeWindowAssociation` with `DXGI_MWA_NO_ALT_ENTER`). Reason: the separate Frame Generation app, Lossless Scaling, cannot work on exclusive fullscreen windows (ADR 0002), and on Windows 11 a borderless flip-model window gets the same low-delay presentation path.
- **Which monitor**: the one the window is currently on (the monitor containing most of the window). No monitor picker, no prompt.
- **Keys**: F11 toggles fullscreen. Esc leaves fullscreen and does nothing when windowed. These are window-level keys that work whenever the GamePlayer window has focus.
- **Restore**: leaving fullscreen returns the window to its previous position, size and maximised state.
- **Always opens windowed.** Fullscreen is not remembered between runs.
- **Cursor**: in fullscreen, hide the mouse cursor after 2 seconds without mouse movement; show it again on movement. In windowed mode the cursor is never hidden. An extra reason beyond tidiness: research found that a visible hardware cursor can disable the low-delay presentation path when Lossless Scaling is capturing with VRR active.
- **No sleep while a Feed plays**: prevent system sleep and display sleep (and the screensaver) while the Viewer is showing a Feed, since controller input on the Console does not count as PC activity. Use `SetThreadExecutionState` with the system-required and display-required flags, or a power request. Release it when no Feed is playing (no card or no signal) and on exit, so an idle GamePlayer does not keep the PC awake forever.
- **Single instance**: a second launch activates the existing window (restores it if minimised, brings it to the foreground) and exits. Reason: the Capture Card can be held by one program at a time, so a second instance could never show anything.
- **Menu bar**: ticket 08 adds a menu bar that must be hidden in fullscreen. Structure the fullscreen switch so that hiding and restoring window chrome is in one place.
- **Presentation unchanged**: the swap chain stays flip-model, presenting on arrival; resize the swap chain buffers on the mode change and keep the 16:9 fit with black bars (on a 16:9 monitor the picture fills the screen).

## Known unknowns

- Whether the borderless fullscreen window reaches independent flip (bypassing the desktop compositor) on this GPU and monitor is unverified. Use the standard approach (a popup-style window exactly covering the monitor's bounds, flip-model swap chain, tearing allowed). If you have a way to check the present mode (for example PresentMon, only if already installed; do not install tools without asking), note the result; otherwise list it as a user check.
- Behaviour when the window is fullscreen and the monitor configuration changes (monitor unplugged, resolution change). Handle `WM_DISPLAYCHANGE` sensibly: stay covering the monitor if it still exists, otherwise drop back to windowed on a remaining monitor. Do not over-engineer it.
- Focus loss in fullscreen with multiple monitors: clicking on another monitor should leave the Viewer fullscreen and visible (it is just a window). Research notes Lossless Scaling may unscale on focus loss unless its "Multi Display Mode" is on; that is user configuration covered by ticket 10, not something to solve here.

## Out of scope

Menu bar and the View menu's fullscreen and always-on-top entries (08); remembering window position between runs (09); an on-screen overlay; choosing a monitor; exclusive fullscreen; HDR; any change to capture or audio.

## Acceptance criteria

- [ ] F11 switches the window to borderless fullscreen covering exactly the monitor the window is on; F11 again restores the previous position, size and maximised state.
- [ ] Esc leaves fullscreen and does nothing in windowed mode.
- [ ] Exclusive fullscreen is never entered; Alt+Enter does not trigger DXGI's fullscreen switch.
- [ ] With two monitors, fullscreen goes to whichever monitor the window was on, with no prompt.
- [ ] In fullscreen the cursor hides after 2 seconds of no mouse movement and reappears on movement; in windowed mode it is always visible.
- [ ] While a Feed is playing, the system and display do not sleep and the screensaver does not start; when no Feed is playing or the app has exited, normal sleep behaviour returns.
- [ ] Launching GamePlayer while it is already running brings the existing window to the foreground and starts no second instance.
- [ ] The picture stays correct through the switch (no stretch, no leftover border, no flash of garbage) and the app always starts windowed.
- [ ] Unit tests cover any pure logic introduced (for example choosing the target monitor rectangle from a set of monitor rectangles and a window rectangle, and the cursor idle timer's state transitions).

## How to verify

Yourself: build, run the unit tests, start the app, send it through fullscreen and back, start a second copy, and confirm from your debug log that the sleep-prevention request is taken and released at the right moments.

For the user. End your work by reporting this checklist:

1. Press F11. Does the game fill the screen with no border or taskbar?
2. Press Esc, then F11 twice. Does the window return to where and how it was?
3. If you have two monitors: move the window to the other one and press F11. Does it fill that one?
4. In fullscreen, leave the mouse alone for a few seconds. Does the cursor vanish, and come back when you move it?
5. Play with only the controller for longer than your screen-off time. Does the screen stay on?
6. Start GamePlayer again while it is open. Does the existing window just come forward?
7. Does fullscreen feel as responsive as windowed?
