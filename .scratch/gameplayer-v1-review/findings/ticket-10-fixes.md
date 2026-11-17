# Fixes: ticket 10, Lossless Scaling readiness and setup guide

- Sources: `ticket-10-claude.md` (0 Blocker, 4 Should fix, 5 Nit), `ticket-10-astra.md` (1 Blocker, 5 Should fix; outside the repo, not copied).
- Changed: `docs/lossless-scaling-setup.md` (owned, whole file), `docs/DEVELOPING.md` "Lossless Scaling" section (one clause; left uncommitted, file carries ticket 05's uncommitted edits).
- Documents only, no code: no build or test needed. None was run: this agent had no shell. Nothing committed for the same reason.
- Readiness claims re-checked at the working copy: title `kWindowTitle` = "GamePlayer", set once (`main.cpp` `CreateWindowExW`), no `SetWindowText` in `src/`; `DXGI_MWA_NO_ALT_ENTER` (`presenter.cpp` `CreateDeviceAndSwapChain`), no `SetFullscreenState`; fullscreen `Cover` uses `HWND_TOP`, `ShowChrome(false)` drops topmost; presents only on repaint (changed text, picture shown/hidden), ended text, resize/rebuild or a converted frame (`presenter.cpp` `Run`); same text again does not repaint (`ShowText`); `SetVsync` and `ShowTextWithNextFrame` never repaint. All hold.

## Findings

### Claude F1 + Astra F1 (sync part): Sync Mode statement untagged
- Verdict: accepted.
- Why: research marks "LS output replaces the Viewer's presentation" as [Reasoning] and flip/tearing as [Unverified] (`capture-viewer-research.md`, section 3 "Flip model, tearing, VRR"); ticket 10 lists it.
- Change: guide section 8, "(not confirmed)" after "its Sync Mode decides".

### Claude F2: filter `GamePlayer` may match other windows
- Verdict: accepted.
- Why: research says the filter takes exe name, path or title, match rule unknown. "About GamePlayer", "GamePlayer hotkeys" (`main.cpp`) and Explorer folders carry the word; the exe name can only match GamePlayer's own windows.
- Change: section 3 now types `GamePlayer.exe`; title kept as the alternative with "may also match folders", tagged.

### Claude F3: Task Scheduler stops the task after 3 days
- Verdict: accepted.
- Why: confirmed in Microsoft Learn, `TaskSettings.ExecutionTimeLimit`: "By default, a task will be stopped 72 hours after it starts to run."
- Change: section 4 steps 13 to 16 (Settings tab, untick `Stop the task if it runs longer than`), with why; "Did not start?" now first asks whether Lossless Scaling is in the tray. The AC power condition was not added (desktop PC).

### Claude F4: "Keep GamePlayer.log" without saying a restart replaces it
- Verdict: accepted.
- Why: `log.cpp` opens with `L"w"`.
- Change: section 9 "Base reads 60": "Copy it before restarting. / A new start replaces it."

### Claude F5 + Astra F4: "redraw GamePlayer once" understates
- Verdict: accepted.
- Why: `ShownTexts` presents on appear and on end; a held volume key changes the text each step (only held M and same-text repeats are filtered, ticket 07).
- Change: section 6: "briefly. / Once when shown. / Once when gone. / A held volume key: each step."

### Claude F6 + Astra F1 (paths part): Steam and Task Scheduler names untraced
- Verdict: accepted (Claude's fix). Astra's ask that "Menu names may vary" become a "(not confirmed)" tag: rejected; that line is the tag the guide already uses for navigation, and the names are standard.
- Change: "Menu names may vary." under the Steam block and the task block.

### Claude F7: "Start minimized at Windows startup" split over two lines
- Verdict: accepted.
- Change: "Find this setting:" / the full name in code style / "Turn it off."

### Claude F8: counter also blank after a Feed restart
- Verdict: accepted.
- Why: `DEVELOPING.md` Run section and ticket 04 fixes (`RestartFeed` resets).
- Change: "after a start or reconnect."

### Claude F9: lowest-delay Sync Mode not mentioned
- Verdict: accepted (Nit, one line pair).
- Why: research, community docs: "Off (Allow tearing)" for lowest latency.
- Change: section 8: "Lowest delay: Off (Allow tearing). / It may tear. (not confirmed)".

### Astra F1, other parts (Blocker claimed)
- Severity: Should fix at most. Criterion 6 was missing a few tags, but no step was wrong.
- Version date: accepted. "Newest news found: July 2025. / Yours may be newer."
- "Frame generation starts itself": accepted, tagged. Auto scale was never recorded as tried.
- WGC/DXGI fallback in section 9: accepted, tagged, plus "Unknown which one counts GamePlayer's frames right" (ticket 10 known unknown).
- WGC needs 24H2: accepted, two lines in section 3 ("This PC is newer": build 26200, research section 3).
- Synthetic hotkey unknown: rejected. The guide has no step that sends a hotkey from a program, and the Viewer never does (ticket 10 decision). A tag on an absent step is noise.
- Xbox breadcrumb: rejected, already tagged.
- Second-monitor DXGI tied to MPO: rejected. "DXGI may fail with a second monitor" is the user-facing effect; MPO is not something the user acts on.

### Astra F2: DEVELOPING.md presents-only-Unique-Frames ignores forced 60
- Verdict: accepted.
- Why: `unique_frames.cpp` `case GameRateOverride::Fps60: emit = true;` (30 override emits only Unique Frames).
- Change (uncommitted, ticket 05 file): clause added after "the FPS counter never presents": `; the Frame rate override "60 fps (every frame)" passes Repeated Frames on too, so Lossless Scaling then sees 60`.

### Astra F3: FPS counter counts frames handed over, not presents
- Verdict: rejected for the guide (already judged in ticket 04, deferred there).
- Why: ticket 04 fixes: differs only when the Presenter is slower than the Feed (V-sync on, stalled GPU); "replaced before shown" was 0 in every smoke run; the guide keeps V-sync off. The WGC/DXGI step it worried about is now tagged (above).

### Astra F5: section 9 filter check contradicts exe filter; restore order wrong
- Verdict: accepted.
- Change: "It must say `GamePlayer.exe`. / Or the title `GamePlayer`."; "Settings lost" now "may be corrupted" and a 1-2-3 list: close, copy back, start.

### Astra F6: Xbox "why" reads as a card limit
- Verdict: accepted.
- Why: research section 0: card captures HDR at 1080p60; VRR and 5.1 only through AVerMedia's software. GamePlayer does not use P010 (`capture_choice.h`).
- Change: "Why: GamePlayer uses 1080p60. / GamePlayer shows no HDR. / VRR and surround need / AVerMedia's own software."

## Outside this ticket (not changed)
- Claude: `presenter.cpp` `MakeWindowAssociation` result not checked or logged (ticket 02). Small; not fixed, no shell to build.
- Claude: `prototype/FINDINGS.md:3` still says the manual checks await the user (prototype, ticket 01).

## Hand checks (Lossless Scaling running)
1. Filter `GamePlayer.exe`: does auto scale start on GamePlayer?
2. Open a folder named GamePlayer. Does Lossless Scaling scale it? (Only with the title filter.)
3. Task Scheduler: menu names as written? Settings tab box there?
4. Restart PC: Lossless Scaling in tray?
5. F11 then play: did it start by itself?
6. Hold Up on a still picture: does LS base number jump?
7. Compare GamePlayer counter, LS base, WGC vs DXGI.
8. Sync Mode Off (Allow tearing): lower delay? Tearing?
9. Record the profile settings once it works.
