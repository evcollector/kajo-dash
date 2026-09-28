@echo off
setlocal

for %%I in ("%~dp0..") do set "PROJECT_DIR=%%~fI\"
cd /d "%PROJECT_DIR%"

set "POWERSHELL=pwsh.exe"
where "%POWERSHELL%" >nul 2>nul
if errorlevel 1 set "POWERSHELL=powershell.exe"

where "%POWERSHELL%" >nul 2>nul
if errorlevel 1 (
  echo PowerShell was not found.
  echo Install PowerShell 7 or enable Windows PowerShell, then retry.
  exit /b 1
)

echo Building and starting the KAJO-Dash simulator...
echo The first build downloads SDL2 and can take a few minutes.
echo.

"%POWERSHELL%" -NoProfile -ExecutionPolicy Bypass -File "%PROJECT_DIR%scripts\run-sim.ps1" %*
set "SIMULATOR_EXIT=%ERRORLEVEL%"

if not "%SIMULATOR_EXIT%"=="0" (
  echo.
  echo Simulator startup failed with exit code %SIMULATOR_EXIT%.
  if "%~1"=="" pause
)

exit /b %SIMULATOR_EXIT%
