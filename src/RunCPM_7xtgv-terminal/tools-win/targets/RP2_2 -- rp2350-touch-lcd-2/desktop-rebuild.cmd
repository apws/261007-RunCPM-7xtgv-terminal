@echo off
setlocal
set "XTGV_SHORTCUT_LOG=%~dpn0.log"
call "%~dp0..\..\windows.cmd" build --target rp2350-touch-lcd-2 %*
exit /b %ERRORLEVEL%
