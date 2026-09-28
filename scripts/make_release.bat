@echo off
setlocal EnableExtensions

rem Double-click to build, sign and package a firmware release. Every prompt
rem offers a default, so a routine release is a run of Enter presses.

for %%I in ("%~dp0..") do set "PROJECT_DIR=%%~fI\"
set "VENV_PY=%PROJECT_DIR%.venv\Scripts\python.exe"
set "BUILDER=%PROJECT_DIR%tools\firmware_update\make_release.py"

if not exist "%VENV_PY%" (
  echo The project virtual environment is missing:
  echo   %VENV_PY%
  echo.
  echo Create it once with:
  echo   py -3 -m venv .venv
  echo   .venv\Scripts\python -m pip install -r tools\firmware_update\requirements.txt
  goto finish
)

if not exist "%BUILDER%" (
  echo Release builder not found: %BUILDER%
  goto finish
)

cd /d "%PROJECT_DIR%"
"%VENV_PY%" "%BUILDER%"

:finish
echo.
pause
