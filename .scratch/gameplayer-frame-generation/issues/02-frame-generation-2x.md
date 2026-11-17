# 02: Frame Generation 2x

**What to build:** The user presses G and motion becomes smoother: for every Unique Frame the Viewer makes one Generated Frame and shows both, evenly spaced. A 60 fps game is shown at 120, a 30 fps game at 60. G again turns it off. The choice is in the View menu too, is remembered between runs, and no second program is needed. If the PC cannot do it, the Feed keeps playing and the Viewer says so.

**Blocked by:** 01 (Presenter shows several timed frames per Unique Frame).

**Status:** ready-for-agent

## Context

GamePlayer (the "Viewer") shows a game Console's picture from a Capture Card. Until now smooth motion came from Lossless Scaling, a separate app run on top of the Viewer. ADR 0003 moves Frame Generation into the Viewer, on the GPU, using AMD's AMF frame rate conversion component (FRC), which makes in-between frames from pictures alone. Ticket 01 gave the Presenter the timing for it. Read `GLOSSARY.md`, `docs/adr/0003-frame-generation-inside-the-viewer.md`, `docs/DEVELOPING.md` and `.scratch/gameplayer-frame-generation/README.md` (it holds the prototype's measurements and where to read the prototype).

## Decisions already made

- **The key and the menu.** G switches Frame Generation between off and 2x (ticket 03 adds 4x to the cycle). A held G switches once. View menu: an item "Frame generation" with a check mark and its key shown. Help > Hotkeys lists G. The user works by keys; the menu is the reference for what each key does.
- **Feedback.** Each press shows the usual short line at the top of the Video Area: "Frame generation 2x" or "Frame generation off".
- **Remembered** in the settings file like the other View choices; off on a fresh install. A value the file does not understand means off.
- **The generator:** AMD AMF FRC, on the Viewer's own Direct3D 11 device, BGRA textures at the Feed's size (not the window's). Settings that worked in the prototype: mode "interpolated only", profile high, fallback blending on, "use future frame" off (on adds a frame of Latency), indicator off. Decision-rich part of the prototype, for precision:

  ```
  engine: Direct3D 11        mode: FRC_ONLY_INTERPOLATED
  profile: FRC_PROFILE_HIGH  fallback: on
  use future frame: off      surface: BGRA, Feed width x height
  ```

- **Which frame comes out.** After Unique Frame N is submitted, FRC gives the frame between N-1 and N. Each input is stamped with a number; an output is used only if it carries the number just submitted. Any other output is late: drop it and ask again. This is what guarantees the picture never steps back.
- **Do not wait long.** Ask for the output right after the submit; if it is not ready within about 2 ms, show the real frame alone for that Unique Frame.
- **Starting takes time** (67 ms normally, about 9 s the very first time on this PC). It happens off the presenting thread; real frames keep showing until it is ready. Turning Frame Generation off, closing the app or losing the device must never freeze the picture or the window while it starts.
- **Input textures** stay untouched for as long as FRC may still read them (the prototype kept a ring of four).
- **Latency.** The real frame is held for half the interval. Accepted (ADR 0003).
- **AMD only.** The AMF runtime comes with the AMD driver and is loaded at run time; the Viewer installs nothing. If the runtime is missing, the menu item is greyed and says why (for example "Frame generation (needs an AMD graphics card)"), and G shows a short line saying the same.
- **When it breaks** (start fails, a call fails mid-game, an unexpected output): real frames keep showing, one short line "Frame generation not available", and it stays off for the rest of the run without retrying on every frame. The remembered choice is not changed by a failure.
- **V-sync** is ignored while Frame Generation is on, and its menu item is greyed then. Waiting for the monitor would break the timing.
- **No automatic limit.** The Viewer never lowers the choice because of the monitor's refresh rate.
- **Off means untouched.** With Frame Generation off, every rule in the "Lossless Scaling" section of `docs/DEVELOPING.md` still holds and the present count is what it was before this sprint.
- **Texts stay on top** of Generated Frames, never mixed into them.
- **AMF header files** go in `third_party/` with their MIT licence text, public headers only. Nothing downloaded is executed.
- **Structure.** The cycle of choices, the labels, the settings value and the "use this output or drop it" rule are pure logic in `core`, unit-tested. The AMF calls are platform code behind the generator interface from ticket 01.
- **Log.** Lines prefixed `FrameGen:`: the start result with the reason on failure; once a second the Generation Factor, Generated Frames made out of attempts, frames presented, late outputs dropped, and generation time (average and worst).

## Known unknowns

- GPU time per Generated Frame was never measured, only CPU-side time (about 1 ms). Measure it if it is cheap to do; otherwise say it is still unknown.
- The AMF documentation does not state which pair of frames an output lies between. The prototype's log (output number equals the last input's, 0 late outputs) and the user's eyes say N-1 and N. Keep the number check; it catches the other case.
- Whether FRC leaves any Direct3D state changed that the Presenter relies on. The prototype saw no problem; check the Presenter sets everything it needs on each pass.
- Behaviour on a 30 fps Game Rate with uneven frame pacing in the game. The user watches for judder.

## Out of scope

4x (ticket 03); the Sharp Upscale (ticket 04); the two-line counter and guide changes (ticket 05); an automatic limit; any non-AMD generator; detecting or controlling Lossless Scaling; exclusive fullscreen.

## Acceptance criteria

- [ ] G switches between off and 2x, a held G once; the short line confirms each press.
- [ ] View > Frame generation shows the state and the key, and does the same as G. Help > Hotkeys lists G.
- [ ] With 2x on and a 60 fps Game Rate the log shows about 60 Generated Frames and 120 presented per second; with a 30 fps Game Rate about 30 and 60.
- [ ] No late output is ever shown; the log counts the ones dropped.
- [ ] While the generator starts, real frames keep showing; switching off, closing the app and a device rebuild during that time do not freeze anything.
- [ ] The choice is remembered across restarts; off on a fresh install; a damaged value means off.
- [ ] Without the AMF runtime the menu item is greyed with its reason and the Feed plays normally.
- [ ] A failure shows "Frame generation not available" once, real frames continue, nothing retries every frame, and the remembered choice is unchanged.
- [ ] While Frame Generation is on, V-sync has no effect and its menu item is greyed; turning Frame Generation off restores it.
- [ ] With Frame Generation off, presents per second and the Lossless Scaling rules are exactly as before this sprint.
- [ ] Texts in the Video Area are sharp and steady on Generated Frames.
- [ ] Unit tests cover: the G cycle; the menu labels and check marks, including the greyed case; the settings round trip and damaged values; the rule that accepts or drops an output by its number.
- [ ] `third_party/` holds the AMF headers with their licence; `docs/DEVELOPING.md` describes the generator, its thread, the key and the log lines.

## How to verify

Yourself: build, run the unit tests, one short smoke run with Frame Generation on (own `--settings` file) and read the `FrameGen:` lines.

For the user. End your work by reporting this checklist:

1. Start GamePlayer, press F11, then G. Does motion get smoother?
2. Press G back and forth on the same scene. Any picture jumping back, or stutter?
3. Pan the camera fast. Are the HUD and text still clean?
4. Close GamePlayer with it on and start again. Is it still on?
5. Try a 30 fps game if you have one. Smoother, or juddery?
