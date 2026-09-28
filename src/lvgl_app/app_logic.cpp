#include "gauge_range_model.h"
#include "app_state.h"

#include <Preferences.h>

#include "config.h"
#include "controller_manager.h"
#include "display_panel_tuning.h"
#include "ui_style.h"

static Preferences preferences;

DashboardMode dashboardMode = MODE_HUD;
AccentTheme accentTheme = ACCENT_DEFAULT;
bool dashboardGradientEnabled = false;
bool dashboardGradientHorizontal = false;
bool dashboardGradientReverse = false;
bool dashboardGradientBell = false;
uint8_t dashboardGradientPosition = 50;
AccentTheme dashboardGradientTheme = ACCENT_DEFAULT;
Language language = LANG_EN;
UnitMode unitMode = UNITS_METRIC;
SubmenuType submenuType = SUBMENU_GENERIC;
char oemName[16] = "KAJO DASH";
char vehicleName[24] = "Vehicle";
char motorName[24] = "Motor";
char controllerName[24] = "VESC";
char vehicleBuildId[24] = "";
uint8_t batterySeriesCount = 20;
uint16_t batteryCapacityDeciAh = 200;
uint16_t batteryMaxAmps = 100;
uint16_t motorMaxAmps = 150;
uint16_t continuousPowerDeciKw = 30;
uint16_t peakPowerDeciKw = 60;
bool automaticGaugeRanges = true;
uint32_t learnedGaugeSpeed[3] = {30,30,30};
uint32_t topSpeedKmh = 100;
uint8_t speedModeCount = 3;
uint32_t speedMode1Kmh = 25;
uint32_t speedMode2Kmh = 50;
uint32_t speedMode3Kmh = 100;
SpeedSetupMode speedSetupMode = SPEED_SETUP_PRESET;
uint8_t speedPresetMode = 2;
uint8_t speedPowerCurvePercent = 100;
uint8_t speedAccelCurvePercent = 60;
bool firstBootConfigured = false;
bool controllerEverConnected = false;
uint16_t wheelDiameterMm = 700;
// Display-side speed trim, percent. 100 = show the controller's figure as-is.
uint16_t speedCalibrationPercent = 100;
uint32_t vescUartBaud = VESC_BAUD;
uint8_t vescCanId = 0;
uint8_t vescMotorPolePairs = VESC_MOTOR_POLE_PAIRS;
uint16_t vescDriveRatioHundredths = (uint16_t)lroundf(VESC_DRIVE_RATIO * 100.0F);
bool pinEnabled = false;
char securityPin[5] = "1234";
uint8_t displayBrightnessPercent = 100;
bool autoBrightnessEnabled = false;
uint16_t lightSensorDarkRaw = CYD_LDR_DARK_RAW;
uint16_t lightSensorBrightRaw = CYD_LDR_BRIGHT_RAW;
bool themeLedEnabled = true;
bool bluetoothEnabled = true;
DisplayPanelProfile displayPanelProfile = DISPLAY_PANEL_STANDARD;
uint8_t displayPanelTuning = PANEL_TUNING_BASELINE;
ControllerType controllerType = CONTROLLER_VESC;
ControllerConnection controllerConnection = CONTROLLER_CONNECTION_UART;
bool developerOptionsEnabled = false;
bool dashboardDemoModeEnabled = false;
bool autoReturnEnabled = true;
uint16_t autoReturnTimeoutSeconds = 30;
uint8_t configStep = 0;
char pinInput[5] = "";
DashboardCustomization dashboardCustomizations[MODE_COUNT] = {};
DashboardAppearanceMode dashboardAppearanceMode = DASH_APPEARANCE_DARK;
static bool autoAppearanceLight = false;
static DashboardMode accentRenderMode = MODE_HUD;

static bool dashboardCustomizationsInitialized = false;
// Increment when Bar Graph profile slot meanings or defaults change. NVS
// survives normal USB/OTA firmware flashing, so an unversioned profile can
// otherwise apply stale prototype-era choices to a newly flashed layout.
static constexpr uint8_t kBarGraphProfileRevision = 1;
static constexpr const char *kBarGraphProfileRevisionKey = "barsProfileRev";
static const uint8_t kDashboardDataCounts[MODE_COUNT] = {
    8, 8, 3, 11, 7, 3, 7, 4, 7, 5, 4, 4,
};
static const DashboardDataItem kDashboardDataDefaults[MODE_COUNT][DASH_DATA_SLOTS_MAX] = {
    {DATA_VOLTAGE, DATA_CURRENT, DATA_MOTOR_TEMP, DATA_ESC_TEMP, DATA_TRIP, DATA_ODOMETER, DATA_AVG_SPEED,
     DATA_RIDE_EFFICIENCY},
    {DATA_VOLTAGE, DATA_CURRENT, DATA_MOTOR_TEMP, DATA_ESC_TEMP, DATA_TRIP, DATA_ODOMETER, DATA_AVG_SPEED,
     DATA_UPTIME},
    {DATA_RANGE, DATA_TRIP, DATA_ODOMETER},
    // Bar Graph: the five meter rows, then the six side readouts.
    {DATA_BATTERY, DATA_SPEED, DATA_POWER, DATA_VOLTAGE, DATA_CURRENT, DATA_MOTOR_TEMP, DATA_ESC_TEMP,
     DATA_TRIP, DATA_ODOMETER, DATA_AVG_SPEED, DATA_UPTIME},
    // Mono: the four metric rows, then the trip/odo/time footer.
    {DATA_POWER, DATA_VOLTAGE, DATA_CURRENT, DATA_MOTOR_TEMP, DATA_TRIP, DATA_ODOMETER, DATA_UPTIME},
    // Pixel: trip in the rail, then range and power along the lower row. Speed,
    // charge and ride mode are fixed instruments, so they are not slots.
    {DATA_TRIP, DATA_RANGE, DATA_POWER},
    {DATA_BATTERY, DATA_VOLTAGE, DATA_CURRENT, DATA_POWER, DATA_TRIP, DATA_MOTOR_TEMP, DATA_ODOMETER},
    {DATA_VOLTAGE, DATA_CURRENT, DATA_MOTOR_TEMP, DATA_RANGE},
    // Redline: charge moved to the status row, so range takes its slot.
    {DATA_MOTOR_TEMP, DATA_VOLTAGE, DATA_CURRENT, DATA_POWER, DATA_TRIP, DATA_RANGE, DATA_ODOMETER},
    {DATA_RIDE_EFFICIENCY, DATA_RANGE, DATA_VOLTAGE, DATA_MOTOR_TEMP, DATA_TRIP},
    {DATA_RANGE, DATA_POWER, DATA_TRIP, DATA_ODOMETER},
    {DATA_RANGE, DATA_RIDE_EFFICIENCY, DATA_RIDE_ENERGY, DATA_REGEN_ENERGY},
};

static void initializeDashboardCustomizations() {
  for (uint8_t mode = 0; mode < MODE_COUNT; mode++) {
    DashboardCustomization &custom = dashboardCustomizations[mode];
    custom.accent = ACCENT_DEFAULT;
    custom.backgroundAccent = ACCENT_DEFAULT;
    custom.gradientPosition = 50;
    custom.flags = 0;
    for (uint8_t slot = 0; slot < DASH_DATA_SLOTS_MAX; slot++) {
      custom.data[slot] = static_cast<uint8_t>(kDashboardDataDefaults[mode][slot]);
    }
  }
  dashboardCustomizationsInitialized = true;
}

static void ensureDashboardCustomizations() {
  if (!dashboardCustomizationsInitialized) initializeDashboardCustomizations();
}

uint8_t dashboardDataSlotCount(DashboardMode mode) {
  return mode < MODE_COUNT ? kDashboardDataCounts[mode] : 0;
}

DashboardDataItem dashboardDataDefault(DashboardMode mode, uint8_t slot) {
  if (mode >= MODE_COUNT || slot >= dashboardDataSlotCount(mode)) return DATA_TRIP;
  return kDashboardDataDefaults[mode][slot];
}

DashboardDataItem dashboardDataSelection(DashboardMode mode, uint8_t slot) {
  ensureDashboardCustomizations();
  if (mode >= MODE_COUNT || slot >= dashboardDataSlotCount(mode)) return DATA_TRIP;
  const uint8_t item = dashboardCustomizations[mode].data[slot];
  return item < DATA_COUNT ? static_cast<DashboardDataItem>(item) : dashboardDataDefault(mode, slot);
}

void setDashboardDataSelection(DashboardMode mode, uint8_t slot, DashboardDataItem item) {
  ensureDashboardCustomizations();
  if (mode >= MODE_COUNT || slot >= dashboardDataSlotCount(mode) || item >= DATA_COUNT) return;
  dashboardCustomizations[mode].data[slot] = static_cast<uint8_t>(item);
}

void captureDashboardCustomization(DashboardMode mode) {
  ensureDashboardCustomizations();
  if (mode >= MODE_COUNT) return;
  DashboardCustomization &custom = dashboardCustomizations[mode];
  custom.accent = static_cast<uint8_t>(accentTheme);
  custom.backgroundAccent = static_cast<uint8_t>(dashboardGradientTheme);
  custom.gradientPosition = dashboardGradientPosition;
  custom.flags = (dashboardGradientEnabled ? 0x01 : 0) | (dashboardGradientHorizontal ? 0x02 : 0) |
                 (dashboardGradientReverse ? 0x04 : 0) | (dashboardGradientBell ? 0x08 : 0) |
                 ((static_cast<uint8_t>(dashboardAppearanceMode) + 1) << 4);
}

void applyDashboardCustomization(DashboardMode mode) {
  ensureDashboardCustomizations();
  if (mode >= MODE_COUNT) return;
  const DashboardCustomization &custom = dashboardCustomizations[mode];
  accentTheme = custom.accent < ACCENT_COUNT ? static_cast<AccentTheme>(custom.accent) : ACCENT_DEFAULT;
  dashboardGradientTheme = custom.backgroundAccent < ACCENT_COUNT
                               ? static_cast<AccentTheme>(custom.backgroundAccent)
                               : ACCENT_DEFAULT;
  dashboardGradientPosition = constrain(custom.gradientPosition, 15, 85);
  dashboardGradientEnabled = (custom.flags & 0x01) != 0;
  dashboardGradientHorizontal = (custom.flags & 0x02) != 0;
  dashboardGradientReverse = (custom.flags & 0x04) != 0;
  dashboardGradientBell = (custom.flags & 0x08) != 0;
  const uint8_t storedAppearance = (custom.flags >> 4) & 0x03;
  if (storedAppearance == 0) {
    // Appearance is stored as mode + 1, so zero means no choice has been
    // captured for this dashboard yet: captureDashboardCustomization() only
    // runs for the dashboard in use, so every profile starts here and returns
    // here after a reset. Fall back to the native look rather than to one
    // mode's preference. Tiles is natively light; every other dashboard is
    // natively dark. This branch is load-bearing, not a migration path.
    dashboardAppearanceMode = mode == MODE_LARGE_TILES ? DASH_APPEARANCE_LIGHT : DASH_APPEARANCE_DARK;
  } else {
    dashboardAppearanceMode = static_cast<DashboardAppearanceMode>(min<uint8_t>(storedAppearance - 1,
                                                                                 DASH_APPEARANCE_AUTO));
  }
}

void resetDashboardCustomization(DashboardMode mode) {
  ensureDashboardCustomizations();
  if (mode >= MODE_COUNT) return;
  DashboardCustomization &custom = dashboardCustomizations[mode];
  custom.accent = ACCENT_DEFAULT;
  custom.backgroundAccent = ACCENT_DEFAULT;
  custom.gradientPosition = 50;
  custom.flags = 0;
  for (uint8_t slot = 0; slot < DASH_DATA_SLOTS_MAX; slot++) {
    custom.data[slot] = static_cast<uint8_t>(kDashboardDataDefaults[mode][slot]);
  }
  applyDashboardCustomization(mode);
}

lv_color_t c565(uint16_t rgb565) {
  const uint8_t r = ((rgb565 >> 11) & 0x1F) * 255 / 31;
  const uint8_t g = ((rgb565 >> 5) & 0x3F) * 255 / 63;
  const uint8_t b = (rgb565 & 0x1F) * 255 / 31;
  return lv_color_make(r, g, b);
}

lv_color_t dashboardColorLv(lv_color_t color) {
  const bool nativeLight = accentRenderMode == MODE_LARGE_TILES;
  if (dashboardLightModeActive() == nativeLight) return color;
  lv_color32_t rgb;
  rgb.full = lv_color_to32(color);
  const lv_color_hsv_t hsv = lv_color_rgb_to_hsv(rgb.ch.red, rgb.ch.green, rgb.ch.blue);
  // Preserve real hues (selected blue stays blue, gauge heat scales stay
  // blue/yellow/red). Only neutral UI furniture changes polarity.
  if (hsv.s >= 38) return color;
  return lv_color_make(255 - rgb.ch.red, 255 - rgb.ch.green, 255 - rgb.ch.blue);
}

