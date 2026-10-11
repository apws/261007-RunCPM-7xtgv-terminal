@echo off
call "%~dp0windows.cmd" stage-local %*
exit /b %ERRORLEVEL%
