@echo off
rem Build, then run the unit tests. Run from anywhere:  test.bat
setlocal
call "%~dp0build.bat" --target gameplayer_tests || exit /b 1
"%~dp0build\gameplayer_tests.exe"
exit /b %errorlevel%
