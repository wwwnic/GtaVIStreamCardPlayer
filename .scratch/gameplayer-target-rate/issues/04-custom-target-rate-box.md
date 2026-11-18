# 04: Custom Target Rate box

**What to build:** "Custom…" in the Target rate submenu opens a small box where the user types any Target Rate from 1 to 1000. After OK, Target uses that number, the menu shows "Custom (N)", and the number is saved.

**Blocked by:** 03 (Target rate menu).

**Status:** ready-for-agent

## Context

Read `README.md` and `plan.md` in this folder first. The app has no input box yet; menus are lists of choices and notices are on-screen text. This ticket adds the first small modal box. Keep it plain Win32.

## Decisions already made

- **The box.** Title "Target rate". One number field, pre-filled with the current value; OK and Cancel. Enter accepts, Esc cancels.
- **Validation** (Q10/Q12). Whole numbers 1 to 1000. Anything else disables OK and shows a short hint ("1 to 1000"). The check is pure logic in `core`, tested first.
- **After OK.** Target switches to the number. The menu shows "Custom (N)" checked. Ticket 03's rules apply: rate in use is the smaller of N and the refresh; the uneven notice; below the Game Rate means real frames only.
- **Saved.** The custom number is saved and kept even when a preset is picked later, so "Custom (N)" stays in the menu. A bad saved value falls back to Monitor.
- **Fullscreen.** The box shows on top of the fullscreen window; the Feed keeps playing behind it.

## Out of scope

Typing a rate anywhere else; decimals.

## Acceptance criteria

- [ ] Custom… opens the box, pre-filled; Enter and Esc work.
- [ ] 0, 1001, letters and empty keep OK disabled with the hint.
- [ ] After OK with 100 the counter shows about 100 and the menu shows "Custom (100)".
- [ ] Picking a preset then reopening the menu still shows "Custom (100)".
- [ ] The value survives a restart; a bad saved value means Monitor.
- [ ] The box works in fullscreen without leaving fullscreen.
- [ ] Unit tests cover validation and the settings round trip.
- [ ] `docs/DEVELOPING.md` mentions Custom.

## How to verify

Yourself: build, run the unit tests, one short smoke run with a custom value (own `--settings` file).

For the user. End your work by reporting this checklist:

1. Custom… and type 100. Counter shows about 100?
2. Type 0, then 2000. OK stays disabled?
3. In fullscreen, does the box show on top?
4. Restart. Still Custom (100)?
