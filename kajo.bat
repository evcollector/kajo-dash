@echo off
setlocal EnableExtensions
title KAJO-Dash

rem Front door for the development scripts in scripts\. Deliberately plain
rem batch with no dependencies: it has to work on a fresh clone, before any
rem virtual environment or toolchain exists, so it can point at what is missing.
rem
rem Installing a signed release is built in: over a USB cable for a first
rem install, over Bluetooth for an update. The release packager copies this
rem file as its standalone launcher; without scripts\ it opens a two-choice
rem installer menu instead of the development menu.

rem Resolve everything from %0 here, before any SHIFT: SHIFT moves %1 into %0,
rem after which %~dp0 means the current directory rather than this file's.
for %%I in ("%~dp0.") do set "PROJECT_DIR=%%~fI"
set "SELF_NAME=%~nx0"
set "SCRIPTS=%PROJECT_DIR%\scripts"
rem The companion app is closed source and kept in a separate private
rem repository, checked out as android-companion\. Its option only appears
rem when that checkout provides this entry point.
set "APP_HOOK=%PROJECT_DIR%\android-companion\kajo-app.bat"

set "UPLOAD_MODE=ble"
if /i "%~1"=="--ble" (
  shift
  goto updater
)
if /i "%~1"=="--usb" (
  set "UPLOAD_MODE=usb"
  shift
  goto updater
)
if /i "%~1"=="--help" goto updater
if /i "%~1"=="/?" goto updater
if not exist "%SCRIPTS%\" (
  if "%~1"=="" goto installer
  goto updater
)

:menu
rem Re-read on every redraw: a release (option 5) moves config.h on to the next
rem version and adds a row to RELEASES.md.
set "FW_NAME=?"
set "FW_CODE=?"
set "LAST_RELEASE=none yet"
for /f "tokens=3" %%V in ('findstr /c:"#define CYD_FIRMWARE_VERSION_NAME" "%PROJECT_DIR%\include\config.h" 2^>nul') do set "FW_NAME=%%~V"
for /f "tokens=3" %%V in ('findstr /c:"#define CYD_FIRMWARE_VERSION_CODE" "%PROJECT_DIR%\include\config.h" 2^>nul') do set "FW_CODE=%%V"
set "FW_CODE=%FW_CODE:UL=%"
rem RELEASES.md lists releases oldest first, so the last row read is the newest.
for /f "tokens=1-3 delims=| " %%A in ('findstr /r /c:"^| *[0-9][0-9.]* *| *[0-9]" "%PROJECT_DIR%\RELEASES.md" 2^>nul') do set "LAST_RELEASE=%%A (version code %%B, %%C)"
cls
echo.
echo   KAJO-Dash %FW_NAME% in development   (version code %FW_CODE%)
echo   Last release: %LAST_RELEASE%
echo   ==========================================
echo.
echo   Build and run
echo     1. Simulator             interactive virtual display
echo     2. Preview renders       regenerate preview_output\lvgl
echo     3. Layout editor         edit tools\layout.json
echo.
echo   Firmware
echo     4. Flash over USB        build and install on a connected display
echo     5. Package firmware      test package, or a release to publish
echo     9. Upload over Bluetooth install this version's signed package
echo.
echo   Test senders
echo     6. Fake VESC             flash a second board as a VESC
echo     7. Fake FarDriver        flash a second board as a FarDriver
echo.
if exist "%APP_HOOK%" (
  echo   Companion app
  echo     8. Install on phone      build the APK and adb install it
  echo.
)
echo     Q. Quit
echo.

