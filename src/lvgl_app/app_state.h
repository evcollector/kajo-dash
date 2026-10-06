#pragma once
#include <Arduino.h>
#include <lvgl.h>

// Shared state and logic for the LVGL firmware. These enums, settings and
// formatting helpers began as a mirror of the pre-LVGL app, which no longer
// exists in the tree; nothing is kept in parity with it.

enum ScreenMode {
  SCREEN_DASHBOARD,
  SCREEN_MENU,
  SCREEN_SUBMENU,
  SCREEN_TEXT_INPUT,
  SCREEN_CONFIG,
  SCREEN_PIN_SETUP,
  SCREEN_PIN_LOCK,
  SCREEN_TOUCH_TEST,
  SCREEN_FARDRIVER_BLE,
  SCREEN_RIDE_LOGS,
  SCREEN_FIRMWARE_UPDATE,
  SCREEN_COMPANION_MODE,
  SCREEN_RIDE_REPLAY
};

enum DashboardMode {
  MODE_HUD,
  MODE_GAUGE,
  MODE_SIMPLE,
  MODE_BARS,
  MODE_MOTOR_DATA,
  MODE_PIXEL_GAUGE,
  MODE_LARGE_TILES,
  MODE_BIG_READOUT,
  MODE_REDLINE,
  MODE_TRACE,
  MODE_MINIMAL,
  MODE_EFFICIENCY,
  MODE_COUNT
};

enum Language {
  LANG_EN,
  LANG_FI,
  LANG_DE,
  LANG_FR,
  LANG_ES,
  LANG_IT,
  LANG_COUNT
};

enum AccentTheme {
  ACCENT_DEFAULT,
  ACCENT_ORANGE,
  ACCENT_BLUE,
  ACCENT_GREEN,
  ACCENT_PURPLE,
  ACCENT_RED,
  ACCENT_CYAN,
  ACCENT_YELLOW,
  ACCENT_WHITE,
  ACCENT_MAGENTA,
  ACCENT_COUNT
};

// Secondary dashboard readouts that can be assigned to the configurable data
// slots of each UI. Primary visualisations (speed dials, power beams, battery
// bars) deliberately keep their meaning so their scales and icons never lie.
enum DashboardDataItem : uint8_t {
  DATA_TRIP,
  DATA_ODOMETER,
  DATA_AVG_SPEED,
  DATA_RANGE,
  DATA_RIDE_EFFICIENCY,
  DATA_LIFETIME_EFFICIENCY,
  DATA_RIDE_ENERGY,
  DATA_REGEN_ENERGY,
  DATA_VOLTAGE,
  DATA_CURRENT,
  DATA_MOTOR_TEMP,
  DATA_ESC_TEMP,
  DATA_BATTERY,
  DATA_UPTIME,
  DATA_POWER,
  DATA_LEARNED_CAPACITY,
  DATA_PACK_RESISTANCE,
  // Appended rather than slotted in beside their relatives: these are persisted
  // by ordinal, so an existing profile would otherwise read back shifted.
  DATA_SPEED,
  DATA_MOTOR_CURRENT,
  DATA_RIDE_MODE,
  DATA_DUTY,
  DATA_COUNT
};

// Bar Graph needs eleven: five meter rows plus its six side readouts.
constexpr uint8_t DASH_DATA_SLOTS_MAX = 12;

struct DashboardCustomization {
  uint8_t accent;
  uint8_t backgroundAccent;
  uint8_t gradientPosition;
  // bits 0..3 gradient settings; bits 4..5 appearance, stored as mode + 1
  // (0 means none captured yet, so the dashboard's native look applies).
  uint8_t flags;
  uint8_t data[DASH_DATA_SLOTS_MAX];
};

enum DashboardAppearanceMode : uint8_t {
  DASH_APPEARANCE_DARK,
  DASH_APPEARANCE_LIGHT,
  DASH_APPEARANCE_AUTO,
};