bool dashboardLightModeActive() {
  if (dashboardAppearanceMode == DASH_APPEARANCE_LIGHT) return true;
  if (dashboardAppearanceMode == DASH_APPEARANCE_DARK) return false;
  const int target = lightSensorTargetPct();
  if (target >= 62) autoAppearanceLight = true;
  else if (target <= 42) autoAppearanceLight = false;
  return autoAppearanceLight;
}

// The Default accent depends on the dashboard theme. Previews render a
// different mode than the saved dashboardMode, so accent resolution follows
// the mode actually being rendered (set by every screen/dashboard build).
static bool chromeAccentActive = false;
static constexpr uint16_t kHudDefaultAccentDark565 = 0x79A0;

void setAccentRenderMode(DashboardMode mode) {
  accentRenderMode = mode;
}

void setUiChromeAccent(bool enabled) { chromeAccentActive = enabled; }

bool uiChromeAccentEnabled() { return chromeAccentActive; }

static uint16_t defaultAccentColor565() {
  if (dashboardLightModeActive()) {
    // Light mode keeps every dashboard's stock identity, but uses deeper
    // versions of luminous dark-mode accents so they remain distinct against
    // white. This table applies only to ACCENT_DEFAULT; explicit user colors
    // continue through the normal shared accent palette unchanged.
    switch (accentRenderMode) {
      case MODE_GAUGE:
      case MODE_BARS:
      case MODE_PIXEL_GAUGE:
        return 0x1484;  // deep leaf green
      case MODE_PIXEL_MONO:
        return 0xFFFF;  // neutral inversion resolves this to black
      case MODE_LARGE_TILES:
        return 0x04B4;  // stronger tile blue
      case MODE_BIG_READOUT:
        return 0xFFFF;  // neutral inversion resolves to Tiles-style black
      case MODE_REDLINE:
        return 0xC0E3;  // deep signal red
      case MODE_TRACE:
        return 0x0450;  // dark cyan
      case MODE_MINIMAL:
        return 0x1376;  // strong instrument blue
      case MODE_EFFICIENCY:
        return 0x0450;  // dark cyan, preserving the efficiency identity
      case MODE_SIMPLE:
        return 0xFFFF;  // neutral inversion resolves to black
      case MODE_HUD:
        return 0xFFFF;  // neutral inversion resolves the default light HUD to black
      default:
        return 0xFFFF;  // neutral inversion resolves to black
    }
  }
  switch (accentRenderMode) {
    case MODE_GAUGE:
    case MODE_BARS:
      return COLOR565_GREEN;
    case MODE_PIXEL_GAUGE:
      // Phosphor lime rather than the pure RGB green the gauges use: against
      // Pixel's cream numerals it reads as a lit LCD instead of a signal light.
      return 0x7E84;
    case MODE_PIXEL_MONO:
      return 0xFFFF;
    case MODE_LARGE_TILES:
      return 0x1D9F;
    case MODE_BIG_READOUT:
      return 0xFFE0;
    case MODE_REDLINE:
      return 0xF800;
    case MODE_TRACE:
      return 0x05FF;
    case MODE_MINIMAL:
      return 0xFFFF;
    case MODE_EFFICIENCY:
      return 0x05FF;  // bright cyan keeps speed/history distinct from the efficiency heat scale
    case MODE_SIMPLE:
      return 0xFB40;  // fixed menu/status amber
    case MODE_HUD:
      return cyd_ui::kChromeAccent565;
    default:
      return COLOR565_ORANGE;
  }
}

static uint16_t defaultAccentDarkColor565() {
  if (dashboardLightModeActive()) {
    // Companion shades for outlines, inactive tracks and filled panels in the
    // light palette. Keep these colored rather than neutral where possible so
    // dashboardColorLv does not polarity-flip the selected theme hue.
    switch (accentRenderMode) {
      case MODE_GAUGE:
      case MODE_BARS:
      case MODE_PIXEL_GAUGE:
        return 0x0AC2;
      case MODE_PIXEL_MONO:
        return 0xBDF7;  // neutral inversion resolves to a dark grey
      case MODE_LARGE_TILES:
        return 0x02AA;
      case MODE_BIG_READOUT:
        return 0xBDF7;
      case MODE_REDLINE:
        return 0x7042;
      case MODE_TRACE:
        return 0x0228;
      case MODE_MINIMAL:
        return 0x09AB;
      case MODE_EFFICIENCY:
        return 0x0228;
      case MODE_SIMPLE:
        return 0xBDF7;
      case MODE_HUD:
        return 0xFFFF;  // black outlines on white gauge faces
      default:
        return 0xBDF7;
    }
  }
  switch (accentRenderMode) {
    case MODE_GAUGE:
    case MODE_BARS:
    case MODE_PIXEL_GAUGE:
      return 0x03A0;
    case MODE_PIXEL_MONO:
      return 0x7BEF;
    case MODE_LARGE_TILES:
      return 0x0C74;
    case MODE_BIG_READOUT:
      return 0x8400;
    case MODE_REDLINE:
      return 0x7800;
    case MODE_TRACE:
      return 0x0330;
    case MODE_MINIMAL:
      return 0x4208;
    case MODE_EFFICIENCY:
      return 0x0330;
    case MODE_SIMPLE:
      return 0x4208;  // neutral menu/status grey
    case MODE_HUD:
      return kHudDefaultAccentDark565;
    default:
      return 0x7A80;
  }
}

uint16_t accentColor565() {
  switch (accentTheme) {
    case ACCENT_DEFAULT:
      return defaultAccentColor565();
    case ACCENT_BLUE:
      return 0x04DF;
    case ACCENT_GREEN:
      return 0x07E0;
    case ACCENT_PURPLE:
      return 0xA01F;
    case ACCENT_RED:
      return 0xF800;
    case ACCENT_CYAN:
      return 0x07FF;
    case ACCENT_YELLOW:
      return 0xFFE0;
    case ACCENT_WHITE:
      return 0xFFFF;
    case ACCENT_MAGENTA:
      return 0xF81F;
    case ACCENT_ORANGE:
    default:
      return COLOR565_ORANGE;
  }
}

uint16_t accentDarkColor565() {
  switch (accentTheme) {
    case ACCENT_DEFAULT:
      return defaultAccentDarkColor565();
    case ACCENT_BLUE:
      return 0x0255;
    case ACCENT_GREEN:
      return 0x03A0;
    case ACCENT_PURPLE:
      return 0x500F;
    case ACCENT_RED:
      return 0x7800;
    case ACCENT_CYAN:
      return 0x0410;
    case ACCENT_YELLOW:
      return 0x8400;
    case ACCENT_WHITE:
      return 0x7BEF;
    case ACCENT_MAGENTA:
      return 0x8010;
    case ACCENT_ORANGE:
    default:
      return 0x7A80;
  }
}

// Colour a given accent choice would produce, without disturbing the current
// one. ACCENT_DEFAULT resolves through the theme being rendered, so its swatch
// shows the colour that choice actually gives.
lv_color_t accentSwatchLv(AccentTheme theme) {
  const AccentTheme saved = accentTheme;
  accentTheme = theme;
  const uint16_t color = accentColor565();
  accentTheme = saved;
  return c565(color);
}

lv_color_t gradientAccentLv() {
  const AccentTheme saved = accentTheme;
  accentTheme = dashboardGradientTheme;
  // Background previews must show the selected dashboard colour even while
  // the surrounding menu chrome is locked to its fixed amber palette.
  const lv_color_t color = c565(accentColor565());
  accentTheme = saved;
  return color;
}

// A colour for a second data series that stays clearly apart from the first
// whatever accent is chosen. themeColor() collapses onto the accent as soon as
// one is picked, which left the Trace plot drawing both lines in one colour.
// Rotating the hue three-quarters round keeps the pairing readable and happens
// to leave the default cyan paired with yellow, as before. Greys have no hue to
// rotate, so they fall back to amber.
lv_color_t seriesContrastLv(lv_color_t base) {
  lv_color32_t rgb;
  rgb.full = lv_color_to32(base);
  lv_color_hsv_t hsv = lv_color_rgb_to_hsv(rgb.ch.red, rgb.ch.green, rgb.ch.blue);
  if (hsv.s < 25) return c565(0xFD20);
  hsv.h = (uint16_t)((hsv.h + 225) % 360);
  hsv.s = max<uint8_t>(hsv.s, 85);  // stay saturated enough to read on near-black
  hsv.v = 100;
  return lv_color_hsv_to_rgb(hsv.h, hsv.s, hsv.v);
}

lv_color_t accentLv() {
  if (chromeAccentActive) return c565(0xFB40);  // WAITING FOR VESC amber
  return dashboardColorLv(c565(accentColor565()));
}

lv_color_t accentDarkLv() {
  if (chromeAccentActive) return c565(0x4208);  // neutral grey from the fixed menu palette
  return dashboardColorLv(c565(accentDarkColor565()));
}

// Values follow VescUart's mc_fault_code enum. Only the codes a rider can act
// on are translated; the rest keep their VESC spelling, because someone looking
// up FAULT_CODE_RESOLVER_LOS wants the term the controller's own documentation
// uses, not a paraphrase of it.
const char *telemetryFaultLabel(uint8_t code) {
  switch (code) {
    case 0:
      return "";
    case 1:  // FAULT_CODE_OVER_VOLTAGE
      return txt("OVER-VOLTAGE", "YLIJÄNNITE", "ÜBERSPANNUNG", "SURTENSION", "SOBRETENSIÓN",
                 "SOVRATENSIONE");
    case 2:  // FAULT_CODE_UNDER_VOLTAGE
      return txt("UNDER-VOLTAGE", "ALIJÄNNITE", "UNTERSPANNUNG", "SOUS-TENSION", "SUBTENSIÓN",
                 "SOTTOTENSIONE");
    case 3:  // FAULT_CODE_DRV
      return txt("DRIVER FAULT", "OHJAINVIKA", "TREIBERFEHLER", "DÉFAUT DRIVER", "FALLO DRIVER",
                 "GUASTO DRIVER");
    case 4:  // FAULT_CODE_ABS_OVER_CURRENT
      return txt("OVER-CURRENT", "YLIVIRTA", "ÜBERSTROM", "SURINTENSITÉ", "SOBRECORRIENTE",
                 "SOVRACORRENTE");
    case 5:  // FAULT_CODE_OVER_TEMP_FET
      return txt("CONTROLLER HOT", "OHJAIN KUUMA", "REGLER HEISS", "CONTRÔLEUR CHAUD",
                 "CONTROLADOR CALIENTE", "CONTROLLER CALDO");
    case 6:  // FAULT_CODE_OVER_TEMP_MOTOR
      return txt("MOTOR HOT", "MOOTTORI KUUMA", "MOTOR HEISS", "MOTEUR CHAUD", "MOTOR CALIENTE",
                 "MOTORE CALDO");
    case 7:
      return "GATE DRIVER OV";
    case 8:
      return "GATE DRIVER UV";
    case 9:
      return "MCU UNDER-VOLTAGE";
    case 10:
      return "WATCHDOG RESET";
    case 11:
      return "ENCODER SPI";
    case 12:
      return "ENCODER AMP LOW";
    case 13:
      return "ENCODER AMP HIGH";
    case 14:
      return "FLASH CORRUPTION";
    case 15:
    case 16:
    case 17:
      return "CURRENT SENSOR";
    case 18:
      return "UNBALANCED CURRENTS";
    case 19:
      return "BRK";
    case 20:
      return "RESOLVER LOT";
    case 21:
      return "RESOLVER DOS";
    case 22:
      return "RESOLVER LOS";
    case 23:
      return "FLASH CORRUPT APPCFG";
    case 24:
      return "FLASH CORRUPT MCCFG";
    default:
      return txt("CONTROLLER FAULT", "OHJAINVIKA", "REGLERFEHLER", "DÉFAUT CONTRÔLEUR",
                 "FALLO CONTROLADOR", "GUASTO CONTROLLER");
  }
}

lv_color_t batteryLevelColorLv(int percent, lv_color_t goodColor) {
  if (!chromeAccentActive && accentRenderMode == MODE_HUD && accentTheme == ACCENT_DEFAULT &&
      dashboardLightModeActive()) return lv_color_black();
  if (percent <= 15) return c565(0xF800);
  if (percent <= 35) return c565(0xFFE0);
  return goodColor;
}

const char *txt(const char *en, const char *fi, const char *de, const char *fr, const char *es, const char *it) {
  switch (language) {
    case LANG_FI:
      return fi ? fi : en;
    case LANG_DE:
      return de ? de : en;
    case LANG_FR:
      return fr ? fr : en;
    case LANG_ES:
      return es ? es : en;
    case LANG_IT:
      return it ? it : en;
    case LANG_EN:
    default:
      return en;
  }
}

