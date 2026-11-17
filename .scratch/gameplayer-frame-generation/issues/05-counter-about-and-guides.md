# 05: Counter on two lines, About, guides

**What to build:** With Frame Generation on, the FPS counter tells the user both numbers on two lines: what the game makes and what the Viewer shows. Help > About says what is running. The user's setup guide explains that the Viewer can now do Frame Generation itself and that Lossless Scaling is the other way, never both at once.

**Blocked by:** 02 (Frame Generation 2x), 03 (Frame Generation 4x), 04 (Sharp Upscale).

**Status:** ready-for-agent

## Context

GamePlayer (the "Viewer") now does its own Frame Generation (G: off, 2x, 4x) and Sharp Upscale (U). The FPS counter (F key) still shows one number, the rate of real frames, which hides what Frame Generation does. The user found a single line with a slash confusing and asked for two lines. The setup guide `docs/lossless-scaling-setup.md` still presents Lossless Scaling as the only way to get smooth motion. Read `GLOSSARY.md`, `docs/adr/0003-frame-generation-inside-the-viewer.md`, `docs/DEVELOPING.md` and `.scratch/gameplayer-frame-generation/README.md`.

## Decisions already made

- **The counter with Frame Generation on,** two lines, top-left as today:

  ```
  Game 30
  Shown 120
  ```

- **The counter with Frame Generation off:** one line, exactly as today (for example "30 FPS").
- **"Game"** is the rate of real frames presented (the Game Rate on Auto), as the counter means today.
- **"Shown"** is every frame the Viewer presents in a second, real and Generated. It is the truth about what the Viewer sends, not about what the monitor displays: 4x on a 60 fps game reads 240 on a 144 Hz monitor. The user asked for it this way. Do not cap or correct it.
- **Same rules as today:** blank for the first two seconds after a start or a Feed restart; hidden while a state message shows; the counter never causes a present.
- **If Frame Generation is chosen but not running** (starting, failed, not available), the counter shows one line, as with it off.
- **Help > About** gains: the Generation Factor in use or "off", with the reason when it is not available; the Sharp Upscale state with the Feed's size and the size shown when in use.
- **The setup guide** stays in the user's vertical style (very short lines, one idea per line, no bullets; see the note in `docs/DEVELOPING.md`). It gains a short part near the top: the Viewer can do it alone now, the two keys, "use one or the other, never both", and how to tell which is running. The Lossless Scaling steps stay for the days the user prefers it. Statements nobody has tested keep the guide's "(not confirmed)" tag.
- **Why never both:** Lossless Scaling counts every frame the Viewer presents as a game frame, so it would take Generated Frames for real ones.

## Known unknowns

- Whether two lines fit the existing text place and sizes at the smallest window (480x270 at 100% scaling). Adjust the layout rule if not, and add a test.
- The earlier guide advises keeping AMD's own driver frame generation (AFMF) off for GamePlayer. That advice now matters for in-app Frame Generation too; keep it, tagged as before.

## Out of scope

Any change to how frames are generated or shown; a counter of what the monitor really displays; an automatic limit; measuring Latency; a settings page.

## Acceptance criteria

- [ ] With Frame Generation running, the counter shows "Game N" and "Shown M" on two lines; with a 30 fps Game Rate at 4x that is 30 and 120.
- [ ] With Frame Generation off, starting, failed or unavailable, the counter shows the single line it shows today.
- [ ] "Shown" counts every present, with no cap.
- [ ] The counter is still blank for two seconds after a start or Feed restart, hidden behind state messages, and never causes a present.
- [ ] Both lines are readable at the smallest window and in fullscreen.
- [ ] Help > About shows the Frame Generation and Sharp Upscale state as described.
- [ ] The setup guide explains the in-app way first, keeps the Lossless Scaling steps, says "one or the other", and keeps the user's vertical style.
- [ ] `docs/DEVELOPING.md` and the sprint README are up to date; the README table marks what is done.
- [ ] Unit tests cover: the counter text for off, 2x, 4x, starting and failed; warm-up; the About lines; the layout at the smallest window.

## How to verify

Yourself: build, run the unit tests, one short smoke run with the counter and Frame Generation on (own `--settings` file); the log's once-a-second line carries the counter text.

For the user. End your work by reporting this checklist:

1. Press F, then G. Do you see two lines, Game and Shown?
2. Do the numbers make sense for your game (30 and 60, 30 and 120, 60 and 120)?
3. Press G until off. Back to one line?
4. Open Help > About. Does it say what is running?
5. Read the new top part of the setup guide. Is anything unclear?
6. One evening of normal play with G and U on. Anything that bothers you?
