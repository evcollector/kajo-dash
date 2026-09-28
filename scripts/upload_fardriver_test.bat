@echo off
setlocal
title Fake FarDriver - CYD Test Firmware
cd /d "%~dp0.."
set "PYTHON=%USERPROFILE%\.platformio\penv\Scripts\python.exe"
if not exist "%PYTHON%" (
  echo PlatformIO was not found. Install PlatformIO before using this uploader.
  echo Expected: %PYTHON%
  pause
  exit /b 1
)
"%PYTHON%" "%CD%\tools\upload_fardriver_test.py" %*
set "RESULT=%ERRORLEVEL%"
echo.
pause
exit /b %RESULT%
