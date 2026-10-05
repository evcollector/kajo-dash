#pragma once
#include <lvgl.h>

#include "app_state.h"

// Screen manager: builds/loads the LVGL screen for the given mode and keeps
// track of the active mode (mirrors screenMode + the draw* functions in
// main.cpp). State-changing widgets trigger an async rebuild.
void uiShow(ScreenMode mode);
ScreenMode uiCurrentScreen();

// Called on a 100 ms timer while the dashboard is visible.
void uiDashboardTick();

// Called on the same timer, from the firmware only: returns a menu screen to
// the dashboard once the vehicle moves or the rider stops touching it. The
// host preview drives screens directly and must never be navigated away from
// the screen it was asked to capture.
void uiAutoReturnTick();

// Called on the same timer: refreshes the live light-sensor readout while
// the DISPLAY submenu is visible.
void uiSensorTick();

// Provided by main_lvgl.cpp: blocking 4-corner touch calibration drawn with
// TFT_eSPI directly; the caller must repaint LVGL afterwards. Returns true
// only when a new calibration passed verification and was saved -- a rejected
// run restores the previous mapping and reports false, which callers use to
// decide whether a follow-up dialog is safe to put on screen.
bool runTouchCalibration();

// Also from main_lvgl.cpp: the shared tail of both recovery paths. Waits for
// the gesture's finger to leave the panel, runs the calibration and re-arms
// the input driver. Returns runTouchCalibration()'s result.
bool runRecoveryCalibration();

#ifdef CYD_LVGL_PREVIEW
void uiPreviewRideReplay(uint32_t position, bool playing=false);
void uiPreviewRideReplayLayout(uint8_t count, const uint8_t fields[4]);
// Jumps to a zoom level about the cursor without the animation.
void uiPreviewRideReplayZoom(uint8_t level);
// The zoom level, the stretch of ride on screen (ms), and whether the reader's
// own columns for it have arrived rather than the overview stretched over it.
void uiPreviewReplayView(uint8_t &zoom, uint32_t &start, uint32_t &end, bool &detail);
// 0 summary, 1 chart layout, 2 the field list for the second chart,
// 3 the delete confirmation.
void uiPreviewRideReplayPopup(uint8_t popup);
// Host-preview controls. These only expose state that is normally changed by
// button events, allowing the headless renderer to capture every menu page.
void uiPreviewSetSettingsCategory(uint8_t category);
void uiPreviewSetSubmenu(SubmenuType type, uint8_t page = 0, bool customSpeed = false);
void uiPreviewSetDashboardMode(DashboardMode mode);
void uiPreviewSetAutoBrightnessPrompt(bool open);
void uiPreviewSetColorPalette(bool open);
void uiPreviewSetGradientPanel(bool open);
void uiPreviewSetDataPanel(bool open);
void uiPreviewSetDataChoice(uint8_t slot);
void uiPreviewSetConfigStep(uint8_t step);
void uiPreviewSetConfigInput(uint8_t input);
void uiPreviewSetVehicleInput(uint8_t field);
void uiPreviewSetGradient(bool enabled, bool horizontal, bool bell, uint8_t position);
void uiPreviewShowClearSdPrompt();
void uiPreviewShowSdClearProgress();
void uiPreviewSetResetConfirm(bool fullReset);
void uiPreviewSetResetCountdown(bool fullReset);
void uiPreviewShowAutoReturnWarning(uint8_t seconds);
void uiPreviewShowHoldBubble(uint8_t percent, int x, int y);
void uiPreviewShowRecoveryHold(uint8_t percent);
void uiPreviewShowRecoveryResetPrompt();
void uiPreviewSetSavedFeedback(bool saved);
void uiPreviewShowDeveloperPrompt();
void uiPreviewShowDeveloperDisablePrompt();
void uiPreviewShowDeveloperToast();
void uiPreviewShowDeveloperAlreadyEnabled();
void uiPreviewShowLoggingNotice(uint8_t variant);
void uiPreviewShowControllerNotice(uint8_t variant);
void uiPreviewShowLightSensorCalibrationPrompt(bool brightStep);
void uiPreviewShowLightSensorNotice(bool failed);
void uiPreviewShowSetupCompleteNotice();
void uiPreviewExpireLinkAlertDelay();
void uiPreviewSetControllerSetup(uint8_t stage, ControllerType type = CONTROLLER_VESC,
                                 ControllerConnection connection = CONTROLLER_CONNECTION_UART);
void uiPreviewSetControllerBackend(ControllerType type, ControllerConnection connection);
void uiPreviewSetFirmwareUpdateUserMode(bool enabled);
#endif
