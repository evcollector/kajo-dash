@echo off
setlocal EnableExtensions

for %%I in ("%~dp0..\..") do set "PROJECT_DIR=%%~fI"
for %%I in ("%~dp0.") do set "TOOL_DIR=%%~fI"
set "VENV_PY=%PROJECT_DIR%\.venv\Scripts\python.exe"
set "REQUIREMENTS=%~dp0requirements.txt"
set "UPLOADER=%~dp0upload_firmware.py"
set "DIST_DIR=%PROJECT_DIR%\dist\firmware_update"
set "WORK_DIR=%PROJECT_DIR%\.pio\pyinstaller-firmware-uploader"

if not exist "%VENV_PY%" (
  echo Create the project uploader environment first by running
  echo kajo.bat --ble from the source checkout.
  exit /b 1
)

"%VENV_PY%" -m pip install -r "%REQUIREMENTS%" "pyinstaller>=6,<7"
if errorlevel 1 exit /b 1

"%VENV_PY%" -m PyInstaller "%UPLOADER%" --noconfirm --clean --onefile --console --name "KAJO Firmware Uploader" --paths "%TOOL_DIR%" --distpath "%DIST_DIR%" --workpath "%WORK_DIR%" --specpath "%WORK_DIR%"
if errorlevel 1 exit /b 1

echo.
echo Standalone uploader created:
echo   %DIST_DIR%\KAJO Firmware Uploader.exe