// CYD batches use electrically compatible 240x320 panels with different
// inversion/gamma requirements. The detected/overridden hardware profile is
// stored separately from ordinary app settings.
enum DisplayPanelProfile : uint8_t {
  DISPLAY_PANEL_STANDARD,
  DISPLAY_PANEL_ALTERNATE,
  DISPLAY_PANEL_COUNT,
};

// Selects the one controller transport that owns live telemetry.  This is
// deliberately separate from controllerName, which is editable vehicle
// metadata shown in a few information views.
enum ControllerType : uint8_t {
  CONTROLLER_VESC,
  CONTROLLER_FARDRIVER,
  CONTROLLER_TYPE_COUNT,
};

enum ControllerConnection : uint8_t {
  CONTROLLER_CONNECTION_UART,
  CONTROLLER_CONNECTION_BLE,
  CONTROLLER_CONNECTION_COUNT,
};

enum UnitMode {
  UNITS_METRIC,
  UNITS_IMPERIAL,
  UNITS_NAUTICAL,
  UNITS_MACH,
  UNITS_COUNT
};

enum SpeedSetupMode {
  SPEED_SETUP_CUSTOM,
  SPEED_SETUP_PRESET
};

enum SubmenuType {
  SUBMENU_DASH_UI,
  SUBMENU_LANGUAGE,
  SUBMENU_UNITS,
  SUBMENU_VESC,
  SUBMENU_CONTROLLER_CONFIG,
  SUBMENU_SPEED,
  SUBMENU_CONNECTION,
  SUBMENU_POWER_LIMITS,
  SUBMENU_SPEED_CALIBRATION,
  SUBMENU_GAUGE_RANGES,
  SUBMENU_PIN,
  SUBMENU_LOGGING,
  SUBMENU_DISPLAY,
  SUBMENU_DISPLAY_INFO,
  SUBMENU_PANEL_COLORS,
  SUBMENU_CHANGELOG,
  SUBMENU_CONFIGURATOR,
  SUBMENU_RESET,
  SUBMENU_BATTERY,
  SUBMENU_BATTERY_CELLS,
  SUBMENU_CONTROLLER_TYPE,
  SUBMENU_AUTO_RETURN,
  SUBMENU_DEMO,
  SUBMENU_GENERIC
};

struct DashboardValues {
  int speedKmh;
  int watts;
  float voltage;
  float current;       // pack side: what the battery delivers
  float motorCurrent;  // phase side: what the motor draws, often several times larger
  int motorTemp;
  int escTemp;
  float tripKm;
  int odoKm;
  float avgSpeedKmh;
  unsigned long uptimeSeconds;
  int batteryPercent;
  float dutyCycle;       // signed VESC duty fraction; availability is separate
  float phaseVoltage;    // estimated fundamental phase-neutral peak volts
};

// Energy, range and wear, derived from the controller's own Ah/Wh/tachometer
// counters. Fields that cannot be known yet are negative (socPercent, rangeKm)
// or zero (the averages), never a guessed placeholder.
struct BatteryStats {
  float tripWh;             // net energy this ride: consumed minus regenerated
  float tripRegenWh;
  float tripKm;
  float tripWhPerKm;        // 0 until the ride has covered enough ground
  float lifetimeWh;
  float lifetimeKm;
  float lifetimeWhPerKm;    // 0 until there is enough history
  int socPercent;           // -1 while it cannot be estimated
  int rangeKm;              // -1 while consumption is unknown
  float equivalentCycles;   // Ah throughput / pack capacity
  float packMilliOhm;       // 0 until learned from load transients
  float learnedCapacityAh;  // 0 until a deep enough discharge has been seen
  uint8_t learnedSamples;   // how many deep cycles fed learnedCapacityAh
};

// Raw controller counters. These restart at zero whenever the VESC reboots, so
// only their deltas carry meaning; batteryStatsUpdate banks those into totals.
struct VescCounters {
  float ampHours;
  float ampHoursCharged;
  float wattHours;
  float wattHoursCharged;
  float odometerKm;  // absolute, converted from the controller's tachometer
  float voltage;
  float current;
};

// Stored as the "logger"/"mode" preference. ON keeps the value the old
// automatic mode used; a stored 2 (the retired manual mode) also loads as ON.
enum RideLoggingMode : uint8_t {
  RIDE_LOG_OFF = 0,
  RIDE_LOG_ON = 1,
};

