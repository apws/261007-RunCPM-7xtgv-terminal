@echo off
setlocal
set "XTGV_SHORTCUT_LOG=%~dpn0.log"
call "%~dp0..\..\windows.cmd" launch --target esp32c6-lcd-1-47 --card-mode card %*
exit /b %ERRORLEVEL%