const char *modeName(DashboardMode mode) {
  switch (mode) {
    case MODE_EFFICIENCY:
      return txt("Efficiency", "Tehokkuus", "Effizienz", "Efficacité", "Eficiencia", "Efficienza");
    case MODE_MINIMAL:
      return txt("Gauge", "Mittari", "Tacho", "Cadran", "Medidor", "Quadrante");
    case MODE_TRACE:
      return txt("Trace", "Käyrä", "Verlauf", "Tracé", "Trazo", "Tracciato");
    case MODE_REDLINE:
      return txt("Redline", "Redline");
    case MODE_BIG_READOUT:
      return txt("Ride Console", "Ajokonsoli", "Fahrkonsole", "Console", "Consola", "Console");
    case MODE_LARGE_TILES:
      return txt("Tiles", "Ruudut", "Kacheln", "Tuiles", "Paneles", "Riquadri");
    case MODE_PIXEL_GAUGE:
      return "Pixel";
    case MODE_PIXEL_MONO:
      return "Mono";
    case MODE_BARS:
      return txt("Bar Graph", "Palkit", "Balken", "Barres", "Barras", "Barre");
    case MODE_SIMPLE:
      return txt("Simple", "Selkeä", "Einfach", "Simple", "Sencillo", "Semplice");
    case MODE_GAUGE:
      return txt("Dual Gauge", "Tuplamittari", "Doppelanzeige", "Double jauge", "Doble indicador",
                 "Doppio indicatore");
    case MODE_HUD:
    default:
      return txt("Cyber HUD", "Cyber HUD");
  }
}

const char *themeName() {
  return modeName(dashboardMode);
}

const char *languageName() {
  switch (language) {
    case LANG_FI:
      return "Suomi";
    case LANG_DE:
      return "Deutsch";
    case LANG_FR:
      return "Français";
    case LANG_ES:
      return "Español";
    case LANG_IT:
      return "Italiano";
    case LANG_EN:
    default:
      return "English";
  }
}

const char *accentName() {
  switch (accentTheme) {
    case ACCENT_DEFAULT:
      return txt("Default", "Oletus", "Standard", "Défaut", "Predeterminado", "Predefinito");
    case ACCENT_BLUE:
      return txt("Blue", "Sininen", "Blau", "Bleu", "Azul", "Blu");
    case ACCENT_GREEN:
      return txt("Green", "Vihreä", "Grün", "Vert", "Verde", "Verde");
    case ACCENT_PURPLE:
      return txt("Purple", "Violetti", "Violett", "Violet", "Morado", "Viola");
    case ACCENT_RED:
      return txt("Red", "Punainen", "Rot", "Rouge", "Rojo", "Rosso");
    case ACCENT_CYAN:
      return "Cyan";
    case ACCENT_YELLOW:
      return txt("Yellow", "Keltainen", "Gelb", "Jaune", "Amarillo", "Giallo");
    case ACCENT_WHITE:
      return txt("White", "Valkoinen", "Weiß", "Blanc", "Blanco", "Bianco");
    case ACCENT_MAGENTA:
      return "Magenta";
    case ACCENT_ORANGE:
    default:
      return txt("Orange", "Oranssi", "Orange", "Orange", "Naranja", "Arancione");
  }
}

const char *unitName() {
  switch (unitMode) {
    case UNITS_IMPERIAL:
      return txt("Imperial", "Imperiaalinen", "Imperial", "Imperial", "Imperial", "Imperiale");
    case UNITS_NAUTICAL:
      return txt("Nautical", "Merenkulku", "Nautisch", "Nautique", "Náutico", "Nautico");
    case UNITS_MACH:
      return "Mach";
    case UNITS_METRIC:
    default:
      return txt("Metric", "Metrinen", "Metrisch", "Métrique", "Métrico", "Metrico");
  }
}

const char *pinStatusName() {
  return pinEnabled ? txt("Enabled", "Käytössä", "Aktiv", "Activé", "Activado", "Attivo")
                    : txt("Disabled", "Pois", "Aus", "Désactivé", "Desactivado", "Disattivo");
}

const char *metricSpeedLabel() {
  return txt("SPEED", "NOPEUS", "TEMPO", "VITESSE", "VELOCIDAD", "VELOCITÀ");
}

const char *metricPowerLabel() {
  return txt("POWER", "TEHO", "LEISTUNG", "PUISSANCE", "POTENCIA", "POTENZA");
}

const char *metricVoltsLabel() {
  return txt("VOLTS", "VOLTIT", "VOLT", "VOLTS", "VOLTIOS", "VOLT");
}

const char *metricAmpsLabel() {
  return txt("AMPS", "AMPEERIT", "AMPERE", "AMPÈRES", "AMPERIOS", "AMPERE");
}

const char *metricMotorLabel() {
  return txt("MOTOR", "MOOTTORI", "MOTOR", "MOTEUR", "MOTOR", "MOTORE");
}

const char *metricBatteryLabel() {
  return txt("BATTERY", "AKKU", "BATTERIE", "BATTERIE", "BATERÍA", "BATTERIA");
}

const char *metricTempLabel() {
  return txt("TEMP", "LÄMPÖ", "TEMP.", "TEMP.", "TEMP.", "TEMP.");
}

const char *metricTripLabel() {
  return txt("TRIP", "MATKA", "STRECKE", "TRAJET", "VIAJE", "VIAGGIO");
}

const char *metricUptimeLabel() {
  return txt("UPTIME", "AIKA", "LAUFZEIT", "DURÉE", "TIEMPO", "DURATA");
}

const char *metricTimeLabel() {
  return txt("TIME", "AIKA", "ZEIT", "TEMPS", "TIEMPO", "TEMPO");
}

const char *metricPowerLabelShort() {
  return txt("POWER", "TEHO", "LEIST.", "PUISS.", "POT.", "POT.");
}

const char *metricVoltsLabelShort() {
  return txt("VOLTS", "VOLTIT", "VOLT", "VOLTS", "VOLT.", "VOLT");
}

const char *metricBatteryAmpsLabel() {
  return txt("BATTERY AMPS", "AKKUVIRTA", "AKKUSTROM", "COURANT BATT.", "AMP. BATERÍA", "AMP. BATTERIA");
}

const char *metricPhaseAmpsLabel() {
  return txt("PHASE AMPS", "VAIHEVIRTA", "PHASENSTROM", "COURANT PHASE", "AMP. FASE", "AMP. FASE");
}

const char *metricAmpsLabelShort() {
  return txt("AMPS", "VIRTA", "STROM", "AMP.", "AMP.", "AMP.");
}

const char *metricTempLabelShort() {
  return txt("TEMP", "LÄMPÖ", "TEMP.", "TEMP.", "TEMP.", "TEMP.");
}

const char *dashboardDataLabel(DashboardDataItem item) {
  if (item == DATA_RIDE_MODE) return txt("MODE", "TILA", "MODUS", "MODE", "MODO", "MODO");
  switch (item) {
    case DATA_TRIP:
      return metricTripLabel();
    case DATA_ODOMETER:
      return "ODO";
    case DATA_AVG_SPEED:
      return metricAvgLabel();
    case DATA_SPEED:
      return metricSpeedLabel();
    case DATA_RANGE:
      return metricRangeLabel();
    case DATA_RIDE_EFFICIENCY:
      return txt("RIDE EFF.", "AJON KUL.", "FAHRTVERBR.", "CONSO TRAJET", "CONS. VIAJE", "CONS. VIAGGIO");
    case DATA_LIFETIME_EFFICIENCY:
      return txt("LIFETIME AVG", "KESKIARVO", "GESAMT-SCHNITT", "MOYENNE", "PROMEDIO", "MEDIA");
    case DATA_RIDE_ENERGY:
      return txt("RIDE ENERGY", "AJON ENERGIA", "FAHRTENERGIE", "ÉNERGIE", "ENERGÍA", "ENERGIA");
    case DATA_REGEN_ENERGY:
      return txt("RECOVERED", "PALAUTETTU", "REKUPERIERT", "RÉCUPÉRÉE", "RECUPERADA", "RECUPERATA");
    case DATA_VOLTAGE:
      return metricVoltsLabel();
    case DATA_CURRENT:
      return metricBatteryAmpsLabel();
    case DATA_MOTOR_CURRENT:
      return metricPhaseAmpsLabel();
    case DATA_MOTOR_TEMP:
      return metricMotorLabel();
    case DATA_ESC_TEMP:
      return "ESC";
    case DATA_BATTERY:
      return metricBatteryLabel();
    case DATA_UPTIME:
      return metricUptimeLabel();
    case DATA_POWER:
      return metricPowerLabel();
    case DATA_LEARNED_CAPACITY:
      return txt("CAPACITY", "KAPASIT.", "KAPAZITÄT", "CAPACITÉ", "CAPACIDAD", "CAPACITÀ");
    case DATA_PACK_RESISTANCE:
      return txt("RESIST.", "VASTUS", "WIDERST.", "RÉSIST.", "RESIST.", "RESIST.");
    default:
      return metricTripLabel();
  }
}

// ── Battery energy, range and wear ────────────────────────────────────────────
//
// Everything here is fed from the VESC's own counters, so the numbers are
// measured rather than modelled. Two things make that less trivial than it
// sounds: the controller's counters restart at zero on every VESC reboot, and
// its voltage is measured under load. Deltas are therefore banked into totals
// kept on this side (NVS namespace "batt"), and the state of charge is seeded
// from resting voltage then carried forward by counting charge.

// The telemetry task drives this from batteryStatsUpdate(), but the LVGL task
// reaches it too: Reset with history calls resetBatteryStats(). Two tasks in
// one Preferences handle at once is not safe, and the totals would tear as
// well, so both are taken under a lock. It cannot be a portMUX critical
// section — NVS writes block, and a critical section may not.
//
// Recursive because batteryStatsUpdate() lazily calls loadBatteryStats().
#ifdef CYD_LVGL_PREVIEW
// The preview renderer runs one thread. The empty destructor keeps this a
// scope guard like the firmware lock, so MSVC does not report every
// `BatteryLock lock;` as an unreferenced local variable.
struct BatteryLock {
  ~BatteryLock() {}
};
#else
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
// Constructed during static init, which on ESP32 runs after the scheduler
// starts and well before telemetryTask is created in setup().
static SemaphoreHandle_t batteryMutex = xSemaphoreCreateRecursiveMutex();
struct BatteryLock {
  BatteryLock() {
    if (batteryMutex) xSemaphoreTakeRecursive(batteryMutex, portMAX_DELAY);
  }
  ~BatteryLock() {
    if (batteryMutex) xSemaphoreGiveRecursive(batteryMutex);
  }
  BatteryLock(const BatteryLock &) = delete;
  BatteryLock &operator=(const BatteryLock &) = delete;
};
#endif

static Preferences batteryPreferences;  // guarded by BatteryLock
static BatteryStats batteryStats;
static bool batteryStatsLoaded = false;
static bool batteryCountersPrimed = false;

static float lastAmpHours = 0.0F;
static float lastAmpHoursCharged = 0.0F;
static float lastWattHours = 0.0F;
static float lastWattHoursCharged = 0.0F;
static float lastOdometerKm = 0.0F;
static float lifetimeAhThroughput = 0.0F;

static float socSeedPercent = -1.0F;    // state of charge at the last resting reading
static float socAhSinceSeed = 0.0F;     // net charge drawn since that reading
static uint32_t restingSinceMs = 0;
static float lastPackVoltage = 0.0F;
static float lastPackCurrent = 0.0F;
static float capacityStartSoc = -1.0F;  // deep-cycle capacity measurement in progress
static float capacityAhUsed = 0.0F;
static uint32_t batteryPersistedAtMs = 0;

constexpr uint32_t kBatteryHistoryMagic = 0x31544142UL;  // "BAT1" in little-endian flash
constexpr uint16_t kBatteryHistoryVersion = 1;
constexpr uint32_t kBatteryPersistIntervalMs = 10UL * 60UL * 1000UL;
constexpr char kBatteryHistoryKey[] = "history";

#pragma pack(push, 1)
struct BatteryHistoryRecord {
  uint32_t magic;
  uint16_t version;
  uint16_t bytes;
  uint32_t sequence;
  float lifetimeWh;
  float lifetimeKm;
  float lifetimeAh;
  float packMilliOhm;
  float learnedCapacityAh;
  uint8_t learnedSamples;
  uint8_t reserved[3];
  uint32_t crc;
};
#pragma pack(pop)

static_assert(sizeof(BatteryHistoryRecord) == 40, "Battery history record format changed");

static BatteryHistoryRecord persistedBatteryHistory = {};
static bool batteryHistorySnapshotValid = false;

static uint32_t batteryHistoryCrc32(const uint8_t *data, size_t size) {
  uint32_t crc = 0xFFFFFFFFUL;
  while (size--) {
    crc ^= *data++;
    for (uint8_t bit = 0; bit < 8; bit++) crc = (crc >> 1) ^ (0xEDB88320UL & (0U - (crc & 1U)));
  }
  return ~crc;
}

static BatteryHistoryRecord currentBatteryHistoryRecord(uint32_t sequence) {
  BatteryHistoryRecord record = {};
  record.magic = kBatteryHistoryMagic;
  record.version = kBatteryHistoryVersion;
  record.bytes = sizeof(record);
  record.sequence = sequence;
  record.lifetimeWh = batteryStats.lifetimeWh;
  record.lifetimeKm = batteryStats.lifetimeKm;
  record.lifetimeAh = lifetimeAhThroughput;
  record.packMilliOhm = batteryStats.packMilliOhm;
  record.learnedCapacityAh = batteryStats.learnedCapacityAh;
  record.learnedSamples = batteryStats.learnedSamples;
  record.crc = batteryHistoryCrc32(reinterpret_cast<const uint8_t *>(&record),
                                  sizeof(record) - sizeof(record.crc));
  return record;
}

