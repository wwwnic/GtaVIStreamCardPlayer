# 04: Sharp Upscale

**What to build:** The user presses U and a Feed shown larger than it arrived (a 720p Feed in fullscreen) looks crisp instead of soft. U again goes back to the plain smooth stretch. The choice is in the View menu, is remembered between runs, and works on any graphics card.

**Blocked by:** None (can start immediately).

**Status:** done

## Validation recorded at integration

All 325 unit tests passed, and the live capture smoke test succeeded. The user confirmed all five manual checks on `5cae0bc`, including 720p fullscreen playback, HUD and text sharpness, edge and grain quality, window resizing, and persistence after restarting the Viewer.

## Context

GamePlayer (the "Viewer") shows a game Console's picture from a Capture Card. The user often sets the Console to 720p to spare it; the Viewer then stretches the picture to the screen with a plain smooth filter, which looks soft. A prototype added FSR 1 (AMD FidelityFX Super Resolution 1.0, MIT licence), which works on pictures alone: one pass rebuilds edges while enlarging, one pass sharpens. The user judged it good. Read `GLOSSARY.md`, `docs/adr/0003-frame-generation-inside-the-viewer.md`, `docs/DEVELOPING.md` and `.scratch/gameplayer-frame-generation/README.md` (where to read the prototype).

## Decisions already made

- **The key and the menu.** U toggles the Sharp Upscale; a held U toggles once. View menu: an item "Sharp upscale" with a check mark and its key shown. Help > Hotkeys lists U.
- **Feedback.** "Sharp upscale on" / "Sharp upscale off", the usual short line.
- **Remembered** in the settings file; off on a fresh install; a value the file does not understand means off.
- **Two passes,** in this order: the edge-following enlargement (FSR 1 "EASU") from the Feed's size into a texture of exactly the size the picture is shown at, then the sharpening (FSR 1 "RCAS") from that texture onto the screen, pixel for pixel.
- **Straight to the size shown.** A 720p Feed on a 1440p screen is enlarged once, 720p to 1440p. Never to 1080p first.
- **Only when enlarging.** If the picture is shown at its own size or smaller, the plain path is used and U changes nothing visible. The choice is still remembered.
- **One fixed strength:** FSR's sharpness 0.2 stops below maximum (the value the user saw). No setting.
- **Every frame.** Real frames and Generated Frames get the same treatment. Frame Generation runs at the Feed's size, before this; the Sharp Upscale is the last step before texts are drawn.
- **Texts stay on top,** drawn after it, never sharpened.
- **Any graphics card.** Plain shaders on the Viewer's own Direct3D 11 device; nothing from AMD's AMF is needed.
- **If it cannot be set up** (a shader does not compile, a texture cannot be made), the plain stretch is used and the log says why. No message on screen; the picture just stays as it was.
- **The shader text.** The prototype wrote the two passes out from the published method rather than using AMD's files. Keep it written out in the Viewer's own shader, with AMD's MIT notice beside it, and compare it against AMD's published FSR 1 source during this ticket: the tap positions, the direction and length estimate, the kernel, the clamp to the four nearest texels, and the sharpening limits. Fix any difference found and say what it was.
- **Structure.** "Is it in use for this picture and this shown size" and the menu, key and settings handling are pure logic in `core`, unit-tested. The passes are platform code.
- **Log.** One line each time it starts or stops being used, with the Feed's size and the size shown (the prototype's `Upscale:` line).

## Known unknowns

- Whether the Capture Card passes a 720p Feed through at 720p or scales it to 1080p itself. If the Feed arrives at 1080p on a 1080p screen, the Sharp Upscale has nothing to do. The log line shows the sizes; report what the card does.
- FSR 1 expects a picture in display (gamma) form, which the Viewer's converted picture is. Confirm against AMD's notes while comparing the shader.
- Whether the fixed strength looks over-sharp on film grain or dithering in some games. The user watches for it; a setting is a later ticket if needed.

## Out of scope

A strength setting; enlarging to a fixed 1080p; FSR 2 or 3 (they need data a video Feed does not have); AMD's driver upscalers; sharpening when the picture is not enlarged; a 480p mode.

## Acceptance criteria

- [ ] U toggles the Sharp Upscale, a held U once; the short line confirms it.
- [ ] View > Sharp upscale shows the state and the key and does the same as U. Help > Hotkeys lists U.
- [ ] With a Feed smaller than the size shown, the log shows it in use with both sizes; switching it off logs that too.
- [ ] With the picture at its own size or smaller, the plain path is used whatever the choice.
- [ ] Resizing the window and going in and out of fullscreen keep it correct at every size, with no flicker or wrong-size picture.
- [ ] It applies to Generated Frames as well as real ones once Frame Generation exists, and texts are drawn after it.
- [ ] If the passes cannot be set up, the plain stretch is used and the log says why.
- [ ] The choice is remembered across restarts; off on a fresh install; a damaged value means off.
- [ ] The shader has been compared with AMD's published FSR 1 source; differences found are fixed and listed in the report; AMD's MIT notice is in the repo.
- [ ] With the Sharp Upscale off, the picture and the present count are exactly as before this ticket.
- [ ] Unit tests cover: the toggle; the menu label and check mark; the settings round trip and damaged values; the "in use or not" rule for a set of Feed and shown sizes.
- [ ] `docs/DEVELOPING.md` describes the two passes, the key and the log line.

## How to verify

Yourself: build, run the unit tests, check that both passes compile, one short smoke run (own `--settings` file) and read the `Upscale:` line.

For the user. End your work by reporting this checklist:

1. Set the Xbox to 720p. Start GamePlayer, press F11.
2. Stand still in a game. Press U back and forth. Is text and the HUD crisper with it on?
3. Any halos or glitter on edges, or grain that looks too sharp?
4. Leave fullscreen and resize the window. Still fine?
5. Close GamePlayer with it on and start again. Still on?