rem The key list stays fixed even without the app: CHOICE reports a key by its
rem position, so dropping 8 would shift 9 and Q onto the wrong branches. Without
rem the app, 8 just redraws the menu.
choice /c 123456789Q /n /m "  Choose: "
if errorlevel 255 goto quit
if errorlevel 10 goto quit
if errorlevel 9 (
  cls
  set "UPLOAD_MODE=ble"
  set "FROM_MENU=1"
  call :updater
  set "FROM_MENU="
  goto menu
)
if errorlevel 8 (
  if exist "%APP_HOOK%" call :run "%APP_HOOK%"
  goto menu
)
if errorlevel 7 (
  call :run "%SCRIPTS%\upload_fardriver_test.bat"
  goto menu
)
if errorlevel 6 (
  call :run "%SCRIPTS%\upload_vesc_test.bat"
  goto menu
)
if errorlevel 5 (
  call :run "%SCRIPTS%\make_release.bat"
  goto menu
)
if errorlevel 4 (
  call :run "%SCRIPTS%\upload_firmware_usb.bat"
  goto menu
)
if errorlevel 3 (
  call :run "%SCRIPTS%\run_layout_editor_lvgl.bat"
  goto menu
)
if errorlevel 2 (
  call :run "%SCRIPTS%\run_preview_lvgl.bat"
  goto menu
)
if errorlevel 1 (
  call :run "%SCRIPTS%\run_simulator_lvgl.bat"
  goto menu
)
rem CHOICE returns zero if interrupted. Exit instead of launching a tool.
goto quit

:run
cls
if not exist "%~1" (
  echo Script not found: %~1
  echo.
  pause
  exit /b 1
)
call "%~1"
rem The scripts pause on their own where it matters, so no second pause here.
exit /b 0

:quit
endlocal
exit /b 0

rem The release ZIP's front door. A first install needs the cable; once
rem KAJO-Dash runs, an update needs nothing but the display's Bluetooth Link.
:installer
cls
echo.
echo   KAJO-Dash firmware
echo   ==================
echo.
echo     1. Install over USB cable    first install, or a display that no longer starts
echo     2. Update over Bluetooth     the display already runs KAJO-Dash
echo.
echo     Q. Quit
echo.
choice /c 12Q /n /m "  Choose: "
if errorlevel 3 goto quit
if errorlevel 2 (
  set "UPLOAD_MODE=ble"
  goto installer_run
)
if errorlevel 1 (
  set "UPLOAD_MODE=usb"
  goto installer_run
)
rem CHOICE returns zero if interrupted.
goto quit

:installer_run
cls
set "FROM_MENU=1"
call :updater
set "FROM_MENU="
goto installer

:updater
setlocal EnableExtensions EnableDelayedExpansion

set "PACKAGE_DIR=%PROJECT_DIR%\"
set "UPLOADER_EXE=%PACKAGE_DIR%KAJO Firmware Uploader.exe"
set "VENV_DIR=%PACKAGE_DIR%.venv"
set "VENV_PY=%VENV_DIR%\Scripts\python.exe"
set "REQUIREMENTS=%PACKAGE_DIR%tools\firmware_update\requirements.txt"
set "UPLOADER_PY=%PACKAGE_DIR%tools\firmware_update\upload_firmware.py"
set "MANIFEST="
set "FORWARD_ARGS="
set "KEEP_OPEN="
set "EXIT_CODE=0"
set "REQUIRED_VERSION="
if exist "%PACKAGE_DIR%include\config.h" for /f "tokens=3" %%V in ('findstr /c:"#define CYD_FIRMWARE_VERSION_CODE" "%PACKAGE_DIR%include\config.h"') do set "REQUIRED_VERSION=%%V"
set "REQUIRED_VERSION_NAME=this version"
if exist "%PACKAGE_DIR%include\config.h" for /f "tokens=3" %%V in ('findstr /c:"#define CYD_FIRMWARE_VERSION_NAME" "%PACKAGE_DIR%include\config.h"') do set "REQUIRED_VERSION_NAME=%%~V"
if defined REQUIRED_VERSION set "REQUIRED_VERSION=!REQUIRED_VERSION:UL=!"

rem A public release ZIP contains this launcher, the standalone uploader EXE,
rem one signed JSON/.bin/.zlib release set, and that release's USB layout
rem (-usb.json with its bootloader, partition table and boot_app0 images).
rem No private key is distributed.
if "%~1"=="" set "KEEP_OPEN=1"
if /i "%~1"=="--help" goto upd_usage
if /i "%~1"=="/?" goto upd_usage

