@echo off
rem Throwaway prototype build. Run from anywhere:  prototype\build.bat
rem Needs VS Build Tools 2022 with the C++ workload (MSVC + Windows SDK).
setlocal
pushd "%ProgramFiles(x86)%\Microsoft Visual Studio\Installer"
for /f "usebackq delims=" %%i in (`.\vswhere.exe -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSROOT=%%i"
popd
if not defined VSROOT (echo MSVC not found & exit /b 1)
call "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
if not exist "%~dp0out" mkdir "%~dp0out"
cl /nologo /std:c++17 /EHsc /O2 /W4 /DUNICODE /D_UNICODE /Fo"%~dp0out\\" /Fe"%~dp0out\gameplayer-prototype.exe" "%~dp0main.cpp" ^
  /link /SUBSYSTEM:WINDOWS d3d11.lib dxgi.lib d3dcompiler.lib mfplat.lib mf.lib mfreadwrite.lib mfuuid.lib ole32.lib oleaut32.lib user32.lib gdi32.lib shell32.lib avrt.lib uuid.lib
exit /b %errorlevel%