struct RideLoggingStatus {
  RideLoggingMode mode;
  bool cardReady;
  bool cardChecking;
  bool recording;
  uint8_t sampleHz;
  uint32_t rideId;
  uint32_t rideCount;
  uint32_t droppedRecords;
  uint32_t ioErrors;
  uint32_t revision;
  uint64_t usedBytes;
  uint64_t totalBytes;
};

constexpr uint8_t RIDE_LOG_CATALOG_MAX = 24;

struct RideLogSummary {
  uint32_t rideId;
  uint32_t durationSeconds;
  uint32_t distanceMeters;
  int32_t netWhDeci;
  uint32_t regenWhDeci;
  uint32_t fileBytes;
  uint8_t sampleHz;
  bool valid;
  bool demo;  // recorded from demo mode rather than a controller
};

struct RideLogCatalogStatus {
  bool loading;
  uint8_t count;
  uint32_t revision;
};

void rideLoggerBegin();
void rideLoggerSample(const DashboardValues &values, const BatteryStats &stats, uint8_t faultCode,
                      uint32_t availableFields);
// Called while the controller link is down. The open ride is treated as
// stopped, so it pauses and closes on the usual timers instead of staying open.
void rideLoggerTelemetryLost();
RideLoggingStatus rideLoggerStatus();
void rideLoggerToggleEnabled();
void rideLoggerCycleRate();
void rideLoggerSetSuspended(bool suspended);
void rideLoggerSetStorageNeeded(bool needed);
// Restore the logging controls to their factory defaults. A full reset also
// schedules a durable SD-card wipe that survives a missing card or reboot.
void rideLoggerResetSettings(bool deleteRideLogs);
void rideLoggerDeleteAll();
// Queues deletion of one finished ride; the ride being recorded is refused.
bool rideLoggerDeleteRide(uint32_t rideId);
bool rideLoggerWipeCard();
// Progress of the last rideLoggerWipeCard(). The writer counts what is on the
// card first, so `total` is only meaningful from Removing on.
struct RideLogWipeStatus {
  enum State : uint8_t { Idle, Counting, Removing, Done, Failed } state;
  uint32_t removed;
  uint32_t total;
};
RideLogWipeStatus rideLoggerWipeStatus();
void rideLoggerRequestCatalog();
RideLogCatalogStatus rideLoggerCatalogStatus();
bool rideLoggerCatalogEntry(uint8_t index, RideLogSummary &entry);

enum RideLogSeriesField : uint8_t {
  RIDE_SERIES_SPEED = 1,
  RIDE_SERIES_POWER = 2,
};

struct RideLogSeriesPoint {
  uint16_t elapsedSeconds;
  int32_t value;
};

// Serialized through the SD writer task. These APIs intentionally expose only
// completed rides so browsing cannot race the file currently being recorded.
bool rideLoggerReadSeries(uint32_t rideId, RideLogSeriesField field, uint32_t startSeconds,
                          uint32_t endSeconds, uint8_t maximumPoints, RideLogSeriesPoint *points,
                          uint8_t &pointCount);
bool rideLoggerReadFileChunk(uint32_t rideId, uint32_t offset, uint8_t *buffer, size_t capacity,
                             size_t &bytesRead, uint32_t &fileBytes);
// The writer keeps the ride it is reading open between chunks. This lets it
// drop that file now; an idle timeout covers a reader that never says so.
void rideLoggerReleaseRead();

