#include <algorithm>
#include <array>
#include <cstdint>

#include <emscripten.h>

#include <lvgl.h>

#include "Preferences.h"
#include "app_state.h"
#include "host_runtime.h"
#include "screens.h"

extern void previewSetInteractiveMode(bool enabled);
extern void previewSetTelemetryLink(TelemetryLink state);
extern void previewSetFaultCode(uint8_t code);
extern void previewSetDashboardValues(int speedKmh, int watts, float voltage, float current,
                                      int motorTemp, int escTemp, int batteryPercent);
extern void previewSetLightSensor(int raw, int targetPercent);
extern void previewSetCardState(bool ready, bool checking);
extern void previewSetLoggingState(RideLoggingMode mode, bool recording);
extern void previewSetDisplayModuleInfo(const char *info);

namespace {

std::array<uint8_t, cyd::preview::kDisplayWidth * cyd::preview::kDisplayHeight * 4> rgbaFrame = {};
double lastFrameAt = 0.0;

EM_JS(void, presentFrame, (const uint8_t *pixels, int width, int height), {
  const canvas = Module.canvas;
  const context = canvas.getContext('2d', {alpha: false});
  if (!Module.cydImageData) Module.cydImageData = context.createImageData(width, height);
  Module.cydImageData.data.set(HEAPU8.subarray(pixels, pixels + width * height * 4));
  context.putImageData(Module.cydImageData, 0, 0);
});

EM_JS(int, requestedTheme, (), {
  const value = new URLSearchParams(window.location.search).get('theme');
  if (value === null) return -1;
  const parsed = Number.parseInt(value, 10);
  return Number.isFinite(parsed) ? parsed : -1;
});

EM_JS(int, copyRequestedHostInfo, (char *buffer, int capacity), {
  const value = new URLSearchParams(window.location.search).get('host');
  if (!value) return 0;
  return stringToUTF8(value, buffer, capacity);
});

EM_JS(void, reportBrightness, (int percent), {
  if (window.CydAndroid && window.CydAndroid.onBrightnessChanged) {
    window.CydAndroid.onBrightnessChanged(percent);
  }
});

void renderFrame() {
  const lv_color_t *source = cyd::preview::framebuffer();
  for (size_t index = 0; index < cyd::preview::kDisplayWidth * cyd::preview::kDisplayHeight; ++index) {
    lv_color32_t converted;
    converted.full = lv_color_to32(source[index]);
    rgbaFrame[index * 4 + 0] = converted.ch.red;
    rgbaFrame[index * 4 + 1] = converted.ch.green;
    rgbaFrame[index * 4 + 2] = converted.ch.blue;
    rgbaFrame[index * 4 + 3] = 255;
  }
  presentFrame(rgbaFrame.data(), cyd::preview::kDisplayWidth, cyd::preview::kDisplayHeight);
}

// The phone's manual sliders drive the vehicle. Manual input is parked
// (silently, without touching the phone's toggle) for as long as the firmware
// is off the dashboard, and the latest values are replayed on the way back.
struct ManualTelemetry {
  int speedKmh;
  int watts;
  int voltageDeci;
  int motorTemp;
  int escTemp;
  int batteryPercent;
};

struct ManualHardware {
  int controllerConnected;
  int faultActive;
  int cardInserted;
  int logging;
  int autoBrightness;
  int ambientRaw;
};

// Mirrors the phone's DEMO DATA selector. OFF is what a real CYD boots into
// with nothing plugged in, so it is also what the Bluetooth link forces.
enum DemoSource {
  DEMO_SOURCE_OFF = 0,
  DEMO_SOURCE_AUTO = 1,
  DEMO_SOURCE_MANUAL = 2,
  // A real controller on the host's Bluetooth radio. Deliberately not a "demo"
  // source: it is never parked while a menu is open, so menus see the same
  // live telemetry they would on hardware.
  DEMO_SOURCE_LIVE = 3,
};

ManualTelemetry manualTelemetry = {};
ManualHardware manualHardware = {};
bool manualTelemetryReceived = false;
bool manualHardwareReceived = false;
bool manualInputParked = false;
int demoSource = DEMO_SOURCE_OFF;
int reportedBrightness = -1;

bool manualSourceActive() {
  return demoSource == DEMO_SOURCE_MANUAL;
}

bool liveSourceActive() {
  return demoSource == DEMO_SOURCE_LIVE;
}

bool firmwareOnDashboard() {
  return uiCurrentScreen() == SCREEN_DASHBOARD;
}

// `stationary` reports a standing vehicle without discarding the rider's
// settings.
void pushManualTelemetry(bool stationary) {
  if (!manualTelemetryReceived || !manualSourceActive()) return;
  const int speedKmh = stationary ? 0 : manualTelemetry.speedKmh;
  const int watts = stationary ? 0 : manualTelemetry.watts;
  const float voltage = std::max(0, manualTelemetry.voltageDeci) / 10.0F;
  const float current = voltage > 0.1F ? static_cast<float>(watts) / voltage : 0.0F;
  previewSetDashboardValues(speedKmh, watts, voltage, current, manualTelemetry.motorTemp,
                            manualTelemetry.escTemp, manualTelemetry.batteryPercent);
}

void pushManualHardware() {
  if (!manualHardwareReceived || !manualSourceActive()) return;
  previewSetTelemetryLink(manualHardware.controllerConnected ? LINK_LIVE : LINK_LOST);
  previewSetFaultCode(manualHardware.faultActive ? 1 : 0);
  previewSetCardState(manualHardware.cardInserted != 0, false);
  previewSetLoggingState(manualHardware.logging ? RIDE_LOG_ON : RIDE_LOG_OFF,
                         manualHardware.logging != 0);
  autoBrightnessEnabled = manualHardware.autoBrightness != 0;
  previewSetLightSensor(manualHardware.ambientRaw,
                        constrain(manualHardware.ambientRaw * 100 / 4095, 0, 100));
  saveAutoBrightnessSetting();
}

void uiTimer(lv_timer_t *) {
  const bool park = manualSourceActive() && !firmwareOnDashboard();
  if (park != manualInputParked) {
    manualInputParked = park;
    pushManualTelemetry(park);
    if (!park) pushManualHardware();
  }
  // The firmware owns the brightness setting; the host dims its own window to
  // match rather than the firmware knowing anything about a phone.
  const int brightness = static_cast<int>(displayBrightnessPercent);
  if (brightness != reportedBrightness) {
    reportedBrightness = brightness;
    reportBrightness(brightness);
  }
  uiDashboardTick();
  uiAutoReturnTick();
  uiSensorTick();
}

void showDashboard() {
  firstBootConfigured = true;
  applyDashboardCustomization(dashboardMode);
  uiShow(SCREEN_DASHBOARD);
  cyd::preview::refreshNow();
  renderFrame();
}

void mainLoop() {
  const double now = emscripten_get_now();
  const uint32_t elapsed = lastFrameAt == 0.0
                               ? 16
                               : static_cast<uint32_t>(std::clamp(now - lastFrameAt, 1.0, 50.0));
  lastFrameAt = now;
  cyd::preview::advanceTime(elapsed, 5);
  renderFrame();
}

}  // namespace

