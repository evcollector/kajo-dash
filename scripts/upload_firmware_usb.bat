@echo off
setlocal

for %%I in ("%~dp0..") do set "PROJECT_DIR=%%~fI\"
set "PIO=pio"
set "UPLOAD_PORT="

if not "%~1"=="" (
  set "UPLOAD_PORT=%~1"
)

where pio >nul 2>nul
if errorlevel 1 (
  set "PIO=%USERPROFILE%\.platformio\penv\Scripts\pio.exe"
)

if not exist "%PIO%" (
  echo PlatformIO CLI was not found.
  echo Install PlatformIO or check this path:
  echo   %USERPROFILE%\.platformio\penv\Scripts\pio.exe
  exit /b 1
)

cd /d "%PROJECT_DIR%"

if not defined UPLOAD_PORT (
  for /f "usebackq delims=" %%P in (`powershell -NoProfile -ExecutionPolicy Bypass -Command "$ports = & $env:PIO device list; $current = ''; foreach ($line in $ports) { if ($line -match '^(COM\d+)$') { $current = $matches[1]; continue }; if ($current -and $line -match 'Hardware ID:|Description:') { if ($line -match 'BTHENUM') { $current = ''; continue }; if ($line -match 'USB|CH340|CH910|CP210|Silicon Labs|FTDI|UART|1A86:7523|10C4:EA60') { $current; break } } }"`) do (
    set "UPLOAD_PORT=%%P"
  )
)

if defined UPLOAD_PORT (
  echo Building and uploading LVGL test to %UPLOAD_PORT%...
  "%PIO%" run -e kajo -t upload --upload-port %UPLOAD_PORT%
) else (
  echo Building and uploading LVGL test using PlatformIO auto-detected port...
  "%PIO%" run -e kajo -t upload
)

if errorlevel 1 (
  echo.
  echo Upload failed.
  echo If you see "Wrong boot mode detected", GPIO0/BOOT was not held low during reset.
  echo Hold BOOT, tap RST/EN, keep holding BOOT until "Connecting..." changes, then release BOOT.
  echo If auto-detection picked the wrong port, specify one explicitly, for example:
  echo   upload_lvgl.bat COM4
  exit /b 1
)

echo.
echo LVGL test upload complete.
