@echo off
REM Register a logon task that switches the GPU logo on at startup.
REM
REM Runs the script FROM THE FOLDER THIS FILE SITS IN - it does not copy
REM anything, so there is exactly one copy of the tool to maintain. Put this
REM folder somewhere permanent (C:\Tools\LegionGpuRgb) before running it.
REM
REM Confirmed: needs nothing from Lenovo. Just an NVIDIA driver and Python.

setlocal
set "TASK=LegionGpuLogo"
set "SCRIPT=%~dp0legion_gpu_rgb.py"

net session >nul 2>&1
if %errorlevel% neq 0 (
  echo   NOT ELEVATED - right-click this file and "Run as administrator".
  pause
  exit /b 1
)

if not exist "%SCRIPT%" (
  echo   legion_gpu_rgb.py is not in this folder.
  echo   Keep install_startup.bat next to it.
  pause
  exit /b 1
)

echo %~dp0 | find " " >nul && (
  echo   WARNING: this path contains spaces. It should still work, but if the
  echo   task fails to run, move the folder somewhere without spaces.
  echo.
)

for /f "delims=" %%P in ('where pythonw 2^>nul') do set "PYW=%%P"
if not defined PYW for /f "delims=" %%P in ('where python 2^>nul') do set "PYW=%%P"
if not defined PYW (
  echo   Could not find python on PATH.
  pause
  exit /b 1
)

echo Script      : %SCRIPT%
echo Interpreter : %PYW%
echo.

REM 45s delay so the NVIDIA driver and display outputs are up first.
schtasks /create /tn "%TASK%" ^
  /tr "\"%PYW%\" \"%SCRIPT%\" --logo on" ^
  /sc onlogon /delay 0000:45 /rl highest /f >nul 2>&1

if %errorlevel% neq 0 (
  echo   Failed to create the scheduled task.
  pause
  exit /b 1
)

echo   Installed. The logo switches on ~45 seconds after logon.
echo.
echo   Test now, without rebooting:
echo       schtasks /run /tn "%TASK%"
echo.
echo   Remove:
echo       schtasks /delete /tn "%TASK%" /f
echo.
echo   To set a colour at logon too, re-run this after editing the line above,
echo   or edit the task action in Task Scheduler and append:  --color RRGGBB
echo.
echo   NOTE: if you move this folder, re-run this file - the task stores an
echo   absolute path and will silently stop working otherwise.
echo.
pause