if "%UPLOAD_MODE%"=="usb" (
  set "MODE_ARGS=--usb"
  echo Install over USB cable
) else (
  set "MODE_ARGS=--reboot"
  echo Update over Bluetooth
)
echo.

cd /d "%PACKAGE_DIR%"

if "%~1"=="" goto upd_find_manifest
set "FIRST_ARGUMENT=%~1"
if "%FIRST_ARGUMENT:~0,2%"=="--" goto upd_find_manifest
set "MANIFEST=%~1"
shift

:upd_find_manifest
if defined MANIFEST goto upd_collect_arguments
for /f "delims=" %%F in ('dir /b /a-d /o-d "%PACKAGE_DIR%*.json" 2^>nul') do call :upd_consider_manifest "%PACKAGE_DIR%%%F"
for /f "delims=" %%F in ('dir /b /a-d /o-d "%PACKAGE_DIR%releases\*.json" 2^>nul') do call :upd_consider_manifest "%PACKAGE_DIR%releases\%%F"
rem Test packages from option 5 carry the version code of the release they lead up to.
for /f "delims=" %%F in ('dir /b /a-d /o-d "%PACKAGE_DIR%releases\test\*.json" 2^>nul') do call :upd_consider_manifest "%PACKAGE_DIR%releases\test\%%F"
if defined MANIFEST goto upd_collect_arguments


echo No compatible signed firmware release was found beside this launcher.
echo.
if exist "%PACKAGE_DIR%scripts\" (
  echo Make a test package or a release of %REQUIRED_VERSION_NAME% with menu option 5,
  echo then try again.
) else (
  echo Download and extract the complete Windows updater ZIP from the project's
  echo GitHub Releases page, then run this launcher from the extracted folder.
)
echo Individual source files or older protocol manifests cannot be uploaded.
set "EXIT_CODE=1"
goto upd_finish

:upd_consider_manifest
if defined MANIFEST exit /b 0
rem A USB install also needs the release's layout file beside it.
if "%UPLOAD_MODE%"=="usb" if not exist "%~dpn1-usb.json" exit /b 0
rem Only a signed manifest carries a signature. releases\ also holds kajo-update.json,
rem which shares protocol and version_code but describes the phone bundle, and the
rem -usb.json layouts, which carry neither.
powershell.exe -NoProfile -Command "try { $m=ConvertFrom-Json ([IO.File]::ReadAllText('%~1')); $required='%REQUIRED_VERSION%'; if ($m.protocol -eq 3 -and $m.signature -and (-not $required -or $m.version_code -eq [int]$required)) { exit 0 } } catch {}; exit 1"
if not errorlevel 1 set "MANIFEST=%~1"
exit /b 0

:upd_collect_arguments
if "%~1"=="" goto upd_validate_manifest
set "FORWARD_ARGS=%FORWARD_ARGS% "%~1""
shift
goto upd_collect_arguments

:upd_validate_manifest
if not exist "!MANIFEST!" (
  echo Signed release manifest was not found:
  echo   !MANIFEST!
  set "EXIT_CODE=1"
  goto upd_finish
)
powershell.exe -NoProfile -Command "try { if ((ConvertFrom-Json ([IO.File]::ReadAllText('!MANIFEST!'))).protocol -eq 3) { exit 0 } } catch {}; exit 1"
if errorlevel 1 (
  echo The selected release uses an unsupported firmware-update protocol:
  echo   !MANIFEST!
  set "EXIT_CODE=1"
  goto upd_finish
)

if exist "%UPLOADER_EXE%" goto upd_upload_exe
if not exist "%UPLOADER_PY%" (
  echo The standalone uploader is missing:
  echo   %UPLOADER_EXE%
  echo Download and extract the complete Windows updater ZIP.
  set "EXIT_CODE=1"
  goto upd_finish
)
goto upd_prepare_python

