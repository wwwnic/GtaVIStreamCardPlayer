# Glossary

**Console**
The game machine producing the picture and sound (an Xbox Series S).

**Capture Card**
The device that brings the Console's picture and sound into the PC.

**Feed**
The live picture and sound arriving from the Capture Card.

**Viewer**
This app. It shows the Feed on the monitor and plays its sound. It does not record or stream.

**Latency**
The delay between the Console producing a moment and that moment appearing on the monitor.

**Signal Rate**
How many frames per second the Console sends to the Capture Card (normally 60).

**Game Rate**
How many different pictures per second the game actually produces (often 30). Can be lower than the Signal Rate.

**Unique Frame**
A frame whose picture differs from the one before it.

**Repeated Frame**
A frame that is a copy of the previous one, sent because the Game Rate is lower than the Signal Rate.

**Frame Generation**
Creating extra in-between frames so motion looks smoother than the Game Rate. The Viewer can do it itself; it can also be left to a separate app running on top of the Viewer.

**Generated Frame**
An in-between frame made by Frame Generation. It was never sent by the Console.

**Generation Factor**
How many frames are shown for each Unique Frame while Frame Generation is on: 2x or 4x. Not used with a Target Rate.

**Target Rate**
How many frames per second the Viewer aims to show while Frame Generation follows a target instead of a Generation Factor. Generated Frames fill whatever the Game Rate leaves, so the shown rate stays steady when the Game Rate drops.

**Rate In Use**
The rate the Viewer actually shows with a Target Rate: the Target Rate, or the monitor's refresh rate when that is lower. When it is not above the Game Rate, only the Console's own frames are shown.

**Night Mode**
Lifting the dark parts of the Feed's picture so night scenes and dark rooms read better, leaving bright parts as they were. Off, low, high or auto (stronger the darker the scene).

**Sharp Upscale**
Showing the Feed's picture larger than it arrived while keeping edges crisp, instead of a plain smooth stretch. A picture shown at exactly its own size gets the sharpening alone.

**Passthrough**
The Capture Card's HDMI output that forwards the Console's picture straight to a TV or monitor, bypassing the PC.

**Audio Delay**
A user-set shift of the sound relative to the picture, adjusted in small steps, to line the two up.

**Video Area**
The part of the Viewer window where the Feed is shown. Also where loading and error messages appear.
