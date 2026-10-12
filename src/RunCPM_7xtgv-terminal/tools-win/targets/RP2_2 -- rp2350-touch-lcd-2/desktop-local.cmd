@echo off
setlocal
set "XTGV_SHORTCUT_LOG=%~dpn0.log"
call "%~dp0..\..\windows.cmd" launch --target rp2350-touch-lcd-2 --card-mode local --window-scale 2 %*
exit /b %ERRORLEVEL%