static bool batteryHistoryRecordValid(const BatteryHistoryRecord &record) {
  return record.magic == kBatteryHistoryMagic && record.version == kBatteryHistoryVersion &&
         record.bytes == sizeof(record) &&
         record.crc == batteryHistoryCrc32(reinterpret_cast<const uint8_t *>(&record),
                                           sizeof(record) - sizeof(record.crc)) &&
         isfinite(record.lifetimeWh) && isfinite(record.lifetimeKm) && isfinite(record.lifetimeAh) &&
         isfinite(record.packMilliOhm) && isfinite(record.learnedCapacityAh) && record.lifetimeKm >= 0.0F &&
         record.lifetimeAh >= 0.0F && record.packMilliOhm >= 0.0F && record.learnedCapacityAh >= 0.0F;
}

static bool batteryHistoryChanged(bool meaningfulOnly) {
  if (!batteryHistorySnapshotValid) return true;
  if (batteryStats.learnedSamples != persistedBatteryHistory.learnedSamples) return true;
  if (!meaningfulOnly) {
    return batteryStats.lifetimeWh != persistedBatteryHistory.lifetimeWh ||
           batteryStats.lifetimeKm != persistedBatteryHistory.lifetimeKm ||
           lifetimeAhThroughput != persistedBatteryHistory.lifetimeAh ||
           batteryStats.packMilliOhm != persistedBatteryHistory.packMilliOhm ||
           batteryStats.learnedCapacityAh != persistedBatteryHistory.learnedCapacityAh;
  }
  return fabsf(batteryStats.lifetimeWh - persistedBatteryHistory.lifetimeWh) >= 1.0F ||
         fabsf(batteryStats.lifetimeKm - persistedBatteryHistory.lifetimeKm) >= 0.1F ||
         fabsf(lifetimeAhThroughput - persistedBatteryHistory.lifetimeAh) >= 0.05F ||
         fabsf(batteryStats.packMilliOhm - persistedBatteryHistory.packMilliOhm) >= 0.5F ||
         fabsf(batteryStats.learnedCapacityAh - persistedBatteryHistory.learnedCapacityAh) >= 0.05F;
}

// Generic Li-ion open-circuit curve, 0..100 % in 10 % steps. The old linear
// ramp from 3.2 V to 4.2 V read far too low through the middle of the pack's
// range, where the real curve barely moves.
static const float kOcvCurve[11] = {3.20F, 3.45F, 3.55F, 3.62F, 3.68F,  3.74F,
                                    3.80F, 3.88F, 3.96F, 4.06F, 4.18F};

static float packCapacityAh() {
  return batteryCapacityDeciAh / 10.0F;
}

static float packCapacityWh() {
  return packCapacityAh() * batterySeriesCount * 3.6F;  // 3.6 V nominal per cell
}

static float socFromCellVoltage(float cellVoltage) {
  if (cellVoltage <= kOcvCurve[0]) return 0.0F;
  for (int i = 1; i < 11; i++) {
    if (cellVoltage < kOcvCurve[i]) {
      const float span = kOcvCurve[i] - kOcvCurve[i - 1];
      const float within = span > 0.0F ? (cellVoltage - kOcvCurve[i - 1]) / span : 0.0F;
      return (i - 1 + within) * 10.0F;
    }
  }
  return 100.0F;
}

// A reading that moved backwards means the controller restarted, so the whole
// new value is the delta.
static float bankCounterDelta(float now, float &last) {
  const float delta = now < last ? now : now - last;
  last = now;
  return delta > 0.0F ? delta : 0.0F;
}

// Between two samples the pack's own resistance is the only thing moving
// voltage against current. Small steps are mostly noise, so only sizeable ones
// count and the result is averaged.
static void learnPackResistance(float voltage, float current) {
  const float deltaI = current - lastPackCurrent;
  const float deltaV = voltage - lastPackVoltage;
  if (fabsf(deltaI) >= 5.0F) {
    const float milliOhm = -deltaV / deltaI * 1000.0F;
    if (milliOhm > 5.0F && milliOhm < 500.0F) {
      batteryStats.packMilliOhm = batteryStats.packMilliOhm <= 0.0F
                                      ? milliOhm
                                      : batteryStats.packMilliOhm * 0.9F + milliOhm * 0.1F;
    }
  }
  lastPackVoltage = voltage;
  lastPackCurrent = current;
}

static void seedStateOfCharge(float voltage) {
  if (batterySeriesCount == 0) return;
  socSeedPercent = socFromCellVoltage(voltage / batterySeriesCount);
  socAhSinceSeed = 0.0F;
  // a full pack starts a capacity measurement; it completes if the ride gets
  // deep enough to make the arithmetic meaningful
  if (socSeedPercent >= 90.0F) {
    capacityStartSoc = socSeedPercent;
    capacityAhUsed = 0.0F;
  }
}

static void updateStateOfCharge(float voltage, float current, float netAh) {
  if (batterySeriesCount == 0) {
    batteryStats.socPercent = -1;
    return;
  }
  socAhSinceSeed += netAh;
  if (capacityStartSoc >= 0.0F) capacityAhUsed += netAh;

  // Resting long enough for the pack to recover: re-seed from open-circuit
  // voltage, which is the only reading that maps cleanly onto the curve.
  const uint32_t now = millis();
  if (fabsf(current) < 1.0F) {
    if (restingSinceMs == 0) restingSinceMs = now;
    if (now - restingSinceMs >= 3000UL) seedStateOfCharge(voltage);
  } else {
    restingSinceMs = 0;
  }

  float soc;
  if (socSeedPercent >= 0.0F && packCapacityAh() > 0.0F) {
    soc = socSeedPercent - socAhSinceSeed / packCapacityAh() * 100.0F;
  } else {
    // no resting reading yet: undo the sag with the learned resistance
    const float compensated = voltage + current * batteryStats.packMilliOhm / 1000.0F;
    soc = socFromCellVoltage(compensated / batterySeriesCount);
  }
  batteryStats.socPercent = (int)constrain((int)lroundf(soc), 0, 100);

  // A deep enough discharge measures real capacity: the charge that came out
  // divided by the fraction of the pack it emptied.
  if (capacityStartSoc >= 0.0F && soc <= 25.0F && capacityAhUsed > 0.5F) {
    const float usedFraction = (capacityStartSoc - soc) / 100.0F;
    if (usedFraction > 0.5F) {
      const float measured = capacityAhUsed / usedFraction;
      batteryStats.learnedCapacityAh =
          batteryStats.learnedSamples == 0
              ? measured
              : (batteryStats.learnedCapacityAh * batteryStats.learnedSamples + measured) /
                    (batteryStats.learnedSamples + 1);
      if (batteryStats.learnedSamples < 255) batteryStats.learnedSamples++;
    }
    capacityStartSoc = -1.0F;
  }
}

static void refreshDerivedBatteryStats() {
  batteryStats.tripWhPerKm = batteryStats.tripKm >= 0.1F ? batteryStats.tripWh / batteryStats.tripKm : 0.0F;
  batteryStats.lifetimeWhPerKm =
      batteryStats.lifetimeKm >= 1.0F ? batteryStats.lifetimeWh / batteryStats.lifetimeKm : 0.0F;
  batteryStats.equivalentCycles = packCapacityAh() > 0.0F ? lifetimeAhThroughput / packCapacityAh() : 0.0F;

  // this ride's consumption once it means something, otherwise the long-run
  // average, so range works from the first metres instead of after a warm-up
  const float whPerKm = (batteryStats.tripKm >= 0.3F && batteryStats.tripWhPerKm > 0.0F)
                            ? batteryStats.tripWhPerKm
                            : batteryStats.lifetimeWhPerKm;
  if (whPerKm > 0.0F && batteryStats.socPercent >= 0) {
    const float remainingWh = packCapacityWh() * batteryStats.socPercent / 100.0F;
    batteryStats.rangeKm = (int)lroundf(remainingWh / whPerKm);
  } else {
    batteryStats.rangeKm = -1;
  }
}

static bool persistBatteryStats() {
  const uint32_t sequence = batteryHistorySnapshotValid ? persistedBatteryHistory.sequence + 1 : 1;
  const BatteryHistoryRecord record = currentBatteryHistoryRecord(sequence);
  if (!batteryPreferences.begin("batt", false)) return false;
  const bool saved = batteryPreferences.putBytes(kBatteryHistoryKey, &record, sizeof(record)) == sizeof(record);
  batteryPreferences.end();
  if (!saved) return false;
  persistedBatteryHistory = record;
  batteryHistorySnapshotValid = true;
  return true;
}

void loadBatteryStats() {
  BatteryLock lock;
  BatteryHistoryRecord record = {};
  if (batteryPreferences.begin("batt", true)) {
    if (batteryPreferences.getBytesLength(kBatteryHistoryKey) == sizeof(record))
      batteryPreferences.getBytes(kBatteryHistoryKey, &record, sizeof(record));
    batteryPreferences.end();
  }
  batteryHistorySnapshotValid = batteryHistoryRecordValid(record);
  if (batteryHistorySnapshotValid) {
    persistedBatteryHistory = record;
    batteryStats.lifetimeWh = record.lifetimeWh;
    batteryStats.lifetimeKm = record.lifetimeKm;
    lifetimeAhThroughput = record.lifetimeAh;
    batteryStats.packMilliOhm = record.packMilliOhm;
    batteryStats.learnedCapacityAh = record.learnedCapacityAh;
    batteryStats.learnedSamples = record.learnedSamples;
  } else {
    persistedBatteryHistory = {};
    batteryStats.lifetimeWh = 0.0F;
    batteryStats.lifetimeKm = 0.0F;
    lifetimeAhThroughput = 0.0F;
    batteryStats.packMilliOhm = 0.0F;
    batteryStats.learnedCapacityAh = 0.0F;
    batteryStats.learnedSamples = 0;
  }
  batteryStats.socPercent = -1;
  batteryStats.rangeKm = -1;
  refreshDerivedBatteryStats();
  batteryStatsLoaded = true;
  batteryPersistedAtMs = millis();
}

void resetBatteryStats() {
  BatteryLock lock;
  batteryStats = BatteryStats();
  batteryStats.socPercent = -1;
  batteryStats.rangeKm = -1;
  lifetimeAhThroughput = 0.0F;
  socSeedPercent = -1.0F;
  socAhSinceSeed = 0.0F;
  capacityStartSoc = -1.0F;
  capacityAhUsed = 0.0F;
  batteryPreferences.begin("batt", false);
  batteryPreferences.clear();
  batteryPreferences.end();
  persistedBatteryHistory = {};
  batteryHistorySnapshotValid = false;
  batteryPersistedAtMs = millis();
  batteryStatsLoaded = true;
}

int batterySocFromVoltage(float packVoltage) {
  if (batterySeriesCount == 0) return 0;
  return (int)constrain((int)lroundf(socFromCellVoltage(packVoltage / batterySeriesCount)), 0, 100);
}

void batteryStatsRebaseDistance(float odometerKm) {
  BatteryLock lock;
  // Only the baseline moves. bankCounterDelta reads a backwards step as a
  // controller reboot and banks the whole new value, so without this a source
  // switch mid-ride would add an entire odometer to the lifetime total — and
  // that total is persisted.
  if (!batteryCountersPrimed) return;
  lastOdometerKm = odometerKm;
}

void batteryStatsStartRide() {
  BatteryLock lock;
  batteryStats.tripWh = 0.0F;
  batteryStats.tripRegenWh = 0.0F;
  batteryStats.tripKm = 0.0F;
  batteryStats.tripWhPerKm = 0.0F;
  batteryCountersPrimed = false;  // the controller restarted its counters too
}

void batteryStatsCheckpoint() {
  BatteryLock lock;
  if (!batteryStatsLoaded || !batteryHistoryChanged(false)) return;
  if (persistBatteryStats()) batteryPersistedAtMs = millis();
}

const BatteryStats &batteryStatsLive() {
  return batteryStats;
}

void batteryStatsUpdate(const VescCounters &counters) {
  BatteryLock lock;
  if (!batteryStatsLoaded) loadBatteryStats();

  if (!batteryCountersPrimed) {
    lastAmpHours = counters.ampHours;
    lastAmpHoursCharged = counters.ampHoursCharged;
    lastWattHours = counters.wattHours;
    lastWattHoursCharged = counters.wattHoursCharged;
    lastOdometerKm = counters.odometerKm;
    lastPackVoltage = counters.voltage;
    lastPackCurrent = counters.current;
    batteryCountersPrimed = true;
    return;  // the first sample only establishes the baseline
  }

  const float ahOut = bankCounterDelta(counters.ampHours, lastAmpHours);
  const float ahIn = bankCounterDelta(counters.ampHoursCharged, lastAmpHoursCharged);
  const float whOut = bankCounterDelta(counters.wattHours, lastWattHours);
  const float whIn = bankCounterDelta(counters.wattHoursCharged, lastWattHoursCharged);
  const float km = bankCounterDelta(counters.odometerKm, lastOdometerKm);

  batteryStats.tripWh += whOut - whIn;
  batteryStats.tripRegenWh += whIn;
  batteryStats.tripKm += km;
  batteryStats.lifetimeWh += whOut - whIn;
  batteryStats.lifetimeKm += km;
  lifetimeAhThroughput += ahOut;

  learnPackResistance(counters.voltage, counters.current);
  updateStateOfCharge(counters.voltage, counters.current, ahOut - ahIn);
  refreshDerivedBatteryStats();

  // NVS is flash: write one CRC-protected snapshot at most every ten minutes,
  // and only after a meaningful change. Ride/controller boundaries checkpoint
  // even smaller changes through batteryStatsCheckpoint().
  const uint32_t now = millis();
  if (now - batteryPersistedAtMs >= kBatteryPersistIntervalMs) {
    batteryPersistedAtMs = now;
    if (batteryHistoryChanged(true)) persistBatteryStats();
  }
}

