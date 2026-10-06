#pragma once

#include "app_state.h"

constexpr uint8_t CONTROLLER_LINK_MAX_DEVICES = 12;
constexpr uint8_t CONTROLLER_ADDRESS_CHARS = 20;
constexpr uint8_t CONTROLLER_NAME_CHARS = 28;

enum TelemetryField : uint32_t {
  TELEMETRY_FIELD_SPEED = 1UL << 0,
  TELEMETRY_FIELD_POWER = 1UL << 1,
  TELEMETRY_FIELD_VOLTAGE = 1UL << 2,
  TELEMETRY_FIELD_CURRENT = 1UL << 3,
  TELEMETRY_FIELD_MOTOR_TEMP = 1UL << 4,
  TELEMETRY_FIELD_ESC_TEMP = 1UL << 5,
  TELEMETRY_FIELD_TRIP_DISTANCE = 1UL << 6,
  TELEMETRY_FIELD_ODOMETER = 1UL << 7,
  TELEMETRY_FIELD_AVG_SPEED = 1UL << 8,
  TELEMETRY_FIELD_UPTIME = 1UL << 9,
  TELEMETRY_FIELD_BATTERY_SOC = 1UL << 10,
  TELEMETRY_FIELD_TRIP_ENERGY = 1UL << 11,
  TELEMETRY_FIELD_REGEN_ENERGY = 1UL << 12,
  TELEMETRY_FIELD_RIDE_EFFICIENCY = 1UL << 13,
  TELEMETRY_FIELD_LIFETIME_ENERGY = 1UL << 14,
  TELEMETRY_FIELD_LIFETIME_DISTANCE = 1UL << 15,
  TELEMETRY_FIELD_LIFETIME_EFFICIENCY = 1UL << 16,
  TELEMETRY_FIELD_RANGE = 1UL << 17,
  TELEMETRY_FIELD_EQUIVALENT_CYCLES = 1UL << 18,
  TELEMETRY_FIELD_PACK_RESISTANCE = 1UL << 19,
  TELEMETRY_FIELD_LEARNED_CAPACITY = 1UL << 20,
  TELEMETRY_FIELD_LEARNED_SAMPLES = 1UL << 21,
  TELEMETRY_FIELD_FAULT = 1UL << 22,
  TELEMETRY_FIELD_RIDE_MODE = 1UL << 23,
  // Phase current is a separate reading from pack current, and not every
  // controller reports it — Fardriver's electrical frame carries one figure.
  TELEMETRY_FIELD_MOTOR_CURRENT = 1UL << 24,
  TELEMETRY_FIELD_DUTY = 1UL << 25,
  TELEMETRY_FIELD_PHASE_VOLTAGE = 1UL << 26,
};

using TelemetryFieldMask = uint32_t;

constexpr TelemetryFieldMask TELEMETRY_FIELDS_DASHBOARD =
    TELEMETRY_FIELD_SPEED | TELEMETRY_FIELD_POWER | TELEMETRY_FIELD_VOLTAGE |
    TELEMETRY_FIELD_CURRENT | TELEMETRY_FIELD_MOTOR_TEMP | TELEMETRY_FIELD_ESC_TEMP |
    TELEMETRY_FIELD_TRIP_DISTANCE | TELEMETRY_FIELD_ODOMETER | TELEMETRY_FIELD_AVG_SPEED |
    TELEMETRY_FIELD_UPTIME | TELEMETRY_FIELD_BATTERY_SOC;
constexpr TelemetryFieldMask TELEMETRY_FIELDS_BATTERY =
    TELEMETRY_FIELD_TRIP_ENERGY | TELEMETRY_FIELD_REGEN_ENERGY |
    TELEMETRY_FIELD_RIDE_EFFICIENCY | TELEMETRY_FIELD_LIFETIME_ENERGY |
    TELEMETRY_FIELD_LIFETIME_DISTANCE | TELEMETRY_FIELD_LIFETIME_EFFICIENCY |
    TELEMETRY_FIELD_RANGE | TELEMETRY_FIELD_EQUIVALENT_CYCLES |
    TELEMETRY_FIELD_PACK_RESISTANCE | TELEMETRY_FIELD_LEARNED_CAPACITY |
    TELEMETRY_FIELD_LEARNED_SAMPLES;
