#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include <lvgl.h>

#include "app_state.h"
#include "controller_manager.h"
#include "dashboards.h"
#include "firmware_update_ble.h"
#include "companion_ble.h"
#include "host_runtime.h"
#include "ride_replay_core.h"
#include "screens.h"

extern void previewSetLoggingState(RideLoggingMode mode, bool recording);
extern void previewSetCardState(bool ready, bool checking);
extern void previewSetFirmwareUpdateState(FirmwareUpdateBleState state, bool configured, uint32_t received,
                                          uint32_t total, const char *message);
extern void previewSetCompanionState(CompanionBleState state, bool paused, uint32_t seconds, const char *message);
extern void previewSetMillis(uint32_t);
extern void previewSetDashboardValues(int, int, float, float, int, int, int);

namespace fs = std::filesystem;

// preview_stubs.cpp: forces the telemetry link/fault state the renderer
// captures. There is no controller to unplug or overheat on the host.
void previewSetTelemetryLink(TelemetryLink state);
void previewSetWipeStatus(RideLogWipeStatus::State state, uint32_t removed, uint32_t total, bool pinned);
void previewRestoreRideLogs();
void previewSetFaultCode(uint8_t code);

static void capture(const fs::path &output, const std::string &name) {
  const fs::path path = output / (name + ".ppm");
  if (!cyd::preview::capturePpm(path)) {
    throw std::runtime_error("Could not write " + path.string());
  }
}

// Every still except the startup-sweep states shows the dashboard at rest.
// The sweep applies its start values at build time, so without settling it a
// dashboard behind a popup would be captured on the sweep's first frame.
static void showRestingDashboard() {
  uiShow(SCREEN_DASHBOARD);
  previewFinishStartupSweep();
}

static DashboardAppearanceMode previewAppearance = DASH_APPEARANCE_DARK;
static bool previewAppearanceOverride = false;

static void showDashboard(const fs::path &output, DashboardMode mode, const char *name) {
  uiPreviewSetDashboardMode(mode);
  if (previewAppearanceOverride) dashboardAppearanceMode = previewAppearance;
  showRestingDashboard();
  DashboardValues values = {};
  getLiveDashboardValues(values);
  updateDashboardMode(mode, values, true);
  // Hardware fills this graph in over five minutes of riding.
  if (mode == MODE_EFFICIENCY) previewSeedEfficiency();
  capture(output, name);
}

// The startup sweep at its peak: every instrument at full scale and every
// readout at its widest value. A fixed 100 km/h scale forces three-digit speed
// text, which is what overflowed boxes sized for the resting value.
static void showStartupSweepPeak(const fs::path &output, DashboardMode mode, const char *name) {
  const bool automatic = automaticGaugeRanges;
  automaticGaugeRanges = false;
  if (mode == MODE_TRACE) previewSeedTrace();
  uiPreviewSetDashboardMode(mode);
  uiShow(SCREEN_DASHBOARD);
  cyd::preview::advanceTime(420);
  capture(output, name);
  previewFinishStartupSweep();
  automaticGaugeRanges = automatic;
}

static void showGaugeRangeTransition(const fs::path &output, DashboardMode mode, const char *name) {
  uiPreviewSetDashboardMode(mode);
  showRestingDashboard();
  setDemoPreview(true);
  resetAutomaticGaugeRanges();
  DashboardValues values = {};
  getLiveDashboardValues(values);
  // A fast burst (60 km/h, 2 kW) pushes the scales up at once; then 15 minutes of gentle riding
  // age it out of the window and the scales ease back down. Capture partway through that.
  values.speedKmh = 60;
  values.watts = 2000;
  uint32_t at = millis();
  for (int frame = 0; frame < 3; ++frame) {
    previewSetMillis(at += 100);
    updateDashboardMode(mode, values, true);
  }
  values.speedKmh = 12;
  values.watts = 400;
  for (int second = 0; second < 15 * 60 - 1; ++second) {
    previewSetMillis(at += 1000);
    updateDashboardMode(mode, values, true);
  }
  for (int frame = 0; frame < 7; ++frame) {  // the window empties on the third; the rest is the ease
    previewSetMillis(at += 250);
    updateDashboardMode(mode, values, true);
  }
  capture(output, name);
  setDemoPreview(false);
}

static void showSubmenu(const fs::path &output, SubmenuType type, uint8_t page, bool custom, const char *name) {
  // a partially filled brightness slider says more about the layout than 100%
  if (type == SUBMENU_DISPLAY) displayBrightnessPercent = 70;
  uiPreviewSetSubmenu(type, page, custom);
  uiShow(SCREEN_SUBMENU);
  if (type == SUBMENU_DISPLAY) uiSensorTick();
  // Exercise the firmware's 100 ms timer path as well as the static layout.
  // The DASH grid must ignore this tick until a live preview has been opened.
  if (type == SUBMENU_DASH_UI) uiDashboardTick();
  capture(output, name);
}

// Renders in another UI language so translated captions can be checked for
// overflow without reflashing. Defaults to English.
static bool applyLanguage(const std::string &code) {
  const std::pair<const char *, Language> languages[] = {{"en", LANG_EN}, {"fi", LANG_FI}, {"de", LANG_DE},
                                                         {"fr", LANG_FR}, {"es", LANG_ES}, {"it", LANG_IT}};
  for (const auto &entry : languages) {
    if (code == entry.first) {
      language = entry.second;
      return true;
    }
  }
  return false;
}

static bool applyAccent(const std::string &name) {
  const std::pair<const char *, AccentTheme> accents[] = {
      {"default", ACCENT_DEFAULT}, {"orange", ACCENT_ORANGE}, {"blue", ACCENT_BLUE},
      {"green", ACCENT_GREEN},     {"purple", ACCENT_PURPLE},   {"red", ACCENT_RED},
      {"cyan", ACCENT_CYAN},       {"yellow", ACCENT_YELLOW},   {"white", ACCENT_WHITE},
      {"magenta", ACCENT_MAGENTA},
  };
  for (const auto &entry : accents) {
    if (name == entry.first) {
      accentTheme = entry.second;
      return true;
    }
  }
  return false;
}

