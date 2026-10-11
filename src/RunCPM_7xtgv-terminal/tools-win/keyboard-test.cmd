@echo off
call "%~dp0windows.cmd" keyboard-probe %*
exit /b %ERRORLEVEL%
