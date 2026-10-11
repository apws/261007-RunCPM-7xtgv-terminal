@echo off
setlocal
set "XTGV_SHORTCUT_LOG=%~dpn0.log"
call "%~dp0..\..\windows.cmd" launch --target jc8048w550 --card-mode card %*
exit /b %ERRORLEVEL%
