@echo off
call "%~dp0windows.cmd" performance --storage-compare %*
exit /b %ERRORLEVEL%
