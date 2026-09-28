@echo off
setlocal

for %%I in ("%~dp0..") do set "PROJECT_DIR=%%~fI\"
cd /d "%PROJECT_DIR%"

set "PY=py"
"%PY%" -c "import sys" >nul 2>nul
if errorlevel 1 (
  set "PY=python"
  "%PY%" -c "import sys" >nul 2>nul
)
if errorlevel 1 (
  set "PY=%USERPROFILE%\.platformio\penv\Scripts\python.exe"
)

if not exist "%PY%" if /i not "%PY%"=="py" if /i not "%PY%"=="python" (
  echo Python was not found.
  echo Install Python or check this path:
  echo   %USERPROFILE%\.platformio\penv\Scripts\python.exe
  exit /b 1
)

echo Generating C++ layout constants...
"%PY%" tools\generate_layout_header.py
if errorlevel 1 (
  echo.
  echo Layout header generation failed.
  echo Check tools\layout.json for invalid JSON or unsupported values.
  exit /b 1
)

echo Generating LVGL font metrics for the editor...
"%PY%" tools\generate_lvgl_font_metrics.py
if errorlevel 1 (
  echo.
  echo LVGL font metric generation failed.
  echo Check src\lvgl_app\lv_font_*.c for generated LVGL font descriptors.
  exit /b 1
)

echo Building the headless LVGL renderer and capturing the real framebuffer...
"%PY%" tools\render_lvgl_native.py
if errorlevel 1 (
  echo.
  echo Native LVGL preview generation failed.
  echo The renderer needs CMake, Visual Studio C++ Build Tools, and Pillow.
  echo If Pillow is missing, install it with:
  echo   "%PY%" -m pip install pillow
  echo If .pio\libdeps\kajo\lvgl is missing, first run:
  echo   pio run -e kajo
  exit /b 1
)

echo.
echo LVGL preview images are in:
echo   %PROJECT_DIR%preview_output\lvgl
echo.
echo Contact sheet:
echo   %PROJECT_DIR%preview_output\lvgl\contact_sheet.png

if /i "%~1"=="--open" (
  start "" "%PROJECT_DIR%preview_output\lvgl\contact_sheet.png"
)