constexpr TelemetryFieldMask TELEMETRY_FIELDS_ALL =
    TELEMETRY_FIELDS_DASHBOARD | TELEMETRY_FIELDS_BATTERY |
    TELEMETRY_FIELD_FAULT | TELEMETRY_FIELD_RIDE_MODE | TELEMETRY_FIELD_MOTOR_CURRENT |
    TELEMETRY_FIELD_DUTY | TELEMETRY_FIELD_PHASE_VOLTAGE;
// The highest field must survive the mask type: narrowing TelemetryFieldMask
// would otherwise drop it silently rather than fail to compile.
static_assert(TELEMETRY_FIELDS_ALL & TELEMETRY_FIELD_PHASE_VOLTAGE, "TelemetryFieldMask is too narrow");

inline bool telemetryHas(TelemetryFieldMask mask, TelemetryField field) {
  return (mask & static_cast<TelemetryFieldMask>(field)) != 0;
}

struct ControllerEnergyCounters {
  bool available;
  bool rebaseDistance;
  float ampHours;
  float ampHoursCharged;
  float wattHours;
  float wattHoursCharged;
  float odometerKm;
  float voltage;
  float current;
};

struct ControllerSample {
  uint32_t sampledAtMs;
  DashboardValues values;
  ControllerEnergyCounters energy;
  TelemetryFieldMask available;
  TelemetryFieldMask derived;
  uint8_t faultCode;
  uint8_t rideMode;
  uint8_t firmwareMajor;
  uint8_t firmwareMinor;
  bool firmwareKnown;
};

struct ControllerSnapshot {
  uint32_t sampledAtMs;
  TelemetryLink link;
  DashboardValues values;
  BatteryStats battery;
  TelemetryFieldMask available;
  TelemetryFieldMask derived;
  uint8_t faultCode;
  uint8_t rideMode;
  uint8_t firmwareMajor;
  uint8_t firmwareMinor;
  bool firmwareKnown;
};

struct ControllerLinkDevice {
  char name[CONTROLLER_NAME_CHARS];
  char address[CONTROLLER_ADDRESS_CHARS];
  int8_t rssi;
  uint8_t addressType;
  bool likelyMatch;
};

enum ControllerLinkState : uint8_t {
  CONTROLLER_LINK_IDLE,
  CONTROLLER_LINK_SCANNING,
  CONTROLLER_LINK_CONNECTING,
  CONTROLLER_LINK_DISCOVERING,
  CONTROLLER_LINK_CONNECTED,
  CONTROLLER_LINK_FAILED,
};

enum ControllerLinkError : uint8_t {
  CONTROLLER_LINK_ERR_NONE,
  CONTROLLER_LINK_ERR_SCAN_FAILED,
  CONTROLLER_LINK_ERR_DEVICE_GONE,
  CONTROLLER_LINK_ERR_CONNECT_FAILED,
  CONTROLLER_LINK_ERR_SERVICE_MISSING,
  CONTROLLER_LINK_ERR_SUBSCRIBE_FAILED,
  CONTROLLER_LINK_ERR_WORKER_FAILED,
  CONTROLLER_LINK_ERR_BUSY,
};

struct ControllerLinkStatus {
  ControllerLinkState state;
  ControllerLinkError error;
  ControllerLinkDevice devices[CONTROLLER_LINK_MAX_DEVICES];
  uint8_t deviceCount;
  bool savedDevice;
  char savedAddress[CONTROLLER_ADDRESS_CHARS];
  char connectedAddress[CONTROLLER_ADDRESS_CHARS];
  uint32_t revision;
};

struct ControllerLinkDiagnostics {
  char serviceUuid[40];
  char characteristicUuid[40];
  char lastPacketHex[64];
  uint32_t frameCount;
  uint32_t crcFailCount;
  uint32_t discardedBytes;
  uint16_t lastPacketLength;
  uint8_t lastFrameId;
  int8_t connectedRssi;
};