// saveAppSettings() is intentionally called at safe navigation boundaries as
// well as directly from controls. Keep that safety net, but do not turn every
// return to the dashboard into 51 NVS set/commit calls when nothing changed.
// This is a RAM-only mirror of the values last known to be persisted.
struct AppSettingsSnapshot {
  uint8_t configured;
  uint8_t language;
  uint8_t units;
  uint8_t mode;
  uint8_t accent;
  uint8_t gradientEnabled;
  uint8_t gradientHorizontal;
  uint8_t gradientReverse;
  uint8_t gradientBell;
  uint8_t gradientPosition;
  uint8_t gradientColor;
  uint16_t wheelDiameterMm;
  uint16_t speedCalibrationPercent;
  uint32_t vescUartBaud;
  uint8_t vescCanId;
  uint8_t motorPolePairs;
  uint16_t driveRatioHundredths;
  uint8_t pinEnabled;
  char pin[sizeof(securityPin)];
  char oem[sizeof(oemName)];
  char vehicle[sizeof(vehicleName)];
  char motor[sizeof(motorName)];
  char controller[sizeof(controllerName)];
  char build[sizeof(vehicleBuildId)];
  char modeLabels[3][13];
  uint8_t batterySeriesCount;
  uint16_t batteryCapacityDeciAh;
  uint16_t batteryMaxAmps;
  uint16_t motorMaxAmps;
  uint16_t continuousPowerDeciKw;
  uint16_t peakPowerDeciKw;
  uint32_t topSpeedKmh;
  uint8_t autoRanges;
  uint32_t learnedSpeed[3];
  uint8_t speedModeCount;
  uint32_t speedMode1Kmh;
  uint32_t speedMode2Kmh;
  uint32_t speedMode3Kmh;
  uint8_t speedSetupMode;
  uint8_t speedPresetMode;
  uint8_t speedPowerCurvePercent;
  uint8_t speedAccelCurvePercent;
  uint8_t displayBrightnessPercent;
  uint8_t autoBrightnessEnabled;
  uint16_t lightSensorDarkRaw;
  uint16_t lightSensorBrightRaw;
  uint8_t themeLedEnabled;
  uint8_t bluetoothEnabled;
  uint8_t developerOptionsEnabled;
  uint8_t demoModeEnabled;
  uint8_t demoSpeed;
  uint8_t autoReturnEnabled;
  uint16_t autoReturnTimeoutSeconds;
  uint8_t controllerBackendId;
  bool controllerEverConnected;
  DashboardCustomization profiles[MODE_COUNT];
};

static AppSettingsSnapshot persistedAppSettings = {};
static bool persistedAppSettingsKnown = false;

static AppSettingsSnapshot currentAppSettingsSnapshot() {
  AppSettingsSnapshot snapshot = {};
  snapshot.configured = firstBootConfigured;
  snapshot.language = static_cast<uint8_t>(language);
  snapshot.units = static_cast<uint8_t>(unitMode);
  snapshot.mode = static_cast<uint8_t>(dashboardMode);
  snapshot.accent = static_cast<uint8_t>(accentTheme);
  snapshot.gradientEnabled = dashboardGradientEnabled;
  snapshot.gradientHorizontal = dashboardGradientHorizontal;
  snapshot.gradientReverse = dashboardGradientReverse;
  snapshot.gradientBell = dashboardGradientBell;
  snapshot.gradientPosition = dashboardGradientPosition;
  snapshot.gradientColor = static_cast<uint8_t>(dashboardGradientTheme);
  snapshot.wheelDiameterMm = wheelDiameterMm;
  snapshot.speedCalibrationPercent = speedCalibrationPercent;
  snapshot.vescUartBaud = vescUartBaud;
  snapshot.vescCanId = vescCanId;
  snapshot.motorPolePairs = vescMotorPolePairs;
  snapshot.driveRatioHundredths = vescDriveRatioHundredths;
  snapshot.pinEnabled = pinEnabled;
  snprintf(snapshot.pin, sizeof(snapshot.pin), "%s", securityPin);
  snprintf(snapshot.oem, sizeof(snapshot.oem), "%s", oemName);
  snprintf(snapshot.vehicle, sizeof(snapshot.vehicle), "%s", vehicleName);
  snprintf(snapshot.motor, sizeof(snapshot.motor), "%s", motorName);
  snprintf(snapshot.controller, sizeof(snapshot.controller), "%s", controllerName);
  snprintf(snapshot.build, sizeof(snapshot.build), "%s", vehicleBuildId);
  memcpy(snapshot.modeLabels, rideModeLabels, sizeof(rideModeLabels));
  snapshot.batterySeriesCount = batterySeriesCount;
  snapshot.batteryCapacityDeciAh = batteryCapacityDeciAh;
  snapshot.batteryMaxAmps = batteryMaxAmps;
  snapshot.motorMaxAmps = motorMaxAmps;
  snapshot.continuousPowerDeciKw = continuousPowerDeciKw;
  snapshot.peakPowerDeciKw = peakPowerDeciKw;
  snapshot.topSpeedKmh = topSpeedKmh;
  snapshot.autoRanges = automaticGaugeRanges;
  memcpy(snapshot.learnedSpeed, learnedGaugeSpeed, sizeof(learnedGaugeSpeed));
  snapshot.speedModeCount = speedModeCount;
  snapshot.speedMode1Kmh = speedMode1Kmh;
  snapshot.speedMode2Kmh = speedMode2Kmh;
  snapshot.speedMode3Kmh = speedMode3Kmh;
  snapshot.speedSetupMode = static_cast<uint8_t>(speedSetupMode);
  snapshot.speedPresetMode = speedPresetMode;
  snapshot.speedPowerCurvePercent = speedPowerCurvePercent;
  snapshot.speedAccelCurvePercent = speedAccelCurvePercent;
  snapshot.displayBrightnessPercent = displayBrightnessPercent;
  snapshot.autoBrightnessEnabled = autoBrightnessEnabled;
  snapshot.lightSensorDarkRaw = lightSensorDarkRaw;
  snapshot.lightSensorBrightRaw = lightSensorBrightRaw;
  snapshot.themeLedEnabled = themeLedEnabled;
  snapshot.bluetoothEnabled = bluetoothEnabled;
  snapshot.developerOptionsEnabled = developerOptionsEnabled;
  snapshot.demoModeEnabled = dashboardDemoModeEnabled;
  snapshot.demoSpeed = demoTimeScale;
  snapshot.autoReturnEnabled = autoReturnEnabled;
  snapshot.autoReturnTimeoutSeconds = autoReturnTimeoutSeconds;
  snapshot.controllerBackendId = static_cast<uint8_t>(controllerBackendIdFor(controllerType, controllerConnection));
  snapshot.controllerEverConnected = controllerEverConnected;
  memcpy(snapshot.profiles, dashboardCustomizations, sizeof(snapshot.profiles));
  return snapshot;
}

void saveAppSettings() {
  captureDashboardCustomization(dashboardMode);
  const AppSettingsSnapshot current = currentAppSettingsSnapshot();
  if (persistedAppSettingsKnown && memcmp(&current, &persistedAppSettings, sizeof(current)) == 0) return;
  if (!preferences.begin("app", false)) return;
  const bool writeAll = !persistedAppSettingsKnown;
#define PUT_CHANGED(field, call) \
  do {                            \
    if (writeAll || current.field != persistedAppSettings.field) preferences.call; \
  } while (0)
  PUT_CHANGED(configured, putBool("configured", current.configured));
  PUT_CHANGED(language, putUChar("lang", current.language));
  PUT_CHANGED(units, putUChar("units", current.units));
  PUT_CHANGED(mode, putUChar("mode", current.mode));
  PUT_CHANGED(accent, putUChar("accent", current.accent));
  PUT_CHANGED(gradientEnabled, putBool("gradOn", current.gradientEnabled));
  PUT_CHANGED(gradientHorizontal, putBool("gradHor", current.gradientHorizontal));
  PUT_CHANGED(gradientReverse, putBool("gradRev", current.gradientReverse));
  PUT_CHANGED(gradientBell, putBool("gradBell", current.gradientBell));
  PUT_CHANGED(gradientPosition, putUChar("gradPos", current.gradientPosition));
  PUT_CHANGED(gradientColor, putUChar("gradColor", current.gradientColor));
  PUT_CHANGED(wheelDiameterMm, putUShort("wheel", current.wheelDiameterMm));
  PUT_CHANGED(speedCalibrationPercent, putUShort("spdCal", current.speedCalibrationPercent));
  PUT_CHANGED(vescUartBaud, putUInt("vescBaud", current.vescUartBaud));
  PUT_CHANGED(vescCanId, putUChar("vescCan", current.vescCanId));
  PUT_CHANGED(motorPolePairs, putUChar("polePairs", current.motorPolePairs));
  PUT_CHANGED(driveRatioHundredths, putUShort("drive100", current.driveRatioHundredths));
  PUT_CHANGED(pinEnabled, putBool("pinEnabled", current.pinEnabled));
  if (writeAll || strcmp(current.pin, persistedAppSettings.pin)) preferences.putString("pin", current.pin);
  if (writeAll || strcmp(current.oem, persistedAppSettings.oem)) preferences.putString("oem", current.oem);
  if (writeAll || strcmp(current.vehicle, persistedAppSettings.vehicle)) preferences.putString("veh", current.vehicle);
  if (writeAll || strcmp(current.motor, persistedAppSettings.motor)) preferences.putString("motor", current.motor);
  if (writeAll || strcmp(current.controller, persistedAppSettings.controller))
    preferences.putString("ctrl", current.controller);
  if (writeAll || strcmp(current.build, persistedAppSettings.build)) preferences.putString("build", current.build);
  for (int i = 0; i < 3; ++i) {
    char key[12];
    snprintf(key, sizeof(key), "modeLabel%d", i);
    if (writeAll || strcmp(current.modeLabels[i], persistedAppSettings.modeLabels[i]))
      preferences.putString(key, current.modeLabels[i]);
  }
  PUT_CHANGED(batterySeriesCount, putUChar("battS", current.batterySeriesCount));
  PUT_CHANGED(batteryCapacityDeciAh, putUShort("battAh10", current.batteryCapacityDeciAh));
  PUT_CHANGED(batteryMaxAmps, putUShort("battMaxA", current.batteryMaxAmps));
  PUT_CHANGED(motorMaxAmps, putUShort("motorMaxA", current.motorMaxAmps));
  PUT_CHANGED(continuousPowerDeciKw, putUShort("contKw10", current.continuousPowerDeciKw));
  PUT_CHANGED(peakPowerDeciKw, putUShort("peakKw10", current.peakPowerDeciKw));
  PUT_CHANGED(topSpeedKmh, putUInt("topKmh", current.topSpeedKmh));
  PUT_CHANGED(autoRanges, putBool("autoRanges", current.autoRanges));
  for (int i = 0; i < 3; ++i) {
    char key[12]; snprintf(key, sizeof(key), "rangeSp%d", i);
    if (writeAll || current.learnedSpeed[i] != persistedAppSettings.learnedSpeed[i])
      preferences.putUInt(key, current.learnedSpeed[i]);
  }
  PUT_CHANGED(speedModeCount, putUChar("spdModes", current.speedModeCount));
  PUT_CHANGED(speedMode1Kmh, putUInt("spdMode1", current.speedMode1Kmh));
  PUT_CHANGED(speedMode2Kmh, putUInt("spdMode2", current.speedMode2Kmh));
  PUT_CHANGED(speedMode3Kmh, putUInt("spdMode3", current.speedMode3Kmh));
  PUT_CHANGED(speedSetupMode, putUChar("spdSetup", current.speedSetupMode));
  PUT_CHANGED(speedPresetMode, putUChar("spdPreset", current.speedPresetMode));
  PUT_CHANGED(speedPowerCurvePercent, putUChar("spdPower", current.speedPowerCurvePercent));
  PUT_CHANGED(speedAccelCurvePercent, putUChar("spdAccel", current.speedAccelCurvePercent));
  PUT_CHANGED(displayBrightnessPercent, putUChar("bright", current.displayBrightnessPercent));
  PUT_CHANGED(autoBrightnessEnabled, putBool("autoBr", current.autoBrightnessEnabled));
  PUT_CHANGED(lightSensorDarkRaw, putUShort("ldrDark", current.lightSensorDarkRaw));
  PUT_CHANGED(lightSensorBrightRaw, putUShort("ldrBright", current.lightSensorBrightRaw));
  PUT_CHANGED(themeLedEnabled, putBool("ledOn", current.themeLedEnabled));
  PUT_CHANGED(bluetoothEnabled, putBool("btOn", current.bluetoothEnabled));
  PUT_CHANGED(developerOptionsEnabled, putBool("devOpts", current.developerOptionsEnabled));
  PUT_CHANGED(demoModeEnabled, putBool("demoMode", current.demoModeEnabled));
  PUT_CHANGED(demoSpeed, putUChar("demoSpeed", current.demoSpeed));
  PUT_CHANGED(autoReturnEnabled, putBool("autoReturn", current.autoReturnEnabled));
  PUT_CHANGED(autoReturnTimeoutSeconds, putUShort("autoRetSec", current.autoReturnTimeoutSeconds));
  PUT_CHANGED(controllerBackendId, putUChar("ctrlBackend", current.controllerBackendId));
  PUT_CHANGED(controllerEverConnected, putBool("ctrlSeen", current.controllerEverConnected));
#undef PUT_CHANGED
  if (writeAll || memcmp(current.profiles, persistedAppSettings.profiles, sizeof(current.profiles)) != 0) {
    preferences.putBytes("uiProfiles", current.profiles, sizeof(current.profiles));
    preferences.putUChar(kBarGraphProfileRevisionKey, kBarGraphProfileRevision);
  }
  preferences.end();
  persistedAppSettings = current;
  persistedAppSettingsKnown = true;
}

