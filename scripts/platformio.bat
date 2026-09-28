@echo off
rem Finds the PlatformIO CLI for the kajo.bat entries that build firmware, and
rem offers to install it into the project's .venv when there is none.
rem
rem   call "%~dp0platformio.bat"
rem
rem Returns 0 with PIO set to pio.exe and PIO_PYTHON to the Python it runs in
rem (empty if that is not beside it), or 1 with both empty when there is no
rem PlatformIO because the install was declined or failed. It looks on PATH,
rem then in the PlatformIO IDE's own copy, then in this project's .venv.
rem
rem The version it installs is the one CI builds with, pinned in
rem .github\workflows\firmware-builds.yml: change the two together.
setlocal EnableExtensions
set "PIO_VERSION=6.1.19"
for %%I in ("%~dp0..") do set "PROJECT_DIR=%%~fI"
set "VENV_DIR=%PROJECT_DIR%\.venv"
set "FOUND="
set "FOUND_PYTHON="

for /f "delims=" %%P in ('where pio 2^>nul') do if not defined FOUND set "FOUND=%%P"
if defined FOUND goto found
if exist "%USERPROFILE%\.platformio\penv\Scripts\pio.exe" set "FOUND=%USERPROFILE%\.platformio\penv\Scripts\pio.exe"
if defined FOUND goto found
if exist "%VENV_DIR%\Scripts\pio.exe" set "FOUND=%VENV_DIR%\Scripts\pio.exe"
if defined FOUND goto found

echo PlatformIO, which builds the firmware, was not found.
echo.
echo It can be installed now into this project's Python environment, .venv.
echo That takes a minute or two. The first firmware build afterwards downloads
echo the ESP32 toolchain, a few hundred MB, into %USERPROFILE%\.platformio.
echo.
choice /c YN /n /m "Install PlatformIO %PIO_VERSION% now? [Y/N] "
rem CHOICE returns 2 for N, and zero or 255 if interrupted or failing.
if errorlevel 2 goto declined
if not errorlevel 1 goto declined

if exist "%VENV_DIR%\Scripts\python.exe" goto install
echo.
echo Creating the project Python environment in .venv...
set "PY_LAUNCHER="
py -3 -c "import sys" >nul 2>nul
if not errorlevel 1 set "PY_LAUNCHER=py -3"
if defined PY_LAUNCHER goto create_environment
python -c "import sys" >nul 2>nul
if not errorlevel 1 set "PY_LAUNCHER=python"
if defined PY_LAUNCHER goto create_environment
echo Python 3 was not found. Install it from python.org, or install the
echo PlatformIO IDE extension for VS Code, which brings its own, and try again.
goto none

:create_environment
%PY_LAUNCHER% -m venv "%VENV_DIR%"
if errorlevel 1 (
  echo Could not create the Python environment in .venv.
  goto none
)

:install
echo.
"%VENV_DIR%\Scripts\python.exe" -m pip install "platformio==%PIO_VERSION%"
if errorlevel 1 (
  echo.
  echo Installing PlatformIO failed. Check the network connection and try again.
  goto none
)
if not exist "%VENV_DIR%\Scripts\pio.exe" goto none
set "FOUND=%VENV_DIR%\Scripts\pio.exe"
echo.
echo PlatformIO %PIO_VERSION% is installed in .venv.
echo.

:found
rem pip and the PlatformIO installer both put the Python beside pio.exe.
for %%F in ("%FOUND%") do if exist "%%~dpFpython.exe" set "FOUND_PYTHON=%%~dpFpython.exe"
endlocal & set "PIO=%FOUND%" & set "PIO_PYTHON=%FOUND_PYTHON%" & exit /b 0

:declined
echo.
echo PlatformIO was not installed. Install it yourself from
echo https://platformio.org/install, or choose this option again.
:none
endlocal & set "PIO=" & set "PIO_PYTHON=" & exit /b 1
