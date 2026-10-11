@echo off
call "%~dp0windows.cmd" storage-profile %*
exit /b %ERRORLEVEL%
