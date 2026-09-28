@echo off
setlocal
title Fake VESC - CYD Test Firmware
cd /d "%~dp0.."
rem The uploader runs PlatformIO as "python -m platformio", so it needs the
rem Python that PlatformIO is installed in. This sets PIO_PYTHON to it, offering
rem to install PlatformIO when it is missing.
set "RESULT=1"
call "%~dp0platformio.bat"
if errorlevel 1 goto finish
if defined PIO_PYTHON goto upload
echo PlatformIO was found at %PIO%, but not the Python it runs in,
echo which this uploader needs. Install the PlatformIO IDE extension for VS Code,
echo or remove that copy from PATH so a project copy can be installed.
goto finish

:upload
"%PIO_PYTHON%" "%CD%\tools\upload_vesc_test.py" %*
set "RESULT=%ERRORLEVEL%"

:finish
echo.
pause
exit /b %RESULT%
