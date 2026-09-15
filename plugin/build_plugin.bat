@echo off
REM build_plugin.bat - build OpenRGBLenovoGPUPlugin.dll
REM
REM RUN FROM: "x64 Native Tools Command Prompt for VS 2019" (or 2022)
REM           NOT a normal cmd, NOT PowerShell, NOT MSYS2.
REM           The VS prompt is what puts nmake and the MSVC compiler on PATH.
REM
REM   build_plugin.bat <QtDir> <OpenRGBSourceDir>
REM
REM   QtDir            e.g. C:\Qt\5.15.2\msvc2019_64
REM   OpenRGBSourceDir e.g. C:\src\OpenRGB
REM
REM The Qt version MUST match the OpenRGB binary you will load this into.
REM Check which Qt your OpenRGB uses by looking for Qt5Core.dll / Qt6Core.dll
REM in its install folder.

setlocal

if "%~2"=="" (
  echo.
  echo   Usage: build_plugin.bat ^<QtDir^> ^<OpenRGBSourceDir^>
  echo.
  echo   Example:
  echo     build_plugin.bat C:\Qt\5.15.2\msvc2019_64 C:\src\OpenRGB
  echo.
  exit /b 1
)

set "QTDIR=%~1"
set "ORGB=%~2"

if not exist "%QTDIR%\bin\qmake.exe" (
  echo   ERROR: no qmake.exe at "%QTDIR%\bin"
  echo   Point the first argument at a Qt kit dir like C:\Qt\5.15.2\msvc2019_64
  exit /b 1
)

if not exist "%ORGB%\OpenRGBPluginInterface.h" (
  echo   ERROR: OpenRGBPluginInterface.h not found in "%ORGB%"
  echo   Point the second argument at the OpenRGB SOURCE tree.
  exit /b 1
)

where nmake >nul 2>&1
if errorlevel 1 (
  echo   ERROR: nmake not found.
  echo   Open "x64 Native Tools Command Prompt for VS" and run this from there.
  exit /b 1
)

set "PATH=%QTDIR%\bin;%PATH%"

echo Qt      : %QTDIR%
echo OpenRGB : %ORGB%
echo.
"%QTDIR%\bin\qmake.exe" -query QT_VERSION
echo.

echo === cleaning ===
if exist Makefile      nmake distclean >nul 2>&1
if exist release        rmdir /s /q release
if exist debug          rmdir /s /q debug

echo === qmake ===
"%QTDIR%\bin\qmake.exe" OPENRGB_PATH="%ORGB%" OpenRGBLenovoGPUPlugin.pro
if errorlevel 1 ( echo   qmake FAILED & exit /b 1 )

echo === nmake ===
nmake
if errorlevel 1 ( echo   BUILD FAILED - send Claude the errors above & exit /b 1 )

echo.
for /r %%F in (OpenRGBLenovoGPUPlugin.dll) do (
  echo   Built: %%F
  echo.
  echo   Copy it to:  %%APPDATA%%\OpenRGB\plugins\
  echo   Then restart OpenRGB AS ADMINISTRATOR.
)
