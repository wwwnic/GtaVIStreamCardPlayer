@echo off
rem Build GamePlayer. Run from anywhere:  build.bat
rem Output: build\GamePlayer.exe and build\gameplayer_tests.exe
rem Needs VS Build Tools 2022 with the C++ workload (MSVC, Windows SDK, CMake, Ninja).
setlocal
if defined VSCMD_VER goto :build
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (echo vswhere not found: install VS Build Tools 2022 & exit /b 1)
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSROOT=%%i"
if not defined VSROOT (echo MSVC not found & exit /b 1)
call "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
:build
cmake -S "%~dp0." -B "%~dp0build" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo || exit /b 1
cmake --build "%~dp0build" %* || exit /b 1
exit /b 0
