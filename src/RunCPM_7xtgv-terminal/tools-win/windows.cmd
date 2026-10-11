@echo off
setlocal DisableDelayedExpansion
if defined XTGV_SHORTCUT_LOG >"%XTGV_SHORTCUT_LOG%" echo 7xtgv-terminal Windows desktop launcher
rem An explicit root wins. Otherwise prefer the project drive, then system
rem drive, then common installation directories on the remaining drive letters.
if defined XTGV_MSYS2_ROOT goto located
for %%D in ("%~d0" "%SystemDrive%" C: D: E: F: G: H: I: J: K: L: M: N: O: P: Q: R: S: T: U: V: W: X: Y: Z:) do (
    call :probe "%%~D\msys64"
    call :probe "%%~D\msys2"
    call :probe "%%~D\"
)
:located
if not defined XTGV_MSYS2_ROOT goto missing
if not exist "%XTGV_MSYS2_ROOT%\usr\bin\bash.exe" goto missing
if not exist "%XTGV_MSYS2_ROOT%\usr\bin\pacman.exe" goto missing
echo Using MSYS2: "%XTGV_MSYS2_ROOT%"
if defined XTGV_SHORTCUT_LOG >>"%XTGV_SHORTCUT_LOG%" echo Using MSYS2: "%XTGV_MSYS2_ROOT%"
set "MSYSTEM=UCRT64"
set "CHERE_INVOKING=1"
set "MSYS2_PATH_TYPE=minimal"
set "XTGV_WIN_DISPATCH=%~dp0dispatch.sh"
set "XTGV_WIN_DISPATCH=%XTGV_WIN_DISPATCH:\=/%"
"%XTGV_MSYS2_ROOT%\usr\bin\bash.exe" --login "%XTGV_WIN_DISPATCH%" %*
set "XTGV_RESULT=%ERRORLEVEL%"
goto finish
:missing
echo A usable MSYS2 installation was not found.
if defined XTGV_SHORTCUT_LOG >>"%XTGV_SHORTCUT_LOG%" echo FAILED: MSYS2 not found. Install MSYS2 or set XTGV_MSYS2_ROOT.
if defined XTGV_MSYS2_ROOT echo Configured location: "%XTGV_MSYS2_ROOT%".
echo Install it from https://www.msys2.org/ and read tools-win\README.adoc.
echo No Windows PATH change is needed. For a custom folder, run in Command Prompt:
echo   set "XTGV_MSYS2_ROOT=D:\your\msys64"
echo Then run setup.cmd again from the same console.
set "XTGV_RESULT=1"
:finish
if defined XTGV_SHORTCUT_LOG >>"%XTGV_SHORTCUT_LOG%" echo Launcher exit code: %XTGV_RESULT%
if /I "%~1"=="launch" if "%XTGV_RESULT%"=="0" exit /b 0
if not defined XTGV_NO_PAUSE pause
exit /b %XTGV_RESULT%

:probe
if defined XTGV_MSYS2_ROOT exit /b 0
if not exist "%~1\usr\bin\bash.exe" exit /b 0
if not exist "%~1\usr\bin\pacman.exe" exit /b 0
set "XTGV_MSYS2_ROOT=%~f1"
exit /b 0
