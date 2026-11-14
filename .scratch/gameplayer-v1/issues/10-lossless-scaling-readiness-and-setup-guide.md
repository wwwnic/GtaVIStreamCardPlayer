# 10: Lossless Scaling readiness and user setup guide

**What to build:** The user gets smooth 60 fps motion from 30 fps Console games by running the Steam app Lossless Scaling on top of GamePlayer, after a one-time setup, without doing anything per session. This ticket has two parts: confirm (and where needed adjust) that the GamePlayer window is a dependable target for Lossless Scaling, and write a short setup guide that the user will follow once.

**Blocked by:** 04 (Unique Frames and Game Rate detection) and 05 (Fullscreen and window behaviour).

**Status:** ready-for-agent

**Gate outcome:** The user could not find a way to run a game at 30 fps, so bit-identical Repeated Frames at 30 fps remain unverified; ticket 04 is built with a tunable tolerance and diagnostic logging instead. The user will judge Lossless Scaling by taking a 60 fps game to 120 on their 144 Hz monitor.

**Main expected use: 60 to 120.** LSFG fixed X2 on a 60 fps Game Rate, monitor at 144 Hz, is the user's main expected use. The setup guide and its checks (Part 1, the guide's daily-use, expectations and FreeSync sections, and the user checklist) must cover it alongside 30 to 60: for example, a base of 60 and an output of 120 in Lossless Scaling's counter, the delay at a 60 fps base, and whether 120 on a 144 Hz monitor needs the 120 Hz step.

## Context

GamePlayer is a small personal Windows 11 app (the "Viewer") that shows a game Console's picture and sound from a USB Capture Card on the PC monitor. The user wants Frame Generation (30 to 60 fps). It was decided that the Viewer does not generate frames itself; the separate app Lossless Scaling (Steam app id 993090, its frame generation feature is called LSFG) does it, drawing its own output window over the Viewer. The Viewer never launches, configures or controls Lossless Scaling.

Read these first:

- `GLOSSARY.md`.
- `docs/adr/0002-frame-generation-stays-outside-the-viewer.md`: the decision and its consequences.
- `docs/research/capture-viewer-research.md`: read the section titled "Lossless Scaling automation" in full. It contains everything found about profiles, auto scale, start-up, capture modes and suggested settings, each with its source and an indication of what could not be verified. That section is the factual basis for the guide; do not go beyond it without saying so.
- The prototype's findings note from ticket 01, `prototype/FINDINGS.md`: once the user has done the hand test, it records what actually happened when they ran Lossless Scaling on the prototype window.

Hardware: Xbox Series S; AVerMedia Live Gamer Ultra S GC553 Pro over USB; AMD Radeon RX 9070; 1080p 144 Hz FreeSync SDR monitor; Windows 11.

## Part 1: make sure the window is a good target

Check each point in the app as it exists after tickets 04 and 05, fix what is not true, and record the result:

- **Stable identity for profile matching.** Lossless Scaling matches a profile by executable path, executable name or window title. The executable must be named GamePlayer and the window title must be exactly "GamePlayer" at all times: no frame-rate counters, device names or state text appended to the title. If an earlier ticket put debug numbers in the title, move them elsewhere (the About dialog, or a debug-only switch that is off by default).
- **Borderless only.** Confirm there is no path to exclusive fullscreen (no `SetFullscreenState`, DXGI Alt+Enter disabled). Lossless Scaling supports windowed and borderless windows only.
- **Only Unique Frames are presented.** Confirm that with a 30 fps game the Viewer presents about 30 times a second, and that nothing else causes extra presents while the picture is unchanged: no idle redraw loop, no per-frame overlay animation, no repaint timer, and no presents added by ticket 04's FPS counter (F key) when it is on. Each extra present would be counted by Lossless Scaling as a game frame. Occasional repaints on resize or when the on-screen feedback text appears are acceptable.
- **Cursor hidden in fullscreen.** Confirm the 2-second cursor hide from ticket 05 works; a visible hardware cursor can disable the low-delay presentation path when Lossless Scaling captures with VRR active.
- **Always on top** (if ticket 08 is done): confirm the topmost flag is dropped in fullscreen so it cannot fight Lossless Scaling's output window.

