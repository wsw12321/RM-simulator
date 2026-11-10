@echo off
rem RMUC Tactics Lab release launcher for Windows. Finds Python 3;
rem tools\release\launch.py then checks version and platform, starts the
rem local-only server and opens the browser once it answers.
rem   start.bat                  http://127.0.0.1:8765
rem   start.bat --port 8766
rem   start.bat --no-browser
rem Set PYTHON to choose an interpreter, e.g. set PYTHON="C:\Python312\python.exe"
setlocal
cd /d "%~dp0"
rem UTF-8 mode: source files and output are UTF-8 regardless of the ANSI code page.
set "PYTHONUTF8=1"
if defined PYTHON goto run
where py >nul 2>nul
if not errorlevel 1 (
  set "PYTHON=py -3"
  goto run
)
where python >nul 2>nul
if not errorlevel 1 (
  set "PYTHON=python"
  goto run
)
echo Python 3.10 or newer was not found.
echo Install it from https://www.python.org/downloads/ and run start.bat again.
pause
exit /b 1
:run
%PYTHON% tools\release\launch.py %*
set "CODE=%ERRORLEVEL%"
if not "%CODE%"=="0" pause
exit /b %CODE%