struct ControllerCapabilities {
  bool showsDeviceList;
  bool usesWiredLink;
  bool hasCanTarget;
  bool hasLinkDiagnostics;
  bool readsConfiguration;
  bool writesConfiguration;
  bool reportsRideMode;
};

enum ControllerBackendId : uint8_t {
  CONTROLLER_ID_VESC_UART = 1,
  CONTROLLER_ID_VESC_BLE = 2,
  CONTROLLER_ID_FARDRIVER_BLE = 3,
};

enum ControllerTransport : uint8_t {
  CONTROLLER_TRANSPORT_UART,
  CONTROLLER_TRANSPORT_BLE,
};

enum ControllerPollResult : uint8_t {
  CONTROLLER_POLL_NO_SAMPLE,
  CONTROLLER_POLL_UPDATED,
  CONTROLLER_POLL_FAILED,
};

struct ControllerBackend {
  ControllerBackendId id;
  const char *name;
  ControllerTransport transport;
  ControllerCapabilities caps;
  void (*begin)();
  void (*stop)();
  void (*resetSettings)();
  bool (*quiesceForFirmwareUpdate)();
  void (*startScan)();
  void (*connectDevice)(uint8_t index);
  void (*disconnect)();
  // Reconnect to the saved device by its stored identity; null when the link
  // needs no pairing (UART). The manager calls it to recover a dropped link.
  void (*reconnectSaved)();
  ControllerLinkStatus (*linkStatus)();
  bool (*diagnostics)(ControllerLinkDiagnostics &out);
  ControllerPollResult (*poll)(ControllerSample &out);
  uint16_t pollIntervalMs;
  uint16_t taskStackBytes;
};

uint8_t controllerBackendCount();
const ControllerBackend *activeControllerBackend();
const ControllerBackend *controllerBackendById(ControllerBackendId id);
ControllerBackendId controllerBackendIdFor(ControllerType type, ControllerConnection connection);
void controllerSelectionForId(ControllerBackendId id, ControllerType &type, ControllerConnection &connection);

const ControllerCapabilities &controllerCapabilities();
const char *controllerTypeName();
bool controllerUsesFarDriverBle();
bool controllerUsesVescBle();
bool controllerUsesBluetooth();

void controllerManagerBegin();
// Change the selected controller without restarting the display. Returns true
// when a switch was started; the telemetry task performs it asynchronously
// because the old backend's radio has to be handed back first.
bool controllerManagerSwitchTo(ControllerType type, ControllerConnection connection);
// Hand the single NimBLE client slot to the backend being set up. The display
// can hold one BLE client at a time, so scanning for a controller other than
// the running one means taking the radio from it first. Returns true once
// `wanted` may use the radio, and false while the active backend is still
// handing it back -- ask again on a later tick.
bool controllerManagerRequestRadioFor(ControllerBackendId wanted);
// Give the radio back to the configured backend. Does nothing when no
// handover is outstanding, so any exit from the wizard can call it.
void controllerManagerReleaseRadioRequest();
void controllerManagerStop();
void controllerManagerResume();
// Follows bluetoothEnabled: stops a Bluetooth controller link, or starts it
// again. Begin, resume and backend switches also leave it stopped while
// Bluetooth is off. A wired UART controller is unaffected.
void controllerManagerApplyBluetoothEnabled();
// True while a screen that scans for, picks or deliberately disconnects a
// controller is open. The manager then leaves the link alone instead of
// reconnecting the saved device underneath it.
void controllerManagerSetSetupOpen(bool open);
void controllerResetAllSettings();
bool controllerQuiesceForFirmwareUpdate();
bool controllerQuiesceForCompanion();
ControllerSnapshot controllerSnapshot();

#ifdef CYD_LVGL_PREVIEW
void previewSetControllerTelemetryFields(TelemetryFieldMask available, TelemetryFieldMask derived);
#endif
