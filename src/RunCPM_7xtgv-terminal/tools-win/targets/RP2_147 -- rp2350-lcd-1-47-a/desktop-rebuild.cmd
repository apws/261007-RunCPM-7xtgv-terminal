@echo off
setlocal
set "XTGV_SHORTCUT_LOG=%~dpn0.log"
call "%~dp0..\..\windows.cmd" build --target rp2350-lcd-1-47-a %*
exit /b %ERRORLEVEL%
