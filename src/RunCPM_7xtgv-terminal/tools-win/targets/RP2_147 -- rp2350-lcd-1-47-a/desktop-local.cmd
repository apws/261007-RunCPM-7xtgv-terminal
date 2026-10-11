@echo off
setlocal
set "XTGV_SHORTCUT_LOG=%~dpn0.log"
call "%~dp0..\..\windows.cmd" launch --target rp2350-lcd-1-47-a --card-mode local %*  --window-scale 2
exit /b %ERRORLEVEL%