// Called from the telemetry task only.
void batteryStatsUpdate(const VescCounters &counters);
void batteryStatsStartRide();  // VESC (re)connected: trip counters restart
// Persist changed lifetime battery history at a ride/controller boundary.
// Periodic checkpoints are handled internally by batteryStatsUpdate().
void batteryStatsCheckpoint();
// The odometer this side banks deltas from switched to a different conversion
// (controller-reported distance vs. eRPM/tachometer). The reading is not a
// distance travelled, so it must replace the baseline instead of being banked.
void batteryStatsRebaseDistance(float odometerKm);
// Cell chemistry decides which open-circuit curve turns resting voltage into a
// charge percentage, the nominal cell voltage behind the Wh figures, and the
// full-cell voltage the pack voltage bar is scaled to.
enum BatteryChemistry : uint8_t {
  BATTERY_LIION,
  BATTERY_LIPO,
  BATTERY_LIFEPO4,
  BATTERY_CHEMISTRY_COUNT
};
// Each chemistry has default cell voltages the rider can then adjust. The
// minimum is the empty cell and the maximum the full-charge voltage, as the
// industry quotes them (3.65 V for LiFePO4). The charge percentage reads a full
// pack as 100 % at the lower voltage it rests at, scaled with the maximum; the
// nominal voltage turns amp-hours into watt-hours.
enum BatteryCellVoltage : uint8_t { BATTERY_CELL_MIN, BATTERY_CELL_NOMINAL, BATTERY_CELL_MAX };
const char *batteryChemistryName(uint8_t chemistry);
uint16_t batteryChemistryDefaultMv(uint8_t chemistry, BatteryCellVoltage which);
float batteryCellFullVolts();  // the rider's maximum cell voltage, in volts
// Selects a chemistry, loads its default cell voltages and saves. Choosing the
// current chemistry again restores those defaults.
void setBatteryChemistry(uint8_t chemistry);
// Call after editing one of the cell voltages: restores their order and, as
// with a chemistry change, starts the charge estimate and measured capacity
// over, because both were derived from the old curve.
void batteryNormalizeCellVoltages(BatteryCellVoltage edited);
void batteryRestartChargeEstimate();
const BatteryStats &batteryStatsLive();
int batterySocFromVoltage(float packVoltage);  // open-circuit curve, no counting
void loadBatteryStats();
void resetBatteryStats();
// Snapshot for the UI task (main_lvgl.cpp publishes it under the telemetry
// lock; the preview build stubs it).
BatteryStats getBatteryStats();

// Settings / state (defined in app_logic.cpp)
extern DashboardMode dashboardMode;
extern AccentTheme accentTheme;
extern bool dashboardGradientEnabled;
extern bool dashboardGradientHorizontal;
extern bool dashboardGradientReverse;
extern bool dashboardGradientBell;
extern uint8_t dashboardGradientPosition;
extern AccentTheme dashboardGradientTheme;
extern DashboardAppearanceMode dashboardAppearanceMode;
extern Language language;
extern UnitMode unitMode;
extern SubmenuType submenuType;
extern char oemName[16];
extern char vehicleName[24];
extern char motorName[24];
extern char controllerName[24];
extern char vehicleBuildId[24];
extern uint8_t batterySeriesCount;
extern uint8_t batteryChemistry;  // BatteryChemistry
extern uint16_t batteryCellMinMv;
extern uint16_t batteryCellNominalMv;
extern uint16_t batteryCellMaxMv;
extern uint32_t batteryCapacityDeciAh;  // tenths of an amp-hour, up to 9999.9 kAh
extern uint16_t batteryMaxAmps;
extern uint16_t motorMaxAmps;
extern uint16_t continuousPowerDeciKw;
extern uint16_t peakPowerDeciKw;
extern uint32_t topSpeedKmh;
extern uint8_t speedModeCount;
extern uint32_t speedMode1Kmh;
extern uint32_t speedMode2Kmh;
extern uint32_t speedMode3Kmh;
extern SpeedSetupMode speedSetupMode;
extern uint8_t speedPresetMode;
extern uint8_t speedPowerCurvePercent;
extern uint8_t speedAccelCurvePercent;
extern bool firstBootConfigured;
// Whether any controller has delivered telemetry since the last settings
// reset. Until one has, the dashboard offers controller setup instead of
// waiting for a link that was never configured.
extern bool controllerEverConnected;
extern uint16_t wheelDiameterMm;
// Display-side speed trim in percent (100 = as reported). Applied to speed
// and to distance together: both carry the same wheel-size error, so a trim
// that fixed only the speed would leave the trip disagreeing with it.
extern uint16_t speedCalibrationPercent;
extern uint32_t vescUartBaud;
extern uint8_t vescCanId;
extern uint8_t vescMotorPolePairs;
extern uint16_t vescDriveRatioHundredths;
extern bool pinEnabled;
extern char securityPin[5];
extern uint8_t displayBrightnessPercent;
extern bool autoBrightnessEnabled;
extern uint16_t lightSensorDarkRaw;
extern uint16_t lightSensorBrightRaw;
extern bool themeLedEnabled;  // LED1 ambient light follows the theme color
// Off stops the Bluetooth controller link and its reconnect attempts; phone
// companion and Bluetooth updates stay available because they are started
// deliberately.
extern bool bluetoothEnabled;
extern DisplayPanelProfile displayPanelProfile;
extern ControllerType controllerType;
extern ControllerConnection controllerConnection;
extern bool developerOptionsEnabled;
extern bool dashboardDemoModeEnabled;
extern bool autoReturnEnabled;
extern uint16_t autoReturnTimeoutSeconds;
extern uint8_t configStep;
extern char pinInput[5];
extern DashboardCustomization dashboardCustomizations[MODE_COUNT];

