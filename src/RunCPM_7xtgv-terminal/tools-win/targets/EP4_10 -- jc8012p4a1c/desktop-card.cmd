@echo off
setlocal
set "XTGV_SHORTCUT_LOG=%~dpn0.log"
call "%~dp0..\..\windows.cmd" launch --target jc8012p4a1c --card-mode card %*
exit /b %ERRORLEVEL%
