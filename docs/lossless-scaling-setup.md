# Lossless Scaling setup

## Start with the Viewer alone

GamePlayer can add smoothness itself.

No second app is needed.

G: off, 2x, then 4x.

U: Sharp Upscale on or off.

For a smaller Feed shown larger.

The Viewer remembers both choices.

F shows the counter.

With generation running:

Game 30

Shown 60, or Shown 120.

Game: real frames per second.

Shown: all frames the Viewer sends.

Your monitor may show fewer.

No monitor limit is applied.

Blank for two seconds after starting.

Help > About says what is running.

It also says why generation is unavailable.

The in-app way needs AMD graphics.

## Or use Lossless Scaling

Use one or the other, never both.

Lossless Scaling treats every Viewer frame

as a game frame.

It would take Generated Frames for real ones.

Before using Lossless Scaling:

press G until generation is off.

Check Help > About.

Before using the Viewer's generation:

stop Lossless Scaling's generation.

The steps below are for Lossless Scaling.

Do this setup once.

Then just start GamePlayer.

Lossless Scaling adds the smoothness.

60 fps game becomes 120.

30 fps game becomes 60.

## How to read this

Each line is one thing.

Do it, or check it.

**(not confirmed)** means:

nobody has tested it here.

Judge it by the result.

**(confirmed)** means:

you saw it work.

Menus may have new names.

Research found LSFG 3.1.

Newest news found: July 2025.

Yours may be newer.

Pick the nearest equivalent.

## What already works

You ran Lossless Scaling

on GamePlayer, 60 fps game.

Result: excellent. **(confirmed)**

Which settings you used:

not recorded.

30 fps games: not tested yet.

## 1. Xbox settings

Why: GamePlayer uses 1080p60.

GamePlayer shows no HDR.

VRR and surround need

AVerMedia's own software.

Xbox menu names **(not confirmed)**:

1. Settings > General.
2. TV & display options.
3. Resolution: 1080p.
4. Refresh rate: 60 Hz.
5. Video modes: HDR off.
6. Video modes: VRR off.
7. Back to General.
8. Volume & audio output.
9. HDMI audio: stereo uncompressed.

## 2. Capture Card

Update its firmware once.

Use AVerMedia's firmware tool.

It is on AVerMedia's page

for the GC553 Pro.

Then close AVerMedia's software.

Only one program

can use the card.

## 3. Lossless Scaling profile

Open Lossless Scaling.

1. Create a new profile.
2. Name it GamePlayer.
3. Find the filter field.
4. Type: `GamePlayer.exe`
5. Turn on auto scale.

That is the exe name.

It matches only GamePlayer.

The title `GamePlayer` also works.

But it may also match

folders named GamePlayer.

**(not confirmed)**

Auto scale starts it

when GamePlayer becomes active.

There may be a delay setting.

Name and default unknown.

**(not confirmed)**

### Settings for that profile

Scaling type: Off.

Reason: picture already 1080p.

Frame generation: LSFG 3.

Mode: Fixed.

Multiplier: X2.

One profile covers both:

60 becomes 120.

30 becomes 60.

Queue Target: 1.

0 is quicker.

0 can stutter.

**(not confirmed)** for GamePlayer.

Max Frame Latency: 3.

Advice for AMD cards.

**(not confirmed)** for GamePlayer.

Flow Scale: 100.

No official 1080p advice.

**(not confirmed)**

Performance mode: off.

Capture API: WGC first.

WGC needs Windows 11 24H2.

This PC is newer.

If delay feels high:

try DXGI.

No one measured either.

**(not confirmed)**

DXGI may fail

with a second monitor.

Turn on Draw FPS.

It shows the frame counter.

### Keep a backup

Once it all works:

copy this file somewhere safe.

`C:\Users\<you>\AppData\Local\Lossless Scaling\Settings.xml`

It holds your profiles.

It can get corrupted.

## 4. Start by itself

### In Lossless Scaling

Turn on Minimize to tray.

Find this setting:

`Start minimized at Windows startup`

Turn it off.

The task below replaces it.

Reason: it runs as admin.

Startup would ask every time.

### Find the exe

1. Open Steam.
2. Right-click Lossless Scaling.
3. Manage.
4. Browse local files.
5. Note the folder path.

Menu names may vary.

Usually:

`C:\Program Files (x86)\Steam\steamapps\common\Lossless Scaling\LosslessScaling.exe`

### Make the task

1. Open Task Scheduler.
2. Click Create Task.
3. Name: Lossless Scaling.
4. Tick Run with highest privileges.
5. Tab Triggers: New.
6. Choose At log on.
7. Optional: delay 30 seconds.
8. OK.
9. Tab Actions: New.
10. Start a program.
11. Program: the exe above.
12. Arguments: `-StartMinimized`
13. OK.
14. Tab Settings.
15. Untick this box:

