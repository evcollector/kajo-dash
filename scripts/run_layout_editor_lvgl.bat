@echo off
setlocal
title KAJO-Dash Layout Editor
cd /d "%~dp0.."

set "WRAPPER_PROJECT=%CD%\BrowserWrapper\BrowserWrapper.csproj"
set "WRAPPER_EXE=%CD%\BrowserWrapper\bin\Release\net8.0-windows\KajoLayoutEditor.exe"

where dotnet >nul 2>nul
if errorlevel 1 (
  echo .NET SDK was not found. Falling back to the local Python server plus browser.
  goto browser_fallback
)

echo Building KAJO-Dash Layout Editor desktop wrapper...
dotnet build "%WRAPPER_PROJECT%" -c Release
if errorlevel 1 goto browser_fallback

echo Opening KAJO-Dash Layout Editor desktop app...
start "" "%WRAPPER_EXE%" --lvgl
exit /b 0

:browser_fallback
set "PORT=8765"
set "PY=py"
"%PY%" -c "" >nul 2>nul
if errorlevel 1 set "PY=python"
"%PY%" -c "" >nul 2>nul
if errorlevel 1 (
  echo Python was not found. Opening the editor file directly.
  echo layout.json auto-load and LVGL upload button are blocked on file://.
  start "" "%CD%\tools\layout_editor.html"
  exit /b 0
)

set "URL=http://localhost:%PORT%/tools/layout_editor.html?v=lvgl-previews-20261007"
echo Serving the LVGL layout editor at:
echo   %URL%
echo.
echo This editor targets the active LVGL firmware path:
echo   src\main_lvgl.cpp
echo   src\lvgl_app\
echo   upload_lvgl.bat
echo.
echo Keep this window open while editing. Close it or press Ctrl+C to stop.
start "" /b cmd /c "ping -n 3 127.0.0.1 >nul & start %URL%"
"%PY%" tools\serve_editor.py %PORT%