extern "C" {

EMSCRIPTEN_KEEPALIVE void cyd_pointer_event(int pressed, int x, int y) {
  cyd::preview::setPointer(pressed != 0, x, y);
}

EMSCRIPTEN_KEEPALIVE void cyd_show_dashboard() {
  showDashboard();
}

EMSCRIPTEN_KEEPALIVE void cyd_set_theme(int mode) {
  if (mode < 0 || mode >= MODE_COUNT) return;
  dashboardMode = static_cast<DashboardMode>(mode);
  applyDashboardCustomization(dashboardMode);
  saveAppSettings();
  showDashboard();
}

EMSCRIPTEN_KEEPALIVE void cyd_set_data_field(int mode, int slot, int item) {
  if (mode < 0 || mode >= MODE_COUNT || slot < 0 || slot >= DASH_DATA_SLOTS_MAX ||
      item < 0 || item >= DATA_COUNT)
    return;
  setDashboardDataSelection(static_cast<DashboardMode>(mode), static_cast<uint8_t>(slot),
                            static_cast<DashboardDataItem>(item));
}

EMSCRIPTEN_KEEPALIVE void cyd_set_customization(int mode, int accent, int background,
                                                int gradientPosition, int flags) {
  if (mode < 0 || mode >= MODE_COUNT || accent < 0 || accent >= ACCENT_COUNT ||
      background < 0 || background >= ACCENT_COUNT)
    return;
  DashboardCustomization &custom = dashboardCustomizations[mode];
  custom.accent = static_cast<uint8_t>(accent);
  custom.backgroundAccent = static_cast<uint8_t>(background);
  custom.gradientPosition = static_cast<uint8_t>(constrain(gradientPosition, 15, 85));
  custom.flags = static_cast<uint8_t>(flags & 0x3F);
  dashboardMode = static_cast<DashboardMode>(mode);
  applyDashboardCustomization(dashboardMode);
  saveAppSettings();
  showDashboard();
}

EMSCRIPTEN_KEEPALIVE void cyd_set_demo_source(int source) {
  demoSource = source < DEMO_SOURCE_OFF || source > DEMO_SOURCE_LIVE ? DEMO_SOURCE_OFF : source;
  dashboardDemoModeEnabled = demoSource == DEMO_SOURCE_AUTO;
  if (demoSource == DEMO_SOURCE_MANUAL) {
    manualInputParked = !firmwareOnDashboard();
    pushManualTelemetry(manualInputParked);
    pushManualHardware();
  } else if (demoSource == DEMO_SOURCE_LIVE) {
    // Show the dead link until the first real packet lands, so a controller
    // that never answers looks the way it would on the CYD.
    manualInputParked = false;
    previewSetTelemetryLink(LINK_LOST);
    previewSetFaultCode(0);
    previewSetDashboardValues(0, 0, 0.0F, 0.0F, 0, 0, 0);
  } else if (demoSource == DEMO_SOURCE_OFF) {
    // No source at all: report the dead link a bare CYD shows on boot.
    manualInputParked = false;
    previewSetTelemetryLink(LINK_LOST);
    previewSetFaultCode(0);
    previewSetCardState(false, false);
    previewSetDashboardValues(0, 0, 0.0F, 0.0F, 0, 0, 0);
  }
  saveAppSettings();
  uiDashboardTick();
  uiSensorTick();
}

EMSCRIPTEN_KEEPALIVE void cyd_set_telemetry(int speedKmh, int watts, int voltageDeci,
                                           int motorTemp, int escTemp, int batteryPercent) {
  manualTelemetry = {speedKmh, watts, voltageDeci, motorTemp, escTemp, batteryPercent};
  manualTelemetryReceived = true;
  if (manualInputParked || !manualSourceActive()) return;
  pushManualTelemetry(false);
  uiDashboardTick();
}

EMSCRIPTEN_KEEPALIVE void cyd_set_virtual_hardware(int controllerConnected, int faultActive,
                                                   int cardInserted, int logging,
                                                   int autoBrightness, int ambientRaw) {
  manualHardware = {controllerConnected, faultActive, cardInserted,
                    logging,             autoBrightness, ambientRaw};
  manualHardwareReceived = true;
  if (manualInputParked || !manualSourceActive()) return;
  pushManualHardware();
  uiDashboardTick();
  uiSensorTick();
}

EMSCRIPTEN_KEEPALIVE void cyd_set_live_telemetry(int speedKmh, int watts, int voltageDeci,
                                                 int motorTemp, int escTemp, int batteryPercent,
                                                 int faultCode) {
  if (!liveSourceActive()) return;
  const float voltage = std::max(0, voltageDeci) / 10.0F;
  const float current = voltage > 0.1F ? static_cast<float>(watts) / voltage : 0.0F;
  previewSetTelemetryLink(LINK_LIVE);
  previewSetFaultCode(static_cast<uint8_t>(faultCode));
  previewSetDashboardValues(speedKmh, watts, voltage, current, motorTemp, escTemp, batteryPercent);
  uiDashboardTick();
}

EMSCRIPTEN_KEEPALIVE void cyd_set_live_link(int connected) {
  if (!liveSourceActive()) return;
  previewSetTelemetryLink(connected ? LINK_LIVE : LINK_LOST);
  if (!connected) previewSetDashboardValues(0, 0, 0.0F, 0.0F, 0, 0, 0);
  uiDashboardTick();
}

EMSCRIPTEN_KEEPALIVE void cyd_show_first_boot() {
  firstBootConfigured = false;
  configStep = 0;
  uiShow(SCREEN_CONFIG);
  cyd::preview::refreshNow();
  renderFrame();
}

EMSCRIPTEN_KEEPALIVE void cyd_reset_demo() {
  resetAppSettings(true);
  showDashboard();
}

}  // extern "C"

int main() {
  previewPreferencesConfigure(nullptr, false);
  previewSetInteractiveMode(true);
  cyd::preview::initRuntime();
  char hostInfo[56] = {};
  if (copyRequestedHostInfo(hostInfo, sizeof(hostInfo)) > 0) {
    previewSetDisplayModuleInfo(hostInfo);
  }
  loadAppSettings();
  const int theme = requestedTheme();
  if (theme >= 0 && theme < MODE_COUNT) {
    dashboardMode = static_cast<DashboardMode>(theme);
    applyDashboardCustomization(dashboardMode);
    saveAppSettings();
  }
  loadDisplayPanelProfile();
  loadBatteryStats();
  lv_timer_create(uiTimer, 100, nullptr);
  showDashboard();
  emscripten_set_main_loop(mainLoop, 0, true);
  return 0;
}
