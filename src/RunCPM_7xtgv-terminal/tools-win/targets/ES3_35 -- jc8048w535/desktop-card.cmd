@echo off
setlocal
set "XTGV_SHORTCUT_LOG=%~dpn0.log"
call "%~dp0..\..\windows.cmd" launch --target jc8048w535 --card-mode card --card-root a: --window-scale 2 %*
exit /b %ERRORLEVEL%