`Stop the task if it runs longer than`

16. OK.

Menu names may vary.

Why step 15:

by default Windows stops it

after 3 days.

`-StartMinimized` comes from users.

Not official.

**(not confirmed)**

## 5. Daily use

1. Start GamePlayer.
2. Press F11.
3. Play.

Frame generation starts itself.

**(not confirmed)**

### Is it working?

Look at Lossless Scaling's counter.

60 fps game: 60 / 120.

30 fps game: 30 / 60.

First number: base.

Second number: output.

GamePlayer's own counter: press F.

With in-app generation off:

it shows one number.

For example: 30 FPS.

Frames shown per second.

Blank for two seconds

after a start or reconnect.

It should match

Lossless Scaling's first number.

### Did not start?

Is Lossless Scaling in the tray?

If not, start it.

Then check the task.

Click the GamePlayer window once.

Or Alt+Tab away and back.

Auto scale needs it active.

F11 after start

may not count. **(not confirmed)**

### Turn it off

Game feels too laggy?

Press the scale hotkey.

Default: Ctrl+Alt+S.

**(not confirmed)**

Change it in Lossless Scaling.

It may come back

when GamePlayer is re-activated.

**(not confirmed)**

## 6. Honest expectations

Frame generation adds delay.

It must hold one real frame.

60 fps base:

about 8 to 17 ms extra.

30 fps base:

about 17 to 33 ms extra.

These are estimates.

Not measurements.

Fine for slower games.

Noticeable in shooters.

60 base is the good case.

30 base is the hard case.

At 30 expect artefacts.

Around the HUD.

In fast motion.

That is normal.

Volume keys and messages

redraw GamePlayer briefly.

Once when shown.

Once when gone.

A held volume key: each step.

A tiny blip. Normal.

## 7. AMD software

Open AMD Software.

AMD Fluid Motion Frames (AFMF):

keep it off for GamePlayer.

That applies to in-app generation too.

Two frame generators would clash.

**(not confirmed)**, a reasoned guess.

Lossless Scaling misbehaves?

Turn off AMD overlays.

Anti-Lag: helps GamePlayer?

Unknown. **(not confirmed)**

## 8. FreeSync and smoothness

Keep FreeSync on.

On the monitor.

In AMD Software.

GamePlayer V-sync: keep off.

Menu: View > V-sync.

The menu bar is at the top.

It hides in fullscreen.

Off is already the default.

GamePlayer remembers your choice.

While Lossless Scaling runs:

its Sync Mode decides.

**(not confirmed)**

Default mode: no tearing.

Lowest delay: Off (Allow tearing).

It may tear. **(not confirmed)**

### 60 to 120 on 144 Hz

120 fits under 144.

FreeSync should handle it.

**(not confirmed)**

### If motion stutters

Set the monitor to 120 Hz.

60 and 30 divide 120 evenly.

1. Windows Settings.
2. System > Display.
3. Advanced display.
4. Refresh rate: 120 Hz.

Menu names may vary.

## 9. If something is off

### Base reads 60, not 30

Check Device > Frame rate.

It should be Auto.

Leave fullscreen to see it.

Still 60?

Keep GamePlayer.log for review.

It sits beside GamePlayer.exe.

Copy it before restarting.

A new start replaces it.

Base far too high?

An overlay may interfere.

Close Discord or AMD overlays.

### No effect in fullscreen

Check the profile filter.

It must say `GamePlayer.exe`.

Or the title `GamePlayer`.

Then click the window once.

### Stops when clicking elsewhere

Turn on Multi Display Mode.

In Lossless Scaling.

### Settings lost

The settings file may be corrupted.

1. Close Lossless Scaling.
2. Copy your backup back.
3. Start Lossless Scaling.

### WGC shows a wrong base

Switch Capture API to DXGI.

**(not confirmed)**

Unknown which one counts

GamePlayer's frames right.

### Screen went dark

Once, the screen turned off.

After about 19 minutes.

Lossless Scaling was in front.

Controller only, no mouse.

GamePlayer now tries harder

to keep it on.

Fixed? **(not confirmed)**

If it happens again:

1. Note the time.
2. Keep GamePlayer.log.
3. Do not restart GamePlayer first.

Check while playing:

1. Open Terminal as admin.
2. Type: `powercfg /requests`
3. Look under DISPLAY.
4. Look for this:

`GamePlayer: a Feed is playing`

In GamePlayer.log look for:

`Display: off`

`Stay awake: still held`

Quick workaround:

1. Windows Settings.
2. System > Power.
3. Screen timeout.
4. Set to Never.

Menu names may vary.

Set it back later

if you want.