// RGB565 palette shared with the TFT firmware, converted on demand.
// The Default accent follows the dashboard mode being *rendered* (which
// differs from dashboardMode inside the DASH UI preview).
void setAccentRenderMode(DashboardMode mode);
void setUiChromeAccent(bool enabled);
bool uiChromeAccentEnabled();
lv_color_t c565(uint16_t rgb565);
lv_color_t accentLv();
lv_color_t gradientAccentLv();
lv_color_t accentSwatchLv(AccentTheme theme);
lv_color_t seriesContrastLv(lv_color_t base);  // second series that never merges with the first  // colour of a choice, current one untouched
lv_color_t accentDarkLv();
lv_color_t dashboardColorLv(lv_color_t color);
bool dashboardLightModeActive();
void saveAutoBrightnessSetting();
uint16_t accentColor565();
uint16_t accentDarkColor565();
lv_color_t batteryLevelColorLv(int percent, lv_color_t goodColor);

constexpr uint16_t COLOR565_DIM = 0x39E7;
constexpr uint16_t COLOR565_PANEL = 0x0841;
constexpr uint16_t COLOR565_ORANGE = 0xFD20;
constexpr uint16_t COLOR565_GREEN = 0x07E0;
constexpr uint16_t COLOR565_LABEL = 0xBDF7;  // muted grey-blue label color

// Settings persistence
void saveAppSettings();
void saveDashboardCustomizationProfiles();
void loadAppSettings();
void applyDashboardCustomization(DashboardMode mode);
void captureDashboardCustomization(DashboardMode mode);
void resetDashboardCustomization(DashboardMode mode);
uint8_t dashboardDataSlotCount(DashboardMode mode);
DashboardDataItem dashboardDataDefault(DashboardMode mode, uint8_t slot);
DashboardDataItem dashboardDataSelection(DashboardMode mode, uint8_t slot);
void setDashboardDataSelection(DashboardMode mode, uint8_t slot, DashboardDataItem item);
const char *dashboardDataLabel(DashboardDataItem item);

// Option cycling (same semantics as main.cpp)
void cycleUnits();
void cycleLanguage();
void cyclePinOption();
void applyDisplayBrightness();

// Raw recovery input, provided by main_lvgl.cpp: touch contact or the on-board
// BOOT button, read straight from the hardware. Deliberately bypasses both
// LVGL and the stored touch calibration, because the recovery gesture has to
// work when that calibration is the thing that is broken.
bool recoveryInputHeld();
// Re-arms the input driver's release gate after a blocking recovery flow, so
// the release that ends the gesture cannot land as a tap on the next screen.
void touchInputArmAfterRecovery();
void applyDisplayPanelProfile();
void loadDisplayPanelProfile();
void saveDisplayPanelProfile();
void autoDetectDisplayPanelProfile(bool force);
void displayModuleInfo(char *buffer, size_t size);
void displayControllerInfo(char *buffer, size_t size);
const char *controllerTypeName();
// The controller choice is applied by a short controlled reboot, after the
// pressed-state and confirmation notice have had time to reach the panel.
void requestControllerRestart();
// Manual backlight range in percent; the panel is unreadable below ~10%.
constexpr uint8_t DISPLAY_BRIGHTNESS_MIN = 10;
constexpr uint8_t DISPLAY_BRIGHTNESS_MAX = 100;

