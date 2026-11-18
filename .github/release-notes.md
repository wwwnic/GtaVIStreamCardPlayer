## Run GamePlayer on Windows

Download `GamePlayer.exe` from the release assets and open it on a Windows 11 x64 PC with Direct3D 11 graphics. Connect a UVC capture card that supports uncompressed 1080p60. The executable includes its C++ runtime. You do not need .NET or the Visual C++ Redistributable.

**Windows 11 N editions** require the Media Feature Pack for capture. Add it in Optional features and restart. [Microsoft installation instructions](https://support.microsoft.com/en-us/windows/experience/platform-variants/media-feature-pack-for-windows-n)

**Optional AMD frame generation** uses the AMF runtime supplied by a compatible AMD graphics driver. Basic capture and playback work without AMF.
