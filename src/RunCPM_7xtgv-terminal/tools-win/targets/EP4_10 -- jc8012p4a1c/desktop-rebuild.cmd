@echo off
setlocal
set "XTGV_SHORTCUT_LOG=%~dpn0.log"
call "%~dp0..\..\windows.cmd" build --target jc8012p4a1c %*
exit /b %ERRORLEVEL%
