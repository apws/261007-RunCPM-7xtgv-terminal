@echo off
setlocal
set "XTGV_SHORTCUT_LOG=%~dpn0.log"
call "%~dp0..\..\windows.cmd" build --target jc8048w550 %*
exit /b %ERRORLEVEL%
