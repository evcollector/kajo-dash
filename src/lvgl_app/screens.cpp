#include "screens.h"
#include "ride_replay.h"
#include <Preferences.h>
#include <new>
#include <math.h>

#include "firmware_changelog.h"

#include <stdlib.h>
#include <string.h>

#include "cyd_dash_thumbnails_4bit.h"
#include "config.h"
#include "fardriver_ble.h"
#include "firmware_update_ble.h"
#include "companion_ble.h"
#include "vesc_ble.h"
#include "controller_manager.h"
#include "dashboards.h"
#include "display_panel_tuning.h"
#include "ui_common.h"
#include "ui_style.h"
#include "vehicle_fields.h"

static ScreenMode currentScreen = SCREEN_DASHBOARD;
static bool rebuildQueued = false;
static lv_obj_t *developerPrompt = NULL;
// Color changes on dashboard previews must release the old preview before the
// replacement is constructed, otherwise two large dashboards briefly coexist.
static bool lowMemoryRebuildQueued = false;
static lv_obj_t *ldrLiveLabel = NULL;  // live light-sensor readout (DISPLAY submenu)
static lv_obj_t *brightnessReadoutLabel = NULL;  // percent beside the brightness slider
static lv_obj_t *ldrCalibrationLiveLabel = NULL;
static lv_obj_t *touchTestCanvas = NULL;
static lv_obj_t *touchTestBack = NULL;
static lv_obj_t *touchTestTitle = NULL;
static lv_obj_t *touchTestClear = NULL;
static lv_obj_t *touchTestInstruction = NULL;
static lv_timer_t *touchTestChromeTimer = NULL;
static uint32_t touchTestLastInputMs = 0;
static bool touchTestChromeHidden = false;
static lv_obj_t *autoReturnOverlay = NULL;
static lv_obj_t *autoReturnProgress = NULL;
static lv_obj_t *autoReturnCountdownLabel = NULL;
static int autoReturnShownSeconds = -1;
static bool firmwareUpdateUserMode = false;
static lv_obj_t *companionStateLabel = NULL;
static lv_obj_t *companionDetailLabel = NULL;
static lv_obj_t *companionTimerLabel = NULL;
static lv_obj_t *companionActionButton = NULL;
static lv_obj_t *companionActionLabel = NULL;
static uint32_t companionUiRevision = 0;
static AccentTheme accentThemeBackup = ACCENT_DEFAULT;  // restored when COLORS exits without SAVE
static bool gradientEnabledBackup = false;
static bool gradientHorizontalBackup = false;
static bool gradientReverseBackup = false;
static bool gradientBellBackup = false;
static uint8_t gradientPositionBackup = 50;
static AccentTheme gradientThemeBackup = ACCENT_DEFAULT;
static DashboardCustomization dashboardCustomizationBackup[MODE_COUNT] = {};
static lv_obj_t *ldrTargetLabel = NULL;
static DashboardMode dashUiPreviewMode = MODE_HUD;
static bool dashUiGridOpen = true;
static uint8_t dashUiGridPage = 0;
static uint8_t dashUiGridReturnPage = 0;
static uint32_t selectorPreviewLastUpdateMs = 0;
static bool selectorPreviewUpdateDue();
static bool selectorSavedFeedback = false;
static bool autoBrightnessPromptOpen = false;
static int pendingLightSensorDarkRaw = -1;
static bool autoAppearanceStateKnown = false;
static bool lastAutoAppearanceLight = false;
static bool autoAppearanceRebuildQueued = false;
static constexpr uint8_t kDashGridPageSize = 4;
static constexpr uint8_t kDashThemeCount = MODE_COUNT;
static constexpr uint8_t kDashGridPageCount = (kDashThemeCount + kDashGridPageSize - 1) / kDashGridPageSize;

enum StatusNotice : uint8_t {
  NOTICE_NONE,
  NOTICE_DEVELOPER_ENABLED,
  NOTICE_DEVELOPER_ALREADY_ENABLED,
  NOTICE_RIDE_SAVED,
  NOTICE_SD_CARD_READY,
  NOTICE_SD_CARD_REMOVED,
  NOTICE_SD_CARD_REMOVED_RECORDING,
  NOTICE_SD_WRITE_FAILED,
  NOTICE_SD_CARD_NOT_READY,
  NOTICE_VESC_PROFILE_SAVED,
  NOTICE_VESC_OFFLINE_SAVED,
  NOTICE_RESTART_DISPLAY_TO_APPLY,
  NOTICE_LIGHT_SENSOR_CALIBRATED,
  NOTICE_LIGHT_SENSOR_RANGE_TOO_SMALL,
  NOTICE_SETUP_COMPLETE,
  NOTICE_CONTROLLER_SETUP_APPLIED,
};

static StatusNotice pendingStatusNotice = NOTICE_NONE;
static RideLoggingStatus loggingNoticeStatus = {};
static bool loggingNoticeStatusKnown = false;
static bool loggingRideSavePending = false;
static lv_obj_t *statusToast = NULL;
static lv_timer_t *statusToastTimer = NULL;
static bool statusToastLeaving = false;
static void showStatusNotice(StatusNotice notice);
static void showPendingStatusNotice();
static void serviceRecoveryHold();
static void showPendingRecoveryPrompt();
static void queueStatusNotice(StatusNotice notice) {
  if (notice != NOTICE_NONE) pendingStatusNotice = notice;
}

// Keep the saved theme at the front of the selector without changing the
// canonical DashboardMode order used by settings, thumbnails, and storage.
// When another theme is saved, the old one automatically falls back into its
// original catalog position.
static int dashGridThemeAt(int displayIndex) {
  const int active = static_cast<int>(dashboardMode);
  if (displayIndex == 0) return active;
  int theme = displayIndex - 1;
  if (theme >= active) theme++;
  return theme;
}

static uint8_t vehiclePage = 0;
static uint8_t speedPage = 0;
static uint8_t displayPage = 0;
// DISPLAY PANEL: profile choice, then tuning over grey and colour ramps.
static constexpr uint8_t kPanelPageCount = 2;
static uint8_t panelPage = 0;
static constexpr uint8_t DISPLAY_USER_PAGE_COUNT = 2;
static uint8_t displayPageCount() { return DISPLAY_USER_PAGE_COUNT; }
static lv_obj_t *farDriverStatusLabel = NULL;
static lv_obj_t *farDriverDetailLabel = NULL;
static lv_obj_t *farDriverPairHintLabel = NULL;
static lv_obj_t *farDriverServiceLabel = NULL;
static lv_obj_t *farDriverCharacteristicLabel = NULL;
static lv_obj_t *farDriverPacketLabel = NULL;
static lv_obj_t *farDriverCountersLabel = NULL;
static lv_obj_t *farDriverDecodedLabel = NULL;
static lv_obj_t *farDriverDisconnectButton = NULL;
static lv_obj_t *farDriverForgetButton = NULL;
static uint32_t farDriverUiRevision = 0;
static void refreshFarDriverBleScreen(bool force = false);
static lv_obj_t *firmwareUpdateStateLabel = NULL;
static lv_obj_t *firmwareUpdateDetailLabel = NULL;
static lv_obj_t *firmwareUpdateProgress = NULL;
static lv_obj_t *firmwareUpdateProgressLabel = NULL;
static lv_obj_t *firmwareUpdateActionButton = NULL;
static lv_obj_t *firmwareUpdateActionLabel = NULL;
static uint32_t firmwareUpdateUiRevision = 0;
static void refreshFirmwareUpdateScreen(bool force = false);
static void refreshCompanionScreen(bool force = false);
// Settings always opens on DISPLAY, its first page. MORE is the second page,
// reached with Next: Vehicle Config for everyone, plus Developer Options once
// they are enabled. CONTROLLER and DEVELOPER are the pages those cards open.
enum SettingsMenuLevel : uint8_t {
  SETTINGS_MENU_MORE,
  SETTINGS_MENU_DISPLAY,
  SETTINGS_MENU_CONTROLLER,
  SETTINGS_MENU_DEVELOPER,
};
static SettingsMenuLevel settingsMenuLevel = SETTINGS_MENU_DISPLAY;
static ControllerType pendingControllerType = CONTROLLER_VESC;
static ControllerConnection pendingControllerConnection = CONTROLLER_CONNECTION_UART;
static const ControllerBackend *pendingControllerBackend() {
  return controllerBackendById(controllerBackendIdFor(pendingControllerType, pendingControllerConnection));
}
static const char *controllerTransportLabel(ControllerTransport transport, bool shortName = false) {
  if (transport == CONTROLLER_TRANSPORT_BLE) return shortName ? "BLE" : "Bluetooth LE";
  return shortName ? "UART" : txt("Wired UART", "UART-kaapeli", "Kabel-UART", "UART câblé",
                                  "UART por cable", "UART cablata");
}
enum ControllerSetupStage : uint8_t { CONTROLLER_SETUP_TYPE, CONTROLLER_SETUP_CONNECTION, CONTROLLER_SETUP_DISCOVERY,
                                     CONTROLLER_SETUP_CONFIRM };
static ControllerSetupStage controllerSetupStage = CONTROLLER_SETUP_TYPE;
static uint32_t controllerSetupRevision = 0;
static uint8_t controllerDeviceOffset = 0;
// A scan the wizard has asked for but cannot start yet, because the running
// backend still holds the display's single Bluetooth client slot.
static bool controllerScanPending = false;

// Entering discovery only asks for the radio. Handing it over means waiting
// for the other backend's worker to disconnect, which the telemetry task does;
// the tick below starts the scan on the pass where the request is granted.
static void requestControllerScan() {
  controllerDeviceOffset = 0;
  controllerScanPending = true;
  const ControllerBackend *backend = pendingControllerBackend();
  if (!backend || !controllerManagerRequestRadioFor(backend->id)) return;
  controllerScanPending = false;
  if (backend->startScan) backend->startScan();
}
// -1 = reset choices, 0 = confirm full reset, 1 = confirm defaults-only.
static int8_t resetConfirmMode = -1;
static bool resetCountdownActive = false;
static int8_t resetCountdownMode = -1;
static uint32_t resetCountdownStartedMs = 0;
static lv_timer_t *resetCountdownTimer = NULL;
static lv_obj_t *resetProgressBar = NULL;
static lv_obj_t *resetProgressLabel = NULL;
static const uint8_t VEHICLE_FIELDS_PER_PAGE = 9;  // 3x3 grid, main-menu style

static uint8_t batteryPage = 0;
static lv_obj_t *rideModeReadout = NULL;
static const uint8_t kVehicleProfileFields[] = {
    VEHICLE_FIELD_OEM, VEHICLE_FIELD_NAME, VEHICLE_FIELD_MOTOR,
    VEHICLE_FIELD_CONTROLLER, VEHICLE_FIELD_BUILD_ID, VEHICLE_FIELD_CONT_KW,
};
static const uint8_t kConnectionFields[] = {VEHICLE_FIELD_VESC_BAUD, VEHICLE_FIELD_VESC_CAN_ID};
// Connection page option beyond the vehicle field ids (the gauge page's
// +10/+11 belong to a different submenu).
static constexpr int kConnectionBluetoothOption = VEHICLE_FIELD_COUNT + 12;
static const uint8_t kCalibrationFields[] = {VEHICLE_FIELD_WHEEL_MM, VEHICLE_FIELD_MOTOR_POLE_PAIRS, VEHICLE_FIELD_DRIVE_RATIO};
static const uint8_t kGaugeFields[] = {SPEED_FIELD_BASE + SPEED_FIELD_TOP_SPEED, VEHICLE_FIELD_PEAK_KW,
                                     VEHICLE_FIELD_BATTERY_MAX_A, VEHICLE_FIELD_MOTOR_MAX_A};
static const uint8_t kPackFields[] = {VEHICLE_FIELD_BATTERY_S, VEHICLE_FIELD_BATTERY_AH};
static const uint8_t kModeLabelFields[] = {VEHICLE_FIELD_MODE_LABEL_1, VEHICLE_FIELD_MODE_LABEL_2, VEHICLE_FIELD_MODE_LABEL_3};

// Some controller fields only mean something for one backend or one link. They
// are hidden rather than greyed out: a FarDriver rider has no CAN id to set,
// and a UART baud rate is not a thing a Bluetooth link has.
static bool controllerFieldApplies(uint8_t field) {
  const ControllerCapabilities &caps = controllerCapabilities();
  switch (field) {
    case VEHICLE_FIELD_VESC_BAUD:
      return caps.usesWiredLink;
    case VEHICLE_FIELD_VESC_CAN_ID:
      return caps.hasCanTarget;
    case VEHICLE_FIELD_MOTOR_POLE_PAIRS:
    case VEHICLE_FIELD_DRIVE_RATIO:
      // Not VESC-only after all: the FarDriver frames carry motor rpm and
      // nothing else, so the pole count and gearing are what turn that into a
      // road speed. Only a controller reporting its own speed makes them idle.
      return !telemetrySpeedFromController();
    default:
      return true;
  }
}

// Copies the applicable entries of a field list into out, which must have room
// for sourceCount of them. Returns how many survived the filter.
static uint8_t collectVisibleFields(const uint8_t *source, uint8_t sourceCount, uint8_t *out) {
  uint8_t count = 0;
  for (uint8_t i = 0; i < sourceCount; i++)
    if (controllerFieldApplies(source[i])) out[count++] = source[i];
  return count;
}

static uint8_t vehicleProfileFieldCount() {
  uint8_t fields[sizeof(kVehicleProfileFields)];
  return collectVisibleFields(kVehicleProfileFields, sizeof(kVehicleProfileFields), fields);
}

// The changelog is one page of its own under Developer options. Four compact
// entries are as much history as is worth reading on the device, so the
// generator compiles in exactly that many and the page never scrolls.
static constexpr uint8_t kChangelogPerPage = 4;

static uint8_t vehiclePageCount() {
  const uint8_t count = vehicleProfileFieldCount();
  return count == 0 ? 1 : (count + VEHICLE_FIELDS_PER_PAGE - 1) / VEHICLE_FIELDS_PER_PAGE;
}

static int vehicleTextField = -1;
static lv_obj_t *vehicleTextArea = NULL;

enum TextInputContext {
  INPUT_VEHICLE_FIELD,
  INPUT_CONFIG_WHEEL,
  INPUT_CONFIG_PIN_NEW,
  INPUT_CONFIG_PIN_CONFIRM,
};

static TextInputContext textInputContext = INPUT_VEHICLE_FIELD;
static char pendingSecurityPin[5] = "";
static bool pinEntryMismatch = false;
static bool pinSetupFromConfigurator = false;
static bool firstBootSetupActive = false;
static void prepareConfiguratorThemeStep();
static void finishConfigurator();

ScreenMode uiCurrentScreen() {
  return currentScreen;
}

// Rebuilds happen async because the trigger is usually a click on a widget
// that lives on the screen being torn down.
static void closeReplayScreen();

static void rebuildCb(void *) {
  closeReplayScreen();
  rebuildQueued = false;
  // actions fire on press-down; ignore the ongoing press so it cannot
  // activate whatever lands under the finger on the rebuilt screen
  lv_indev_t *indev = lv_indev_get_next(NULL);
  if (indev) lv_indev_wait_release(indev);
  if (lowMemoryRebuildQueued) {
    lowMemoryRebuildQueued = false;
    lv_obj_t *old = lv_scr_act();
    if (old) lv_obj_clean(old);
    // Cached icon descriptors allocate from LVGL's small internal heap.  Once
    // the old screen has been cleaned no live object references them, so free
    // them before building another object-heavy screen such as the DASH grid.
    clearIconCache();
  }
  // An automatic light/dark flip only restyles the dashboard already on
  // screen, so it must not replay the entry sweep.
  const bool restyleOnly = autoAppearanceRebuildQueued;
  autoAppearanceRebuildQueued = false;
  if (restyleOnly) setDashboardStartupSweepEnabled(false);
  uiShow(currentScreen);
  if (restyleOnly) setDashboardStartupSweepEnabled(true);
}

static void queueRebuild(ScreenMode mode) {
  // The card-ready logging views carry several status widgets and may also be
  // entered from an icon-heavy menu. Never construct their successor while
  // that object tree is still resident in LVGL's fixed 56 KB heap.
  if ((currentScreen == SCREEN_SUBMENU && submenuType == SUBMENU_LOGGING) || currentScreen == SCREEN_RIDE_LOGS)
    lowMemoryRebuildQueued = true;
  currentScreen = mode;
  if (!rebuildQueued) {
    rebuildQueued = true;
    lv_async_call(rebuildCb, NULL);
  }
}

static void queueLowMemoryRebuild(ScreenMode mode) {
  lowMemoryRebuildQueued = true;
  queueRebuild(mode);
}

// ── Button helpers (drawMenuButton / drawSmallNavButton) ──────────────────────

typedef void (*ButtonAction)(int id);
static ButtonAction currentAction = NULL;

static void buttonEventCb(lv_event_t *e) {
  const int id = (int)(intptr_t)lv_event_get_user_data(e);
  if (currentAction) currentAction(id);
}

static const lv_style_transition_dsc_t *buttonTransition() {
  static bool initialized = false;
  static lv_style_transition_dsc_t transition;
  static const lv_style_prop_t props[] = {
      LV_STYLE_BG_COLOR,
      LV_STYLE_BORDER_COLOR,
      LV_STYLE_SHADOW_WIDTH,
      LV_STYLE_TRANSLATE_Y,
      LV_STYLE_PROP_INV,
  };
  if (!initialized) {
    lv_style_transition_dsc_init(&transition, props, lv_anim_path_ease_out, 90, 0, NULL);
    initialized = true;
  }
  return &transition;
}

static void applyButtonTouchFx(lv_obj_t *btn) {
  lv_obj_set_style_transition(btn, buttonTransition(), 0);
  lv_obj_set_style_transition(btn, buttonTransition(), LV_STATE_PRESSED);
  lv_obj_set_style_bg_color(btn, cyd_ui::activeControlSurface(), LV_STATE_PRESSED);
  lv_obj_set_style_border_width(btn, 2, LV_STATE_PRESSED);
  lv_obj_set_style_border_color(btn, cyd_ui::chromeAccent(), LV_STATE_PRESSED);
  lv_obj_set_style_shadow_width(btn, 8, LV_STATE_PRESSED);
  lv_obj_set_style_shadow_spread(btn, 1, LV_STATE_PRESSED);
  lv_obj_set_style_shadow_color(btn, cyd_ui::chromeAccent(), LV_STATE_PRESSED);
  lv_obj_set_style_shadow_opa(btn, LV_OPA_50, LV_STATE_PRESSED);
  // The pressed border is one pixel thicker than the idle border. Offset its
  // content origin so only the intentional one-pixel downward press motion is
  // visible; labels and chevrons must not also drift inward.
  lv_obj_set_style_pad_all(btn, -1, LV_STATE_PRESSED);
  lv_obj_set_style_translate_y(btn, 1, LV_STATE_PRESSED);
}

// Selector controls sit over a continuously redrawn dashboard. Keep their
// immediate color/border/position feedback, but avoid software-rendering an
// animated blur around every tap.
static void applySelectorTouchFx(lv_obj_t *btn) {
  applyButtonTouchFx(btn);
  lv_obj_set_style_shadow_width(btn, 0, LV_STATE_PRESSED);
  lv_obj_set_style_shadow_spread(btn, 0, LV_STATE_PRESSED);
  lv_obj_set_style_shadow_opa(btn, LV_OPA_TRANSP, LV_STATE_PRESSED);
}

// A caption too wide for its tile used to be cut off mid-glyph ("Battery
// Capacit"). Step it down a size or two instead — a smaller whole word beats a
// larger truncated one, and the tiles are close enough in weight that mixed
// sizes read as fitted rather than as a mistake.
static const lv_font_t *fittedTileFont(const char *text, const lv_font_t *preferred, int maxWidth) {
  static const lv_font_t *const kLadder[] = {&lv_font_rajdhani_20, &lv_font_rajdhani_14,
                                             &lv_font_rajdhani_12, &lv_font_rajdhani_12};
  const int count = (int)(sizeof(kLadder) / sizeof(kLadder[0]));
  int start = 0;
  for (int i = 0; i < count; i++) {
    if (kLadder[i] == preferred) {
      start = i;
      break;
    }
  }
  for (int i = start; i < count; i++) {
    lv_point_t size;
    lv_txt_get_size(&size, text, kLadder[i], 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    if (size.x <= maxWidth) return kLadder[i];
  }
  return kLadder[count - 1];
}

static lv_obj_t *makeMenuButton(lv_obj_t *parent, int x, int y, int w, int h, const char *title, const char *value,
                                bool active, int id) {
  lv_obj_t *btn = lv_btn_create(parent);
  lv_obj_set_pos(btn, x, y);
  lv_obj_set_size(btn, w, h);
  lv_obj_set_style_radius(btn, cyd_ui::kControlRadius, 0);
  const bool interactive = id != 0;
  lv_obj_set_style_bg_color(
      btn, active ? cyd_ui::activeControlSurface()
                  : (interactive ? cyd_ui::controlSurface() : cyd_ui::panelSurface()),
      0);
  lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(btn, active ? 3 : (interactive ? cyd_ui::kBorderWidth : 0), 0);
  // id 0 means the tile is information-only. It deliberately has no border,
  // chevron or touch feedback: those cues are reserved for actual controls.
  lv_obj_set_style_border_color(
      btn, active ? cyd_ui::chromeAccent() : cyd_ui::idleControlBorder(), 0);
  lv_obj_set_style_shadow_width(btn, 0, 0);
  // A selected option uses a three-pixel inset border. Compensate that extra
  // width so the child content returns to the exact idle coordinates after a
  // press instead of looking permanently pushed inward.
  lv_obj_set_style_pad_all(btn, active ? -2 : 0, 0);
  if (interactive) {
    applyButtonTouchFx(btn);
    lv_obj_add_event_cb(btn, buttonEventCb, LV_EVENT_CLICKED, (void *)(intptr_t)id);
  } else {
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_CLICKABLE);
  }

  lv_obj_t *titleLabel = lv_label_create(btn);
  const bool isNumpadKey = value && !value[0] && title && strlen(title) <= 3 && h >= 36;
  const bool isMainGridTile = w == 100 && h >= 56;
  const bool isCompactValueTile = value && value[0] && h <= 44;
  const lv_font_t *titleFont = isNumpadKey
                                   ? &lv_font_rajdhani_20
                                   : (isMainGridTile ? &lv_font_rajdhani_14 : &lv_font_rajdhani_12);
  if (!isNumpadKey) titleFont = fittedTileFont(title, titleFont, w - 14);
  lv_obj_set_style_text_font(titleLabel, titleFont, 0);
  lv_obj_set_style_text_color(titleLabel, lv_color_white(), 0);
  lv_obj_set_style_text_align(titleLabel, LV_TEXT_ALIGN_LEFT, 0);
  lv_label_set_text(titleLabel, title);
  lv_label_set_long_mode(titleLabel, LV_LABEL_LONG_CLIP);
  lv_obj_set_width(titleLabel, w - 14);
  lv_obj_set_pos(titleLabel, isNumpadKey ? 0 : cyd_ui::kControlTitleInset,
                 isNumpadKey ? 0 : (isCompactValueTile ? 5 : cyd_ui::kControlTitleY));
  if (isNumpadKey) lv_obj_center(titleLabel);

  if (value && value[0]) {
    lv_obj_t *valueLabel = lv_label_create(btn);
    lv_obj_set_style_text_font(valueLabel, &lv_font_rajdhani_12, 0);
    lv_obj_set_style_text_color(valueLabel, cyd_ui::secondaryText(), 0);
    // Explanations may use two compact rows. A slightly tighter line gap keeps
    // both rows legible even in the 42 px menu tiles without crowding the
    // title above them.
    lv_obj_set_style_text_line_space(valueLabel, -4, 0);
    lv_label_set_text(valueLabel, value);
    // LONG_DOT wraps into the second allotted line, then ellipsizes if the
    // complete explanation still does not fit. Reserve the chevron corner.
    lv_label_set_long_mode(valueLabel, LV_LABEL_LONG_DOT);
    const int valueWidth = w - 14 - (interactive && !isNumpadKey ? 9 : 0);
    const int lineHeight = lv_font_get_line_height(&lv_font_rajdhani_12);
    const int twoLineHeight = lineHeight * 2 - 4;
    const int twoLineY = isMainGridTile ? 29 : (h >= 54 ? 24 : 18);
    lv_point_t naturalSize;
    lv_txt_get_size(&naturalSize, value, &lv_font_rajdhani_12, 0, -4, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    // Compact selector rows have room for one subtitle baseline only. Forcing
    // a single ellipsized row prevents LVGL from starting a second row behind
    // the lower border, which was especially visible on the selected language.
    const bool fitsOneLine = isCompactValueTile || (!strchr(value, '\n') && naturalSize.x <= valueWidth);
    // A short status belongs on the same lower baseline as the second line of
    // a longer explanation. This keeps mixed tiles visually level instead of
    // leaving one-line values floating directly under their titles.
    const int valueY = isCompactValueTile ? h - lineHeight - 7
                                          : (fitsOneLine ? twoLineY + lineHeight - 4 : twoLineY);
    // LVGL's label box needs a small amount of descent room beyond the font's
    // nominal line height. Without it, letters such as g/y and even the lower
    // antialiasing row of status text can be clipped at the bottom across all
    // compact menu tiles.
    const int requestedHeight = (fitsOneLine ? lineHeight : twoLineHeight) + 2;
    const int availableHeight = LV_MAX(lineHeight, h - valueY - 3);
    lv_obj_set_size(valueLabel, valueWidth, LV_MIN(requestedHeight, availableHeight));
    lv_obj_set_pos(valueLabel, cyd_ui::kControlTitleInset, valueY);
  }

  // The filled triangle promises the tile does something, so inert tiles do
  // not get one. Drawing it avoids the undersized, font-dependent ">" glyph.
  if (!isNumpadKey && interactive) {
    lv_obj_t *chevron = lv_obj_create(btn);
    lv_obj_remove_style_all(chevron);
    makePassive(chevron);
    lv_obj_set_size(chevron, 7, 9);
    lv_obj_align(chevron, LV_ALIGN_BOTTOM_RIGHT, -6, -6);
    lv_obj_add_event_cb(
        chevron,
        [](lv_event_t *e) {
          lv_area_t area;
          lv_obj_get_content_coords(lv_event_get_target(e), &area);
          lv_point_t points[] = {{area.x1, area.y1},
                                 {area.x2, (lv_coord_t)((area.y1 + area.y2) / 2)},
                                 {area.x1, area.y2}};
          lv_draw_rect_dsc_t dsc;
          lv_draw_rect_dsc_init(&dsc);
          dsc.bg_color = accentLv();
          dsc.bg_opa = LV_OPA_COVER;
          lv_draw_triangle(lv_event_get_draw_ctx(e), &dsc, points);
        },
        LV_EVENT_DRAW_MAIN, NULL);
  }
  return btn;
}

// Canonical read-only surface for status, diagnostics and summary data. It may
// group related information with a subtle fill, but never resembles a button.
static lv_obj_t *makeInfoPanel(lv_obj_t *parent, int x, int y, int w, int h, int radius,
                               lv_color_t bg = cyd_ui::panelSurface()) {
  lv_obj_t *panel = makePanel(parent, x, y, w, h, radius, cyd_ui::idleControlBorder(), bg, true);
  makePassive(panel);
  lv_obj_set_style_border_width(panel, 0, 0);
  return panel;
}

static void lineArrowDrawCb(lv_event_t *e) {
  lv_obj_t *obj = lv_event_get_target(e);
  lv_area_t area;
  lv_obj_get_content_coords(obj, &area);
  const bool pointsLeft = (bool)(intptr_t)lv_obj_get_user_data(obj);
  const lv_coord_t cy = (area.y1 + area.y2) / 2;
  const lv_coord_t tailX = pointsLeft ? area.x2 - 1 : area.x1 + 1;
  const lv_coord_t tipX = pointsLeft ? area.x1 + 1 : area.x2 - 1;
  const lv_coord_t headX = pointsLeft ? tipX + 5 : tipX - 5;

  lv_draw_line_dsc_t dsc;
  lv_draw_line_dsc_init(&dsc);
  dsc.color = lv_color_white();
  dsc.width = 2;
  dsc.round_start = true;
  dsc.round_end = true;
  lv_point_t from = {tailX, cy};
  lv_point_t to = {tipX, cy};
  lv_draw_line(lv_event_get_draw_ctx(e), &dsc, &from, &to);
  from = {headX, (lv_coord_t)(cy - 5)};
  lv_draw_line(lv_event_get_draw_ctx(e), &dsc, &from, &to);
  from = {headX, (lv_coord_t)(cy + 5)};
  lv_draw_line(lv_event_get_draw_ctx(e), &dsc, &from, &to);
}

static lv_obj_t *makeLineArrow(lv_obj_t *parent, bool pointsLeft, int w = 17, int h = 13) {
  lv_obj_t *arrow = lv_obj_create(parent);
  lv_obj_remove_style_all(arrow);
  lv_obj_clear_flag(arrow, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
  lv_obj_set_size(arrow, w, h);
  lv_obj_set_user_data(arrow, (void *)(intptr_t)pointsLeft);
  lv_obj_add_event_cb(arrow, lineArrowDrawCb, LV_EVENT_DRAW_MAIN, NULL);
  return arrow;
}

static lv_obj_t *makeNavButton(lv_obj_t *parent, int x, int y, const char *label, int id, int h = 28) {
  lv_obj_t *btn = lv_btn_create(parent);
  lv_obj_set_pos(btn, x, y);
  lv_obj_set_size(btn, 68, h);
  lv_obj_set_style_radius(btn, 5, 0);
  lv_obj_set_style_bg_color(btn, cyd_ui::controlSurface(), 0);
  lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(btn, cyd_ui::kBorderWidth, 0);
  lv_obj_set_style_border_color(btn, cyd_ui::idleControlBorder(), 0);
  lv_obj_set_style_shadow_width(btn, 0, 0);
  lv_obj_set_style_pad_all(btn, 0, 0);
  applyButtonTouchFx(btn);
  lv_obj_add_event_cb(btn, buttonEventCb, LV_EVENT_CLICKED, (void *)(intptr_t)id);

  const size_t labelLen = label ? strlen(label) : 0;
  const bool pointsLeft = labelLen > 0 && label[0] == '<';
  const bool pointsRight = labelLen > 0 && label[labelLen - 1] == '>';
  const bool hasArrow = pointsLeft || pointsRight;
  const bool arrowOnly = hasArrow && strspn(label, "<> ") == labelLen;

  char cleanLabel[32] = "";
  if (!arrowOnly) {
    size_t start = pointsLeft ? 1 : 0;
    size_t end = labelLen - (pointsRight ? 1 : 0);
    while (start < end && label[start] == ' ') start++;
    while (end > start && label[end - 1] == ' ') end--;
    const size_t cleanLen = LV_MIN(end - start, sizeof(cleanLabel) - 1);
    memcpy(cleanLabel, label + start, cleanLen);
    cleanLabel[cleanLen] = '\0';
  }

  if (hasArrow && !arrowOnly) {
    // Keep icon and text in one measured row. Independent alignment allowed
    // translated labels to grow back over the arrow on narrow top-bar buttons.
    lv_obj_t *content = lv_obj_create(btn);
    lv_obj_remove_style_all(content);
    makePassive(content);
    lv_obj_set_size(content, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(content, 6, 0);
    if (pointsLeft) makeLineArrow(content, true);

    lv_obj_t *text = lv_label_create(content);
    lv_obj_set_style_text_font(text, &lv_font_rajdhani_12, 0);
    lv_obj_set_style_text_color(text, lv_color_white(), 0);
    lv_label_set_text(text, cleanLabel);
    if (pointsRight) makeLineArrow(content, false);
  } else if (arrowOnly) {
    lv_obj_t *arrow = makeLineArrow(btn, pointsLeft);
    lv_obj_set_align(arrow, LV_ALIGN_CENTER);
  } else {
    lv_obj_t *text = lv_label_create(btn);
    lv_obj_set_style_text_font(text, &lv_font_rajdhani_12, 0);
    lv_obj_set_style_text_color(text, lv_color_white(), 0);
    lv_label_set_text(text, cleanLabel);
    lv_obj_set_align(text, LV_ALIGN_CENTER);
  }
  return btn;
}

// This firmware only ever reads from a controller. Every page under Controller
// Settings looks like it might write, so each one says that it does not: a
// rider who thinks they have raised a limit and finds the bike unchanged has
// been misled by the UI, not by the controller.
static lv_obj_t *makeSmallNavButton(lv_obj_t *parent, int x, int y, const char *label, int id, int h = 28) {
  lv_obj_t *btn = makeNavButton(parent, x, y, label, id, h);
  lv_obj_set_size(btn, 34, h);
  return btn;
}

static void verticalArrowDrawCb(lv_event_t *e) {
  lv_obj_t *obj = lv_event_get_target(e);
  lv_area_t area;
  lv_obj_get_content_coords(obj, &area);
  const bool pointsUp = (bool)(intptr_t)lv_obj_get_user_data(obj);
  const lv_coord_t cx = (area.x1 + area.x2) / 2;
  const lv_coord_t tailY = pointsUp ? area.y2 - 1 : area.y1 + 1;
  const lv_coord_t tipY = pointsUp ? area.y1 + 1 : area.y2 - 1;
  const lv_coord_t headY = pointsUp ? tipY + 5 : tipY - 5;
  lv_draw_line_dsc_t dsc;
  lv_draw_line_dsc_init(&dsc);
  dsc.color = lv_color_white();
  dsc.width = 2;
  dsc.round_start = true;
  dsc.round_end = true;
  lv_point_t from = {cx, tailY};
  lv_point_t to = {cx, tipY};
  lv_draw_line(lv_event_get_draw_ctx(e), &dsc, &from, &to);
  from = {(lv_coord_t)(cx - 5), headY};
  lv_draw_line(lv_event_get_draw_ctx(e), &dsc, &from, &to);
  from = {(lv_coord_t)(cx + 5), headY};
  lv_draw_line(lv_event_get_draw_ctx(e), &dsc, &from, &to);
}

static lv_obj_t *makeVerticalNavButton(lv_obj_t *parent, int x, int y, int w, int h, bool pointsUp, int id) {
  lv_obj_t *btn = lv_btn_create(parent);
  lv_obj_set_pos(btn, x, y);
  lv_obj_set_size(btn, w, h);
  lv_obj_set_style_radius(btn, 5, 0);
  lv_obj_set_style_bg_color(btn, cyd_ui::controlSurface(), 0);
  lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(btn, cyd_ui::kBorderWidth, 0);
  lv_obj_set_style_border_color(btn, cyd_ui::idleControlBorder(), 0);
  lv_obj_set_style_shadow_width(btn, 0, 0);
  lv_obj_set_style_pad_all(btn, 0, 0);
  applyButtonTouchFx(btn);
  lv_obj_add_event_cb(btn, buttonEventCb, LV_EVENT_CLICKED, (void *)(intptr_t)id);
  lv_obj_t *arrow = lv_obj_create(btn);
  lv_obj_remove_style_all(arrow);
  makePassive(arrow);
  lv_obj_set_size(arrow, 13, 17);
  lv_obj_set_user_data(arrow, (void *)(intptr_t)pointsUp);
  lv_obj_add_event_cb(arrow, verticalArrowDrawCb, LV_EVENT_DRAW_MAIN, NULL);
  lv_obj_center(arrow);
  return btn;
}

// ── Brightness slider ─────────────────────────────────────────────────────────

static void setBrightnessReadout(lv_obj_t *readout) {
  if (!readout) return;
  char text[8];
  snprintf(text, sizeof(text), "%u%%", displayBrightnessPercent);
  setLabelText(readout, text);
}

// Percent for a touch at x. LVGL's own slider maps the value across the whole
// track, but the calibrated touch band stops at TOUCH_X_MIN/TOUCH_X_MAX, so the
// ends of a full-width track can never be reached (they topped out near 14% and
// 96%). Mapping over the reachable band instead — with a little margin — means
// the finger hits 10% and 100% well inside it, while the track still draws the
// true percentage across its full width.
static int brightnessForTouch(lv_obj_t *slider, lv_coord_t x) {
  static const int kEdgeMargin = 6;
  lv_area_t track;
  lv_obj_get_content_coords(slider, &track);
  const int left = LV_MAX(track.x1, TOUCH_X_MIN + kEdgeMargin);
  const int right = LV_MIN(track.x2, TOUCH_X_MAX - kEdgeMargin);
  if (right <= left) return DISPLAY_BRIGHTNESS_MIN;
  const int span = right - left;
  const int range = DISPLAY_BRIGHTNESS_MAX - DISPLAY_BRIGHTNESS_MIN;
  const int value = DISPLAY_BRIGHTNESS_MIN + ((x - left) * range + span / 2) / span;
  return constrain(value, DISPLAY_BRIGHTNESS_MIN, DISPLAY_BRIGHTNESS_MAX);
}

// Input for the brightness slider. LVGL's built-in slider input is bypassed
// entirely (the slider is not clickable; this runs on a transparent pad above
// it) because it hit-tests the knob only, samples the position just while
// LV_EVENT_PRESSING repeats, and maps across unreachable track ends.
static void brightnessTouchCb(lv_event_t *e) {
  // smoothed touch x, -1 while idle: raw XPT2046 samples wander a few pixels
  // and the knob followed every one of them, which read as judder
  static int filteredX = -1;
  lv_obj_t *slider = (lv_obj_t *)lv_event_get_user_data(e);
  const lv_event_code_t code = lv_event_get_code(e);
  if (!slider) return;

  if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
    filteredX = -1;
    saveAppSettings();  // one NVS write per gesture, not one per dragged pixel
    return;
  }
  if (code != LV_EVENT_PRESSED && code != LV_EVENT_PRESSING) return;

  lv_indev_t *indev = lv_indev_get_act();
  if (!indev || lv_indev_get_type(indev) != LV_INDEV_TYPE_POINTER) return;
  lv_point_t point;
  lv_indev_get_point(indev, &point);

  // a tap lands where it was made; a drag follows a 2/5-weighted average
  filteredX = (code == LV_EVENT_PRESSED || filteredX < 0) ? point.x : (filteredX * 3 + point.x * 2) / 5;

  const int value = brightnessForTouch(slider, filteredX);
  if (value == displayBrightnessPercent) return;
  displayBrightnessPercent = value;
  lv_slider_set_value(slider, value, LV_ANIM_OFF);
  applyDisplayBrightness();
  setBrightnessReadout(brightnessReadoutLabel);
}

static lv_obj_t *makeBrightnessSlider(lv_obj_t *parent, int x, int y, int w, int h) {
  const bool locked = autoBrightnessEnabled;
  lv_obj_t *slider = lv_slider_create(parent);
  lv_obj_set_pos(slider, x, y);
  lv_obj_set_size(slider, w, h);
  lv_slider_set_range(slider, DISPLAY_BRIGHTNESS_MIN, DISPLAY_BRIGHTNESS_MAX);
  lv_slider_set_value(slider, displayBrightnessPercent, LV_ANIM_OFF);
  lv_obj_clear_flag(slider, LV_OBJ_FLAG_CLICKABLE);  // brightnessTouchCb drives it

  lv_obj_set_style_radius(slider, 5, LV_PART_MAIN);
  lv_obj_set_style_bg_color(slider, locked ? c565(0x2104) : c565(0x1082), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(slider, 1, LV_PART_MAIN);
  lv_obj_set_style_border_color(slider, cyd_ui::idleControlBorder(), LV_PART_MAIN);

  lv_obj_set_style_radius(slider, 5, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(slider, locked ? c565(0x4208) : accentLv(), LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_INDICATOR);

  lv_obj_set_style_radius(slider, 4, LV_PART_KNOB);
  lv_obj_set_style_bg_color(slider, locked ? c565(COLOR565_DIM) : lv_color_white(), LV_PART_KNOB);
  lv_obj_set_style_bg_opa(slider, locked ? LV_OPA_TRANSP : LV_OPA_COVER, LV_PART_KNOB);
  lv_obj_set_style_border_width(slider, locked ? 0 : 2, LV_PART_KNOB);
  lv_obj_set_style_border_color(slider, accentDarkLv(), LV_PART_KNOB);
  lv_obj_set_style_pad_all(slider, locked ? 0 : 2, LV_PART_KNOB);  // knob overhangs the track

  // Transparent touch pad spanning the full screen width and overhanging the
  // track vertically, so the gesture keeps working past either end of the bar.
  lv_obj_t *pad = lv_obj_create(parent);
  lv_obj_remove_style_all(pad);
  lv_obj_set_pos(pad, 0, y - 10);
  lv_obj_set_size(pad, 320, h + 20);
  lv_obj_clear_flag(pad, LV_OBJ_FLAG_SCROLLABLE);
  if (locked) {
    lv_obj_clear_flag(pad, LV_OBJ_FLAG_CLICKABLE);
    makeLabelAt(parent, x + w / 2, y + h / 2,
                txt("AUTO BRIGHTNESS ACTIVE", "AUTOMAATTINEN KIRKKAUS KÄYTÖSSÄ", "AUTO-HELLIGKEIT AKTIV",
                    "LUMINOSITÉ AUTO ACTIVE", "BRILLO AUTOMÁTICO ACTIVO", "LUMINOSITÀ AUTO ATTIVA"),
                lv_color_white(), &lv_font_rajdhani_12, 1);
  } else {
    lv_obj_add_flag(pad, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(pad, brightnessTouchCb, LV_EVENT_PRESSED, slider);
    lv_obj_add_event_cb(pad, brightnessTouchCb, LV_EVENT_PRESSING, slider);
    lv_obj_add_event_cb(pad, brightnessTouchCb, LV_EVENT_RELEASED, slider);
    lv_obj_add_event_cb(pad, brightnessTouchCb, LV_EVENT_PRESS_LOST, slider);
  }
  return slider;
}

// All top-bar controls share this height (matches the header box)
static constexpr int kTopBarButtonH = cyd_ui::kTopBarHeight;
static constexpr int kWideTopNavW = cyd_ui::kTopBarWidth;
static constexpr int kWideTopNavRightX = cyd_ui::kTopBarRightX;
static constexpr int kMenuEdgeX = cyd_ui::kEdgeInset;
static constexpr int kMenuContentW = cyd_ui::kScreenWidth - 2 * kMenuEdgeX;
static constexpr int kTwoColLeftW = (kMenuContentW - cyd_ui::kControlGap) / 2;
static constexpr int kTwoColRightX = kMenuEdgeX + kTwoColLeftW + cyd_ui::kControlGap;
static constexpr int kTwoColRightW = cyd_ui::kScreenWidth - kMenuEdgeX - kTwoColRightX;
static constexpr int kThreeColW = (kMenuContentW - 2 * cyd_ui::kControlGap) / 3;
static constexpr int kThreeColX1 = kMenuEdgeX + kThreeColW + cyd_ui::kControlGap;
static constexpr int kThreeColX2 = kThreeColX1 + kThreeColW + cyd_ui::kControlGap;
static constexpr int kFourColW = (kMenuContentW - 3 * cyd_ui::kControlGap) / 4;
static constexpr int kFourColX1 = kMenuEdgeX + kFourColW + cyd_ui::kControlGap;
static constexpr int kFourColX2 = kFourColX1 + kFourColW + cyd_ui::kControlGap;
static constexpr int kFourColX3 = kFourColX2 + kFourColW + cyd_ui::kControlGap;
static constexpr int kFourColLastW = cyd_ui::kScreenWidth - kMenuEdgeX - kFourColX3;

static void makeHeaderBox(lv_obj_t *scr, const char *title, const char *value, int x = 76, int w = 166) {
  // This is status/navigation context, not a touch target. Keeping it as bare
  // text prevents it from competing visually with the actionable side buttons.
  const int cx = x + w / 2;
  makeLabelAt(scr, cx, 8, title, accentLv(), &lv_font_rajdhani_14, 3);
  if (value && value[0]) makeLabelAt(scr, cx, 25, value, c565(COLOR565_LABEL), &lv_font_rajdhani_12, 3);
}

static StatusNotice controllerProfileSavedNotice(int field) {
  // UART is constructed once during boot, so its new baud rate cannot become
  // active until the display restarts. CAN target and the remaining vehicle
  // profile values are read live by the running firmware.
  if (field == VEHICLE_FIELD_VESC_BAUD) return NOTICE_RESTART_DISPLAY_TO_APPLY;
  return telemetryLinkState() == LINK_LIVE ? NOTICE_VESC_PROFILE_SAVED : NOTICE_VESC_OFFLINE_SAVED;
}

static void loadScreen(lv_obj_t *scr) {
  lv_obj_t *old = lv_scr_act();
  lv_scr_load(scr);
  if (old && old != scr) lv_obj_del(old);
  showPendingStatusNotice();
  showPendingRecoveryPrompt();
}

// ── Dashboard screen ──────────────────────────────────────────────────────────

// ── Tap controls overlay ─────────────────────────────────────────────────────

static constexpr int kDashboardSettingsAction = 900;
static constexpr uint32_t kDashboardControlsTimeoutMs = 2000;
static lv_obj_t *dashboardSettingsButton = NULL;
static lv_obj_t *dashboardLoggingControl = NULL;
static lv_obj_t *dashboardLoggingLabel = NULL;
static lv_timer_t *dashboardControlsTimer = NULL;
static bool dashboardControlsVisible = false;
static constexpr uint32_t kSelectorSlideMs = cyd_ui::kMotionMs;
static constexpr uint32_t kPressHoldMs = 250;
static uint32_t loggingUiRevision = 0;
static uint32_t rideCatalogUiRevision = 0;
static uint8_t rideLogsPage = 0;
// True from a confirmed SD card clear until its dialog closes; see
// showSdClearProgress().
static bool sdClearActive = false;
static void slideSelectorPart(lv_obj_t *obj, int32_t to, bool bounce, bool horizontal = false,
                              bool marksTransition = false);

static int compactHousingWidth(const char *text, const lv_font_t *font, int minimumWidth = 0) {
  lv_point_t textSize;
  lv_txt_get_size(&textSize, text ? text : "", font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  const int maximumWidth = cyd_ui::kScreenWidth - 2 * cyd_ui::kEdgeInset;
  // Text rasterization on the physical ESP32 build can occupy a few more
  // pixels than the native preview reports. Keep the housing compact, but
  // reserve four safety pixels beyond the normal inset on each side.
  const int horizontalInset = cyd_ui::kControlTitleInset + 4;
  return constrain(textSize.x + 2 * horizontalInset, minimumWidth, maximumWidth);
}

static void fitDashboardLoggingControl(const char *message) {
  if (!dashboardLoggingControl || !dashboardLoggingLabel) return;
  // Measure the source string rather than querying the label immediately
  // after changing it. On-device LVGL may not expose the replacement buffer
  // until its label refresh has completed, which collapsed this control to
  // its 44 px minimum and produced an ellipsis.
  setLabelText(dashboardLoggingLabel, message);
  const int width = compactHousingWidth(message, &lv_font_rajdhani_12, 44);
  lv_obj_set_width(dashboardLoggingControl, width);
  lv_obj_set_x(dashboardLoggingControl, (cyd_ui::kScreenWidth - width) / 2);
  lv_obj_set_size(dashboardLoggingLabel, width - 2 * cyd_ui::kControlTitleInset,
                  lv_font_get_line_height(&lv_font_rajdhani_12) + 2);
  lv_label_set_long_mode(dashboardLoggingLabel, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_align(dashboardLoggingLabel, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(dashboardLoggingLabel, LV_ALIGN_CENTER, 0, 0);
}

static void dashboardControlsTimeoutCb(lv_timer_t *timer) {
  dashboardControlsTimer = NULL;
  dashboardControlsVisible = false;
  if (dashboardSettingsButton) slideSelectorPart(dashboardSettingsButton, -kTopBarButtonH - 2, false);
  if (dashboardLoggingControl) slideSelectorPart(dashboardLoggingControl, 242, false);
  lv_timer_del(timer);
}

static void refreshDashboardLoggingControl() {
  if (!dashboardLoggingControl || !dashboardLoggingLabel) return;
  const RideLoggingStatus status = rideLoggerStatus();
  if (status.mode != RIDE_LOG_ON) return;
  const char *message =
      status.cardChecking
          ? txt("CHECKING SD CARD", "TARKISTETAAN SD-KORTTIA", "SD-KARTE WIRD GEPRÜFT", "VÉRIFICATION CARTE SD",
                "COMPROBANDO TARJETA SD", "VERIFICA SCHEDA SD")
          : !status.cardReady
                ? txt("DATA LOGGING WAITING FOR SD", "DATALOKITUS ODOTTAA SD-KORTTIA",
                      "DATEN-LOGGING WARTET AUF SD", "JOURNAL ATTEND LA SD",
                      "REGISTRO ESPERA SD", "LOG DATI ATTESA SD")
                : status.recording
                      ? txt("RECORDING RIDE DATA", "TALLENNETAAN AJODATAA", "FAHRTDATEN WERDEN AUFGEZEICHNET",
                            "ENREGISTREMENT DU TRAJET", "GRABANDO DATOS DEL VIAJE", "REGISTRAZIONE DATI CORSA")
                      : txt("DATA LOGGING ON", "DATALOKITUS PÄÄLLÄ", "DATEN-LOGGING AN",
                            "JOURNALISATION ACTIVE", "REGISTRO DE DATOS ACTIVO", "LOG DATI ATTIVO");
  fitDashboardLoggingControl(message);
}

// ── Recovery hold gesture ────────────────────────────────────────────────────
//
// Press and hold anywhere on the dashboard to reach touch calibration. This is
// the escape hatch for a calibration bad enough that the Display submenu can no
// longer be tapped, so it is read from the raw panel rather than from an LVGL
// long-press: widget hit-testing would send a hold over the SETTINGS or logging
// control to that widget instead, LVGL's scroll threshold would cancel the
// press as XPT2046 samples jitter over five seconds, and every coordinate
// involved would pass through the very mapping that is suspect.
//
// Nothing is drawn for the first phase, so incidental contact stays invisible;
// the ring then owns the rest of the window and releasing during it aborts.
static uint32_t recoveryHoldStartedMs = 0;
static bool recoveryHoldPrompted = false;
// Armed only after a clean release. This is what keeps a panel whose IRQ is
// stuck LOW from re-triggering calibration the instant the previous attempt
// gives up, which would leave the display looping through the flow forever.
static bool recoveryHoldArmed = false;
static uint8_t recoveryHoldReleasePolls = 0;
static lv_obj_t *recoveryHoldOverlay = NULL;
static lv_obj_t *recoveryHoldArc = NULL;
static bool recoveryResetPromptPending = false;

static void showDashboardControls() {
  if (!dashboardSettingsButton) return;
  if (!dashboardControlsVisible) {
    dashboardControlsVisible = true;
    slideSelectorPart(dashboardSettingsButton, 4, true);
    if (dashboardLoggingControl) slideSelectorPart(dashboardLoggingControl, 206, true);
  }
  if (dashboardControlsTimer) lv_timer_del(dashboardControlsTimer);
  dashboardControlsTimer = lv_timer_create(dashboardControlsTimeoutCb, kDashboardControlsTimeoutMs, NULL);
  lv_timer_set_repeat_count(dashboardControlsTimer, 1);
}

static void hideDashboardControls() {
  if (!dashboardControlsVisible) return;
  dashboardControlsVisible = false;
  if (dashboardControlsTimer) {
    lv_timer_del(dashboardControlsTimer);
    dashboardControlsTimer = NULL;
  }
  if (dashboardSettingsButton) slideSelectorPart(dashboardSettingsButton, -kTopBarButtonH - 2, false);
  if (dashboardLoggingControl) slideSelectorPart(dashboardLoggingControl, 242, false);
}

// A press is the first sign of a tap, a release later: stop live repaints and instrument
// glides at once, so the frame budget is free when the slide starts instead of a dashboard
// frame being under way. A hold that is not a tap (the recovery gesture) loses only this long.
static void pressHoldCb(lv_event_t *) {
  holdUiUpdates(kPressHoldMs);
  pauseDashboardAnimations(kPressHoldMs);
}

static void dashboardTapCb(lv_event_t *) {
  if (dashboardControlsVisible)
    hideDashboardControls();
  else
    showDashboardControls();
}

static void dashboardAction(int id) {
  if (id != kDashboardSettingsAction) return;
  if (dashboardControlsTimer) {
    lv_timer_del(dashboardControlsTimer);
    dashboardControlsTimer = NULL;
  }
  if (pinEnabled) {
    queueRebuild(SCREEN_PIN_LOCK);
  } else {
    settingsMenuLevel = SETTINGS_MENU_DISPLAY;
    queueRebuild(SCREEN_MENU);
  }
}

// ── Dashboard alert overlay ──────────────────────────────────────────────────
// Two things the rider must not have to infer from the numbers: that the link
// is gone, and that the controller has faulted. Rather than teach all thirteen
// dashboards to render either state, one chip names whichever applies, over
// whichever theme is loaded.
//
// A dead link also gets a scrim, because every reading behind it is a zero
// standing in for nothing, and the chip sits centre-screen over the speed —
// covering a figure that is not true costs nothing. A fault is the opposite
// case: the readings are live and worth watching, so there is no scrim and the
// chip drops to the foot of the screen. Both are passive, so the hold gesture
// still reaches the screen underneath.

// The link chip is a centred pill; the fault alert is a full-width band along
// the foot. A 232 px pill down there left fragments of the secondary readouts
// poking out at either side, which looked like a misplaced dialog rather than
// a status line.
static const int kAlertLinkW = 232;
static const int kAlertLinkY = 103;   // centred, over the speed
static const int kAlertFaultW = 304;  // full width inside the 8 px margin
static const int kAlertFaultY = 200;  // over the least costly row on every theme
static const int kAlertChipH = 34;
static const uint16_t kAlertLinkColor = 0xFB40;   // amber
static const uint16_t kAlertFaultColor = 0xF9C0;  // red-orange: worse than a lost link

static lv_obj_t *linkScrim = NULL;
static lv_obj_t *linkChip = NULL;
static lv_obj_t *linkChipText = NULL;
static lv_obj_t *linkChipIcon = NULL;
static lv_obj_t *linkChipDetail = NULL;
static TelemetryLink linkShown = LINK_LIVE;
static uint8_t faultShown = 0;
// The alerts that offer controller setup rather than waiting for data.
// Bluetooth off is not a setup offer: it only names where the switch lives.
enum SetupAlert : uint8_t {
  SETUP_ALERT_NONE,
  SETUP_ALERT_UNPAIRED,
  SETUP_ALERT_NEVER_CONNECTED,
  SETUP_ALERT_BLUETOOTH_OFF
};
static SetupAlert setupAlertShown = SETUP_ALERT_NONE;
// Guidance under the caption makes the chip taller by one row per line.
static const int kAlertChipDetailBaseH = 38;
static const int kAlertChipDetailLineH = 14;
static const int kAlertUnpairedW = 304;
// Set when the no-controller alert asked for setup behind a PIN, so the
// unlock continues there instead of on the first Settings page.
static bool pinUnlockOpensControllerSetup = false;

// Straight to controller setup, as Settings > Next > Vehicle Config >
// Connection > Setup would reach it. Back walks out the same way.
// Pairing and connecting need the radio, so starting controller setup is
// also the rider asking for Bluetooth back.
static void enableBluetoothForControllerSetup() {
  if (bluetoothEnabled) return;
  bluetoothEnabled = true;
  saveAppSettings();
  controllerManagerApplyBluetoothEnabled();
}

static void openControllerSetup() {
  enableBluetoothForControllerSetup();
  pendingControllerType = controllerType;
  pendingControllerConnection = controllerConnection;
  controllerSetupStage = CONTROLLER_SETUP_TYPE;
  controllerDeviceOffset = 0;
  settingsMenuLevel = SETTINGS_MENU_CONTROLLER;
  submenuType = SUBMENU_CONTROLLER_TYPE;
  queueRebuild(SCREEN_SUBMENU);
}

static void linkChipTapCb(lv_event_t *) {
  // Only the controller setup alerts are clickable; the others stay passive.
  if (setupAlertShown == SETUP_ALERT_NONE || setupAlertShown == SETUP_ALERT_BLUETOOTH_OFF) return;
  if (pinEnabled) {
    pinUnlockOpensControllerSetup = true;
    queueRebuild(SCREEN_PIN_LOCK);
  } else {
    openControllerSetup();
  }
}
// When the link last stopped being live, or 0 while it is live. Kept across
// screen rebuilds so returning to the dashboard mid-outage does not restart
// the grace period below.
static uint32_t linkDownSinceMs = 0;
// Telemetry is already called lost 1.5 s after the last sample; these add the
// grace before the dashboard says so. Waiting covers boot and controller
// switches, where a Bluetooth connect alone can take several seconds.
static constexpr uint32_t kLinkLostAlertDelayMs = 5000;
static constexpr uint32_t kLinkWaitingAlertDelayMs = 10000;

static void makeLinkOverlay(lv_obj_t *scr) {
  linkScrim = makePanel(scr, 0, 0, 320, 240, 0, lv_color_black(), lv_color_black(), true);
  lv_obj_set_style_border_width(linkScrim, 0, 0);
  lv_obj_set_style_bg_opa(linkScrim, LV_OPA_60, 0);
  makePassive(linkScrim);
  lv_obj_add_flag(linkScrim, LV_OBJ_FLAG_HIDDEN);

  linkChip = makePanel(scr, (320 - kAlertLinkW) / 2, kAlertLinkY, kAlertLinkW, kAlertChipH, 8,
                       c565(kAlertLinkColor), c565(0x2104), true);
  makePassive(linkChip);
  linkChipIcon = makeIcon(linkChip, 10, 8, CYD_ICON_WARNING, c565(kAlertLinkColor));
  linkChipText = lv_label_create(linkChip);
  makePassive(linkChipText);
  lv_obj_set_style_text_font(linkChipText, &lv_font_rajdhani_14, 0);
  lv_obj_set_style_text_color(linkChipText, lv_color_white(), 0);
  lv_obj_set_style_text_align(linkChipText, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_text(linkChipText, "");
  lv_obj_set_pos(linkChipText, 32, 9);
  linkChipDetail = lv_label_create(linkChip);
  makePassive(linkChipDetail);
  lv_obj_set_style_text_font(linkChipDetail, &lv_font_rajdhani_12, 0);
  lv_obj_set_style_text_color(linkChipDetail, c565(COLOR565_LABEL), 0);
  lv_obj_set_style_text_align(linkChipDetail, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_text(linkChipDetail, "");
  lv_obj_set_pos(linkChipDetail, 32, 28);
  lv_obj_add_flag(linkChipDetail, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_event_cb(linkChip, linkChipTapCb, LV_EVENT_CLICKED, NULL);
  lv_obj_add_flag(linkChip, LV_OBJ_FLAG_HIDDEN);
}

static void setAlertChip(const char *text, uint16_t color, int w, int y, bool scrim,
                         const char *detail = nullptr) {
  lv_obj_set_style_border_color(linkChip, c565(color), 0);
  if (linkChipIcon) lv_obj_set_style_img_recolor(linkChipIcon, c565(color), 0);
  setLabelText(linkChipText, text);
  lv_obj_set_pos(linkChip, (320 - w) / 2, y);
  lv_obj_set_width(linkChip, w);
  // A second line of guidance makes the chip taller; the icon stays centred
  // against whichever height it has.
  int detailLines = detail ? 1 : 0;
  for (const char *c = detail; c && *c; c++)
    if (*c == '\n') detailLines++;
  const int h = detail ? kAlertChipDetailBaseH + kAlertChipDetailLineH * detailLines : kAlertChipH;
  lv_obj_set_height(linkChip, h);
  if (linkChipIcon) lv_obj_set_y(linkChipIcon, (h - kAlertChipH) / 2 + 8);
  lv_obj_set_y(linkChipText, detail ? 7 : 9);
  if (detail) {
    // Establish the wrapping box before assigning text. LVGL otherwise lays
    // out the new text against the label's creation-time content size, which
    // made one of these otherwise identical three-line alerts keep only its
    // first line after a fresh screen build.
    lv_obj_set_size(linkChipDetail, w - 32 - 8, kAlertChipDetailLineH * detailLines);
    lv_label_set_long_mode(linkChipDetail, LV_LABEL_LONG_WRAP);
    setLabelText(linkChipDetail, detail);
    lv_obj_clear_flag(linkChipDetail, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(linkChipDetail, LV_OBJ_FLAG_HIDDEN);
  }
  // centred in what the icon leaves, so the band stays balanced whatever the
  // translated caption measures
  lv_obj_set_width(linkChipText, w - 32 - 8);
  lv_obj_clear_flag(linkChip, LV_OBJ_FLAG_HIDDEN);
  if (scrim) {
    lv_obj_clear_flag(linkScrim, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(linkScrim, LV_OBJ_FLAG_HIDDEN);
  }
}

static void refreshLinkOverlay(bool force) {
  if (!linkScrim || !linkChip) return;
  if (dashboardDemoModeEnabled && currentScreen == SCREEN_DASHBOARD) {
    linkShown = LINK_LIVE;
    faultShown = 0;
    lv_obj_add_flag(linkScrim, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(linkChip, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  const TelemetryLink actual = telemetryLinkState();
  // Two states offer controller setup instead of waiting for data, and both
  // say so at once: a Bluetooth controller that was never paired cannot
  // connect by itself, and until any controller has delivered data since the
  // last reset there may be nothing set up at all. Neither waits, because a
  // display that has never seen a controller has nothing else to offer -- and
  // the flag behind the second one is remembered, so this is the first-run
  // case rather than something a rider meets after every boot.
  const ControllerBackend *backend = activeControllerBackend();
  const bool down = actual != LINK_LIVE;
  // A Bluetooth controller with the radio switched off cannot connect, so say
  // that at once instead of waiting for data or offering to pair.
  const bool bluetoothOff = down && !bluetoothEnabled && controllerUsesBluetooth();
  const bool unpaired = down && !bluetoothOff && backend && backend->caps.showsDeviceList && backend->linkStatus &&
                        !backend->linkStatus().savedDevice;
  const bool neverConnected = down && !bluetoothOff && !unpaired && !controllerEverConnected;
  // Hold the alert back until the link has stayed down for a while, so a
  // brief dropout or the seconds a Bluetooth controller takes to connect after
  // boot do not flash it. It still clears the moment data returns.
  const uint32_t now = millis();
  if (actual == LINK_LIVE) linkDownSinceMs = 0;
  else if (linkDownSinceMs == 0) linkDownSinceMs = now ? now : 1;
  const uint32_t graceMs = bluetoothOff || unpaired || neverConnected ? 0
                           : actual == LINK_WAITING ? kLinkWaitingAlertDelayMs
                                                    : kLinkLostAlertDelayMs;
  const bool held = down && now - linkDownSinceMs < graceMs;
  const TelemetryLink state = held ? LINK_LIVE : actual;
  const SetupAlert setupAlert = held ? SETUP_ALERT_NONE
                                : bluetoothOff ? SETUP_ALERT_BLUETOOTH_OFF
                                : unpaired ? SETUP_ALERT_UNPAIRED
                                : neverConnected ? SETUP_ALERT_NEVER_CONNECTED
                                                 : SETUP_ALERT_NONE;
  // telemetryFaultCode() already reports none while the link is down, so a
  // fault latched just before a dropout cannot outlive the data behind it.
  const uint8_t fault = telemetryFaultCode();
  if (!force && state == linkShown && fault == faultShown && setupAlert == setupAlertShown) return;
  linkShown = state;
  faultShown = fault;
  setupAlertShown = setupAlert;

  // Clickable only while it offers controller setup, so a tap on any other
  // alert still reaches the dashboard behind it.
  if (setupAlert == SETUP_ALERT_UNPAIRED || setupAlert == SETUP_ALERT_NEVER_CONNECTED)
    lv_obj_add_flag(linkChip, LV_OBJ_FLAG_CLICKABLE);
  else lv_obj_clear_flag(linkChip, LV_OBJ_FLAG_CLICKABLE);

  if (setupAlert == SETUP_ALERT_BLUETOOTH_OFF) {
    setAlertChip(txt("BLUETOOTH IS OFF", "BLUETOOTH ON POIS PÄÄLTÄ", "BLUETOOTH IST AUS",
                     "BLUETOOTH DÉSACTIVÉ", "BLUETOOTH APAGADO", "BLUETOOTH SPENTO"),
                 kAlertLinkColor, kAlertUnpairedW, kAlertLinkY - 16, true,
                 txt("Turn it on in Settings > Next >\nVehicle configuration > Connection",
                     "Kytke päälle: Asetukset > Seur >\nAjoneuvon määritys > Yhteys",
                     "Einschalten: Einstell. > Weiter >\nFahrzeugkonfiguration > Verbindung",
                     "Activez-le : Réglages > Suiv >\nConfiguration du véhicule > Connexion",
                     "Actívelo en Ajustes > Sig >\nConfiguración del vehículo > Conexión",
                     "Attivalo in Impostaz. > Avanti >\nConfigurazione veicolo > Connessione"));
    return;
  }
  if (setupAlert == SETUP_ALERT_UNPAIRED) {
    // The route names the labels exactly as the menus show them.
    setAlertChip(txt("NO CONTROLLER PAIRED", "OHJAINTA EI OLE PARITETTU", "KEIN CONTROLLER GEKOPPELT",
                     "AUCUN CONTRÔLEUR APPAIRÉ", "SIN CONTROLADOR VINCULADO", "NESSUN CONTROLLER ABBINATO"),
                 kAlertLinkColor, kAlertUnpairedW, kAlertLinkY - 23, true,
                 txt("Tap here to pair it\nor go to Settings > Next >\nVehicle configuration > Connection",
                     "Napauta tästä parittaaksesi\ntai Asetukset > Seur >\nAjoneuvon määritys > Yhteys",
                     "Hier tippen zum Koppeln\noder Einstell. > Weiter >\nFahrzeugkonfiguration > Verbindung",
                     "Touchez ici pour l'appairer\nou Réglages > Suiv >\nConfiguration du véhicule > Connexion",
                     "Toca aquí para vincularlo\no Ajustes > Sig >\nConfiguración del vehículo > Conexión",
                     "Tocca qui per abbinarlo\no Impostaz. > Avanti >\nConfigurazione veicolo > Connessione"));
    return;
  }
  if (setupAlert == SETUP_ALERT_NEVER_CONNECTED) {
    setAlertChip(txt("NO CONTROLLER CONNECTED YET", "OHJAINTA EI OLE VIELÄ YHDISTETTY",
                     "NOCH KEIN CONTROLLER VERBUNDEN", "AUCUN CONTRÔLEUR CONNECTÉ",
                     "SIN CONTROLADOR CONECTADO", "NESSUN CONTROLLER COLLEGATO"),
                 kAlertLinkColor, kAlertUnpairedW, kAlertLinkY - 23, true,
                 txt("Tap here to set it up\nor go to Settings > Next >\nVehicle configuration > Connection",
                     "Napauta tästä määrittääksesi\ntai Asetukset > Seur >\nAjoneuvon määritys > Yhteys",
                     "Hier tippen zum Einrichten\noder Einstell. > Weiter >\nFahrzeugkonfiguration > Verbindung",
                     "Touchez ici pour le configurer\nou Réglages > Suiv >\nConfiguration du véhicule > Connexion",
                     "Toca aquí para configurarlo\no Ajustes > Sig >\nConfiguración del vehículo > Conexión",
                     "Tocca qui per configurarlo\no Impostaz. > Avanti >\nConfigurazione veicolo > Connessione"));
    return;
  }

  // A dead link outranks a fault: with nothing arriving, the fault byte is as
  // stale as everything else on the screen.
  if (state != LINK_LIVE) {
    const bool farDriver = controllerUsesFarDriverBle();
    const char *message =
        state == LINK_WAITING
            ? (farDriver ? txt("WAITING FOR FARDRIVER DATA", "ODOTETAAN FARDRIVER-DATAA", "WARTE AUF FARDRIVER-DATEN",
                               "ATTENTE DONNÉES FARDRIVER", "ESPERANDO DATOS FARDRIVER", "ATTESA DATI FARDRIVER")
                         : txt("WAITING FOR VESC", "ODOTETAAN VESCIÄ", "WARTE AUF VESC", "ATTENTE VESC",
                               "ESPERANDO VESC", "ATTESA VESC"))
            : (farDriver ? txt("FARDRIVER CONNECTION LOST", "FARDRIVER-YHTEYS KATKESI",
                               "FARDRIVER-VERBINDUNG WEG", "CONNEXION FARDRIVER PERDUE",
                               "CONEXIÓN FARDRIVER PERDIDA", "CONNESSIONE FARDRIVER PERSA")
                         : txt("VESC LINK LOST", "VESC-YHTEYS KATKESI", "VESC-VERBINDUNG WEG",
                               "LIAISON VESC PERDUE", "ENLACE VESC PERDIDO", "LINK VESC PERSO"));
    setAlertChip(message,
                 kAlertLinkColor, kAlertLinkW, kAlertLinkY, true);
    return;
  }
  if (fault != 0) {
    setAlertChip(telemetryFaultLabel(fault), kAlertFaultColor, kAlertFaultW, kAlertFaultY, false);
    return;
  }
  lv_obj_add_flag(linkScrim, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(linkChip, LV_OBJ_FLAG_HIDDEN);
}

static void showDashboard() {
  setDemoMode(dashboardDemoModeEnabled);
  saveAppSettings();  // enterDashboard() persisted settings on every entry
  lv_obj_t *scr = makeScreen();
  const ControllerSnapshot snapshot = controllerSnapshot();
  const DashboardValues values = snapshot.link == LINK_LIVE ? snapshot.values : DashboardValues{};
  setDashboardTelemetryFields(snapshot.available);
  buildDashboard(scr, values);
  // Alerts remain below the controls overlay.
  makeLinkOverlay(scr);
  refreshLinkOverlay(true);
  currentAction = dashboardAction;
  dashboardSettingsButton = makeNavButton(
      scr, kWideTopNavRightX, -kTopBarButtonH - 2,
      txt("SETTINGS", "ASETUKSET", "EINSTELL.", "RÉGLAGES", "AJUSTES", "IMPOSTAZ."),
      kDashboardSettingsAction, kTopBarButtonH);
  lv_obj_set_width(dashboardSettingsButton, kWideTopNavW);
  const RideLoggingStatus logging = rideLoggerStatus();
  if (logging.mode == RIDE_LOG_ON) {
    dashboardLoggingControl = makePanel(scr, 138, 242, 44, 28, 5, lv_color_black(), lv_color_black(), true);
    makePassive(dashboardLoggingControl);
    lv_obj_set_style_border_width(dashboardLoggingControl, 0, 0);
    lv_obj_set_style_bg_opa(dashboardLoggingControl, LV_OPA_90, 0);
    dashboardLoggingLabel = makeLabelAt(dashboardLoggingControl, 22, 8, "", lv_color_white(),
                                        &lv_font_rajdhani_12, 3);
  }
  refreshDashboardLoggingControl();
  lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(scr, pressHoldCb, LV_EVENT_PRESSED, NULL);
  lv_obj_add_event_cb(scr, dashboardTapCb, LV_EVENT_CLICKED, NULL);
  loadScreen(scr);
}

static void deliverLoggingNotice(StatusNotice notice) {
  if (notice == NOTICE_NONE) return;
  if (currentScreen == SCREEN_DASHBOARD && !rebuildQueued)
    showStatusNotice(notice);
  else
    queueStatusNotice(notice);
}

static void observeRideLoggingNotices() {
  const RideLoggingStatus status = rideLoggerStatus();
  if (!loggingNoticeStatusKnown) {
    loggingNoticeStatus = status;
    loggingNoticeStatusKnown = true;
    return;
  }

  StatusNotice notice = NOTICE_NONE;
  if (status.ioErrors > loggingNoticeStatus.ioErrors) {
    loggingRideSavePending = false;
    notice = NOTICE_SD_WRITE_FAILED;
  } else if (loggingNoticeStatus.cardReady && !status.cardReady) {
    loggingRideSavePending = false;
    notice = loggingNoticeStatus.recording ? NOTICE_SD_CARD_REMOVED_RECORDING : NOTICE_SD_CARD_REMOVED;
  } else if (!loggingNoticeStatus.cardReady && status.cardReady && status.mode != RIDE_LOG_OFF) {
    notice = NOTICE_SD_CARD_READY;
  } else if (!loggingNoticeStatus.recording && status.recording) {
    loggingRideSavePending = false;
  } else if (loggingNoticeStatus.recording && !status.recording) {
    if (status.rideCount > loggingNoticeStatus.rideCount) {
      loggingRideSavePending = false;
      notice = NOTICE_RIDE_SAVED;
    } else {
      loggingRideSavePending = true;
    }
  } else if (loggingRideSavePending && status.rideCount > loggingNoticeStatus.rideCount) {
    loggingRideSavePending = false;
    notice = NOTICE_RIDE_SAVED;
  }

  loggingNoticeStatus = status;
  deliverLoggingNotice(notice);
}

void uiDashboardTick() {
  // The first real telemetry since a reset: from here on a missing link means
  // the controller is off, not unconfigured. Saved here because the UI thread
  // owns the settings store; demo data does not count.
  if (!controllerEverConnected && !dashboardDemoModeEnabled && telemetryLinkState() == LINK_LIVE) {
    controllerEverConnected = true;
    saveAppSettings();
  }
  serviceDemoMode();
  serviceGaugeRanges();
  // A slide is running (or a press that is about to start one): nothing else draws. The clocks
  // above keep time; every repaint below waits for the next tick.
  if (uiUpdatesHeld()) return;
  if (currentScreen == SCREEN_SUBMENU && submenuType == SUBMENU_SPEED && speedPage == 0 &&
      !rebuildQueued && rideModeReadout) {
    const char *name = rideModeName(telemetryRideMode());
    if (strcmp(lv_label_get_text(rideModeReadout), name)) lv_label_set_text(rideModeReadout, name);
  }
  observeRideLoggingNotices();
  serviceRecoveryHold();
  // Any way out of the controller wizard -- BACK, apply, or the automatic
  // return to the dashboard -- hands the radio back to the configured backend.
  if (currentScreen != SCREEN_SUBMENU || submenuType != SUBMENU_CONTROLLER_TYPE) {
    controllerScanPending = false;
    controllerManagerReleaseRadioRequest();
  }
  if (dashboardAppearanceMode == DASH_APPEARANCE_AUTO && !rebuildQueued &&
      (currentScreen == SCREEN_DASHBOARD ||
       (currentScreen == SCREEN_SUBMENU && submenuType == SUBMENU_DASH_UI && !dashUiGridOpen) ||
       (currentScreen == SCREEN_CONFIG && configStep == 5 && !dashUiGridOpen))) {
    const bool light = dashboardLightModeActive();
    if (autoAppearanceStateKnown && light != lastAutoAppearanceLight) {
      lastAutoAppearanceLight = light;
      autoAppearanceRebuildQueued = true;
      queueLowMemoryRebuild(currentScreen);
      return;
    }
    lastAutoAppearanceLight = light;
    autoAppearanceStateKnown = true;
  }
  if (currentScreen == SCREEN_DASHBOARD && !rebuildQueued) {
    const ControllerSnapshot snapshot = controllerSnapshot();
    const DashboardValues values = snapshot.link == LINK_LIVE ? snapshot.values : DashboardValues{};
    setDashboardTelemetryFields(snapshot.available);
    updateDashboard(values);
    refreshLinkOverlay(false);
    refreshDashboardLoggingControl();
  } else if (currentScreen == SCREEN_SUBMENU && submenuType == SUBMENU_LOGGING && !rebuildQueued) {
    const RideLoggingStatus status = rideLoggerStatus();
    if (loggingUiRevision != status.revision) {
      loggingUiRevision = status.revision;
      queueLowMemoryRebuild(SCREEN_SUBMENU);
    }
  } else if (currentScreen == SCREEN_RIDE_LOGS && !rebuildQueued && !sdClearActive) {
    const RideLoggingStatus storage = rideLoggerStatus();
    const RideLogCatalogStatus catalog = rideLoggerCatalogStatus();
    // The catalog was scanned while that ride was still growing. Once it
    // closes, rescan so its row unlocks with its final length.
    static bool listedWhileRecording = false;
    if (listedWhileRecording && !storage.recording) rideLoggerRequestCatalog();
    listedWhileRecording = storage.recording;
    const uint32_t combinedRevision = storage.revision ^ (catalog.revision * 2654435761UL);
    if (rideCatalogUiRevision != combinedRevision) {
      rideCatalogUiRevision = combinedRevision;
      queueLowMemoryRebuild(SCREEN_RIDE_LOGS);
    }
  } else if (currentScreen == SCREEN_FARDRIVER_BLE && !rebuildQueued) {
    refreshFarDriverBleScreen();
  } else if (currentScreen == SCREEN_FIRMWARE_UPDATE && !rebuildQueued) {
    refreshFirmwareUpdateScreen();
  } else if (currentScreen == SCREEN_COMPANION_MODE && !rebuildQueued) {
    refreshCompanionScreen();
  } else if (currentScreen == SCREEN_SUBMENU && submenuType == SUBMENU_CONTROLLER_TYPE &&
             controllerSetupStage == CONTROLLER_SETUP_DISCOVERY && !rebuildQueued) {
    const ControllerBackend *backend = pendingControllerBackend();
    if (controllerScanPending && backend && controllerManagerRequestRadioFor(backend->id)) {
      controllerScanPending = false;
      if (backend->startScan) backend->startScan();
    }
    const uint32_t revision = backend && backend->linkStatus ? backend->linkStatus().revision : 0;
    if (revision != controllerSetupRevision) {
      controllerSetupRevision = revision;
      queueLowMemoryRebuild(SCREEN_SUBMENU);
    }
  } else if (currentScreen == SCREEN_SUBMENU && submenuType == SUBMENU_DASH_UI && !dashUiGridOpen &&
             !rebuildQueued) {
    // The selector's grid contains only thumbnail cards.  Calling the live
    // dashboard updater there uses widget pointers from the previous preview
    // and can reset the ESP32 on the first 100 ms timer tick.  The setup
    // wizard did not expose this because it runs under SCREEN_CONFIG.
    if (!uiUpdatesHeld() && selectorPreviewUpdateDue()) {
      setDashboardTelemetryFields(TELEMETRY_FIELDS_ALL);
      updateDashboardMode(dashUiPreviewMode, makeDummyValues());
    }
  }
}

// ── Handing the screen back to the dashboard ─────────────────────────────────
// A settings screen left open is a speedometer the rider does not have. Menus
// hand back after a spell with no touches, whether or not the vehicle is
// moving. The first-boot wizard and text entry are exempt so partially entered
// setup data is not discarded. PIN and menu screens return home normally.

static const uint32_t kMenuIdleWarningMs = 5000;
static const int kRecoveryHoldMaxKmh = 5;  // above a walking push of the vehicle

// ── Recovery hold service ────────────────────────────────────────────────────

// The overlay is a child of whichever screen was loaded when the hold began,
// so a screen rebuild frees it without anyone calling hideRecoveryHoldOverlay.
// Forget it when LVGL destroys it, however that happens, rather than leaving a
// pointer into freed memory for the next dashboard tick to delete a second
// time. Same guard as statusToastDeleteCb and the developer hold button.
static void recoveryHoldOverlayDeleteCb(lv_event_t *) {
  recoveryHoldOverlay = NULL;
  recoveryHoldArc = NULL;
}

static void hideRecoveryHoldOverlay() {
  if (!recoveryHoldOverlay) return;
  lv_obj_del(recoveryHoldOverlay);
  recoveryHoldOverlay = NULL;
  recoveryHoldArc = NULL;
}

static void showRecoveryHoldOverlay() {
  if (recoveryHoldOverlay) return;
  recoveryHoldOverlay = lv_obj_create(lv_scr_act());
  lv_obj_add_event_cb(recoveryHoldOverlay, recoveryHoldOverlayDeleteCb, LV_EVENT_DELETE, NULL);
  lv_obj_remove_style_all(recoveryHoldOverlay);
  lv_obj_set_pos(recoveryHoldOverlay, 0, 0);
  lv_obj_set_size(recoveryHoldOverlay, cyd_ui::kScreenWidth, cyd_ui::kScreenHeight);
  lv_obj_set_style_bg_color(recoveryHoldOverlay, lv_color_black(), 0);
  // Fully opaque, not a scrim. A dashboard showing through put its own values
  // straight behind this copy, and it matches the black the boot-side half of
  // the same gesture draws with TFT_eSPI.
  lv_obj_set_style_bg_opa(recoveryHoldOverlay, LV_OPA_COVER, 0);
  // Passive: the gesture is read from the raw panel, so the overlay must not
  // become a press target and steal the click LVGL is already tracking.
  makePassive(recoveryHoldOverlay);
  lv_obj_clear_flag(recoveryHoldOverlay, LV_OBJ_FLAG_SCROLLABLE);

  recoveryHoldArc = lv_arc_create(recoveryHoldOverlay);
  lv_obj_set_size(recoveryHoldArc, 74, 74);
  lv_obj_set_pos(recoveryHoldArc, (cyd_ui::kScreenWidth - 74) / 2, 46);
  makePassive(recoveryHoldArc);
  lv_obj_remove_style(recoveryHoldArc, NULL, LV_PART_KNOB);
  lv_arc_set_rotation(recoveryHoldArc, 270);
  lv_arc_set_bg_angles(recoveryHoldArc, 0, 360);
  lv_arc_set_range(recoveryHoldArc, 0, 100);
  lv_arc_set_value(recoveryHoldArc, 0);
  lv_obj_set_style_arc_width(recoveryHoldArc, 7, LV_PART_MAIN);
  lv_obj_set_style_arc_width(recoveryHoldArc, 7, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(recoveryHoldArc, cyd_ui::idleControlBorder(), LV_PART_MAIN);
  lv_obj_set_style_arc_color(recoveryHoldArc, cyd_ui::chromeAccent(), LV_PART_INDICATOR);

  makeLabelAt(recoveryHoldOverlay, 160, 140,
              txt("KEEP HOLDING", "PIDÄ POHJASSA", "WEITER HALTEN", "MAINTENEZ",
                  "MANTENGA PULSADO", "CONTINUA A TENERE"),
              lv_color_white(), &lv_font_rajdhani_16, 3);
  makeLabelAt(recoveryHoldOverlay, 160, 166,
              txt("Opens touch calibration", "Avaa kosketuskalibroinnin",
                  "Öffnet die Touch-Kalibrierung", "Ouvre le calibrage tactile",
                  "Abre la calibración táctil", "Apre la calibrazione touch"),
              cyd_ui::secondaryText(), &lv_font_rajdhani_12, 3);
  makeLabelAt(recoveryHoldOverlay, 160, 188,
              txt("Release to cancel", "Vapauta peruaksesi", "Loslassen zum Abbrechen",
                  "Relâchez pour annuler", "Suelte para cancelar", "Rilascia per annullare"),
              cyd_ui::secondaryText(), &lv_font_rajdhani_12, 3);
  lv_obj_move_foreground(recoveryHoldOverlay);
}

static void resetRecoveryHold() {
  recoveryHoldStartedMs = 0;
  recoveryHoldPrompted = false;
  hideRecoveryHoldOverlay();
}

// Runs the blocking calibration, then hands the outcome back to the LVGL side.
static void enterRecoveryCalibration() {
  resetRecoveryHold();
  // Disarm across the whole flow: the gesture may only re-arm once the panel
  // has reported a genuine release afterwards.
  recoveryHoldArmed = false;
  recoveryHoldReleasePolls = 0;
  // Offer the settings reset only when the calibration actually took. After a
  // rejected run the mapping is still the old broken one, and a dialog nobody
  // can tap is worse than no dialog.
  recoveryResetPromptPending = runRecoveryCalibration();
  queueRebuild(SCREEN_DASHBOARD);
}

static void serviceRecoveryHold() {
  if (currentScreen != SCREEN_DASHBOARD || rebuildQueued || developerPrompt) {
    resetRecoveryHold();
    return;
  }

  const bool held = recoveryInputHeld();

  if (!recoveryHoldArmed) {
    if (held) {
      recoveryHoldReleasePolls = 0;
    } else if (++recoveryHoldReleasePolls >= 3) {
      recoveryHoldArmed = true;
    }
    return;
  }

  if (!held) {
    resetRecoveryHold();
    return;
  }

  // Never arm while the vehicle is moving. A handlebar-mounted display collects
  // long contacts from rain, a sleeve or a glove, and calibration is not
  // something to fall into at speed. Stationary false triggers are merely
  // annoying, and the calibration targets time out on their own.
  DashboardValues values = {};
  if (!dashboardDemoModeEnabled && getLiveDashboardValues(values) &&
      values.speedKmh >= kRecoveryHoldMaxKmh) {
    resetRecoveryHold();
    return;
  }

  if (!recoveryHoldStartedMs) recoveryHoldStartedMs = millis();
  const uint32_t elapsed = millis() - recoveryHoldStartedMs;
  if (elapsed < RECOVERY_HOLD_SILENT_MS) return;

  if (!recoveryHoldPrompted) {
    recoveryHoldPrompted = true;
    showRecoveryHoldOverlay();
    // Drop the press LVGL is still tracking, now, while the finger is down.
    // Every hold that reaches the ring ends in a release, and LVGL would turn
    // that release into a click on the dashboard -- toggling the controls
    // overlay as the reward for aborting a recovery attempt. Waiting until the
    // release is visible here is too late: this runs on the 100 ms dashboard
    // tick and the input driver is read roughly three times as often.
    lv_indev_t *indev = lv_indev_get_next(NULL);
    if (indev) lv_indev_wait_release(indev);
  }
  if (elapsed >= RECOVERY_HOLD_TOTAL_MS) {
    enterRecoveryCalibration();
    return;
  }
  constexpr uint32_t kVisibleSpanMs = RECOVERY_HOLD_TOTAL_MS - RECOVERY_HOLD_SILENT_MS;
  const int32_t percent = (int32_t)((elapsed - RECOVERY_HOLD_SILENT_MS) * 100UL / kVisibleSpanMs);
  if (recoveryHoldArc) lv_arc_set_value(recoveryHoldArc, percent);
}

static const uint16_t kAutoReturnChoices[] = {30, 60, 120, 300, 600, 900, 1800, 3600};

static bool screenHandsBackToDashboard(ScreenMode mode) {
  return mode == SCREEN_MENU || mode == SCREEN_SUBMENU ||
         mode == SCREEN_PIN_SETUP || mode == SCREEN_PIN_LOCK || mode == SCREEN_TOUCH_TEST ||
         mode == SCREEN_FARDRIVER_BLE || mode == SCREEN_RIDE_LOGS;
}

static void hideAutoReturnOverlay() {
  autoReturnShownSeconds = -1;
  if (autoReturnOverlay) lv_obj_add_flag(autoReturnOverlay, LV_OBJ_FLAG_HIDDEN);
}

static void ensureAutoReturnOverlay() {
  if (autoReturnOverlay) return;
  lv_obj_t *scr = lv_scr_act();
  if (!scr) return;

  autoReturnOverlay = makePanel(scr, 48, 76, 224, 88, 9, c565(0xFB40), c565(0x2104), true);
  makePassive(autoReturnOverlay);
  makeLabelAt(autoReturnOverlay, 112, 13,
              txt("RETURNING HOME", "PALATAAN PÄÄNÄYTTÖÖN", "ZURÜCK ZUR ANZEIGE", "RETOUR ACCUEIL",
                  "VOLVIENDO A INICIO", "RITORNO ALLA HOME"),
              lv_color_white(), &lv_font_rajdhani_14, 3);
  makeLabelAt(autoReturnOverlay, 112, 35,
              txt("Touch anywhere to stay", "Pysy valikossa koskettamalla", "Zum Bleiben berühren", "Touchez pour rester",
                  "Toca para cancelar", "Tocca per restare"),
              c565(COLOR565_LABEL), &lv_font_rajdhani_12, 3);
  autoReturnCountdownLabel = makeLabelAt(autoReturnOverlay, 112, 51, "", c565(0xFB40),
                                         &lv_font_rajdhani_12, 3);

  autoReturnProgress = lv_bar_create(autoReturnOverlay);
  makePassive(autoReturnProgress);
  lv_obj_set_pos(autoReturnProgress, 16, 72);
  lv_obj_set_size(autoReturnProgress, 192, 7);
  lv_bar_set_range(autoReturnProgress, 0, 100);
  lv_bar_set_value(autoReturnProgress, 0, LV_ANIM_OFF);
  lv_obj_set_style_radius(autoReturnProgress, 3, LV_PART_MAIN);
  lv_obj_set_style_bg_color(autoReturnProgress, c565(COLOR565_DIM), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(autoReturnProgress, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(autoReturnProgress, 3, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(autoReturnProgress, c565(0xFB40), LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(autoReturnProgress, LV_OPA_COVER, LV_PART_INDICATOR);
}

void uiAutoReturnTick() {
  if (!autoReturnEnabled || currentScreen == SCREEN_CONFIG || firstBootSetupActive) {
    hideAutoReturnOverlay();
    return;
  }
  if (rebuildQueued || !screenHandsBackToDashboard(currentScreen)) return;
  // Leaving mid-wipe would hide both the progress and the outcome.
  if (sdClearActive) {
    hideAutoReturnOverlay();
    return;
  }
  const uint32_t inactiveMs = lv_disp_get_inactive_time(NULL);
  const uint32_t menuIdleReturnMs = (uint32_t)autoReturnTimeoutSeconds * 1000UL;
  if (inactiveMs >= menuIdleReturnMs) {
    hideAutoReturnOverlay();
    queueRebuild(SCREEN_DASHBOARD);
    return;
  }
  const uint32_t warningStartsMs = menuIdleReturnMs - kMenuIdleWarningMs;
  if (inactiveMs < warningStartsMs) {
    hideAutoReturnOverlay();
    return;
  }

  ensureAutoReturnOverlay();
  if (!autoReturnOverlay) return;
  lv_obj_clear_flag(autoReturnOverlay, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(autoReturnOverlay);
  const uint32_t remainingMs = menuIdleReturnMs - inactiveMs;
  const int seconds = LV_MAX(1, (int)((remainingMs + 999) / 1000));
  if (seconds != autoReturnShownSeconds) {
    autoReturnShownSeconds = seconds;
    char text[16];
    snprintf(text, sizeof(text), "%d s", seconds);
    setLabelText(autoReturnCountdownLabel, text);
  }
  const int progress = (int)((kMenuIdleWarningMs - remainingMs) * 100 / kMenuIdleWarningMs);
  lv_bar_set_value(autoReturnProgress, constrain(progress, 0, 100), LV_ANIM_OFF);
}

void uiSensorTick() {
  if ((!ldrLiveLabel && !ldrCalibrationLiveLabel) || rebuildQueued || currentScreen != SCREEN_SUBMENU ||
      submenuType != SUBMENU_DISPLAY)
    return;
  char text[64];
  if (ldrLiveLabel && ldrTargetLabel) {
    snprintf(text, sizeof(text), "%s %d", txt("RAW", "RAAKA", "ROH", "BRUT", "BRUTO", "GREZZO"),
             lightSensorRaw());
    setLabelText(ldrLiveLabel, text);
    snprintf(text, sizeof(text), "%s %d%%", txt("Target", "Tavoite", "Ziel", "Cible", "Objetivo", "Obiettivo"),
             lightSensorTargetPct());
    setLabelText(ldrTargetLabel, text);
  } else if (ldrLiveLabel) {
    snprintf(text, sizeof(text), "%s %d   %s %d%%",
             txt("RAW", "RAAKA", "ROH", "BRUT", "BRUTO", "GREZZO"), lightSensorRaw(),
             txt("Target", "Tavoite", "Ziel", "Cible", "Objetivo", "Obiettivo"), lightSensorTargetPct());
    setLabelText(ldrLiveLabel, text);
  }
  if (ldrCalibrationLiveLabel) {
    snprintf(text, sizeof(text), "%s %d", txt("RAW", "RAAKA", "ROH", "BRUT", "BRUTO", "GREZZO"),
             lightSensorRaw());
    setLabelText(ldrCalibrationLiveLabel, text);
  }
}

// ── Main menu ─────────────────────────────────────────────────────────────────

enum MenuButtonId {
  BTN_BACK = 1,
  BTN_NEXT,
  BTN_PREV,
  BTN_SAVE,
  BTN_SETTINGS_CONTROLLER,
  BTN_SETTINGS_DEVELOPER,
  BTN_MENU_CONTROLLER_CONNECTION,
  BTN_MENU_DASH_UI,
  BTN_MENU_LANGUAGE,
  BTN_MENU_UNITS,
  BTN_MENU_VESC,
  BTN_MENU_CONTROLLER_CONFIG,
  BTN_MENU_PIN,
  BTN_MENU_LOGGING,
  BTN_MENU_DISPLAY,
  BTN_MENU_INFO,
  BTN_MENU_CHANGELOG,
  BTN_MENU_BATTERY,
  BTN_MENU_RESET,
  BTN_DEV_PROMPT_CANCEL,
  BTN_DEV_PROMPT_ENABLE,
  BTN_DEV_PROMPT_DISABLE,
  BTN_FIRMWARE_DOWNGRADE_CONFIRM,
  BTN_HIDE_DEVELOPER_OPTIONS,
  BTN_RESET_CANCEL,
  BTN_RECOVERY_RESET_CONFIRM,
  BTN_PIN_SETUP_DISABLE,
  BTN_PIN_SETUP_CHANGE,
  BTN_COLOR_TOGGLE = 60,  // opens/closes the DASH UI colour strip
  BTN_GRADIENT_TOGGLE,
  BTN_GRADIENT_CUSTOM,
  BTN_GRADIENT_ORIENTATION,
  BTN_GRADIENT_BELL,
  BTN_GRADIENT_ENABLE,
  BTN_GRADIENT_BACK,
  BTN_COLORS_MASTER,
  BTN_DATA_TAB = 380,
  BTN_THEME_DARK,
  BTN_THEME_LIGHT,
  BTN_THEME_AUTO,
  BTN_AUTO_BRIGHTNESS_YES,
  BTN_AUTO_BRIGHTNESS_NO,
  BTN_RESET_CURRENT_UI,
  BTN_DATA_UP,
  BTN_DATA_DOWN,
  BTN_DATA_BACK,
  BTN_EXPLORE_RIDE_LOGS,
  BTN_RIDE_LOGS_PREV,
  BTN_RIDE_LOGS_NEXT,
  BTN_RIDE_LOGS_CLEAR_SD,
  BTN_RIDE_LOGS_CLEAR_SD_CONFIRM,
  BTN_RIDE_LOGS_CLEAR_SD_DONE,
  BTN_CONTROLLER_DIAGNOSTICS,
  BTN_FIRMWARE_UPDATE_ACTION,
  BTN_USER_COMPANION,
  BTN_COMPANION_ACTION,
  BTN_DEMO_MODE,
  BTN_AUTO_RETURN,
  BTN_LIGHT_SENSOR_CALIBRATE,
  BTN_LIGHT_SENSOR_SET_DARK,
  BTN_LIGHT_SENSOR_SET_BRIGHT,
  BTN_FARDRIVER_DISCONNECT,
  BTN_FARDRIVER_FORGET,
  BTN_CONTROLLER_VESC = 600,
  BTN_CONTROLLER_FARDRIVER,
  BTN_CONTROLLER_UART,
  BTN_CONTROLLER_BLE,
  BTN_CONTROLLER_RETRY,
  BTN_CONTROLLER_CONTINUE,
  BTN_CONTROLLER_CANCEL_APPLY,
  BTN_CONTROLLER_APPLY,
  BTN_CONTROLLER_LIST_UP,
  BTN_CONTROLLER_LIST_DOWN,
};

// Bases for the ranges of ids that carry an index: the button's id is the base
// plus an accent, language, data item or device number. They are plain ints
// rather than MenuButtonId members because that index is usually itself an
// enumerator, and C++20 deprecates arithmetic between two enumeration types.
// Nothing treats an id as a MenuButtonId value -- every handler takes an int.
constexpr int BTN_COLOR_BASE = 70;             // + accent index
constexpr int BTN_OPTION_BASE = 100;           // + option index, per-screen semantics
constexpr int BTN_GRADIENT_COLOR_BASE = 220;   // + accent index
constexpr int BTN_BACKGROUND_STYLE_BASE = 240; // + background style
constexpr int BTN_DASH_GRID_BASE = 260;        // + dashboard theme index
constexpr int BTN_DATA_SLOT_BASE = 400;        // + dashboard data slot
constexpr int BTN_DATA_CHOICE_BASE = 420;      // + data item
constexpr int BTN_CONTROLLER_DEVICE_BASE = 620;// + scanned device index

static lv_obj_t *developerHoldButton = NULL;
static lv_timer_t *developerHoldTimer = NULL;
static uint32_t developerHoldStartedMs = 0;
static bool developerHoldColorStarted = false;
static bool developerHoldTriggered = false;
static lv_color_t developerHoldStartColor;

static void closeDeveloperPrompt();
static void showDeveloperStatusToast(bool alreadyEnabled);
static void showDisableDeveloperPrompt();
static void showLightSensorDarkPrompt();
static void showLightSensorBrightPrompt();
static void developerDisplayButtonEventCb(lv_event_t *e);

static const uint32_t kResetCountdownMs = 4000;

static void cancelResetCountdown() {
  if (resetCountdownTimer) {
    lv_timer_del(resetCountdownTimer);
    resetCountdownTimer = NULL;
  }
  resetCountdownActive = false;
  resetCountdownMode = -1;
  resetProgressBar = NULL;
  resetProgressLabel = NULL;
}

static void resetCountdownTick(lv_timer_t *timer) {
  const uint32_t rawElapsed = millis() - resetCountdownStartedMs;
  const uint32_t elapsed = rawElapsed < kResetCountdownMs ? rawElapsed : kResetCountdownMs;
  if (resetProgressBar) lv_bar_set_value(resetProgressBar, elapsed * 1000UL / kResetCountdownMs, LV_ANIM_OFF);
  if (resetProgressLabel) {
    char text[28];
    const float remaining = (kResetCountdownMs - elapsed) / 1000.0F;
    snprintf(text, sizeof(text), "%s %.1f s",
             txt("RESET IN", "NOLLAUS", "RESET IN", "RÉINIT. DANS", "REINICIO EN", "RESET TRA"), remaining);
    lv_label_set_text(resetProgressLabel, text);
  }
  if (elapsed < kResetCountdownMs) return;

  lv_timer_del(timer);
  resetCountdownTimer = NULL;
  const bool includeHistory = resetCountdownMode == 0;  // mode 0 is the full reset
  resetCountdownActive = false;
  resetCountdownMode = -1;
  resetProgressBar = NULL;
  resetProgressLabel = NULL;
  resetAppSettings(includeHistory);
  firstBootSetupActive = false;
  resetConfirmMode = -1;
  // Controller transports and their telemetry tasks are selected at boot.
  // Restart into the clean first-boot flow so the reset UART/BLE choice is
  // applied instead of leaving the previous transport alive in memory.
  requestControllerRestart();
}

static void startResetCountdown() {
  resetCountdownMode = resetConfirmMode;
  resetCountdownStartedMs = millis();
  resetCountdownActive = true;
  if (submenuType == SUBMENU_DASH_UI)
    queueLowMemoryRebuild(SCREEN_SUBMENU);
  else
    queueRebuild(SCREEN_SUBMENU);
}

// Shared by the menu action and the selector overlay built later in this file.
static bool colorPaletteOpen = false;
static bool gradientPanelOpen = false;
static bool dataPanelOpen = false;
static bool dataChoiceOpen = false;
static uint8_t dataListOffset = 0;
static uint8_t dataChoiceOffset = 0;
static uint8_t dataSelectedSlot = 0;
static lv_obj_t *selectorPopup = NULL;
static lv_obj_t *selectorScreen = NULL;
static lv_obj_t *selectorArrowLeft = NULL;
static lv_obj_t *selectorArrowRight = NULL;
static lv_obj_t *selectorHousing = NULL;
static lv_obj_t *selectorBottomRow = NULL;
static void selectedBadgeDrawCb(lv_event_t *e);
static void toggleSelectorPalette();
static void toggleColorsMaster();
static void closeSelectorColors();
static void openSelectorGradientPanel();
static void openSelectorDataPanel();
static void rebuildSelectorPopup();
static void backupDashboardCustomizations() {
  memcpy(dashboardCustomizationBackup, dashboardCustomizations, sizeof(dashboardCustomizationBackup));
}

static void restoreDashboardCustomizations() {
  memcpy(dashboardCustomizations, dashboardCustomizationBackup, sizeof(dashboardCustomizationBackup));
}

static void selectBackgroundStyle(int style) {
  dashboardGradientEnabled = style != 0;
  dashboardGradientHorizontal = style == 2;
  dashboardGradientReverse = false;
  dashboardGradientBell = style == 3;
  dashboardGradientPosition = style == 3 ? 50 : 15;
}

static int selectedBackgroundStyle() {
  if (!dashboardGradientEnabled) return 0;
  if (dashboardGradientBell) return 3;
  return dashboardGradientHorizontal ? 2 : 1;
}

static void selectOrRotateBackgroundStyle(int style) {
  if (style != 0 && style == selectedBackgroundStyle()) {
    if (style == 3) {
      // Bell has four useful arrangements. Keep the first repeat compatible
      // with the old behaviour (vertical -> horizontal), then offer both
      // inverse variants where the colour sits at the edges and black at the
      // centre: V, H, V inverse, H inverse.
      const uint8_t state = (dashboardGradientReverse ? 2 : 0) +
                            (dashboardGradientHorizontal ? 1 : 0);
      const uint8_t next = (state + 1) & 3;
      dashboardGradientHorizontal = (next & 1) != 0;
      dashboardGradientReverse = (next & 2) != 0;
    } else {
      dashboardGradientReverse = !dashboardGradientReverse;
    }
    return;
  }
  selectBackgroundStyle(style);
}

static void menuAction(int id) {
  switch (id) {
    case BTN_DEV_PROMPT_CANCEL:
      closeDeveloperPrompt();
      return;
    case BTN_DEV_PROMPT_ENABLE:
      closeDeveloperPrompt();
      developerOptionsEnabled = true;
      saveAppSettings();
      // The second page is where the new Developer Options card appears.
      settingsMenuLevel = SETTINGS_MENU_MORE;
      queueStatusNotice(NOTICE_DEVELOPER_ENABLED);
      queueRebuild(SCREEN_MENU);
      return;
    case BTN_DEV_PROMPT_DISABLE:
      closeDeveloperPrompt();
      developerOptionsEnabled = false;
      saveAppSettings();
      settingsMenuLevel = SETTINGS_MENU_DISPLAY;
      queueRebuild(SCREEN_MENU);
      return;
    case BTN_RECOVERY_RESET_CONFIRM:
      closeDeveloperPrompt();
      resetAppSettings(false);
      // Controller transports are selected at boot, so the cleared choice only
      // takes effect on a restart -- the same reason the menu reset restarts.
      requestControllerRestart();
      return;
    case BTN_HIDE_DEVELOPER_OPTIONS:
      showDisableDeveloperPrompt();
      return;
    case BTN_DEMO_MODE:
      submenuType = SUBMENU_DEMO;
      queueRebuild(SCREEN_SUBMENU);
      return;
    case BTN_AUTO_RETURN:
      submenuType = SUBMENU_AUTO_RETURN;
      queueRebuild(SCREEN_SUBMENU);
      return;
    case BTN_BACK:
      // Display is the first page, so it leaves Settings. The second page
      // steps back to it, and the pages opened from the second page return
      // there.
      if (settingsMenuLevel == SETTINGS_MENU_DISPLAY) {
        queueRebuild(SCREEN_DASHBOARD);
        return;
      }
      settingsMenuLevel = settingsMenuLevel == SETTINGS_MENU_MORE ? SETTINGS_MENU_DISPLAY : SETTINGS_MENU_MORE;
      queueRebuild(SCREEN_MENU);
      return;
    case BTN_NEXT:
      if (settingsMenuLevel != SETTINGS_MENU_DISPLAY) return;
      settingsMenuLevel = SETTINGS_MENU_MORE;
      queueRebuild(SCREEN_MENU);
      return;
    case BTN_SETTINGS_CONTROLLER:
      settingsMenuLevel = SETTINGS_MENU_CONTROLLER;
      queueRebuild(SCREEN_MENU);
      return;
    case BTN_SETTINGS_DEVELOPER:
      if (!developerOptionsEnabled) return;
      settingsMenuLevel = SETTINGS_MENU_DEVELOPER;
      queueRebuild(SCREEN_MENU);
      return;
    case BTN_MENU_CONTROLLER_CONNECTION:
      pendingControllerType = controllerType;
      pendingControllerConnection = controllerConnection;
      controllerSetupStage = CONTROLLER_SETUP_TYPE;
      controllerDeviceOffset = 0;
      submenuType = SUBMENU_CONNECTION;
      break;
    case BTN_MENU_DASH_UI:
      submenuType = SUBMENU_DASH_UI;
      dashUiPreviewMode = dashboardMode;
      dashUiGridOpen = true;
      dashUiGridPage = 0;
      dashUiGridReturnPage = 0;
      selectorSavedFeedback = false;
      backupDashboardCustomizations();
      accentThemeBackup = accentTheme;  // colour edits preview live; BACK restores
      gradientEnabledBackup = dashboardGradientEnabled;
      gradientHorizontalBackup = dashboardGradientHorizontal;
      gradientReverseBackup = dashboardGradientReverse;
      gradientBellBackup = dashboardGradientBell;
      gradientPositionBackup = dashboardGradientPosition;
      gradientThemeBackup = dashboardGradientTheme;
      colorPaletteOpen = false;
      gradientPanelOpen = false;
      dataPanelOpen = false;
      dataChoiceOpen = false;
      break;
    case BTN_MENU_LANGUAGE:
      submenuType = SUBMENU_LANGUAGE;
      break;
    case BTN_MENU_UNITS:
      submenuType = SUBMENU_UNITS;
      break;
    case BTN_MENU_VESC:
      submenuType = SUBMENU_VESC;
      vehiclePage = 0;
      break;
    case BTN_MENU_CONTROLLER_CONFIG:
      submenuType = SUBMENU_CONTROLLER_CONFIG;
      break;
    case BTN_MENU_PIN:
      pinSetupFromConfigurator = false;
      queueRebuild(SCREEN_PIN_SETUP);
      return;
    case BTN_MENU_LOGGING:
      submenuType = SUBMENU_LOGGING;
      break;
    case BTN_MENU_DISPLAY:
      submenuType = SUBMENU_DISPLAY;
      displayPage = 0;
      break;
    case BTN_MENU_INFO:
      submenuType = SUBMENU_DISPLAY_INFO;
      break;
    case BTN_MENU_CHANGELOG:
      submenuType = SUBMENU_CHANGELOG;
      break;
    case BTN_CONTROLLER_DIAGNOSTICS:
      queueLowMemoryRebuild(SCREEN_FARDRIVER_BLE);
      return;
    case BTN_MENU_BATTERY:
      submenuType = SUBMENU_BATTERY;
      batteryPage = 0;
      break;
    case BTN_MENU_RESET:
      submenuType = SUBMENU_RESET;
      break;
    default:
      return;
  }
  if (submenuType == SUBMENU_DASH_UI)
    queueLowMemoryRebuild(SCREEN_SUBMENU);
  else if (submenuType == SUBMENU_LOGGING)
    queueLowMemoryRebuild(SCREEN_SUBMENU);
  else
    queueRebuild(SCREEN_SUBMENU);
}

// Also a child of the screen it opened over, and the auto-return timer can
// rebuild that screen while it is up. Without this the pointer outlives the
// object: closing would delete freed memory, and showDeveloperPrompt would see
// a non-null pointer and refuse to open ever again.
static void developerPromptDeleteCb(lv_event_t *) {
  developerPrompt = NULL;
  ldrCalibrationLiveLabel = NULL;
}

static void closeDeveloperPrompt() {
  if (!developerPrompt) return;
  lv_obj_t *prompt = developerPrompt;
  developerPrompt = NULL;
  ldrCalibrationLiveLabel = NULL;
  lv_obj_del(prompt);
}

static const char *statusNoticeText(StatusNotice notice) {
  switch (notice) {
    case NOTICE_DEVELOPER_ENABLED:
      return txt("YOU ARE NOW A DEVELOPER!", "OLET NYT KEHITTÄJÄ!", "SIE SIND JETZT ENTWICKLER!",
                 "VOUS ÊTES DÉSORMAIS DÉVELOPPEUR !", "¡AHORA ERES DESARROLLADOR!",
                 "ORA SEI UNO SVILUPPATORE!");
    case NOTICE_DEVELOPER_ALREADY_ENABLED:
      return txt("DEVELOPER MODE ALREADY ON", "KEHITTÄJÄTILA ON JO KÄYTÖSSÄ", "ENTWICKLERMODUS BEREITS AN",
                 "MODE DÉVELOPPEUR DÉJÀ ACTIF", "MODO DESARROLLADOR YA ACTIVO",
                 "MODALITÀ SVILUPPATORE GIÀ ATTIVA");
    case NOTICE_RIDE_SAVED:
      return txt("RIDE SAVED", "AJO TALLENNETTU", "FAHRT GESPEICHERT", "TRAJET SAUVÉ", "VIAJE GUARDADO",
                 "VIAGGIO SALVATO");
    case NOTICE_SD_CARD_READY:
      return txt("SD CARD READY", "SD-KORTTI VALMIS", "SD-KARTE BEREIT", "CARTE SD PRÊTE",
                 "TARJETA SD LISTA", "SCHEDA SD PRONTA");
    case NOTICE_SD_CARD_REMOVED:
      return txt("SD CARD REMOVED", "SD-KORTTI POISTETTU", "SD-KARTE ENTFERNT", "CARTE SD RETIRÉE",
                 "TARJETA SD RETIRADA", "SCHEDA SD RIMOSSA");
    case NOTICE_SD_CARD_REMOVED_RECORDING:
      return txt("SD REMOVED - RECORDING STOPPED", "SD POISTETTU - TALLENNUS PYSÄYTETTY",
                 "SD ENTFERNT - AUFNAHME BEENDET", "SD RETIRÉE - ENREGISTREMENT ARRÊTÉ",
                 "SD RETIRADA - GRABACIÓN DETENIDA", "SD RIMOSSA - REGISTRAZIONE FERMA");
    case NOTICE_SD_WRITE_FAILED:
      return txt("SD WRITE FAILED - RECORDING STOPPED", "SD-KIRJOITUS EPÄONNISTUI",
                 "SD-SCHREIBFEHLER - AUFNAHME BEENDET", "ÉCHEC ÉCRITURE SD - ENREG. ARRÊTÉ",
                 "FALLO DE ESCRITURA SD", "ERRORE SCRITTURA SD");
    case NOTICE_SD_CARD_NOT_READY:
      return txt("SD CARD NOT READY", "SD-KORTTI EI VALMIS", "SD-KARTE NICHT BEREIT",
                 "CARTE SD NON PRÊTE", "TARJETA SD NO LISTA", "SCHEDA SD NON PRONTA");
    case NOTICE_VESC_PROFILE_SAVED:
      return txt("LOCAL VESC PROFILE SAVED", "VESC-PROFIILI TALLENNETTU", "VESC-PROFIL LOKAL GESPEICHERT",
                 "PROFIL VESC SAUVÉ", "PERFIL VESC GUARDADO", "PROFILO VESC SALVATO");
    case NOTICE_VESC_OFFLINE_SAVED:
      return txt("VESC OFFLINE - LOCAL SAVE", "VESC EI YHTEYTTÄ - TALLENNETTU",
                 "VESC OFFLINE - LOKAL GESPEICHERT", "VESC HORS LIGNE - SAUVÉ",
                 "VESC SIN CONEXIÓN - GUARDADO", "VESC OFFLINE - SALVATO");
    case NOTICE_RESTART_DISPLAY_TO_APPLY:
      return txt("RESTART DISPLAY TO APPLY", "KÄYNNISTÄ NÄYTTÖ UUDELLEEN", "DISPLAY NEU STARTEN",
                 "REDÉMARREZ L'ÉCRAN", "REINICIE LA PANTALLA", "RIAVVIARE LO SCHERMO");
    case NOTICE_LIGHT_SENSOR_CALIBRATED:
      return txt("LIGHT SENSOR CALIBRATED", "VALOANTURI KALIBROITU", "LICHTSENSOR KALIBRIERT",
                 "CAPTEUR DE LUMIÈRE CALIBRÉ", "SENSOR DE LUZ CALIBRADO", "SENSORE LUCE CALIBRATO");
    case NOTICE_LIGHT_SENSOR_RANGE_TOO_SMALL:
      return txt("LIGHT RANGE TOO SMALL - TRY AGAIN", "VALOALUE LIIAN PIENI - YRITÄ UUDELLEEN",
                 "LICHTBEREICH ZU KLEIN - ERNEUT VERSUCHEN", "PLAGE LUMINEUSE TROP FAIBLE - RÉESSAYEZ",
                 "RANGO DE LUZ MUY PEQUEÑO - REPITA", "INTERVALLO LUCE TROPPO RIDOTTO - RIPROVA");
    case NOTICE_SETUP_COMPLETE:
      return txt("SETUP COMPLETE - SETTINGS SAVED", "KÄYTTÖÖNOTTO VALMIS - ASETUKSET TALLENNETTU",
                 "EINRICHTUNG FERTIG - EINSTELLUNGEN GESPEICHERT",
                 "CONFIGURATION TERMINÉE - RÉGLAGES SAUVEGARDÉS",
                 "CONFIGURACIÓN LISTA - AJUSTES GUARDADOS",
                 "CONFIGURAZIONE COMPLETA - IMPOSTAZIONI SALVATE");
    case NOTICE_CONTROLLER_SETUP_APPLIED:
      return txt("CONTROLLER SETUP SAVED", "OHJAINASETUKSET TALLENNETTU",
                 "CONTROLLER-SETUP GESPEICHERT", "CONFIGURATION CONTRÔLEUR ENREGISTRÉE",
                 "CONFIGURACIÓN DEL CONTROLADOR GUARDADA", "CONFIGURAZIONE CENTRALINA SALVATA");
    default:
      return "";
  }
}

static bool statusNoticeIsWarning(StatusNotice notice) {
  return notice == NOTICE_SD_CARD_REMOVED || notice == NOTICE_SD_CARD_REMOVED_RECORDING ||
         notice == NOTICE_SD_WRITE_FAILED || notice == NOTICE_SD_CARD_NOT_READY ||
         notice == NOTICE_VESC_OFFLINE_SAVED || notice == NOTICE_RESTART_DISPLAY_TO_APPLY ||
         notice == NOTICE_LIGHT_SENSOR_RANGE_TOO_SMALL;
}

static void statusToastDeleteCb(lv_event_t *) {
  statusToast = NULL;
  if (statusToastTimer) {
    lv_timer_del(statusToastTimer);
    statusToastTimer = NULL;
  }
}

static void statusToastTimerCb(lv_timer_t *timer) {
  if (!statusToast) {
    statusToastTimer = NULL;
    lv_timer_del(timer);
    return;
  }
  if (!statusToastLeaving) {
    statusToastLeaving = true;
    // Match the Customize/Reset housing exactly: its visible housing rests at
    // global y=197 and leaves through global y=244 over the shared 140 ms.
    slideSelectorPart(statusToast, cyd_ui::kStatusToastHiddenY, false);
    lv_timer_set_period(timer, kSelectorSlideMs + 40);
    lv_timer_reset(timer);
    return;
  }

  lv_obj_t *toast = statusToast;
  statusToast = NULL;
  statusToastTimer = NULL;
  lv_timer_del(timer);
  lv_obj_del(toast);
}

static void showStatusNotice(StatusNotice notice) {
  if (notice == NOTICE_NONE) return;
  if (statusToast) lv_obj_del(statusToast);
  statusToastLeaving = false;
  const char *message = statusNoticeText(notice);
  const int toastWidth = compactHousingWidth(message, &lv_font_rajdhani_12, 44);
  statusToast = makePanel(lv_scr_act(), (cyd_ui::kScreenWidth - toastWidth) / 2,
                          cyd_ui::kStatusToastHiddenY, toastWidth, 40, 6,
                          cyd_ui::chromeAccent(), cyd_ui::panelSurface(), true);
  makePassive(statusToast);
  lv_obj_set_style_border_width(statusToast, cyd_ui::kBorderWidth, 0);
  lv_obj_add_event_cb(statusToast, statusToastDeleteCb, LV_EVENT_DELETE, NULL);
  lv_obj_t *label = makeLabelAt(statusToast, toastWidth / 2, 12, message, lv_color_white(),
                                &lv_font_rajdhani_12, 3);
  // Every notice string is static program text. Release the copied LVGL label
  // buffer immediately so a toast can coexist with object-heavy menu screens.
  lv_label_set_text_static(label, message);
  lv_obj_set_size(label, toastWidth - 2 * cyd_ui::kControlTitleInset, 24);
  lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
  lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_line_space(label, -4, 0);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
  // Unlike the selector housing, this object has just been created. Commit
  // its off-screen starting state before starting the animation so LVGL does
  // not collapse creation and the first movement into one rendered frame.
  lv_obj_update_layout(statusToast);
  lv_refr_now(NULL);
  slideSelectorPart(statusToast, cyd_ui::kStatusToastVisibleY, true);
  statusToastTimer = lv_timer_create(statusToastTimerCb, statusNoticeIsWarning(notice) ? 2600 : 1700, NULL);
  lv_timer_set_repeat_count(statusToastTimer, -1);
}

static void showPendingStatusNotice() {
  if (pendingStatusNotice == NOTICE_NONE) return;
  const StatusNotice notice = pendingStatusNotice;
  pendingStatusNotice = NOTICE_NONE;
  showStatusNotice(notice);
}

static void showDeveloperStatusToast(bool alreadyEnabled) {
  showStatusNotice(alreadyEnabled ? NOTICE_DEVELOPER_ALREADY_ENABLED : NOTICE_DEVELOPER_ENABLED);
}

// Canonical two-choice confirmation used by menu operations. Keep modal
// confirmations visually distinct from transient status notices, but route
// every confirmation through this housing instead of hand-building variants.
static void showConfirmationDialog(const char *headingText, const char *bodyText,
                                   const char *confirmText, int confirmAction) {
  if (developerPrompt) return;
  developerPrompt = lv_obj_create(lv_scr_act());
  lv_obj_add_event_cb(developerPrompt, developerPromptDeleteCb, LV_EVENT_DELETE, NULL);
  lv_obj_remove_style_all(developerPrompt);
  lv_obj_set_pos(developerPrompt, 0, 0);
  lv_obj_set_size(developerPrompt, cyd_ui::kScreenWidth, cyd_ui::kScreenHeight);
  lv_obj_set_style_bg_color(developerPrompt, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(developerPrompt, LV_OPA_70, 0);
  lv_obj_add_flag(developerPrompt, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(developerPrompt, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *panel = makePanel(developerPrompt, 20, 40, 280, 160, cyd_ui::kPanelRadius,
                              cyd_ui::chromeAccent(), cyd_ui::panelSurface(), true);
  lv_obj_t *heading = makeLabelAt(panel, 140, 11, headingText, lv_color_white(),
                                  &lv_font_rajdhani_12, 3);
  lv_obj_set_x(heading, 10);
  lv_obj_set_width(heading, 260);
  lv_label_set_long_mode(heading, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(heading, LV_TEXT_ALIGN_CENTER, 0);

  lv_obj_t *body = makeLabelAt(panel, 140, 47, bodyText, cyd_ui::secondaryText(),
                               &lv_font_rajdhani_12, 3);
  lv_obj_set_pos(body, 12, 43);
  lv_obj_set_size(body, 256, 67);
  lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(body, LV_TEXT_ALIGN_CENTER, 0);
  makeMenuButton(panel, 10, 116, 124, 34,
                 txt("CANCEL", "PERUUTA", "ABBRECHEN", "ANNULER", "CANCELAR", "ANNULLA"), "", false,
                 BTN_DEV_PROMPT_CANCEL);
  makeMenuButton(panel, 146, 116, 124, 34, confirmText, "", false, confirmAction);
}

static void showLightSensorDarkPrompt() {
  pendingLightSensorDarkRaw = -1;
  showConfirmationDialog(
      txt("CALIBRATE DARK LEVEL", "KALIBROI PIMEÄ TASO", "DUNKELWERT KALIBRIEREN", "CALIBRER LE NIVEAU SOMBRE",
          "CALIBRAR NIVEL OSCURO", "CALIBRA LIVELLO BUIO"),
      txt("Cover the light sensor, wait for the RAW value to settle, then save the dark level.",
          "Peitä valoanturi, odota RAAKA-arvon tasaantumista ja tallenna pimeä taso.",
          "Lichtsensor abdecken, stabilen ROH-Wert abwarten und Dunkelwert speichern.",
          "Couvrez le capteur, attendez que la valeur BRUT se stabilise, puis enregistrez.",
          "Cubra el sensor, espere a que se estabilice BRUTO y guarde el nivel oscuro.",
          "Copri il sensore, attendi che GREZZO si stabilizzi e salva il livello buio."),
      txt("SET DARK", "ASETA PIMEÄ", "DUNKEL SPEICHERN", "RÉGLER SOMBRE", "FIJAR OSCURO", "IMPOSTA BUIO"),
      BTN_LIGHT_SENSOR_SET_DARK);
  if (developerPrompt) {
    ldrCalibrationLiveLabel = makeLabelAt(developerPrompt, 160, 127, "", cyd_ui::chromeAccent(),
                                          &lv_font_rajdhani_12, 3);
    uiSensorTick();
  }
}

static void showLightSensorBrightPrompt() {
  showConfirmationDialog(
      txt("CALIBRATE BRIGHT LEVEL", "KALIBROI KIRKAS TASO", "HELLWERT KALIBRIEREN", "CALIBRER LE NIVEAU CLAIR",
          "CALIBRAR NIVEL CLARO", "CALIBRA LIVELLO CHIARO"),
      txt("Expose the sensor to normal bright light, wait for the RAW value to settle, then save the bright level.",
          "Vie anturi normaaliin kirkkaaseen valoon, odota RAAKA-arvon tasaantumista ja tallenna kirkas taso.",
          "Sensor normalem hellem Licht aussetzen, stabilen ROH-Wert abwarten und Hellwert speichern.",
          "Exposez le capteur à une lumière vive normale, attendez la stabilisation, puis enregistrez.",
          "Exponga el sensor a luz brillante normal, espere a que se estabilice y guarde el nivel claro.",
          "Esponi il sensore a luce intensa normale, attendi la stabilizzazione e salva il livello chiaro."),
      txt("SET BRIGHT", "ASETA KIRKAS", "HELL SPEICHERN", "RÉGLER CLAIR", "FIJAR CLARO", "IMPOSTA CHIARO"),
      BTN_LIGHT_SENSOR_SET_BRIGHT);
  if (developerPrompt) {
    ldrCalibrationLiveLabel = makeLabelAt(developerPrompt, 160, 127, "", cyd_ui::chromeAccent(),
                                          &lv_font_rajdhani_12, 3);
    uiSensorTick();
  }
}

static void showDeveloperPrompt() {
  if (developerPrompt || developerOptionsEnabled) return;
  showConfirmationDialog(
      txt("ENABLE DEVELOPER OPTIONS?", "OTA KEHITTÄJÄASETUKSET KÄYTTÖÖN?",
          "ENTWICKLEROPTIONEN AKTIVIEREN?", "ACTIVER LES OPTIONS DÉVELOPPEUR ?",
          "¿ACTIVAR OPCIONES DE DESARROLLADOR?", "ATTIVARE LE OPZIONI SVILUPPATORE?"),
      txt("Enable advanced hardware diagnostics and display compatibility controls.",
          "Ota käyttöön laitteiston diagnostiikka ja näytön yhteensopivuusasetukset.",
          "Erweiterte Hardwarediagnose und Display-Einstellungen aktivieren.",
          "Activer les diagnostics matériels et les réglages avancés de l'écran.",
          "Activar diagnósticos y controles avanzados de la pantalla.",
          "Attiva diagnostica hardware e controlli avanzati del display."),
      txt("ENABLE", "OTA KÄYTTÖÖN", "AKTIVIEREN", "ACTIVER", "ACTIVAR", "ATTIVA"),
      BTN_DEV_PROMPT_ENABLE);
}

// Empties the card completely, not just the rides the list can show: broken
// logs and anything another device left behind go too, so the wording has to
// say so rather than talk about ride logs.
static void showClearSdCardPrompt() {
  if (developerPrompt) return;
  showConfirmationDialog(
      txt("CLEAR THE SD CARD?", "TYHJENNETÄÄNKÖ SD-KORTTI?", "SD-KARTE LEEREN?", "VIDER LA CARTE SD ?",
          "¿BORRAR LA TARJETA SD?", "SVUOTARE LA SCHEDA SD?"),
      txt("Deletes every file on the card, including unreadable logs. This cannot be undone.",
          "Poistaa kortilta kaikki tiedostot, myös vialliset lokit. Tätä ei voi perua.",
          "Löscht alle Dateien der Karte, auch unlesbare Logs. Nicht umkehrbar.",
          "Supprime tous les fichiers de la carte, journaux illisibles compris. Irréversible.",
          "Borra todos los archivos de la tarjeta, incluidos los registros ilegibles. Irreversible.",
          "Elimina tutti i file della scheda, anche i registri illeggibili. Irreversibile."),
      txt("CLEAR", "TYHJENNÄ", "LEEREN", "VIDER", "BORRAR", "SVUOTA"),
      BTN_RIDE_LOGS_CLEAR_SD_CONFIRM);
}

// ── SD card clear progress ──────────────────────────────────────────────────
// Confirming the clear swaps the prompt for this dialog in the same housing: a
// bar fed by the writer's own count, then the outcome. Success returns to the
// ride list on its own once the rider has had time to read it; a failure waits
// for OK. While it is open the list neither rebuilds under it (the wipe moves
// the storage revision constantly) nor hands back to the dashboard.
static lv_timer_t *sdClearTimer = NULL;
static lv_obj_t *sdClearHeading = NULL;
static lv_obj_t *sdClearBody = NULL;
static lv_obj_t *sdClearBar = NULL;
static lv_obj_t *sdClearCount = NULL;
static uint32_t sdClearStartedMs = 0;
static uint32_t sdClearFinishedMs = 0;
// A near-empty card clears in a blink; hold the bar long enough to register.
static constexpr uint32_t kSdClearMinimumMs = 900;
static constexpr uint32_t kSdClearDoneHoldMs = 1600;

static void stopSdClearTimer() {
  if (sdClearTimer) {
    lv_timer_del(sdClearTimer);
    sdClearTimer = NULL;
  }
}

static void forgetSdClearWidgets() {
  sdClearHeading = sdClearBody = sdClearBar = sdClearCount = NULL;
}

static void finishSdClear() {
  stopSdClearTimer();
  closeDeveloperPrompt();
  forgetSdClearWidgets();
  sdClearActive = false;
  // The rider waited without touching anything; do not let the menu timeout
  // count that against them the moment the list comes back.
  lv_disp_trig_activity(NULL);
  rideLogsPage = 0;
  queueLowMemoryRebuild(SCREEN_RIDE_LOGS);
}

// Brings the dialog up to date. `ignoreMinimum` lets preview captures show an
// outcome without waiting out the on-screen minimum.
static void refreshSdClearDialog(bool ignoreMinimum) {
  if (!developerPrompt || !sdClearBar || !sdClearActive) return;
  const RideLogWipeStatus wipe = rideLoggerWipeStatus();
  const uint32_t now = millis();
  const bool finished = wipe.state == RideLogWipeStatus::Done || wipe.state == RideLogWipeStatus::Failed;
  const bool showOutcome = finished && (ignoreMinimum || now - sdClearStartedMs >= kSdClearMinimumMs);

  if (!showOutcome) {
    char count[48];
    if (wipe.state == RideLogWipeStatus::Removing || finished) {
      const uint32_t total = wipe.total ? wipe.total : 1;
      const uint32_t shown = finished ? total : LV_MIN(wipe.removed, total);
      lv_bar_set_value(sdClearBar, (int32_t)(shown * 100 / total), LV_ANIM_ON);
      snprintf(count, sizeof(count), "%lu / %lu %s", (unsigned long)(finished ? wipe.total : shown),
               (unsigned long)wipe.total,
               txt("removed", "poistettu", "gelöscht", "supprimés", "borrados", "eliminati"));
    } else {
      lv_bar_set_value(sdClearBar, 0, LV_ANIM_OFF);
      snprintf(count, sizeof(count), "%s",
               txt("Checking the card...", "Tarkistetaan korttia...", "Karte wird geprüft...",
                   "Vérification de la carte...", "Comprobando la tarjeta...", "Controllo della scheda..."));
    }
    setLabelText(sdClearCount, count);
    return;
  }

  if (!sdClearFinishedMs) {
    sdClearFinishedMs = now ? now : 1;
    const bool done = wipe.state == RideLogWipeStatus::Done;
    lv_bar_set_value(sdClearBar, done ? 100 : lv_bar_get_value(sdClearBar), LV_ANIM_ON);
    setLabelText(sdClearHeading,
                 done ? txt("SD CARD CLEARED", "SD-KORTTI TYHJENNETTY", "SD-KARTE GELEERT", "CARTE SD VIDÉE",
                            "TARJETA SD BORRADA", "SCHEDA SD SVUOTATA")
                      : txt("COULD NOT CLEAR THE CARD", "KORTIN TYHJENNYS EPÄONNISTUI", "KARTE NICHT GELEERT",
                            "IMPOSSIBLE DE VIDER LA CARTE", "NO SE PUDO BORRAR LA TARJETA",
                            "IMPOSSIBILE SVUOTARE LA SCHEDA"));
    setLabelText(sdClearBody,
                 done ? txt("Every file was removed. Returning to the ride list...",
                            "Kaikki tiedostot poistettiin. Palataan ajolistaan...",
                            "Alle Dateien gelöscht. Zurück zur Fahrtenliste...",
                            "Tous les fichiers supprimés. Retour à la liste...",
                            "Se borraron todos los archivos. Volviendo a la lista...",
                            "Tutti i file sono stati eliminati. Ritorno all'elenco...")
                      : txt("Some files could not be removed. Check the card and try again.",
                            "Kaikkia tiedostoja ei voitu poistaa. Tarkista kortti ja yritä uudelleen.",
                            "Einige Dateien ließen sich nicht löschen. Karte prüfen und erneut versuchen.",
                            "Certains fichiers n'ont pas pu être supprimés. Vérifiez la carte et réessayez.",
                            "Algunos archivos no se pudieron borrar. Revisa la tarjeta e inténtalo de nuevo.",
                            "Alcuni file non sono stati eliminati. Controlla la scheda e riprova."));
    char count[48];
    snprintf(count, sizeof(count), "%lu / %lu %s", (unsigned long)wipe.removed, (unsigned long)wipe.total,
             txt("removed", "poistettu", "gelöscht", "supprimés", "borrados", "eliminati"));
    setLabelText(sdClearCount, count);
    if (!done) {
      // A failure is worth reading at leisure, so it waits for the rider.
      stopSdClearTimer();
      lv_obj_t *panel = lv_obj_get_parent(sdClearBar);
      makeMenuButton(panel, 78, 116, 124, 34, "OK", "", false, BTN_RIDE_LOGS_CLEAR_SD_DONE);
    }
    return;
  }
  if (wipe.state == RideLogWipeStatus::Done && now - sdClearFinishedMs >= kSdClearDoneHoldMs) finishSdClear();
}

static void sdClearTimerCb(lv_timer_t *) {
  // The screen was replaced under the dialog (a card pulled mid-wipe forces
  // the list to rebuild); there is nothing left to update.
  if (!developerPrompt || !sdClearActive) {
    stopSdClearTimer();
    forgetSdClearWidgets();
    sdClearActive = false;
    return;
  }
  refreshSdClearDialog(false);
}

static void showSdClearProgress() {
  if (developerPrompt) return;
  developerPrompt = lv_obj_create(lv_scr_act());
  lv_obj_add_event_cb(developerPrompt, developerPromptDeleteCb, LV_EVENT_DELETE, NULL);
  lv_obj_remove_style_all(developerPrompt);
  lv_obj_set_pos(developerPrompt, 0, 0);
  lv_obj_set_size(developerPrompt, cyd_ui::kScreenWidth, cyd_ui::kScreenHeight);
  lv_obj_set_style_bg_color(developerPrompt, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(developerPrompt, LV_OPA_70, 0);
  // Clickable so nothing behind it can be touched while the card is wiped.
  lv_obj_add_flag(developerPrompt, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(developerPrompt, LV_OBJ_FLAG_SCROLLABLE);

  // The same housing and geometry as the confirmation it replaces.
  lv_obj_t *panel = makePanel(developerPrompt, 20, 40, 280, 160, cyd_ui::kPanelRadius,
                              cyd_ui::chromeAccent(), cyd_ui::panelSurface(), true);
  sdClearHeading = makeLabelAt(panel, 140, 11,
                               txt("CLEARING THE SD CARD", "TYHJENNETÄÄN SD-KORTTIA", "SD-KARTE WIRD GELEERT",
                                   "VIDAGE DE LA CARTE SD", "BORRANDO LA TARJETA SD", "SVUOTAMENTO SCHEDA SD"),
                               lv_color_white(), &lv_font_rajdhani_12, 3);
  lv_obj_set_x(sdClearHeading, 10);
  lv_obj_set_width(sdClearHeading, 260);
  lv_label_set_long_mode(sdClearHeading, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(sdClearHeading, LV_TEXT_ALIGN_CENTER, 0);

  sdClearBody = makeLabelAt(panel, 140, 36,
                            txt("Keep the card in and the display powered.",
                                "Pidä kortti paikallaan ja näyttö päällä.",
                                "Karte nicht entfernen, Display eingeschaltet lassen.",
                                "Laissez la carte en place et l'écran allumé.",
                                "Mantén la tarjeta puesta y la pantalla encendida.",
                                "Lascia la scheda inserita e il display acceso."),
                            cyd_ui::secondaryText(), &lv_font_rajdhani_12, 3);
  lv_obj_set_pos(sdClearBody, 12, 34);
  lv_obj_set_size(sdClearBody, 256, 34);
  lv_label_set_long_mode(sdClearBody, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(sdClearBody, LV_TEXT_ALIGN_CENTER, 0);

  sdClearBar = lv_bar_create(panel);
  makePassive(sdClearBar);
  lv_obj_set_pos(sdClearBar, 20, 76);
  lv_obj_set_size(sdClearBar, 240, 10);
  lv_bar_set_range(sdClearBar, 0, 100);
  lv_obj_set_style_radius(sdClearBar, 4, LV_PART_MAIN);
  lv_obj_set_style_bg_color(sdClearBar, c565(COLOR565_DIM), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(sdClearBar, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(sdClearBar, 4, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(sdClearBar, cyd_ui::chromeAccent(), LV_PART_INDICATOR);
  lv_obj_set_style_anim_time(sdClearBar, 250, LV_PART_MAIN);

  sdClearCount = makeLabelAt(panel, 140, 94, "", lv_color_white(), &lv_font_rajdhani_12, 3);
  lv_obj_set_x(sdClearCount, 10);
  lv_obj_set_width(sdClearCount, 260);
  lv_obj_set_style_text_align(sdClearCount, LV_TEXT_ALIGN_CENTER, 0);

  sdClearActive = true;
  sdClearStartedMs = millis();
  sdClearFinishedMs = 0;
  stopSdClearTimer();
  sdClearTimer = lv_timer_create(sdClearTimerCb, 100, NULL);
  refreshSdClearDialog(false);
}

static void showPendingRecoveryPrompt() {
  if (!recoveryResetPromptPending || currentScreen != SCREEN_DASHBOARD) return;
  recoveryResetPromptPending = false;
  showConfirmationDialog(
      txt("RESET SETTINGS TOO?", "PALAUTETAANKO ASETUKSET?", "AUCH EINSTELLUNGEN ZURÜCKSETZEN?",
          "RÉINITIALISER AUSSI LES RÉGLAGES ?", "¿RESTABLECER TAMBIÉN LOS AJUSTES?",
          "AZZERARE ANCHE LE IMPOSTAZIONI?"),
      txt("Touch is calibrated. Settings, controller pairing and logging mode can also be cleared.",
          "Kosketus on kalibroitu. Myös asetukset, ohjainpariliitos ja lokitustila voidaan tyhjentää.",
          "Touch ist kalibriert. Einstellungen, Controller-Kopplung und Logging können auch gelöscht werden.",
          "Le tactile est calibré. Les réglages, l'appairage et la journalisation peuvent aussi être effacés.",
          "El táctil está calibrado. También pueden borrarse ajustes, emparejamiento y registro.",
          "Il touch è calibrato. Anche impostazioni, accoppiamento e log possono essere azzerati."),
      txt("RESET", "PALAUTA", "ZURÜCKSETZEN", "RÉINITIALISER", "RESTABLECER", "AZZERA"),
      BTN_RECOVERY_RESET_CONFIRM);
}

static void showDisableDeveloperPrompt() {
  if (developerPrompt || !developerOptionsEnabled) return;
  showConfirmationDialog(
      txt("DISABLE DEVELOPER OPTIONS?", "POISTA KEHITTÄJÄASETUKSET KÄYTÖSTÄ?", "ENTWICKLEROPTIONEN DEAKTIVIEREN?",
          "DÉSACTIVER LES OPTIONS DÉVELOPPEUR ?", "¿DESACTIVAR OPCIONES DESARROLLADOR?",
          "DISATTIVARE OPZIONI SVILUPPATORE?"),
      txt("The display will return to basic user mode. Developer Options can be enabled again by holding the "
          "Display button in Settings.",
          "Palaa tavalliseen käyttötilaan. Kehittäjäasetukset voi ottaa uudelleen käyttöön pitämällä Asetusten "
          "Näyttö-painiketta pohjassa.",
          "Zum normalen Benutzermodus zurückkehren. Entwickleroptionen lassen sich durch Gedrückthalten der "
          "Display-Taste erneut aktivieren.",
          "Retour au mode utilisateur standard. Maintenez le bouton Écran dans les réglages pour réactiver les "
          "options développeur.",
          "Volver al modo de usuario básico. Mantenga pulsado Pantalla en Ajustes para reactivar las opciones "
          "de desarrollador.",
          "Torna alla modalità utente base. Tieni premuto Display nelle Impostazioni per riattivare le opzioni "
          "sviluppatore."),
      txt("DISABLE", "POISTA", "DEAKTIVIEREN", "DÉSACTIVER", "DESACTIVAR", "DISATTIVA"),
      BTN_DEV_PROMPT_DISABLE);
}

static void showFirmwareDowngradePrompt(uint32_t versionCode) {
  char body[260];
  snprintf(
      body, sizeof(body),
      txt("Signed firmware version %lu is older than the installed version. Continue only if you intentionally "
          "want to downgrade this display.",
          "Allekirjoitettu versio %lu on asennettua versiota vanhempi. Jatka vain, jos haluat tarkoituksella "
          "palauttaa näytön vanhempaan versioon.",
          "Die signierte Firmware %lu ist älter als die installierte Version. Fahren Sie nur fort, wenn Sie "
          "bewusst ein Downgrade durchführen möchten.",
          "Le firmware signé %lu est plus ancien que la version installée. Continuez uniquement si vous "
          "souhaitez réellement revenir à cette version.",
          "El firmware firmado %lu es anterior a la versión instalada. Continúe solo si desea volver "
          "intencionadamente a esa versión.",
          "Il firmware firmato %lu è precedente alla versione installata. Continua solo se desideri "
          "eseguire intenzionalmente il downgrade."),
      static_cast<unsigned long>(versionCode));
  showConfirmationDialog(
      txt("INSTALL OLDER FIRMWARE?", "ASENNA VANHEMPI OHJELMISTO?", "ÄLTERE FIRMWARE INSTALLIEREN?",
          "INSTALLER UN ANCIEN FIRMWARE ?", "¿INSTALAR FIRMWARE ANTERIOR?",
          "INSTALLARE FIRMWARE PRECEDENTE?"),
      body, txt("CONTINUE", "JATKA", "FORTFAHREN", "CONTINUER", "CONTINUAR", "CONTINUA"),
      BTN_FIRMWARE_DOWNGRADE_CONFIRM);
}

static void stopDeveloperHoldTimer() {
  if (!developerHoldTimer) return;
  lv_timer_del(developerHoldTimer);
  developerHoldTimer = NULL;
}

static void developerHoldTimerCb(lv_timer_t *timer) {
  if (!developerHoldButton) {
    developerHoldTimer = NULL;
    lv_timer_del(timer);
    return;
  }
  const uint32_t elapsed = millis() - developerHoldStartedMs;
  if (elapsed >= 1000) {
    if (!developerHoldColorStarted) {
      developerHoldColorStarted = true;
      developerHoldStartColor = lv_obj_get_style_bg_color(developerHoldButton, LV_PART_MAIN);
    }
    const uint32_t progress = constrain(elapsed - 1000, 0UL, 1000UL);
    const lv_opa_t mix = static_cast<lv_opa_t>((progress * 255UL) / 1000UL);
    lv_obj_set_style_bg_color(developerHoldButton,
                              lv_color_mix(cyd_ui::chromeAccent(), developerHoldStartColor, mix),
                              LV_STATE_PRESSED);
  }
  if (elapsed < 2000) return;

  developerHoldTriggered = true;
  developerHoldTimer = NULL;
  lv_timer_del(timer);
  if (developerOptionsEnabled)
    showDeveloperStatusToast(true);
  else
    showDeveloperPrompt();
}

static void developerDisplayButtonEventCb(lv_event_t *e) {
  const lv_event_code_t code = lv_event_get_code(e);
  lv_obj_t *button = lv_event_get_target(e);
  if (code == LV_EVENT_DELETE) {
    stopDeveloperHoldTimer();
    developerHoldButton = NULL;
    return;
  }
  if (code == LV_EVENT_PRESSED) {
    developerHoldButton = button;
    developerHoldTriggered = false;
    developerHoldColorStarted = false;
    developerHoldStartedMs = millis();
    stopDeveloperHoldTimer();
    developerHoldTimer = lv_timer_create(developerHoldTimerCb, 40, NULL);
    return;
  }
  if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
    stopDeveloperHoldTimer();
    lv_obj_set_style_bg_color(button, cyd_ui::activeControlSurface(), LV_STATE_PRESSED);
    return;
  }
  if (code != LV_EVENT_CLICKED) return;
  if (developerHoldTriggered) {
    developerHoldTriggered = false;
    return;
  }
  menuAction(BTN_MENU_DISPLAY);
}

static lv_obj_t *makeMenuTopBar(lv_obj_t *scr, const char *title, uint8_t pageNumber, uint8_t pageCount,
                                bool showNext, bool reservePageLine = false) {
  lv_obj_t *back = makeNavButton(scr, kMenuEdgeX, 4,
                                 txt("< BACK", "< TAKAISIN", "< ZURÜCK", "< RETOUR", "< ATRÁS", "< INDIETRO"),
                                 BTN_BACK, kTopBarButtonH);
  lv_obj_set_width(back, kWideTopNavW);
  // pageCount == 0 is the non-paginated first-boot PIN step, where Next means
  // Continue. Real paginated menus stop at their last page and omit the
  // button entirely instead of wrapping back to page one.
  const bool nextAvailable = showNext && (pageCount == 0 || pageNumber < pageCount);
  lv_obj_t *nextButton = NULL;
  if (nextAvailable) {
    nextButton = makeNavButton(
        scr, kWideTopNavRightX, 4, txt("NEXT >", "SEUR >", "WEITER >", "SUIV >", "SIG >", "AVANTI >"),
        BTN_NEXT, kTopBarButtonH);
    lv_obj_set_width(nextButton, kWideTopNavW);
  }

  const int titleY = pageCount > 1 || reservePageLine ? 7 : 14;
  const lv_font_t *titleFont = title && strlen(title) > 16 ? &lv_font_rajdhani_12 : &lv_font_rajdhani_14;
  lv_obj_t *titleLabel = makeLabelAt(scr, 160, titleY, title, accentLv(), titleFont, 3);
  (void)titleLabel;
  if (pageCount > 1) {
    char pageText[8];
    snprintf(pageText, sizeof(pageText), "%u/%u", pageNumber, pageCount);
    makeLabelAt(scr, 160, 27, pageText, c565(COLOR565_LABEL), &lv_font_rajdhani_12, 3);
  }
  // Long translated headings can have a label box wider than their visible
  // glyphs. Keep actual controls above that passive text box.
  lv_obj_move_foreground(back);
  if (nextButton) lv_obj_move_foreground(nextButton);
  return back;
}

static void showMenu() {
  // Developer Options is only reachable while they are enabled; a route that
  // still points there after they were turned off lands on the second page.
  if (!developerOptionsEnabled && settingsMenuLevel == SETTINGS_MENU_DEVELOPER)
    settingsMenuLevel = SETTINGS_MENU_MORE;
  lv_obj_t *scr = makeScreen();
  currentAction = menuAction;
  const char *title = txt("SETTINGS", "ASETUKSET", "EINSTELL.", "RÉGLAGES", "AJUSTES", "IMPOSTAZ.");
  if (settingsMenuLevel == SETTINGS_MENU_CONTROLLER)
    title = txt("VEHICLE CONFIGURATION", "AJONEUVON MÄÄRITYS", "FAHRZEUGKONFIG.", "CONFIG. VÉHICULE",
                "CONFIG. VEHÍCULO", "CONFIG. VEICOLO");
  else if (settingsMenuLevel == SETTINGS_MENU_DEVELOPER)
    title = txt("DEVELOPER OPTIONS", "KEHITTÄJÄASETUKSET", "ENTWICKLEROPTIONEN", "OPTIONS DÉVELOPPEUR",
                "OPCIONES DESARROLLADOR", "OPZIONI SVILUPPATORE");
  if (settingsMenuLevel == SETTINGS_MENU_MORE) {
    // Vehicle Config is where every rider pairs and sets up the controller,
    // so it leads the page. Developer Options joins it only once enabled.
    lv_obj_t *controllerCard = makeMenuButton(
        scr, kMenuEdgeX, 48, kMenuContentW, 94,
        txt("VEHICLE CONFIGURATION", "AJONEUVON MÄÄRITYS", "FAHRZEUGKONFIGURATION",
            "CONFIGURATION DU VÉHICULE", "CONFIGURACIÓN DEL VEHÍCULO", "CONFIGURAZIONE VEICOLO"),
        txt("Connect your controller, set speed and power limits, and see vehicle and battery details",
            "Yhdistä ohjain, aseta nopeus- ja tehorajat ja katso ajoneuvon ja akun tiedot",
            "Controller verbinden, Tempo- und Leistungsgrenzen setzen, Fahrzeug- und Akkudaten ansehen",
            "Connecter le contrôleur, régler vitesse et puissance, voir véhicule et batterie",
            "Conectar el controlador, ajustar velocidad y potencia, ver vehículo y batería",
            "Collegare il controller, impostare velocità e potenza, vedere veicolo e batteria"),
        false, BTN_SETTINGS_CONTROLLER);
    lv_obj_t *developerCard =
        developerOptionsEnabled
            ? makeMenuButton(scr, kMenuEdgeX, 150, kMenuContentW, 80,
                             txt("DEVELOPER OPTIONS", "KEHITTÄJÄASETUKSET", "ENTWICKLEROPTIONEN",
                                 "OPTIONS DÉVELOPPEUR", "OPCIONES DE DESARROLLADOR", "OPZIONI SVILUPPATORE"),
                             txt("Display panel fixes, demo mode, auto return, changelog and diagnostics",
                                 "Paneelin korjaukset, demotila, automaattipaluu, muutosloki ja diagnostiikka",
                                 "Panel-Korrekturen, Demo-Modus, Auto-Rückkehr, Änderungen und Diagnose",
                                 "Réglages de la dalle, mode démo, retour auto, changements et diagnostic",
                                 "Ajustes del panel, modo demo, retorno auto, cambios y diagnóstico",
                                 "Correzioni pannello, modalità demo, ritorno auto, modifiche e diagnostica"),
                             false, BTN_SETTINGS_DEVELOPER)
            : NULL;

    // makeMenuButton lays a card out from the top, which leaves these tall
    // cards bottom-heavy with empty space. Centre each title/explanation pair
    // in its own card instead, and give the explanations the card width.
    struct PageCard {
      lv_obj_t *card;
      int titleY;
    };
    // Each explanation may run to two lines, so the pairs sit higher.
    const PageCard pageCards[] = {{controllerCard, 22}, {developerCard, 15}};
    for (const PageCard &entry : pageCards) {
      if (!entry.card) continue;
      lv_obj_t *cardTitle = lv_obj_get_child(entry.card, 0);
      lv_obj_t *cardValue = lv_obj_get_child(entry.card, 1);
      if (cardTitle) {
        lv_obj_set_style_text_font(cardTitle, &lv_font_rajdhani_14, 0);
        lv_obj_set_y(cardTitle, entry.titleY);
      }
      if (cardValue) {
        lv_obj_set_y(cardValue, entry.titleY + 22);
        lv_obj_set_width(cardValue, 284);
        lv_label_set_long_mode(cardValue, LV_LABEL_LONG_WRAP);
        lv_obj_set_height(cardValue, lv_font_get_line_height(&lv_font_rajdhani_12) * 2);
      }
    }
    // Build navigation last so translated back labels remain above every
    // full-width card on the small display.
    makeMenuTopBar(scr, title, 2, 2, false);
    loadScreen(scr);
    return;
  }

  char brightnessText[8];
  snprintf(brightnessText, sizeof(brightnessText), "%u%%", displayBrightnessPercent);

  if (settingsMenuLevel == SETTINGS_MENU_DISPLAY) {
    makeMenuButton(scr, kMenuEdgeX, 48, kTwoColLeftW, 66, txt("THEME", "TEEMA", "DESIGN", "THÈME", "TEMA", "TEMA"), themeName(),
                   false, BTN_MENU_DASH_UI);
    lv_obj_t *displayButton = makeMenuButton(
        scr, kTwoColRightX, 48, kTwoColRightW, 66, txt("DISPLAY", "NÄYTTÖ", "DISPLAY", "ÉCRAN", "PANTALLA", "SCHERMO"),
        brightnessText, false, BTN_MENU_DISPLAY);
    // A normal tap remains ordinary navigation. Only an uninterrupted
    // two-second hold invokes the hidden developer-options gesture.
    lv_obj_remove_event_cb(displayButton, buttonEventCb);
    lv_obj_add_event_cb(displayButton, developerDisplayButtonEventCb, LV_EVENT_ALL, NULL);
    makeMenuButton(scr, kMenuEdgeX, 118, kThreeColW, 54,
                   txt("LANGUAGE", "KIELI", "SPRACHE", "LANGUE", "IDIOMA", "LINGUA"), languageName(), false,
                   BTN_MENU_LANGUAGE);
    makeMenuButton(scr, kThreeColX1, 118, kThreeColW, 54,
                   txt("UNITS", "YKSIKÖT", "EINHEIT.", "UNITÉS", "UNIDADES", "UNITÀ"), unitName(), false,
                   BTN_MENU_UNITS);
    makeMenuButton(scr, kThreeColX2, 118, kThreeColW, 54, "PIN",
                   pinEnabled ? txt("Enabled", "Päällä", "Aktiv", "Actif", "Activo", "Attivo")
                              : txt("Disabled", "Pois", "Aus", "Désactivé", "Desactivado", "Disattivo"),
                   false, BTN_MENU_PIN);
    makeMenuButton(scr, kMenuEdgeX, 176, kThreeColW, 54, txt("LOGGING", "LOKI", "LOG", "JOURNAL", "REGISTRO", "LOG"),
                   rideLoggerStatus().recording
                       ? txt("Recording", "Tallentaa", "Aufnahme", "Enregistre", "Grabando", "Registrazione")
                       : txt("Ride data", "Ajotiedot", "Fahrdaten", "Données de trajet", "Datos de viaje",
                             "Dati di viaggio"),
                   false, BTN_MENU_LOGGING);
    makeMenuButton(scr, kThreeColX1, 176, kThreeColW, 54, txt("INFO", "TIEDOT", "INFO", "INFOS", "INFORMACIÓN", "INFO"),
                   txt("Status info", "Tilatiedot", "Statusinformationen", "Informations d'état",
                       "Información de estado", "Informazioni stato"),
                   false, BTN_MENU_INFO);
    makeMenuButton(scr, kThreeColX2, 176, kThreeColW, 54, txt("RESET", "NOLLAA", "RESET", "RESET", "REINICIAR", "RESET"),
                   txt("Defaults", "Oletukset", "Standard", "Par défaut", "Por defecto", "Predef."), false,
                   BTN_MENU_RESET);
  } else if (settingsMenuLevel == SETTINGS_MENU_DEVELOPER) {
    // Diagnostics exist only for BLE controllers; without them the remaining
    // four cards share the page in two taller rows.
    const bool showDiagnostics = controllerCapabilities().hasLinkDiagnostics;
    if (showDiagnostics)
      makeMenuButton(scr, kMenuEdgeX, 48, kMenuContentW, 54,
                     txt("DIAGNOSTICS", "DIAGNOSTIIKKA", "DIAGNOSE", "DIAGNOSTIC", "DIAGNÓSTICO", "DIAGNOSTICA"),
                     txt("Raw BLE packets", "BLE-raakapaketit", "BLE-Rohpakete", "Paquets BLE bruts",
                         "Paquetes BLE", "Pacchetti BLE"),
                     false, BTN_CONTROLLER_DIAGNOSTICS);
    const int firstRowY = showDiagnostics ? 107 : 48;
    const int firstRowH = showDiagnostics ? 57 : 88;
    const int secondRowY = showDiagnostics ? 169 : 142;
    const int secondRowH = showDiagnostics ? 61 : 88;
    makeMenuButton(scr, kMenuEdgeX, firstRowY, kTwoColLeftW, firstRowH,
                   txt("DEMO MODE", "DEMOTILA", "DEMO-MODUS", "MODE DÉMO", "MODO DEMO", "MODALITÀ DEMO"),
                   dashboardDemoModeEnabled
                       ? txt("Enabled", "Päällä", "Aktiv", "Actif", "Activo", "Attiva")
                       : txt("Disabled", "Pois", "Aus", "Désactivé", "Desactivado", "Disattiva"),
                   false, BTN_DEMO_MODE);
    makeMenuButton(scr, kTwoColRightX, firstRowY, kTwoColRightW, firstRowH,
                   txt("CHANGELOG", "MUUTOSLOKI", "ÄNDERUNGEN", "JOURNAL", "CAMBIOS", "MODIFICHE"),
                   txt("Recent firmware changes", "Viimeisimmät muutokset", "Letzte Änderungen",
                       "Derniers changements", "Cambios recientes", "Modifiche recenti"),
                   false, BTN_MENU_CHANGELOG);
    const char *autoReturnValue =
        autoReturnEnabled
            ? txt("Enabled", "Päällä", "Aktiv", "Actif", "Activo", "Attivo")
            : txt("Disabled", "Pois", "Aus", "Désactivé", "Desactivado", "Disattivo");
    makeMenuButton(scr, kMenuEdgeX, secondRowY, kTwoColLeftW, secondRowH,
                   txt("AUTO RETURN", "AUTOMAATTIPALUU", "AUTO-RÜCKKEHR", "RETOUR AUTO", "RETORNO AUTO", "RITORNO AUTO"),
                   autoReturnValue, false, BTN_AUTO_RETURN);
    makeMenuButton(scr, kTwoColRightX, secondRowY, kTwoColRightW, secondRowH,
                   txt("DEVELOPER MODE", "KEHITTÄJÄTILA", "ENTWICKLERMODUS", "MODE DÉVELOPPEUR",
                       "MODO DESARROLLADOR", "MODALITÀ SVILUPPO"),
                   txt("Return to user mode", "Palaa käyttäjätilaan", "Zurück zum Benutzermodus",
                       "Retour au mode utilisateur", "Volver al modo usuario", "Torna alla modalità utente"),
                   false, BTN_HIDE_DEVELOPER_OPTIONS);
  } else {
    const ControllerBackend *backend = activeControllerBackend();
    // The Connection card carries the live link state: it is the one thing on
    // this page that changes by itself, and the usual reason to open the page.
    char connectionValue[48];
    const char *pairPrompt = txt("Pair a device", "Parita laite", "Gerät koppeln", "Associer un appareil",
                                 "Vincular dispositivo", "Associa dispositivo");
    const char *bleConnected = txt("Bluetooth connected", "Bluetooth yhdistetty", "Bluetooth verbunden",
                                   "Bluetooth connecté", "Bluetooth conectado", "Bluetooth connesso");
    const char *blePaired = txt("Paired - not connected", "Paritettu - ei yhteyttä", "Gekoppelt - nicht verbunden",
                                "Associé - non connecté", "Vinculado - sin conexión", "Associato - non connesso");
    if (backend && backend->caps.showsDeviceList) {
      const ControllerLinkStatus status = backend->linkStatus ? backend->linkStatus() : ControllerLinkStatus{};
      snprintf(connectionValue, sizeof(connectionValue), "%s",
               status.state == CONTROLLER_LINK_CONNECTED ? bleConnected
                                                         : status.savedDevice ? blePaired : pairPrompt);
    } else {
      snprintf(connectionValue, sizeof(connectionValue), "%s",
               telemetryLinkState() == LINK_LIVE
                   ? txt("UART connected", "UART yhdistetty", "UART verbunden", "UART connecté", "UART conectado",
                         "UART connessa")
                   : txt("UART - no link", "UART - ei yhteyttä", "UART - keine Verbindung", "UART - pas de liaison",
                         "UART - sin enlace", "UART - nessun link"));
    }
    char configValue[40];
    snprintf(configValue, sizeof(configValue), "%s | %s", controllerTypeName(),
             controllerTransportLabel(backend ? backend->transport : CONTROLLER_TRANSPORT_UART, true));
    lv_obj_t *connectionCard = makeMenuButton(
        scr, kMenuEdgeX, 48, kTwoColLeftW, 82,
        txt("CONNECTION", "YHTEYS", "VERBINDUNG", "CONNEXION", "CONEXIÓN", "CONNESSIONE"), connectionValue, false,
        BTN_MENU_CONTROLLER_CONNECTION);
    lv_obj_t *configCard = makeMenuButton(
        scr, kTwoColRightX, 48, kTwoColRightW, 82,
        txt("CONFIGURATION", "OHJAINASETUKSET", "KONFIGURATION", "CONFIGURATION", "CONFIGURACIÓN",
            "CONFIGURAZIONE"),
        configValue, false, BTN_MENU_CONTROLLER_CONFIG);
    lv_obj_t *vehicleCard = makeMenuButton(
        scr, kMenuEdgeX, 135, kTwoColLeftW, 89,
        txt("VEHICLE INFO", "AJONEUVOTIEDOT", "FAHRZEUG-INFO", "INFO VÉHICULE", "INFO VEHÍCULO",
            "INFO VEICOLO"),
        txt("Identity and ratings", "Tunnisteet ja nimellisarvot", "Identität und Nenndaten",
            "Identité et caractéristiques", "Identidad y características", "Identità e specifiche"),
        false, BTN_MENU_VESC);
    lv_obj_t *batteryCard = makeMenuButton(
        scr, kTwoColRightX, 135, kTwoColRightW, 89,
        txt("BATTERY INFO", "AKKUTIEDOT", "AKKU-INFO", "INFO BATTERIE", "INFO BATERÍA", "INFO BATTERIA"),
        txt("Energy and health", "Energia ja kunto", "Energie und Zustand", "Énergie et santé", "Energía y salud",
            "Energia e salute"),
        false, BTN_MENU_BATTERY);
    for (lv_obj_t *card : {connectionCard, configCard, vehicleCard, batteryCard}) {
      lv_obj_t *cardTitle = lv_obj_get_child(card, 0);
      if (cardTitle) lv_obj_set_style_text_font(cardTitle, &lv_font_rajdhani_12, 0);
    }
  }
  if (settingsMenuLevel == SETTINGS_MENU_DISPLAY)
    makeMenuTopBar(scr, title, 1, 2, true);
  else
    makeMenuTopBar(scr, title, 1, 1, false);
  loadScreen(scr);
}

// ── FarDriver BLE developer diagnostic ──────────────────────────────────────

static const char *farDriverStateName(FarDriverBleState state) {
  switch (state) {
    case FARDRIVER_BLE_SCANNING:
      return txt("SCANNING", "HAETAAN", "SUCHE", "RECHERCHE", "BUSCANDO", "RICERCA");
    case FARDRIVER_BLE_CONNECTING:
      return txt("CONNECTING", "YHDISTETÄÄN", "VERBINDET", "CONNEXION", "CONECTANDO", "CONNESSIONE");
    case FARDRIVER_BLE_DISCOVERING:
      return txt("DISCOVERING", "TUTKITAAN", "ERKENNUNG", "DÉTECTION", "DETECTANDO", "RILEVAMENTO");
    case FARDRIVER_BLE_CONNECTED:
      return txt("CONNECTED", "YHDISTETTY", "VERBUNDEN", "CONNECTÉ", "CONECTADO", "CONNESSO");
    case FARDRIVER_BLE_ERROR:
      return txt("ERROR", "VIRHE", "FEHLER", "ERREUR", "ERROR", "ERRORE");
    case FARDRIVER_BLE_IDLE:
    default:
      return txt("READY", "VALMIS", "BEREIT", "PRÊT", "LISTO", "PRONTO");
  }
}

// Rider-facing sentence for a Bluetooth link's state.
//
// The backends also publish a free-text `message`, but it is written in English
// by the transport layer and cannot be translated there without dragging the
// localisation layer into it. The state enum is the part worth showing, so the
// UI maps that instead - the same split the VESC fault codes already use, where
// the transport hands over a number and telemetryFaultLabel() names it.
// The English detail keeps its place in the serial log and in the raw packet
// view, where a technical reading is what is wanted.
static const char *bleLinkStateDetail(uint8_t state, bool connecting, bool discovering, bool connected,
                                      bool error, bool savedDevice) {
  if (connected)
    return txt("Connected", "Yhdistetty", "Verbunden", "Connecté", "Conectado", "Connesso");
  if (discovering)
    return txt("Reading services...", "Luetaan palveluita...", "Dienste werden gelesen...",
               "Lecture des services...", "Leyendo servicios...", "Lettura servizi...");
  if (connecting)
    return txt("Connecting...", "Yhdistetään...", "Verbinde...", "Connexion...", "Conectando...",
               "Connessione...");
  if (error)
    return txt("Connection failed - try again", "Yhteys epäonnistui - yritä uudelleen",
               "Verbindung fehlgeschlagen - erneut versuchen", "Échec - réessayez",
               "Error de conexión - inténtelo de nuevo", "Connessione fallita - riprova");
  if (state == 1)  // scanning
    return txt("Searching for devices...", "Etsitään laitteita...", "Suche nach Geräten...",
               "Recherche d'appareils...", "Buscando dispositivos...", "Ricerca dispositivi...");
  if (savedDevice)
    return txt("Not connected", "Ei yhteyttä", "Nicht verbunden", "Non connecté", "Sin conexión",
               "Non connesso");
  return txt("Select a Bluetooth device", "Valitse Bluetooth-laite", "Bluetooth-Gerät wählen",
             "Choisir un appareil", "Elija un dispositivo", "Seleziona un dispositivo");
}

static const char *farDriverStateDetail(const FarDriverBleStatus &status) {
  return bleLinkStateDetail(status.state == FARDRIVER_BLE_SCANNING ? 1 : 0,
                            status.state == FARDRIVER_BLE_CONNECTING,
                            status.state == FARDRIVER_BLE_DISCOVERING,
                            status.state == FARDRIVER_BLE_CONNECTED,
                            status.state == FARDRIVER_BLE_ERROR, status.savedDevice);
}

// A failed connect says only "try again" everywhere else, which is fine next
// to a diagnostics screen but useless in the wizard, where the tap that failed
// is the only thing the rider did. The backends already classify the reason.
static const char *controllerLinkErrorDetail(ControllerLinkError error) {
  switch (error) {
    case CONTROLLER_LINK_ERR_SCAN_FAILED:
      return txt("Scan could not start", "Haku ei käynnisty", "Suche startet nicht",
                 "Recherche impossible", "No se pudo buscar", "Ricerca non avviata");
    case CONTROLLER_LINK_ERR_DEVICE_GONE:
      return txt("Device gone - scan again", "Laite kadonnut - hae uudelleen",
                 "Gerät weg - neu suchen", "Appareil perdu - rechercher",
                 "Dispositivo perdido - buscar", "Dispositivo perso - ricerca");
    case CONTROLLER_LINK_ERR_SERVICE_MISSING:
      return txt("No controller service", "Ei ohjainpalvelua", "Kein Controller-Dienst",
                 "Aucun service contrôleur", "Sin servicio de controlador", "Nessun servizio");
    case CONTROLLER_LINK_ERR_SUBSCRIBE_FAILED:
      return txt("Controller refused data", "Ohjain esti datan", "Controller lehnt Daten ab",
                 "Contrôleur refuse les données", "El controlador rechazó datos",
                 "Controller rifiuta i dati");
    case CONTROLLER_LINK_ERR_WORKER_FAILED:
      return txt("Bluetooth task failed", "Bluetooth-tehtävä kaatui", "Bluetooth-Task fehlt",
                 "Tâche Bluetooth échouée", "Fallo de tarea Bluetooth", "Attività Bluetooth fallita");
    case CONTROLLER_LINK_ERR_BUSY:
      return txt("Bluetooth busy - try again", "Bluetooth varattu - yritä taas",
                 "Bluetooth belegt - erneut", "Bluetooth occupé - réessayez",
                 "Bluetooth ocupado - reintente", "Bluetooth occupato - riprova");
    case CONTROLLER_LINK_ERR_CONNECT_FAILED:
    case CONTROLLER_LINK_ERR_NONE:
    default:
      return txt("Connection failed - try again", "Yhteys epäonnistui - yritä uudelleen",
                 "Verbindung fehlgeschlagen", "Échec - réessayez",
                 "Error de conexión - reintente", "Connessione fallita - riprova");
  }
}

static const char *controllerLinkStateDetail(const ControllerLinkStatus &status) {
  return bleLinkStateDetail(status.state == CONTROLLER_LINK_SCANNING ? 1 : 0,
                            status.state == CONTROLLER_LINK_CONNECTING,
                            status.state == CONTROLLER_LINK_DISCOVERING,
                            status.state == CONTROLLER_LINK_CONNECTED,
                            status.state == CONTROLLER_LINK_FAILED, status.savedDevice);
}

static void setFarDriverObjectVisible(lv_obj_t *object, bool visible) {
  if (!object) return;
  if (visible) lv_obj_clear_flag(object, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
}

static void setFarDriverButtonEnabled(lv_obj_t *button, bool enabled) {
  if (!button) return;
  if (enabled) lv_obj_clear_state(button, LV_STATE_DISABLED);
  else lv_obj_add_state(button, LV_STATE_DISABLED);
}

static void formatFarDriverUuid(char *output, size_t outputSize, const char *label, const char *uuid) {
  if (!uuid || !uuid[0]) {
    snprintf(output, outputSize, "%s  discovering...", label);
    return;
  }
  const size_t length = strlen(uuid);
  if (length > 20) {
    snprintf(output, outputSize, "%s  %.8s...%.8s", label, uuid, uuid + length - 8);
    return;
  }
  snprintf(output, outputSize, "%s  %s", label, uuid);
}

static void refreshFarDriverBleScreen(bool force) {
  if (currentScreen != SCREEN_FARDRIVER_BLE || !farDriverStatusLabel) return;
  const FarDriverBleStatus status = farDriverBleStatus();
  if (!force && status.revision == farDriverUiRevision) return;
  farDriverUiRevision = status.revision;

  char text[128];
  snprintf(text, sizeof(text), "FARDRIVER BLE  |  %s", farDriverStateName(status.state));
  setLabelText(farDriverStatusLabel, text);
  lv_obj_set_style_text_color(farDriverStatusLabel,
                              status.state == FARDRIVER_BLE_CONNECTED
                                  ? c565(COLOR565_GREEN)
                                  : status.state == FARDRIVER_BLE_ERROR ? c565(0xFBE0) : lv_color_white(),
                              0);

  const bool diagnostic = status.state == FARDRIVER_BLE_CONNECTING || status.state == FARDRIVER_BLE_DISCOVERING ||
                          status.state == FARDRIVER_BLE_CONNECTED;
  if (diagnostic && status.connectedAddress[0])
    snprintf(text, sizeof(text), "%s   RSSI %d dBm", status.connectedAddress, status.connectedRssi);
  else if (status.savedDevice)
    snprintf(text, sizeof(text), "%s  |  %s %s", farDriverStateDetail(status),
             txt("Paired", "Paritettu", "Gekoppelt", "Associé", "Vinculado", "Associato"),
             status.savedAddress);
  else
    snprintf(text, sizeof(text), "%s", farDriverStateDetail(status));
  setLabelText(farDriverDetailLabel, text);

  setFarDriverObjectVisible(farDriverPairHintLabel, !diagnostic && !status.savedDevice);

  setFarDriverObjectVisible(farDriverServiceLabel, diagnostic);
  setFarDriverObjectVisible(farDriverCharacteristicLabel, diagnostic);
  setFarDriverObjectVisible(farDriverPacketLabel, diagnostic);
  setFarDriverObjectVisible(farDriverCountersLabel, diagnostic);
  setFarDriverObjectVisible(farDriverDecodedLabel, diagnostic);
  if (diagnostic) {
    formatFarDriverUuid(text, sizeof(text), "SERVICE", status.serviceUuid);
    setLabelText(farDriverServiceLabel, text);
    formatFarDriverUuid(text, sizeof(text), "NOTIFY", status.characteristicUuid);
    setLabelText(farDriverCharacteristicLabel, text);
    snprintf(text, sizeof(text), "RAW %u B  %s", status.lastPacketLength,
             status.lastPacketHex[0] ? status.lastPacketHex : "Waiting for first packet...");
    setLabelText(farDriverPacketLabel, text);
    snprintf(text, sizeof(text), "FRAMES %lu   ID %02X   CRC FAIL %lu   SKIP %lu B",
             (unsigned long)status.frameCount, status.lastFrameId, (unsigned long)status.crcFailCount,
             (unsigned long)status.discardedBytes);
    setLabelText(farDriverCountersLabel, text);

    // km/h is taken from the live snapshot rather than recomputed here, so this
    // is the same number the dashboard shows and the motion auto-return acts on.
    const FarDriverTelemetry telemetry = farDriverTelemetry();
    DashboardValues live = {};
    const bool haveLive = getLiveDashboardValues(live);
    snprintf(text, sizeof(text), "%.1f V  %.1f A   RPM %d   GEAR %u   %s",
             telemetry.voltage, telemetry.current, telemetry.rawRpm, (unsigned)telemetry.gear,
             haveLive ? "" : "(no live sample)");
    if (haveLive) {
      const size_t used = strlen(text);
      snprintf(text + used, sizeof(text) - used, "%d km/h", live.speedKmh);
    }
    setLabelText(farDriverDecodedLabel, text);
  }

  setFarDriverButtonEnabled(farDriverDisconnectButton, diagnostic);
  setFarDriverButtonEnabled(farDriverForgetButton, status.savedDevice);
}

static void farDriverBleAction(int id) {
  if (id == BTN_BACK) {
    settingsMenuLevel = developerOptionsEnabled ? SETTINGS_MENU_DEVELOPER : SETTINGS_MENU_CONTROLLER;
    queueLowMemoryRebuild(SCREEN_MENU);
    return;
  }
  if (id == BTN_FARDRIVER_DISCONNECT) farDriverBleDisconnect();
  else if (id == BTN_FARDRIVER_FORGET) farDriverBleForget();
  refreshFarDriverBleScreen(true);
}

static void showFarDriverBleDiagnostic() {
  currentAction = farDriverBleAction;
  farDriverBleBegin();
  lv_obj_t *scr = makeScreen();
  makeMenuTopBar(scr, txt("LINK DIAGNOSTICS", "YHTEYSDIAGNOSTIIKKA", "LINK-DIAGNOSE", "DIAGNOSTIC LIAISON",
                          "DIAGNÓSTICO ENLACE", "DIAGNOSTICA LINK"),
                 1, 1, false);

  lv_obj_t *statusPanel = makeInfoPanel(scr, kMenuEdgeX, 48, kMenuContentW, 30, 6);
  farDriverStatusLabel = makeLabelAt(statusPanel, 152, 8, "", lv_color_white(), &lv_font_rajdhani_12, 3);
  farDriverDetailLabel = makeLabelAt(scr, 160, 82, "", c565(COLOR565_LABEL), &lv_font_rajdhani_12, 3);
  lv_obj_set_width(farDriverDetailLabel, 304);
  lv_obj_set_style_text_align(farDriverDetailLabel, LV_TEXT_ALIGN_CENTER, 0);
  farDriverPairHintLabel = makeLabelAt(
      scr, 160, 120,
      txt("No device paired. Pair one under Vehicle Configuration > Connection.",
          "Ei paritettua laitetta. Parita se kohdassa Ohjainasetukset > Yhteys.",
          "Kein Gerät gekoppelt. Koppeln unter Controller-Einstellungen > Verbindung.",
          "Aucun appareil associé. Associez-en un dans Réglages contrôleur > Connexion.",
          "Ningún dispositivo vinculado. Vincúlelo en Ajustes del controlador > Conexión.",
          "Nessun dispositivo associato. Associalo in Impostazioni controller > Connessione."),
      c565(COLOR565_LABEL), &lv_font_rajdhani_12, 3);
  lv_obj_set_x(farDriverPairHintLabel, 20);
  lv_obj_set_width(farDriverPairHintLabel, 280);
  lv_obj_set_style_text_align(farDriverPairHintLabel, LV_TEXT_ALIGN_CENTER, 0);

  farDriverServiceLabel = makeLabelAt(scr, 8, 100, "", lv_color_white(), &lv_font_rajdhani_12, 0);
  farDriverCharacteristicLabel = makeLabelAt(scr, 8, 117, "", lv_color_white(), &lv_font_rajdhani_12, 0);
  farDriverPacketLabel = makeLabelAt(scr, 8, 136, "", c565(COLOR565_LABEL), &lv_font_rajdhani_12, 0);
  lv_obj_set_size(farDriverPacketLabel, 304, 32);
  lv_label_set_long_mode(farDriverPacketLabel, LV_LABEL_LONG_WRAP);
  farDriverCountersLabel = makeLabelAt(scr, 8, 170, "", lv_color_white(), &lv_font_rajdhani_12, 0);
  // What the bytes above actually decoded to. Raw hex says the link works;
  // this says whether the numbers coming out of it are believable, which is
  // the question a bring-up on unknown hardware keeps running into.
  farDriverDecodedLabel = makeLabelAt(scr, 8, 187, "", c565(COLOR565_LABEL), &lv_font_rajdhani_12, 0);

  farDriverDisconnectButton = makeMenuButton(
      scr, kMenuEdgeX, 204, kTwoColLeftW, 34,
      txt("DISCONNECT", "KATKAISE", "TRENNEN", "DÉCONNECTER", "DESCONECTAR", "DISCONNETTI"), "", false,
      BTN_FARDRIVER_DISCONNECT);
  farDriverForgetButton = makeMenuButton(
      scr, kTwoColRightX, 204, kTwoColRightW, 34, txt("FORGET", "UNOHDA", "VERGESSEN", "OUBLIER", "OLVIDAR", "DIMENTICA"), "", false,
      BTN_FARDRIVER_FORGET);
  const int actionWidths[] = {150, 150};
  lv_obj_t *actionButtons[] = {farDriverDisconnectButton, farDriverForgetButton};
  for (uint8_t i = 0; i < 2; i++) {
    lv_obj_t *button = actionButtons[i];
    lv_obj_t *label = lv_obj_get_child(button, 0);
    if (label) {
      lv_obj_set_width(label, actionWidths[i] - 20);
      lv_obj_set_x(label, 4);
      lv_obj_set_y(label, 11);
      lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    }
  }

  farDriverUiRevision = 0;
  refreshFarDriverBleScreen(true);
  loadScreen(scr);
}

// ── Phone companion session ─────────────────────────────────────────────────

static const char *companionStateName(CompanionBleState state) {
  switch (state) {
    case COMPANION_BLE_PREPARING:
      return txt("PREPARING", "VALMISTELLAAN", "VORBEREITUNG", "PRÉPARATION", "PREPARANDO", "PREPARAZIONE");
    case COMPANION_BLE_ADVERTISING:
      return txt("READY FOR DEVICE", "VALMIS LAITTEELLE", "BEREIT FÜR GERÄT", "PRÊT POUR APPAREIL",
                 "LISTO PARA DISPOSITIVO", "PRONTO PER DISPOSITIVO");
    case COMPANION_BLE_CONNECTED:
      return txt("DEVICE CONNECTED", "LAITE YHDISTETTY", "GERÄT VERBUNDEN", "APPAREIL CONNECTÉ",
                 "DISPOSITIVO CONECTADO", "DISPOSITIVO CONNESSO");
    case COMPANION_BLE_ERROR:
      return txt("CONNECTION ERROR", "YHTEYSVIRHE", "VERBINDUNGSFEHLER", "ERREUR DE CONNEXION",
                 "ERROR DE CONEXIÓN", "ERRORE CONNESSIONE");
    case COMPANION_BLE_OFF:
    default:
      return txt("BLUETOOTH LINK OFF", "BLUETOOTH LINK POIS", "BLUETOOTH LINK AUS", "BLUETOOTH LINK ARRÊTÉ",
                 "BLUETOOTH LINK APAGADO", "BLUETOOTH LINK SPENTO");
  }
}

static void refreshCompanionScreen(bool force) {
  if (currentScreen != SCREEN_COMPANION_MODE || !companionStateLabel) return;
  if (firmwareUpdateBleActive()) {
    firmwareUpdateUserMode = true;
    queueLowMemoryRebuild(SCREEN_FIRMWARE_UPDATE);
    return;
  }
  const CompanionBleStatus status = companionBleStatus();
  if (!force && status.revision == companionUiRevision && status.state != COMPANION_BLE_ADVERTISING &&
      status.state != COMPANION_BLE_CONNECTED) return;
  companionUiRevision = status.revision;
  setLabelText(companionStateLabel, companionStateName(status.state));
  lv_obj_set_style_text_color(companionStateLabel,
                              status.state == COMPANION_BLE_CONNECTED ? c565(COLOR565_GREEN) : lv_color_white(), 0);

  const char *detail = status.message;
  if (status.state == COMPANION_BLE_OFF)
    detail = txt("Open this page to make Bluetooth Link available.", "Avaa tämä sivu käyttääksesi Bluetooth Linkiä.",
                 "Diese Seite öffnet Bluetooth Link.", "Ouvrez cette page pour Bluetooth Link.",
                 "Abra esta página para usar Bluetooth Link.", "Apri questa pagina per Bluetooth Link.");
  else if (status.state == COMPANION_BLE_ADVERTISING)
    detail = txt("Open KAJO Companion and connect to Bluetooth Link.", "Avaa KAJO Companion ja yhdistä Bluetooth Linkiin.",
                 "KAJO Companion öffnen und mit Bluetooth Link verbinden.", "Ouvrez KAJO Companion et connectez Bluetooth Link.",
                 "Abra KAJO Companion y conecte Bluetooth Link.", "Apri KAJO Companion e collega Bluetooth Link.");
  setLabelText(companionDetailLabel, detail);

  char timer[72];
  if (status.state == COMPANION_BLE_ADVERTISING)
    snprintf(timer, sizeof(timer), "%s  %lu s",
             txt("CONNECTION WINDOW", "YHTEYSIKKUNA", "VERBINDUNGSFENSTER", "FENÊTRE DE CONNEXION",
                 "VENTANA DE CONEXIÓN", "FINESTRA CONNESSIONE"),
             (unsigned long)status.secondsRemaining);
  else if (status.state == COMPANION_BLE_CONNECTED)
    snprintf(timer, sizeof(timer), "%s  |  %lu s",
             status.controllerPaused
                 ? txt("CONTROLLER BLE PAUSED", "OHJAIMEN BLE TAUOLLA", "CONTROLLER-BLE PAUSIERT",
                       "BLE CONTRÔLEUR EN PAUSE", "BLE CONTROLADOR EN PAUSA", "BLE CONTROLLER IN PAUSA")
                 : txt("CONTROLLER ACTIVE", "OHJAIN AKTIIVINEN", "CONTROLLER AKTIV", "CONTRÔLEUR ACTIF",
                       "CONTROLADOR ACTIVO", "CONTROLLER ATTIVO"),
             (unsigned long)status.secondsRemaining);
  else
    snprintf(timer, sizeof(timer), "%s",
             txt("Controller telemetry resumes automatically.", "Ohjaimen telemetria jatkuu automaattisesti.",
                 "Controller-Telemetrie startet automatisch neu.", "La télémétrie reprend automatiquement.",
                 "La telemetría se reanuda automáticamente.", "La telemetria riprende automaticamente."));
  setLabelText(companionTimerLabel, timer);

  setLabelText(companionActionLabel,
               status.state == COMPANION_BLE_OFF || status.state == COMPANION_BLE_ERROR
                   ? txt("RESTART BLUETOOTH LINK", "KÄYNNISTÄ BLUETOOTH LINK", "BLUETOOTH LINK NEU STARTEN",
                         "REDÉMARRER BLUETOOTH LINK", "REINICIAR BLUETOOTH LINK", "RIAVVIA BLUETOOTH LINK")
                   : txt("END BLUETOOTH LINK", "LOPETA BLUETOOTH LINK", "BLUETOOTH LINK BEENDEN",
                         "TERMINER BLUETOOTH LINK", "FINALIZAR BLUETOOTH LINK", "TERMINA BLUETOOTH LINK"));
}

static void companionAction(int id) {
  if (id == BTN_BACK) {
    companionBleStop();
    submenuType = SUBMENU_DISPLAY_INFO;
    queueLowMemoryRebuild(SCREEN_SUBMENU);
    return;
  }
  if (id != BTN_COMPANION_ACTION) return;
  const CompanionBleStatus status = companionBleStatus();
  if (status.state == COMPANION_BLE_OFF || status.state == COMPANION_BLE_ERROR)
    companionBleStart();
  else
    companionBleStop();
  refreshCompanionScreen(true);
}

static void showCompanionMode() {
  currentAction = companionAction;
  const CompanionBleStatus initialStatus = companionBleStatus();
  if (initialStatus.state == COMPANION_BLE_OFF) companionBleStart();
  lv_obj_t *scr = makeScreen();
  makeMenuTopBar(scr,
                 txt("BLUETOOTH LINK", "BLUETOOTH LINK", "BLUETOOTH LINK", "BLUETOOTH LINK", "BLUETOOTH LINK", "BLUETOOTH LINK"),
                 1, 1, false);
  lv_obj_t *statusPanel = makeInfoPanel(scr, 18, 50, 284, 42, 7);
  companionStateLabel = makeLabelAt(statusPanel, 142, 13, "", lv_color_white(), &lv_font_rajdhani_14, 3);
  companionDetailLabel = makeLabelAt(scr, 8, 103, "", c565(COLOR565_LABEL), &lv_font_rajdhani_12, 0);
  lv_obj_set_width(companionDetailLabel, 304);
  lv_obj_set_style_text_align(companionDetailLabel, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_t *sessionPanel = makeInfoPanel(scr, 18, 132, 284, 43, 6);
  companionTimerLabel = makeLabelAt(sessionPanel, 7, 14, "", lv_color_white(), &lv_font_rajdhani_12, 0);
  lv_obj_set_width(companionTimerLabel, 270);
  lv_obj_set_style_text_align(companionTimerLabel, LV_TEXT_ALIGN_CENTER, 0);
  companionActionButton = makeMenuButton(scr, 28, 190, 264, 44, "", "", false, BTN_COMPANION_ACTION);
  companionActionLabel = lv_obj_get_child(companionActionButton, 0);
  companionUiRevision = 0;
  loadScreen(scr);
  refreshCompanionScreen(true);
}

// ── Signed Bluetooth firmware update ─────────────────────────────────────────

static const char *firmwareUpdateStateName(FirmwareUpdateBleState state) {
  switch (state) {
    case FIRMWARE_UPDATE_BLE_READY:
      return txt("READY", "VALMIS", "BEREIT", "PRÊT", "LISTO", "PRONTO");
    case FIRMWARE_UPDATE_BLE_PREPARING:
      return txt("PREPARING", "VALMISTELLAAN", "VORBEREITUNG", "PRÉPARATION", "PREPARANDO", "PREPARAZIONE");
    case FIRMWARE_UPDATE_BLE_ADVERTISING:
      return txt("WAITING FOR UPLOADER", "ODOTTAA LÄHETINTÄ", "WARTET AUF UPLOADER", "ATTENTE ACTUALISATEUR",
                 "ESPERANDO ACTUALIZADOR", "ATTESA UPLOADER");
    case FIRMWARE_UPDATE_BLE_CONNECTED:
      return txt("UPLOADER CONNECTED", "LÄHETIN YHDISTETTY", "UPLOADER VERBUNDEN", "OUTIL CONNECTÉ",
                 "ACTUALIZADOR CONECTADO", "UPLOADER CONNESSO");
    case FIRMWARE_UPDATE_BLE_CONFIRM_DOWNGRADE:
      return txt("CONFIRM DOWNGRADE", "VAHVISTA PALAUTUS", "DOWNGRADE BESTÄTIGEN", "CONFIRMER LE RETOUR",
                 "CONFIRMAR VERSIÓN", "CONFERMA DOWNGRADE");
    case FIRMWARE_UPDATE_BLE_RECEIVING:
      return txt("RECEIVING", "VASTAANOTETAAN", "EMPFANG", "RÉCEPTION", "RECIBIENDO", "RICEZIONE");
    case FIRMWARE_UPDATE_BLE_READY_TO_REBOOT:
      return txt("UPDATE SUCCESSFUL", "PÄIVITYS ONNISTUI", "UPDATE ERFOLGREICH", "MISE À JOUR RÉUSSIE",
                 "ACTUALIZACIÓN CORRECTA", "AGGIORNAMENTO RIUSCITO");
    case FIRMWARE_UPDATE_BLE_CANCELLED:
      return txt("UPDATE CANCELLED", "PÄIVITYS PERUUTETTU", "UPDATE ABGEBROCHEN", "MISE À JOUR ANNULÉE",
                 "ACTUALIZACIÓN CANCELADA", "AGGIORNAMENTO ANNULLATO");
    case FIRMWARE_UPDATE_BLE_ERROR:
      return txt("UPDATE ERROR", "PÄIVITYSVIRHE", "UPDATE-FEHLER", "ERREUR MISE À JOUR",
                 "ERROR DE ACTUALIZACIÓN", "ERRORE AGGIORNAMENTO");
    case FIRMWARE_UPDATE_BLE_LOCKED:
    default:
      return txt("LOCKED", "LUKITTU", "GESPERRT", "VERROUILLÉ", "BLOQUEADO", "BLOCCATO");
  }
}

static const char *firmwareUpdateUserStateName(FirmwareUpdateBleState state) {
  switch (state) {
    case FIRMWARE_UPDATE_BLE_PREPARING:
      return txt("STARTING BLUETOOTH", "KÄYNNISTETÄÄN BLUETOOTH", "BLUETOOTH STARTET", "DÉMARRAGE BLUETOOTH",
                 "INICIANDO BLUETOOTH", "AVVIO BLUETOOTH");
    case FIRMWARE_UPDATE_BLE_ADVERTISING:
      return txt("READY TO RECEIVE UPDATE", "VALMIS VASTAANOTTAMAAN", "BEREIT FÜR DAS UPDATE",
                 "PRÊT À RECEVOIR", "LISTO PARA RECIBIR", "PRONTO A RICEVERE");
    case FIRMWARE_UPDATE_BLE_CONNECTED:
      return txt("UPDATER CONNECTED", "PÄIVITTÄJÄ YHDISTETTY", "UPDATER VERBUNDEN", "OUTIL CONNECTÉ",
                 "ACTUALIZADOR CONECTADO", "UPDATER CONNESSO");
    default:
      return firmwareUpdateStateName(state);
  }
}

static const char *firmwareUpdateUserDetail(FirmwareUpdateBleState state) {
  switch (state) {
    case FIRMWARE_UPDATE_BLE_READY:
    case FIRMWARE_UPDATE_BLE_PREPARING:
      return txt("Preparing the display for an update...", "Näyttö valmistellaan päivitystä varten...",
                 "Display wird für das Update vorbereitet...", "Préparation de l'écran...",
                 "Preparando la pantalla...", "Preparazione del display...");
    case FIRMWARE_UPDATE_BLE_ADVERTISING:
      return txt("Run the updater on your phone or PC.", "Käynnistä päivittäjä puhelimella tai PC:llä.",
                 "Updater auf Handy oder PC starten.", "Lancez l'outil sur téléphone ou PC.",
                 "Ejecute el actualizador en el teléfono o PC.", "Avvia l'updater su telefono o PC.");
    case FIRMWARE_UPDATE_BLE_CONNECTED:
      return txt("Your updater found the display.", "Päivittäjä löysi näytön.", "Der Updater hat das Display gefunden.",
                 "L'outil a trouvé l'écran.", "El actualizador encontró la pantalla.",
                 "L'updater ha trovato il display.");
    case FIRMWARE_UPDATE_BLE_RECEIVING:
      return txt("Receiving and checking the update.", "Päivitystä vastaanotetaan ja tarkistetaan.",
                 "Update wird empfangen und geprüft.", "Réception et vérification de la mise à jour.",
                 "Recibiendo y verificando la actualización.", "Ricezione e verifica dell'aggiornamento.");
    case FIRMWARE_UPDATE_BLE_READY_TO_REBOOT:
      return txt("Restarting automatically in 5 seconds...", "Käynnistyy uudelleen 5 sekunnin kuluttua...",
                 "Automatischer Neustart in 5 Sekunden...", "Redémarrage automatique dans 5 secondes...",
                 "Reinicio automático en 5 segundos...", "Riavvio automatico tra 5 secondi...");
    case FIRMWARE_UPDATE_BLE_CANCELLED:
      return txt("Update cancelled. Restarting...", "Päivitys peruutettu. Käynnistetään uudelleen...",
                 "Update abgebrochen. Neustart...", "Mise à jour annulée. Redémarrage...",
                 "Actualización cancelada. Reiniciando...", "Aggiornamento annullato. Riavvio...");
    case FIRMWARE_UPDATE_BLE_LOCKED:
      return txt("Display updates are not configured.", "Näytön päivityksiä ei ole määritetty.",
                 "Display-Updates sind nicht eingerichtet.", "Les mises à jour ne sont pas configurées.",
                 "Las actualizaciones no están configuradas.", "Gli aggiornamenti non sono configurati.");
    case FIRMWARE_UPDATE_BLE_ERROR:
      return txt("The update could not continue.", "Päivitystä ei voitu jatkaa.", "Das Update konnte nicht fortgesetzt werden.",
                 "La mise à jour n'a pas pu continuer.", "La actualización no pudo continuar.",
                 "Impossibile continuare l'aggiornamento.");
    default:
      return "";
  }
}

static void refreshFirmwareUpdateScreen(bool force) {
  if (currentScreen != SCREEN_FIRMWARE_UPDATE || !firmwareUpdateStateLabel) return;
  const FirmwareUpdateBleStatus status = firmwareUpdateBleStatus();
  if (!force && status.revision == firmwareUpdateUiRevision) return;
  firmwareUpdateUiRevision = status.revision;

  setLabelText(firmwareUpdateStateLabel,
               firmwareUpdateUserMode ? firmwareUpdateUserStateName(status.state) : firmwareUpdateStateName(status.state));
  lv_obj_set_style_text_color(
      firmwareUpdateStateLabel,
      status.state == FIRMWARE_UPDATE_BLE_READY_TO_REBOOT
          ? c565(COLOR565_GREEN)
          : status.state == FIRMWARE_UPDATE_BLE_CANCELLED
                ? cyd_ui::chromeAccent()
          : status.state == FIRMWARE_UPDATE_BLE_ERROR || status.state == FIRMWARE_UPDATE_BLE_LOCKED
                ? c565(0xFBE0)
                : lv_color_white(),
      0);
  setLabelText(
      firmwareUpdateDetailLabel,
      firmwareUpdateUserMode && status.state != FIRMWARE_UPDATE_BLE_CONFIRM_DOWNGRADE
          ? firmwareUpdateUserDetail(status.state)
          : status.state == FIRMWARE_UPDATE_BLE_CONFIRM_DOWNGRADE
          ? txt("Signed older firmware requires approval.", "Vanhempi allekirjoitettu ohjelmisto vaatii hyväksynnän.",
                "Ältere signierte Firmware erfordert eine Bestätigung.",
                "L'ancien firmware signé nécessite une confirmation.",
                "El firmware anterior firmado requiere aprobación.",
                "Il firmware firmato precedente richiede conferma.")
          : status.message);

  const int progress = status.totalBytes ? static_cast<int>((uint64_t)status.expectedOffset * 100 / status.totalBytes) : 0;
  lv_bar_set_value(firmwareUpdateProgress, constrain(progress, 0, 100), LV_ANIM_OFF);
  char progressText[72];
  if (status.totalBytes && status.state == FIRMWARE_UPDATE_BLE_RECEIVING &&
      status.transferBytesPerSecond > 0)
    snprintf(progressText, sizeof(progressText), "%.1f / %.1f KiB  |  %d%%  |  %.1f KiB/s",
             status.expectedOffset / 1024.0, status.totalBytes / 1024.0, progress,
             status.transferBytesPerSecond / 1024.0);
  else if (status.totalBytes)
    snprintf(progressText, sizeof(progressText), "%.1f / %.1f KiB  |  %d%%",
             status.expectedOffset / 1024.0, status.totalBytes / 1024.0, progress);
  else if (firmwareUpdateUserMode && status.state == FIRMWARE_UPDATE_BLE_ADVERTISING)
    snprintf(progressText, sizeof(progressText), "%s", "KAJO-Dash Firmware Update");
  else
    snprintf(progressText, sizeof(progressText), "%s", status.keyConfigured ? "SHA-256 + ECDSA P-256" : "NO RELEASE KEY");
  setLabelText(firmwareUpdateProgressLabel, progressText);
  if (!firmwareUpdateUserMode && status.state == FIRMWARE_UPDATE_BLE_RECEIVING && status.imageBytes > 0 &&
      status.lastError == 0) {
    char detailText[64];
    snprintf(detailText, sizeof(detailText), "Compressed transfer | %.1f KiB firmware",
             status.imageBytes / 1024.0);
    setLabelText(firmwareUpdateDetailLabel, detailText);
  }

  const char *action = "";
  bool enabled = true;
  if (status.state == FIRMWARE_UPDATE_BLE_LOCKED) {
    action = txt("UPDATE LOCKED", "PÄIVITYS LUKITTU", "UPDATE GESPERRT", "MISE À JOUR VERROUILLÉE",
                 "ACTUALIZACIÓN BLOQUEADA", "AGGIORNAMENTO BLOCCATO");
    enabled = false;
  } else if (status.state == FIRMWARE_UPDATE_BLE_READY) {
    action = txt("START UPDATE MODE", "KÄYNNISTÄ PÄIVITYSTILA", "UPDATE-MODUS STARTEN", "DÉMARRER LE MODE",
                 "INICIAR MODO ACTUALIZACIÓN", "AVVIA MODALITÀ UPDATE");
  } else if (status.state == FIRMWARE_UPDATE_BLE_READY_TO_REBOOT) {
    action = txt("RESTART NOW", "KÄYNNISTÄ NYT", "JETZT NEUSTARTEN", "REDÉMARRER MAINTENANT",
                 "REINICIAR AHORA", "RIAVVIA ORA");
  } else if (status.state == FIRMWARE_UPDATE_BLE_CANCELLED) {
    action = txt("RESTARTING...", "KÄYNNISTETÄÄN...", "NEUSTART...", "REDÉMARRAGE...",
                 "REINICIANDO...", "RIAVVIO...");
    enabled = false;
  } else {
    action = txt("CANCEL & RESTART", "PERUUTA JA KÄYNNISTÄ", "ABBRUCH & NEUSTART", "ANNULER & REDÉMARRER",
                 "CANCELAR Y REINICIAR", "ANNULLA E RIAVVIA");
  }
  setLabelText(firmwareUpdateActionLabel, action);
  if (enabled)
    lv_obj_clear_state(firmwareUpdateActionButton, LV_STATE_DISABLED);
  else
    lv_obj_add_state(firmwareUpdateActionButton, LV_STATE_DISABLED);

  if (status.state == FIRMWARE_UPDATE_BLE_CONFIRM_DOWNGRADE && !developerPrompt)
    showFirmwareDowngradePrompt(status.versionCode);
}

static void firmwareUpdateAction(int id) {
  const FirmwareUpdateBleStatus status = firmwareUpdateBleStatus();
  if (id == BTN_DEV_PROMPT_CANCEL && status.state == FIRMWARE_UPDATE_BLE_CONFIRM_DOWNGRADE) {
    closeDeveloperPrompt();
    if (!firmwareUpdateBleCancelFromDisplay()) {
      firmwareUpdateBleStop();
      requestControllerRestart();
    }
    return;
  }
  if (id == BTN_FIRMWARE_DOWNGRADE_CONFIRM && status.state == FIRMWARE_UPDATE_BLE_CONFIRM_DOWNGRADE) {
    closeDeveloperPrompt();
    firmwareUpdateBleConfirmDowngrade();
    return;
  }
  if (id == BTN_BACK) {
    if (status.state == FIRMWARE_UPDATE_BLE_LOCKED || status.state == FIRMWARE_UPDATE_BLE_READY) {
      if (firmwareUpdateUserMode) {
        submenuType = SUBMENU_DISPLAY_INFO;
        queueLowMemoryRebuild(SCREEN_SUBMENU);
      } else {
        settingsMenuLevel = SETTINGS_MENU_DEVELOPER;
        queueLowMemoryRebuild(SCREEN_MENU);
      }
    } else {
      if (!firmwareUpdateBleCancelFromDisplay()) {
        firmwareUpdateBleStop();
        requestControllerRestart();
      }
    }
    return;
  }
  if (id != BTN_FIRMWARE_UPDATE_ACTION) return;
  if (status.state == FIRMWARE_UPDATE_BLE_READY) {
    firmwareUpdateBleStart();
    refreshFirmwareUpdateScreen(true);
  } else if (status.state == FIRMWARE_UPDATE_BLE_READY_TO_REBOOT) {
    firmwareUpdateBleRequestReboot();
  } else if (status.state != FIRMWARE_UPDATE_BLE_LOCKED) {
    if (status.state != FIRMWARE_UPDATE_BLE_CANCELLED && !firmwareUpdateBleCancelFromDisplay()) {
      firmwareUpdateBleStop();
      requestControllerRestart();
    }
  }
}

static void showUserFirmwareUpdate() {
  currentAction = firmwareUpdateAction;
  lv_obj_t *scr = makeScreen();
  makeMenuTopBar(scr,
                 txt("UPDATE DISPLAY", "PÄIVITÄ NÄYTTÖ", "DISPLAY AKTUALISIEREN", "METTRE À JOUR L'ÉCRAN",
                     "ACTUALIZAR PANTALLA", "AGGIORNA DISPLAY"),
                 1, 1, false);

  lv_obj_t *statusPanel = makeInfoPanel(scr, 18, 49, 284, 40, 7);
  firmwareUpdateStateLabel = makeLabelAt(statusPanel, 142, 12, "", lv_color_white(), &lv_font_rajdhani_14, 3);

  firmwareUpdateDetailLabel = makeLabelAt(scr, 160, 99, "", c565(COLOR565_LABEL), &lv_font_rajdhani_12, 3);
  lv_obj_set_x(firmwareUpdateDetailLabel, 8);
  lv_obj_set_width(firmwareUpdateDetailLabel, 304);
  lv_obj_set_style_text_align(firmwareUpdateDetailLabel, LV_TEXT_ALIGN_CENTER, 0);

  firmwareUpdateProgress = lv_bar_create(scr);
  makePassive(firmwareUpdateProgress);
  lv_obj_set_pos(firmwareUpdateProgress, 28, 123);
  lv_obj_set_size(firmwareUpdateProgress, 264, 11);
  lv_bar_set_range(firmwareUpdateProgress, 0, 100);
  lv_obj_set_style_radius(firmwareUpdateProgress, 4, LV_PART_MAIN);
  lv_obj_set_style_bg_color(firmwareUpdateProgress, cyd_ui::idleControlBorder(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(firmwareUpdateProgress, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(firmwareUpdateProgress, 4, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(firmwareUpdateProgress, cyd_ui::chromeAccent(), LV_PART_INDICATOR);
  firmwareUpdateProgressLabel = makeLabelAt(scr, 160, 140, "", lv_color_white(), &lv_font_rajdhani_12, 3);
  lv_obj_set_x(firmwareUpdateProgressLabel, 5);
  lv_obj_set_width(firmwareUpdateProgressLabel, 310);
  lv_obj_set_style_text_align(firmwareUpdateProgressLabel, LV_TEXT_ALIGN_CENTER, 0);

  lv_obj_t *notice = makeInfoPanel(scr, 18, 159, 284, 34, 6);
  lv_obj_t *noticeLabel = makeLabelAt(
      notice, 142, 11,
      txt("Keep the display powered and nearby.", "Pidä näyttö virrassa ja lähellä.",
          "Display eingeschaltet und in der Nähe lassen.", "Gardez l'écran alimenté et à proximité.",
          "Mantenga la pantalla encendida y cerca.", "Mantieni il display acceso e vicino."),
      c565(COLOR565_LABEL), &lv_font_rajdhani_12, 3);
  lv_obj_set_pos(noticeLabel, 7, 11);
  lv_obj_set_width(noticeLabel, 270);
  lv_obj_set_style_text_align(noticeLabel, LV_TEXT_ALIGN_CENTER, 0);

  firmwareUpdateActionButton = makeMenuButton(scr, 28, 198, 264, 36, "", "", false, BTN_FIRMWARE_UPDATE_ACTION);
  firmwareUpdateActionLabel = lv_obj_get_child(firmwareUpdateActionButton, 0);
  if (firmwareUpdateActionLabel) {
    lv_obj_set_style_text_font(firmwareUpdateActionLabel, &lv_font_rajdhani_12, 0);
    lv_obj_set_size(firmwareUpdateActionLabel, 248, lv_font_get_line_height(&lv_font_rajdhani_12) + 2);
    lv_obj_align(firmwareUpdateActionLabel, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_align(firmwareUpdateActionLabel, LV_TEXT_ALIGN_CENTER, 0);
  }

  firmwareUpdateUiRevision = 0;
  loadScreen(scr);
  const FirmwareUpdateBleStatus status = firmwareUpdateBleStatus();
  if (status.state == FIRMWARE_UPDATE_BLE_READY) firmwareUpdateBleStart();
  refreshFirmwareUpdateScreen(true);
}

static void showFirmwareUpdate() {
  if (firmwareUpdateUserMode) {
    showUserFirmwareUpdate();
    return;
  }
  currentAction = firmwareUpdateAction;
  lv_obj_t *scr = makeScreen();
  makeMenuTopBar(scr,
                 txt("FIRMWARE UPDATE", "LAITEOHJELMISTON PÄIVITYS", "FIRMWARE-UPDATE", "MISE À JOUR",
                     "ACTUALIZAR FIRMWARE", "AGGIORNA FIRMWARE"),
                 1, 1, false);

  lv_obj_t *statusPanel = makeInfoPanel(scr, 18, 48, 284, 35, 7);
  firmwareUpdateStateLabel = makeLabelAt(statusPanel, 142, 10, "", lv_color_white(), &lv_font_rajdhani_12, 3);
  firmwareUpdateDetailLabel = makeLabelAt(scr, 160, 91, "", c565(COLOR565_LABEL), &lv_font_rajdhani_12, 3);
  lv_obj_set_x(firmwareUpdateDetailLabel, 13);
  lv_obj_set_width(firmwareUpdateDetailLabel, 294);
  lv_obj_set_style_text_align(firmwareUpdateDetailLabel, LV_TEXT_ALIGN_CENTER, 0);

  firmwareUpdateProgress = lv_bar_create(scr);
  makePassive(firmwareUpdateProgress);
  lv_obj_set_pos(firmwareUpdateProgress, 28, 117);
  lv_obj_set_size(firmwareUpdateProgress, 264, 10);
  lv_bar_set_range(firmwareUpdateProgress, 0, 100);
  lv_obj_set_style_radius(firmwareUpdateProgress, 4, LV_PART_MAIN);
  lv_obj_set_style_bg_color(firmwareUpdateProgress, c565(COLOR565_DIM), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(firmwareUpdateProgress, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(firmwareUpdateProgress, 4, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(firmwareUpdateProgress, c565(0xFB40), LV_PART_INDICATOR);
  firmwareUpdateProgressLabel = makeLabelAt(scr, 160, 132, "", lv_color_white(), &lv_font_rajdhani_12, 3);
  lv_obj_set_x(firmwareUpdateProgressLabel, 5);
  lv_obj_set_width(firmwareUpdateProgressLabel, 310);
  lv_obj_set_style_text_align(firmwareUpdateProgressLabel, LV_TEXT_ALIGN_CENTER, 0);

  lv_obj_t *notice = makeInfoPanel(scr, 18, 151, 284, 39, 6);
  lv_obj_t *noticeLabel = makeLabelAt(
      notice, 142, 7,
      txt("Keep power connected. Only signed KAJO-Dash images are accepted.",
          "Pidä virta kytkettynä. Vain allekirjoitetut KAJO-Dash-ohjelmistot hyväksytään.",
          "Strom angeschlossen lassen. Nur signierte KAJO-Dash-Images.",
          "Gardez l'alimentation. Images KAJO-Dash signées uniquement.",
          "Mantenga la alimentación. Solo imágenes KAJO-Dash firmadas.",
          "Mantieni l'alimentazione. Solo immagini KAJO-Dash firmate."),
      c565(COLOR565_LABEL), &lv_font_rajdhani_12, 3);
  lv_obj_set_pos(noticeLabel, 7, 5);
  lv_obj_set_width(noticeLabel, 270);
  lv_obj_set_height(noticeLabel, 30);
  lv_label_set_long_mode(noticeLabel, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(noticeLabel, LV_TEXT_ALIGN_CENTER, 0);

  firmwareUpdateActionButton = makeMenuButton(scr, 28, 198, 264, 36, "", "", false, BTN_FIRMWARE_UPDATE_ACTION);
  firmwareUpdateActionLabel = lv_obj_get_child(firmwareUpdateActionButton, 0);
  if (firmwareUpdateActionLabel) {
    lv_obj_set_style_text_font(firmwareUpdateActionLabel, &lv_font_rajdhani_12, 0);
    lv_obj_set_size(firmwareUpdateActionLabel, 248, lv_font_get_line_height(&lv_font_rajdhani_12) + 2);
    lv_obj_align(firmwareUpdateActionLabel, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_align(firmwareUpdateActionLabel, LV_TEXT_ALIGN_CENTER, 0);
  }

  firmwareUpdateUiRevision = 0;
  loadScreen(scr);
  refreshFirmwareUpdateScreen(true);
}

// ── Submenu ───────────────────────────────────────────────────────────────────

static uint8_t submenuItemCount() {
  switch (submenuType) {
    case SUBMENU_DASH_UI:
      return kDashThemeCount;
    case SUBMENU_LANGUAGE:
      return LANG_COUNT;
    case SUBMENU_UNITS:
      return 4;
    case SUBMENU_VESC:
      return vehiclePageCount();
    case SUBMENU_CONTROLLER_CONFIG:
      return 1;
    case SUBMENU_BATTERY:
      return 2;
    case SUBMENU_SPEED:
      return controllerCapabilities().reportsRideMode ? 2 : 1;
    case SUBMENU_DISPLAY:
      return displayPageCount();
    case SUBMENU_PIN:
      return 3;
    case SUBMENU_RESET:
      return 2;
    case SUBMENU_CONTROLLER_TYPE:
      return CONTROLLER_TYPE_COUNT;
    case SUBMENU_AUTO_RETURN:
      return 1;
    default:
      return 1;
  }
}

static uint8_t submenuItemIndex() {
  switch (submenuType) {
    case SUBMENU_DASH_UI:
      return static_cast<uint8_t>(dashUiPreviewMode) + 1;
    case SUBMENU_LANGUAGE:
      return static_cast<uint8_t>(language) + 1;
    case SUBMENU_UNITS:
      return static_cast<uint8_t>(unitMode) + 1;
    case SUBMENU_PIN:
      if (!pinEnabled) return 1;
      return strcmp(securityPin, "1234") == 0 ? 2 : 3;
    case SUBMENU_VESC:
      return vehiclePage + 1;
    case SUBMENU_CONTROLLER_CONFIG:
      return 1;
    case SUBMENU_BATTERY:
      return batteryPage + 1;
    case SUBMENU_SPEED:
      return speedPage + 1;
    case SUBMENU_DISPLAY:
      return displayPage + 1;
    case SUBMENU_RESET:
      return 1;
    case SUBMENU_CONTROLLER_TYPE:
      return static_cast<uint8_t>(pendingControllerType) + 1;
    default:
      return 1;
  }
}

static const char *submenuCurrentName() {
  switch (submenuType) {
    case SUBMENU_DASH_UI:
      return modeName(dashUiPreviewMode);
    case SUBMENU_LANGUAGE:
      return languageName();
    case SUBMENU_UNITS:
      return unitName();
    case SUBMENU_VESC:
      return txt("Stored on this display", "Tallennettu näyttöön", "Gespeichert",
                 "Enregistré sur l'écran", "Guardado en la pantalla", "Salvato sul display");
    case SUBMENU_CONTROLLER_CONFIG:
      return txt("Controller reading & speed modes", "Ohjaimen luku ja nopeustilat",
                 "Controllerdaten & Fahrmodi", "Lecture contrôleur et vitesses",
                 "Lectura del controlador y modos", "Lettura controller e modalità");
    case SUBMENU_SPEED:
      return txt("Controller ride modes", "Ohjaimen ajotilat", "Controller-Fahrmodi", "Modes contrôleur", "Modos controlador", "Modalità controller");
    case SUBMENU_PIN:
      return pinStatusName();
    case SUBMENU_DISPLAY:
      if (displayPage == 0)
        return txt("Brightness & lighting", "Kirkkaus ja valot", "Helligkeit & Licht", "Luminosité & éclairage",
                   "Brillo e iluminación", "Luminosità e luci");
      return txt("Touch & panel", "Kosketus ja paneeli", "Touch & Panel", "Tactile & dalle", "Táctil y panel",
                 "Touch e pannello");
    case SUBMENU_DISPLAY_INFO:
      return txt("Vehicle and display", "Ajoneuvo ja näyttö", "Fahrzeug und Display", "Véhicule et écran",
                 "Vehículo y pantalla", "Veicolo e display");
    case SUBMENU_PANEL_COLORS:
      return txt("Panel compatibility", "Paneelin yhteensopivuus", "Panel-Kompatibilität", "Compatibilité dalle",
                 "Compatibilidad panel", "Compatibilità pannello");
    case SUBMENU_CHANGELOG:
      return txt("Recent firmware changes", "Viimeisimmät muutokset", "Letzte Firmware-Änderungen",
                 "Derniers changements", "Cambios recientes", "Modifiche recenti");
    case SUBMENU_BATTERY:
      return txt("Energy and health", "Energia ja kunto", "Energie und Zustand", "Énergie et santé",
                 "Energía y salud", "Energia e salute");
    case SUBMENU_CONFIGURATOR:
      return txt("New User Setup", "Käyttöönotto", "Ersteinrichtung", "Config. initiale", "Config. inicial", "Config. iniziale");
    case SUBMENU_RESET:
      return txt("Choose reset type", "Valitse nollaus", "Reset-Typ", "Type de réinit.", "Tipo de reinicio", "Tipo di reset");
    case SUBMENU_CONTROLLER_TYPE:
      return pendingControllerType == CONTROLLER_FARDRIVER ? "FarDriver | Bluetooth LE" : "VESC";
    case SUBMENU_AUTO_RETURN:
      return autoReturnEnabled
                 ? txt("Return timer enabled", "Paluuaika käytössä", "Rückkehr-Timer aktiv", "Minuteur actif",
                       "Temporizador activo", "Timer attivo")
                 : txt("Automatic return disabled", "Automaattipaluu pois", "Auto-Rückkehr aus",
                       "Retour automatique désactivé", "Retorno automático desactivado", "Ritorno automatico disattivo");
    default:
      return "";
  }
}

static void submenuPrevious() {
  if (submenuType == SUBMENU_DASH_UI) {
    captureDashboardCustomization(dashUiPreviewMode);
    dashUiPreviewMode = static_cast<DashboardMode>((static_cast<int>(dashUiPreviewMode) + kDashThemeCount - 1) %
                                                   kDashThemeCount);
    applyDashboardCustomization(dashUiPreviewMode);
  } else if (submenuType == SUBMENU_LANGUAGE) {
    cycleLanguage();
  } else if (submenuType == SUBMENU_UNITS) {
    unitMode = static_cast<UnitMode>((static_cast<int>(unitMode) + UNITS_COUNT - 1) % UNITS_COUNT);
  } else if (submenuType == SUBMENU_VESC) {
    if (vehiclePage > 0) vehiclePage--;
  } else if (submenuType == SUBMENU_SPEED) {
    if (speedPage > 0) speedPage--;
  } else if (submenuType == SUBMENU_DISPLAY) {
    if (displayPage > 0) displayPage--;
  } else if (submenuType == SUBMENU_BATTERY) {
    if (batteryPage > 0) batteryPage--;
  } else if (submenuType == SUBMENU_PIN) {
    cyclePinOption();
  }
}

static void submenuNext() {
  if (submenuType == SUBMENU_DASH_UI) {
    captureDashboardCustomization(dashUiPreviewMode);
    dashUiPreviewMode = static_cast<DashboardMode>((static_cast<int>(dashUiPreviewMode) + 1) % kDashThemeCount);
    applyDashboardCustomization(dashUiPreviewMode);
  } else if (submenuType == SUBMENU_LANGUAGE) {
    cycleLanguage();
  } else if (submenuType == SUBMENU_UNITS) {
    cycleUnits();
  } else if (submenuType == SUBMENU_VESC) {
    if (vehiclePage + 1 < vehiclePageCount()) vehiclePage++;
  } else if (submenuType == SUBMENU_SPEED) {
    if (speedPage + 1 < submenuItemCount()) speedPage++;
  } else if (submenuType == SUBMENU_DISPLAY) {
    if (displayPage + 1 < displayPageCount()) displayPage++;
  } else if (submenuType == SUBMENU_PANEL_COLORS) {
    if (panelPage + 1 < kPanelPageCount) panelPage++;
  } else if (submenuType == SUBMENU_BATTERY) {
    if (batteryPage < 1) batteryPage++;
  } else if (submenuType == SUBMENU_PIN) {
    cyclePinOption();
  }
}

static void submenuAction(int id) {
  if (submenuType == SUBMENU_GAUGE_RANGES && (id == BTN_OPTION_BASE + VEHICLE_FIELD_COUNT + 10 || id == BTN_OPTION_BASE + VEHICLE_FIELD_COUNT + 11)) {
    if (id == BTN_OPTION_BASE + VEHICLE_FIELD_COUNT + 10) automaticGaugeRanges = !automaticGaugeRanges;
    else resetAutomaticGaugeRanges();
    saveAppSettings(); queueRebuild(SCREEN_SUBMENU); return;
  }

  if (submenuType == SUBMENU_DISPLAY) {
    if (id == BTN_DEV_PROMPT_CANCEL && developerPrompt) {
      pendingLightSensorDarkRaw = -1;
      closeDeveloperPrompt();
      return;
    }
    if (id == BTN_LIGHT_SENSOR_CALIBRATE) {
      showLightSensorDarkPrompt();
      return;
    }
    if (id == BTN_LIGHT_SENSOR_SET_DARK) {
      pendingLightSensorDarkRaw = lightSensorRaw();
      closeDeveloperPrompt();
      showLightSensorBrightPrompt();
      return;
    }
    if (id == BTN_LIGHT_SENSOR_SET_BRIGHT) {
      const int brightRaw = lightSensorRaw();
      closeDeveloperPrompt();
      const bool calibrated = pendingLightSensorDarkRaw >= 0 &&
                              setLightSensorCalibration(pendingLightSensorDarkRaw, brightRaw);
      pendingLightSensorDarkRaw = -1;
      showStatusNotice(calibrated ? NOTICE_LIGHT_SENSOR_CALIBRATED : NOTICE_LIGHT_SENSOR_RANGE_TOO_SMALL);
      return;
    }
  }
  if (submenuType == SUBMENU_CONTROLLER_TYPE) {
    if (id == BTN_CONTROLLER_VESC || id == BTN_CONTROLLER_FARDRIVER) {
      pendingControllerType = id == BTN_CONTROLLER_VESC ? CONTROLLER_VESC : CONTROLLER_FARDRIVER;
      if (pendingControllerType == CONTROLLER_FARDRIVER) {
        pendingControllerConnection = CONTROLLER_CONNECTION_BLE;
        controllerSetupStage = CONTROLLER_SETUP_DISCOVERY;
        requestControllerScan();
      } else {
        controllerSetupStage = CONTROLLER_SETUP_CONNECTION;
      }
      queueLowMemoryRebuild(SCREEN_SUBMENU);
      return;
    }
    if (id == BTN_CONTROLLER_UART || id == BTN_CONTROLLER_BLE) {
      pendingControllerConnection = id == BTN_CONTROLLER_UART ? CONTROLLER_CONNECTION_UART : CONTROLLER_CONNECTION_BLE;
      if (pendingControllerConnection == CONTROLLER_CONNECTION_UART) {
        controllerSetupStage = CONTROLLER_SETUP_CONFIRM;
      } else {
        controllerSetupStage = CONTROLLER_SETUP_DISCOVERY;
        requestControllerScan();
      }
      queueLowMemoryRebuild(SCREEN_SUBMENU);
      return;
    }
    if (id == BTN_CONTROLLER_RETRY) {
      requestControllerScan();
      return;
    }
    if (id == BTN_CONTROLLER_LIST_UP) {
      if (controllerDeviceOffset > 0) controllerDeviceOffset--;
      queueLowMemoryRebuild(SCREEN_SUBMENU);
      return;
    }
    if (id == BTN_CONTROLLER_LIST_DOWN) {
      const ControllerBackend *backend = pendingControllerBackend();
      const uint8_t count = backend && backend->linkStatus ? backend->linkStatus().deviceCount : 0;
      if (controllerDeviceOffset + 3 < count) controllerDeviceOffset++;
      queueLowMemoryRebuild(SCREEN_SUBMENU);
      return;
    }
    if (id >= BTN_CONTROLLER_DEVICE_BASE && id < BTN_CONTROLLER_DEVICE_BASE + CONTROLLER_LINK_MAX_DEVICES) {
      const uint8_t index = id - BTN_CONTROLLER_DEVICE_BASE;
      const ControllerBackend *backend = pendingControllerBackend();
      if (backend && backend->connectDevice) backend->connectDevice(index);
      return;
    }
    if (id == BTN_CONTROLLER_CONTINUE) {
      controllerSetupStage = CONTROLLER_SETUP_CONFIRM;
      queueLowMemoryRebuild(SCREEN_SUBMENU);
      return;
    }
    if (id == BTN_CONTROLLER_CANCEL_APPLY) {
      controllerSetupStage = pendingControllerType == CONTROLLER_VESC &&
                                     pendingControllerConnection == CONTROLLER_CONNECTION_UART
                                 ? CONTROLLER_SETUP_CONNECTION : CONTROLLER_SETUP_TYPE;
      queueLowMemoryRebuild(SCREEN_SUBMENU);
      return;
    }
    if (id == BTN_CONTROLLER_APPLY) {
      const ControllerType type = pendingControllerType;
      const ControllerConnection connection =
          pendingControllerType == CONTROLLER_FARDRIVER ? CONTROLLER_CONNECTION_BLE : pendingControllerConnection;
      // The switch is asynchronous and sets the selection itself; write it here
      // too so a selection that needs no switch still persists.
      if (!controllerManagerSwitchTo(type, connection)) {
        controllerType = type;
        controllerConnection = connection;
      }
      saveAppSettings();
      // The wizard is finished: say so and hand the display back, the same way
      // the first-boot configurator ends. Landing back in the settings menu
      // with no word of the outcome read as nothing having happened.
      settingsMenuLevel = SETTINGS_MENU_CONTROLLER;
      queueStatusNotice(NOTICE_CONTROLLER_SETUP_APPLIED);
      queueRebuild(SCREEN_DASHBOARD);
      return;
    }
  }
  if (submenuType == SUBMENU_LOGGING && id == BTN_EXPLORE_RIDE_LOGS) {
    rideLogsPage = 0;
    rideLoggerRequestCatalog();
    queueLowMemoryRebuild(SCREEN_RIDE_LOGS);
    return;
  }
  if (submenuType == SUBMENU_DASH_UI && id >= BTN_DASH_GRID_BASE && id < BTN_DASH_GRID_BASE + kDashThemeCount) {
    dashUiGridReturnPage = dashUiGridPage;
    selectorSavedFeedback = false;
    dashUiPreviewMode = static_cast<DashboardMode>(id - BTN_DASH_GRID_BASE);
    applyDashboardCustomization(dashUiPreviewMode);
    dashUiGridOpen = false;
    colorPaletteOpen = false;
    gradientPanelOpen = false;
    dataPanelOpen = false;
    dataChoiceOpen = false;
    queueLowMemoryRebuild(SCREEN_SUBMENU);
    return;
  }
  if (submenuType == SUBMENU_DASH_UI && !dashUiGridOpen && id != BTN_SAVE && id != BTN_BACK) {
    selectorSavedFeedback = false;
  }
  if (submenuType == SUBMENU_DASH_UI && dashUiGridOpen && id == BTN_NEXT) {
    if (dashUiGridPage + 1 < kDashGridPageCount) {
      dashUiGridPage++;
      queueLowMemoryRebuild(SCREEN_SUBMENU);
    }
    return;
  }
  if (firstBootSetupActive && id == BTN_NEXT &&
      (submenuType == SUBMENU_LANGUAGE || submenuType == SUBMENU_UNITS)) {
    saveAppSettings();
    if (submenuType == SUBMENU_LANGUAGE) {
      submenuType = SUBMENU_UNITS;
      queueRebuild(SCREEN_SUBMENU);
    } else {
      submenuType = SUBMENU_DASH_UI;
      prepareConfiguratorThemeStep();
      queueLowMemoryRebuild(SCREEN_SUBMENU);
    }
    return;
  }
  if (id == BTN_CONTROLLER_DIAGNOSTICS) {
    queueLowMemoryRebuild(SCREEN_FARDRIVER_BLE);
    return;
  }
  if (id == BTN_RESET_CANCEL) {
    cancelResetCountdown();
    queueRebuild(SCREEN_SUBMENU);
    return;
  }
  if (id == BTN_COLORS_MASTER) {
    toggleColorsMaster();
    return;
  }
  if (id == BTN_RESET_CURRENT_UI) {
    resetDashboardCustomization(dashUiPreviewMode);
    dashboardCustomizationBackup[dashUiPreviewMode] = dashboardCustomizations[dashUiPreviewMode];
    saveDashboardCustomizationProfiles();
    dataChoiceOpen = false;
    dataListOffset = 0;
    queueLowMemoryRebuild(SCREEN_SUBMENU);
    return;
  }
  if (id == BTN_COLOR_TOGGLE) {
    toggleSelectorPalette();
    return;
  }
  if (id >= BTN_COLOR_BASE && id < BTN_COLOR_BASE + ACCENT_COUNT) {
    accentTheme = static_cast<AccentTheme>(id - BTN_COLOR_BASE);
    captureDashboardCustomization(dashUiPreviewMode);
    queueLowMemoryRebuild(SCREEN_SUBMENU);  // repaint after freeing the old preview
    return;
  }
  if (id >= BTN_THEME_DARK && id <= BTN_THEME_AUTO) {
    dashboardAppearanceMode = static_cast<DashboardAppearanceMode>(id - BTN_THEME_DARK);
    captureDashboardCustomization(dashUiPreviewMode);
    autoBrightnessPromptOpen = dashboardAppearanceMode == DASH_APPEARANCE_AUTO && !autoBrightnessEnabled;
    queueLowMemoryRebuild(SCREEN_SUBMENU);
    return;
  }
  if (id == BTN_AUTO_BRIGHTNESS_YES || id == BTN_AUTO_BRIGHTNESS_NO) {
    if (id == BTN_AUTO_BRIGHTNESS_YES) {
      autoBrightnessEnabled = true;
      saveAutoBrightnessSetting();
    }
    autoBrightnessPromptOpen = false;
    rebuildSelectorPopup();
    return;
  }
  if (id == BTN_GRADIENT_TOGGLE) {
    openSelectorGradientPanel();
    return;
  }
  if (id == BTN_DATA_TAB) {
    openSelectorDataPanel();
    return;
  }
  if (id == BTN_DATA_BACK) {
    dataChoiceOpen = false;
    dataChoiceOffset = 0;
    rebuildSelectorPopup();
    return;
  }
  if (id == BTN_DATA_UP || id == BTN_DATA_DOWN) {
    const int direction = id == BTN_DATA_UP ? -1 : 1;
    uint8_t &offset = dataChoiceOpen ? dataChoiceOffset : dataListOffset;
    const int count = dataChoiceOpen ? DATA_COUNT : dashboardDataSlotCount(dashUiPreviewMode);
    offset = static_cast<uint8_t>(constrain((int)offset + direction, 0, max(0, count - 3)));
    rebuildSelectorPopup();
    return;
  }
  if (id >= BTN_DATA_SLOT_BASE && id < BTN_DATA_SLOT_BASE + DASH_DATA_SLOTS_MAX) {
    dataSelectedSlot = static_cast<uint8_t>(id - BTN_DATA_SLOT_BASE);
    if (dataSelectedSlot < dashboardDataSlotCount(dashUiPreviewMode)) {
      dataChoiceOpen = true;
      const int selected = dashboardDataSelection(dashUiPreviewMode, dataSelectedSlot);
      dataChoiceOffset = static_cast<uint8_t>(constrain(selected - 1, 0, max(0, (int)DATA_COUNT - 3)));
      rebuildSelectorPopup();
    }
    return;
  }
  if (id >= BTN_DATA_CHOICE_BASE && id < BTN_DATA_CHOICE_BASE + DATA_COUNT) {
    setDashboardDataSelection(dashUiPreviewMode, dataSelectedSlot,
                              static_cast<DashboardDataItem>(id - BTN_DATA_CHOICE_BASE));
    dataChoiceOpen = false;
    queueLowMemoryRebuild(SCREEN_SUBMENU);
    return;
  }
  if (id == BTN_GRADIENT_BACK) {
    gradientPanelOpen = false;
    slideSelectorPart(selectorArrowLeft, 3, true, true);
    slideSelectorPart(selectorArrowRight, 279, true, true);
    rebuildSelectorPopup();
    return;
  }
  if (id == BTN_GRADIENT_ENABLE) {
    dashboardGradientEnabled = !dashboardGradientEnabled;
    rebuildSelectorPopup();
    return;
  }
  if (id == BTN_GRADIENT_ORIENTATION) {
    dashboardGradientHorizontal = !dashboardGradientHorizontal;
    rebuildSelectorPopup();
    return;
  }
  if (id == BTN_GRADIENT_BELL) {
    dashboardGradientBell = !dashboardGradientBell;
    rebuildSelectorPopup();
    return;
  }
  if (id >= BTN_GRADIENT_COLOR_BASE && id < BTN_GRADIENT_COLOR_BASE + ACCENT_COUNT) {
    dashboardGradientTheme = static_cast<AccentTheme>(id - BTN_GRADIENT_COLOR_BASE);
    captureDashboardCustomization(dashUiPreviewMode);
    queueLowMemoryRebuild(SCREEN_SUBMENU);
    return;
  }
  if (id >= BTN_BACKGROUND_STYLE_BASE && id < BTN_BACKGROUND_STYLE_BASE + 4) {
    selectOrRotateBackgroundStyle(id - BTN_BACKGROUND_STYLE_BASE);
    captureDashboardCustomization(dashUiPreviewMode);
    queueLowMemoryRebuild(SCREEN_SUBMENU);
    return;
  }
  switch (id) {
    case BTN_USER_COMPANION:
      queueLowMemoryRebuild(SCREEN_COMPANION_MODE);
      return;
    case BTN_BACK:
      if (firstBootSetupActive && submenuType == SUBMENU_LANGUAGE) {
        firstBootSetupActive = false;
        configStep = 0;
        queueRebuild(SCREEN_CONFIG);
        return;
      }
      if (firstBootSetupActive && submenuType == SUBMENU_UNITS) {
        submenuType = SUBMENU_LANGUAGE;
        queueRebuild(SCREEN_SUBMENU);
        return;
      }
      if (submenuType == SUBMENU_CONTROLLER_TYPE && controllerSetupStage != CONTROLLER_SETUP_TYPE) {
        if (controllerSetupStage == CONTROLLER_SETUP_CONNECTION) controllerSetupStage = CONTROLLER_SETUP_TYPE;
        else if (pendingControllerType == CONTROLLER_VESC) controllerSetupStage = CONTROLLER_SETUP_CONNECTION;
        else controllerSetupStage = CONTROLLER_SETUP_TYPE;
        queueLowMemoryRebuild(SCREEN_SUBMENU);
        return;
      }
      if (submenuType == SUBMENU_DISPLAY && displayPage > 0) {
        displayPage--;
        queueRebuild(SCREEN_SUBMENU);
        return;
      }
      if (submenuType == SUBMENU_PANEL_COLORS) {
        // The panel pages are entered from the second DISPLAY page.
        if (panelPage > 0) {
          panelPage--;
        } else {
          submenuType = SUBMENU_DISPLAY;
          displayPage = 1;
        }
        queueRebuild(SCREEN_SUBMENU);
        return;
      }
      if (submenuType == SUBMENU_VESC && vehiclePage > 0) {
        vehiclePage--;
        queueRebuild(SCREEN_SUBMENU);
        return;
      }
      // leaving a selector without SAVE keeps the previously chosen setting
      if (submenuType == SUBMENU_DASH_UI) {
        if (dashUiGridOpen && dashUiGridPage > 0) {
          dashUiGridPage--;
          queueLowMemoryRebuild(SCREEN_SUBMENU);
          return;
        }
        restoreDashboardCustomizations();
        if (!dashUiGridOpen) {
          // The full-screen choice is only a preview until SAVE. BACK must
          // therefore mark the persisted dashboard, not the abandoned preview.
          dashUiPreviewMode = dashboardMode;
          applyDashboardCustomization(dashUiPreviewMode);
          dashUiGridOpen = true;
          dashUiGridPage = dashUiGridReturnPage;
          colorPaletteOpen = false;
          gradientPanelOpen = false;
          dataPanelOpen = false;
          dataChoiceOpen = false;
          queueLowMemoryRebuild(SCREEN_SUBMENU);
          return;
        }
        dashUiPreviewMode = dashboardMode;
        applyDashboardCustomization(dashboardMode);
        if (firstBootSetupActive) {
          submenuType = SUBMENU_UNITS;
          queueRebuild(SCREEN_SUBMENU);
          return;
        }
      }
      if (submenuType == SUBMENU_RESET) {
        cancelResetCountdown();
        resetConfirmMode = -1;
      }
      if (submenuType == SUBMENU_BATTERY && batteryPage > 0) {
        batteryPage--; queueRebuild(SCREEN_SUBMENU); return;
      }
      if (submenuType == SUBMENU_SPEED && speedPage > 0) {
        speedPage--; queueRebuild(SCREEN_SUBMENU); return;
      }
      if (submenuType == SUBMENU_SPEED || submenuType == SUBMENU_POWER_LIMITS ||
          submenuType == SUBMENU_SPEED_CALIBRATION || submenuType == SUBMENU_GAUGE_RANGES) {
        submenuType = SUBMENU_CONTROLLER_CONFIG;
        queueRebuild(SCREEN_SUBMENU); return;
      }
      if (submenuType == SUBMENU_CONTROLLER_TYPE && !firstBootSetupActive) {
        submenuType = SUBMENU_CONNECTION; queueRebuild(SCREEN_SUBMENU); return;
      }
      if (submenuType == SUBMENU_DASH_UI) applyDashboardCustomization(dashboardMode);
      queueRebuild(SCREEN_MENU);
      return;
    case BTN_SAVE:
      if (submenuType == SUBMENU_DASH_UI) {
        captureDashboardCustomization(dashUiPreviewMode);
        dashboardMode = dashUiPreviewMode;
        applyDashboardCustomization(dashboardMode);
        saveAppSettings();
        // SAVE is an in-place commit. This becomes the rollback baseline for
        // any further edits made before the user eventually presses BACK.
        backupDashboardCustomizations();
        accentThemeBackup = accentTheme;
        gradientEnabledBackup = dashboardGradientEnabled;
        gradientHorizontalBackup = dashboardGradientHorizontal;
        gradientReverseBackup = dashboardGradientReverse;
        gradientBellBackup = dashboardGradientBell;
        gradientPositionBackup = dashboardGradientPosition;
        gradientThemeBackup = dashboardGradientTheme;
        dashUiGridReturnPage = 0;
        selectorSavedFeedback = true;
        if (firstBootSetupActive) {
          finishConfigurator();
          return;
        }
        queueLowMemoryRebuild(SCREEN_SUBMENU);
        return;
      }
      saveAppSettings();
      queueRebuild(SCREEN_MENU);
      return;
    case BTN_PREV:
      submenuPrevious();
      if (submenuType == SUBMENU_DASH_UI)
        queueLowMemoryRebuild(SCREEN_SUBMENU);
      else
        queueLowMemoryRebuild(SCREEN_SUBMENU);
      return;
    case BTN_NEXT:
      submenuNext();
      if (submenuType == SUBMENU_DASH_UI)
        queueLowMemoryRebuild(SCREEN_SUBMENU);
      else
        queueRebuild(SCREEN_SUBMENU);
      return;
    default:
      break;
  }
  const int option = id - BTN_OPTION_BASE;
  if (option < 0) return;
  switch (submenuType) {
    case SUBMENU_DEMO:
      if (option == 0) setDemoMode(!dashboardDemoModeEnabled);
      else if (option == 1) cycleDemoTimeScale();
      else if (option == 2) restartDemoRide();
      saveAppSettings();
      queueRebuild(SCREEN_SUBMENU);
      return;
    case SUBMENU_DASH_UI:
      dashboardMode = dashUiPreviewMode;
      applyDashboardCustomization(dashboardMode);
      queueRebuild(SCREEN_MENU);
      return;
      break;
    case SUBMENU_LANGUAGE:
      language = static_cast<Language>(constrain(option, 0, static_cast<int>(LANG_COUNT) - 1));
      break;
    case SUBMENU_UNITS:
      unitMode = static_cast<UnitMode>(constrain(option, 0, 3));
      break;
    case SUBMENU_CONTROLLER_TYPE:
      pendingControllerType = static_cast<ControllerType>(constrain(option, 0, (int)CONTROLLER_TYPE_COUNT - 1));
      queueRebuild(SCREEN_SUBMENU);
      return;
    case SUBMENU_PIN:
      if (option == 0) {
        pinEnabled = false;
      } else {
        pinEnabled = true;
        strncpy(securityPin, option == 1 ? "1234" : "0000", sizeof(securityPin));
        securityPin[sizeof(securityPin) - 1] = '\0';
      }
      break;
    case SUBMENU_LOGGING: {
      const RideLoggingStatus loggingStatus = rideLoggerStatus();
      // Switching on needs a card that is ready now. Switching off and the
      // sample rate stay available without one.
      if (option == 0 && loggingStatus.mode == RIDE_LOG_OFF &&
          (loggingStatus.cardChecking || !loggingStatus.cardReady)) {
        showStatusNotice(NOTICE_SD_CARD_NOT_READY);
        return;
      }
      if (option == 0)
        rideLoggerToggleEnabled();
      else if (option == 1)
        rideLoggerCycleRate();
      queueLowMemoryRebuild(SCREEN_SUBMENU);
      return;
    }
    case SUBMENU_DISPLAY:
      if (option == 0) {
        autoBrightnessEnabled = !autoBrightnessEnabled;
        if (!autoBrightnessEnabled) applyDisplayBrightness();  // restore manual level
        saveAppSettings();
      } else if (option == 1) {
        themeLedEnabled = !themeLedEnabled;  // LED updates on the next 100 ms tick
        saveAppSettings();
      } else if (option == 2) {
        runTouchCalibration();
        touchInputArmAfterRecovery();
        queueRebuild(SCREEN_SUBMENU);
        return;
      } else if (option == 3) {
        queueRebuild(SCREEN_TOUCH_TEST);
        return;
      } else if (option == 4) {
        submenuType = SUBMENU_PANEL_COLORS;
        panelPage = 0;
        queueRebuild(SCREEN_SUBMENU);
        return;
      }
      break;
    case SUBMENU_PANEL_COLORS:
      if (option == 0 || option == 1) {
        const DisplayPanelProfile profile = option == 1 ? DISPLAY_PANEL_ALTERNATE : DISPLAY_PANEL_STANDARD;
        // Choosing the other panel type also starts it from that glass's
        // own tuning; re-tapping the current type keeps a custom choice.
        if (profile != displayPanelProfile) {
          displayPanelTuning = defaultDisplayPanelTuning(profile);
          saveDisplayPanelTuning();
        }
        displayPanelProfile = profile;
        applyDisplayPanelProfile();
        saveDisplayPanelProfile();
        queueRebuild(SCREEN_SUBMENU);
        return;
      }
      if (option == 10 || option == 11) {
        displayPanelTuning = (displayPanelTuning + (option == 10 ? PANEL_TUNING_COUNT - 1 : 1)) % PANEL_TUNING_COUNT;
        applyDisplayPanelTuning();
        saveDisplayPanelTuning();
        queueRebuild(SCREEN_SUBMENU);
        return;
      }
      break;
    case SUBMENU_AUTO_RETURN:
      if (option == 0) {
        autoReturnEnabled = !autoReturnEnabled;
      } else if (option <= (int)(sizeof(kAutoReturnChoices) / sizeof(kAutoReturnChoices[0]))) {
        autoReturnTimeoutSeconds = kAutoReturnChoices[option - 1];
        autoReturnEnabled = true;
      }
      saveAppSettings();
      queueRebuild(SCREEN_SUBMENU);
      return;
    case SUBMENU_CONTROLLER_CONFIG: {
      const SubmenuType sections[] = {SUBMENU_POWER_LIMITS, SUBMENU_SPEED, SUBMENU_SPEED_CALIBRATION, SUBMENU_GAUGE_RANGES};
      if (option < 0 || option >= 4) return;
      submenuType = sections[option]; speedPage = 0;
      queueRebuild(SCREEN_SUBMENU); return;
    }
    case SUBMENU_CONNECTION:
      if (option == kConnectionBluetoothOption) {
        bluetoothEnabled = !bluetoothEnabled;
        saveAppSettings();
        controllerManagerApplyBluetoothEnabled();
        queueRebuild(SCREEN_SUBMENU);
        return;
      }
      if (option == VEHICLE_FIELD_SETUP) {
        enableBluetoothForControllerSetup();
        pendingControllerType = controllerType; pendingControllerConnection = controllerConnection;
        controllerSetupStage = CONTROLLER_SETUP_TYPE; controllerDeviceOffset = 0;
        submenuType = SUBMENU_CONTROLLER_TYPE; queueRebuild(SCREEN_SUBMENU); return;
      }
      if (option != VEHICLE_FIELD_VESC_BAUD && option != VEHICLE_FIELD_VESC_CAN_ID) return;
      if (!controllerFieldApplies(option)) return;
      // Fall through to the common local field editor.
    case SUBMENU_VESC:
    case SUBMENU_SPEED_CALIBRATION:
    case SUBMENU_GAUGE_RANGES:
    case SUBMENU_BATTERY:
    case SUBMENU_SPEED:
      if (submenuType == SUBMENU_BATTERY && batteryPage != 1) return;
      if (submenuType == SUBMENU_SPEED && (!controllerCapabilities().reportsRideMode || speedPage != 1 ||
          option < VEHICLE_FIELD_MODE_LABEL_1 || option > VEHICLE_FIELD_MODE_LABEL_3)) return;
      if (vehicleFieldIsText(option) || vehicleFieldIsNumeric(option)) {
        vehicleTextField = option; textInputContext = INPUT_VEHICLE_FIELD;
        queueRebuild(SCREEN_TEXT_INPUT); return;
      }
      break;
    case SUBMENU_CONFIGURATOR:
      if (option == 0) {
        firstBootSetupActive = false;
        configStep = 0;
        queueRebuild(SCREEN_CONFIG);
        return;
      }
      queueRebuild(SCREEN_MENU);
      return;
    case SUBMENU_RESET:
      if (resetConfirmMode < 0) {
        resetConfirmMode = option;
        queueRebuild(SCREEN_SUBMENU);
        return;
      }
      if (option == 0) {
        startResetCountdown();
        return;
      }
      resetConfirmMode = -1;
      queueRebuild(SCREEN_MENU);
      return;
    default:
      return;
  }
  queueRebuild(SCREEN_SUBMENU);
}

static void makeSubmenuTopBar(lv_obj_t *scr, const char *title) {
  lv_obj_t *back = makeNavButton(scr, kMenuEdgeX, 4,
                                 txt("< BACK", "< TAKAISIN", "< ZURÜCK", "< RETOUR", "< ATRÁS", "< INDIETRO"),
                                 BTN_BACK, kTopBarButtonH);
  lv_obj_set_width(back, kWideTopNavW);
  char itemText[48];
  // screens without a selectable list (e.g. DISPLAY) only get the caption
  if (submenuItemCount() > 1) {
    snprintf(itemText, sizeof(itemText), "%u/%u - %s", submenuItemIndex(), submenuItemCount(), submenuCurrentName());
  } else {
    snprintf(itemText, sizeof(itemText), "%s", submenuCurrentName());
  }
  makeHeaderBox(scr, title, itemText, 90, 152);
  lv_obj_t *next = NULL;
  if (firstBootSetupActive && (submenuType == SUBMENU_LANGUAGE || submenuType == SUBMENU_UNITS)) {
    next = makeNavButton(
        scr, kWideTopNavRightX, 4,
        txt("NEXT >", "SEUR >", "WEITER >", "SUIV >", "SIG >", "AVANTI >"), BTN_NEXT, kTopBarButtonH);
    lv_obj_set_width(next, kWideTopNavW);
  }
  // Keep navigation above the passive header labels, including translations
  // whose label boxes extend beyond their visible glyphs.
  lv_obj_move_foreground(back);
  if (next) lv_obj_move_foreground(next);
}

// Borderless horizontal gradient for the panel colour check. Segments placed
// edge to edge read as one continuous sweep.
static void makeDisplayGradientReference(lv_obj_t *parent, int x, int y, int w, int h, uint16_t from,
                                         uint16_t to) {
  lv_obj_t *sweep = lv_obj_create(parent);
  lv_obj_remove_style_all(sweep);
  lv_obj_set_pos(sweep, x, y);
  lv_obj_set_size(sweep, w, h);
  lv_obj_set_style_bg_color(sweep, c565(from), 0);
  lv_obj_set_style_bg_grad_color(sweep, c565(to), 0);
  lv_obj_set_style_bg_grad_dir(sweep, LV_GRAD_DIR_HOR, 0);
  lv_obj_set_style_bg_opa(sweep, LV_OPA_COVER, 0);
  lv_obj_clear_flag(sweep, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
}

static void makeDisplayColorReference(lv_obj_t *parent, int x, int y, int w, uint16_t color,
                                      const char *caption, uint16_t textColor, int h = 22) {
  lv_obj_t *swatch = lv_obj_create(parent);
  lv_obj_remove_style_all(swatch);
  lv_obj_set_pos(swatch, x, y);
  lv_obj_set_size(swatch, w, h);
  lv_obj_set_style_radius(swatch, 3, 0);
  lv_obj_set_style_bg_color(swatch, c565(color), 0);
  lv_obj_set_style_bg_opa(swatch, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(swatch, 1, 0);
  lv_obj_set_style_border_color(swatch, c565(COLOR565_DIM), 0);
  lv_obj_clear_flag(swatch, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
  makeLabelAt(swatch, w / 2, 5, caption, c565(textColor), &lv_font_rajdhani_12, 3);
}

static void makeDashUiPreview(lv_obj_t *scr) {
  setDemoPreview(true);  // independent from dashboard demo and live logging
  setDemoPreviewFrozen(true);
  DashboardValues values = makeDummyValues();
  setDashboardTelemetryFields(TELEMETRY_FIELDS_ALL);
  setDashboardStartupSweepEnabled(false);  // the theme is shown at rest, not self-tested
  buildDashboardMode(scr, dashUiPreviewMode, values);
  setDashboardStartupSweepEnabled(true);
  updateDashboardMode(dashUiPreviewMode, values, true);
  selectorPreviewLastUpdateMs = millis();
}

// ── Selector overlay (DASH UI) ───────────────────────────────────────────────
// The controls live on two containers, one hugging each edge, so they can
// slide off-screen as a unit: tapping the preview clears both for a fullscreen
// look, tapping again brings them back. The bottom one carries colour choice,
// which belongs with the theme it is previewed against.

static lv_obj_t *selectorOverlay = NULL;
static lv_obj_t *selectorTopBack = NULL;
static lv_obj_t *selectorTopInfo = NULL;
static lv_obj_t *selectorTopSave = NULL;
static lv_obj_t *selectorColorBar = NULL;
static lv_obj_t *selectorPalette = NULL;
static bool selectorOverlayHidden = false;
static int selectorColorBarY = 0;      // resting y, so it can slide back to it
static const int kSelectorOverlayH = 44;  // controls occupy y 4..40
static const int kColorBarH = 44;         // collapsed strip
static const int kSelectorArrowW = 38;    // side steppers, sized for a thumb
static const int kSelectorArrowH = 76;
static const int kSelectorArrowInset = 3;
static lv_timer_t *selectorDemoTimer = nullptr;

static void cancelSelectorDemoTimer() {
  if (selectorDemoTimer) lv_timer_del(selectorDemoTimer);
  selectorDemoTimer = nullptr;
}

// A clear view of the theme at rest, then the demo ride takes over: the instruments glide from
// the thumbnail readings into the ride's own, with no self-test sweep in between.
static void selectorDemoTimerCb(lv_timer_t *) {
  selectorDemoTimer = nullptr;
  if (selectorOverlayHidden && !colorPaletteOpen && demoPreviewIsFrozen()) {
    setDemoPreviewFrozen(false);
    selectorPreviewLastUpdateMs = 0;  // the first ride reading is due at once
  }
}

static bool selectorTransitionActive = false;
static uint8_t selectorTransitionPending = 0;

static bool selectorPreviewUpdateDue() {
  // Configuration actions need the whole frame budget. A stationary
  // preview only needs a gentle sense of live data; the fully exposed theme
  // retains the normal 10 Hz demo rate.
  if (colorPaletteOpen) return false;
  if (demoPreviewIsFrozen()) return false;
  const uint32_t now = millis();
  const uint32_t period = selectorOverlayHidden ? 100 : 300;
  if (selectorPreviewLastUpdateMs != 0 && now - selectorPreviewLastUpdateMs < period) return false;
  selectorPreviewLastUpdateMs = now;
  return true;
}

static void selectorOverlayAnimCb(void *obj, int32_t v) {
  lv_obj_set_y((lv_obj_t *)obj, v);
}

static void selectorOverlayAnimXCb(void *obj, int32_t v) {
  lv_obj_set_x((lv_obj_t *)obj, v);
}

static void selectorSlideAnimReadyCb(lv_anim_t *) {
  if (selectorTransitionPending > 0) selectorTransitionPending--;
  if (selectorTransitionPending > 0) return;
  selectorTransitionActive = false;
  if (selectorOverlayHidden && !colorPaletteOpen) {
    cancelSelectorDemoTimer();
    selectorDemoTimer = lv_timer_create(selectorDemoTimerCb, cyd_ui::kSelectorDemoDelayMs, nullptr);
    lv_timer_set_repeat_count(selectorDemoTimer, 1);
  }
}

static void slideSelectorPart(lv_obj_t *obj, int32_t to, bool bounce, bool horizontal,
                              bool marksTransition) {
  if (!obj) return;
  // Dashboard data animation is useful while inspecting a stationary theme,
  // but it should never compete with selector chrome for the frame budget.
  holdUiUpdates(kSelectorSlideMs + 20);
  pauseDashboardAnimations(kSelectorSlideMs + 20);
  boostFrameRate(kSelectorSlideMs + 40);
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, obj);
  lv_anim_set_exec_cb(&a, horizontal ? selectorOverlayAnimXCb : selectorOverlayAnimCb);
  lv_anim_set_time(&a, kSelectorSlideMs);
  lv_anim_set_values(&a, horizontal ? lv_obj_get_x(obj) : lv_obj_get_y(obj), to);
  (void)bounce;  // retained at call sites; selector motion is now uniformly direct
  lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
  if (marksTransition) {
    selectorTransitionPending++;
    lv_anim_set_ready_cb(&a, selectorSlideAnimReadyCb);
  }
  lv_anim_start(&a);
}

// Every control leaves by its own edge: header up, colours down, steppers out
// to the sides, so the theme underneath is left completely clear.
static void selectorToggleCb(lv_event_t *) {
  if (!selectorOverlay) return;
  if (selectorTransitionActive) return;
  if (colorPaletteOpen) {
    closeSelectorColors();
    return;
  }
  cancelSelectorDemoTimer();
  const bool hiding = !selectorOverlayHidden;
  selectorTransitionActive = true;
  selectorTransitionPending = 0;
  slideSelectorPart(selectorTopBack, hiding ? -48 : 4, !hiding, false, true);
  slideSelectorPart(selectorTopInfo, hiding ? -48 : 2, !hiding, false, true);
  slideSelectorPart(selectorTopSave, hiding ? -48 : 4, !hiding, false, true);
  // The carrier stays still. Only the visible 232 px housing moves, avoiding
  // invalidation of a transparent 320 px-wide strip on every animation frame.
  const int housingHiddenY = 244 - selectorColorBarY;
  slideSelectorPart(selectorHousing, hiding ? housingHiddenY : 1, !hiding, false, true);
  slideSelectorPart(selectorArrowLeft, hiding ? -(kSelectorArrowW + 8) : kSelectorArrowInset, !hiding, true);
  slideSelectorPart(selectorArrowRight, hiding ? 328 : 320 - kSelectorArrowInset - kSelectorArrowW, !hiding,
                    true);
  selectorOverlayHidden = hiding;
}

static lv_obj_t *makeSelectorArrow(lv_obj_t *scr, int x, int y, bool pointsLeft, int id) {
  lv_obj_t *btn = lv_btn_create(scr);
  lv_obj_set_pos(btn, x, y);
  lv_obj_set_size(btn, kSelectorArrowW, kSelectorArrowH);
  lv_obj_set_style_radius(btn, 6, 0);
  lv_obj_set_style_bg_color(btn, cyd_ui::controlSurface(), 0);
  lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(btn, 1, 0);
  lv_obj_set_style_border_color(btn, cyd_ui::idleControlBorder(), 0);
  lv_obj_set_style_shadow_width(btn, 0, 0);
  applySelectorTouchFx(btn);
  lv_obj_add_event_cb(btn, buttonEventCb, LV_EVENT_CLICKED, (void *)(intptr_t)id);

  lv_obj_t *arrow = makeLineArrow(btn, pointsLeft, 21, 17);
  lv_obj_set_align(arrow, LV_ALIGN_CENTER);
  return btn;
}

static void makeSelectorArrows(lv_obj_t *scr) {
  // centred in the space between the header and the colour strip; both are a
  // fixed height, so the steppers sit still whatever the palette is doing
  const int bottomTop = 240 - kColorBarH;
  const int y = kSelectorOverlayH + (bottomTop - kSelectorOverlayH - kSelectorArrowH) / 2;
  selectorArrowLeft = makeSelectorArrow(scr, kSelectorArrowInset, y, true, BTN_PREV);
  selectorArrowRight = makeSelectorArrow(scr, 320 - kSelectorArrowInset - kSelectorArrowW, y, false, BTN_NEXT);
  if (colorPaletteOpen) {
    lv_obj_set_x(selectorArrowLeft, -(kSelectorArrowW + 8));
    lv_obj_set_x(selectorArrowRight, 328);
  }
}

static void defaultSwatchDrawCb(lv_event_t *e) {
  lv_obj_t *obj = lv_event_get_target(e);
  lv_area_t area;
  lv_obj_get_content_coords(obj, &area);
  const lv_coord_t cellW = max(1, lv_area_get_width(&area) / 3);
  const lv_coord_t cellH = max(1, lv_area_get_height(&area) / 2);
  lv_draw_rect_dsc_t dsc;
  lv_draw_rect_dsc_init(&dsc);
  dsc.bg_opa = LV_OPA_COVER;
  for (int row = 0; row < 2; row++) {
    for (int col = 0; col < 3; col++) {
      dsc.bg_color = ((row + col) & 1) ? c565(0x4208) : lv_color_black();
      lv_area_t cell = {(lv_coord_t)(area.x1 + col * cellW), (lv_coord_t)(area.y1 + row * cellH),
                        (lv_coord_t)(col == 2 ? area.x2 : area.x1 + (col + 1) * cellW - 1),
                        (lv_coord_t)(row == 1 ? area.y2 : area.y1 + (row + 1) * cellH - 1)};
      lv_draw_rect(lv_event_get_draw_ctx(e), &dsc, &cell);
    }
  }
}

// Color and preset changes rebuild the dashboard. Keep the tapped control in
// its pressed state for one visible beat first, so even a quick tap gets clear
// feedback before the more expensive preview refresh starts.
static lv_timer_t *selectorChoiceTimer = NULL;
static int pendingSelectorChoiceId = -1;

static void selectorChoiceTimerCb(lv_timer_t *) {
  selectorChoiceTimer = NULL;
  const int id = pendingSelectorChoiceId;
  pendingSelectorChoiceId = -1;
  if (currentAction && id >= 0) currentAction(id);
}

static void selectorChoiceEventCb(lv_event_t *e) {
  pendingSelectorChoiceId = (int)(intptr_t)lv_event_get_user_data(e);
  lv_obj_t *target = lv_event_get_target(e);
  lv_obj_add_state(target, LV_STATE_PRESSED);
  lv_obj_invalidate(target);
  if (selectorChoiceTimer) {
    lv_timer_reset(selectorChoiceTimer);
  } else {
    selectorChoiceTimer = lv_timer_create(selectorChoiceTimerCb, 65, NULL);
    lv_timer_set_repeat_count(selectorChoiceTimer, 1);
  }
}

static lv_obj_t *makePopupButton(lv_obj_t *parent, int x, int y, int w, int h, const char *label, bool active,
                                 int id) {
  lv_obj_t *btn = lv_btn_create(parent);
  lv_obj_set_pos(btn, x, y);
  lv_obj_set_size(btn, w, h);
  lv_obj_set_style_radius(btn, 5, 0);
  lv_obj_set_style_bg_color(btn, active ? cyd_ui::activeControlSurface() : cyd_ui::controlSurface(), 0);
  lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(btn, active ? 3 : cyd_ui::kBorderWidth, 0);
  lv_obj_set_style_border_color(btn, active ? cyd_ui::chromeAccent() : cyd_ui::idleControlBorder(), 0);
  lv_obj_set_style_shadow_width(btn, 0, 0);
  lv_obj_set_style_pad_all(btn, 0, 0);
  applySelectorTouchFx(btn);
  lv_obj_add_event_cb(btn, id >= BTN_BACKGROUND_STYLE_BASE && id < BTN_BACKGROUND_STYLE_BASE + 4
                              ? selectorChoiceEventCb
                              : buttonEventCb,
                      LV_EVENT_CLICKED, (void *)(intptr_t)id);
  lv_obj_t *text = lv_label_create(btn);
  lv_obj_set_style_text_font(text, &lv_font_rajdhani_12, 0);
  lv_obj_set_style_text_color(text, active ? cyd_ui::chromeAccent() : lv_color_white(), 0);
  lv_label_set_text(text, label);
  lv_obj_center(text);
  return btn;
}

static lv_obj_t *makePopupSwatch(lv_obj_t *parent, int x, int y, int w, int h, AccentTheme theme, bool active,
                                 int id) {
  lv_obj_t *swatch = lv_btn_create(parent);
  lv_obj_set_pos(swatch, x, y);
  lv_obj_set_size(swatch, w, h);
  lv_obj_set_style_radius(swatch, 4, 0);
  lv_obj_set_style_bg_color(swatch, theme == ACCENT_DEFAULT ? lv_color_black() : accentSwatchLv(theme), 0);
  lv_obj_set_style_bg_opa(swatch, LV_OPA_COVER, 0);
  lv_obj_set_style_shadow_width(swatch, 0, 0);
  lv_obj_set_style_border_width(swatch, active ? 3 : 1, 0);
  lv_obj_set_style_border_color(swatch, active ? lv_color_white() : c565(COLOR565_DIM), 0);
  applySelectorTouchFx(swatch);
  lv_obj_add_event_cb(swatch, selectorChoiceEventCb, LV_EVENT_CLICKED, (void *)(intptr_t)id);
  if (theme == ACCENT_DEFAULT) lv_obj_add_event_cb(swatch, defaultSwatchDrawCb, LV_EVENT_DRAW_MAIN, NULL);
  return swatch;
}

static constexpr int kCustomizerExplanationH = 21;
static constexpr int kCustomizerContentY = 34 + kCustomizerExplanationH;
static const AccentTheme kCustomizerColorOrder[ACCENT_COUNT] = {
    ACCENT_DEFAULT, ACCENT_WHITE, ACCENT_YELLOW, ACCENT_ORANGE, ACCENT_RED,
    ACCENT_MAGENTA, ACCENT_PURPLE, ACCENT_BLUE, ACCENT_CYAN, ACCENT_GREEN};

static lv_obj_t *makeBackgroundPreset(lv_obj_t *parent, int x, const char *label, int style, bool active) {
  lv_obj_t *btn = makePopupButton(parent, x, 94 + kCustomizerExplanationH, 52, 38, label, active,
                                  BTN_BACKGROUND_STYLE_BASE + style);
  lv_obj_t *preview = lv_obj_create(btn);
  lv_obj_remove_style_all(preview);
  makePassive(preview);
  lv_obj_set_pos(preview, 4, 3);
  lv_obj_set_size(preview, 44, 17);
  lv_obj_set_style_radius(preview, 2, 0);
  const lv_color_t color = gradientAccentLv();
  const lv_color_t flat = dashboardGradientTheme == ACCENT_DEFAULT ? lv_color_black() : color;
  if (style == 0) {
    lv_obj_set_style_bg_color(preview, flat, 0);
  } else if (style == 1 || style == 2) {
    const bool reverse = active && dashboardGradientReverse;
    lv_obj_set_style_bg_color(preview, reverse ? lv_color_black() : color, 0);
    lv_obj_set_style_bg_grad_color(preview, reverse ? color : lv_color_black(), 0);
    lv_obj_set_style_bg_grad_dir(preview, style == 1 ? LV_GRAD_DIR_VER : LV_GRAD_DIR_HOR, 0);
    // Keep a wider black end in the preview, matching the real background.
    lv_obj_set_style_bg_main_stop(preview, reverse ? 89 : 38, 0);
    lv_obj_set_style_bg_grad_stop(preview, reverse ? 217 : 166, 0);
  } else {
    const bool horizontal = !active || dashboardGradientHorizontal;
    const bool reverse = active && dashboardGradientReverse;
    const lv_color_t edge = reverse ? color : lv_color_black();
    const lv_color_t center = reverse ? lv_color_black() : color;
    lv_obj_set_style_bg_color(preview, edge, 0);
    lv_obj_set_style_bg_grad_color(preview, center, 0);
    lv_obj_set_style_bg_grad_dir(preview, horizontal ? LV_GRAD_DIR_HOR : LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_main_stop(preview, reverse ? 0 : 51, 0);
    lv_obj_set_style_bg_grad_stop(preview, reverse ? 96 : 255, 0);
    lv_obj_t *right = lv_obj_create(preview);
    lv_obj_remove_style_all(right);
    makePassive(right);
    lv_obj_set_pos(right, horizontal ? 22 : 0, horizontal ? 0 : 8);
    lv_obj_set_size(right, horizontal ? 22 : 44, horizontal ? 17 : 9);
    lv_obj_set_style_bg_color(right, center, 0);
    lv_obj_set_style_bg_grad_color(right, edge, 0);
    lv_obj_set_style_bg_grad_dir(right, horizontal ? LV_GRAD_DIR_HOR : LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_main_stop(right, reverse ? 159 : 0, 0);
    lv_obj_set_style_bg_grad_stop(right, reverse ? 255 : 204, 0);
    lv_obj_set_style_bg_opa(right, LV_OPA_COVER, 0);
  }
  lv_obj_set_style_bg_opa(preview, LV_OPA_COVER, 0);
  lv_obj_t *text = lv_obj_get_child(btn, 0);
  if (text) lv_obj_align(text, LV_ALIGN_BOTTOM_MID, 0, -2);
  return btn;
}

static void makeColorPopupTabs(lv_obj_t *popup) {
  makePopupButton(popup, 5, 4, 70, 24,
                  txt("THEME", "TEEMA", "DESIGN", "THÈME", "TEMA", "TEMA"), !gradientPanelOpen && !dataPanelOpen,
                  BTN_COLOR_TOGGLE);
  makePopupButton(popup, 81, 4, 90, 24,
                  txt("BACKGROUND", "TAUSTA", "HINTERGR.", "ARRIÈRE-PLAN", "FONDO", "SFONDO"),
                  gradientPanelOpen, BTN_GRADIENT_TOGGLE);
  makePopupButton(popup, 177, 4, 50, 24,
                  txt("DATA", "TIEDOT", "DATEN", "DONNÉES", "DATOS", "DATI"), dataPanelOpen, BTN_DATA_TAB);
}

static void makeCustomizerExplanation(lv_obj_t *popup) {
  char message[96];
  if (dataPanelOpen) {
    if (dataChoiceOpen) {
      snprintf(message, sizeof(message), "%s %s %s",
               txt("Choose which ride data is displayed in the", "Valitse mitä tietoa näytetään kohdassa",
                   "Wähle die Fahrdaten für", "Choisissez les données pour", "Elige los datos para",
                   "Scegli i dati per"),
               dashboardDataLabel(dashboardDataDefault(dashUiPreviewMode, dataSelectedSlot)),
               txt("section.", ".", "aus.", ".", ".", "."));
    } else if (displayPage == 1) {
      snprintf(message, sizeof(message), "%s",
               txt("Select a data section to customize.", "Valitse muokattava tietokenttä.",
                   "Wähle ein Datenfeld zum Anpassen.", "Sélectionnez un champ à modifier.",
                   "Selecciona un campo para personalizar.", "Seleziona un campo da modificare."));
    }
  } else if (gradientPanelOpen) {
    const AccentTheme saved = accentTheme;
    accentTheme = dashboardGradientTheme;
    snprintf(message, sizeof(message), "%s: %s",
             txt("Background color", "Taustaväri", "Hintergrundfarbe", "Couleur de fond", "Color de fondo",
                 "Colore sfondo"),
             accentName());
    accentTheme = saved;
  } else {
    snprintf(message, sizeof(message), "%s: %s",
             txt("Theme accent color", "Teeman korostusväri", "Theme-Akzentfarbe", "Couleur d'accent",
                 "Color de acento", "Colore principale"),
             accentName());
  }
  lv_obj_t *label = makeLabelAt(popup, 7, 33, message, c565(COLOR565_LABEL), &lv_font_rajdhani_12, 0);
  lv_obj_set_width(label, 218);
  lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
}

static void makeAppearanceSplit(lv_obj_t *popup) {
  lv_obj_t *housing = lv_obj_create(popup);
  lv_obj_remove_style_all(housing);
  // Match the BACKGROUND preset row exactly: same y, height, colors and
  // selected treatment. The housing clips only the two outside ends round;
  // all three button edges themselves stay square for flat internal joins.
  lv_obj_set_pos(housing, 6, 94 + kCustomizerExplanationH);
  lv_obj_set_size(housing, 220, 38);
  lv_obj_set_style_radius(housing, 5, 0);
  lv_obj_set_style_clip_corner(housing, true, 0);
  lv_obj_set_style_bg_color(housing, cyd_ui::controlSurface(), 0);
  lv_obj_set_style_bg_opa(housing, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(housing, 0, 0);
  lv_obj_set_style_pad_all(housing, 0, 0);
  lv_obj_clear_flag(housing, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *dark = makePopupButton(housing, 0, 0, 73, 38,
                                   txt("DARK", "TUMMA", "DUNKEL", "SOMBRE", "OSCURO", "SCURO"),
                                   dashboardAppearanceMode == DASH_APPEARANCE_DARK, BTN_THEME_DARK);
  lv_obj_t *light = makePopupButton(housing, 73, 0, 74, 38,
                                    txt("LIGHT", "VAALEA", "HELL", "CLAIR", "CLARO", "CHIARO"),
                                    dashboardAppearanceMode == DASH_APPEARANCE_LIGHT, BTN_THEME_LIGHT);
  lv_obj_t *automatic = makePopupButton(housing, 147, 0, 73, 38, "AUTO",
                                        dashboardAppearanceMode == DASH_APPEARANCE_AUTO, BTN_THEME_AUTO);
  lv_obj_set_style_radius(dark, 0, 0);
  lv_obj_set_style_radius(light, 0, 0);
  lv_obj_set_style_radius(automatic, 0, 0);

  for (int x : {73, 146}) {
    lv_obj_t *divider = lv_obj_create(housing);
    lv_obj_remove_style_all(divider);
    lv_obj_set_pos(divider, x, 4);
    lv_obj_set_size(divider, 1, 30);
    lv_obj_set_style_bg_color(divider, c565(COLOR565_DIM), 0);
    lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, 0);
    makePassive(divider);
  }
}

static void buildColorPopup(lv_obj_t *popup) {
  makeColorPopupTabs(popup);
  makeCustomizerExplanation(popup);
  const int swatchW = 40;
  for (int i = 0; i < ACCENT_COUNT; i++) {
    const int col = i % 5;
    const int row = i / 5;
    const AccentTheme theme = kCustomizerColorOrder[i];
    makePopupSwatch(popup, 6 + col * 44, kCustomizerContentY + row * 30, swatchW, 26, theme,
                    theme == accentTheme, BTN_COLOR_BASE + static_cast<int>(theme));
  }
  makeAppearanceSplit(popup);
}

static void buildGradientPopup(lv_obj_t *popup) {
  makeColorPopupTabs(popup);
  makeCustomizerExplanation(popup);
  const int swatchW = 40;
  for (int i = 0; i < ACCENT_COUNT; i++) {
    const int col = i % 5;
    const int row = i / 5;
    const AccentTheme theme = kCustomizerColorOrder[i];
    makePopupSwatch(popup, 6 + col * 44, kCustomizerContentY + row * 30, swatchW, 26, theme,
                    theme == dashboardGradientTheme, BTN_GRADIENT_COLOR_BASE + static_cast<int>(theme));
  }
  const int currentStyle = selectedBackgroundStyle();
  makeBackgroundPreset(popup, 6, "FLAT", 0, currentStyle == 0);
  makeBackgroundPreset(popup, 62, "FADE", 1, currentStyle == 1);
  makeBackgroundPreset(popup, 118, "SIDE", 2, currentStyle == 2);
  makeBackgroundPreset(popup, 174, "BELL", 3, currentStyle == 3);
}

static lv_obj_t *makeDataRow(lv_obj_t *parent, int y, const char *title, const char *value, bool active, int id) {
  lv_obj_t *btn = lv_btn_create(parent);
  lv_obj_set_pos(btn, 6, y);
  lv_obj_set_size(btn, 188, 29);
  lv_obj_set_style_radius(btn, 4, 0);
  lv_obj_set_style_bg_color(btn, active ? cyd_ui::activeControlSurface() : cyd_ui::controlSurface(), 0);
  lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(btn, active ? 3 : cyd_ui::kBorderWidth, 0);
  lv_obj_set_style_border_color(btn, active ? cyd_ui::chromeAccent() : cyd_ui::idleControlBorder(), 0);
  lv_obj_set_style_shadow_width(btn, 0, 0);
  lv_obj_set_style_pad_all(btn, 0, 0);
  applySelectorTouchFx(btn);
  lv_obj_add_event_cb(btn, buttonEventCb, LV_EVENT_CLICKED, (void *)(intptr_t)id);

  lv_obj_t *heading = makeLabelAt(btn, 5, 2, title, active ? cyd_ui::chromeAccent() : lv_color_white(),
                                  &lv_font_rajdhani_12, 0);
  lv_obj_set_width(heading, 176);
  lv_label_set_long_mode(heading, LV_LABEL_LONG_DOT);
  lv_obj_t *sub = makeLabelAt(btn, 5, 16, value, cyd_ui::secondaryText(), &lv_font_rajdhani_12, 0);
  lv_obj_set_width(sub, 176);
  lv_label_set_long_mode(sub, LV_LABEL_LONG_DOT);
  return btn;
}

static void makeDataPagingButtons(lv_obj_t *popup, int count, int offset) {
  lv_obj_t *up = makePopupButton(popup, 200, 35 + kCustomizerExplanationH, 27, 41, "^", false, BTN_DATA_UP);
  lv_obj_t *down = makePopupButton(popup, 200, 82 + kCustomizerExplanationH, 27, 41, "v", false,
                                   BTN_DATA_DOWN);
  if (offset <= 0) lv_obj_add_state(up, LV_STATE_DISABLED);
  if (offset >= max(0, count - 3)) lv_obj_add_state(down, LV_STATE_DISABLED);
}

static void buildDataPopup(lv_obj_t *popup) {
  makeColorPopupTabs(popup);
  makeCustomizerExplanation(popup);
  if (dataChoiceOpen) {
    char title[40];
    snprintf(title, sizeof(title), "< %s", dashboardDataLabel(dashboardDataDefault(dashUiPreviewMode, dataSelectedSlot)));
    makePopupButton(popup, 6, kCustomizerContentY, 188, 23, title, false, BTN_DATA_BACK);
    const DashboardDataItem selected = dashboardDataSelection(dashUiPreviewMode, dataSelectedSlot);
    for (int row = 0; row < 3; row++) {
      const int item = dataChoiceOffset + row;
      if (item >= DATA_COUNT) break;
      makePopupButton(popup, 6, 61 + kCustomizerExplanationH + row * 22, 188, 20,
                      dashboardDataLabel(static_cast<DashboardDataItem>(item)), item == selected,
                      BTN_DATA_CHOICE_BASE + item);
    }
    makeDataPagingButtons(popup, DATA_COUNT, dataChoiceOffset);
    return;
  }

  const uint8_t count = dashboardDataSlotCount(dashUiPreviewMode);
  if (count == 0) {
    lv_obj_t *empty = makeLabelAt(
        popup, 116, 78 + kCustomizerExplanationH,
        txt("NO DATA SLOTS", "EI TIETOKENTTIÄ", "KEINE DATENFELDER", "AUCUNE DONNÉE", "SIN CAMPOS DE DATOS",
            "NESSUN CAMPO DATI"),
        c565(COLOR565_LABEL), &lv_font_rajdhani_12, 1);
    lv_obj_set_width(empty, 210);
    lv_obj_set_x(empty, 11);
    lv_obj_set_style_text_align(empty, LV_TEXT_ALIGN_CENTER, 0);
    return;
  }
  for (int row = 0; row < 3; row++) {
    const int slot = dataListOffset + row;
    if (slot >= count) break;
    makeDataRow(popup, kCustomizerContentY + row * 31,
                dashboardDataLabel(dashboardDataDefault(dashUiPreviewMode, slot)),
                dashboardDataLabel(dashboardDataSelection(dashUiPreviewMode, slot)), false,
                BTN_DATA_SLOT_BASE + slot);
  }
  makeDataPagingButtons(popup, count, dataListOffset);
}

static void buildAutoBrightnessPrompt(lv_obj_t *popup) {
  lv_obj_t *shade = lv_obj_create(popup);
  lv_obj_remove_style_all(shade);
  lv_obj_set_pos(shade, 0, 0);
  lv_obj_set_size(shade, 232, 162);
  lv_obj_set_style_bg_color(shade, c565(0x0841), 0);
  lv_obj_set_style_bg_opa(shade, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(shade, 1, 0);
  lv_obj_set_style_border_color(shade, c565(0xFB40), 0);
  lv_obj_set_style_radius(shade, 6, 0);
  lv_obj_add_flag(shade, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(shade, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t *title = makeLabelAt(shade, 116, 18,
                                txt("ENABLE AUTO BRIGHTNESS?", "OTA AUTOM. KIRKKAUS KÄYTTÖÖN?",
                                    "AUTO-HELLIGKEIT AKTIVIEREN?", "ACTIVER LA LUMINOSITÉ AUTO ?",
                                    "¿ACTIVAR BRILLO AUTOMÁTICO?", "ATTIVARE LUMINOSITÀ AUTO?"),
                                lv_color_white(), &lv_font_rajdhani_12, 1);
  lv_obj_set_width(title, 214);
  lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_t *body = makeLabelAt(
      shade, 116, 55,
      txt("Auto appearance uses the light sensor. Auto brightness can use the same sensor too.",
          "Automaattinen ulkoasu käyttää valoanturia. Myös kirkkaus voi seurata sitä.",
          "Auto-Darstellung nutzt den Lichtsensor. Die Helligkeit kann ihm ebenfalls folgen.",
          "L'apparence auto utilise le capteur. La luminosité peut aussi le suivre.",
          "La apariencia automática usa el sensor. El brillo también puede seguirlo.",
          "L'aspetto automatico usa il sensore. Anche la luminosità può seguirlo."),
      c565(COLOR565_LABEL), &lv_font_rajdhani_12, 1);
  // Two sentences: they need the full height between the title and the buttons.
  wrapLabel(body, 12, 42, 208);
  makePopupButton(shade, 12, 119, 98, 32, txt("NOT NOW", "EI NYT", "NICHT JETZT", "PAS MAINT.", "AHORA NO", "NON ORA"),
                  false, BTN_AUTO_BRIGHTNESS_NO);
  makePopupButton(shade, 122, 119, 98, 32, txt("ENABLE", "OTA KÄYTTÖÖN", "AKTIVIEREN", "ACTIVER", "ACTIVAR", "ATTIVA"),
                  true, BTN_AUTO_BRIGHTNESS_YES);
}

static void rebuildSelectorPopup() {
  if (selectorPopup) {
    lv_obj_del(selectorPopup);
    selectorPopup = NULL;
    selectorPalette = NULL;
  }
  if (!selectorHousing || !selectorBottomRow) return;

  const bool open = colorPaletteOpen;
  const int housingH = open ? 184 + kCustomizerExplanationH : 42;
  selectorColorBarY = 240 - (housingH + 2);
  lv_obj_set_y(selectorColorBar, selectorColorBarY);
  lv_obj_set_height(selectorColorBar, housingH + 2);
  lv_obj_set_height(selectorHousing, housingH);
  lv_obj_set_y(selectorBottomRow, housingH - 42);
  lv_obj_set_style_bg_opa(selectorHousing, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(selectorHousing, 1, 0);

  // When collapsed, both actions read as distinct buttons inside the shared
  // housing. In the expanded customizer, CUSTOMIZE THEME becomes the panel's
  // active bottom area while RESET THEME remains visibly nested.
  lv_obj_t *master = lv_obj_get_child(selectorBottomRow, 0);
  if (master) {
    lv_obj_set_pos(master, open ? 1 : 0, open ? 3 : 0);
    lv_obj_set_size(master, open ? 144 : 150, open ? 36 : 42);
    lv_obj_set_style_radius(master, open ? 6 : 0, 0);
    lv_obj_set_style_border_width(master, open ? 0 : 1, 0);
    lv_obj_t *title = lv_obj_get_child(master, 0);
    lv_obj_t *hint = lv_obj_get_child(master, 1);
    if (title) lv_obj_set_y(title, open ? 4 : 7);
    if (hint) lv_obj_set_y(hint, open ? 20 : 23);
  }
  lv_obj_t *reset = lv_obj_get_child(selectorBottomRow, 1);
  // RESET THEME remains a visibly separate action even inside the expanded
  // customizer housing.
  if (reset) {
    lv_obj_set_pos(reset, 149, open ? 3 : 0);
    lv_obj_set_size(reset, open ? 82 : 83, open ? 36 : 42);
    lv_obj_set_style_radius(reset, open ? 6 : 0, 0);
    lv_obj_set_style_border_width(reset, 1, 0);
    lv_obj_t *title = lv_obj_get_child(reset, 0);
    lv_obj_t *hint = lv_obj_get_child(reset, 1);
    if (title) lv_obj_set_y(title, open ? 4 : 7);
    if (hint) lv_obj_set_y(hint, open ? 20 : 23);
  }

  lv_obj_set_style_border_color(selectorHousing,
                                open ? cyd_ui::chromeAccent() : cyd_ui::idleControlBorder(), 0);

  if (!open) return;

  // This is only a disposable content group. The housing supplies the one
  // shared background and border around both the menu and its bottom button.
  selectorPopup = lv_obj_create(selectorHousing);
  lv_obj_remove_style_all(selectorPopup);
  lv_obj_set_pos(selectorPopup, 0, 0);
  lv_obj_set_size(selectorPopup, 232, housingH - 43);
  lv_obj_clear_flag(selectorPopup, LV_OBJ_FLAG_SCROLLABLE);
  selectorPalette = NULL;
  if (dataPanelOpen)
    buildDataPopup(selectorPopup);
  else if (gradientPanelOpen)
    buildGradientPopup(selectorPopup);
  else
    buildColorPopup(selectorPopup);

  if (autoBrightnessPromptOpen) buildAutoBrightnessPrompt(selectorPopup);

}

static void animateSelectorChrome(bool hide) {
  cancelSelectorDemoTimer();
  selectorTransitionActive = true;
  selectorTransitionPending = 0;
  slideSelectorPart(selectorTopBack, hide ? -48 : 4, !hide, false, true);
  slideSelectorPart(selectorTopInfo, hide ? -48 : 2, !hide, false, true);
  slideSelectorPart(selectorTopSave, hide ? -48 : 4, !hide, false, true);
  slideSelectorPart(selectorArrowLeft, hide ? -(kSelectorArrowW + 8) : 3, !hide, true);
  slideSelectorPart(selectorArrowRight, hide ? 328 : 279, !hide, true);
}

static void toggleColorsMaster() {
  if (colorPaletteOpen) {
    closeSelectorColors();
    return;
  }
  colorPaletteOpen = true;
  gradientPanelOpen = false;
  dataPanelOpen = false;
  dataChoiceOpen = false;
  animateSelectorChrome(true);
  rebuildSelectorPopup();
}

static void closeSelectorColors() {
  if (!colorPaletteOpen) return;
  colorPaletteOpen = false;
  autoBrightnessPromptOpen = false;
  dataChoiceOpen = false;
  rebuildSelectorPopup();  // the panel itself collapses immediately
  animateSelectorChrome(false);
}

static void toggleSelectorPalette() {
  const bool wasOpen = colorPaletteOpen;
  colorPaletteOpen = true;
  gradientPanelOpen = false;
  dataPanelOpen = false;
  dataChoiceOpen = false;
  autoBrightnessPromptOpen = false;
  if (!wasOpen) animateSelectorChrome(true);
  rebuildSelectorPopup();
}

static void openSelectorGradientPanel() {
  const bool wasOpen = colorPaletteOpen;
  colorPaletteOpen = true;
  gradientPanelOpen = true;
  dataPanelOpen = false;
  dataChoiceOpen = false;
  autoBrightnessPromptOpen = false;
  if (!wasOpen) animateSelectorChrome(true);
  rebuildSelectorPopup();
}

static void openSelectorDataPanel() {
  const bool wasOpen = colorPaletteOpen;
  colorPaletteOpen = true;
  gradientPanelOpen = false;
  dataPanelOpen = true;
  dataChoiceOpen = false;
  autoBrightnessPromptOpen = false;
  dataListOffset = 0;
  if (!wasOpen) animateSelectorChrome(true);
  rebuildSelectorPopup();
}

static void makeColorBar(lv_obj_t *scr) {
  selectorScreen = scr;
  selectorColorBarY = 240 - kColorBarH;

  lv_obj_t *bar = lv_obj_create(scr);
  lv_obj_remove_style_all(bar);
  lv_obj_clear_flag(bar, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
  lv_obj_set_pos(bar, 0, selectorColorBarY);
  lv_obj_set_size(bar, 320, kColorBarH);

  selectorColorBar = bar;
  selectorPalette = NULL;

  selectorHousing = lv_obj_create(bar);
  lv_obj_set_pos(selectorHousing, 44, 1);
  lv_obj_set_size(selectorHousing, 232, 42);
  lv_obj_set_style_radius(selectorHousing, 7, 0);
  lv_obj_set_style_bg_color(selectorHousing, cyd_ui::controlSurface(), 0);
  lv_obj_set_style_bg_opa(selectorHousing, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(selectorHousing, 1, 0);
  lv_obj_set_style_border_color(selectorHousing, cyd_ui::idleControlBorder(), 0);
  lv_obj_set_style_clip_corner(selectorHousing, true, 0);
  lv_obj_set_style_pad_all(selectorHousing, 0, 0);
  lv_obj_clear_flag(selectorHousing, LV_OBJ_FLAG_SCROLLABLE);

  selectorBottomRow = lv_obj_create(selectorHousing);
  lv_obj_remove_style_all(selectorBottomRow);
  lv_obj_set_pos(selectorBottomRow, 0, 0);
  lv_obj_set_size(selectorBottomRow, 232, 42);
  lv_obj_clear_flag(selectorBottomRow, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *master = lv_btn_create(selectorBottomRow);
  lv_obj_set_pos(master, 1, 3);
  lv_obj_set_size(master, 144, 36);
  lv_obj_set_style_radius(master, 6, 0);
  lv_obj_set_style_bg_color(master, cyd_ui::controlSurface(), 0);
  lv_obj_set_style_bg_opa(master, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(master, 1, 0);
  lv_obj_set_style_border_color(master, cyd_ui::idleControlBorder(), 0);
  lv_obj_set_style_shadow_width(master, 0, 0);
  lv_obj_set_style_pad_all(master, 0, 0);
  applySelectorTouchFx(master);
  lv_obj_add_event_cb(master, buttonEventCb, LV_EVENT_CLICKED, (void *)(intptr_t)BTN_COLORS_MASTER);

  makeLabelAt(master, 7, 4,
              txt("CUSTOMIZE THEME", "MUOKKAA TEEMAA", "DESIGN ANPASSEN", "PERSONNALISER", "PERSONALIZAR TEMA",
                  "PERSONALIZZA TEMA"),
              lv_color_white(), &lv_font_rajdhani_12, 0);
  lv_obj_t *hint = makeLabelAt(
      master, 7, 20,
      txt("Colors, background & data", "Värit, tausta ja tiedot", "Farben, Hintergrund & Daten",
          "Couleurs, fond et données", "Colores, fondo y datos", "Colori, sfondo e dati"),
      c565(COLOR565_LABEL), &lv_font_rajdhani_12, 0);
  lv_obj_set_width(hint, 132);
  lv_label_set_long_mode(hint, LV_LABEL_LONG_DOT);

  lv_obj_t *reset = lv_btn_create(selectorBottomRow);
  lv_obj_set_pos(reset, 149, 3);
  lv_obj_set_size(reset, 82, 36);
  lv_obj_set_style_radius(reset, 6, 0);
  lv_obj_set_style_bg_color(reset, cyd_ui::controlSurface(), 0);
  lv_obj_set_style_bg_opa(reset, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(reset, 1, 0);
  lv_obj_set_style_border_color(reset, cyd_ui::idleControlBorder(), 0);
  lv_obj_set_style_shadow_width(reset, 0, 0);
  lv_obj_set_style_pad_all(reset, 0, 0);
  applySelectorTouchFx(reset);
  lv_obj_add_event_cb(reset, buttonEventCb, LV_EVENT_CLICKED, (void *)(intptr_t)BTN_RESET_CURRENT_UI);
  // Both labels are pinned to the 82 px button, so the longest translations are
  // kept short enough to fit rather than spilling over the neighbouring
  // control; LONG_DOT keeps any future overrun inside the button.
  lv_obj_t *resetTitle = makeLabelAt(reset, 0, 4,
                                     txt("RESET THEME", "NOLLAA TEEMA", "DESIGN-RESET", "RÉINIT. THÈME", "REINIC. TEMA",
                                         "RESET TEMA"),
                                     lv_color_white(), &lv_font_rajdhani_12, 0);
  lv_obj_set_width(resetTitle, 82);
  lv_label_set_long_mode(resetTitle, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_align(resetTitle, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_t *resetHint = makeLabelAt(
      reset, 0, 20, txt("Defaults", "Oletus", "Standard", "Défaut", "Por defecto", "Predefinito"),
      c565(COLOR565_LABEL), &lv_font_rajdhani_12, 0);
  lv_obj_set_width(resetHint, 82);
  lv_label_set_long_mode(resetHint, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_align(resetHint, LV_TEXT_ALIGN_CENTER, 0);

  rebuildSelectorPopup();
}

static void makeSelectorOverlay(lv_obj_t *scr, const char *title, uint8_t itemIndex, uint8_t itemCount,
                                const char *itemName, const char *saveText = NULL) {
  lv_obj_t *ov = lv_obj_create(scr);
  lv_obj_remove_style_all(ov);
  lv_obj_clear_flag(ov, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
  lv_obj_set_pos(ov, 0, 0);
  lv_obj_set_size(ov, 320, kSelectorOverlayH);

  char itemText[48];
  snprintf(itemText, sizeof(itemText), "%u/%u - %s", itemIndex, itemCount, itemName);
  lv_point_t titleSize;
  lv_point_t itemSize;
  lv_txt_get_size(&titleSize, title, &lv_font_rajdhani_14, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  lv_txt_get_size(&itemSize, itemText, &lv_font_rajdhani_12, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  const int infoW = constrain(max(titleSize.x, itemSize.x) + 16, 72, 144);

  // The actionable buttons already carry their own dark surfaces. Keep the
  // parent transparent and fit the passive centre backdrop to its translated
  // text, with eight pixels of breathing room on each side.
  lv_obj_t *infoBackdrop = lv_obj_create(ov);
  lv_obj_remove_style_all(infoBackdrop);
  makePassive(infoBackdrop);
  lv_obj_set_pos(infoBackdrop, 160 - infoW / 2, 2);
  lv_obj_set_size(infoBackdrop, infoW, 40);
  lv_obj_set_style_radius(infoBackdrop, 6, 0);
  lv_obj_set_style_bg_color(infoBackdrop, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(infoBackdrop, LV_OPA_90, 0);
  selectorTopInfo = infoBackdrop;

  // layout: < BACK | header | SAVE — stepping between themes moved to the big
  // side arrows, which leaves the header the whole middle
  lv_obj_t *back = makeNavButton(
      ov, kMenuEdgeX, 4, txt("< BACK", "< TAKAISIN", "< ZURÜCK", "< RETOUR", "< ATRÁS", "< INDIETRO"), BTN_BACK,
      kTopBarButtonH);
  lv_obj_set_size(back, kWideTopNavW, kTopBarButtonH);
  applySelectorTouchFx(back);
  selectorTopBack = back;
  makeHeaderBox(infoBackdrop, title, itemText, 0, infoW);
  const bool showSaved = !saveText && selectorSavedFeedback;
  lv_obj_t *save = makeNavButton(
      ov, kWideTopNavRightX, 4,
      saveText ? saveText
               : (showSaved ? txt("SAVED", "TALLEN.", "GESPEICH.", "ENREG.", "GUARDADO", "SALVATO")
                            : txt("SAVE", "TALLENNA", "SPEICHERN", "SAUVER", "GUARDAR", "SALVA")),
      BTN_SAVE,
      kTopBarButtonH);
  lv_obj_set_size(save, kWideTopNavW, kTopBarButtonH);
  applySelectorTouchFx(save);
  selectorTopSave = save;
  if (showSaved) {
    lv_obj_t *badge = lv_obj_create(save);
    lv_obj_remove_style_all(badge);
    makePassive(badge);
    lv_obj_set_size(badge, 15, 15);
    lv_obj_align(badge, LV_ALIGN_RIGHT_MID, -4, 0);
    lv_obj_add_event_cb(badge, selectedBadgeDrawCb, LV_EVENT_DRAW_MAIN, NULL);
  }

  selectorOverlay = ov;
  selectorOverlayHidden = false;
  if (colorPaletteOpen) {
    lv_obj_set_y(selectorTopBack, -48);
    lv_obj_set_y(selectorTopInfo, -48);
    lv_obj_set_y(selectorTopSave, -48);
  }

  // tapping the preview area toggles the overlay
  lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(scr, pressHoldCb, LV_EVENT_PRESSED, NULL);
  lv_obj_add_event_cb(scr, selectorToggleCb, LV_EVENT_CLICKED, NULL);
}

static void showDashUiSelector(lv_obj_t *scr, const char *title, uint8_t itemIndex, uint8_t itemCount,
                               const char *itemName, const char *saveText = NULL) {
  makeDashUiPreview(scr);

  // Each control provides its own contrast; keep the dashboard unobscured.
  selectorTransitionActive = false;

  makeSelectorOverlay(scr, title, itemIndex, itemCount, itemName, saveText);
  makeColorBar(scr);
  makeSelectorArrows(scr);
}

static void selectedBadgeDrawCb(lv_event_t *e) {
  lv_obj_t *obj = lv_event_get_target(e);
  lv_area_t area;
  lv_obj_get_content_coords(obj, &area);

  lv_draw_rect_dsc_t circle;
  lv_draw_rect_dsc_init(&circle);
  circle.bg_color = c565(0xFB40);
  circle.bg_opa = LV_OPA_COVER;
  circle.radius = LV_RADIUS_CIRCLE;
  lv_draw_rect(lv_event_get_draw_ctx(e), &circle, &area);

  lv_draw_line_dsc_t check;
  lv_draw_line_dsc_init(&check);
  check.color = lv_color_black();
  check.width = 2;
  check.round_start = true;
  check.round_end = true;
  lv_point_t a = {(lv_coord_t)(area.x1 + 3), (lv_coord_t)(area.y1 + 8)};
  lv_point_t b = {(lv_coord_t)(area.x1 + 6), (lv_coord_t)(area.y1 + 11)};
  lv_point_t c = {(lv_coord_t)(area.x1 + 12), (lv_coord_t)(area.y1 + 4)};
  lv_draw_line(lv_event_get_draw_ctx(e), &check, &a, &b);
  lv_draw_line(lv_event_get_draw_ctx(e), &check, &b, &c);
}

static void captionFadeDrawCb(lv_event_t *e) {
  lv_obj_t *obj = lv_event_get_target(e);
  lv_area_t area;
  lv_obj_get_content_coords(obj, &area);
  const int width = lv_area_get_width(&area);
  const int strips = 12;
  for (int i = 0; i < strips; i++) {
    lv_area_t strip = {(lv_coord_t)(area.x1 + width * i / strips), area.y1,
                       (lv_coord_t)(area.x1 + width * (i + 1) / strips - 1), area.y2};
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = lv_color_black();
    const int distance = abs((2 * i + 1) - strips);
    const int strength = strips - distance;
    dsc.bg_opa = (lv_opa_t)(LV_OPA_30 + (LV_OPA_90 - LV_OPA_30) * strength / strips);
    lv_draw_rect(lv_event_get_draw_ctx(e), &dsc, &strip);
  }
}

static void showDashUiGrid(lv_obj_t *scr, const char *title) {
  makeMenuTopBar(scr, title, dashUiGridPage + 1, kDashGridPageCount, true);

  lv_obj_t *grid = lv_obj_create(scr);
  lv_obj_set_pos(grid, 0, 45);
  lv_obj_set_size(grid, 320, 195);
  lv_obj_set_style_radius(grid, 0, 0);
  lv_obj_set_style_bg_color(grid, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(grid, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(grid, 0, 0);
  lv_obj_set_style_pad_all(grid, 0, 0);
  lv_obj_set_style_pad_row(grid, 0, 0);
  lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);

  // Fill the content width with generous resistive-touch targets while keeping
  // the 124x93 dashboard captures at their native embedded size and centred
  // beneath the compact title overlay.
  constexpr int cardW = 158;
  constexpr int cardH = 94;
  // Retain the approved equal three-pixel row and column gaps.
  constexpr int colStep = 161;
  constexpr int rowStep = 97;
  constexpr int cardX = 0;
  constexpr int cardY = 2;
  const int first = dashUiGridPage * kDashGridPageSize;
  const int last = LV_MIN(first + kDashGridPageSize, LV_MIN(kDashThemeCount, CYD_DASH_THUMB_COUNT));
  for (int displayIndex = first; displayIndex < last; displayIndex++) {
    const int themeIndex = dashGridThemeAt(displayIndex);
    const int slot = displayIndex - first;
    const int col = slot % 2;
    const int row = slot / 2;
    const int x = cardX + col * colStep;
    const int y = cardY + row * rowStep;
    const bool selected = themeIndex == static_cast<int>(dashUiPreviewMode);

    // A neutral frame keeps the browser quiet; orange is reserved for the one
    // theme that is actually selected. The direct-color preview sits between
    // this passive background and the transparent button hit target.
    const lv_color_t cardBorder = selected ? cyd_ui::chromeAccent() : cyd_ui::idleControlBorder();
    // Extend the captured dashboard's own background through the otherwise
    // empty card margins. The generator records each thumbnail's canvas colour
    // (its top-left pixel), which keeps Tiles' light canvas, Pixel's near-black
    // canvas, and the dark themes in sync with the preview assets without
    // another per-theme colour table.
    const uint16_t thumbBg565 = cydDashThumbBackground565[themeIndex];
    // The single-dial Gauge tile intentionally uses a true-black browser
    // surface. Its dashboard remains independently customizable; this applies
    // only to the preview grid card.
    const lv_color_t cardSurface = themeIndex == MODE_MINIMAL ? lv_color_black() : c565(thumbBg565);
    makePanel(grid, x, y, cardW, cardH, 4, cardBorder, cardSurface, true);
    lv_obj_t *preview = lv_img_create(grid);
    makePassive(preview);
    lv_img_set_src(preview, cydDashThumbs[themeIndex]);
    lv_obj_set_pos(preview, x + (cardW - CYD_DASH_THUMB_WIDTH) / 2,
                   y + (cardH - CYD_DASH_THUMB_HEIGHT) / 2);

    lv_obj_t *card = lv_btn_create(grid);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_size(card, cardW, cardH);
    lv_obj_set_style_radius(card, 4, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(card, selected ? 3 : 1, 0);
    lv_obj_set_style_border_color(card, cardBorder, 0);
    lv_obj_set_style_shadow_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    applyButtonTouchFx(card);
    lv_obj_add_event_cb(card, buttonEventCb, LV_EVENT_CLICKED,
                        (void *)(intptr_t)(BTN_DASH_GRID_BASE + themeIndex));

    lv_point_t captionSize;
    const char *captionText = modeName(static_cast<DashboardMode>(themeIndex));
    lv_txt_get_size(&captionSize, captionText, &lv_font_rajdhani_14, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    const int captionW = LV_MIN(selected ? cardW - 25 : cardW - 8, captionSize.x + 5);
    constexpr int captionX = 5;
    const int fadeW = LV_MIN(cardW - 4, captionW + 22);

    lv_obj_t *captionFade = lv_obj_create(card);
    lv_obj_remove_style_all(captionFade);
    makePassive(captionFade);
    lv_obj_set_size(captionFade, fadeW, 19);
    lv_obj_set_pos(captionFade, 2, 1);
    lv_obj_add_event_cb(captionFade, captionFadeDrawCb, LV_EVENT_DRAW_MAIN, NULL);

    // A one-pixel black copy keeps white names readable over both dark and
    // light dashboards without hiding the preview's top status row in a box.
    lv_obj_t *captionShadow = lv_label_create(card);
    makePassive(captionShadow);
    lv_obj_set_style_text_font(captionShadow, &lv_font_rajdhani_14, 0);
    lv_obj_set_style_text_color(captionShadow, lv_color_black(), 0);
    lv_obj_set_width(captionShadow, captionW);
    lv_obj_set_style_text_align(captionShadow, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_long_mode(captionShadow, LV_LABEL_LONG_DOT);
    lv_label_set_text(captionShadow, captionText);
    lv_obj_set_pos(captionShadow, captionX + 1, 3);

    lv_obj_t *caption = lv_label_create(card);
    makePassive(caption);
    lv_obj_set_style_text_font(caption, &lv_font_rajdhani_14, 0);
    lv_obj_set_style_text_color(caption, lv_color_white(), 0);
    lv_obj_set_width(caption, captionW);
    lv_obj_set_style_text_align(caption, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_long_mode(caption, LV_LABEL_LONG_DOT);
    lv_label_set_text(caption, captionText);
    lv_obj_set_pos(caption, captionX, 2);

    if (selected) {
      lv_obj_t *badge = lv_obj_create(card);
      lv_obj_remove_style_all(badge);
      makePassive(badge);
      lv_obj_set_size(badge, 15, 15);
      lv_obj_align(badge, LV_ALIGN_TOP_RIGHT, -3, 2);
      lv_obj_add_event_cb(badge, selectedBadgeDrawCb, LV_EVENT_DRAW_MAIN, NULL);
    }
  }

}

static const char *vehicleSectionTitle(SubmenuType type) {
  switch (type) {
    case SUBMENU_CONNECTION: return txt("CONNECTION", "YHTEYS", "VERBINDUNG", "CONNEXION", "CONEXIÓN", "CONNESSIONE");
    case SUBMENU_POWER_LIMITS: return txt("POWER & CURRENT", "TEHO JA VIRTA", "LEISTUNG / STROM", "PUISSANCE", "POTENCIA", "POTENZA");
    case SUBMENU_SPEED: return txt("RIDE MODES", "AJOTILAT", "FAHRMODI", "MODES", "MODOS", "MODALITÀ");
    case SUBMENU_SPEED_CALIBRATION: return txt("CALIBRATION", "KALIBROINTI", "KALIBRIERUNG", "ÉTALONNAGE", "CALIBRACIÓN", "CALIBRAZIONE");
    case SUBMENU_GAUGE_RANGES: return txt("GAUGE RANGES", "MITTARIASTEIKOT", "ANZEIGESKALEN", "ÉCHELLES", "ESCALAS", "SCALE");
    default: return txt("PACK SETUP", "AKUN ASETUKSET", "AKKU-EINSTELL.", "RÉGLAGES BATTERIE", "AJUSTES BATERÍA", "IMPOSTAZIONI BATTERIA");
  }
}

static void makeVehicleFields(lv_obj_t *scr, const uint8_t *source, uint8_t count, int y = 68) {
  uint8_t fields[VEHICLE_FIELD_COUNT];
  count = collectVisibleFields(source, count, fields);
  for (uint8_t i = 0; i < count; ++i) {
    char value[32]; vehicleFieldValue(fields[i], value, sizeof(value));
    makeMenuButton(scr, i % 2 ? kTwoColRightX : kMenuEdgeX, y + (i / 2) * 58,
                   i % 2 ? kTwoColRightW : kTwoColLeftW, 54,
                   vehicleFieldTitle(fields[i]), value, false, BTN_OPTION_BASE + fields[i]);
  }
}

static void makeVehicleExplanation(lv_obj_t *scr, const char *text) {
  lv_obj_t *label = makeLabelAt(scr, kMenuEdgeX + 8, 58, text, cyd_ui::secondaryText(), &lv_font_rajdhani_14, 0);
  lv_obj_set_width(label, kMenuContentW - 16);
  lv_obj_set_height(label, LV_SIZE_CONTENT);
  lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
}

static void showSubmenu() {
  rideModeReadout = NULL;
  lv_obj_t *scr = makeScreen();
  currentAction = submenuAction;

  const char *title = txt("SETTING", "ASETUS", "EINSTELL.", "RÉGLAGE", "AJUSTE", "IMPOSTAZ.");
  if (submenuType == SUBMENU_DASH_UI) title = txt("THEME", "TEEMA", "DESIGN", "THÈME", "TEMA", "TEMA");
  else if (submenuType == SUBMENU_LANGUAGE) title = txt("LANGUAGE", "KIELI", "SPRACHE", "LANGUE", "IDIOMA", "LINGUA");
  else if (submenuType == SUBMENU_UNITS) title = txt("UNITS", "YKSIKÖT", "EINHEIT.", "UNITÉS", "UNIDADES", "UNITÀ");
  else if (submenuType == SUBMENU_VESC)
    title = txt("VEHICLE INFO", "AJONEUVOTIEDOT", "FAHRZEUG-INFO", "INFO VÉHICULE", "INFO VEHÍCULO",
                "INFO VEICOLO");
  else if (submenuType == SUBMENU_CONTROLLER_CONFIG)
    title = txt("CONTROLLER CONFIG", "OHJAINASETUKSET", "CONTROLLER-KONFIG.", "CONFIG. CONTRÔLEUR",
                "CONFIG. CONTROLADOR", "CONFIG. CONTROLLER");
  else if (submenuType == SUBMENU_SPEED)
    title = txt("SPEEDS", "NOPEUDET", "TEMPO", "VITESSES", "VELOCIDADES", "VELOCITÀ");
  else if (submenuType == SUBMENU_PIN) title = "PIN";
  else if (submenuType == SUBMENU_LOGGING)
    title = txt("LOGGING", "LOKI", "PROTOKOLL", "JOURNAL", "REGISTRO", "REGISTRO");
  else if (submenuType == SUBMENU_DISPLAY) title = txt("DISPLAY", "NÄYTTÖ", "DISPLAY", "ÉCRAN", "PANTALLA", "SCHERMO");
  else if (submenuType == SUBMENU_DISPLAY_INFO)
    title = txt("INFORMATION", "TIEDOT", "INFORMATION", "INFORMATIONS", "INFORMACIÓN", "INFORMAZIONI");
  else if (submenuType == SUBMENU_PANEL_COLORS)
    title = txt("DISPLAY PANEL", "NÄYTTÖPANEELI", "DISPLAY-PANEL", "DALLE D'AFFICHAGE", "PANEL DE PANTALLA",
                "PANNELLO DISPLAY");
  else if (submenuType == SUBMENU_CHANGELOG)
    title = txt("CHANGELOG", "MUUTOSLOKI", "ÄNDERUNGEN", "JOURNAL", "CAMBIOS", "MODIFICHE");
  else if (submenuType == SUBMENU_AUTO_RETURN)
    title = txt("AUTO RETURN", "AUTOMAATTIPALUU", "AUTO-RÜCKKEHR", "RETOUR AUTO", "RETORNO AUTO", "RITORNO AUTO");
  else if (submenuType == SUBMENU_RESET) title = txt("RESET", "NOLLAA", "RESET", "RESET", "REINICIAR", "RESET");
  else if (submenuType == SUBMENU_BATTERY) title = metricBatteryLabel();
  else if (submenuType == SUBMENU_CONTROLLER_TYPE)
    title = txt("CONTROLLER SETUP", "OHJAIMEN MÄÄRITYS", "CONTROLLER-SETUP", "CONFIG. CONTRÔLEUR",
                "CONFIG. CONTROLADOR", "CONFIG. CONTROLLER");
  else if (submenuType == SUBMENU_CONFIGURATOR)
    title = txt("NEW USER SETUP", "KÄYTTÖÖNOTTO", "ERSTEINRICHT.", "CONFIG INIT.", "CONFIG INICIAL",
                "CONFIG INIZ.");

  if (submenuType == SUBMENU_DEMO) {
    makeMenuTopBar(scr, txt("DEMO MODE", "DEMOTILA", "DEMO-MODUS", "MODE DÉMO", "MODO DEMO", "MODALITÀ DEMO"), 1, 1, false);
    char rate[40];
    snprintf(rate, sizeof(rate), "%ux (1 s = %u s)", demoTimeScale, demoTimeScale);
    makeMenuButton(scr, kMenuEdgeX, 48, kMenuContentW, 56,
                   txt("DEMO MODE", "DEMOTILA", "DEMO-MODUS", "MODE DÉMO", "MODO DEMO", "MODALITÀ DEMO"),
                   dashboardDemoModeEnabled ? txt("Enabled", "Päällä", "Aktiv", "Actif", "Activo", "Attiva")
                                            : txt("Disabled", "Pois", "Aus", "Désactivé", "Desactivado", "Disattiva"),
                   dashboardDemoModeEnabled, BTN_OPTION_BASE);
    makeMenuButton(scr, kMenuEdgeX, 110, kMenuContentW, 56,
                   txt("TIME COMPRESSION", "AJAN NOPEUTUS", "ZEITRAFFER", "VITESSE DU TEMPS", "VELOCIDAD DEL TIEMPO", "VELOCITÀ DEL TEMPO"),
                   rate, false, BTN_OPTION_BASE + 1);
    makeMenuButton(scr, kMenuEdgeX, 172, kMenuContentW, 56,
                   txt("RESTART RIDE", "ALOITA AJO ALUSTA", "FAHRT NEU STARTEN", "RECOMMENCER", "REINICIAR VIAJE", "RIAVVIA VIAGGIO"),
                   "13S3P / 505 Wh / 100%", false, BTN_OPTION_BASE + 2);
    loadScreen(scr);
    return;
  }

  if (submenuType == SUBMENU_DASH_UI) {
    if (dashUiGridOpen)
      showDashUiGrid(scr, title);
    else
      showDashUiSelector(scr, title, submenuItemIndex(), submenuItemCount(), submenuCurrentName());
    loadScreen(scr);
    return;
  }

  if (submenuType == SUBMENU_VESC) {
    // Main-menu look: same top bar, same 3x3 tile grid and tile styling.
    uint8_t fields[sizeof(kVehicleProfileFields)];
    const uint8_t fieldCount = collectVisibleFields(kVehicleProfileFields, sizeof(kVehicleProfileFields), fields);
    const uint8_t pageCount = vehiclePageCount();
    if (vehiclePage >= pageCount) vehiclePage = pageCount - 1;
    makeMenuTopBar(scr, title, vehiclePage + 1, pageCount, true);
    const int first = vehiclePage * VEHICLE_FIELDS_PER_PAGE;
    for (int i = 0; i < VEHICLE_FIELDS_PER_PAGE && first + i < fieldCount; i++) {
      const int field = fields[first + i];
      const int col = i % 3;
      const int row = i / 3;
      char value[32];
      vehicleFieldValue(field, value, sizeof(value));
      const int x = col == 0 ? kMenuEdgeX : (col == 1 ? kThreeColX1 : kThreeColX2);
      makeMenuButton(scr, x, 48 + row * 58, kThreeColW, 54, vehicleFieldTitle(field), value, false,
                     BTN_OPTION_BASE + field);
    }
    loadScreen(scr);
    return;
  }

  if (submenuType == SUBMENU_CONTROLLER_CONFIG) {
    makeMenuTopBar(scr, txt("CONFIGURATION", "ASETUKSET", "KONFIGURATION", "CONFIGURATION", "CONFIGURACIÓN", "CONFIGURAZIONE"), 1, 1, false);
    const SubmenuType sections[] = {SUBMENU_POWER_LIMITS, SUBMENU_SPEED, SUBMENU_SPEED_CALIBRATION, SUBMENU_GAUGE_RANGES};
    const char *descriptions[] = {
      txt("Controller limits", "Ohjaimen rajat", "Controllerlimits", "Limites contrôleur", "Límites controlador", "Limiti controller"),
      controllerUsesFarDriverBle() ? txt("Gears and labels", "Vaihteet ja nimet", "Gänge und Namen", "Rapports et noms", "Marchas y nombres", "Marce e nomi") : txt("Profile status", "Profiilin tila", "Profilstatus", "État du profil", "Estado perfil", "Stato profilo"),
      txt("Wheel and road speed", "Pyörä ja nopeus", "Rad und Tempo", "Roue et vitesse", "Rueda y velocidad", "Ruota e velocità"),
      txt("Display scales only", "Vain näyttöasteikot", "Nur Anzeigeskalen", "Échelles écran", "Escalas pantalla", "Scale display")};
    for (int i = 0; i < 4; ++i)
      makeMenuButton(scr, i % 2 ? kTwoColRightX : kMenuEdgeX, 48 + (i / 2) * 88,
                     i % 2 ? kTwoColRightW : kTwoColLeftW, 82, vehicleSectionTitle(sections[i]), descriptions[i], false, BTN_OPTION_BASE + i);
    loadScreen(scr); return;
  }
  if (submenuType == SUBMENU_CONNECTION) {
    makeMenuTopBar(scr, vehicleSectionTitle(submenuType), 1, 1, false);
    makeMenuButton(scr, kMenuEdgeX, 48, kMenuContentW, 54,
                   txt("CONTROLLER & LINK", "OHJAIN JA YHTEYS", "CONTROLLER / LINK", "CONTRÔLEUR / LIEN", "CONTROLADOR / ENLACE", "CONTROLLER / LINK"),
                   controllerTypeName(), false, BTN_OPTION_BASE + VEHICLE_FIELD_SETUP);
    // A wired UART link never uses the radio, so only a Bluetooth controller
    // offers the switch; its fields then move down a row.
    const bool bluetoothLink = controllerUsesBluetooth();
    if (bluetoothLink)
      makeMenuButton(scr, kMenuEdgeX, 108, kMenuContentW, 54, "BLUETOOTH",
                     bluetoothEnabled
                         ? txt("On - controller link active", "Päällä - ohjainyhteys käytössä",
                               "An - Controller-Verbindung aktiv", "Actif - liaison contrôleur",
                               "Activado - enlace activo", "Attivo - collegamento attivo")
                         : txt("Off - no controller link", "Pois - ei ohjainyhteyttä", "Aus - keine Verbindung",
                               "Inactif - aucune liaison", "Desactivado - sin enlace", "Spento - nessun collegamento"),
                     bluetoothEnabled, BTN_OPTION_BASE + kConnectionBluetoothOption);
    makeVehicleFields(scr, kConnectionFields, sizeof(kConnectionFields), bluetoothLink ? 168 : 108);
    if (controllerCapabilities().hasLinkDiagnostics)
      makeMenuButton(scr, kMenuEdgeX, 172, kMenuContentW, 52,
                     txt("LINK DIAGNOSTICS", "YHTEYSDIAGNOSTIIKKA", "LINK-DIAGNOSE", "DIAGNOSTIC", "DIAGNÓSTICO", "DIAGNOSTICA"), "", false, BTN_CONTROLLER_DIAGNOSTICS);
    loadScreen(scr); return;
  }
  if (submenuType == SUBMENU_GAUGE_RANGES) {
    makeMenuTopBar(scr, vehicleSectionTitle(submenuType), 1, 1, false);
    makeMenuButton(scr, kMenuEdgeX, 48, kMenuContentW, 54,
      txt("RANGE SCALING", "ASTEIKON SÄÄTÖ", "SKALIERUNG", "ÉCHELLE", "ESCALA", "SCALA"),
      automaticGaugeRanges ? txt("Automatic", "Automaattinen", "Automatisch", "Automatique", "Automático", "Automatico")
                           : txt("Manual", "Manuaalinen", "Manuell", "Manuel", "Manual", "Manuale"),
      automaticGaugeRanges, BTN_OPTION_BASE + VEHICLE_FIELD_COUNT + 10);
    if (!automaticGaugeRanges) makeVehicleFields(scr, kGaugeFields, sizeof(kGaugeFields), 108);
    else {
      char ranges[64]; snprintf(ranges, sizeof(ranges), "%d km/h / %d W / %d A", automaticGaugeMaximum(RANGE_SPEED), automaticGaugeMaximum(RANGE_POWER), automaticGaugeMaximum(RANGE_CURRENT));
      makeMenuButton(scr, kMenuEdgeX, 108, kMenuContentW, 54,
        txt("RESET LEARNED RANGES", "NOLLAA OPITUT ASTEIKOT", "GELERNTE SKALEN LÖSCHEN", "RÉINITIALISER ÉCHELLES", "REINICIAR ESCALAS", "AZZERA SCALE APPRESE"),
        ranges, false, BTN_OPTION_BASE + VEHICLE_FIELD_COUNT + 11);
      makeMenuButton(scr, kMenuEdgeX, 168, kMenuContentW, 60,
        txt("AUTOMATIC", "AUTOMAATTINEN", "AUTOMATISCH", "AUTOMATIQUE", "AUTOMÁTICO", "AUTOMATICO"),
        txt("Grows as needed. Holds during the ride.", "Kasvaa tarvittaessa. Säilyy ajon aikana.", "Wächst bei Bedarf. Bleibt während der Fahrt.", "Augmente au besoin. Stable en trajet.", "Crece si hace falta. Estable en ruta.", "Cresce se serve. Stabile in viaggio."), false, 0);
    }
    loadScreen(scr); return;
  }
  if (submenuType == SUBMENU_SPEED_CALIBRATION || submenuType == SUBMENU_GAUGE_RANGES) {
    makeMenuTopBar(scr, vehicleSectionTitle(submenuType), 1, 1, false);
    makeLabelAt(scr, 160, 47, txt("Saved on this display", "Tallennus näyttöön", "Auf Display gespeichert", "Stocké sur écran", "Guardado en pantalla", "Salvato sul display"), cyd_ui::secondaryText(), &lv_font_rajdhani_12, 3);
    if (submenuType == SUBMENU_SPEED_CALIBRATION) makeVehicleFields(scr, kCalibrationFields, sizeof(kCalibrationFields));
    else makeVehicleFields(scr, kGaugeFields, sizeof(kGaugeFields));
    loadScreen(scr); return;
  }
  if (submenuType == SUBMENU_POWER_LIMITS) {
    makeMenuTopBar(scr, vehicleSectionTitle(submenuType), 1, 1, false);
    makeVehicleExplanation(scr, controllerUsesFarDriverBle()
      ? txt("Use the FarDriver app to change battery current, phase current and gear limits. Controller writes are not supported here yet.", "Muuta akkuvirtaa, vaihevirtaa ja vaihderajoja FarDriver-sovelluksessa. Näyttö ei vielä kirjoita ohjainasetuksia.", "Batteriestrom, Phasenstrom und Ganglimits in der FarDriver-App ändern. Schreiben wird hier noch nicht unterstützt.", "Modifiez les courants et rapports dans FarDriver. Écriture non prise en charge ici.", "Cambie corrientes y marchas en FarDriver. Escritura aún no disponible aquí.", "Modifica correnti e marce in FarDriver. Scrittura non ancora disponibile qui.")
      : txt("Use VESC Tool to change battery current, motor current and power limits. Controller writes are not supported here yet.", "Muuta akkuvirtaa, moottorivirtaa ja tehorajoja VESC Toolissa. Näyttö ei vielä kirjoita ohjainasetuksia.", "Batteriestrom, Motorstrom und Leistungslimits in VESC Tool ändern. Schreiben wird hier noch nicht unterstützt.", "Modifiez courants et puissance dans VESC Tool. Écriture non prise en charge ici.", "Cambie corrientes y potencia en VESC Tool. Escritura aún no disponible aquí.", "Modifica correnti e potenza in VESC Tool. Scrittura non ancora disponibile qui."));
    loadScreen(scr); return;
  }
  if (submenuType == SUBMENU_SPEED) {
    if (!controllerCapabilities().reportsRideMode) speedPage = 0;
    else speedPage = min<uint8_t>(speedPage, 1);
    makeMenuTopBar(scr, vehicleSectionTitle(submenuType), speedPage + 1, submenuItemCount(), true);
    if (controllerCapabilities().reportsRideMode) {
      if (speedPage == 0) {
        lv_obj_t *gearPanel = makeMenuButton(scr, kMenuEdgeX, 48, kMenuContentW, 58,
                       txt("REPORTED GEAR", "ILMOITETTU VAIHDE", "GEMELDETER GANG", "RAPPORT REÇU", "MARCHA RECIBIDA", "MARCIA RILEVATA"), rideModeName(telemetryRideMode()), false, 0);
        rideModeReadout = lv_obj_get_child(gearPanel, 1);
        lv_obj_t *note = makeLabelAt(scr, kMenuEdgeX + 8, 122,
          txt("Next: display labels for Low / Mid / High. Labels do not change controller settings.", "Seuraava: matalan, keski- ja korkean vaihteen nimet. Nimet eivät muuta ohjainta.", "Weiter: Namen für Niedrig / Mittel / Hoch. Namen ändern keine Controllerwerte.", "Suivant : noms bas / moyen / haut. Aucun réglage contrôleur modifié.", "Siguiente: nombres bajo / medio / alto. No cambia el controlador.", "Avanti: nomi basso / medio / alto. Non modifica il controller."), cyd_ui::secondaryText(), &lv_font_rajdhani_14, 0);
        lv_obj_set_width(note, kMenuContentW - 16); lv_obj_set_height(note, LV_SIZE_CONTENT); lv_label_set_long_mode(note, LV_LABEL_LONG_WRAP);
      } else makeVehicleFields(scr, kModeLabelFields, sizeof(kModeLabelFields));
    } else makeVehicleExplanation(scr, txt("VESC does not report an active profile in standard telemetry. Configure profiles in VESC Tool. Mode display is unavailable until a profile integration is connected.", "VESC ei ilmoita aktiivista profiilia perustiedoissa. Määritä profiilit VESC Toolissa. Tilan näyttö vaatii profiili-integraation.", "VESC meldet kein aktives Profil in Standard-Telemetrie. Profile in VESC Tool einstellen. Die Anzeige benötigt eine Profilintegration.", "VESC ne transmet pas le profil actif. Configurez les profils dans VESC Tool. Affichage indisponible sans intégration.", "VESC no informa del perfil activo. Configure perfiles en VESC Tool. Se requiere integración para mostrarlos.", "VESC non comunica il profilo attivo. Configura i profili in VESC Tool. La visualizzazione richiede un'integrazione."));
    loadScreen(scr); return;
  }
  if (submenuType == SUBMENU_BATTERY && batteryPage == 1) {
    makeMenuTopBar(scr, vehicleSectionTitle(SUBMENU_PACK_SETUP), 2, 2, true);
    makeVehicleFields(scr, kPackFields, sizeof(kPackFields));
    loadScreen(scr); return;
  }

  if (submenuType == SUBMENU_CONTROLLER_TYPE) {
    makeMenuTopBar(scr, title, 1, 1, false);
    if (controllerSetupStage == CONTROLLER_SETUP_TYPE) {
      makeLabelAt(scr, 160, 48, txt("CHOOSE THE CONTROLLER FAMILY", "VALITSE OHJAIMEN TYYPPI",
                                    "CONTROLLER-FAMILIE WÄHLEN", "CHOISIR LE CONTRÔLEUR",
                                    "ELEGIR EL CONTROLADOR", "SCEGLI IL CONTROLLER"),
                  c565(COLOR565_LABEL), &lv_font_rajdhani_12, 3);
      makeMenuButton(scr, kMenuEdgeX, 66, kMenuContentW, 66, "VESC",
                     txt("UART or Bluetooth LE", "UART tai Bluetooth LE", "UART oder Bluetooth LE",
                         "UART ou Bluetooth LE", "UART o Bluetooth LE", "UART o Bluetooth LE"),
                     false, BTN_CONTROLLER_VESC);
      makeMenuButton(scr, kMenuEdgeX, 140, kMenuContentW, 66, "FARDRIVER", "Bluetooth LE", false,
                     BTN_CONTROLLER_FARDRIVER);
    } else if (controllerSetupStage == CONTROLLER_SETUP_CONNECTION) {
      makeLabelAt(scr, 160, 48, txt("HOW IS THE VESC CONNECTED?", "MITEN VESC ON YHDISTETTY?",
                                    "WIE IST DER VESC VERBUNDEN?", "COMMENT LE VESC EST-IL CONNECTÉ ?",
                                    "¿CÓMO ESTÁ CONECTADO EL VESC?", "COME È COLLEGATO IL VESC?"),
                  c565(COLOR565_LABEL), &lv_font_rajdhani_12, 3);
      makeMenuButton(scr, kMenuEdgeX, 66, kMenuContentW, 66, txt("WIRED UART", "UART-KAAPELI", "KABEL-UART", "UART CÂBLÉ",
                                                               "UART POR CABLE", "UART CABLATA"),
                     txt("Direct cable to the controller", "Suora kaapeli ohjaimeen", "Direktes Controller-Kabel",
                         "Câble direct au contrôleur", "Cable directo al controlador", "Cavo diretto al controller"),
                     false, BTN_CONTROLLER_UART);
      makeMenuButton(scr, kMenuEdgeX, 140, kMenuContentW, 66, "BLUETOOTH LE",
                     txt("VESC Express or compatible UART adapter", "VESC Express tai yhteensopiva sovitin",
                         "VESC Express oder kompatibler Adapter", "VESC Express ou adaptateur compatible",
                         "VESC Express o adaptador compatible", "VESC Express o adattatore compatibile"),
                     false, BTN_CONTROLLER_BLE);
    } else if (controllerSetupStage == CONTROLLER_SETUP_DISCOVERY) {
      const ControllerBackend *backend = pendingControllerBackend();
      const ControllerLinkStatus status = backend && backend->linkStatus ? backend->linkStatus()
                                                                          : ControllerLinkStatus{};
      const uint8_t count = status.deviceCount;
      const bool connected = status.state == CONTROLLER_LINK_CONNECTED;
      const char *message = controllerLinkStateDetail(status);
      uint8_t ordered[CONTROLLER_LINK_MAX_DEVICES] = {};
      uint8_t orderedCount = 0;
      uint8_t likelyCount = 0;
      for (uint8_t pass = 0; pass < 2; pass++) {
        for (uint8_t i = 0; i < count; i++) {
          const bool likely = status.devices[i].likelyMatch;
          if (likely == (pass == 0)) {
            ordered[orderedCount++] = i;
            if (pass == 0) likelyCount++;
          }
        }
      }
      const uint8_t maxOffset = count > 3 ? count - 3 : 0;
      if (controllerDeviceOffset > maxOffset) controllerDeviceOffset = maxOffset;
      const bool failed = status.state == CONTROLLER_LINK_FAILED;
      char heading[64] = {};
      if (connected) {
        snprintf(heading, sizeof(heading), "%s", txt("CONTROLLER FOUND", "OHJAIN LÖYDETTY", "CONTROLLER GEFUNDEN",
                                                     "CONTRÔLEUR TROUVÉ", "CONTROLADOR ENCONTRADO", "CONTROLLER TROVATO"));
      } else if (controllerScanPending) {
        snprintf(heading, sizeof(heading), "%s",
                 txt("RELEASING BLUETOOTH...", "VAPAUTETAAN BLUETOOTH...", "BLUETOOTH WIRD FREIGEGEBEN...",
                     "LIBÉRATION DU BLUETOOTH...", "LIBERANDO BLUETOOTH...", "RILASCIO BLUETOOTH..."));
      } else if (failed || status.state == CONTROLLER_LINK_CONNECTING ||
                 status.state == CONTROLLER_LINK_DISCOVERING) {
        // What happened to a connect attempt has to reach the screen: with
        // devices listed the section heading would otherwise hide every
        // failure, and tapping a device would look like it did nothing.
        snprintf(heading, sizeof(heading), "%s", failed ? controllerLinkErrorDetail(status.error) : message);
      } else if (count > 0) {
        const bool likelySection = likelyCount > 0 && controllerDeviceOffset < likelyCount;
        const char *section =
            likelySection ? txt("LIKELY CONTROLLERS", "TODENNÄKÖISET", "WAHRSCHEINLICHE", "PROBABLES",
                                "PROBABLES", "PROBABILI")
                          : (likelyCount > 0 ? txt("OTHER DEVICES", "MUUT LAITTEET", "ANDERE GERÄTE",
                                                   "AUTRES APPAREILS", "OTROS DISPOSITIVOS",
                                                   "ALTRI DISPOSITIVI")
                                             : txt("ALL DEVICES", "KAIKKI LAITTEET", "ALLE GERÄTE",
                                                   "TOUS LES APPAREILS", "TODOS LOS DISPOSITIVOS",
                                                   "TUTTI I DISPOSITIVI"));
        snprintf(heading, sizeof(heading), "%s  |  %u-%u/%u", section, controllerDeviceOffset + 1,
                 min<uint8_t>(controllerDeviceOffset + 3, count), count);
      } else {
        snprintf(heading, sizeof(heading), "%s", message);
      }
      makeLabelAt(scr, 160, 47, heading,
                  connected ? c565(COLOR565_GREEN) : failed ? c565(COLOR565_ORANGE) : c565(COLOR565_LABEL),
                  &lv_font_rajdhani_12, 3);
      for (uint8_t row = 0; row < 3 && controllerDeviceOffset + row < orderedCount; row++) {
        const uint8_t index = ordered[controllerDeviceOffset + row];
        const char *name = status.devices[index].name;
        const char *address = status.devices[index].address;
        const int rssi = status.devices[index].rssi;
        const bool likely = status.devices[index].likelyMatch;
        char detail[56];
        if (likely)
          snprintf(detail, sizeof(detail), "%s  |  %s  |  %d dBm",
                   txt("LIKELY", "MAHDOLLINEN", "MÖGLICH", "PROBABLE", "PROBABLE", "PROBABILE"), address,
                   rssi);
        else
          snprintf(detail, sizeof(detail), "%s  |  %d dBm", address, rssi);
        lv_obj_t *deviceButton = makeMenuButton(scr, kMenuEdgeX, 62 + row * 43, 269, 39, name, detail, false,
                                                BTN_CONTROLLER_DEVICE_BASE + index);
        // Candidate ranking is useful information, not a selected state. Keep
        // the control neutral and emphasize only its likelihood label.
        if (likely) {
          lv_obj_t *detailLabel = lv_obj_get_child(deviceButton, 1);
          if (detailLabel) lv_obj_set_style_text_color(detailLabel, cyd_ui::chromeAccent(), 0);
        }
      }
      if (controllerDeviceOffset > 0)
        makeVerticalNavButton(scr, 276, 62, 42, 61, true, BTN_CONTROLLER_LIST_UP);
      if (controllerDeviceOffset + 3 < count)
        makeVerticalNavButton(scr, 276, 130, 42, 61, false, BTN_CONTROLLER_LIST_DOWN);
      makeMenuButton(scr, kMenuEdgeX, 198, 119, 36, txt("SCAN AGAIN", "HAE UUDELLEEN", "NEU SUCHEN", "RECHERCHER",
                                                   "BUSCAR OTRA VEZ", "CERCA ANCORA"),
                     "", false, BTN_CONTROLLER_RETRY);
      if (connected)
        makeMenuButton(scr, 126, 198, 192, 36, txt("CONTINUE >", "JATKA >", "WEITER >", "CONTINUER >",
                                                     "CONTINUAR >", "CONTINUA >"),
                       "", false, BTN_CONTROLLER_CONTINUE);
    } else {
      const ControllerBackend *backend = pendingControllerBackend();
      const bool wiredVesc = backend && backend->id == CONTROLLER_ID_VESC_UART;
      lv_obj_t *panel = makePanel(scr, 18, 51, 284, 168, 8, c565(0xFB40), c565(0x0841), true);
      makeLabelAt(panel, 142, 15,
                  wiredVesc
                      ? txt("ENABLE WIRED VESC?", "OTA VESC UART KÄYTTÖÖN?", "VESC-KABEL AKTIVIEREN?",
                            "ACTIVER VESC CÂBLÉ ?", "¿ACTIVAR VESC CABLEADO?", "ATTIVARE VESC CABLATO?")
                      : txt("APPLY CONTROLLER SETUP?", "KÄYTÄ OHJAINASETUKSIA?", "CONTROLLER-SETUP ANWENDEN?",
                            "APPLIQUER LA CONFIGURATION ?", "¿APLICAR CONFIGURACIÓN?", "APPLICARE CONFIGURAZIONE?"),
                  lv_color_white(), &lv_font_rajdhani_12, 3);
      char summary[64];
      snprintf(summary, sizeof(summary), "%s  |  %s", backend ? backend->name : "VESC",
               controllerTransportLabel(backend ? backend->transport : CONTROLLER_TRANSPORT_UART));
      makeLabelAt(panel, 142, 48, summary, c565(0xFB40), &lv_font_rajdhani_12, 3);
      lv_obj_t *confirmBody = makeLabelAt(
          panel, 142, 70,
          wiredVesc
              ? txt("Enables the wired VESC link.\nOnly this connection runs.",
                    "Ottaa VESC UART -yhteyden käyttöön.\nVain tämä yhteys on käytössä.",
                    "Aktiviert den VESC-Kabelmodus.\nNur diese Verbindung läuft.",
                    "Active la liaison VESC câblée.\nSeule cette connexion tourne.",
                    "Activa el VESC cableado.\nSolo funciona esta conexión.",
                    "Attiva il collegamento VESC cablato.\nSolo questa connessione resta attiva.")
              : txt("Saves the setup and starts this\nlink. Only this connection runs.",
                    "Tallentaa asetukset ja avaa tämän\nyhteyden. Vain tämä yhteys on käytössä.",
                    "Speichert das Setup und startet\ndiese Verbindung. Nur diese läuft.",
                    "Enregistre et démarre cette\nliaison. Seule cette connexion tourne.",
                    "Guarda e inicia esta conexión.\nSolo funciona esta conexión.",
                    "Salva e avvia questo collegamento.\nSolo questa connessione resta attiva."),
          c565(COLOR565_LABEL), &lv_font_rajdhani_12, 3);
      lv_obj_set_pos(confirmBody, 17, 68);
      lv_obj_set_size(confirmBody, 250, 34);
      lv_obj_set_style_text_align(confirmBody, LV_TEXT_ALIGN_CENTER, 0);
      makeMenuButton(panel, 10, 120, 124, 36, txt("CANCEL", "PERUUTA", "ABBRECHEN", "ANNULER", "CANCELAR", "ANNULLA"),
                     "", false, BTN_CONTROLLER_CANCEL_APPLY);
      makeMenuButton(panel, 150, 120, 124, 36,
                     wiredVesc ? txt("CONFIRM", "VAHVISTA", "BESTÄTIGEN", "CONFIRMER", "CONFIRMAR", "CONFERMA")
                               : txt("APPLY", "KÄYTÄ", "ANWENDEN", "APPLIQUER", "APLICAR", "APPLICA"),
                     "", false, BTN_CONTROLLER_APPLY);
    }
    loadScreen(scr);
    return;
  }

  if (submenuType == SUBMENU_LOGGING)
    makeMenuTopBar(scr, title, 1, 1, false, true);
  else if (submenuType == SUBMENU_BATTERY)
    makeMenuTopBar(scr, title, 1, 2, true);
  else if (submenuType == SUBMENU_PANEL_COLORS)
    makeMenuTopBar(scr, title, panelPage + 1, kPanelPageCount, true);
  else if (submenuType != SUBMENU_DISPLAY)
    makeSubmenuTopBar(scr, title);

  if (submenuType == SUBMENU_LANGUAGE) {
    makeMenuButton(scr, kMenuEdgeX, 56, kTwoColLeftW, 42, "English", "English UI", language == LANG_EN, BTN_OPTION_BASE + LANG_EN);
    makeMenuButton(scr, kTwoColRightX, 56, kTwoColRightW, 42, "Suomi", "Suomenkielinen", language == LANG_FI,
                   BTN_OPTION_BASE + LANG_FI);
    makeMenuButton(scr, kMenuEdgeX, 106, kTwoColLeftW, 42, "Deutsch", "Deutsche UI", language == LANG_DE, BTN_OPTION_BASE + LANG_DE);
    makeMenuButton(scr, kTwoColRightX, 106, kTwoColRightW, 42, "Français", "Interface FR", language == LANG_FR,
                   BTN_OPTION_BASE + LANG_FR);
    makeMenuButton(scr, kMenuEdgeX, 156, kTwoColLeftW, 42, "Español", "Interfaz ES", language == LANG_ES, BTN_OPTION_BASE + LANG_ES);
    makeMenuButton(scr, kTwoColRightX, 156, kTwoColRightW, 42, "Italiano", "Interfaccia IT", language == LANG_IT,
                   BTN_OPTION_BASE + LANG_IT);
  } else if (submenuType == SUBMENU_UNITS) {
    makeMenuButton(scr, kMenuEdgeX, 52, kTwoColLeftW, 50, txt("Metric", "Metrinen", "Metrisch", "Métrique", "Métrico", "Metrico"),
                   "km/h, km", unitMode == UNITS_METRIC, BTN_OPTION_BASE + 0);
    makeMenuButton(scr, kTwoColRightX, 52, kTwoColRightW, 50,
                   txt("Imperial", "Imperiaalinen", "Imperial", "Imperial", "Imperial", "Imperiale"), "mph, mi",
                   unitMode == UNITS_IMPERIAL, BTN_OPTION_BASE + 1);
    makeMenuButton(scr, kMenuEdgeX, 112, kTwoColLeftW, 50,
                   txt("Nautical", "Merenkulku", "Nautisch", "Nautique", "Náutico", "Nautico"), "kn, nm",
                   unitMode == UNITS_NAUTICAL, BTN_OPTION_BASE + 2);
    makeMenuButton(scr, kTwoColRightX, 112, kTwoColRightW, 50, "Mach",
                   txt("Mach number", "Mach-luku", "Machzahl", "Nombre de Mach", "Número Mach", "Numero Mach"),
                   unitMode == UNITS_MACH, BTN_OPTION_BASE + 3);
  } else if (submenuType == SUBMENU_DISPLAY) {
    if (displayPage == 0) {
      makeLabelAt(scr, 12, 52, txt("Screen Brightness", "Näytön kirkkaus", "Display-Helligkeit",
                                   "Luminosité écran", "Brillo pantalla", "Luminosità schermo"),
                  lv_color_white(), &lv_font_rajdhani_14, 0);
      brightnessReadoutLabel = makeLabelAt(scr, 300, 52, "", lv_color_white(), &lv_font_rajdhani_14, 2);
      setBrightnessReadout(brightnessReadoutLabel);
      // The 36 px knob overhangs the track by roughly half its width. Inset the
      // track so the knob remains wholly inside the 320 px panel at both ends.
      makeBrightnessSlider(scr, 22, 76, 276, 36);

      makeLabelAt(scr, 12, 119, txt("Automation", "Automaatio", "Automatik", "Automatisation", "Automatización",
                                    "Automazione"),
                  lv_color_white(), &lv_font_rajdhani_14, 0);
      lv_obj_t *autoButton = makeMenuButton(
          scr, kMenuEdgeX, 139, kTwoColLeftW, 58,
          txt("Auto Brightness", "Autom. kirkkaus", "Auto-Helligkeit", "Luminosité auto", "Brillo auto",
              "Luminosità auto"),
          autoBrightnessEnabled ? txt("On - light sensor", "Päällä - valoanturi", "An - Lichtsensor",
                                      "Actif - capteur", "Activado - sensor", "Attivo - sensore")
                                : txt("Off - manual", "Pois - manuaalinen", "Aus - manuell", "Inactif - manuel",
                                      "Apagado - manual", "Spento - manuale"),
          autoBrightnessEnabled, BTN_OPTION_BASE + 0);
      lv_obj_t *autoTitle = lv_obj_get_child(autoButton, 0);
      lv_obj_t *autoValue = lv_obj_get_child(autoButton, 1);
      if (autoTitle) {
        lv_obj_set_style_text_font(autoTitle, &lv_font_rajdhani_14, 0);
        lv_obj_set_y(autoTitle, 5);
      }
      if (autoValue) lv_obj_set_y(autoValue, 23);
      // Live sensor readouts are refreshed by uiSensorTick. Keeping them inside
      // this button makes their relationship to automatic brightness explicit.
      ldrLiveLabel = makeLabelAt(autoButton, 74, 41, "", c565(COLOR565_LABEL), &lv_font_rajdhani_12, 3);
      lv_obj_set_x(ldrLiveLabel, 4);
      lv_obj_set_width(ldrLiveLabel, 126);  // reserve the right edge for the button chevron
      lv_obj_set_style_text_align(ldrLiveLabel, LV_TEXT_ALIGN_CENTER, 0);
      ldrTargetLabel = NULL;
      // Auto Brightness and its calibration form one compound control: the
      // shared width and tight seam make their ownership clear while retaining
      // canonical button behavior for each independent touch target.
      makeMenuButton(
          scr, kMenuEdgeX, 200, kTwoColLeftW, 34,
          txt("CALIBRATE SENSOR", "KALIBROI ANTURI", "SENSOR KALIBRIEREN", "CALIBRER CAPTEUR",
              "CALIBRAR SENSOR", "CALIBRA SENSORE"),
          "", false, BTN_LIGHT_SENSOR_CALIBRATE);

      lv_obj_t *ledButton = makeMenuButton(
          scr, kTwoColRightX, 139, kTwoColRightW, 95,
          txt("Ambient LED", "Tunnelmavalo", "Ambiente-LED", "LED ambiance", "LED ambiente", "LED ambiente"),
          themeLedEnabled ? txt("On - theme color", "Päällä - teeman väri", "An - Designfarbe", "Actif - couleur",
                                "Activado - color", "Attivo - colore")
                          : txt("Off", "Pois", "Aus", "Inactif", "Apagado", "Spento"),
          themeLedEnabled, BTN_OPTION_BASE + 1);
      // Match the title and status baselines with Auto Brightness; the sensor
      // readout simply continues below those shared rows on the left.
      lv_obj_t *ledTitle = lv_obj_get_child(ledButton, 0);
      lv_obj_t *ledValue = lv_obj_get_child(ledButton, 1);
      if (ledTitle) {
        lv_obj_set_style_text_font(ledTitle, &lv_font_rajdhani_14, 0);
        lv_obj_set_y(ledTitle, 5);
      }
      if (ledValue) lv_obj_set_y(ledValue, 23);
    } else if (displayPage == 1) {
      makeMenuButton(scr, kMenuEdgeX, 48, kMenuContentW, 57,
                     txt("TOUCH CALIBRATION", "KOSKETUSKALIBROINTI", "TOUCH-KALIBRIERUNG", "CALIBRAGE TACTILE",
                         "CALIBRACIÓN TÁCTIL", "CALIBRAZIONE TOUCH"),
                     txt("Recalibrate the touch coordinates", "Kalibroi kosketuspisteet", "Touch-Koordinaten neu kalibrieren",
                         "Recalibrer les coordonnées", "Recalibrar coordenadas", "Ricalibra le coordinate"),
                     false, BTN_OPTION_BASE + 2);
      makeMenuButton(scr, kMenuEdgeX, 110, kMenuContentW, 57,
                     txt("TOUCH TEST", "KOSKETUSTESTI", "TOUCH-TEST", "TEST TACTILE", "PRUEBA TÁCTIL", "TEST TOUCH"),
                     txt("Draw detected touch points", "Piirrä havaitut kosketuspisteet", "Erkannte Berührungen zeichnen",
                         "Dessiner les points détectés", "Dibujar puntos detectados", "Disegna i punti rilevati"),
                     false, BTN_OPTION_BASE + 3);
      makeMenuButton(scr, kMenuEdgeX, 172, kMenuContentW, 57,
                     txt("DISPLAY PANEL", "NÄYTTÖPANEELI", "DISPLAY-PANEL", "DALLE D'AFFICHAGE",
                         "PANEL DE PANTALLA", "PANNELLO DISPLAY"),
                     txt("Panel type, contrast and colors", "Paneelityyppi, kontrasti ja värit",
                         "Paneltyp, Kontrast und Farben", "Type de dalle, contraste, couleurs",
                         "Tipo de panel, contraste y colores", "Tipo pannello, contrasto e colori"),
                     false, BTN_OPTION_BASE + 4);
    }
    // Build navigation last on these dense pages so diagnostic panels and
    // translated labels can never obscure the shared top controls.
    makeMenuTopBar(scr, title, displayPage + 1, displayPageCount(), true);
  } else if (submenuType == SUBMENU_AUTO_RETURN) {
    makeMenuButton(scr, kMenuEdgeX, 52, kMenuContentW, 46,
                   txt("AUTOMATIC RETURN", "AUTOMAATTINEN PALUU", "AUTOMATISCHE RÜCKKEHR", "RETOUR AUTOMATIQUE",
                       "RETORNO AUTOMÁTICO", "RITORNO AUTOMATICO"),
                   autoReturnEnabled
                       ? txt("Enabled", "Päällä", "Aktiv", "Actif", "Activo", "Attivo")
                       : txt("Disabled", "Pois", "Aus", "Désactivé", "Desactivado", "Disattivo"),
                   false, BTN_OPTION_BASE);
    makeLabelAt(scr, 12, 104,
                txt("RETURN TO HOME SCREEN AFTER", "PALUU PÄÄNÄYTTÖÖN AJASSA:",
                    "ZUM STARTBILDSCHIRM NACH", "RETOUR À L'ACCUEIL APRÈS",
                    "VOLVER A INICIO DESPUÉS DE", "TORNA ALLA HOME DOPO"),
                c565(COLOR565_LABEL), &lv_font_rajdhani_12, 0);
    static const char *labels[] = {"30 SEC", "1 MIN", "2 MIN", "5 MIN", "10 MIN", "15 MIN", "30 MIN", "60 MIN"};
    for (uint8_t i = 0; i < sizeof(kAutoReturnChoices) / sizeof(kAutoReturnChoices[0]); i++) {
      const int col = i % 4;
      const int row = i / 4;
      const int x[] = {kMenuEdgeX, kFourColX1, kFourColX2, kFourColX3};
      const int w[] = {kFourColW, kFourColW, kFourColW, kFourColLastW};
      makeMenuButton(scr, x[col], 120 + row * 54, w[col], 46, labels[i], "",
                     autoReturnEnabled && autoReturnTimeoutSeconds == kAutoReturnChoices[i], BTN_OPTION_BASE + 1 + i);
    }
  } else if (submenuType == SUBMENU_PANEL_COLORS && panelPage > 0) {
    lv_obj_t *info = makeInfoPanel(scr, 10, 44, 300, 40, 6);
    char heading[40];
    snprintf(heading, sizeof(heading), "%u/%u  %s", displayPanelTuning + 1, PANEL_TUNING_COUNT,
             displayPanelTuningName(displayPanelTuning));
    makeLabelAt(info, 10, 3, heading, lv_color_white(), &lv_font_rajdhani_12, 0);
    makeLabelAt(info, 10, 20, displayPanelTuningNote(displayPanelTuning), cyd_ui::secondaryText(),
                &lv_font_rajdhani_12, 0);
    // Two rows make one 32-step grey ramp; the dark first row is where a
    // lifted black level shows. Below: a continuous hue sweep (red, yellow,
    // green, cyan, blue, magenta, red) and each channel rising from black.
    for (uint8_t level = 0; level < 32; level++) {
      const uint16_t grey = static_cast<uint16_t>((level << 11) | (level << 6) | level);
      makeDisplayColorReference(scr, 16 + (level % 16) * 18, 89 + (level / 16) * 20, 17, grey, "", 0, 18);
    }
    static constexpr uint16_t kHueStops[] = {0xF800, 0xFFE0, 0x07E0, 0x07FF, 0x001F, 0xF81F, 0xF800};
    for (uint8_t i = 0; i < 6; i++)
      makeDisplayGradientReference(scr, 16 + i * 48, 131, 48, 16, kHueStops[i], kHueStops[i + 1]);
    static constexpr uint16_t kChannelTops[] = {0xF800, 0x07E0, 0x001F};
    for (uint8_t i = 0; i < 3; i++)
      makeDisplayGradientReference(scr, 16, 151 + i * 10, 288, 8, 0x0000, kChannelTops[i]);
    const char *savedHere = txt("Saved on this display", "Tallennetaan näyttöön", "Gespeichert",
                                "Enregistré sur l'écran", "Se guarda en la pantalla", "Salvato sul display");
    makeMenuButton(scr, kMenuEdgeX, 184, kTwoColLeftW, 46,
                   txt("< PREVIOUS", "< EDELLINEN", "< ZURÜCK", "< PRÉCÉDENT", "< ANTERIOR", "< PRECEDENTE"),
                   savedHere, false, BTN_OPTION_BASE + 10);
    makeMenuButton(scr, kTwoColRightX, 184, kTwoColRightW, 46,
                   txt("NEXT >", "SEURAAVA >", "WEITER >", "SUIVANT >", "SIGUIENTE >", "SUCCESSIVO >"),
                   savedHere, false, BTN_OPTION_BASE + 11);
  } else if (submenuType == SUBMENU_PANEL_COLORS) {
    makeMenuButton(
        scr, kMenuEdgeX, 62, kTwoColLeftW, 54,
        txt("STANDARD CYD", "VAKIO CYD", "STANDARD-CYD", "CYD STANDARD", "CYD ESTÁNDAR", "CYD STANDARD"),
        txt("Original panel colors", "Alkuperäisen paneelin värit", "Originale Panelfarben", "Couleurs d'origine",
            "Colores originales", "Colori originali"),
        displayPanelProfile == DISPLAY_PANEL_STANDARD, BTN_OPTION_BASE + 0);
    makeMenuButton(
        scr, kTwoColRightX, 62, kTwoColRightW, 54,
        txt("ALTERNATE CYD", "VAIHTOEHTOINEN", "ALTERNATIV-CYD", "CYD ALTERNATIF", "CYD ALTERNATIVO",
            "CYD ALTERNATIVO"),
        txt("Fix inverted colors", "Korjaa käänteiset värit", "Invertierte Farben korrigieren", "Corrige les couleurs",
            "Corrige colores invertidos", "Corregge colori invertiti"),
        displayPanelProfile == DISPLAY_PANEL_ALTERNATE, BTN_OPTION_BASE + 1);

    static constexpr uint16_t kReferenceColors[] = {0xF800, 0xFFE0, 0x07E0, 0x07FF, 0x001F, 0xF81F, 0xFFFF};
    static constexpr uint16_t kReferenceText[] = {0xFFFF, 0x0000, 0x0000, 0x0000, 0xFFFF, 0xFFFF, 0x0000};
    static const char *kReferenceLabels[] = {"R", "Y", "G", "C", "B", "M", "W"};
    for (uint8_t i = 0; i < 7; i++)
      makeDisplayColorReference(scr, 10 + i * 43, 124, 40, kReferenceColors[i], kReferenceLabels[i],
                                kReferenceText[i]);

    lv_obj_t *hardware = makeInfoPanel(scr, 10, 152, 300, 64, 6);
    char module[48], controller[48], firmware[40];
    displayModuleInfo(module, sizeof(module));
    displayControllerInfo(controller, sizeof(controller));
    snprintf(firmware, sizeof(firmware), "FW %s %s", __DATE__, __TIME__);
    makeLabelAt(hardware, 10, 6, module, lv_color_white(), &lv_font_rajdhani_12, 0);
    makeLabelAt(hardware, 10, 25, controller, c565(COLOR565_LABEL), &lv_font_rajdhani_12, 0);
    makeLabelAt(hardware, 10, 44, firmware, c565(COLOR565_LABEL), &lv_font_rajdhani_12, 0);
    makeLabelAt(scr, 160, 224,
                txt("Changes apply immediately", "Muutokset tulevat heti voimaan", "Änderungen gelten sofort",
                    "Application immédiate", "Los cambios se aplican al instante", "Modifiche applicate subito"),
                c565(COLOR565_LABEL), &lv_font_rajdhani_12, 3);
  } else if (submenuType == SUBMENU_CHANGELOG) {
    // Four entries fill the page, so there is no room for a heading above
    // them; the top bar already names the page. The entries are English only
    // (see firmware_changelog.h).
    for (uint8_t slot = 0; slot < kChangelogPerPage; slot++) {
      if (slot >= kFirmwareChangelogCount) break;
      const FirmwareChangelogEntry &entry = kFirmwareChangelog[slot];
      lv_obj_t *panel = makeInfoPanel(scr, kMenuEdgeX, 42 + slot * 49, kMenuContentW, 47, 6);
      makeLabelAt(panel, 8, 3, entry.date, c565(COLOR565_LABEL), &lv_font_rajdhani_12, 0);
      lv_obj_t *titleLabel = makeLabelAt(panel, 82, 3, entry.title, lv_color_white(), &lv_font_rajdhani_12, 0);
      lv_obj_set_width(titleLabel, 212);
      lv_label_set_long_mode(titleLabel, LV_LABEL_LONG_DOT);
      lv_obj_t *summary = makeLabelAt(panel, 8, 18, entry.summary, c565(COLOR565_LABEL), &lv_font_rajdhani_12, 0);
      lv_obj_set_width(summary, kMenuContentW - 16);
      // makeLabelAt fixes every label to one line's height, which silently clips
      // wrapped text; grow this one to its content instead. The generator caps
      // summaries at two lines so they cannot outgrow the panel.
      lv_obj_set_height(summary, LV_SIZE_CONTENT);
      lv_label_set_long_mode(summary, LV_LABEL_LONG_WRAP);
    }
  } else if (submenuType == SUBMENU_DISPLAY_INFO) {
    char controllerValue[52], linkValue[44], firmwareValue[40], hardwareValue[56], displayFirmware[40];
    const ControllerBackend *backend = activeControllerBackend();
    snprintf(controllerValue, sizeof(controllerValue), "%s | %s", controllerTypeName(),
             controllerTransportLabel(backend ? backend->transport : CONTROLLER_TRANSPORT_UART));
    // A controller transport can be live before it publishes decoded
    // telemetry. This is currently always true for FarDriver, whose packet map
    // is still provisional. Report the selected transport here; the dashboard
    // separately reports whether usable telemetry is arriving.
    TelemetryLink infoLinkState = telemetryLinkState();
    if (backend && backend->caps.showsDeviceList && backend->linkStatus) {
      const ControllerLinkState state = backend->linkStatus().state;
      infoLinkState = state == CONTROLLER_LINK_CONNECTED
                          ? LINK_LIVE
                          : (state == CONTROLLER_LINK_SCANNING || state == CONTROLLER_LINK_CONNECTING ||
                                     state == CONTROLLER_LINK_DISCOVERING
                                 ? LINK_WAITING : LINK_LOST);
    }
    const char *linkState = infoLinkState == LINK_LIVE
                                ? txt("Connected", "Yhdistetty", "Verbunden", "Connecté", "Conectado", "Connesso")
                                : infoLinkState == LINK_WAITING
                                      ? txt("Waiting", "Odottaa", "Warten", "Attente", "Esperando", "In attesa")
                                      : txt("Disconnected", "Ei yhteyttä", "Getrennt", "Déconnecté", "Desconectado",
                                            "Disconnesso");
    snprintf(linkValue, sizeof(linkValue), "%s", linkState);
    uint8_t fwMajor = 0, fwMinor = 0;
    if (telemetryFirmwareVersion(fwMajor, fwMinor))
      snprintf(firmwareValue, sizeof(firmwareValue), "%u.%02u", fwMajor, fwMinor);
    else
      snprintf(firmwareValue, sizeof(firmwareValue), "%s",
               txt("Not reported", "Ei ilmoitettu", "Nicht gemeldet", "Non indiqué", "No informado",
                   "Non riportato"));
    displayModuleInfo(hardwareValue, sizeof(hardwareValue));
    // The same name the boot splash shows; the version code is for OTA only.
    snprintf(displayFirmware, sizeof(displayFirmware), "v%s  |  %s", CYD_FIRMWARE_VERSION_LABEL, __DATE__);

    makeLabelAt(scr, 8, 48,
                txt("VEHICLE & CONTROLLER", "AJONEUVO JA OHJAIN", "FAHRZEUG & CONTROLLER",
                    "VÉHICULE & CONTRÔLEUR", "VEHÍCULO Y CONTROLADOR", "VEICOLO E CONTROLLER"),
                c565(COLOR565_LABEL), &lv_font_rajdhani_12, 0);
    lv_obj_t *controllerPanel = makeInfoPanel(scr, kMenuEdgeX, 61, kMenuContentW, 76, 6);
    auto makeInfoRow = [](lv_obj_t *parent, int y, const char *label, const char *value) {
      makeLabelAt(parent, 8, y, label, c565(COLOR565_LABEL), &lv_font_rajdhani_12, 0);
      lv_obj_t *valueLabel = makeLabelAt(parent, 111, y, value, lv_color_white(), &lv_font_rajdhani_12, 0);
      lv_obj_set_width(valueLabel, 191);
      lv_label_set_long_mode(valueLabel, LV_LABEL_LONG_DOT);
      lv_obj_set_style_text_align(valueLabel, LV_TEXT_ALIGN_RIGHT, 0);
    };
    makeInfoRow(controllerPanel, 5, txt("VEHICLE", "AJONEUVO", "FAHRZEUG", "VÉHICULE", "VEHÍCULO", "VEICOLO"),
                vehicleName[0] ? vehicleName : "-");
    makeInfoRow(controllerPanel, 23,
                txt("CONTROLLER", "OHJAIN", "CONTROLLER", "CONTRÔLEUR", "CONTROLADOR", "CONTROLLER"),
                controllerValue);
    makeInfoRow(controllerPanel, 41,
                txt("CONNECTION", "YHTEYS", "VERBINDUNG", "CONNEXION", "CONEXIÓN", "CONNESSIONE"), linkValue);
    makeInfoRow(controllerPanel, 59,
                txt("CONTROLLER FW", "OHJAIMEN FW", "CONTROLLER-FW", "FW CONTRÔLEUR", "FW CONTROLADOR",
                    "FW CONTROLLER"),
                firmwareValue);

    makeLabelAt(scr, 8, 144, txt("DISPLAY", "NÄYTTÖ", "DISPLAY", "ÉCRAN", "PANTALLA", "DISPLAY"),
                c565(COLOR565_LABEL), &lv_font_rajdhani_12, 0);
    lv_obj_t *displayPanel = makeInfoPanel(scr, kMenuEdgeX, 157, 196, 73, 6);
    makeLabelAt(displayPanel, 8, 7,
                txt("HARDWARE", "LAITTEISTO", "HARDWARE", "MATÉRIEL", "HARDWARE", "HARDWARE"),
                c565(COLOR565_LABEL), &lv_font_rajdhani_12, 0);
    lv_obj_t *hardwareLabel = makeLabelAt(displayPanel, 8, 23, hardwareValue, lv_color_white(),
                                          &lv_font_rajdhani_12, 0);
    lv_obj_set_width(hardwareLabel, 174);
    lv_label_set_long_mode(hardwareLabel, LV_LABEL_LONG_DOT);
    makeLabelAt(displayPanel, 8, 43,
                txt("DISPLAY FIRMWARE", "NÄYTÖN LAITEOHJELMISTO", "DISPLAY-FIRMWARE", "FIRMWARE ÉCRAN",
                    "FIRMWARE PANTALLA", "FIRMWARE DISPLAY"),
                c565(COLOR565_LABEL), &lv_font_rajdhani_12, 0);
    lv_obj_t *displayFirmwareLabel = makeLabelAt(displayPanel, 8, 59, displayFirmware, lv_color_white(),
                                                 &lv_font_rajdhani_12, 0);
    lv_obj_set_width(displayFirmwareLabel, 174);
    lv_label_set_long_mode(displayFirmwareLabel, LV_LABEL_LONG_DOT);

    makeMenuButton(scr, 203, 157, 115, 73,
                   txt("BLUETOOTH LINK", "BLUETOOTH LINK", "BLUETOOTH LINK", "BLUETOOTH LINK", "BLUETOOTH LINK", "BLUETOOTH LINK"),
                   txt("Phone + updates", "Puhelin + päivitys", "Handy + Updates", "Téléphone + MAJ",
                       "Móvil + actualiz.", "Telefono + update"),
                   false, BTN_USER_COMPANION);
  } else if (submenuType == SUBMENU_LOGGING) {
    const RideLoggingStatus status = rideLoggerStatus();
    loggingUiRevision = status.revision;
    const char *mode = status.mode == RIDE_LOG_ON
                           ? txt("Enabled", "Käytössä", "Aktiviert", "Activé", "Activado", "Attivato")
                           : txt("Disabled", "Pois käytöstä", "Deaktiviert", "Désactivé", "Desactivado",
                                 "Disattivato");
    char rate[16], storage[24], storageLine[64], errorLine[64];
    snprintf(rate, sizeof(rate), "%u Hz", status.sampleHz);
    if (status.cardChecking)
      snprintf(storage, sizeof(storage), "%s", txt("Checking...", "Tarkistetaan...", "Prüfen...", "Vérification...",
                                                     "Comprobando...", "Verifica..."));
    else if (status.cardReady && status.totalBytes > 0)
      snprintf(storage, sizeof(storage), "%.1f / %.1f GB", status.usedBytes / 1073741824.0,
               status.totalBytes / 1073741824.0);
    else
      snprintf(storage, sizeof(storage), "%s", txt("No card", "Ei korttia", "Keine Karte", "Pas de carte",
                                                     "Sin tarjeta", "Nessuna scheda"));
    makeLabelAt(scr, 160, 46,
                txt("TAP TO CHANGE", "MUUTA KOSKETTAMALLA", "ZUM ÄNDERN TIPPEN", "TOUCHER POUR MODIFIER",
                    "TOCA PARA CAMBIAR", "TOCCA PER CAMBIARE"),
                c565(COLOR565_LABEL), &lv_font_rajdhani_12, 3);
    lv_obj_t *modeButton = makeMenuButton(scr, kMenuEdgeX, 60, kTwoColLeftW, 56,
                   txt("DATA LOGGING", "DATALOKITUS", "DATEN-LOGGING", "JOURNAL", "REGISTRO DATOS", "LOG DATI"),
                   mode, false, BTN_OPTION_BASE + 0);
    lv_obj_t *rateButton = makeMenuButton(scr, kTwoColRightX, 60, kTwoColRightW, 56,
                   txt("SAMPLE RATE", "NÄYTETAAJUUS", "ABTASTRATE", "FRÉQUENCE", "FRECUENCIA", "FREQUENZA"),
                   rate, false, BTN_OPTION_BASE + 1);
    for (lv_obj_t *button : {modeButton, rateButton}) {
      lv_obj_set_style_bg_color(button, cyd_ui::controlSurface(), 0);
      lv_obj_set_style_border_color(button, cyd_ui::idleControlBorder(), 0);
      lv_obj_set_style_border_width(button, cyd_ui::kBorderWidth, 0);
    }
    lv_obj_t *info = makeInfoPanel(scr, kMenuEdgeX, 124, kMenuContentW, 62, 7);
    makeLabelAt(info, 10, 8, txt("LOGGING STATUS", "LOKIN TILA", "LOG-STATUS", "ÉTAT DU JOURNAL",
                                "ESTADO DEL REGISTRO", "STATO REGISTRO"),
                lv_color_white(), &lv_font_rajdhani_12, 0);
    makeLabelAt(info, 294, 8, storage,
                c565(COLOR565_LABEL), &lv_font_rajdhani_12, 2);

    const char *description;
    if (status.cardChecking) {
      description = txt("Checking the SD card in the background...", "SD-korttia tarkistetaan taustalla...",
                        "SD-Karte wird im Hintergrund geprüft...", "Vérification de la carte SD en arrière-plan...",
                        "Comprobando la tarjeta SD en segundo plano...", "Verifica della scheda SD in background...");
    } else if (!status.cardReady) {
      description = txt("No SD card detected. Insert it; detection is automatic.",
                        "SD-korttia ei havaittu. Aseta kortti; tunnistus tapahtuu automaattisesti.",
                        "Keine SD-Karte. Einsetzen; Erkennung erfolgt automatisch.",
                        "Carte SD absente. Insérez-la; elle sera détectée automatiquement.",
                        "Sin tarjeta SD. Insértala; se detectará automáticamente.",
                        "Scheda SD assente. Inseriscila; verrà rilevata automaticamente.");
    } else if (status.mode == RIDE_LOG_OFF) {
      description = txt("Logging is off. No ride data will be saved.", "Lokitus on pois. Ajotietoja ei tallenneta.",
                        "Logging ist aus. Fahrtdaten werden nicht gespeichert.",
                        "Journal désactivé. Aucune donnée ne sera enregistrée.",
                        "Registro apagado. No se guardarán datos del viaje.",
                        "Registrazione spenta. I dati non saranno salvati.");
    } else {
      description = status.recording
                        ? txt("Recording this ride. It is saved after 5 min without movement.",
                              "Ajoa tallennetaan. Se tallentuu 5 min paikallaanolon jälkeen.",
                              "Fahrt wird aufgezeichnet. Gespeichert nach 5 Min. Stillstand.",
                              "Trajet enregistré. Sauvegardé après 5 min sans mouvement.",
                              "Grabando el viaje. Se guarda tras 5 min sin movimiento.",
                              "Registrazione in corso. Salvata dopo 5 min senza movimento.")
                        : txt("Logging is on. Each ride starts after 2 s of riding.",
                              "Lokitus päällä. Jokainen ajo alkaa 2 s ajon jälkeen.",
                              "Logging ist an. Jede Fahrt startet nach 2 Sek. Fahrt.",
                              "Journal actif. Chaque trajet démarre après 2 s de route.",
                              "Registro activo. Cada viaje comienza tras 2 s de marcha.",
                              "Registrazione attiva. Ogni corsa parte dopo 2 s di marcia.");
    }
    lv_obj_t *descriptionLabel = makeLabelAt(info, 10, 27, description, c565(COLOR565_LABEL),
                                             &lv_font_rajdhani_12, 0);
    lv_obj_set_size(descriptionLabel, 284, 30);
    lv_label_set_long_mode(descriptionLabel, LV_LABEL_LONG_WRAP);
    snprintf(storageLine, sizeof(storageLine), "%lu %s", (unsigned long)status.rideCount,
             txt("rides", "ajoa", "Fahrten", "trajets", "viajes", "viaggi"));
    snprintf(errorLine, sizeof(errorLine), "%s %lu  |  %s %lu",
             txt("Drops", "Häviöt", "Verlust", "Pertes", "Pérdidas", "Perdite"),
             (unsigned long)status.droppedRecords,
             txt("SD err", "SD-virh", "SD-Fehl", "Err SD", "Err SD", "Err SD"),
             (unsigned long)status.ioErrors);
    lv_obj_t *stats = makeInfoPanel(scr, kMenuEdgeX, 192, kTwoColLeftW, 38, 7);
    makeLabelAt(stats, 8, 5, storageLine, c565(COLOR565_LABEL), &lv_font_rajdhani_12, 0);
    makeLabelAt(stats, 8, 21, errorLine, c565(COLOR565_DIM), &lv_font_rajdhani_12, 0);
    makeMenuButton(scr, kTwoColRightX, 192, kTwoColRightW, 38,
                   txt("RIDE LOGS", "AJOLOKIT", "FAHRTEN", "TRAJETS", "VIAJES", "VIAGGI"),
                   txt("Explore saved rides", "Selaa tallennuksia", "Gespeicherte Fahrten", "Voir les trajets",
                       "Explorar viajes", "Esplora viaggi"),
                   false, BTN_EXPLORE_RIDE_LOGS);
  } else if (submenuType == SUBMENU_BATTERY) {
    // Read-only 3x3 grid in the vehicle-page style. Every figure is measured
    // from the controller's counters, so anything not yet known shows "-"
    // rather than a plausible-looking zero.
    const BatteryStats stats = getBatteryStats();
    char charge[16], range[16], rideRate[20], lifeRate[20], energy[16], regen[16], cycles[16], resistance[16],
        capacity[20];

    if (stats.socPercent >= 0) {
      snprintf(charge, sizeof(charge), "%d %%", stats.socPercent);
    } else {
      snprintf(charge, sizeof(charge), "-");
    }
    if (stats.rangeKm >= 0) {
      snprintf(range, sizeof(range), "%d %s", (int)lroundf(displayDistance((float)stats.rangeKm)),
               distanceUnitLabel());
    } else {
      snprintf(range, sizeof(range), "-");
    }
    formatEnergyRate(rideRate, sizeof(rideRate), stats.tripWhPerKm);
    formatEnergyRate(lifeRate, sizeof(lifeRate), stats.lifetimeWhPerKm);
    snprintf(energy, sizeof(energy), "%d Wh", (int)lroundf(stats.tripWh));
    snprintf(regen, sizeof(regen), "%d Wh", (int)lroundf(stats.tripRegenWh));
    snprintf(cycles, sizeof(cycles), "%.1f", stats.equivalentCycles);
    if (stats.packMilliOhm > 0.0F) {
      snprintf(resistance, sizeof(resistance), "%d mOhm", (int)lroundf(stats.packMilliOhm));
    } else {
      snprintf(resistance, sizeof(resistance), "-");
    }
    if (stats.learnedSamples > 0) {
      snprintf(capacity, sizeof(capacity), "%.1f Ah (%u)", stats.learnedCapacityAh, stats.learnedSamples);
    } else {
      snprintf(capacity, sizeof(capacity), "-");
    }

    const char *titles[9] = {
        txt("CHARGE", "VARAUS", "LADUNG", "CHARGE", "CARGA", "CARICA"),
        txt("RANGE", "KANTAMA", "REICHWEITE", "AUTONOMIE", "AUTONOMÍA", "AUTONOMIA"),
        txt("RIDE", "AJO", "FAHRT", "TRAJET", "VIAJE", "VIAGGIO"),
        txt("LIFETIME", "KAIKKIAAN", "GESAMT", "TOTAL", "TOTAL", "TOTALE"),
        txt("ENERGY", "ENERGIA", "ENERGIE", "ÉNERGIE", "ENERGÍA", "ENERGIA"),
        txt("REGEN", "PALAUTUS", "REKUP.", "RÉGÉN.", "REGEN.", "RECUPERO"),
        txt("CYCLES", "SYKLIT", "ZYKLEN", "CYCLES", "CICLOS", "CICLI"),
        txt("PACK R", "SISÄINEN R", "INNENWID.", "RÉSIST.", "RESIST.", "RESIST."),
        txt("CAPACITY", "KAPASITEETTI", "KAPAZITÄT", "CAPACITÉ", "CAPACIDAD", "CAPACITÀ")};
    const char *values[9] = {charge, range, rideRate, lifeRate, energy, regen, cycles, resistance, capacity};
    for (int i = 0; i < 9; i++) {
      const int col = i % 3;
      const int x = col == 0 ? kMenuEdgeX : (col == 1 ? kThreeColX1 : kThreeColX2);
      makeMenuButton(scr, x, 48 + (i / 3) * 62, kThreeColW, 58, titles[i], values[i], false, 0);
    }
  } else if (submenuType == SUBMENU_PIN) {
    makeMenuButton(scr, 36, 68, 248, 44, txt("SKIP PIN", "EI PIN", "PIN AUS", "SANS PIN", "SIN PIN", "NO PIN"),
                   txt("No lock", "Ei lukitusta", "Keine Sperre", "Pas de verrou", "Sin bloqueo", "Nessun blocco"),
                   !pinEnabled,
                   BTN_OPTION_BASE + 0);
    makeMenuButton(scr, 36, 120, 248, 44, "PIN 1234",
                   txt("Enable screen lock", "Käytä lukitusta", "Sperre aktiv", "Activer verrou",
                       "Activar bloqueo", "Attiva blocco"),
                   pinEnabled && strcmp(securityPin, "1234") == 0, BTN_OPTION_BASE + 1);
    makeMenuButton(scr, 36, 172, 248, 44, "PIN 0000",
                   txt("Enable screen lock", "Käytä lukitusta", "Sperre aktiv", "Activer verrou",
                       "Activar bloqueo", "Attiva blocco"),
                   pinEnabled && strcmp(securityPin, "0000") == 0, BTN_OPTION_BASE + 2);
  } else if (submenuType == SUBMENU_RESET) {
    if (resetCountdownActive) {
      makeLabelAt(scr, 160, 66,
                  resetCountdownMode == 0
                      ? txt("FULL RESET PENDING", "TÄYSNOLLAUS ODOTTAA", "VOLLRESET AUSSTEHEND", "RÉINIT. COMPLÈTE",
                            "RESET TOTAL PENDIENTE", "RESET TOTALE IN ATTESA")
                      : txt("RESET PENDING", "NOLLAUS ODOTTAA", "RESET AUSSTEHEND", "RÉINIT. EN ATTENTE",
                            "REINICIO PENDIENTE", "RESET IN ATTESA"),
                  lv_color_white(), &lv_font_rajdhani_16, 1);
      lv_obj_t *resetHint = makeLabelAt(
          scr, 160, 96,
          txt("Settings stay unchanged until completion", "Asetukset säilyvät loppuun asti",
              "Bis zum Ende bleibt alles unverändert", "Aucun changement avant la fin",
              "Sin cambios hasta completar", "Nessuna modifica fino al termine"),
          c565(COLOR565_LABEL), &lv_font_rajdhani_12, 1);
      lv_obj_set_pos(resetHint, 24, 91);
      lv_obj_set_width(resetHint, 272);
      lv_obj_set_style_text_align(resetHint, LV_TEXT_ALIGN_CENTER, 0);
      resetProgressBar = lv_bar_create(scr);
      lv_obj_set_pos(resetProgressBar, 24, 122);
      lv_obj_set_size(resetProgressBar, 272, 20);
      lv_bar_set_range(resetProgressBar, 0, 1000);
      lv_bar_set_value(resetProgressBar, 0, LV_ANIM_OFF);
      lv_obj_set_style_radius(resetProgressBar, 5, LV_PART_MAIN);
      lv_obj_set_style_bg_color(resetProgressBar, c565(0x1082), LV_PART_MAIN);
      lv_obj_set_style_bg_opa(resetProgressBar, LV_OPA_COVER, LV_PART_MAIN);
      lv_obj_set_style_border_width(resetProgressBar, 1, LV_PART_MAIN);
      lv_obj_set_style_border_color(resetProgressBar, accentDarkLv(), LV_PART_MAIN);
      lv_obj_set_style_radius(resetProgressBar, 4, LV_PART_INDICATOR);
      lv_obj_set_style_bg_color(resetProgressBar, accentLv(), LV_PART_INDICATOR);
      resetProgressLabel = makeLabelAt(scr, 160, 151, "", accentLv(), &lv_font_rajdhani_14, 1);
      makeMenuButton(scr, 72, 178, 176, 48,
                     txt("CANCEL", "PERUUTA", "ABBRECHEN", "ANNULER", "CANCELAR", "ANNULLA"),
                     txt("Stop reset", "Keskeytä nollaus", "Reset stoppen", "Arrêter la réinit.", "Detener reinicio",
                         "Interrompi reset"),
                     false, BTN_RESET_CANCEL);
      if (!resetCountdownTimer) {
        resetCountdownTimer = lv_timer_create(resetCountdownTick, 100, NULL);
        resetCountdownTick(resetCountdownTimer);
      }
    } else if (resetConfirmMode >= 0) {
      lv_obj_t *resetQuestion = makeLabelAt(
          scr, 160, 58,
          resetConfirmMode == 0
                      ? txt("Reset settings, ride logs and battery history?",
                            "Nollataanko asetukset, ajolokit ja akkuhistoria?",
                            "Einstellungen, Fahrten und Akkuverlauf löschen?",
                            "Effacer réglages, trajets et historique batterie ?",
                            "¿Borrar ajustes, viajes e historial de batería?",
                            "Azzerare impostazioni, viaggi e cronologia batteria?")
                      : txt("Reset settings and keep ride data?", "Nollataanko asetukset ja säilytetään ajotiedot?",
                            "Einstellungen löschen und Fahrtdaten behalten?",
                            "Réinitialiser et garder les données de trajet ?",
                            "¿Reiniciar y conservar los datos de viaje?",
                            "Azzerare e mantenere i dati dei viaggi?"),
          lv_color_white(), &lv_font_rajdhani_16, 1);
      // A full sentence, and the longest translations are wider than the panel,
      // so it has to wrap rather than run off both edges.
      wrapLabel(resetQuestion, 16, 52, 288);
      makeMenuButton(scr, 24, 112, 132, 58, txt("CONFIRM", "VAHVISTA", "BESTÄTIGEN", "CONFIRMER", "CONFIRMAR",
                                                    "CONFERMA"),
                     txt("Cannot be undone", "Ei voi perua", "Nicht umkehrbar", "Irreversible", "Irreversible",
                         "Irreversibile"),
                     false, BTN_OPTION_BASE + 0);
      makeMenuButton(scr, 164, 112, 132, 58, txt("CANCEL", "PERUUTA", "ABBRECHEN", "ANNULER", "CANCELAR",
                                                     "ANNULLA"),
                     txt("Keep current settings", "Pidä asetukset", "Einstellungen behalten", "Garder réglages",
                         "Conservar ajustes", "Mantieni impostazioni"),
                     false, BTN_OPTION_BASE + 1);
    } else {
      makeMenuButton(scr, 24, 72, 272, 58, txt("FULL RESET", "TÄYSNOLLAUS", "VOLLRESET", "RÉINIT. COMPLÈTE",
                                             "RESET TOTAL", "RESET TOTALE"),
                   txt("Settings, ride logs and battery history", "Asetukset, ajolokit ja akkuhistoria",
                       "Einstellungen, Fahrten und Akkuverlauf", "Réglages, trajets et historique batterie",
                       "Ajustes, viajes e historial de batería", "Impostazioni, viaggi e cronologia batteria"),
                   false, BTN_OPTION_BASE + 0);
    makeMenuButton(scr, 24, 148, 272, 58, txt("DEFAULT RESET", "OLETUSNOLLAUS", "STANDARDRESET", "RÉINIT. DÉFAUT",
                                              "RESET NORMAL", "RESET DEFAULT"),
                   txt("Settings only - logs and battery history kept",
                       "Vain asetukset - lokit ja akkuhistoria säilyvät",
                       "Nur Einstellungen - Fahrten und Akkuverlauf bleiben",
                       "Réglages seuls - trajets et historique conservés",
                       "Solo ajustes - viajes e historial conservados",
                       "Solo impostazioni - viaggi e cronologia conservati"),
                   false, BTN_OPTION_BASE + 1);
    makeLabelAt(scr, 160, 214,
                txt("The odometer lives in the controller", "Matkamittari on ohjaimen muistissa",
                    "Kilometerzähler liegt im Controller", "L'odomètre est dans le contrôleur",
                    "El odómetro está en el controlador", "L'odometro è nel controller"),
                c565(COLOR565_DIM), &lv_font_rajdhani_12, 3);
    }
  } else if (submenuType == SUBMENU_CONFIGURATOR) {
    makeMenuButton(scr, 36, 78, 248, 58, txt("START SETUP", "ALOITA", "SETUP START", "DÉMARRER", "INICIAR",
                                             "AVVIA SETUP"),
                   txt("Run first boot wizard", "Aloita opastettu määritys", "Assistent starten", "Lancer assistant",
                       "Iniciar asistente", "Avvia procedura"),
                   false, BTN_OPTION_BASE + 0);
    makeMenuButton(scr, 36, 150, 248, 58, txt("BACK", "TAKAISIN", "ZURÜCK", "RETOUR", "ATRÁS", "INDIETRO"),
                   txt("Return to settings", "Palaa asetuksiin", "Zu Einstellungen", "Retour réglages",
                       "Volver a ajustes", "Torna impostaz."),
                   false,
                   BTN_OPTION_BASE + 1);
  } else {
    makeLabelAt(scr, 160, 118,
                txt("Not implemented yet", "Ei vielä toteutettu", "Noch nicht verfügbar", "Pas encore disponible",
                    "No implementado", "Non implementato"),
                lv_color_white(),
                &lv_font_rajdhani_14, 1);
  }
  loadScreen(scr);
}

// ── Saved ride log browser ──────────────────────────────────────────────────
// Files are ordered by their monotonic ride ID. The current binary header does
// not contain a wall-clock timestamp, so this is chronological without claiming
// calendar dates that the display cannot know yet.

static void formatRideDuration(char *out, size_t outSize, uint32_t seconds) {
  const uint32_t hours = seconds / 3600U;
  const uint32_t minutes = (seconds / 60U) % 60U;
  if (hours > 0)
    snprintf(out, outSize, "%lu:%02lu:%02lu", (unsigned long)hours, (unsigned long)minutes,
             (unsigned long)(seconds % 60U));
  else
    snprintf(out, outSize, "%02lu:%02lu", (unsigned long)minutes, (unsigned long)(seconds % 60U));
}

#include "ride_replay_screen.inc"

static void rideLogsAction(int id) {
  if(id>=kReplayRowBase && id<kReplayRowBase+3) {
    RideLogSummary entry={};
    if(rideLoggerCatalogEntry(rideLogsPage*3+id-kReplayRowBase,entry) && entry.valid) {
      replayRideId=entry.rideId; queueLowMemoryRebuild(SCREEN_RIDE_REPLAY);
    }
    return;
  }
  const RideLogCatalogStatus catalog = rideLoggerCatalogStatus();
  const uint8_t pageCount = catalog.count > 0 ? (catalog.count + 2) / 3 : 1;
  if (id == BTN_BACK) {
    queueLowMemoryRebuild(SCREEN_SUBMENU);
  } else if (id == BTN_RIDE_LOGS_PREV && rideLogsPage > 0) {
    rideLogsPage--;
    queueLowMemoryRebuild(SCREEN_RIDE_LOGS);
  } else if (id == BTN_RIDE_LOGS_NEXT && rideLogsPage + 1 < pageCount) {
    rideLogsPage++;
    queueLowMemoryRebuild(SCREEN_RIDE_LOGS);
  } else if (id == BTN_RIDE_LOGS_CLEAR_SD) {
    showClearSdCardPrompt();
  } else if (id == BTN_DEV_PROMPT_CANCEL && developerPrompt) {
    // The shared dialog routes both of its buttons through the current
    // screen's handler, so Cancel has to be answered here too.
    closeDeveloperPrompt();
  } else if (id == BTN_RIDE_LOGS_CLEAR_SD_CONFIRM) {
    closeDeveloperPrompt();
    rideReplayClose();  // a reader still holding a file the wipe is about to remove
    // The dialog follows the wipe to the end, including a request that could
    // not even be queued, which reports as a failure.
    rideLoggerWipeCard();
    showSdClearProgress();
  } else if (id == BTN_RIDE_LOGS_CLEAR_SD_DONE) {
    finishSdClear();
  }
}

static void showRideLogs() {
  lv_obj_t *scr = makeScreen();
  currentAction = rideLogsAction;
  const RideLoggingStatus storage = rideLoggerStatus();
  const RideLogCatalogStatus catalog = rideLoggerCatalogStatus();
  rideCatalogUiRevision = storage.revision ^ (catalog.revision * 2654435761UL);

  lv_obj_t *back = makeNavButton(
      scr, kMenuEdgeX, 4, txt("< BACK", "< TAKAISIN", "< ZURÜCK", "< RETOUR", "< ATRÁS", "< INDIETRO"), BTN_BACK,
      kTopBarButtonH);
  lv_obj_set_width(back, kWideTopNavW);
  makeLabelAt(scr, 160, 7, txt("RIDE LOGS", "AJOLOKIT", "FAHRTEN", "TRAJETS", "VIAJES", "VIAGGI"),
              accentLv(), &lv_font_rajdhani_14, 3);
  if (storage.cardReady && !storage.cardChecking) {
    lv_obj_t *clear = makeNavButton(scr, cyd_ui::kScreenWidth - kMenuEdgeX - kWideTopNavW, 4,
                                    txt("CLEAR SD", "TYHJENNÄ", "SD LEEREN", "VIDER SD", "BORRAR SD", "SVUOTA SD"),
                                    BTN_RIDE_LOGS_CLEAR_SD, kTopBarButtonH);
    lv_obj_set_width(clear, kWideTopNavW);
  }
  makeLabelAt(scr, 160, 27,
              txt("NEWEST FIRST", "UUSIN ENSIN", "NEUESTE ZUERST", "PLUS RÉCENTS", "MÁS RECIENTES",
                  "PIÙ RECENTI"),
              c565(COLOR565_LABEL), &lv_font_rajdhani_12, 3);

  if (!storage.cardReady || storage.cardChecking) {
    lv_obj_t *panel = makeInfoPanel(scr, 8, 58, 304, 134, 8);
    makeIcon(panel, 68, 28, CYD_ICON_WARNING, accentLv());
    makeLabelAt(panel, 92, 28,
                txt("SD CARD UNAVAILABLE", "SD-KORTTI EI SAATAVILLA", "SD-KARTE NICHT BEREIT",
                    "CARTE SD INDISPONIBLE", "TARJETA SD NO DISPONIBLE", "SCHEDA SD NON DISPONIBILE"),
                lv_color_white(), &lv_font_rajdhani_14, 0);
    makeLabelAt(panel, 152, 70,
                txt("Insert the card to browse saved rides.", "Aseta kortti selataksesi tallennettuja ajoja.",
                    "Karte einsetzen, um Fahrten anzusehen.", "Insérez la carte pour voir les trajets.",
                    "Inserta la tarjeta para ver los viajes.", "Inserisci la scheda per vedere i viaggi."),
                c565(COLOR565_LABEL), &lv_font_rajdhani_12, 3);
  } else if (catalog.loading) {
    makeLabelAt(scr, 160, 116,
                txt("LOADING RIDE LOGS...", "LADATAAN AJOLOKEJA...", "FAHRTEN WERDEN GELADEN...",
                    "CHARGEMENT DES TRAJETS...", "CARGANDO VIAJES...", "CARICAMENTO VIAGGI..."),
                c565(COLOR565_LABEL), &lv_font_rajdhani_12, 3);
  } else if (catalog.count == 0) {
    makeLabelAt(scr, 160, 105,
                txt("NO SAVED RIDES", "EI TALLENNETTUJA AJOJA", "KEINE GESPEICHERTEN FAHRTEN",
                    "AUCUN TRAJET ENREGISTRÉ", "NO HAY VIAJES GUARDADOS", "NESSUN VIAGGIO SALVATO"),
                lv_color_white(), &lv_font_rajdhani_14, 3);
    makeLabelAt(scr, 160, 132,
                txt("Completed recordings will appear here.", "Valmiit tallennukset näkyvät täällä.",
                    "Abgeschlossene Aufnahmen erscheinen hier.", "Les enregistrements apparaîtront ici.",
                    "Las grabaciones aparecerán aquí.", "Le registrazioni appariranno qui."),
                c565(COLOR565_LABEL), &lv_font_rajdhani_12, 3);
  } else {
    const uint8_t pageCount = catalog.count > 0 ? (catalog.count + 2) / 3 : 1;
    if (rideLogsPage >= pageCount) rideLogsPage = pageCount - 1;
    const uint8_t first = rideLogsPage * 3;
    for (uint8_t row = 0; row < 3 && first + row < catalog.count; row++) {
      RideLogSummary entry = {};
      if (!rideLoggerCatalogEntry(first + row, entry)) continue;
      char title[48], duration[16], details[64];
      // Demo recordings sit in the same list as real ones, so say which is which.
      snprintf(title,sizeof(title),"%s%s %lu",entry.demo?"DEMO ":"",
               txt("RIDE","AJO","FAHRT","TRAJET","VIAJE","VIAGGIO"),(unsigned long)entry.rideId);
      formatRideDuration(duration,sizeof(duration),entry.durationSeconds);
      // The writer never hands out the file it is still appending to, so the
      // open ride is listed but cannot be replayed until it closes.
      const bool recording=storage.recording && entry.rideId==storage.rideId;
      if(recording) {
        const size_t used=strlen(title);
        snprintf(title+used,sizeof(title)-used," | %s",txt("RECORDING","TALLENTAA","AUFNAHME","ENREGISTRE","GRABANDO","IN REGISTRAZIONE"));
        snprintf(details,sizeof(details),"%s",txt("Replay opens when the ride ends","Toisto aukeaa, kun ajo päättyy",
                 "Wiedergabe nach Fahrtende","Relecture à la fin du trajet","Repetición al terminar el viaje",
                 "Riproduzione a fine viaggio"));
      }
      else if(entry.valid) snprintf(details,sizeof(details),"%.1f %s | %s | %.0f Wh",displayDistance(entry.distanceMeters/1000.0F),distanceUnitLabel(),duration,entry.netWhDeci/10.0F);
      else snprintf(details,sizeof(details),"%s",txt("Unreadable log","Virheellinen loki","Unlesbare Datei","Journal illisible","Registro ilegible","Registro illeggibile"));
      makeMenuButton(scr,8,52+row*49,304,43,title,details,false,entry.valid && !recording?kReplayRowBase+row:0);
    }
    char pageText[20];
    snprintf(pageText, sizeof(pageText), "%u / %u   |   %u %s", rideLogsPage + 1, pageCount, catalog.count,
             txt("rides", "ajoa", "Fahrten", "trajets", "viajes", "viaggi"));
    makeLabelAt(scr, 160, 216, pageText, c565(COLOR565_LABEL), &lv_font_rajdhani_12, 3);
    if (rideLogsPage > 0) makeSmallNavButton(scr, kMenuEdgeX, 204, "<", BTN_RIDE_LOGS_PREV, 30);
    if (rideLogsPage + 1 < pageCount)
      makeSmallNavButton(scr, cyd_ui::kScreenWidth - kMenuEdgeX - 34, 204, ">", BTN_RIDE_LOGS_NEXT, 30);
  }
  loadScreen(scr);
}

// ── Full-screen touch test ──────────────────────────────────────────────────
// A one-bit canvas keeps the test inexpensive (~9.6 kB) and lets every sample
// be recorded as exactly one physical display pixel. Updating the image buffer
// directly also means only that pixel needs to be invalidated and flushed.

static void clearTouchTestCanvas() {
  if (!touchTestCanvas) return;
  lv_canvas_fill_bg(touchTestCanvas, lv_color_black(), LV_OPA_COVER);
}

static void plotTouchTestPoint(lv_coord_t x, lv_coord_t y) {
  if (!touchTestCanvas || x < 0 || x >= 320 || y < 0 || y >= 240) return;
  lv_img_buf_set_px_color(lv_canvas_get_img(touchTestCanvas), x, y, accentLv());
  const lv_area_t dirty = {x, y, x, y};
  lv_obj_invalidate_area(touchTestCanvas, &dirty);
}

static void animateTouchTestChrome(bool hide) {
  if (touchTestChromeHidden == hide) return;
  touchTestChromeHidden = hide;
  // A new contact may arrive during the brief idle reveal. Stop that direction
  // before starting the reverse so two animations never fight over one y value.
  lv_anim_del(touchTestBack, selectorOverlayAnimCb);
  lv_anim_del(touchTestTitle, selectorOverlayAnimCb);
  lv_anim_del(touchTestClear, selectorOverlayAnimCb);
  lv_anim_del(touchTestInstruction, selectorOverlayAnimCb);
  // Same 140 ms direct ease-out motion as the theme selector: top chrome
  // leaves upward and the helper caption leaves through the bottom edge.
  slideSelectorPart(touchTestBack, hide ? -48 : 4, !hide);
  slideSelectorPart(touchTestTitle, hide ? -48 : 10, !hide);
  slideSelectorPart(touchTestClear, hide ? -48 : 4, !hide);
  slideSelectorPart(touchTestInstruction, hide ? 244 : 224, !hide);
}

static void touchTestChromeTimerCb(lv_timer_t *) {
  if (currentScreen != SCREEN_TOUCH_TEST || !touchTestChromeHidden) return;
  if ((uint32_t)(millis() - touchTestLastInputMs) >= 1000U) animateTouchTestChrome(false);
}

static void touchTestCanvasEvent(lv_event_t *event) {
  const lv_event_code_t code = lv_event_get_code(event);
  if (code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING) {
    touchTestLastInputMs = millis();
    animateTouchTestChrome(true);
  } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
    // Start the complete one-second quiet period from the final contact.
    touchTestLastInputMs = millis();
    return;
  } else {
    return;
  }
  lv_indev_t *indev = lv_event_get_indev(event);
  if (!indev) return;
  lv_point_t point;
  lv_indev_get_point(indev, &point);
  plotTouchTestPoint(point.x, point.y);
}

static void touchTestCanvasDeleteEvent(lv_event_t *event) {
  free(lv_event_get_user_data(event));
  if (lv_event_get_target(event) == touchTestCanvas) touchTestCanvas = NULL;
}

static void touchTestAction(int id) {
  if (id == BTN_BACK) {
    queueRebuild(SCREEN_SUBMENU);
  } else if (id == BTN_OPTION_BASE) {
    clearTouchTestCanvas();
  }
}

static void showTouchTest() {
  lv_obj_t *scr = makeScreen();
  currentAction = touchTestAction;

  uint8_t *canvasBuffer = static_cast<uint8_t *>(malloc(LV_CANVAS_BUF_SIZE_INDEXED_1BIT(320, 240)));
  if (canvasBuffer) {
    touchTestCanvas = lv_canvas_create(scr);
    lv_canvas_set_buffer(touchTestCanvas, canvasBuffer, 320, 240, LV_IMG_CF_INDEXED_1BIT);
    lv_canvas_set_palette(touchTestCanvas, 0, lv_color_black());
    lv_canvas_set_palette(touchTestCanvas, 1, accentLv());
    lv_obj_set_pos(touchTestCanvas, 0, 0);
    lv_obj_add_flag(touchTestCanvas, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(touchTestCanvas, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(touchTestCanvas, touchTestCanvasEvent, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(touchTestCanvas, touchTestCanvasDeleteEvent, LV_EVENT_DELETE, canvasBuffer);
    clearTouchTestCanvas();
  } else {
    makeLabelAt(scr, 160, 120,
                txt("Not enough memory for touch test", "Muisti ei riitä kosketustestiin", "Nicht genug Speicher",
                    "Mémoire insuffisante", "Memoria insuficiente", "Memoria insufficiente"),
                lv_color_white(), &lv_font_rajdhani_12, 3);
  }

#ifdef CYD_LVGL_PREVIEW
  // A small sample stroke makes the headless preview useful without a touch
  // device; firmware starts with a completely blank canvas.
  for (int i = 0; i < 90; i++) plotTouchTestPoint(115 + i, 132 + (i * i) / 900);
#endif

  touchTestBack = makeNavButton(
      scr, kMenuEdgeX, 4, txt("< BACK", "< TAKAISIN", "< ZURÜCK", "< RETOUR", "< ATRÁS", "< INDIETRO"), BTN_BACK,
      kTopBarButtonH);
  lv_obj_set_width(touchTestBack, kWideTopNavW);
  touchTestTitle = makeLabelAt(
      scr, 160, 10,
      txt("TOUCH TEST", "KOSKETUSTESTI", "TOUCH-TEST", "TEST TACTILE", "PRUEBA TÁCTIL", "TEST TOUCH"),
      accentLv(), &lv_font_rajdhani_14, 3);
  touchTestClear = makeNavButton(
      scr, 250, 4, txt("CLEAR", "TYHJENNÄ", "LÖSCHEN", "EFFACER", "BORRAR", "PULISCI"), BTN_OPTION_BASE,
      kTopBarButtonH);
  lv_obj_set_width(touchTestClear, 68);
  touchTestInstruction = makeLabelAt(
      scr, 160, 224,
      txt("Drag anywhere to draw detected points", "Piirrä vetämällä missä tahansa",
          "Ziehen, um Touch-Punkte zu zeichnen", "Glissez pour dessiner les points",
          "Arrastre para dibujar puntos", "Trascina per disegnare i punti"),
      c565(COLOR565_LABEL), &lv_font_rajdhani_12, 3);
  touchTestChromeHidden = false;
  touchTestLastInputMs = millis();
  touchTestChromeTimer = lv_timer_create(touchTestChromeTimerCb, 100, NULL);
  loadScreen(scr);
}

// ── First-boot configurator ───────────────────────────────────────────────────

static void finishConfigurator() {
  firstBootSetupActive = false;
  firstBootConfigured = true;
  saveAppSettings();
  queueStatusNotice(NOTICE_SETUP_COMPLETE);
  queueRebuild(SCREEN_DASHBOARD);
}

static void configAction(int id) {
  // The last wizard step is the same full-screen selector used by DASH UI in
  // Settings. Its side arrows and colour palette therefore keep their normal
  // meanings, while SAVE becomes the wizard's DONE action.
  if (configStep == 5) {
    if (id >= BTN_DASH_GRID_BASE && id < BTN_DASH_GRID_BASE + kDashThemeCount) {
      dashUiGridReturnPage = dashUiGridPage;
      dashUiPreviewMode = static_cast<DashboardMode>(id - BTN_DASH_GRID_BASE);
      applyDashboardCustomization(dashUiPreviewMode);
      dashUiGridOpen = false;
      colorPaletteOpen = false;
      gradientPanelOpen = false;
      dataPanelOpen = false;
      dataChoiceOpen = false;
      queueLowMemoryRebuild(SCREEN_CONFIG);
      return;
    }
    if (dashUiGridOpen && id == BTN_NEXT) {
      if (dashUiGridPage + 1 < kDashGridPageCount) {
        dashUiGridPage++;
        queueLowMemoryRebuild(SCREEN_CONFIG);
      }
      return;
    }
    if (id == BTN_COLORS_MASTER) {
      toggleColorsMaster();
      return;
    }
    if (id == BTN_RESET_CURRENT_UI) {
      resetDashboardCustomization(dashUiPreviewMode);
      dashboardCustomizationBackup[dashUiPreviewMode] = dashboardCustomizations[dashUiPreviewMode];
      saveDashboardCustomizationProfiles();
      dataChoiceOpen = false;
      queueLowMemoryRebuild(SCREEN_CONFIG);
      return;
    }
    if (id == BTN_COLOR_TOGGLE) {
      toggleSelectorPalette();
      return;
    }
    if (id >= BTN_COLOR_BASE && id < BTN_COLOR_BASE + ACCENT_COUNT) {
      accentTheme = static_cast<AccentTheme>(id - BTN_COLOR_BASE);
      captureDashboardCustomization(dashUiPreviewMode);
      queueLowMemoryRebuild(SCREEN_CONFIG);
      return;
    }
    if (id >= BTN_THEME_DARK && id <= BTN_THEME_AUTO) {
      dashboardAppearanceMode = static_cast<DashboardAppearanceMode>(id - BTN_THEME_DARK);
      captureDashboardCustomization(dashUiPreviewMode);
      autoBrightnessPromptOpen = dashboardAppearanceMode == DASH_APPEARANCE_AUTO && !autoBrightnessEnabled;
      queueLowMemoryRebuild(SCREEN_CONFIG);
      return;
    }
    if (id == BTN_AUTO_BRIGHTNESS_YES || id == BTN_AUTO_BRIGHTNESS_NO) {
      if (id == BTN_AUTO_BRIGHTNESS_YES) {
        autoBrightnessEnabled = true;
        saveAutoBrightnessSetting();
      }
      autoBrightnessPromptOpen = false;
      rebuildSelectorPopup();
      return;
    }
    if (id == BTN_GRADIENT_TOGGLE) {
      openSelectorGradientPanel();
      return;
    }
    if (id == BTN_DATA_TAB) {
      openSelectorDataPanel();
      return;
    }
    if (id == BTN_DATA_BACK) {
      dataChoiceOpen = false;
      rebuildSelectorPopup();
      return;
    }
    if (id == BTN_DATA_UP || id == BTN_DATA_DOWN) {
      const int direction = id == BTN_DATA_UP ? -1 : 1;
      uint8_t &offset = dataChoiceOpen ? dataChoiceOffset : dataListOffset;
      const int count = dataChoiceOpen ? DATA_COUNT : dashboardDataSlotCount(dashUiPreviewMode);
      offset = static_cast<uint8_t>(constrain((int)offset + direction, 0, max(0, count - 3)));
      rebuildSelectorPopup();
      return;
    }
    if (id >= BTN_DATA_SLOT_BASE && id < BTN_DATA_SLOT_BASE + DASH_DATA_SLOTS_MAX) {
      dataSelectedSlot = static_cast<uint8_t>(id - BTN_DATA_SLOT_BASE);
      dataChoiceOpen = true;
      const int selected = dashboardDataSelection(dashUiPreviewMode, dataSelectedSlot);
      dataChoiceOffset = static_cast<uint8_t>(constrain(selected - 1, 0, max(0, (int)DATA_COUNT - 3)));
      rebuildSelectorPopup();
      return;
    }
    if (id >= BTN_DATA_CHOICE_BASE && id < BTN_DATA_CHOICE_BASE + DATA_COUNT) {
      setDashboardDataSelection(dashUiPreviewMode, dataSelectedSlot,
                                static_cast<DashboardDataItem>(id - BTN_DATA_CHOICE_BASE));
      dataChoiceOpen = false;
      queueLowMemoryRebuild(SCREEN_CONFIG);
      return;
    }
    if (id == BTN_GRADIENT_BACK) {
      gradientPanelOpen = false;
      slideSelectorPart(selectorArrowLeft, 3, true, true);
      slideSelectorPart(selectorArrowRight, 279, true, true);
      rebuildSelectorPopup();
      return;
    }
    if (id == BTN_GRADIENT_ENABLE) {
      dashboardGradientEnabled = !dashboardGradientEnabled;
      rebuildSelectorPopup();
      return;
    }
    if (id == BTN_GRADIENT_ORIENTATION) {
      dashboardGradientHorizontal = !dashboardGradientHorizontal;
      rebuildSelectorPopup();
      return;
    }
    if (id == BTN_GRADIENT_BELL) {
      dashboardGradientBell = !dashboardGradientBell;
      rebuildSelectorPopup();
      return;
    }
    if (id >= BTN_GRADIENT_COLOR_BASE && id < BTN_GRADIENT_COLOR_BASE + ACCENT_COUNT) {
      dashboardGradientTheme = static_cast<AccentTheme>(id - BTN_GRADIENT_COLOR_BASE);
      captureDashboardCustomization(dashUiPreviewMode);
      queueLowMemoryRebuild(SCREEN_CONFIG);
      return;
    }
    if (id >= BTN_BACKGROUND_STYLE_BASE && id < BTN_BACKGROUND_STYLE_BASE + 4) {
      selectOrRotateBackgroundStyle(id - BTN_BACKGROUND_STYLE_BASE);
      captureDashboardCustomization(dashUiPreviewMode);
      queueLowMemoryRebuild(SCREEN_CONFIG);
      return;
    }
    if (id == BTN_PREV || id == BTN_NEXT) {
      captureDashboardCustomization(dashUiPreviewMode);
      const int delta = id == BTN_PREV ? kDashThemeCount - 1 : 1;
      dashUiPreviewMode = static_cast<DashboardMode>((static_cast<int>(dashUiPreviewMode) + delta) % kDashThemeCount);
      applyDashboardCustomization(dashUiPreviewMode);
      queueLowMemoryRebuild(SCREEN_CONFIG);
      return;
    }
    if (id == BTN_SAVE) {
      captureDashboardCustomization(dashUiPreviewMode);
      dashboardMode = dashUiPreviewMode;
      applyDashboardCustomization(dashboardMode);
      finishConfigurator();
      return;
    }
    if (id == BTN_BACK) {
      if (dashUiGridOpen && dashUiGridPage > 0) {
        dashUiGridPage--;
        queueLowMemoryRebuild(SCREEN_CONFIG);
        return;
      }
      restoreDashboardCustomizations();
      if (!dashUiGridOpen) {
        dashUiPreviewMode = dashboardMode;
        applyDashboardCustomization(dashUiPreviewMode);
        dashUiGridOpen = true;
        dashUiGridPage = dashUiGridReturnPage;
        colorPaletteOpen = false;
        gradientPanelOpen = false;
        dataPanelOpen = false;
        dataChoiceOpen = false;
        queueLowMemoryRebuild(SCREEN_CONFIG);
        return;
      }
      dashUiPreviewMode = dashboardMode;
      configStep = 4;
      applyDashboardCustomization(dashboardMode);
      pinSetupFromConfigurator = true;
      queueRebuild(SCREEN_PIN_SETUP);
      return;
    }
    return;
  }

  if (id == BTN_BACK) {
    configStep = configStep > 1 ? configStep - 1 : 0;
    queueRebuild(SCREEN_CONFIG);
    return;
  }
  if (id == BTN_NEXT) {
    configStep++;
    if (configStep == 4) {
      pinSetupFromConfigurator = true;
      queueRebuild(SCREEN_PIN_SETUP);
      return;
    }
    if (configStep == 5) {
      dashUiPreviewMode = dashboardMode;
      dashUiGridOpen = true;
      dashUiGridPage = 0;
      dashUiGridReturnPage = 0;
      backupDashboardCustomizations();
      accentThemeBackup = accentTheme;
      gradientEnabledBackup = dashboardGradientEnabled;
      gradientHorizontalBackup = dashboardGradientHorizontal;
      gradientReverseBackup = dashboardGradientReverse;
      gradientBellBackup = dashboardGradientBell;
      gradientPositionBackup = dashboardGradientPosition;
      gradientThemeBackup = dashboardGradientTheme;
      colorPaletteOpen = false;
      gradientPanelOpen = false;
      dataPanelOpen = false;
      dataChoiceOpen = false;
    }
    if (configStep == 5)
      queueLowMemoryRebuild(SCREEN_CONFIG);
    else
      queueRebuild(SCREEN_CONFIG);
    return;
  }
  const int option = id - BTN_OPTION_BASE;
  if (option < 0) return;

  if (configStep == 0) {
    if (option == 0) {
      firstBootSetupActive = true;
      submenuType = SUBMENU_LANGUAGE;
      queueRebuild(SCREEN_SUBMENU);
    } else {
      finishConfigurator();
    }
    return;
  }
  if (configStep == 1) {
    language = static_cast<Language>(constrain(option, 0, static_cast<int>(LANG_COUNT) - 1));
  } else if (configStep == 2) {
    unitMode = static_cast<UnitMode>(constrain(option, 0, 3));
  } else if (configStep == 3) {
    if (option == 0) {
      textInputContext = INPUT_CONFIG_WHEEL;
      queueRebuild(SCREEN_TEXT_INPUT);
      return;
    }
  }
  queueRebuild(SCREEN_CONFIG);
}

static void showConfigurator() {
  // SCREEN_CONFIG now owns only the welcome decision. Every subsequent setup
  // page is the normal settings submenu, so stale wizard step state must not
  // resurrect the retired speed/PIN copies.
  configStep = 0;
  lv_obj_t *scr = makeScreen();
  currentAction = configAction;

  if (configStep == 0) {
    makeLabelAt(scr, 160, 24,
                txt("FIRST BOOT", "ENSIKÄYNNISTYS", "ERSTER START", "PREMIER DEM.", "PRIMER INICIO", "PRIMO AVVIO"),
                accentLv(), &lv_font_rajdhani_24, 3);
    makeLabelAt(scr, 160, 68,
                txt("Run new user configuration?", "Aloitetaanko asetukset?", "Einrichtung starten?",
                    "Lancer la configuration?", "¿Iniciar configuración?", "Avviare la configurazione?"),
                lv_color_white(),
                &lv_font_rajdhani_14, 3);
    makeMenuButton(scr, 30, 104, 260, 78,
                   txt("START SETUP", "ALOITA", "SETUP START", "DÉMARRER", "INICIAR", "AVVIA SETUP"),
                   txt("Recommended", "Suositus", "Empfohlen", "Recommandé", "Recomendado", "Consigliato"), false,
                   BTN_OPTION_BASE + 0);
    lv_obj_t *skip = makeNavButton(
        scr, kWideTopNavRightX, 202,
        txt("SKIP", "OHITA", "ÜBERSPR.", "IGNORER", "OMITIR", "SALTA"), BTN_OPTION_BASE + 1, 36);
    lv_obj_set_width(skip, kWideTopNavW);
    loadScreen(scr);
    return;
  }

  char stepText[8];
  snprintf(stepText, sizeof(stepText), "%u/5", configStep);

  if (configStep == 5) {
    const char *dashTitle = txt("THEME", "TEEMA", "DESIGN", "THÈME", "TEMA", "TEMA");
    if (dashUiGridOpen)
      showDashUiGrid(scr, dashTitle);
    else
      showDashUiSelector(scr, dashTitle, static_cast<uint8_t>(dashUiPreviewMode) + 1, kDashThemeCount,
                         modeName(dashUiPreviewMode), txt("DONE", "VALMIS", "FERTIG", "TERMINÉ", "HECHO", "FINE"));
    loadScreen(scr);
    return;
  }

  makeLabelAt(scr, 160, 8, stepText, accentLv(), &lv_font_rajdhani_14, 3);

  if (configStep == 1) {
    makeLabelAt(scr, 160, 34, txt("LANGUAGE", "KIELI", "SPRACHE", "LANGUE", "IDIOMA", "LINGUA"), accentLv(),
                &lv_font_rajdhani_24, 3);
    makeMenuButton(scr, kMenuEdgeX, 64, kTwoColLeftW, 34, "English", "", language == LANG_EN, BTN_OPTION_BASE + LANG_EN);
    makeMenuButton(scr, kTwoColRightX, 64, kTwoColRightW, 34, "Suomi", "", language == LANG_FI, BTN_OPTION_BASE + LANG_FI);
    makeMenuButton(scr, kMenuEdgeX, 106, kTwoColLeftW, 34, "Deutsch", "", language == LANG_DE, BTN_OPTION_BASE + LANG_DE);
    makeMenuButton(scr, kTwoColRightX, 106, kTwoColRightW, 34, "Français", "", language == LANG_FR, BTN_OPTION_BASE + LANG_FR);
    makeMenuButton(scr, kMenuEdgeX, 148, kTwoColLeftW, 34, "Español", "", language == LANG_ES, BTN_OPTION_BASE + LANG_ES);
    makeMenuButton(scr, kTwoColRightX, 148, kTwoColRightW, 34, "Italiano", "", language == LANG_IT, BTN_OPTION_BASE + LANG_IT);
  } else if (configStep == 2) {
    makeLabelAt(scr, 160, 34, txt("UNITS", "YKSIKÖT", "EINHEIT.", "UNITÉS", "UNIDADES", "UNITÀ"), accentLv(),
                &lv_font_rajdhani_24, 3);
    makeMenuButton(scr, kMenuEdgeX, 86, kTwoColLeftW, 42, txt("Metric", "Metrinen", "Metrisch", "Métrique", "Métrico", "Metrico"),
                   "km/h", unitMode == UNITS_METRIC, BTN_OPTION_BASE + 0);
    makeMenuButton(scr, kTwoColRightX, 86, kTwoColRightW, 42,
                   txt("Imperial", "Imperiaalinen", "Imperial", "Imperial", "Imperial", "Imperiale"), "mph",
                   unitMode == UNITS_IMPERIAL, BTN_OPTION_BASE + 1);
    makeMenuButton(scr, kMenuEdgeX, 140, kTwoColLeftW, 42,
                   txt("Nautical", "Merenkulku", "Nautisch", "Nautique", "Náutico", "Nautico"), "kn",
                   unitMode == UNITS_NAUTICAL, BTN_OPTION_BASE + 2);
    makeMenuButton(scr, kTwoColRightX, 140, kTwoColRightW, 42, "Mach",
                   txt("Mach number", "Mach-luku", "Machzahl", "Nombre de Mach", "Número Mach", "Numero Mach"),
                   unitMode == UNITS_MACH, BTN_OPTION_BASE + 3);
  } else if (configStep == 3) {
    // Same swap as the VESC page: setup must not ask for a wheel size the
    // running controller has already made irrelevant. Before any controller has
    // answered this falls through to the wheel, which is what the eRPM path
    // will need if none ever does.
    const bool trim = telemetrySpeedFromController();
    char wheelText[18];
    if (trim) {
      snprintf(wheelText, sizeof(wheelText), "%u.%02ux", speedCalibrationPercent / 100,
               speedCalibrationPercent % 100);
    } else {
      snprintf(wheelText, sizeof(wheelText), "%u mm", wheelDiameterMm);
    }
    makeLabelAt(scr, 160, 34,
                trim ? txt("SPEED CALIBRATION", "NOPEUDEN KALIBROINTI", "TEMPO-KALIBRIERUNG",
                           "CALIBRAGE VITESSE", "CALIBRACIÓN VELOCIDAD", "CALIBRAZIONE VELOCITÀ")
                     : txt("WHEEL DIAMETER", "RENKAAN KOKO", "RADDURCHMESSER", "DIAMÈTRE ROUE",
                           "DIÁMETRO RUEDA", "DIAMETRO RUOTA"),
                accentLv(), &lv_font_rajdhani_14, 3);
    makeMenuButton(scr, 36, 82, 248, 82, wheelText,
                   txt("Tap to enter with number pad", "Syötä numeronäppäimillä", "Mit Ziffernblock eingeben",
                       "Saisir avec le pavé numérique", "Introducir con teclado numérico",
                       "Inserisci con tastierino"),
                   true, BTN_OPTION_BASE + 0);
  }

  lv_obj_t *back = makeNavButton(scr, kMenuEdgeX, 196,
                                 txt("< BACK", "< TAKAISIN", "< ZURÜCK", "< RETOUR", "< ATRÁS", "< INDIETRO"),
                                 BTN_BACK, 42);
  lv_obj_set_size(back, 104, 42);
  lv_obj_t *next = makeNavButton(scr, cyd_ui::kScreenWidth - kMenuEdgeX - 104, 196,
                                 txt("NEXT >", "SEUR >", "WEITER >", "SUIV >", "SIG >", "AVANTI >"), BTN_NEXT,
                                 42);
  lv_obj_set_size(next, 104, 42);
  loadScreen(scr);
}

// ── PIN lock ──────────────────────────────────────────────────────────────────

static lv_obj_t *pinMaskLabel = NULL;
static lv_obj_t *pinNoticeLabel = NULL;
static bool pinShowWrong = false;
// Failures are counted in RAM only. Persisting them would cost an NVS write per
// wrong digit-guess, and on a board anyone can power-cycle the counter buys
// deterrence rather than security; the delay is there to stop a bystander
// walking the 10 000 combinations, not a workshop.
static uint8_t pinFailures = 0;
static uint32_t pinLockedUntilMs = 0;
static lv_timer_t *pinLockTimer = NULL;
static const uint8_t kPinFailuresPerLockout = 3;

static void prepareConfiguratorThemeStep() {
  dashUiPreviewMode = dashboardMode;
  dashUiGridOpen = true;
  dashUiGridPage = 0;
  dashUiGridReturnPage = 0;
  backupDashboardCustomizations();
  accentThemeBackup = accentTheme;
  gradientEnabledBackup = dashboardGradientEnabled;
  gradientHorizontalBackup = dashboardGradientHorizontal;
  gradientReverseBackup = dashboardGradientReverse;
  gradientBellBackup = dashboardGradientBell;
  gradientPositionBackup = dashboardGradientPosition;
  gradientThemeBackup = dashboardGradientTheme;
  colorPaletteOpen = false;
  gradientPanelOpen = false;
  dataPanelOpen = false;
  dataChoiceOpen = false;
}

static void pinSetupAction(int id) {
  if (id == BTN_BACK) {
    if (pinSetupFromConfigurator) {
      configStep = 3;
      queueRebuild(SCREEN_CONFIG);
    } else {
      queueRebuild(SCREEN_MENU);
    }
    return;
  }
  if (id == BTN_NEXT && pinSetupFromConfigurator) {
    prepareConfiguratorThemeStep();
    queueLowMemoryRebuild(SCREEN_CONFIG);
    return;
  }
  if (id == BTN_PIN_SETUP_DISABLE) {
    pinEnabled = false;
    pendingSecurityPin[0] = '\0';
    saveAppSettings();
    queueRebuild(SCREEN_PIN_SETUP);
    return;
  }
  if (id == BTN_PIN_SETUP_CHANGE) {
    pendingSecurityPin[0] = '\0';
    pinEntryMismatch = false;
    textInputContext = INPUT_CONFIG_PIN_NEW;
    queueRebuild(SCREEN_TEXT_INPUT);
  }
}

static void showPinSetup() {
  lv_obj_t *scr = makeScreen();
  currentAction = pinSetupAction;
  makeMenuTopBar(scr,
                 txt("PIN SECURITY", "PIN-SUOJAUS", "PIN-SCHUTZ", "SÉCURITÉ PIN", "SEGURIDAD PIN",
                     "SICUREZZA PIN"),
                 0, 0, pinSetupFromConfigurator);

  makeLabelAt(scr, 160, 51,
              pinEnabled
                  ? txt("PIN required when Settings is opened", "PIN kysytään asetuksia avattaessa",
                        "PIN beim Öffnen der Einstellungen", "PIN requis pour ouvrir les réglages",
                        "PIN necesario para abrir ajustes", "PIN richiesto per aprire impostazioni")
                  : txt("Settings currently opens without a PIN", "Asetukset avautuvat ilman PIN-koodia",
                        "Einstellungen öffnen ohne PIN", "Les réglages s'ouvrent sans PIN",
                        "Los ajustes se abren sin PIN", "Le impostazioni si aprono senza PIN"),
              c565(COLOR565_LABEL), &lv_font_rajdhani_12, 3);

  makeMenuButton(scr, 20, 76, 280, 62,
                 txt("NO PIN", "EI PIN-KOODIA", "KEINE PIN", "SANS PIN", "SIN PIN", "NESSUN PIN"),
                 txt("Open Settings directly", "Avaa asetukset suoraan", "Einstellungen direkt öffnen",
                     "Ouvrir directement", "Abrir directamente", "Apri direttamente"),
                 !pinEnabled, BTN_PIN_SETUP_DISABLE);
  makeMenuButton(scr, 20, 148, 280, 62,
                 pinEnabled
                     ? txt("CHANGE 4-DIGIT PIN", "VAIHDA 4-NUM. PIN", "4-STELLIGE PIN ÄNDERN",
                           "CHANGER LE PIN", "CAMBIAR PIN", "CAMBIA PIN")
                     : txt("SET 4-DIGIT PIN", "ASETA 4-NUM. PIN", "4-STELLIGE PIN SETZEN",
                           "DÉFINIR LE PIN", "CONFIGURAR PIN", "IMPOSTA PIN"),
                 txt("Enter it twice to confirm", "Syötä kahdesti vahvistukseksi", "Zur Bestätigung zweimal",
                     "Saisir deux fois", "Introducir dos veces", "Inserisci due volte"),
                 pinEnabled, BTN_PIN_SETUP_CHANGE);
  loadScreen(scr);
}

static void cancelPinLockoutTimer() {
  if (pinLockTimer) {
    lv_timer_del(pinLockTimer);
    pinLockTimer = NULL;
  }
}

static bool pinLockoutActive() {
  return pinLockedUntilMs != 0 && (int32_t)(pinLockedUntilMs - millis()) > 0;
}

static void refreshPinNotice() {
  if (!pinNoticeLabel) return;
  char text[48];
  if (pinLockoutActive()) {
    const uint32_t left = (pinLockedUntilMs - millis() + 999) / 1000;
    snprintf(text, sizeof(text), "%s %us",
             txt("TRY AGAIN IN", "YRITÄ UUDELLEEN", "ERNEUT IN", "RÉESSAYER DANS", "REINTENTAR EN",
                 "RIPROVA TRA"),
             (unsigned)left);
    lv_obj_set_style_text_color(pinNoticeLabel, c565(0xFB40), 0);
  } else if (pinShowWrong) {
    snprintf(text, sizeof(text), "%s",
             txt("WRONG PIN", "VÄÄRÄ PIN", "FALSCHE PIN", "PIN INCORRECT", "PIN INCORRECTO", "PIN ERRATO"));
    lv_obj_set_style_text_color(pinNoticeLabel, c565(0xF9C0), 0);
  } else {
    text[0] = '\0';
  }
  setLabelText(pinNoticeLabel, text);
}

static void pinLockoutTick(lv_timer_t *) {
  refreshPinNotice();
  if (!pinLockoutActive()) {
    pinLockedUntilMs = 0;
    cancelPinLockoutTimer();
    refreshPinNotice();
  }
}

static void startPinLockout(uint32_t seconds) {
  pinLockedUntilMs = millis() + seconds * 1000;
  cancelPinLockoutTimer();
  pinLockTimer = lv_timer_create(pinLockoutTick, 250, NULL);
}

static void updatePinMask() {
  if (!pinMaskLabel) return;
  char masked[8] = "";
  for (size_t i = 0; i < strlen(pinInput); i++) strcat(masked, "*");
  setLabelText(pinMaskLabel, masked);
}

static void pinAction(int id) {
  if (id == BTN_BACK) {
    pinShowWrong = false;
    pinUnlockOpensControllerSetup = false;
    queueRebuild(SCREEN_DASHBOARD);
    return;
  }
  const int key = id - BTN_OPTION_BASE;
  if (key < 0 || key > 11) return;
  if (pinLockoutActive()) return;  // keypad stays cold until the countdown ends
  size_t len = strlen(pinInput);
  if (key <= 8) {
    if (len < 4) {
      pinInput[len] = '1' + key;
      pinInput[len + 1] = '\0';
    }
    pinShowWrong = false;
  } else if (key == 9) {
    // One digit, not the whole code: mistyping the last of four should not
    // cost the other three.
    if (len > 0) pinInput[len - 1] = '\0';
    pinShowWrong = false;
  } else if (key == 10) {
    if (len < 4) {
      pinInput[len] = '0';
      pinInput[len + 1] = '\0';
    }
    pinShowWrong = false;
  } else {
    if (strcmp(pinInput, securityPin) == 0) {
      pinShowWrong = false;
      pinFailures = 0;
      pinLockedUntilMs = 0;
      cancelPinLockoutTimer();
      if (pinUnlockOpensControllerSetup) {
        pinUnlockOpensControllerSetup = false;
        openControllerSetup();
        return;
      }
      settingsMenuLevel = SETTINGS_MENU_DISPLAY;
      queueRebuild(SCREEN_MENU);
      return;
    }
    // Clearing the field silently was indistinguishable from a missed OK press,
    // so a wrong code now says so.
    pinInput[0] = '\0';
    pinShowWrong = true;
    if (pinFailures < 255) pinFailures++;
    if (pinFailures % kPinFailuresPerLockout == 0) {
      uint32_t seconds = 10 * (pinFailures / kPinFailuresPerLockout);
      if (seconds > 60) seconds = 60;
      startPinLockout(seconds);
    }
  }
  updatePinMask();
  refreshPinNotice();
}

static void showPinLock() {
  lv_obj_t *scr = makeScreen();
  currentAction = pinAction;
  pinInput[0] = '\0';

  lv_obj_t *back = makeNavButton(
      scr, kMenuEdgeX, 3, txt("< BACK", "< TAKAISIN", "< ZURÜCK", "< RETOUR", "< ATRÁS", "< INDIETRO"), BTN_BACK,
      kTopBarButtonH);
  lv_obj_set_width(back, kWideTopNavW);
  makeLabelAt(scr, 160, 4, txt("ENTER PIN", "SYÖTÄ PIN", "PIN EINGEBEN", "ENTRER PIN", "INTRODUCIR PIN",
                               "INSERISCI PIN"),
              accentLv(), &lv_font_rajdhani_20, 3);
  // The mask used to sit at y=54, which a full four asterisks overlapped the
  // top key row with. Header and notice now share the band above the keypad.
  pinMaskLabel = makeLabelAt(scr, 160, 28, "", lv_color_white(), &lv_font_rajdhani_24, 3);
  pinNoticeLabel = makeLabelAt(scr, 160, 61, "", c565(0xF9C0), &lv_font_rajdhani_12, 3);

  const char *keys[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "DEL", "0", "OK"};
  for (int i = 0; i < 12; i++) {
    const int col = i % 3;
    const int row = i / 3;
    makeMenuButton(scr, 18 + col * 101, 78 + row * 40, 86, 38, keys[i], "", false, BTN_OPTION_BASE + i);
  }
  updatePinMask();
  // A lockout that was running when the screen was built keeps its countdown
  if (pinLockoutActive() && !pinLockTimer) pinLockTimer = lv_timer_create(pinLockoutTick, 250, NULL);
  refreshPinNotice();
  loadScreen(scr);
}

// ── Text input ────────────────────────────────────────────────────────────────

static void finishTextEdit(bool save) {
  const char *text = vehicleTextArea ? lv_textarea_get_text(vehicleTextArea) : "";
  const bool editingPin = textInputContext == INPUT_CONFIG_PIN_NEW || textInputContext == INPUT_CONFIG_PIN_CONFIRM;
  if (save && textInputContext == INPUT_VEHICLE_FIELD && vehicleTextField >= 0) {
    saveVehicleInputValue(vehicleTextField, text);
  } else if (save && textInputContext == INPUT_CONFIG_WHEEL) {
    if (telemetrySpeedFromController()) {
      speedCalibrationPercent = constrain(atoi(text), 50, 200);
    } else {
      wheelDiameterMm = constrain(atoi(text), 300, 1000);
    }
    saveVehicleProfile();
  } else if (save && textInputContext == INPUT_CONFIG_PIN_NEW) {
    if (strlen(text) != 4) {
      lv_obj_set_style_border_color(vehicleTextArea, c565(0xF800), 0);
      lv_obj_set_style_border_width(vehicleTextArea, 2, 0);
      return;
    }
    strncpy(pendingSecurityPin, text, sizeof(pendingSecurityPin));
    pendingSecurityPin[sizeof(pendingSecurityPin) - 1] = '\0';
    vehicleTextArea = NULL;
    textInputContext = INPUT_CONFIG_PIN_CONFIRM;
    queueRebuild(SCREEN_TEXT_INPUT);
    return;
  } else if (save && textInputContext == INPUT_CONFIG_PIN_CONFIRM) {
    if (strlen(text) != 4) {
      lv_obj_set_style_border_color(vehicleTextArea, c565(0xF800), 0);
      lv_obj_set_style_border_width(vehicleTextArea, 2, 0);
      return;
    }
    if (strcmp(text, pendingSecurityPin) != 0) {
      pendingSecurityPin[0] = '\0';
      vehicleTextArea = NULL;
      textInputContext = INPUT_CONFIG_PIN_NEW;
      pinEntryMismatch = true;
      queueRebuild(SCREEN_TEXT_INPUT);
      return;
    }
    strncpy(securityPin, pendingSecurityPin, sizeof(securityPin));
    securityPin[sizeof(securityPin) - 1] = '\0';
    pinEnabled = true;
    pendingSecurityPin[0] = '\0';
    pinEntryMismatch = false;
    saveAppSettings();
  }

  const bool returnToConfig = textInputContext == INPUT_CONFIG_WHEEL;
  vehicleTextArea = NULL;
  vehicleTextField = -1;
  textInputContext = INPUT_VEHICLE_FIELD;
  queueRebuild(editingPin ? SCREEN_PIN_SETUP : (returnToConfig ? SCREEN_CONFIG : SCREEN_SUBMENU));
}

static void textInputAction(int id) {
  if (id == BTN_BACK) {
    if (textInputContext == INPUT_CONFIG_PIN_CONFIRM) {
      vehicleTextArea = NULL;
      textInputContext = INPUT_CONFIG_PIN_NEW;
      queueRebuild(SCREEN_TEXT_INPUT);
    } else {
      finishTextEdit(false);
    }
  }
}

// Custom compact keyboard (lv_btnmatrix): the stock lv_keyboard uses symbol
// glyphs that only exist in the Montserrat fonts, so its control keys render
// blank with the Rajdhani UI font — and it is much heavier than needed.
static const char *kbTextMapUpper[] = {
    "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "\n",
    "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", "\n",
    "A", "S", "D", "F", "G", "H", "J", "K", "L", "-", "\n",
    "Z", "X", "C", "V", "B", "N", "M", "_", ".", "\n",
    "abc", "DEL", "SPACE", "OK", "",
};
static const char *kbTextMapLower[] = {
    "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "\n",
    "q", "w", "e", "r", "t", "y", "u", "i", "o", "p", "\n",
    "a", "s", "d", "f", "g", "h", "j", "k", "l", "-", "\n",
    "z", "x", "c", "v", "b", "n", "m", "_", ".", "\n",
    "ABC", "DEL", "SPACE", "OK", "",
};
static const char *kbNumberMap[] = {
    "1", "2", "3", "\n", "4", "5", "6", "\n", "7", "8", "9", "\n", ".", "0", "DEL", "\n", "OK", "",
};
static const char *kbPinMap[] = {
    "1", "2", "3", "\n", "4", "5", "6", "\n", "7", "8", "9", "\n", "CLR", "0", "DEL", "\n", "OK", "",
};
static constexpr uint16_t kNumericOkButton = 12;
static constexpr uint16_t kTextOkButton = 42;

// set_map resets button widths/ctrl, so reapply the bottom-row widths
static void applyKbTextLayout(lv_obj_t *kb, bool upper) {
  lv_btnmatrix_set_map(kb, upper ? kbTextMapUpper : kbTextMapLower);
  lv_btnmatrix_set_btn_width(kb, 39, 2);  // case toggle
  lv_btnmatrix_set_btn_width(kb, 40, 2);  // DEL
  lv_btnmatrix_set_btn_width(kb, 41, 3);  // SPACE
  lv_btnmatrix_set_btn_width(kb, 42, 2);  // OK
  lv_btnmatrix_set_btn_ctrl(kb, kTextOkButton, LV_BTNMATRIX_CTRL_CLICK_TRIG);
}

static void customKeyboardCb(lv_event_t *e) {
  lv_obj_t *kb = lv_event_get_target(e);
  const uint16_t id = lv_btnmatrix_get_selected_btn(kb);
  if (id == LV_BTNMATRIX_BTN_NONE || !vehicleTextArea) return;
  const char *key = lv_btnmatrix_get_btn_text(kb, id);
  if (!key) return;
  if (strcmp(key, "OK") == 0) {
    finishTextEdit(true);
  } else if (strcmp(key, "DEL") == 0) {
    lv_textarea_del_char(vehicleTextArea);
  } else if (strcmp(key, "CLR") == 0) {
    lv_textarea_set_text(vehicleTextArea, "");
  } else if (strcmp(key, "SPACE") == 0) {
    lv_textarea_add_char(vehicleTextArea, ' ');
  } else if (strcmp(key, "abc") == 0) {
    applyKbTextLayout(kb, false);
  } else if (strcmp(key, "ABC") == 0) {
    applyKbTextLayout(kb, true);
  } else {
    lv_textarea_add_text(vehicleTextArea, key);
  }
}

// hold-to-repeat for DEL, like a real keyboard
static void customKeyboardRepeatCb(lv_event_t *e) {
  lv_obj_t *kb = lv_event_get_target(e);
  const uint16_t id = lv_btnmatrix_get_selected_btn(kb);
  if (id == LV_BTNMATRIX_BTN_NONE || !vehicleTextArea) return;
  const char *key = lv_btnmatrix_get_btn_text(kb, id);
  if (key && strcmp(key, "DEL") == 0) {
    lv_textarea_del_char(vehicleTextArea);
  }
}

static lv_obj_t *makeCustomKeyboard(lv_obj_t *scr, bool numeric, bool pinPad = false) {
  lv_obj_t *kb = lv_btnmatrix_create(scr);
  // Meet the input row directly: its final pixel is y=69, so the first key
  // begins at y=70 without an unused horizontal strip between the controls.
  lv_obj_set_pos(kb, 2, 70);
  lv_obj_set_size(kb, 316, 168);

  // container: invisible, tight padding
  lv_obj_set_style_bg_opa(kb, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(kb, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(kb, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_row(kb, 3, LV_PART_MAIN);
  lv_obj_set_style_pad_column(kb, 3, LV_PART_MAIN);

  // keys: same look as the menu buttons
  lv_obj_set_style_radius(kb, 4, LV_PART_ITEMS);
  lv_obj_set_style_bg_color(kb, cyd_ui::controlSurface(), LV_PART_ITEMS);
  lv_obj_set_style_bg_opa(kb, LV_OPA_COVER, LV_PART_ITEMS);
  lv_obj_set_style_border_width(kb, 1, LV_PART_ITEMS);
  lv_obj_set_style_border_color(kb, cyd_ui::idleControlBorder(), LV_PART_ITEMS);
  lv_obj_set_style_shadow_width(kb, 0, LV_PART_ITEMS);
  lv_obj_set_style_text_color(kb, lv_color_white(), LV_PART_ITEMS);
  lv_obj_set_style_text_font(kb, &lv_font_rajdhani_14, LV_PART_ITEMS);

  // same press effect as the menu buttons (applyButtonTouchFx)
  // Each operand is widened before the or: LV_PART_* and LV_STATE_* are two
  // different unnamed enums, and combining them directly is deprecated in C++20.
  const lv_style_selector_t pressed = (lv_style_selector_t)LV_PART_ITEMS | (lv_style_selector_t)LV_STATE_PRESSED;
  lv_obj_set_style_transition(kb, buttonTransition(), LV_PART_ITEMS);
  lv_obj_set_style_transition(kb, buttonTransition(), pressed);
  lv_obj_set_style_bg_color(kb, cyd_ui::activeControlSurface(), pressed);
  lv_obj_set_style_border_width(kb, 2, pressed);
  lv_obj_set_style_border_color(kb, cyd_ui::chromeAccent(), pressed);
  lv_obj_set_style_shadow_width(kb, 8, pressed);
  lv_obj_set_style_shadow_spread(kb, 1, pressed);
  lv_obj_set_style_shadow_color(kb, accentLv(), pressed);
  lv_obj_set_style_shadow_opa(kb, LV_OPA_50, pressed);

  if (numeric) {
    lv_btnmatrix_set_map(kb, pinPad ? kbPinMap : kbNumberMap);
  } else {
    applyKbTextLayout(kb, true);  // start uppercase
  }
  // Normal characters are entered on press for a responsive keyboard. OK is
  // deliberately click-triggered so save/navigation happens only after the
  // pointer is released over the same key; a dragged-away press is cancelled.
  if (numeric) lv_btnmatrix_set_btn_ctrl(kb, kNumericOkButton, LV_BTNMATRIX_CTRL_CLICK_TRIG);
  lv_obj_add_event_cb(kb, customKeyboardCb, LV_EVENT_VALUE_CHANGED, NULL);
  lv_obj_add_event_cb(kb, customKeyboardRepeatCb, LV_EVENT_LONG_PRESSED_REPEAT, NULL);
  return kb;
}

static void textAreaEventCb(lv_event_t *e) {
  if (lv_event_get_code(e) == LV_EVENT_READY) finishTextEdit(true);
}

static void showTextInput() {
  lv_obj_t *scr = makeScreen();
  currentAction = textInputAction;

  lv_obj_t *back = makeNavButton(
      scr, cyd_ui::kTopBarInset, cyd_ui::kTopBarY,
      txt("< BACK", "< TAKAISIN", "< ZURÜCK", "< RETOUR", "< ATRÁS", "< INDIETRO"), BTN_BACK,
      kTopBarButtonH);
  lv_obj_set_size(back, kWideTopNavW, kTopBarButtonH);
  lv_obj_update_layout(back);

  // named apart from the global pinInput[] keypad buffer
  const bool editingPin = textInputContext == INPUT_CONFIG_PIN_NEW || textInputContext == INPUT_CONFIG_PIN_CONFIRM;
  const char *title = "";
  const char *hint = "";
  lv_color_t hintColor = cyd_ui::secondaryText();
  if (editingPin) {
    title = textInputContext == INPUT_CONFIG_PIN_CONFIRM
                ? txt("CONFIRM PIN", "VAHVISTA PIN", "PIN BESTÄTIGEN", "CONFIRMER PIN", "CONFIRMAR PIN",
                      "CONFERMA PIN")
                : txt("ENTER NEW PIN", "SYÖTÄ UUSI PIN", "NEUE PIN EINGEBEN", "NOUVEAU PIN", "NUEVO PIN",
                      "NUOVO PIN");
    hint = textInputContext == INPUT_CONFIG_PIN_CONFIRM
               ? txt("Enter the same four digits again", "Syötä samat neljä numeroa uudelleen",
                     "Dieselben vier Ziffern erneut eingeben", "Saisissez à nouveau les quatre chiffres",
                     "Introduzca de nuevo los cuatro dígitos", "Inserisci di nuovo le stesse quattro cifre")
               : txt("Choose exactly four digits", "Valitse täsmälleen neljä numeroa", "Genau vier Ziffern wählen",
                     "Choisissez exactement quatre chiffres", "Elija exactamente cuatro dígitos",
                     "Scegli esattamente quattro cifre");
    if (pinEntryMismatch) {
      hint = txt("PINs did not match - try again", "PIN-koodit eivät täsmänneet", "PINs stimmen nicht überein",
                 "Les PIN ne correspondent pas", "Los PIN no coinciden", "I PIN non corrispondono");
      hintColor = c565(0xF9C0);
      pinEntryMismatch = false;
    }
  } else if (textInputContext == INPUT_CONFIG_WHEEL) {
    title = telemetrySpeedFromController()
                ? txt("SPEED CALIBRATION %", "KALIBROINTI %", "KALIBRIERUNG %", "CALIBRAGE %", "CALIBRACIÓN %",
                      "CALIBRAZIONE %")
                : txt("WHEEL DIAMETER", "RENKAAN KOKO", "RADDURCHMESSER", "DIAMÈTRE ROUE", "DIÁMETRO RUEDA",
                      "DIAMETRO RUOTA");
    hint = vehicleFieldInputHint(VEHICLE_FIELD_WHEEL_MM);
  } else {
    title = vehicleFieldTitle(vehicleTextField);
    hint = vehicleFieldInputHint(vehicleTextField);
  }

  vehicleTextArea = lv_textarea_create(scr);
  constexpr int inputX = cyd_ui::kTopBarInset + cyd_ui::kTopBarWidth + cyd_ui::kControlGap;
  constexpr int inputW = cyd_ui::kScreenWidth - cyd_ui::kTopBarInset - inputX;
  // Keep field context alongside the standard Back control, then give the
  // editable value its own full-width row directly above the keyboard.
  constexpr int inputRowY = 42;
  constexpr int inputRowH = 28;
  lv_obj_set_pos(vehicleTextArea, cyd_ui::kTopBarInset, inputRowY);
  lv_obj_set_size(vehicleTextArea, cyd_ui::kScreenWidth - 2 * cyd_ui::kTopBarInset, inputRowH);
  lv_obj_set_style_radius(vehicleTextArea, 5, 0);
  lv_obj_set_style_bg_color(vehicleTextArea, cyd_ui::controlSurface(), 0);
  lv_obj_set_style_bg_opa(vehicleTextArea, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(vehicleTextArea, 1, 0);
  lv_obj_set_style_border_color(vehicleTextArea, cyd_ui::idleControlBorder(), 0);
  lv_obj_set_style_text_color(vehicleTextArea, lv_color_white(), 0);
  lv_obj_set_style_text_font(vehicleTextArea, &lv_font_rajdhani_14, 0);
  lv_obj_set_style_pad_left(vehicleTextArea, 8, 0);
  lv_obj_set_style_pad_right(vehicleTextArea, 8, 0);
  lv_obj_set_style_pad_top(vehicleTextArea, 4, 0);
  lv_obj_set_style_pad_bottom(vehicleTextArea, 2, 0);
  lv_textarea_set_one_line(vehicleTextArea, true);
  size_t maxLength = 8;
  if (textInputContext == INPUT_VEHICLE_FIELD && vehicleFieldIsText(vehicleTextField)) {
    maxLength = vehicleFieldTextSize(vehicleTextField) - 1;
  } else if (editingPin) {
    maxLength = 4;
  } else if (textInputContext == INPUT_CONFIG_WHEEL) {
    maxLength = 4;
  }
  lv_textarea_set_max_length(vehicleTextArea, maxLength);
  char current[32];
  if (textInputContext == INPUT_CONFIG_WHEEL) {
    snprintf(current, sizeof(current), "%u",
             telemetrySpeedFromController() ? speedCalibrationPercent : wheelDiameterMm);
  } else if (editingPin) {
    current[0] = '\0';
    lv_textarea_set_password_mode(vehicleTextArea, true);
  } else {
    vehicleFieldEditText(vehicleTextField, current, sizeof(current));
  }
  lv_textarea_set_text(vehicleTextArea, current);
  lv_obj_add_event_cb(vehicleTextArea, textAreaEventCb, LV_EVENT_ALL, NULL);

  lv_obj_t *titleLabel = makeLabelAt(scr, inputX + inputW / 2, 3, title, accentLv(),
                                     &lv_font_rajdhani_14, 3);
  lv_obj_set_width(titleLabel, inputW);
  lv_obj_set_x(titleLabel, inputX);
  lv_obj_set_style_text_align(titleLabel, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(titleLabel, LV_LABEL_LONG_CLIP);
  lv_obj_t *hintLabel = makeLabelAt(scr, inputX + inputW / 2, 21, hint, hintColor,
                                    &lv_font_rajdhani_12, 3);
  lv_obj_set_width(hintLabel, inputW);
  lv_obj_set_x(hintLabel, inputX);
  lv_obj_set_style_text_align(hintLabel, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(hintLabel, LV_LABEL_LONG_DOT);

  makeCustomKeyboard(scr, textInputContext != INPUT_VEHICLE_FIELD || vehicleFieldIsNumeric(vehicleTextField),
                     editingPin);

  loadScreen(scr);
}

// ── Dispatcher ────────────────────────────────────────────────────────────────

void uiShow(ScreenMode mode) {
  cancelSelectorDemoTimer();
  closeReplayScreen();
  if (selectorChoiceTimer) {
    lv_timer_del(selectorChoiceTimer);
    selectorChoiceTimer = NULL;
    pendingSelectorChoiceId = -1;
  }
  if (touchTestChromeTimer) {
    lv_timer_del(touchTestChromeTimer);
    touchTestChromeTimer = NULL;
  }
  setDemoPreview(false);
  currentScreen = mode;
  rideLoggerSetStorageNeeded((mode == SCREEN_SUBMENU && submenuType == SUBMENU_LOGGING) ||
                             mode == SCREEN_RIDE_LOGS || mode == SCREEN_RIDE_REPLAY);
  // These screens scan, pick or deliberately disconnect a controller, so the
  // manager must not redial the saved one behind them.
  controllerManagerSetSetupOpen((mode == SCREEN_SUBMENU && submenuType == SUBMENU_CONTROLLER_TYPE) ||
                                mode == SCREEN_FARDRIVER_BLE);
  pinMaskLabel = NULL;
  pinNoticeLabel = NULL;
  cancelPinLockoutTimer();  // showPinLock re-arms it if a lockout is still running
  ldrLiveLabel = NULL;
  ldrTargetLabel = NULL;
  ldrCalibrationLiveLabel = NULL;
  brightnessReadoutLabel = NULL;
  touchTestCanvas = NULL;
  touchTestBack = NULL;
  touchTestTitle = NULL;
  touchTestClear = NULL;
  touchTestInstruction = NULL;
  touchTestChromeHidden = false;
  farDriverStatusLabel = NULL;
  farDriverDetailLabel = NULL;
  farDriverPairHintLabel = NULL;
  farDriverServiceLabel = NULL;
  farDriverCharacteristicLabel = NULL;
  farDriverPacketLabel = NULL;
  farDriverCountersLabel = NULL;
  farDriverDecodedLabel = NULL;
  farDriverDisconnectButton = NULL;
  farDriverForgetButton = NULL;
  firmwareUpdateStateLabel = NULL;
  firmwareUpdateDetailLabel = NULL;
  firmwareUpdateProgress = NULL;
  firmwareUpdateProgressLabel = NULL;
  firmwareUpdateActionButton = NULL;
  firmwareUpdateActionLabel = NULL;
  companionStateLabel = NULL;
  companionDetailLabel = NULL;
  companionTimerLabel = NULL;
  companionActionButton = NULL;
  companionActionLabel = NULL;
  autoReturnOverlay = NULL;
  autoReturnProgress = NULL;
  autoReturnCountdownLabel = NULL;
  autoReturnShownSeconds = -1;
  if (dashboardControlsTimer) {
    lv_timer_del(dashboardControlsTimer);
    dashboardControlsTimer = NULL;
  }
  dashboardSettingsButton = NULL;
  dashboardLoggingControl = NULL;
  dashboardLoggingLabel = NULL;
  dashboardControlsVisible = false;
  linkScrim = NULL;
  linkChip = NULL;
  linkChipText = NULL;
  linkChipIcon = NULL;
  selectorOverlay = NULL;  // rebuilt by the selector screens
  selectorTopBack = NULL;
  selectorTopInfo = NULL;
  selectorTopSave = NULL;
  selectorTransitionActive = false;
  selectorTransitionPending = 0;
  selectorColorBar = NULL;
  selectorPalette = NULL;
  selectorPopup = NULL;
  selectorScreen = NULL;
  selectorArrowLeft = NULL;
  selectorArrowRight = NULL;
  selectorHousing = NULL;
  selectorBottomRow = NULL;
  resetProgressBar = NULL;
  resetProgressLabel = NULL;
  // menu/wizard chrome follows the saved theme's accent; a dashboard (or
  // DASH UI preview) build overrides this with the mode it renders
  setAccentRenderMode(dashboardMode);
  switch (mode) {
    case SCREEN_MENU:
      showMenu();
      break;
    case SCREEN_SUBMENU:
      showSubmenu();
      break;
    case SCREEN_TEXT_INPUT:
      showTextInput();
      break;
    case SCREEN_CONFIG:
      showConfigurator();
      break;
    case SCREEN_PIN_SETUP:
      showPinSetup();
      break;
    case SCREEN_PIN_LOCK:
      showPinLock();
      break;
    case SCREEN_TOUCH_TEST:
      showTouchTest();
      break;
    case SCREEN_FARDRIVER_BLE:
      showFarDriverBleDiagnostic();
      break;
    case SCREEN_RIDE_REPLAY:
      showRideReplay();
      break;
    case SCREEN_RIDE_LOGS:
      showRideLogs();
      break;
    case SCREEN_FIRMWARE_UPDATE:
      showFirmwareUpdate();
      break;
    case SCREEN_COMPANION_MODE:
      showCompanionMode();
      break;
    case SCREEN_DASHBOARD:
    default:
      showDashboard();
      break;
  }
}

#ifdef CYD_LVGL_PREVIEW
void uiPreviewSetControllerBackend(ControllerType type, ControllerConnection connection) {
  controllerType = type;
  controllerConnection = type == CONTROLLER_FARDRIVER ? CONTROLLER_CONNECTION_BLE : connection;
}

void uiPreviewSetFirmwareUpdateUserMode(bool enabled) {
  firmwareUpdateUserMode = enabled;
}

void uiPreviewSetControllerSetup(uint8_t stage, ControllerType type, ControllerConnection connection) {
  submenuType = SUBMENU_CONTROLLER_TYPE;
  controllerSetupStage = static_cast<ControllerSetupStage>(min<uint8_t>(stage, CONTROLLER_SETUP_CONFIRM));
  pendingControllerType = type;
  pendingControllerConnection = type == CONTROLLER_FARDRIVER ? CONTROLLER_CONNECTION_BLE : connection;
}

void uiPreviewSetSettingsCategory(uint8_t category) {
  developerOptionsEnabled = category != 1;
  settingsMenuLevel = category == 1   ? SETTINGS_MENU_DISPLAY
                      : category == 2 ? SETTINGS_MENU_CONTROLLER
                      : category == 3 ? SETTINGS_MENU_DEVELOPER
                                      : SETTINGS_MENU_MORE;
}

void uiPreviewSetSubmenu(SubmenuType type, uint8_t page, bool customSpeed) {
  cancelResetCountdown();
  submenuType = type;
  vehiclePage = page;
  speedPage = page;
  batteryPage = page % 2;
  displayPage = page % displayPageCount();
  panelPage = page % kPanelPageCount;
  speedSetupMode = customSpeed ? SPEED_SETUP_CUSTOM : SPEED_SETUP_PRESET;
  resetConfirmMode = -1;
  if (type == SUBMENU_DASH_UI && !colorPaletteOpen) dashUiGridOpen = true;
  if (type == SUBMENU_DASH_UI) dashUiGridPage = page % kDashGridPageCount;
}

void uiPreviewShowClearSdPrompt() { showClearSdCardPrompt(); }
// Shows the clear dialog at whatever stage the stubbed wipe reports, without
// waiting out the on-screen minimum.
void uiPreviewShowSdClearProgress() {
  showSdClearProgress();
  refreshSdClearDialog(true);
  // Frozen at this stage: the caller advances time only to settle the bar.
  stopSdClearTimer();
}

void uiPreviewSetResetConfirm(bool fullReset) {
  resetConfirmMode = fullReset ? 0 : 1;
  resetCountdownActive = false;
}

void uiPreviewSetResetCountdown(bool fullReset) {
  resetConfirmMode = fullReset ? 0 : 1;
  resetCountdownMode = resetConfirmMode;
  resetCountdownStartedMs = millis();
  resetCountdownActive = true;
}

void uiPreviewSetAutoBrightnessPrompt(bool open) {
  autoBrightnessPromptOpen = open;
  if (open) {
    dashboardAppearanceMode = DASH_APPEARANCE_AUTO;
    autoBrightnessEnabled = false;
  }
}

void uiPreviewSetColorPalette(bool open) {
  colorPaletteOpen = open;
  if (open) dashUiGridOpen = false;
  if (!open) {
    gradientPanelOpen = false;
    dataPanelOpen = false;
    dataChoiceOpen = false;
  }
}

void uiPreviewSetGradientPanel(bool open) {
  colorPaletteOpen = open;
  gradientPanelOpen = open;
  dataPanelOpen = false;
  if (open) dashUiGridOpen = false;
}

void uiPreviewSetDataPanel(bool open) {
  colorPaletteOpen = open;
  gradientPanelOpen = false;
  dataPanelOpen = open;
  dataChoiceOpen = false;
  dataListOffset = 0;
  if (open) dashUiGridOpen = false;
}

void uiPreviewSetDataChoice(uint8_t slot) {
  uiPreviewSetDataPanel(true);
  dataSelectedSlot = min(slot, (uint8_t)(DASH_DATA_SLOTS_MAX - 1));
  dataChoiceOpen = true;
  const int selected = dashboardDataSelection(dashUiPreviewMode, dataSelectedSlot);
  dataChoiceOffset = static_cast<uint8_t>(constrain(selected - 1, 0, max(0, (int)DATA_COUNT - 3)));
}

void uiPreviewSetDashboardMode(DashboardMode mode) {
  dashboardMode = mode;
  dashUiPreviewMode = mode;
  applyDashboardCustomization(mode);
}

void uiPreviewSetConfigStep(uint8_t step) {
  configStep = 0;
  firstBootSetupActive = step > 0;
  if (step == 1) submenuType = SUBMENU_LANGUAGE;
  else if (step == 2) submenuType = SUBMENU_UNITS;
  else if (step >= 3) {
    submenuType = SUBMENU_DASH_UI;
    prepareConfiguratorThemeStep();
  }
}

void uiPreviewSetConfigInput(uint8_t input) {
  configStep = input == 0 ? 3 : 4;
  textInputContext = input == 0 ? INPUT_CONFIG_WHEEL
                                : (input == 1 ? INPUT_CONFIG_PIN_NEW : INPUT_CONFIG_PIN_CONFIRM);
  if (input == 2) strncpy(pendingSecurityPin, "2580", sizeof(pendingSecurityPin));
}

void uiPreviewSetVehicleInput(uint8_t field) {
  vehicleTextField = min<uint8_t>(field, VEHICLE_FIELD_COUNT - 1);
  textInputContext = INPUT_VEHICLE_FIELD;
}

void uiPreviewSetGradient(bool enabled, bool horizontal, bool bell, uint8_t position) {
  dashboardGradientEnabled = enabled;
  dashboardGradientHorizontal = horizontal;
  dashboardGradientReverse = false;
  dashboardGradientBell = bell;
  dashboardGradientPosition = constrain(position, 15, 85);
}

void uiPreviewShowAutoReturnWarning(uint8_t seconds) {
  ensureAutoReturnOverlay();
  if (!autoReturnOverlay) return;
  seconds = constrain(seconds, (uint8_t)1, (uint8_t)5);
  lv_obj_clear_flag(autoReturnOverlay, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(autoReturnOverlay);
  char text[16];
  snprintf(text, sizeof(text), "%u s", seconds);
  setLabelText(autoReturnCountdownLabel, text);
  lv_bar_set_value(autoReturnProgress, (5 - seconds) * 20, LV_ANIM_OFF);
}

void uiPreviewShowHoldBubble(uint8_t percent, int x, int y) {
  (void)percent;
  (void)x;
  (void)y;
  showDashboardControls();
  // The native capture happens in the same tick in which the controls are
  // requested. Put them at their settled positions so the contact sheet shows
  // the real overlay layout rather than the animation's pre-layout origin.
  if (dashboardSettingsButton) {
    lv_anim_del(dashboardSettingsButton, selectorOverlayAnimCb);
    lv_obj_set_y(dashboardSettingsButton, 4);
  }
  if (dashboardLoggingControl) {
    lv_anim_del(dashboardLoggingControl, selectorOverlayAnimCb);
    lv_obj_set_y(dashboardLoggingControl, 206);
  }
}

void uiPreviewShowRecoveryHold(uint8_t percent) {
  showRecoveryHoldOverlay();
  if (recoveryHoldArc) lv_arc_set_value(recoveryHoldArc, constrain((int)percent, 0, 100));
}

void uiPreviewShowRecoveryResetPrompt() {
  recoveryResetPromptPending = true;
  showPendingRecoveryPrompt();
}

void uiPreviewSetSavedFeedback(bool saved) {
  selectorSavedFeedback = saved;
}

void uiPreviewShowDeveloperPrompt() { showDeveloperPrompt(); }

void uiPreviewShowDeveloperDisablePrompt() { showDisableDeveloperPrompt(); }

void uiPreviewShowDeveloperToast() {
  developerOptionsEnabled = true;
  showDeveloperStatusToast(false);
  if (statusToast) {
    lv_anim_del(statusToast, selectorOverlayAnimCb);
    lv_obj_set_y(statusToast, 197);
  }
}

void uiPreviewShowDeveloperAlreadyEnabled() {
  developerOptionsEnabled = true;
  showDeveloperStatusToast(true);
  if (statusToast) {
    lv_anim_del(statusToast, selectorOverlayAnimCb);
    lv_obj_set_y(statusToast, 197);
  }
}

void uiPreviewShowLoggingNotice(uint8_t variant) {
  static const StatusNotice notices[] = {
      NOTICE_RIDE_SAVED, NOTICE_SD_CARD_READY, NOTICE_SD_CARD_REMOVED, NOTICE_SD_CARD_REMOVED_RECORDING,
      NOTICE_SD_WRITE_FAILED, NOTICE_SD_CARD_NOT_READY,
  };
  showStatusNotice(notices[constrain((int)variant, 0, (int)(sizeof(notices) / sizeof(notices[0])) - 1)]);
  if (statusToast) {
    lv_anim_del(statusToast, selectorOverlayAnimCb);
    lv_obj_set_y(statusToast, 197);
  }
}

void uiPreviewShowControllerNotice(uint8_t variant) {
  static const StatusNotice notices[] = {
      NOTICE_VESC_PROFILE_SAVED, NOTICE_VESC_OFFLINE_SAVED, NOTICE_RESTART_DISPLAY_TO_APPLY,
      NOTICE_CONTROLLER_SETUP_APPLIED,
  };
  showStatusNotice(notices[constrain((int)variant, 0, (int)(sizeof(notices) / sizeof(notices[0])) - 1)]);
  if (statusToast) {
    lv_anim_del(statusToast, selectorOverlayAnimCb);
    lv_obj_set_y(statusToast, 197);
  }
}

void uiPreviewShowLightSensorCalibrationPrompt(bool brightStep) {
  if (brightStep) {
    pendingLightSensorDarkRaw = 900;
    showLightSensorBrightPrompt();
  } else {
    showLightSensorDarkPrompt();
  }
}

void uiPreviewShowLightSensorNotice(bool failed) {
  showStatusNotice(failed ? NOTICE_LIGHT_SENSOR_RANGE_TOO_SMALL : NOTICE_LIGHT_SENSOR_CALIBRATED);
  if (statusToast) {
    lv_anim_del(statusToast, selectorOverlayAnimCb);
    lv_obj_set_y(statusToast, 197);
  }
}

void uiPreviewShowSetupCompleteNotice() {
  showStatusNotice(NOTICE_SETUP_COMPLETE);
  if (statusToast) {
    lv_anim_del(statusToast, selectorOverlayAnimCb);
    lv_obj_set_y(statusToast, 197);
  }
}

// Captures show the alert a rider sees once an outage has lasted, not the
// grace period before it.
void uiPreviewExpireLinkAlertDelay() {
  refreshLinkOverlay(false);  // starts the outage clock for a forced link state
  if (linkDownSinceMs) linkDownSinceMs = millis() - kLinkWaitingAlertDelayMs - 1;
  refreshLinkOverlay(true);
}
#endif