:upd_prepare_python
if exist "%VENV_PY%" goto upd_check_dependencies
echo Preparing the developer Python fallback...
set "PY_LAUNCHER="
py -3 -c "import sys" >nul 2>nul
if not errorlevel 1 set "PY_LAUNCHER=py -3"
if defined PY_LAUNCHER goto upd_create_environment
python -c "import sys" >nul 2>nul
if not errorlevel 1 set "PY_LAUNCHER=python"
if defined PY_LAUNCHER goto upd_create_environment
if exist "%USERPROFILE%\.platformio\penv\Scripts\python.exe" set PY_LAUNCHER="%USERPROFILE%\.platformio\penv\Scripts\python.exe"
if not defined PY_LAUNCHER (
  echo Python 3 was not found. Public release packages include a standalone EXE;
  echo this source checkout currently has only the developer fallback.
  set "EXIT_CODE=1"
  goto upd_finish
)

:upd_create_environment
%PY_LAUNCHER% -m venv "%VENV_DIR%"
if errorlevel 1 (
  echo Could not create the uploader environment.
  set "EXIT_CODE=1"
  goto upd_finish
)

:upd_check_dependencies
"%VENV_PY%" -c "import bleak, cryptography, esptool" >nul 2>nul
if not errorlevel 1 goto upd_python_ready
echo Installing uploader dependencies...
"%VENV_PY%" -m pip install -r "%REQUIREMENTS%"
if errorlevel 1 (
  echo Dependency installation failed. Check the network connection and retry.
  set "EXIT_CODE=1"
  goto upd_finish
)

:upd_python_ready
goto upd_upload_python


:upd_upload_python
call :upd_announce
"%VENV_PY%" "%UPLOADER_PY%" "!MANIFEST!" %MODE_ARGS% %FORWARD_ARGS%
goto upd_upload_result

:upd_upload_exe
call :upd_announce
"%UPLOADER_EXE%" "!MANIFEST!" %MODE_ARGS% %FORWARD_ARGS%

:upd_upload_result
if errorlevel 1 goto upd_upload_failed
echo.
if "%UPLOAD_MODE%"=="usb" (
  echo Firmware installed and verified. The display is restarting; a display
  echo that has not been set up before opens its first-boot setup.
) else (
  echo Firmware uploaded and verified. The display is restarting automatically.
)
goto upd_finish

:upd_upload_failed
echo.
if "%UPLOAD_MODE%"=="usb" (
  echo Firmware installation failed. Check the cable and run this launcher
  echo again; every attempt writes the complete firmware.
) else (
  echo Firmware upload failed. Leave the display in update mode and run this
  echo launcher again; completed sectors will be resumed automatically.
)
set "EXIT_CODE=1"
goto upd_finish

:upd_announce
echo.
if "%UPLOAD_MODE%"=="usb" (
  echo Looking for the display on a USB serial port...
) else (
  echo Searching for a KAJO-Dash display in firmware-update mode...
)
exit /b 0

:upd_usage
echo KAJO-Dash signed firmware installer
echo.
echo Usage:
echo   "%SELF_NAME%" --usb [release.json] [--port COMx] [--erase-all]
echo   "%SELF_NAME%" --ble [release.json] [uploader options]
if exist "%SCRIPTS%\" echo   "%SELF_NAME%"                 open the development menu
echo.
echo In a release ZIP, double-click the launcher to choose between a USB install
echo and a Bluetooth update.
echo.
echo Omitting release.json selects a compatible protocol 3 release. --usb writes
echo it over a USB cable and keeps the display's settings unless --erase-all is
echo given. --ble discovers the display, updates, verifies, and restarts it.

:upd_finish
if defined KEEP_OPEN (
  echo.
  if "%EXIT_CODE%"=="0" (
    echo Done.
  ) else (
    echo The updater stopped with an error.
  )
  if defined FROM_MENU (
    echo Press any key to return to the menu.
  ) else (
    echo Press any key to close this window.
  )
  pause >nul
)
endlocal & exit /b %EXIT_CODE%