int main(int argc, char **argv) {
  try {
    std::vector<std::string> positional;
    for (int i = 1; i < argc; ++i) {
      const std::string argument = argv[i];
      if (argument.rfind("--lang=", 0) == 0) {
        if (!applyLanguage(argument.substr(7))) {
          std::cerr << "Unknown language: " << argument.substr(7) << "\n";
          return 2;
        }
      } else if (argument.rfind("--accent=", 0) == 0) {
        if (!applyAccent(argument.substr(9))) {
          std::cerr << "Unknown accent: " << argument.substr(9) << "\n";
          return 2;
        }
      } else if (argument.rfind("--top-speed=", 0) == 0) {
        const int value = std::stoi(argument.substr(12));
        if (value < 1 || value > 999) {
          std::cerr << "Top speed must be between 1 and 999 km/h\n";
          return 2;
        }
        topSpeedKmh = (uint32_t)value;
      } else if (argument.rfind("--appearance=", 0) == 0) {
        const std::string appearance = argument.substr(13);
        if (appearance == "dark") previewAppearance = DASH_APPEARANCE_DARK;
        else if (appearance == "light") previewAppearance = DASH_APPEARANCE_LIGHT;
        else if (appearance == "auto") previewAppearance = DASH_APPEARANCE_AUTO;
        else {
          std::cerr << "Unknown appearance: " << appearance << "\n";
          return 2;
        }
        previewAppearanceOverride = true;
      } else {
        positional.push_back(argument);
      }
    }
    if (positional.empty() || positional.size() > 2) {
      std::cerr << "usage: cyd_lvgl_preview <output-directory> [screen-name] "
                   "[--lang=en|fi|de|fr|es|it] "
                   "[--accent=default|orange|blue|green|magenta|red|cyan|yellow|white] "
                   "[--top-speed=kmh] [--appearance=dark|light|auto]\n";
      return 2;
    }
    const fs::path output = fs::absolute(positional[0]);
    const std::string requested = positional.size() == 2 ? positional[1] : "";
    const auto selected = [&](const char *name) { return requested.empty() || requested == name; };
    const auto stopAfterSelected = [&]() { return !requested.empty(); };
    fs::create_directories(output);
    cyd::preview::initRuntime();

    if (selected("01_cyber_hud")) { showDashboard(output, MODE_HUD, "01_cyber_hud"); if (stopAfterSelected()) return 0; }
    if (selected("02_dual_gauge")) { showDashboard(output, MODE_GAUGE, "02_dual_gauge"); if (stopAfterSelected()) return 0; }
    if (selected("02_dual_gauge_range_transition")) {
      showGaugeRangeTransition(output, MODE_GAUGE, "02_dual_gauge_range_transition");
      if (stopAfterSelected()) return 0;
    }
    if (selected("03_simple")) { showDashboard(output, MODE_SIMPLE, "03_simple"); if (stopAfterSelected()) return 0; }
    if (selected("04_bar_graph")) { showDashboard(output, MODE_BARS, "04_bar_graph"); if (stopAfterSelected()) return 0; }
    if (selected("05_motor_data_rebuild")) {
      // Rebuild before deleting the old screen: its buffer must not free the
      // new screen's tick coordinates (appearance/selector rebuild path).
      uiPreviewSetDashboardMode(MODE_MOTOR_DATA);
      showRestingDashboard();
      showRestingDashboard();
      showDashboard(output, MODE_MOTOR_DATA, "05_motor_data_rebuild");
      if (stopAfterSelected()) return 0;
    }
    if (selected("05_motor_data_missing")) {
      previewSetControllerTelemetryFields(TELEMETRY_FIELDS_DASHBOARD, TELEMETRY_FIELD_POWER);
      showDashboard(output, MODE_MOTOR_DATA, "05_motor_data_missing");
      previewSetControllerTelemetryFields(TELEMETRY_FIELDS_ALL, TELEMETRY_FIELDS_ALL);
      if (stopAfterSelected()) return 0;
    }
    if (selected("05_motor_data_high")) {
      previewSetDashboardValues(123, 12500, 108.8F, 114.9F, 105, 84, 90);
      showDashboard(output, MODE_MOTOR_DATA, "05_motor_data_high");
      previewSetDashboardValues(25, 1000, 52.0F, 19.2F, 42, 38, 75);
      if (stopAfterSelected()) return 0;
    }
    if (selected("05_motor_data_regen")) {
      previewSetDashboardValues(25, -1500, 52.0F, -28.8F, 42, 38, 75);
      showDashboard(output, MODE_MOTOR_DATA, "05_motor_data_regen");
      previewSetDashboardValues(25, 1000, 52.0F, 19.2F, 42, 38, 75);
      if (stopAfterSelected()) return 0;
    }
    if (selected("05_motor_data")) { showDashboard(output, MODE_MOTOR_DATA, "05_motor_data"); if (stopAfterSelected()) return 0; }
    if (selected("06_pixel_gauge")) { showDashboard(output, MODE_PIXEL_GAUGE, "06_pixel_gauge"); if (stopAfterSelected()) return 0; }
    if (selected("07_large_tiles")) { showDashboard(output, MODE_LARGE_TILES, "07_large_tiles"); if (stopAfterSelected()) return 0; }
    if (selected("08_big_readout")) { showDashboard(output, MODE_BIG_READOUT, "08_big_readout"); if (stopAfterSelected()) return 0; }
    if (selected("09_redline")) { showDashboard(output, MODE_REDLINE, "09_redline"); if (stopAfterSelected()) return 0; }
    if (selected("09_redline_range_transition")) {
      showGaugeRangeTransition(output, MODE_REDLINE, "09_redline_range_transition");
      if (stopAfterSelected()) return 0;
    }

    if (selected("10_trace")) {
      previewSeedTrace();  // hardware fills this history in over ~60 s of riding
      showDashboard(output, MODE_TRACE, "10_trace");
      if (stopAfterSelected()) return 0;
    }

    const std::pair<DashboardMode, const char *> sweepStates[] = {
        {MODE_HUD, "13_sweep_cyber_hud"},       {MODE_GAUGE, "13_sweep_dual_gauge"},
        {MODE_SIMPLE, "13_sweep_simple"},       {MODE_BARS, "13_sweep_bar_graph"},
        {MODE_MOTOR_DATA, "13_sweep_motor_data"}, {MODE_PIXEL_GAUGE, "13_sweep_pixel_gauge"},
        {MODE_LARGE_TILES, "13_sweep_large_tiles"}, {MODE_BIG_READOUT, "13_sweep_big_readout"},
        {MODE_REDLINE, "13_sweep_redline"},     {MODE_TRACE, "13_sweep_trace"},
        {MODE_MINIMAL, "13_sweep_minimal_ride"}, {MODE_EFFICIENCY, "13_sweep_efficiency"},
    };
    for (const auto &state : sweepStates) {
      if (!selected(state.second)) continue;
      showStartupSweepPeak(output, state.first, state.second);
      if (stopAfterSelected()) return 0;
    }

    if (selected("11_settings_page_2")) {
      uiPreviewSetSettingsCategory(0);
      uiPreviewSetDashboardMode(MODE_HUD); uiShow(SCREEN_MENU);
      capture(output, "11_settings_page_2"); if (stopAfterSelected()) return 0;
    }
    if (selected("11_settings_page_1")) {
      uiPreviewSetSettingsCategory(1); uiShow(SCREEN_MENU);
      capture(output, "11_settings_page_1"); if (stopAfterSelected()) return 0;
    }
    if (selected("21_gauge_ranges_manual")) {
      automaticGaugeRanges = false;
      uiPreviewSetSubmenu(SUBMENU_GAUGE_RANGES, 0, false); uiShow(SCREEN_SUBMENU);
      capture(output, "21_gauge_ranges_manual"); automaticGaugeRanges = true;
      if (stopAfterSelected()) return 0;
    }
    if (selected("11_demo_mode")) {
      uiPreviewSetSubmenu(SUBMENU_DEMO, 0, false); uiShow(SCREEN_SUBMENU);
      capture(output, "11_demo_mode"); if (stopAfterSelected()) return 0;
    }
    if (selected("11_developer_options")) {
      uiPreviewSetSettingsCategory(3); uiShow(SCREEN_MENU);
      capture(output, "11_developer_options"); if (stopAfterSelected()) return 0;
    }
    if (selected("11_developer_options_fardriver")) {
      uiPreviewSetControllerBackend(CONTROLLER_FARDRIVER, CONTROLLER_CONNECTION_BLE);
      uiPreviewSetSettingsCategory(3); uiShow(SCREEN_MENU);
      capture(output, "11_developer_options_fardriver");
      uiPreviewSetControllerBackend(CONTROLLER_VESC, CONTROLLER_CONNECTION_UART);
      if (stopAfterSelected()) return 0;
    }
    if (selected("11_developer_disable_prompt")) {
      uiPreviewSetSettingsCategory(3); uiShow(SCREEN_MENU);
      uiPreviewShowDeveloperDisablePrompt();
      capture(output, "11_developer_disable_prompt"); if (stopAfterSelected()) return 0;
    }
    if (selected("11_recovery_hold")) {
      showRestingDashboard();
      uiPreviewShowRecoveryHold(60);
      capture(output, "11_recovery_hold"); if (stopAfterSelected()) return 0;
    }
    if (selected("11_recovery_reset_prompt")) {
      showRestingDashboard();
      uiPreviewShowRecoveryResetPrompt();
      capture(output, "11_recovery_reset_prompt"); if (stopAfterSelected()) return 0;
    }
    if (selected("11_developer_prompt")) {
      uiPreviewSetSettingsCategory(1); uiShow(SCREEN_MENU);
      uiPreviewShowDeveloperPrompt();
      capture(output, "11_developer_prompt"); if (stopAfterSelected()) return 0;
    }
    if (selected("11_developer_enabled")) {
      uiPreviewSetSettingsCategory(1); uiShow(SCREEN_MENU);
      uiPreviewShowDeveloperToast();
      capture(output, "11_developer_enabled"); if (stopAfterSelected()) return 0;
    }
    if (selected("11_developer_already_enabled")) {
      uiPreviewSetSettingsCategory(1); uiShow(SCREEN_MENU);
      uiPreviewShowDeveloperAlreadyEnabled();
      capture(output, "11_developer_already_enabled"); if (stopAfterSelected()) return 0;
    }
    const char *loggingNotices[] = {
        "11_logging_saved_notice", "11_logging_card_ready_notice", "11_logging_card_removed_notice",
        "11_logging_card_removed_active_notice", "11_logging_error_notice", "11_logging_not_ready_notice",
    };
    for (uint8_t i = 0; i < sizeof(loggingNotices) / sizeof(loggingNotices[0]); i++) {
      if (selected(loggingNotices[i])) {
        showRestingDashboard();
        uiPreviewShowLoggingNotice(i);
        capture(output, loggingNotices[i]); if (stopAfterSelected()) return 0;
      }
    }
    if (selected("11_controller_saved_notice")) {
      uiPreviewSetSettingsCategory(2); uiShow(SCREEN_MENU);
      uiPreviewShowControllerNotice(0);
      capture(output, "11_controller_saved_notice"); if (stopAfterSelected()) return 0;
    }
    if (selected("11_controller_offline_notice")) {
      uiPreviewSetSettingsCategory(2); uiShow(SCREEN_MENU);
      uiPreviewShowControllerNotice(1);
      capture(output, "11_controller_offline_notice"); if (stopAfterSelected()) return 0;
    }
    if (selected("11_controller_restart_notice")) {
      uiPreviewSetSettingsCategory(2); uiShow(SCREEN_MENU);
      uiPreviewShowControllerNotice(2);
      capture(output, "11_controller_restart_notice"); if (stopAfterSelected()) return 0;
    }
    if (selected("11_controller_setup_saved_notice")) {
      showRestingDashboard();
      uiPreviewShowControllerNotice(3);
      capture(output, "11_controller_setup_saved_notice"); if (stopAfterSelected()) return 0;
    }
    if (selected("11_controller_settings")) {
      uiPreviewSetSettingsCategory(2); uiShow(SCREEN_MENU);
      capture(output, "11_controller_settings"); if (stopAfterSelected()) return 0;
    }
    if (selected("11_controller_settings_fardriver")) {
      uiPreviewSetControllerBackend(CONTROLLER_FARDRIVER, CONTROLLER_CONNECTION_BLE);
      uiPreviewSetSettingsCategory(2); uiShow(SCREEN_MENU);
      capture(output, "11_controller_settings_fardriver");
      uiPreviewSetControllerBackend(CONTROLLER_VESC, CONTROLLER_CONNECTION_UART);
      if (stopAfterSelected()) return 0;
    }
    if (selected("11_controller_setup")) {
      uiPreviewSetControllerSetup(0); uiShow(SCREEN_SUBMENU);
      capture(output, "11_controller_setup"); if (stopAfterSelected()) return 0;
    }
    if (selected("11_controller_setup_connection")) {
      uiPreviewSetControllerSetup(1); uiShow(SCREEN_SUBMENU);
      capture(output, "11_controller_setup_connection"); if (stopAfterSelected()) return 0;
    }
    if (selected("11_controller_setup_discovery")) {
      uiPreviewSetControllerSetup(2, CONTROLLER_VESC, CONTROLLER_CONNECTION_BLE); uiShow(SCREEN_SUBMENU);
      capture(output, "11_controller_setup_discovery"); if (stopAfterSelected()) return 0;
    }
    if (selected("11_controller_setup_confirm")) {
      uiPreviewSetControllerSetup(3, CONTROLLER_VESC, CONTROLLER_CONNECTION_UART); uiShow(SCREEN_SUBMENU);
      capture(output, "11_controller_setup_confirm"); if (stopAfterSelected()) return 0;
    }
    if (selected("11_controller_setup_fardriver_connected")) {
      uiPreviewSetControllerSetup(2, CONTROLLER_FARDRIVER, CONTROLLER_CONNECTION_BLE); uiShow(SCREEN_SUBMENU);
      capture(output, "11_controller_setup_fardriver_connected"); if (stopAfterSelected()) return 0;
    }
    if (selected("11_controller_setup_fardriver_confirm")) {
      uiPreviewSetControllerSetup(3, CONTROLLER_FARDRIVER, CONTROLLER_CONNECTION_BLE); uiShow(SCREEN_SUBMENU);
      capture(output, "11_controller_setup_fardriver_confirm"); if (stopAfterSelected()) return 0;
    }

    if (selected("19_controller_config_uart")) {
      uiPreviewSetControllerBackend(CONTROLLER_VESC, CONTROLLER_CONNECTION_UART);
      showSubmenu(output, SUBMENU_CONTROLLER_CONFIG, 0, false, "19_controller_config_uart");
      if (stopAfterSelected()) return 0;
    }
    if (selected("19_controller_config_ble")) {
      uiPreviewSetControllerBackend(CONTROLLER_VESC, CONTROLLER_CONNECTION_BLE);
      showSubmenu(output, SUBMENU_CONTROLLER_CONFIG, 0, false, "19_controller_config_ble");
      uiPreviewSetControllerBackend(CONTROLLER_VESC, CONTROLLER_CONNECTION_UART);
      if (stopAfterSelected()) return 0;
    }
    if (selected("19_controller_config_fardriver")) {
      uiPreviewSetControllerBackend(CONTROLLER_FARDRIVER, CONTROLLER_CONNECTION_BLE);
      showSubmenu(output, SUBMENU_CONTROLLER_CONFIG, 0, false, "19_controller_config_fardriver");
      uiPreviewSetControllerBackend(CONTROLLER_VESC, CONTROLLER_CONNECTION_UART);
      if (stopAfterSelected()) return 0;
    }

    if (selected("12_gradient_example")) {
      uiPreviewSetGradient(true, false, true, 42);
      showDashboard(output, MODE_LARGE_TILES, "12_gradient_example");
      uiPreviewSetGradient(false, false, false, 50);
      if (stopAfterSelected()) return 0;
    }
    if (selected("11_minimal_ride")) { showDashboard(output, MODE_MINIMAL, "11_minimal_ride"); if (stopAfterSelected()) return 0; }
    if (selected("12_efficiency")) { showDashboard(output, MODE_EFFICIENCY, "12_efficiency"); if (stopAfterSelected()) return 0; }
    if (selected("13_dashboard_hold")) {
      uiPreviewSetDashboardMode(MODE_HUD);
      showRestingDashboard();
      uiPreviewShowHoldBubble(58, 48, 120);
      capture(output, "13_dashboard_hold");
      if (stopAfterSelected()) return 0;
    }
    if (selected("13_dashboard_demo")) {
      dashboardDemoModeEnabled = true;
      uiPreviewSetDashboardMode(MODE_HUD);
      showRestingDashboard();
      capture(output, "13_dashboard_demo");
      dashboardDemoModeEnabled = false;
      setDemoMode(false);
      if (stopAfterSelected()) return 0;
    }
    // The shared segmented battery at each warning step: amber at two blocks,
    // red at one, and a blinking last block below that, shown in both phases.
    const struct {
      const char *name;
      DashboardMode mode;
      int percent;
      bool light;
      uint32_t blinkMs;
    } batteryStates[] = {
        {"13_battery_two_bars", MODE_GAUGE, 45, false, 0},
        {"13_battery_one_bar", MODE_HUD, 25, false, 0},
        {"13_battery_critical", MODE_HUD, 8, false, 0},
        {"13_battery_critical_blink_off", MODE_HUD, 8, false, 500},
        {"13_battery_critical_light", MODE_TRACE, 8, true, 0},
    };
    for (const auto &state : batteryStates) {
      if (!selected(state.name)) continue;
      const DashboardAppearanceMode appearance = dashboardAppearanceMode;
      if (state.mode == MODE_TRACE) previewSeedTrace();
      uiPreviewSetDashboardMode(state.mode);
      if (state.light) dashboardAppearanceMode = DASH_APPEARANCE_LIGHT;
      showRestingDashboard();
      DashboardValues values = {};
      getLiveDashboardValues(values);
      values.batteryPercent = state.percent;
      updateDashboardMode(state.mode, values, true);
      if (state.blinkMs) cyd::preview::advanceTime(state.blinkMs);
      capture(output, state.name);
      dashboardAppearanceMode = appearance;
      if (stopAfterSelected()) return 0;
    }
    if (selected("13_dashboard_fardriver_fields")) {
      const TelemetryFieldMask available =
          TELEMETRY_FIELD_SPEED | TELEMETRY_FIELD_POWER | TELEMETRY_FIELD_VOLTAGE |
          TELEMETRY_FIELD_CURRENT | TELEMETRY_FIELD_MOTOR_TEMP | TELEMETRY_FIELD_ESC_TEMP |
          TELEMETRY_FIELD_TRIP_DISTANCE | TELEMETRY_FIELD_AVG_SPEED | TELEMETRY_FIELD_UPTIME |
          TELEMETRY_FIELD_BATTERY_SOC | TELEMETRY_FIELD_RIDE_MODE;
      const TelemetryFieldMask derived =
          TELEMETRY_FIELD_SPEED | TELEMETRY_FIELD_POWER | TELEMETRY_FIELD_TRIP_DISTANCE |
          TELEMETRY_FIELD_AVG_SPEED | TELEMETRY_FIELD_UPTIME;
      previewSetControllerTelemetryFields(available, derived);
      uiPreviewSetControllerBackend(CONTROLLER_FARDRIVER, CONTROLLER_CONNECTION_BLE);
      showDashboard(output, MODE_SIMPLE, "13_dashboard_fardriver_fields");
      previewSetControllerTelemetryFields(TELEMETRY_FIELDS_ALL, TELEMETRY_FIELDS_ALL);
      uiPreviewSetControllerBackend(CONTROLLER_VESC, CONTROLLER_CONNECTION_UART);
      if (stopAfterSelected()) return 0;
    }
    if (selected("13_dashboard_logging_on")) {
      previewSetLoggingState(RIDE_LOG_ON, true);
      uiPreviewSetDashboardMode(MODE_HUD);
      showRestingDashboard();
      uiPreviewShowHoldBubble(100, 160, 120);
      capture(output, "13_dashboard_logging_on");
      previewSetLoggingState(RIDE_LOG_ON, false);
      if (stopAfterSelected()) return 0;
    }
    if (selected("13_menu_auto_return")) {
      uiPreviewSetSettingsCategory(0);
      uiShow(SCREEN_MENU);
      uiPreviewShowAutoReturnWarning(4);
      capture(output, "13_menu_auto_return");
      if (stopAfterSelected()) return 0;
    }

    if (selected("13_dash_ui_selector")) { showSubmenu(output, SUBMENU_DASH_UI, 0, false, "13_dash_ui_selector"); if (stopAfterSelected()) return 0; }
    if (selected("13_dash_ui_selector_full")) {
      uiPreviewSetSubmenu(SUBMENU_DASH_UI, 0, false);
      uiPreviewSetColorPalette(true);
      uiPreviewSetColorPalette(false);
      uiShow(SCREEN_SUBMENU);
      uiDashboardTick();
      capture(output, "13_dash_ui_selector_full");
      if (stopAfterSelected()) return 0;
    }
    if (selected("13_dash_ui_selector_saved")) {
      uiPreviewSetSubmenu(SUBMENU_DASH_UI, 0, false);
      uiPreviewSetColorPalette(true);
      uiPreviewSetColorPalette(false);
      uiPreviewSetSavedFeedback(true);
      uiShow(SCREEN_SUBMENU);
      uiDashboardTick();
      capture(output, "13_dash_ui_selector_saved");
      uiPreviewSetSavedFeedback(false);
      if (stopAfterSelected()) return 0;
    }
    if (selected("13_dash_ui_selector_page_2")) { showSubmenu(output, SUBMENU_DASH_UI, 1, false, "13_dash_ui_selector_page_2"); if (stopAfterSelected()) return 0; }
    if (selected("13_dash_ui_selector_page_3")) { showSubmenu(output, SUBMENU_DASH_UI, 2, false, "13_dash_ui_selector_page_3"); if (stopAfterSelected()) return 0; }
    if (selected("14_auto_brightness_prompt")) {
      // The prompt is built into the open Customize Theme panel, so the colour
      // popup has to stay open across the capture.
      uiPreviewSetColorPalette(true);
      uiPreviewSetAutoBrightnessPrompt(true);
      showSubmenu(output, SUBMENU_DASH_UI, 0, false, "14_auto_brightness_prompt");
      uiPreviewSetAutoBrightnessPrompt(false);
      uiPreviewSetColorPalette(false);
      if (stopAfterSelected()) return 0;
    }
    if (selected("14_dash_ui_colors")) {
      uiPreviewSetColorPalette(true);  // the same screen with the colour strip opened
      showSubmenu(output, SUBMENU_DASH_UI, 0, false, "14_dash_ui_colors");
      uiPreviewSetColorPalette(false);
      if (stopAfterSelected()) return 0;
    }
    if (selected("14_dash_ui_data")) {
      uiPreviewSetDataPanel(true);
      showSubmenu(output, SUBMENU_DASH_UI, 0, false, "14_dash_ui_data");
      uiPreviewSetDataPanel(false);
      if (stopAfterSelected()) return 0;
    }
    if (selected("14_dash_ui_data_choice")) {
      uiPreviewSetDataChoice(4);
      showSubmenu(output, SUBMENU_DASH_UI, 0, false, "14_dash_ui_data_choice");
      uiPreviewSetDataPanel(false);
      if (stopAfterSelected()) return 0;
    }
    if (selected("14_dash_ui_data_applied")) {
      setDashboardDataSelection(MODE_SIMPLE, 1, DATA_RIDE_EFFICIENCY);
      showDashboard(output, MODE_SIMPLE, "14_dash_ui_data_applied");
      resetDashboardCustomization(MODE_SIMPLE);
      if (stopAfterSelected()) return 0;
    }
    if (selected("14_gradient_custom")) {
      uiPreviewSetDashboardMode(MODE_HUD);
      uiPreviewSetGradient(true, false, true, 50);
      uiPreviewSetGradientPanel(true);
      showSubmenu(output, SUBMENU_DASH_UI, 0, false, "14_gradient_custom");
      uiPreviewSetGradientPanel(false);
      uiPreviewSetGradient(false, false, false, 50);
      if (stopAfterSelected()) return 0;
    }
    if (selected("15_language_submenu")) { showSubmenu(output, SUBMENU_LANGUAGE, 0, false, "15_language_submenu"); if (stopAfterSelected()) return 0; }
    if (selected("16_units_submenu")) { showSubmenu(output, SUBMENU_UNITS, 0, false, "16_units_submenu"); if (stopAfterSelected()) return 0; }
    if (selected("17_vesc_submenu_page_1")) { showSubmenu(output, SUBMENU_VESC, 0, false, "17_vesc_submenu_page_1"); if (stopAfterSelected()) return 0; }
    if (selected("18_vesc_submenu_page_2")) { showSubmenu(output, SUBMENU_VESC, 1, false, "18_vesc_submenu_page_2"); if (stopAfterSelected()) return 0; }
    struct VehiclePreview { const char *name; SubmenuType type; uint8_t page; ControllerBackendId backend; };
    const VehiclePreview vehiclePreviews[] = {
      {"20_connection_uart", SUBMENU_CONNECTION, 0, CONTROLLER_ID_VESC_UART},
      {"20_connection_ble", SUBMENU_CONNECTION, 0, CONTROLLER_ID_VESC_BLE},
      {"20_connection_fardriver", SUBMENU_CONNECTION, 0, CONTROLLER_ID_FARDRIVER_BLE},
      {"20_power_vesc", SUBMENU_POWER_LIMITS, 0, CONTROLLER_ID_VESC_UART},
      {"20_power_fardriver", SUBMENU_POWER_LIMITS, 0, CONTROLLER_ID_FARDRIVER_BLE},
      {"21_calibration", SUBMENU_SPEED_CALIBRATION, 0, CONTROLLER_ID_FARDRIVER_BLE},
      {"21_gauge_ranges", SUBMENU_GAUGE_RANGES, 0, CONTROLLER_ID_VESC_UART},
      {"22_modes_vesc", SUBMENU_SPEED, 0, CONTROLLER_ID_VESC_UART},
      {"22_modes_fardriver", SUBMENU_SPEED, 0, CONTROLLER_ID_FARDRIVER_BLE},
      {"23_mode_labels", SUBMENU_SPEED, 1, CONTROLLER_ID_FARDRIVER_BLE},
      {"27_pack_setup", SUBMENU_BATTERY, 1, CONTROLLER_ID_FARDRIVER_BLE},
    };
    for (const auto &state : vehiclePreviews) {
      if (!selected(state.name)) continue;
      controllerSelectionForId(state.backend, controllerType, controllerConnection);
      showSubmenu(output, state.type, state.page, false, state.name);
      if (stopAfterSelected()) return 0;
    }
    controllerType = CONTROLLER_VESC;
    controllerConnection = CONTROLLER_CONNECTION_UART;
    if (selected("23_dashboard_ride_mode")) {
      controllerType = CONTROLLER_FARDRIVER;
      controllerConnection = CONTROLLER_CONNECTION_BLE;
      snprintf(rideModeLabels[1], sizeof(rideModeLabels[1]), "Drive");
      setDashboardDataSelection(MODE_HUD, 0, DATA_RIDE_MODE);
      showDashboard(output, MODE_HUD, "23_dashboard_ride_mode");
      rideModeLabels[1][0] = '\0';
      setDashboardDataSelection(MODE_HUD, 0, dashboardDataDefault(MODE_HUD, 0));
      controllerType = CONTROLLER_VESC;
      controllerConnection = CONTROLLER_CONNECTION_UART;
      if (stopAfterSelected()) return 0;
    }
    if (selected("24_display_submenu")) { showSubmenu(output, SUBMENU_DISPLAY, 0, false, "24_display_submenu"); if (stopAfterSelected()) return 0; }
    if (selected("24_display_submenu_auto")) {
      autoBrightnessEnabled = true;
      showSubmenu(output, SUBMENU_DISPLAY, 0, false, "24_display_submenu_auto");
      autoBrightnessEnabled = false;
      if (stopAfterSelected()) return 0;
    }
    if (selected("24_auto_brightness_calibrate_dark")) {
      uiPreviewSetSubmenu(SUBMENU_DISPLAY, 0, false); uiShow(SCREEN_SUBMENU);
      uiPreviewShowLightSensorCalibrationPrompt(false);
      capture(output, "24_auto_brightness_calibrate_dark"); if (stopAfterSelected()) return 0;
    }
    if (selected("24_auto_brightness_calibrate_bright")) {
      uiPreviewSetSubmenu(SUBMENU_DISPLAY, 0, false); uiShow(SCREEN_SUBMENU);
      uiPreviewShowLightSensorCalibrationPrompt(true);
      capture(output, "24_auto_brightness_calibrate_bright"); if (stopAfterSelected()) return 0;
    }
    if (selected("24_auto_brightness_calibrated")) {
      uiPreviewSetSubmenu(SUBMENU_DISPLAY, 0, false); uiShow(SCREEN_SUBMENU);
      uiPreviewShowLightSensorNotice(false);
      capture(output, "24_auto_brightness_calibrated"); if (stopAfterSelected()) return 0;
    }
    if (selected("24_auto_brightness_calibration_failed")) {
      uiPreviewSetSubmenu(SUBMENU_DISPLAY, 0, false); uiShow(SCREEN_SUBMENU);
      uiPreviewShowLightSensorNotice(true);
      capture(output, "24_auto_brightness_calibration_failed"); if (stopAfterSelected()) return 0;
    }
    if (selected("24_display_submenu_page_2")) { showSubmenu(output, SUBMENU_DISPLAY, 1, false, "24_display_submenu_page_2"); if (stopAfterSelected()) return 0; }
    if (selected("24_display_info")) { showSubmenu(output, SUBMENU_DISPLAY_INFO, 0, false, "24_display_info"); if (stopAfterSelected()) return 0; }
    // The firmware changelog lives under Developer options, not Information.
    if (selected("11_changelog")) { showSubmenu(output, SUBMENU_CHANGELOG, 0, false, "11_changelog"); if (stopAfterSelected()) return 0; }
    if (selected("24_companion_off")) {
      previewSetCompanionState(COMPANION_BLE_OFF, false, 0, "Companion mode off");
      uiShow(SCREEN_COMPANION_MODE); capture(output, "24_companion_off"); if (stopAfterSelected()) return 0;
    }
    if (selected("24_companion_ready")) {
      previewSetCompanionState(COMPANION_BLE_ADVERTISING, false, 48, "Ready for phone connection");
      uiShow(SCREEN_COMPANION_MODE); capture(output, "24_companion_ready"); if (stopAfterSelected()) return 0;
    }
    if (selected("24_companion_connected")) {
      previewSetCompanionState(COMPANION_BLE_CONNECTED, true, 287, "Phone connected - controller Bluetooth paused");
      uiShow(SCREEN_COMPANION_MODE); capture(output, "24_companion_connected"); if (stopAfterSelected()) return 0;
    }
    if (selected("24_display_panel")) { showSubmenu(output, SUBMENU_PANEL_COLORS, 0, false, "24_display_panel"); if (stopAfterSelected()) return 0; }
    if (selected("24_display_panel_gamma")) { showSubmenu(output, SUBMENU_PANEL_COLORS, 1, false, "24_display_panel_gamma"); if (stopAfterSelected()) return 0; }
    if (selected("24_auto_return")) { showSubmenu(output, SUBMENU_AUTO_RETURN, 0, false, "24_auto_return"); if (stopAfterSelected()) return 0; }
    if (selected("24_fardriver_ble")) { uiShow(SCREEN_FARDRIVER_BLE); capture(output, "24_fardriver_ble"); if (stopAfterSelected()) return 0; }
    if (selected("24_firmware_update_locked")) {
      previewSetFirmwareUpdateState(FIRMWARE_UPDATE_BLE_LOCKED, false, 0, 0, "Release signing key not configured");
      uiShow(SCREEN_FIRMWARE_UPDATE); capture(output, "24_firmware_update_locked"); if (stopAfterSelected()) return 0;
    }
    if (selected("24_firmware_update_receiving")) {
      previewSetFirmwareUpdateState(FIRMWARE_UPDATE_BLE_RECEIVING, true, 614400, 926687,
                                    "Receiving compressed signed firmware");
      uiShow(SCREEN_FIRMWARE_UPDATE); capture(output, "24_firmware_update_receiving"); if (stopAfterSelected()) return 0;
    }
    if (selected("24_firmware_update_downgrade_confirm")) {
      previewSetFirmwareUpdateState(FIRMWARE_UPDATE_BLE_CONFIRM_DOWNGRADE, true, 0, 926687,
                                    "Signed older firmware - confirm downgrade on display");
      uiShow(SCREEN_FIRMWARE_UPDATE); capture(output, "24_firmware_update_downgrade_confirm");
      if (stopAfterSelected()) return 0;
    }
    if (selected("24_firmware_update_verified")) {
      previewSetFirmwareUpdateState(FIRMWARE_UPDATE_BLE_READY_TO_REBOOT, true, 1603952, 1603952,
                                    "Firmware verified - restarting in 5 seconds");
      uiShow(SCREEN_FIRMWARE_UPDATE); capture(output, "24_firmware_update_verified"); if (stopAfterSelected()) return 0;
    }
    if (selected("24_firmware_update_user_locked")) {
      previewSetFirmwareUpdateState(FIRMWARE_UPDATE_BLE_LOCKED, false, 0, 0, "Release signing key not configured");
      uiPreviewSetFirmwareUpdateUserMode(true);
      uiShow(SCREEN_FIRMWARE_UPDATE); capture(output, "24_firmware_update_user_locked");
      uiPreviewSetFirmwareUpdateUserMode(false);
      if (stopAfterSelected()) return 0;
    }
    if (selected("24_firmware_update_user_ready")) {
      previewSetFirmwareUpdateState(FIRMWARE_UPDATE_BLE_ADVERTISING, true, 0, 0,
                                    "Advertising as CYD Firmware Update");
      uiPreviewSetFirmwareUpdateUserMode(true);
      uiShow(SCREEN_FIRMWARE_UPDATE); capture(output, "24_firmware_update_user_ready");
      uiPreviewSetFirmwareUpdateUserMode(false);
      if (stopAfterSelected()) return 0;
    }
    if (selected("24_firmware_update_user_receiving")) {
      previewSetFirmwareUpdateState(FIRMWARE_UPDATE_BLE_RECEIVING, true, 614400, 926687,
                                    "Receiving compressed signed firmware");
      uiPreviewSetFirmwareUpdateUserMode(true);
      uiShow(SCREEN_FIRMWARE_UPDATE); capture(output, "24_firmware_update_user_receiving");
      uiPreviewSetFirmwareUpdateUserMode(false);
      if (stopAfterSelected()) return 0;
    }
    if (selected("24_firmware_update_user_success")) {
      previewSetFirmwareUpdateState(FIRMWARE_UPDATE_BLE_READY_TO_REBOOT, true, 926687, 926687,
                                    "Firmware verified - restarting in 5 seconds");
      uiPreviewSetFirmwareUpdateUserMode(true);
      uiShow(SCREEN_FIRMWARE_UPDATE); capture(output, "24_firmware_update_user_success");
      uiPreviewSetFirmwareUpdateUserMode(false);
      if (stopAfterSelected()) return 0;
    }
    if (selected("24_firmware_update_user_cancelled")) {
      previewSetFirmwareUpdateState(FIRMWARE_UPDATE_BLE_CANCELLED, true, 307200, 926687,
                                    "Upload cancelled by phone - restarting");
      uiPreviewSetFirmwareUpdateUserMode(true);
      uiShow(SCREEN_FIRMWARE_UPDATE); capture(output, "24_firmware_update_user_cancelled");
      uiPreviewSetFirmwareUpdateUserMode(false);
      if (stopAfterSelected()) return 0;
    }
    if (selected("24_touch_test")) { uiShow(SCREEN_TOUCH_TEST); capture(output, "24_touch_test"); if (stopAfterSelected()) return 0; }
    if (selected("25_pin_submenu")) { uiShow(SCREEN_PIN_SETUP); capture(output, "25_pin_submenu"); if (stopAfterSelected()) return 0; }
    if (selected("25_logging_submenu")) { showSubmenu(output, SUBMENU_LOGGING, 0, false, "25_logging_submenu"); if (stopAfterSelected()) return 0; }
    if (selected("25_logging_no_card")) {
      previewSetCardState(false, false);
      previewSetLoggingState(RIDE_LOG_OFF, false);
      showSubmenu(output, SUBMENU_LOGGING, 0, false, "25_logging_no_card");
      previewSetLoggingState(RIDE_LOG_ON, false);
      previewSetCardState(true, false);
      if (stopAfterSelected()) return 0;
    }
    if (selected("25_ride_logs")) {
      uiShow(SCREEN_RIDE_LOGS);
      capture(output, "25_ride_logs");
      if (stopAfterSelected()) return 0;
    }
    if (selected("25_ride_logs_recording")) {
      previewSetLoggingState(RIDE_LOG_ON, true);
      uiShow(SCREEN_RIDE_LOGS);
      capture(output, "25_ride_logs_recording");
      previewSetLoggingState(RIDE_LOG_ON, false);
      if (stopAfterSelected()) return 0;
    }
    if (selected("25_replay")) { uiPreviewRideReplay(504000, false); capture(output, "25_replay"); if (stopAfterSelected()) return 0; }
    if (selected("25_replay_edge")) { uiPreviewRideReplay(2833000, false); capture(output, "25_replay_edge"); if (stopAfterSelected()) return 0; }
    if (selected("25_replay_playing")) { uiPreviewRideReplay(504000, true); capture(output, "25_replay_playing"); if (stopAfterSelected()) return 0; }
    if (selected("25_replay_regen")) { uiPreviewRideReplay(1170000, false); capture(output, "25_replay_regen"); if (stopAfterSelected()) return 0; }
    if (selected("25_replay_gap")) { uiPreviewRideReplay(1410000, false); capture(output, "25_replay_gap"); if (stopAfterSelected()) return 0; }
    // Just past the quarter-way bubble switch, where wide bubbles cover the scale labels.
    if (selected("25_replay_swap")) { uiPreviewRideReplay(730000, false); capture(output, "25_replay_swap"); if (stopAfterSelected()) return 0; }
    if (selected("25_replay_summary")) { uiPreviewRideReplay(504000, false); uiPreviewRideReplayPopup(0); capture(output, "25_replay_summary"); if (stopAfterSelected()) return 0; }
    if (selected("25_replay_delete")) { uiPreviewRideReplay(504000, false); uiPreviewRideReplayPopup(3); capture(output, "25_replay_delete"); if (stopAfterSelected()) return 0; }
    if (selected("25_replay_charts")) { uiPreviewRideReplay(504000, false); uiPreviewRideReplayPopup(1); capture(output, "25_replay_charts"); if (stopAfterSelected()) return 0; }
    if (selected("25_replay_fields")) { uiPreviewRideReplay(504000, false); uiPreviewRideReplayPopup(2); capture(output, "25_replay_fields"); if (stopAfterSelected()) return 0; }
    if (selected("25_replay_charts_two")) {
      const uint8_t fields[4] = {ride_replay::kCurrent, ride_replay::kBattery, ride_replay::kSpeed, ride_replay::kPower};
      uiPreviewRideReplay(504000, false); uiPreviewRideReplayLayout(2, fields); uiPreviewRideReplayPopup(1);
      capture(output, "25_replay_charts_two");
      if (stopAfterSelected()) return 0;
    }
    if (selected("25_replay_four")) {
      const uint8_t fields[4] = {ride_replay::kSpeed, ride_replay::kPower, ride_replay::kCurrent, ride_replay::kBattery};
      uiPreviewRideReplay(1170000, false); uiPreviewRideReplayLayout(4, fields); capture(output, "25_replay_four");
      if (stopAfterSelected()) return 0;
    }
    if (selected("25_replay_four_low")) {
      // Late low readings expose any gap between the minimum scale label and
      // the bottom row of a densely stacked chart.
      const uint8_t fields[4] = {ride_replay::kPower, ride_replay::kSpeed,
                                 ride_replay::kVoltage, ride_replay::kCurrent};
      uiPreviewRideReplay(2664000, false); uiPreviewRideReplayLayout(4, fields);
      capture(output, "25_replay_four_low");
      if (stopAfterSelected()) return 0;
    }
    if (selected("25_replay_phase")) {
      // Pack and phase current side by side: the pair most likely to be
      // charted together, and the check that their colours stay apart.
      const uint8_t fields[4] = {ride_replay::kSpeed, ride_replay::kPower, ride_replay::kCurrent,
                                 ride_replay::kMotorCurrent};
      uiPreviewRideReplay(1170000, false); uiPreviewRideReplayLayout(4, fields); capture(output, "25_replay_phase");
      if (stopAfterSelected()) return 0;
    }
    if (selected("25_replay_two")) {
      const uint8_t fields[4] = {ride_replay::kCurrent, ride_replay::kBattery, ride_replay::kSpeed, ride_replay::kPower};
      uiPreviewRideReplay(1170000, false); uiPreviewRideReplayLayout(2, fields); capture(output, "25_replay_two");
      if (stopAfterSelected()) return 0;
    }
    if (selected("25_replay_one")) {
      const uint8_t fields[4] = {ride_replay::kMotorTemp, ride_replay::kPower, ride_replay::kVoltage, ride_replay::kCurrent};
      uiPreviewRideReplay(2000000, false); uiPreviewRideReplayLayout(1, fields); capture(output, "25_replay_one");
      if (stopAfterSelected()) return 0;
    }
    // Zoomed in: the chart shows a window around the cursor and the track
    // under the charts shows where that window sits in the ride. Zoom steps
    // halve the stretch; the last step is where a reading is two columns wide.
    if (selected("25_replay_zoom")) {
      uiPreviewRideReplay(1170000, false); uiPreviewRideReplayZoom(3); capture(output, "25_replay_zoom");
      if (stopAfterSelected()) return 0;
    }
    if (selected("25_replay_zoom_max")) {
      uiPreviewRideReplay(1170000, false); uiPreviewRideReplayZoom(6); capture(output, "25_replay_zoom_max");
      if (stopAfterSelected()) return 0;
    }
    if (selected("25_replay_zoom_four")) {
      const uint8_t fields[4] = {ride_replay::kSpeed, ride_replay::kPower, ride_replay::kCurrent, ride_replay::kBattery};
      uiPreviewRideReplay(1170000, false); uiPreviewRideReplayLayout(4, fields); uiPreviewRideReplayZoom(2);
      capture(output, "25_replay_zoom_four");
      if (stopAfterSelected()) return 0;
    }
    if (selected("25_replay_no_card")) { previewSetCardState(false,false); uiPreviewRideReplay(0,false); capture(output,"25_replay_no_card"); previewSetCardState(true,false); if(stopAfterSelected())return 0; }

    if (selected("26_reset_submenu")) { showSubmenu(output, SUBMENU_RESET, 0, false, "26_reset_submenu"); if (stopAfterSelected()) return 0; }
    if (selected("25_ride_logs_clear_sd")) {
      uiShow(SCREEN_RIDE_LOGS);
      uiPreviewShowClearSdPrompt();
      capture(output, "25_ride_logs_clear_sd");
      if (stopAfterSelected()) return 0;
    }
    // The clear dialog at each stage: part way through, finished, and failed.
    struct ClearStage {
      const char *name;
      RideLogWipeStatus::State state;
      uint32_t removed, total;
    };
    const ClearStage clearStages[] = {{"25_ride_logs_clearing", RideLogWipeStatus::Removing, 57, 142},
                                      {"25_ride_logs_cleared", RideLogWipeStatus::Done, 142, 142},
                                      {"25_ride_logs_clear_failed", RideLogWipeStatus::Failed, 139, 142}};
    for (const ClearStage &stage : clearStages) {
      if (!selected(stage.name)) continue;
      uiShow(SCREEN_RIDE_LOGS);
      previewSetWipeStatus(stage.state, stage.removed, stage.total, true);
      uiPreviewShowSdClearProgress();
      cyd::preview::advanceTime(400);  // let the bar finish sliding to its value
      capture(output, stage.name);
      previewSetWipeStatus(RideLogWipeStatus::Idle, 0, 0, false);
      previewRestoreRideLogs();
      if (stopAfterSelected()) return 0;
    }
    if (selected("26_reset_confirm_full")) {
      uiPreviewSetSubmenu(SUBMENU_RESET, 0, false);
      uiPreviewSetResetConfirm(true);
      uiShow(SCREEN_SUBMENU);
      capture(output, "26_reset_confirm_full");
      if (stopAfterSelected()) return 0;
    }
    if (selected("26_reset_confirm_default")) {
      uiPreviewSetSubmenu(SUBMENU_RESET, 0, false);
      uiPreviewSetResetConfirm(false);
      uiShow(SCREEN_SUBMENU);
      capture(output, "26_reset_confirm_default");
      if (stopAfterSelected()) return 0;
    }
    if (selected("26_reset_progress")) {
      uiPreviewSetSubmenu(SUBMENU_RESET, 0, false);
      uiPreviewSetResetCountdown(false);
      uiShow(SCREEN_SUBMENU);
      capture(output, "26_reset_progress");
      if (stopAfterSelected()) return 0;
    }

    if (selected("27_battery_submenu")) { showSubmenu(output, SUBMENU_BATTERY, 0, false, "27_battery_submenu"); if (stopAfterSelected()) return 0; }

    if (selected("28_first_boot_config")) {
      uiPreviewSetConfigStep(0); uiShow(SCREEN_CONFIG); capture(output, "28_first_boot_config");
      if (stopAfterSelected()) return 0;
    }
    if (selected("28_vehicle_name_input")) {
      uiPreviewSetVehicleInput(1); uiShow(SCREEN_TEXT_INPUT); capture(output, "28_vehicle_name_input");
      if (stopAfterSelected()) return 0;
    }
    if (selected("28_battery_capacity_input")) {
      uiPreviewSetVehicleInput(6); uiShow(SCREEN_TEXT_INPUT); capture(output, "28_battery_capacity_input");
      if (stopAfterSelected()) return 0;
    }
    if (selected("29_pin_lock")) {
      uiShow(SCREEN_PIN_LOCK); capture(output, "29_pin_lock"); if (stopAfterSelected()) return 0;
    }
    if (selected("30_config_language")) {
      uiPreviewSetConfigStep(1); uiShow(SCREEN_SUBMENU); capture(output, "30_config_language");
      if (stopAfterSelected()) return 0;
    }
    if (selected("31_config_units")) {
      uiPreviewSetConfigStep(2); uiShow(SCREEN_SUBMENU); capture(output, "31_config_units");
      if (stopAfterSelected()) return 0;
    }
    if (selected("32_config_theme")) {
      uiPreviewSetConfigStep(3); uiShow(SCREEN_SUBMENU); capture(output, "32_config_theme");
      if (stopAfterSelected()) return 0;
    }
    if (selected("33_config_complete")) {
      showRestingDashboard(); uiPreviewShowSetupCompleteNotice(); capture(output, "33_config_complete");
      if (stopAfterSelected()) return 0;
    }

    // The disconnected dashboard. Host-side there is no controller to unplug,
    // so the link state is forced; the values behind the scrim are the stub's
    // and stand in for whatever was last on screen.
    // A controller has connected before, so a lost link is only that.
    if (selected("36_link_lost")) {
      controllerEverConnected = true;
      previewSetTelemetryLink(LINK_LOST);
      showRestingDashboard();
      uiPreviewExpireLinkAlertDelay();
      showDashboard(output, MODE_BIG_READOUT, "36_link_lost");
      previewSetTelemetryLink(LINK_LIVE);
      if (stopAfterSelected()) return 0;
    }

    // A Bluetooth controller with the radio switched off: the Connection page
    // shows the switch off, and the dashboard names where to turn it back on.
    if (selected("20_connection_bluetooth_off")) {
      uiPreviewSetControllerBackend(CONTROLLER_FARDRIVER, CONTROLLER_CONNECTION_BLE);
      bluetoothEnabled = false;
      showSubmenu(output, SUBMENU_CONNECTION, 0, false, "20_connection_bluetooth_off");
      bluetoothEnabled = true;
      uiPreviewSetControllerBackend(CONTROLLER_VESC, CONTROLLER_CONNECTION_UART);
      if (stopAfterSelected()) return 0;
    }
    if (selected("36_bluetooth_off")) {
      uiPreviewSetControllerBackend(CONTROLLER_FARDRIVER, CONTROLLER_CONNECTION_BLE);
      bluetoothEnabled = false;
      controllerEverConnected = true;
      previewSetTelemetryLink(LINK_LOST);
      showRestingDashboard();
      showDashboard(output, MODE_BIG_READOUT, "36_bluetooth_off");
      previewSetTelemetryLink(LINK_LIVE);
      bluetoothEnabled = true;
      uiPreviewSetControllerBackend(CONTROLLER_VESC, CONTROLLER_CONNECTION_UART);
      if (stopAfterSelected()) return 0;
    }

    // After a reset nothing has connected yet. A wired VESC has nothing to
    // pair, so once the short grace has passed the display offers setup.
    if (selected("36_never_connected")) {
      uiPreviewSetDashboardMode(MODE_BIG_READOUT);
      controllerEverConnected = false;
      previewSetTelemetryLink(LINK_WAITING);
      showRestingDashboard();
      // Exercise the same grace-period path as the firmware instead of
      // forcing the overlay and then rebuilding its screen underneath it.
      cyd::preview::advanceTime(3200);
      uiDashboardTick();
      // Let LVGL complete the layout and draw created by that timer tick before
      // copying the framebuffer.
      cyd::preview::advanceTime(50);
      capture(output, "36_never_connected");
      previewSetTelemetryLink(LINK_LIVE);
      controllerEverConnected = true;
      if (stopAfterSelected()) return 0;
    }

    // First boot on a Bluetooth controller: nothing is paired, so the display
    // asks for pairing at once instead of waiting for a link that cannot come.
    if (selected("36_no_controller")) {
      uiPreviewSetControllerBackend(CONTROLLER_VESC, CONTROLLER_CONNECTION_BLE);
      previewSetTelemetryLink(LINK_WAITING);
      showDashboard(output, MODE_BIG_READOUT, "36_no_controller");
      previewSetTelemetryLink(LINK_LIVE);
      uiPreviewSetControllerBackend(CONTROLLER_VESC, CONTROLLER_CONNECTION_UART);
      if (stopAfterSelected()) return 0;
    }

    // A live link with the controller reporting FAULT_CODE_OVER_TEMP_MOTOR (6):
    // readings still mean something, so no scrim and the chip stays low.
    if (selected("37_fault")) {
      previewSetFaultCode(6);
      showDashboard(output, MODE_BIG_READOUT, "37_fault");
      previewSetFaultCode(0);
      if (stopAfterSelected()) return 0;
    }

    if (!requested.empty()) {
      std::cerr << "Unknown screen name: " << requested << "\n";
      return 2;
    }

    std::cout << "Rendered 37 screens with native LVGL to " << output.string() << "\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << "\n";
    return 1;
  }
}