void saveDashboardCustomizationProfiles() {
  // Theme previewing can render a different profile than dashboardMode. Save
  // only the profile array here so Reset Theme cannot accidentally persist the
  // preview as the selected dashboard or capture its colors into another UI.
  if (!preferences.begin("app", false)) return;
  const bool saved = preferences.putBytes("uiProfiles", dashboardCustomizations, sizeof(dashboardCustomizations)) ==
                         sizeof(dashboardCustomizations) &&
                     preferences.putUChar(kBarGraphProfileRevisionKey, kBarGraphProfileRevision) == sizeof(uint8_t);
  preferences.end();
  if (saved && persistedAppSettingsKnown)
    memcpy(persistedAppSettings.profiles, dashboardCustomizations, sizeof(dashboardCustomizations));
}

void saveAutoBrightnessSetting() {
  if (!preferences.begin("app", false)) return;
  const bool saved = preferences.putBool("autoBr", autoBrightnessEnabled) == sizeof(bool);
  preferences.end();
  if (saved && persistedAppSettingsKnown) persistedAppSettings.autoBrightnessEnabled = autoBrightnessEnabled;
}

bool setLightSensorCalibration(int darkRaw, int brightRaw) {
  darkRaw = constrain(darkRaw, 0, 4095);
  brightRaw = constrain(brightRaw, 0, 4095);
  // A smaller range amplifies normal ADC noise enough to make the backlight
  // visibly hunt. Keep the previous calibration when the two captures do not
  // establish a useful span.
  if (abs(darkRaw - brightRaw) < 50) return false;
  lightSensorDarkRaw = (uint16_t)darkRaw;
  lightSensorBrightRaw = (uint16_t)brightRaw;
  if (!preferences.begin("app", false)) return false;
  const bool savedDark = preferences.putUShort("ldrDark", lightSensorDarkRaw) == sizeof(lightSensorDarkRaw);
  const bool savedBright = preferences.putUShort("ldrBright", lightSensorBrightRaw) == sizeof(lightSensorBrightRaw);
  preferences.end();
  if (savedDark && savedBright && persistedAppSettingsKnown) {
    persistedAppSettings.lightSensorDarkRaw = lightSensorDarkRaw;
    persistedAppSettings.lightSensorBrightRaw = lightSensorBrightRaw;
  }
  return savedDark && savedBright;
}

static void setDefaultAppSettings() {
  initializeDashboardCustomizations();
  dashboardMode = MODE_HUD;
  accentTheme = ACCENT_DEFAULT;
  dashboardGradientEnabled = false;
  dashboardGradientHorizontal = false;
  dashboardGradientReverse = false;
  dashboardGradientBell = false;
  dashboardGradientPosition = 50;
  dashboardGradientTheme = ACCENT_DEFAULT;
  language = LANG_EN;
  unitMode = UNITS_METRIC;
  firstBootConfigured = false;
  controllerEverConnected = false;
  strncpy(oemName, "KAJO DASH", sizeof(oemName));
  strncpy(vehicleName, "Vehicle", sizeof(vehicleName));
  strncpy(motorName, "Motor", sizeof(motorName));
  strncpy(controllerName, "VESC", sizeof(controllerName));
  vehicleBuildId[0] = '\0';
  memset(rideModeLabels, 0, sizeof(rideModeLabels));
  oemName[sizeof(oemName) - 1] = '\0';
  vehicleName[sizeof(vehicleName) - 1] = '\0';
  motorName[sizeof(motorName) - 1] = '\0';
  controllerName[sizeof(controllerName) - 1] = '\0';
  batterySeriesCount = 20;
  batteryCapacityDeciAh = 200;
  batteryMaxAmps = 100;
  motorMaxAmps = 150;
  continuousPowerDeciKw = 30;
  peakPowerDeciKw = 60;
  topSpeedKmh = 100;
  automaticGaugeRanges = true;
  for (auto &speed : learnedGaugeSpeed) speed = 30;
  clearGaugeRangeRuntime();
  speedModeCount = 3;
  speedMode1Kmh = 25;
  speedMode2Kmh = 50;
  speedMode3Kmh = topSpeedKmh;
  speedSetupMode = SPEED_SETUP_PRESET;
  speedPresetMode = 2;
  speedPowerCurvePercent = 100;
  speedAccelCurvePercent = 60;
  wheelDiameterMm = 700;
  speedCalibrationPercent = 100;
  vescUartBaud = VESC_BAUD;
  vescCanId = 0;
  vescMotorPolePairs = VESC_MOTOR_POLE_PAIRS;
  vescDriveRatioHundredths = (uint16_t)lroundf(VESC_DRIVE_RATIO * 100.0F);
  pinEnabled = false;
  strncpy(securityPin, "1234", sizeof(securityPin));
  securityPin[sizeof(securityPin) - 1] = '\0';
  displayBrightnessPercent = 100;
  autoBrightnessEnabled = false;
  lightSensorDarkRaw = CYD_LDR_DARK_RAW;
  lightSensorBrightRaw = CYD_LDR_BRIGHT_RAW;
  themeLedEnabled = true;
  bluetoothEnabled = true;
  developerOptionsEnabled = false;
  dashboardDemoModeEnabled = false;
  demoTimeScale = 1;
  restartDemoRide();
  autoReturnEnabled = true;
  autoReturnTimeoutSeconds = 30;
  controllerType = CONTROLLER_VESC;
  controllerConnection = CONTROLLER_CONNECTION_UART;
  configStep = 0;
  pinInput[0] = '\0';
  applyDashboardCustomization(dashboardMode);
}

void resetAppSettings(bool includeHistory) {
  // Clearing the entire namespace makes this future-proof: newly added
  // settings cannot accidentally survive just because their key was omitted
  // from a list. Nothing in it needs carrying across — the trip and odometer
  // the dashboards show are derived from the controller's own tachometer, so
  // no reset on this board can move them. ("odoKm"/"tripKm" keys used to be
  // saved and restored here; nothing ever read them back.)
  preferences.begin("app", false);
  preferences.clear();
  preferences.end();
  persistedAppSettingsKnown = false;

  // These subsystems intentionally own separate NVS namespaces. A display
  // reset must still cover them or the next setup can inherit a previous
  // controller pairing or logging mode even though the UI said "settings"
  // were reset.
  controllerResetAllSettings();
  rideLoggerResetSettings(includeHistory);

  setDefaultAppSettings();
  // A reset is also a fresh hardware setup. Re-probe from the controller IDs
  // already captured during boot and replace any previous manual override.
  autoDetectDisplayPanelProfile(true);
  saveAppSettings();
  applyDisplayBrightness();
  // Ride-log removal is handled by the logger above; the remaining full-reset
  // difference is the display's accumulated battery energy/wear history.
  if (includeHistory) resetBatteryStats();
}