// The calibrated touch map (touchpad_read in main_lvgl.cpp) only spans this x
// band, so the outermost columns of the screen can never be touched. Widgets
// that must be draggable to both extremes have to saturate inside it.
constexpr int TOUCH_X_MIN = 24;
constexpr int TOUCH_X_MAX = 295;
// LDR auto-brightness (implemented in main_lvgl.cpp with the backlight PWM)
int lightSensorRaw();        // smoothed ADC reading, 0..4095
int lightSensorTargetPct();  // brightness percent the sensor maps to
bool setLightSensorCalibration(int darkRaw, int brightRaw);
void saveVehicleProfile();
// Both reset types clear user/controller configuration, saved BLE pairings and
// logging mode/rate. includeHistory additionally clears ride logs and lifetime
// energy/wear totals. Neither can touch the controller-owned odometer.
void resetAppSettings(bool includeHistory);

// Naming / localization
const char *txt(const char *en, const char *fi, const char *de = nullptr, const char *fr = nullptr,
                const char *es = nullptr, const char *it = nullptr);
const char *modeName(DashboardMode mode);
const char *themeName();
const char *languageName();
const char *accentName();
const char *unitName();
const char *pinStatusName();

// Uppercase metric captions shared by every dashboard theme. They live here so
// a new theme cannot quietly reintroduce English-only labels, and so wording
// stays identical across themes.
const char *metricSpeedLabel();
const char *metricPowerLabel();
const char *metricVoltsLabel();
const char *metricAmpsLabel();
const char *metricMotorLabel();
const char *metricBatteryLabel();
const char *metricTripLabel();
const char *metricUptimeLabel();
// Abbreviated form for narrow label slots (Ride Console's cards leave ~42 px
// between the card edge and the icon). English keeps the full word.
const char *metricPowerLabelShort();
// Qualified forms for the two currents, used where a caption has room for them.
const char *metricBatteryAmpsLabel();
const char *metricPhaseAmpsLabel();

// Units and formatting
float displaySpeed(float kmh);
float displaySpeedToKmh(float speed);
float displayDistance(float km);
enum GaugeRangeKind { RANGE_SPEED, RANGE_POWER, RANGE_CURRENT, RANGE_MOTOR_CURRENT, RANGE_REGEN, RANGE_EFFICIENCY, RANGE_COUNT };
extern bool automaticGaugeRanges;
extern uint32_t learnedGaugeSpeed[3];
int automaticGaugeMaximum(GaugeRangeKind kind);
int thumbnailGaugeMaximum(GaugeRangeKind kind);
int automaticGaugeSource();
void serviceGaugeRanges();
void observePreviewGaugeRanges(const DashboardValues &values, uint32_t fields);
void resetAutomaticGaugeRanges();
void clearGaugeRangeRuntime();
int speedScaleMaxKmh();
const char *speedUnitLabel();
const char *distanceUnitLabel();
void formatFloat(char *buffer, size_t size, float value, uint8_t decimals);
void formatSpeedValue(char *buffer, size_t size, float kmh);
void formatPowerValue(char *buffer, size_t size, int watts);
const char *powerUnitLabel(int watts);
void formatPowerWithUnit(char *buffer, size_t size, int watts);
const char *metricAvgLabel();
const char *metricRangeLabel();
const char *energyRateLabel();  // "WH/KM" or "WH/MI", following the unit setting
void formatRangeText(char *buffer, size_t size, int rangeKm, bool withUnit = true);
void formatEnergyRate(char *buffer, size_t size, float whPerKm, bool withUnit = true);
// Amp-hours with one decimal, switching to kAh above 9999.9 Ah ("12.0 kAh").
constexpr uint32_t kBatteryCapacityMinDeciAh = 10;
constexpr uint32_t kBatteryCapacityMaxDeciAh = 99999000UL;  // 9999.9 kAh
void formatBatteryCapacityDeciAh(char *buffer, size_t size, uint32_t deciAh, bool withUnit = true);
void formatBatteryCapacity(char *buffer, size_t size, float amphours);
void formatUptime(char *buffer, size_t size, unsigned long seconds);
void formatUptimeShort(char *buffer, size_t size, unsigned long seconds);

