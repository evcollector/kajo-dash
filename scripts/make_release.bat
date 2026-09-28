@echo off
setlocal EnableExtensions

rem Double-click to package the version in include\config.h: a test package
rem for your own boards, or a release. The builder checks everything first and
rem shows the plan; after that it only needs the signing-key password.
rem Arguments are passed through, for example --test or --release --yes (see
rem make_release.py --help).

for %%I in ("%~dp0..") do set "PROJECT_DIR=%%~fI\"
set "VENV_DIR=%PROJECT_DIR%.venv"
set "VENV_PY=%VENV_DIR%\Scripts\python.exe"
set "REQUIREMENTS=%PROJECT_DIR%tools\firmware_update\requirements.txt"
set "BUILDER=%PROJECT_DIR%tools\firmware_update\make_release.py"

if not exist "%BUILDER%" (
  echo Release builder not found: %BUILDER%
  goto finish
)

rem The same environment kajo.bat's uploader fallback creates, so either one
rem can set it up on a fresh clone.
if exist "%VENV_PY%" goto check_dependencies
echo Creating the project Python environment in .venv...
set "PY_LAUNCHER="
py -3 -c "import sys" >nul 2>nul
if not errorlevel 1 set "PY_LAUNCHER=py -3"
if defined PY_LAUNCHER goto create_environment
python -c "import sys" >nul 2>nul
if not errorlevel 1 set "PY_LAUNCHER=python"
if defined PY_LAUNCHER goto create_environment
if exist "%USERPROFILE%\.platformio\penv\Scripts\python.exe" set PY_LAUNCHER="%USERPROFILE%\.platformio\penv\Scripts\python.exe"
if not defined PY_LAUNCHER (
  echo Python 3 was not found. Install it from python.org, or install PlatformIO,
  echo which brings its own, and run this again.
  goto finish
)

:create_environment
%PY_LAUNCHER% -m venv "%VENV_DIR%"
if errorlevel 1 (
  echo Could not create the Python environment in %VENV_DIR%.
  goto finish
)

:check_dependencies
"%VENV_PY%" -c "import bleak, cryptography, esptool" >nul 2>nul
if not errorlevel 1 goto build
echo Installing the release tools' dependencies...
"%VENV_PY%" -m pip install -r "%REQUIREMENTS%"
if errorlevel 1 (
  echo Dependency installation failed. Check the network connection and retry.
  goto finish
)

:build
cd /d "%PROJECT_DIR%"
"%VENV_PY%" "%BUILDER%" %*

:finish
echo.
pause