void loadAppSettings() {
  initializeDashboardCustomizations();
  preferences.begin("app", true);
  firstBootConfigured = preferences.getBool("configured", false);
  // Firmware older than this key never stored it. A display that was already
  // set up then counts as having had a controller, so an update does not
  // suddenly ask for setup; a reset stores false explicitly.
  controllerEverConnected = preferences.getBool("ctrlSeen", firstBootConfigured);
  language = static_cast<Language>(preferences.getUChar("lang", static_cast<uint8_t>(language)));
  if (language >= LANG_COUNT) language = LANG_EN;
  unitMode = static_cast<UnitMode>(preferences.getUChar("units", static_cast<uint8_t>(unitMode)));
  if (unitMode >= UNITS_COUNT) unitMode = UNITS_METRIC;
  dashboardMode = static_cast<DashboardMode>(preferences.getUChar("mode", static_cast<uint8_t>(dashboardMode)));
  if (dashboardMode >= MODE_COUNT) dashboardMode = MODE_HUD;
  accentTheme = static_cast<AccentTheme>(preferences.getUChar("accent", static_cast<uint8_t>(accentTheme)));
  if (accentTheme >= ACCENT_COUNT) accentTheme = ACCENT_DEFAULT;
  dashboardGradientEnabled = preferences.getBool("gradOn", dashboardGradientEnabled);
  dashboardGradientHorizontal = preferences.getBool("gradHor", dashboardGradientHorizontal);
  dashboardGradientReverse = preferences.getBool("gradRev", dashboardGradientReverse);
  dashboardGradientBell = preferences.getBool("gradBell", dashboardGradientBell);
  dashboardGradientPosition = constrain(preferences.getUChar("gradPos", dashboardGradientPosition), 15, 85);
  dashboardGradientTheme =
      static_cast<AccentTheme>(preferences.getUChar("gradColor", static_cast<uint8_t>(dashboardGradientTheme)));
  if (dashboardGradientTheme >= ACCENT_COUNT) dashboardGradientTheme = ACCENT_DEFAULT;
  wheelDiameterMm = preferences.getUShort("wheel", wheelDiameterMm);
  speedCalibrationPercent = constrain((int)preferences.getUShort("spdCal", speedCalibrationPercent), 50, 200);
  vescUartBaud = constrain(preferences.getUInt("vescBaud", vescUartBaud), 9600UL, 921600UL);
  vescCanId = constrain(preferences.getUChar("vescCan", vescCanId), 0, 254);
  vescMotorPolePairs = constrain(preferences.getUChar("polePairs", vescMotorPolePairs), 1, 64);
  vescDriveRatioHundredths = constrain(preferences.getUShort("drive100", vescDriveRatioHundredths), 10, 10000);
  pinEnabled = preferences.getBool("pinEnabled", pinEnabled);
  String savedPin = preferences.getString("pin", securityPin);
  const char *savedPinText = savedPin.c_str();
  bool validPin = strlen(savedPinText) == 4;
  for (size_t i = 0; validPin && i < 4; i++) {
    validPin = savedPinText[i] >= '0' && savedPinText[i] <= '9';
  }
  if (validPin) {
    savedPin.toCharArray(securityPin, sizeof(securityPin));
  } else {
    pinEnabled = false;
    strncpy(securityPin, "1234", sizeof(securityPin));
    securityPin[sizeof(securityPin) - 1] = '\0';
  }
  for (int i = 0; i < 3; ++i) {
    char key[12];
    snprintf(key, sizeof(key), "modeLabel%d", i);
    preferences.getString(key, "").toCharArray(rideModeLabels[i], sizeof(rideModeLabels[i]));
  }
  String savedOem = preferences.getString("oem", oemName);
  savedOem.toCharArray(oemName, sizeof(oemName));
  preferences.getString("veh", vehicleName).toCharArray(vehicleName, sizeof(vehicleName));
  preferences.getString("motor", motorName).toCharArray(motorName, sizeof(motorName));
  preferences.getString("ctrl", controllerName).toCharArray(controllerName, sizeof(controllerName));
  preferences.getString("build", vehicleBuildId).toCharArray(vehicleBuildId, sizeof(vehicleBuildId));
  batterySeriesCount = constrain(preferences.getUChar("battS", batterySeriesCount), 4, 32);
  batteryCapacityDeciAh = constrain(preferences.getUShort("battAh10", batteryCapacityDeciAh), 10, 999);
  batteryMaxAmps = constrain(preferences.getUShort("battMaxA", batteryMaxAmps), 1, 500);
  motorMaxAmps = constrain(preferences.getUShort("motorMaxA", motorMaxAmps), 1, 500);
  continuousPowerDeciKw = constrain(preferences.getUShort("contKw10", continuousPowerDeciKw), 1, 500);
  peakPowerDeciKw = constrain(preferences.getUShort("peakKw10", peakPowerDeciKw), 1, 500);
  topSpeedKmh = constrain((int)preferences.getUInt("topKmh", topSpeedKmh), 0, 999999);
  automaticGaugeRanges = preferences.getBool("autoRanges", true);
  for (int i = 0; i < 3; ++i) {
    char key[12]; snprintf(key, sizeof(key), "rangeSp%d", i);
    learnedGaugeSpeed[i] = constrain(preferences.getUInt(key, 30), 30U, 1200U);
  }
  clearGaugeRangeRuntime();
  speedModeCount = constrain(preferences.getUChar("spdModes", speedModeCount), 1, 3);
  speedMode1Kmh = constrain((int)preferences.getUInt("spdMode1", speedMode1Kmh), 0, (int)topSpeedKmh);
  speedMode2Kmh = constrain((int)preferences.getUInt("spdMode2", speedMode2Kmh), 0, (int)topSpeedKmh);
  speedMode3Kmh = constrain((int)preferences.getUInt("spdMode3", speedMode3Kmh), 0, (int)topSpeedKmh);
  speedSetupMode = static_cast<SpeedSetupMode>(preferences.getUChar("spdSetup", static_cast<uint8_t>(speedSetupMode)));
  if (speedSetupMode > SPEED_SETUP_PRESET) speedSetupMode = SPEED_SETUP_PRESET;
  speedPresetMode = constrain(preferences.getUChar("spdPreset", speedPresetMode), 0, 4);
  speedPowerCurvePercent = constrain(preferences.getUChar("spdPower", speedPowerCurvePercent), 0, 100);
  speedAccelCurvePercent = constrain(preferences.getUChar("spdAccel", speedAccelCurvePercent), 0, 100);
  displayBrightnessPercent = preferences.getUChar("bright", displayBrightnessPercent);
  autoBrightnessEnabled = preferences.getBool("autoBr", autoBrightnessEnabled);
  lightSensorDarkRaw = constrain((int)preferences.getUShort("ldrDark", CYD_LDR_DARK_RAW), 0, 4095);
  lightSensorBrightRaw = constrain((int)preferences.getUShort("ldrBright", CYD_LDR_BRIGHT_RAW), 0, 4095);
  if (abs((int)lightSensorDarkRaw - (int)lightSensorBrightRaw) < 50) {
    lightSensorDarkRaw = CYD_LDR_DARK_RAW;
    lightSensorBrightRaw = CYD_LDR_BRIGHT_RAW;
  }
  themeLedEnabled = preferences.getBool("ledOn", themeLedEnabled);
  bluetoothEnabled = preferences.getBool("btOn", true);
  developerOptionsEnabled = preferences.getBool("devOpts", false);
  dashboardDemoModeEnabled = preferences.getBool("demoMode", false);
  demoTimeScale = preferences.getUChar("demoSpeed", 1);
  if (demoTimeScale != 1 && demoTimeScale != 5 && demoTimeScale != 15 && demoTimeScale != 30 && demoTimeScale != 60) demoTimeScale = 1;
  autoReturnEnabled = preferences.getBool("autoReturn", true);
  autoReturnTimeoutSeconds = constrain(preferences.getUShort("autoRetSec", 30), 30, 3600);
  ControllerBackendId backendId = static_cast<ControllerBackendId>(
      preferences.getUChar("ctrlBackend", static_cast<uint8_t>(CONTROLLER_ID_VESC_UART)));
  if (!controllerBackendById(backendId)) backendId = CONTROLLER_ID_VESC_UART;
  controllerSelectionForId(backendId, controllerType, controllerConnection);
  displayBrightnessPercent = constrain(displayBrightnessPercent, DISPLAY_BRIGHTNESS_MIN, DISPLAY_BRIGHTNESS_MAX);
  const uint8_t storedBarGraphProfileRevision = preferences.getUChar(kBarGraphProfileRevisionKey, 0);
  const size_t storedLength = preferences.getBytesLength("uiProfiles");
  const bool dashboardProfileBlobCurrent = storedLength == sizeof(dashboardCustomizations);
  if (dashboardProfileBlobCurrent) {
    preferences.getBytes("uiProfiles", dashboardCustomizations, sizeof(dashboardCustomizations));
  }
  preferences.end();
  const bool resetBarGraphProfile = storedBarGraphProfileRevision != kBarGraphProfileRevision;
  if (resetBarGraphProfile) {
    DashboardCustomization &custom = dashboardCustomizations[MODE_BARS];
    custom.accent = ACCENT_DEFAULT;
    custom.backgroundAccent = ACCENT_DEFAULT;
    custom.gradientPosition = 50;
    custom.flags = 0;
    for (uint8_t slot = 0; slot < DASH_DATA_SLOTS_MAX; slot++) {
      custom.data[slot] = static_cast<uint8_t>(kDashboardDataDefaults[MODE_BARS][slot]);
    }
  }
  if ((!dashboardProfileBlobCurrent || resetBarGraphProfile) && preferences.begin("app", false)) {
    preferences.putBytes("uiProfiles", dashboardCustomizations, sizeof(dashboardCustomizations));
    preferences.putUChar(kBarGraphProfileRevisionKey, kBarGraphProfileRevision);
    preferences.end();
  }
  for (uint8_t mode = 0; mode < MODE_COUNT; mode++) {
    DashboardCustomization &custom = dashboardCustomizations[mode];
    if (custom.accent >= ACCENT_COUNT) custom.accent = ACCENT_DEFAULT;
    if (custom.backgroundAccent >= ACCENT_COUNT) custom.backgroundAccent = ACCENT_DEFAULT;
    custom.gradientPosition = constrain(custom.gradientPosition, 15, 85);
    for (uint8_t slot = 0; slot < dashboardDataSlotCount(static_cast<DashboardMode>(mode)); slot++) {
      if (custom.data[slot] >= DATA_COUNT) {
        custom.data[slot] = static_cast<uint8_t>(dashboardDataDefault(static_cast<DashboardMode>(mode), slot));
      }
    }
  }
  applyDashboardCustomization(dashboardMode);
  persistedAppSettings = currentAppSettingsSnapshot();
  persistedAppSettingsKnown = true;
}

void saveVehicleProfile() {
  saveAppSettings();
}

void cycleUnits() {
  unitMode = static_cast<UnitMode>((static_cast<int>(unitMode) + 1) % UNITS_COUNT);
}

void cycleLanguage() {
  language = static_cast<Language>((static_cast<int>(language) + 1) % LANG_COUNT);
}

void cyclePinOption() {
  if (!pinEnabled) {
    pinEnabled = true;
    strncpy(securityPin, "1234", sizeof(securityPin));
  } else if (strcmp(securityPin, "1234") == 0) {
    strncpy(securityPin, "0000", sizeof(securityPin));
  } else {
    pinEnabled = false;
  }
  securityPin[sizeof(securityPin) - 1] = '\0';
}

float displaySpeed(float kmh) {
  switch (unitMode) {
    case UNITS_IMPERIAL:
      return kmh * 0.621371F;
    case UNITS_NAUTICAL:
      return kmh * 0.539957F;
    case UNITS_MACH:
      return kmh / 1234.8F;
    case UNITS_METRIC:
    default:
      return kmh;
  }
}

float displaySpeedToKmh(float speed) {
  switch (unitMode) {
    case UNITS_IMPERIAL:
      return speed / 0.621371F;
    case UNITS_NAUTICAL:
      return speed / 0.539957F;
    case UNITS_MACH:
      return speed * 1234.8F;
    case UNITS_METRIC:
    default:
      return speed;
  }
}

int speedScaleMaxKmh() {
  return automaticGaugeRanges ? automaticGaugeMaximum(RANGE_SPEED) : max(1, (int)topSpeedKmh);
}

float displayDistance(float km) {
  switch (unitMode) {
    case UNITS_IMPERIAL:
      return km * 0.621371F;
    case UNITS_NAUTICAL:
      return km * 0.539957F;
    case UNITS_MACH:
    case UNITS_METRIC:
    default:
      return km;
  }
}

const char *speedUnitLabel() {
  switch (unitMode) {
    case UNITS_IMPERIAL:
      return "MPH";
    case UNITS_NAUTICAL:
      return "KT";
    case UNITS_MACH:
      return "MACH";
    case UNITS_METRIC:
    default:
      return "KM/H";
  }
}

const char *distanceUnitLabel() {
  switch (unitMode) {
    case UNITS_IMPERIAL:
      return "MI";
    case UNITS_NAUTICAL:
      return "NM";
    case UNITS_MACH:
    case UNITS_METRIC:
    default:
      return "KM";
  }
}

void formatFloat(char *buffer, size_t size, float value, uint8_t decimals) {
  (void)size;
  dtostrf(value, 0, decimals, buffer);
}

void formatSpeedValue(char *buffer, size_t size, float kmh) {
  const float speed = displaySpeed(kmh);
  if (unitMode == UNITS_MACH) {
    formatFloat(buffer, size, speed, 3);
  } else {
    snprintf(buffer, size, "%d", (int)roundf(speed));
  }
}

void formatPowerValue(char *buffer, size_t size, int watts) {
  if (watts >= 10000) {
    snprintf(buffer, size, "%d.%d", watts / 1000, (watts % 1000) / 100);
  } else {
    snprintf(buffer, size, "%d", watts);
  }
}

const char *powerUnitLabel(int watts) {
  return watts >= 10000 ? "kW" : "W";
}

void formatPowerWithUnit(char *buffer, size_t size, int watts) {
  char value[12];
  formatPowerValue(value, sizeof(value), watts);
  snprintf(buffer, size, "%s %s", value, powerUnitLabel(watts));
}

void formatPowerTight(char *buffer, size_t size, int watts) {
  char value[12];
  formatPowerValue(value, sizeof(value), watts);
  snprintf(buffer, size, "%s%s", value, powerUnitLabel(watts));
}

static const unsigned long DEMO_UPTIME_FLAG = 0x80000000UL;

static bool decodeDemoUptime(unsigned long encoded, unsigned long &hours, unsigned long &minutes,
                             unsigned long &seconds) {
  if ((encoded & DEMO_UPTIME_FLAG) == 0) return false;
  const unsigned long packed = encoded & ~DEMO_UPTIME_FLAG;
  hours = (packed / 10000UL) % 100UL;
  minutes = (packed / 100UL) % 100UL;
  seconds = packed % 100UL;
  return true;
}

const char *metricAvgLabel() {
  // "MITT." over the fuller "MITTEL": the HUD pairs this with the unit in a
  // 64 px cell, where the longer form leaves no gap to the next column
  return txt("AVG", "KA.", "MITT.", "MOY.", "PROM.", "MEDIA");
}

const char *metricRangeLabel() {
  return txt("RANGE", "KANTAMA", "REICHW.", "AUTONOMIE", "AUTONOMÍA", "AUTONOMIA");
}

// Unit symbol, not prose: it tracks the selected distance unit (WH/KM, WH/MI).
const char *energyRateLabel() {
  static char label[12];
  snprintf(label, sizeof(label), "WH/%s", distanceUnitLabel());
  return label;
}

void formatRangeText(char *buffer, size_t size, int rangeKm, bool withUnit) {
  if (rangeKm < 0) {
    snprintf(buffer, size, "-");
    return;
  }
  const int shown = (int)lroundf(displayDistance((float)rangeKm));
  if (withUnit) {
    snprintf(buffer, size, "%d %s", shown, distanceUnitLabel());
  } else {
    snprintf(buffer, size, "%d", shown);
  }
}

// Energy per displayed distance unit ("12.3 Wh/km", "19.8 Wh/mi"). Averages
// that have not been measured yet print as "-" rather than a misleading 0.0.
void formatEnergyRate(char *buffer, size_t size, float whPerKm, bool withUnit) {
  if (whPerKm <= 0.0F) {
    snprintf(buffer, size, "-");
    return;
  }
  const float unitsPerKm = displayDistance(1.0F);
  const float perUnit = unitsPerKm > 0.0F ? whPerKm / unitsPerKm : whPerKm;
  if (withUnit) {
    snprintf(buffer, size, "%.1f Wh/%s", perUnit, distanceUnitLabel());
  } else {
    snprintf(buffer, size, "%.1f", perUnit);
  }
}

void formatUptime(char *buffer, size_t size, unsigned long seconds) {
  unsigned long hours, minutes, secs;
  if (!decodeDemoUptime(seconds, hours, minutes, secs)) {
    hours = seconds / 3600UL;
    minutes = (seconds / 60UL) % 60UL;
    secs = seconds % 60UL;
  }
  snprintf(buffer, size, "%02lu:%02lu:%02lu", hours, minutes, secs);
}

void formatUptimeShort(char *buffer, size_t size, unsigned long seconds) {
  unsigned long hours, minutes, secs;
  if (!decodeDemoUptime(seconds, hours, minutes, secs)) {
    hours = seconds / 3600UL;
    minutes = (seconds / 60UL) % 60UL;
  }
  snprintf(buffer, size, "%02lu:%02lu", hours, minutes);
}

static float clamp01(float value) {
  return constrain(value, 0.0F, 1.0F);
}

static float smoothStep(float value) {
  const float x = clamp01(value);
  return x * x * (3.0F - 2.0F * x);
}

static float lerpFloat(float from, float to, float amount) {
  return from + (to - from) * amount;
}