## Part 2: the user setup guide

Write one guide in the repo's `docs` folder for the user to follow once.

**Writing style for the guide, and only for the guide:** the user has ADHD and reads vertically. Use very short lines, about five words at most. One idea per line. No bullet points, no numbered lists with long sentences, no wide tables; plain short lines grouped under short headings. Every step must be something the user does or checks. (This ticket itself is written in normal prose because it is for you; the guide is for them.)

The guide covers, in this order:

1. **Xbox settings**: resolution 1080p; refresh rate 60 Hz; HDR off; VRR off; audio set to stereo uncompressed. Say in one line why (the Capture Card captures 1080p60 SDR, and cannot capture VRR or surround outside AVerMedia's own software).
2. **Capture Card**: update its firmware once using AVerMedia's tool; then close AVerMedia's software, because only one program can use the card at a time.
3. **Lossless Scaling, one-time profile**: create a profile for GamePlayer; set the profile's filter to the window title or executable name; turn on auto scale for that profile so it applies whenever the GamePlayer window becomes active. Suggested starting settings from the research: scaling off (the Feed is already 1080p on a 1080p monitor, so only Frame Generation is wanted); frame generation LSFG 3 in fixed X2 mode; Queue Target 1 (0 is lower delay but less stable); Max Frame Latency 3; Flow Scale 100; Performance mode off; capture API WGC to start with, DXGI as the alternative to try if delay feels high.
4. **Lossless Scaling starts by itself**: enable its minimise-to-tray option and make it start hidden at logon. The research found this is done with a Task Scheduler task running the Lossless Scaling executable with a start-minimised argument at logon with highest privileges (the app runs elevated, so a plain start-up shortcut triggers a prompt). Give the steps briefly.
5. **Daily use**: start GamePlayer; press F11; Frame Generation applies by itself; how to tell it is working (Lossless Scaling's own frame counter should read a base of 30 and an output of 60 in a 30 fps game); how to switch it off for a game where the added delay bothers them (Lossless Scaling's scale hotkey).
6. **Honest expectations**: Frame Generation adds delay because a real frame must be held back, roughly 17-33 ms at a 30 fps base (an estimate, not a measurement); fine for slower games, noticeable in shooters. A 30 fps base is the hardest case, so some artefacts around the HUD and in fast motion are normal.
7. **AMD software**: keep AMD Fluid Motion Frames (AFMF) off for GamePlayer, since two frame generators would conflict; turn off AMD overlays if Lossless Scaling misbehaves.
8. **FreeSync and smoothness**: keep FreeSync on (monitor and AMD software); keep GamePlayer's V-sync switch off; when Lossless Scaling is active its own sync setting decides what reaches the screen; if motion stutters at 144 Hz, set the monitor to 120 Hz in Windows display settings, because 60 divides evenly into 120.
9. **If something is off**: a handful of one-line fixes (base rate shows 60 instead of 30: check Device > Frame rate is Auto; no effect in fullscreen: check the profile filter matches "GamePlayer"; scaling drops when clicking another monitor: turn on Lossless Scaling's Multi Display Mode; settings lost: Lossless Scaling's settings file can corrupt, keep a backup copy).

**Marking certainty in the guide.** Several of the items above are unverified in the research. In the guide, mark each such step with a short tag such as "(not confirmed)" so the user knows to judge by result. From the research, the unverified items are at least: the name, range and default of the auto scale delay setting; the start-minimised argument (reported by users and community documentation, not official); whether Lossless Scaling's hotkey can be triggered by another program; the Flow Scale value for 1080p (official guidance covers only 1440p and 4K); the Queue Target and Max Frame Latency suggestions as applied to a capture viewer; WGC versus DXGI delay (no measurements exist; WGC needs Windows 11 24H2 or later, and DXGI depends on multiplane overlay support that a second monitor can remove); the AFMF conflict (an inference, no source); the effect of AMD Anti-Lag on a capture viewer; behaviour on a flip-model, tearing-allowed source window; and the newest Lossless Scaling version (the research found nothing newer than LSFG 3.1 with a July 2025 hotfix, so menu names may have changed; tell the user to look for the nearest equivalent). Use the prototype findings to upgrade any of these to confirmed where the user actually saw it work.

Do not invent setting names, default values or version numbers. If the research does not name something precisely, describe it in words ("the option that starts scaling automatically for this profile") rather than guessing a label.

## Decisions already made

- Frame Generation stays outside the Viewer for now, with the door open to building it into the Processor stage later (ADR 0002). Nothing in this ticket builds Frame Generation.
- The Viewer never controls Lossless Scaling: no launching it, no sending it hotkeys, no editing its settings file. No interface for controlling a running instance was found, and the user chose to configure it by hand once.
- AFMF is not part of the plan; the user may try it on their own in AMD's software, but the guide's recommendation is off for GamePlayer.
- No sharpening or upscaling: source and display are both 1080p.
- No formal latency measurement.

## Known unknowns

Beyond the unverified items listed above for the guide:

- Whether auto scale re-triggers correctly when GamePlayer goes from windowed to fullscreen after the profile has already matched, or needs a focus change. If the prototype findings or the user's test show a problem, document the workaround in the guide (for example: press F11 first, then click the window once).
- Whether Lossless Scaling's WGC mode counts the Viewer's presents (as DXGI mode reportedly does) or samples at the desktop compositor's rate. If WGC shows a wrong base rate with Unique Frames presented, the guide should recommend DXGI instead.

If any Part 1 check cannot be made true without changing a decision from another ticket, do not change it silently; report it.

## Out of scope

Any code that talks to Lossless Scaling; in-app Frame Generation, interpolation, optical flow or sharpening; AFMF integration; purchasing or installing Lossless Scaling (the user already owns it on Steam); changing the user's Windows, AMD or Lossless Scaling settings yourself.

## Acceptance criteria

- [ ] The executable is named GamePlayer and the window title is exactly "GamePlayer" in every state, windowed and fullscreen.
- [ ] No code path can enter exclusive fullscreen.
- [ ] With a 30 fps game the Viewer presents about 30 times per second, and no idle redraws, timers or overlay animations add presents while the picture is unchanged.
- [ ] The cursor hides in fullscreen and the topmost flag is not set in fullscreen.
- [ ] A setup guide exists in `docs`, written in the user's vertical style (very short lines, one idea per line, no bullets, no wide tables), covering the nine topics above in order.
- [ ] Every unverified step in the guide carries a visible "not confirmed" style tag, and nothing in the guide is invented beyond the research and the prototype findings.
- [ ] The guide states the added delay of Frame Generation and how to turn it off for a game.
- [ ] The results of the Part 1 checks, including anything changed, are listed in your final report.

## How to verify

Yourself: build and run the unit tests (no regressions); inspect the window title and present counts through your debug log in both windowed and fullscreen modes; re-read the guide against the research section line by line and confirm each claim traces to it or to the prototype findings.

For the user. End your work by reporting this checklist:

1. Follow the guide once from top to bottom. Was any step unclear or impossible? Which one?
2. Restart the PC. Is Lossless Scaling in the tray by itself?
3. Start GamePlayer with a 30 fps game and press F11. Does Frame Generation switch on by itself?
4. Does Lossless Scaling show a base of 30 and an output of 60?
5. Is motion smooth? If not, try the 120 Hz step in the guide and say whether it helped.
6. Does the delay feel acceptable for the game you are playing?
7. Which "not confirmed" steps worked as written, and which did not?