// Demo ride used by the DASH UI selector: a 48 V hub launched at full throttle
// and braked back on regen, with every derived figure integrated from the same
// model so the themes agree with each other.
struct DemoRide {
  float speedKmh;
  float watts;    // negative while regenerating
  float km;       // covered since the cycle began
  float wh;       // net energy drawn since the cycle began
  float seconds;  // elapsed in this cycle
  float regenWh;  // energy returned by regen since the cycle began
};
// Pass totals = false when only speed and power are needed (history graphs).
DemoRide demoRideAt(float seconds, bool totals = true);
DemoRide demoRidePeak();
extern uint8_t demoTimeScale;
void serviceDemoMode();
void restartDemoRide();
void cycleDemoTimeScale();
void setDemoPreview(bool active);
void setDemoPreviewFrozen(bool frozen);
bool demoPreviewIsFrozen();
// Shared readings and derived stats used by selector entry and thumbnail rendering.
DashboardValues makeThumbnailDashboardValues();
BatteryStats makeThumbnailBatteryStats();
float demoRideSeconds();
DashboardValues makeDummyValues(bool dashboardOnly = false);
BatteryStats makeDemoBatteryStats(bool dashboardOnly = false);
// Pause every live repaint while a screen animates: the slide gets the whole frame budget.
// Holds only ever extend.
void holdUiUpdates(uint32_t ms);
bool uiUpdatesHeld();
void setDemoMode(bool active);
bool demoModeIsActive();
// Vehicle name for the dashboards: the demo announces itself instead of
// borrowing the configured one.
const char *dashOemName();

// Latest live VESC snapshot, supplied by main_lvgl.cpp. Returns false until a
// packet has been received, and whenever the last packet has become stale.
bool getLiveDashboardValues(DashboardValues &values);

// Health of that link. When it is not LINK_LIVE the values above are all zero,
// which on a dashboard is indistinguishable from standing still — so the
// dashboard says so instead of presenting the blanks as readings. WAITING is
// the state before the first packet ever arrives (boot, or a controller that is
// switched off); LOST means packets were flowing and then stopped.
enum TelemetryLink : uint8_t { LINK_LIVE, LINK_WAITING, LINK_LOST };
TelemetryLink telemetryLinkState();

// The controller's latest fault, as a raw mc_fault_code byte (0 = none). It is
// kept numeric so this layer needs no VescUart header — the host preview builds
// these sources without the library. Returns 0 whenever the link is not live: a
// fault latched before the link went quiet says nothing about the present.
uint8_t telemetryFaultCode();
// Short rider-facing name for that code, e.g. "MOTOR HOT". Codes with no
// plain-language equivalent fall back to their VESC name.
const char *telemetryFaultLabel(uint8_t code);

// Controller firmware version, read once per connection. False until the
// controller has answered, and again after a disconnect — the next controller
// to reply may not be the same one.
bool telemetryFirmwareVersion(uint8_t &major, uint8_t &minor);

// True once the controller has been confirmed to report its own speed and
// distance (COMM_GET_VALUES_SETUP). While it does, this board needs no wheel or
// gearing figure and offers a trim instead; when it does not, the wheel size is
// what converts motor revolutions into distance and has to be asked for.
bool telemetrySpeedFromController();

// The ride mode the controller says it is in, 1..4, or 0 when it does not
// report one. This is an observation, never an instruction: nothing in this
// firmware sets a mode, so a dashboard may only show what came back over the
// link. Standard VESC telemetry has no active-profile field and answers 0.
uint8_t telemetryRideMode();

extern char rideModeLabels[3][13];
const char *rideModeName(uint8_t mode);