// ── Demo ride ────────────────────────────────────────────────────────────────
// Demo telemetry is generated from one shared simulated clock.
// While a screen animation runs, live updates stand down so the frame budget
// goes to the animation instead of being split with a repaint. Deadline based
// rather than a flag cleared by a callback: if an animation is interrupted the
// updates resume by themselves instead of staying frozen.
static uint32_t uiUpdatesHeldUntilMs = 0;

void holdUiUpdates(uint32_t ms) {
  uiUpdatesHeldUntilMs = millis() + ms;
}

bool uiUpdatesHeld() {
  return (int32_t)(uiUpdatesHeldUntilMs - millis()) > 0;  // signed: survives wrap
}

// One interpolated ride profile: 500 W nominal / 1500 W peak, about 25 km/h.
// Repeating road conditions do not reset the trip or battery.
uint8_t demoTimeScale = 1;
static bool demoPreviewActive = false;
static const float kDemoPackWh = 13.0F * 3.0F * 3.5F * 3.7F;
static const float kDemoTimes[]  = {0, 5, 15, 45, 60, 80, 95, 105, 115, 120};
static const float kDemoSpeeds[] = {0, 0, 33, 33, 28, 34, 32, 26, 0, 0};
static const float kDemoPowers[] = {0, 0, 1500, 400, 900, 420, 200, 0, -200, 0};
static const int kDemoPointCount = sizeof(kDemoTimes) / sizeof(kDemoTimes[0]);
struct DemoSession { double seconds = 0; uint32_t lastMs = 0; bool running = false; };
static DemoSession demoSession, previewSession;

static float demoIntegral(const float *values, float seconds, bool regen = false) {
  float area = 0;
  for (int i = 0; i < kDemoPointCount - 1 && seconds > kDemoTimes[i]; ++i) {
    const float dt = min(seconds, kDemoTimes[i + 1]) - kDemoTimes[i];
    const float a = values[i];
    const float b = lerpFloat(a, values[i + 1], dt / (kDemoTimes[i + 1] - kDemoTimes[i]));
    if (!regen) area += (a + b) * 0.5F * dt;
    else if (a <= 0 && b <= 0) area -= (a + b) * 0.5F * dt;
    else if (a < 0) area += a * a / (b - a) * 0.5F * dt;
    else if (b < 0) area += b * b / (a - b) * 0.5F * dt;
  }
  return area / 3600.0F;
}
static float demoTotal(const float *values, float seconds, bool regen = false) {
  return floorf(seconds / 120.0F) * demoIntegral(values, 120, regen) +
         demoIntegral(values, fmodf(seconds, 120.0F), regen);
}
static float demoEndSeconds() {
  static const float end = []() {
    float lo = 0, hi = kDemoPackWh / demoIntegral(kDemoPowers, 120) * 120 + 120;
    for (int i = 0; i < 32; ++i) {
      const float mid = (lo + hi) * 0.5F;
      if (demoTotal(kDemoPowers, mid) < kDemoPackWh) lo = mid; else hi = mid;
    }
    return hi;
  }();
  return end;
}
DemoRide demoRideAt(float seconds) {
  DemoRide ride = {};
  seconds = constrain(seconds, 0.0F, demoEndSeconds());
  ride.seconds = seconds;
  ride.km = demoTotal(kDemoSpeeds, seconds);
  ride.wh = constrain(demoTotal(kDemoPowers, seconds), 0.0F, kDemoPackWh);
  if (seconds >= demoEndSeconds()) return ride;
  const float phase = fmodf(seconds, 120.0F);
  for (int i = 0; i < kDemoPointCount - 1; ++i) {
    if (phase > kDemoTimes[i + 1]) continue;
    const float t = (phase - kDemoTimes[i]) / (kDemoTimes[i + 1] - kDemoTimes[i]);
    ride.speedKmh = lerpFloat(kDemoSpeeds[i], kDemoSpeeds[i + 1], t);
    ride.watts = lerpFloat(kDemoPowers[i], kDemoPowers[i + 1], t);
    break;
  }
  return ride;
}
DemoRide demoRidePeak() { DemoRide r = {}; r.speedKmh = 34; r.watts = 1500; return r; }

void serviceDemoMode() {
  const uint32_t now = millis();
  DemoSession *sessions[] = {&demoSession, &previewSession};
  const bool active[] = {dashboardDemoModeEnabled, demoPreviewActive};
  for (int i = 0; i < 2; ++i) {
    DemoSession &s = *sessions[i];
    if (active[i] && s.running)
      s.seconds = min((double)demoEndSeconds(), s.seconds + (uint32_t)(now - s.lastMs) * 0.001 * demoTimeScale);
    s.lastMs = now;
    s.running = active[i];
  }
}
void restartDemoRide() { demoSession.seconds = 0; demoSession.lastMs = millis(); }
void setDemoMode(bool active) {
  serviceDemoMode();
  if (active && !dashboardDemoModeEnabled) restartDemoRide();
  dashboardDemoModeEnabled = active;
  demoSession.running = active;
}
void setDemoPreview(bool active) {
  serviceDemoMode();
  demoPreviewActive = active;
  previewSession.running = active;
}
void cycleDemoTimeScale() {
  serviceDemoMode();
  const uint8_t rates[] = {1, 5, 15, 30, 60};
  for (unsigned i = 0; i < sizeof(rates); ++i) {
    if (demoTimeScale == rates[i]) { demoTimeScale = rates[(i + 1) % sizeof(rates)]; return; }
  }
  demoTimeScale = 1;
}
bool demoModeIsActive() { return dashboardDemoModeEnabled || demoPreviewActive; }
float demoRideSeconds() { return demoPreviewActive ? previewSession.seconds : demoSession.seconds; }
struct DemoOutput { DashboardValues values; BatteryStats battery; float seconds = -1; };
static const DemoOutput &demoOutput(bool dashboardOnly) {
  static DemoOutput cache[2];
  DemoOutput &output = cache[!dashboardOnly && demoPreviewActive ? 1 : 0];
  const float seconds = dashboardOnly ? demoSession.seconds : demoRideSeconds();
  if (output.seconds == seconds) return output;
  const DemoRide r = demoRideAt(seconds);
  const float soc = constrain(1.0F - r.wh / kDemoPackWh, 0.0F, 1.0F);
  // Approximate NMC resting curve with load sag; demo-only, not a pack estimator.
  const float cellV = soc < 0.1F ? 3.0F + soc * 5.0F :
                      soc < 0.9F ? 3.5F + (soc - 0.1F) * 0.5F : 3.9F + (soc - 0.9F) * 3.0F;
  DashboardValues v = {};
  v.speedKmh = (int)lroundf(r.speedKmh); v.watts = (int)lroundf(r.watts);
  v.voltage = constrain(13 * cellV - r.watts / 1500 * 1.8F, 39.0F, 54.6F);
  v.current = r.watts / v.voltage;
  v.motorCurrent = v.current * (3.0F - 2.0F * min(1.0F, r.speedKmh / 34));
  const float heat = 1 - expf(-r.seconds / 900);
  v.motorTemp = (int)lroundf(25 + heat * 35); v.escTemp = (int)lroundf(25 + heat * 20);
  v.tripKm = r.km; v.odoKm = (int)lroundf(1284 + r.km);
  v.avgSpeedKmh = r.seconds > 0 ? r.km * 3600 / r.seconds : 0;
  v.uptimeSeconds = (unsigned long)r.seconds; v.batteryPercent = (int)lroundf(soc * 100);
  output.values = v;
  BatteryStats s = {};
  s.tripWh = r.wh; s.tripRegenWh = demoTotal(kDemoPowers, r.seconds, true); s.tripKm = r.km;
  s.tripWhPerKm = r.km > 0.05F ? r.wh / r.km : 0;
  s.lifetimeKm = 1284; s.lifetimeWhPerKm = 20; s.lifetimeWh = 1284 * 20;
  s.socPercent = (int)lroundf(100 * (1 - r.wh / kDemoPackWh));
  s.rangeKm = (int)lroundf((kDemoPackWh - r.wh) / (s.tripWhPerKm > 0 ? s.tripWhPerKm : 20));
  s.equivalentCycles = 41; s.packMilliOhm = 62; s.learnedCapacityAh = 10.5F; s.learnedSamples = 4;
  output.battery = s;
  output.seconds = seconds;
  return output;
}
DashboardValues makeDummyValues(bool dashboardOnly) { return demoOutput(dashboardOnly).values; }
BatteryStats makeDemoBatteryStats(bool dashboardOnly) { return demoOutput(dashboardOnly).battery; }
const char *dashOemName() { return demoModeIsActive() ? "DEMO MODE" : oemName; }


// Separate state for each transport, dashboard demo and theme preview.
static GaugeRangeTracker gaugeRanges[5][RANGE_COUNT];
void clearGaugeRangeRuntime() { for (auto &source : gaugeRanges) for (auto &range : source) range = GaugeRangeTracker{}; }
static int gaugeBackendIndex() { return controllerType == CONTROLLER_FARDRIVER ? 2 : controllerConnection == CONTROLLER_CONNECTION_BLE ? 1 : 0; }
static int gaugeSource() { return demoPreviewActive ? 4 : dashboardDemoModeEnabled ? 3 : gaugeBackendIndex(); }
int automaticGaugeSource() { return gaugeSource(); }
static void initGaugeRanges(int source) {
  const int defaults[] = {30, 1000, 25, 100, 500, 40};
  for (int i=0; i<RANGE_COUNT; ++i)
    if (!gaugeRanges[source][i].ceiling) gaugeRanges[source][i].ceiling = defaults[i];
  if (source < 3) gaugeRanges[source][RANGE_SPEED].ceiling = max(gaugeRanges[source][RANGE_SPEED].ceiling, (int)learnedGaugeSpeed[source]);
}
int automaticGaugeMaximum(GaugeRangeKind kind) { const int source=gaugeSource(); initGaugeRanges(source); return gaugeRanges[source][kind].ceiling; }
void resetAutomaticGaugeRanges() {
  const int source = gaugeSource();
  for (auto &range : gaugeRanges[source]) range = GaugeRangeTracker{};
  if (source < 3) learnedGaugeSpeed[source] = 30;
}
static void observeGaugeRanges(int source, const DashboardValues &v, uint32_t fields, uint32_t now) {
  if (!automaticGaugeRanges) return;
  initGaugeRanges(source);
  const float readings[] = {(float)v.speedKmh, (float)max(0,v.watts), max(0.0F,v.current),
                           max(0.0F,v.motorCurrent), max(0.0F,-(float)v.watts),
                           v.speedKmh >= 8 && v.watts > 0 ? (float)v.watts/v.speedKmh : 0};
  const uint32_t masks[] = {TELEMETRY_FIELD_SPEED, TELEMETRY_FIELD_POWER, TELEMETRY_FIELD_CURRENT,
    TELEMETRY_FIELD_MOTOR_CURRENT, TELEMETRY_FIELD_POWER, TELEMETRY_FIELD_SPEED | TELEMETRY_FIELD_POWER};
  const float limits[] = {1000, 100000, 2000, 4000, 100000, 1000};
  for (int i=0; i<RANGE_COUNT; ++i)
    gaugeRanges[source][i].observe((fields & masks[i]) == masks[i] ? readings[i] : -1, now,
                                  i == RANGE_SPEED ? 1000 : i == RANGE_EFFICIENCY ? 5000 : 300, limits[i]);
  if (source < 3) learnedGaugeSpeed[source] = gaugeRanges[source][RANGE_SPEED].ceiling;
}
void serviceGaugeRanges() {
  const ControllerSnapshot snapshot = controllerSnapshot();
  const int source = dashboardDemoModeEnabled ? 3 : gaugeBackendIndex();
  observeGaugeRanges(source, snapshot.values, snapshot.link == LINK_LIVE ? snapshot.available : 0, snapshot.sampledAtMs);
  // Persist learned speed after a sustained stop, not on every telemetry frame.
  static uint32_t stoppedAt = 0, savedAt = 0;
  const uint32_t now = millis();
  if (source < 3 && snapshot.link == LINK_LIVE && telemetryHas(snapshot.available, TELEMETRY_FIELD_SPEED) && snapshot.values.speedKmh < 2) {
    if (!stoppedAt) stoppedAt = now;
    if (now - stoppedAt >= 10000 && now - savedAt >= 60000) { saveAppSettings(); savedAt = now; }
  } else stoppedAt = 0;
}
void observePreviewGaugeRanges(const DashboardValues &values, uint32_t fields) {
  if (demoPreviewActive) observeGaugeRanges(4, values, fields, millis());
}

char rideModeLabels[3][13] = {};
const char *rideModeName(uint8_t mode) {
  if (mode < 1 || mode > 3) return "-";
  if (rideModeLabels[mode - 1][0]) return rideModeLabels[mode - 1];
  switch (mode) {
    case 1: return txt("LOW", "MAT", "NIE", "BAS", "BAJ", "BAS");
    case 2: return txt("MID", "KES", "MIT", "MOY", "MED", "MED");
    default: return txt("HI", "KOR", "HOCH", "HAUT", "ALT", "ALT");
  }
}
