#include "controller_manager.h"

#include <string.h>

#include "config.h"
#include "fardriver_ble.h"
#include "vesc_ble.h"

#ifndef CYD_LVGL_PREVIEW
#include "controller_backends.h"
#endif

namespace {

ControllerLinkState mapVescState(VescBleState state) {
  switch (state) {
    case VESC_BLE_SCANNING: return CONTROLLER_LINK_SCANNING;
    case VESC_BLE_CONNECTING: return CONTROLLER_LINK_CONNECTING;
    case VESC_BLE_CONNECTED: return CONTROLLER_LINK_CONNECTED;
    case VESC_BLE_ERROR: return CONTROLLER_LINK_FAILED;
    case VESC_BLE_IDLE:
    default: return CONTROLLER_LINK_IDLE;
  }
}

ControllerLinkState mapFarDriverState(FarDriverBleState state) {
  switch (state) {
    case FARDRIVER_BLE_SCANNING: return CONTROLLER_LINK_SCANNING;
    case FARDRIVER_BLE_CONNECTING: return CONTROLLER_LINK_CONNECTING;
    case FARDRIVER_BLE_DISCOVERING: return CONTROLLER_LINK_DISCOVERING;
    case FARDRIVER_BLE_CONNECTED: return CONTROLLER_LINK_CONNECTED;
    case FARDRIVER_BLE_ERROR: return CONTROLLER_LINK_FAILED;
    case FARDRIVER_BLE_IDLE:
    default: return CONTROLLER_LINK_IDLE;
  }
}

ControllerLinkError linkErrorFromMessage(const char *message) {
  if (!message || !message[0]) return CONTROLLER_LINK_ERR_NONE;
  if (strstr(message, "scan") || strstr(message, "Scan")) return CONTROLLER_LINK_ERR_SCAN_FAILED;
  if (strstr(message, "unavailable") || strstr(message, "no longer")) return CONTROLLER_LINK_ERR_DEVICE_GONE;
  if (strstr(message, "service") || strstr(message, "characteristic")) return CONTROLLER_LINK_ERR_SERVICE_MISSING;
  if (strstr(message, "subscription")) return CONTROLLER_LINK_ERR_SUBSCRIBE_FAILED;
  if (strstr(message, "queue") || strstr(message, "busy")) return CONTROLLER_LINK_ERR_BUSY;
  if (strstr(message, "worker")) return CONTROLLER_LINK_ERR_WORKER_FAILED;
  return CONTROLLER_LINK_ERR_CONNECT_FAILED;
}

void copyDevice(ControllerLinkDevice &target, const char *name, const char *address,
                int8_t rssi, uint8_t addressType, bool likely) {
  snprintf(target.name, sizeof(target.name), "%s", name ? name : "");
  snprintf(target.address, sizeof(target.address), "%s", address ? address : "");
  target.rssi = rssi;
  target.addressType = addressType;
  target.likelyMatch = likely;
}

ControllerLinkStatus vescBleLinkStatus() {
  const VescBleStatus source = vescBleStream.status();
  ControllerLinkStatus target = {};
  target.state = mapVescState(source.state);
  target.error = source.state == VESC_BLE_ERROR ? linkErrorFromMessage(source.message) : CONTROLLER_LINK_ERR_NONE;
  target.deviceCount = min<uint8_t>(source.deviceCount, CONTROLLER_LINK_MAX_DEVICES);
  for (uint8_t i = 0; i < target.deviceCount; i++)
    copyDevice(target.devices[i], source.devices[i].name, source.devices[i].address,
               source.devices[i].rssi, source.devices[i].addressType, source.devices[i].likelyVesc);
  target.savedDevice = source.savedDevice;
  snprintf(target.savedAddress, sizeof(target.savedAddress), "%s", source.savedAddress);
  snprintf(target.connectedAddress, sizeof(target.connectedAddress), "%s", source.connectedAddress);
  target.revision = source.revision;
  return target;
}

ControllerLinkStatus farDriverLinkStatus() {
  const FarDriverBleStatus source = farDriverBleStatus();
  ControllerLinkStatus target = {};
  target.state = mapFarDriverState(source.state);
  target.error = source.state == FARDRIVER_BLE_ERROR ? linkErrorFromMessage(source.message) : CONTROLLER_LINK_ERR_NONE;
  target.deviceCount = min<uint8_t>(source.deviceCount, CONTROLLER_LINK_MAX_DEVICES);
  for (uint8_t i = 0; i < target.deviceCount; i++)
    copyDevice(target.devices[i], source.devices[i].name, source.devices[i].address,
               source.devices[i].rssi, source.devices[i].addressType, source.devices[i].likelyFarDriver);
  target.savedDevice = source.savedDevice;
  snprintf(target.savedAddress, sizeof(target.savedAddress), "%s", source.savedAddress);
  snprintf(target.connectedAddress, sizeof(target.connectedAddress), "%s", source.connectedAddress);
  target.revision = source.linkRevision;
  return target;
}

ControllerLinkStatus uartLinkStatus() {
  ControllerLinkStatus status = {};
  status.state = telemetryLinkState() == LINK_LIVE ? CONTROLLER_LINK_CONNECTED : CONTROLLER_LINK_IDLE;
  return status;
}

bool noDiagnostics(ControllerLinkDiagnostics &) { return false; }

bool farDriverDiagnostics(ControllerLinkDiagnostics &out) {
  const FarDriverBleStatus source = farDriverBleStatus();
  out = {};
  snprintf(out.serviceUuid, sizeof(out.serviceUuid), "%s", source.serviceUuid);
  snprintf(out.characteristicUuid, sizeof(out.characteristicUuid), "%s", source.characteristicUuid);
  snprintf(out.lastPacketHex, sizeof(out.lastPacketHex), "%s", source.lastPacketHex);
  out.frameCount = source.frameCount;
  out.crcFailCount = source.crcFailCount;
  out.discardedBytes = source.discardedBytes;
  out.lastPacketLength = source.lastPacketLength;
  out.lastFrameId = source.lastFrameId;
  out.connectedRssi = source.connectedRssi;
  return true;
}

void noBegin() {}
void noStop() {}
void noReset() {}
void noScan() {}
void noConnect(uint8_t) {}
void noDisconnect() {}
bool quiesceImmediately() { return true; }

void resetVescBle() { vescBleStream.resetSettings(); }
void resetFarDriverBle() { farDriverBleResetSettings(); }
void startVescScan() { vescBleStream.startScan(); }
void connectVesc(uint8_t index) { vescBleStream.connectDevice(index); }
void disconnectVesc() { vescBleStream.disconnectDevice(); }
void startFarDriverScan() { farDriverBleStartScan(); }
void connectFarDriver(uint8_t index) { farDriverBleConnect(index); }
void disconnectFarDriver() { farDriverBleDisconnect(); }
void reconnectVesc() { vescBleStream.reconnectSaved(); }
void reconnectFarDriver() { farDriverBleReconnectSaved(); }

// Quiescing means the backend has stopped using the Bluetooth radio, which is
// not the same as being disconnected: the client object keeps the single
// NimBLE slot until it is deleted, and the other backend cannot create its own
// while it is held. Releasing here rather than in stop() keeps it on the one
// path that already waits for completion.
bool quiesceVescBle() {
  const VescBleState state = vescBleStream.status().state;
  const bool busy = state == VESC_BLE_CONNECTED || state == VESC_BLE_CONNECTING || state == VESC_BLE_SCANNING;
  if (!busy && vescBleStream.radioReleased()) return true;
  // One request covers all of it: the worker stops the scan, disconnects and
  // frees the client, and leaves the backend idle.
  vescBleStream.releaseRadio();
  return false;
}

bool quiesceFarDriverBle() {
  const FarDriverBleState state = farDriverBleStatus().state;
  const bool busy = state == FARDRIVER_BLE_CONNECTED || state == FARDRIVER_BLE_CONNECTING ||
                    state == FARDRIVER_BLE_DISCOVERING || state == FARDRIVER_BLE_SCANNING;
  if (!busy && farDriverBleRadioReleased()) return true;
  // One request covers all of it: the worker stops the scan, disconnects and
  // frees the client, and leaves the backend idle.
  farDriverBleReleaseRadio();
  return false;
}

#ifdef CYD_LVGL_PREVIEW
ControllerPollResult noPoll(ControllerSample &) { return CONTROLLER_POLL_NO_SAMPLE; }
#define VESC_UART_BEGIN noBegin
#define VESC_BLE_BEGIN noBegin
#define VESC_BACKEND_STOP noStop
#define VESC_BACKEND_POLL noPoll
#define FARDRIVER_BEGIN noBegin
#define FARDRIVER_STOP noStop
#define FARDRIVER_POLL noPoll
#else
#define VESC_UART_BEGIN vescUartBackendBegin
#define VESC_BLE_BEGIN vescBleBackendBegin
#define VESC_BACKEND_STOP vescBackendStop
#define VESC_BACKEND_POLL vescBackendPoll
#define FARDRIVER_BEGIN farDriverBackendBegin
#define FARDRIVER_STOP farDriverBackendStop
#define FARDRIVER_POLL farDriverBackendPoll
#endif

const ControllerBackend kBackends[] = {
    {CONTROLLER_ID_VESC_UART, "VESC", CONTROLLER_TRANSPORT_UART,
     {false, true, true, false, true, false, false},
     VESC_UART_BEGIN, VESC_BACKEND_STOP, noReset, quiesceImmediately,
     noScan, noConnect, noDisconnect, nullptr, uartLinkStatus, noDiagnostics,
     VESC_BACKEND_POLL, VESC_CONNECTED_POLL_MS, 4096},
    {CONTROLLER_ID_VESC_BLE, "VESC", CONTROLLER_TRANSPORT_BLE,
     {true, false, true, false, true, false, false},
     VESC_BLE_BEGIN, VESC_BACKEND_STOP, resetVescBle, quiesceVescBle,
     startVescScan, connectVesc, disconnectVesc, reconnectVesc, vescBleLinkStatus, noDiagnostics,
     VESC_BACKEND_POLL, VESC_CONNECTED_POLL_MS, 4096},
    {CONTROLLER_ID_FARDRIVER_BLE, "FarDriver", CONTROLLER_TRANSPORT_BLE,
     {true, false, false, true, false, false, true},
     FARDRIVER_BEGIN, FARDRIVER_STOP, resetFarDriverBle, quiesceFarDriverBle,
     startFarDriverScan, connectFarDriver, disconnectFarDriver, reconnectFarDriver, farDriverLinkStatus,
     farDriverDiagnostics,
     FARDRIVER_POLL, 100, 4096},
};

#undef VESC_UART_BEGIN
#undef VESC_BLE_BEGIN
#undef VESC_BACKEND_STOP
#undef VESC_BACKEND_POLL
#undef FARDRIVER_BEGIN
#undef FARDRIVER_STOP
#undef FARDRIVER_POLL

#ifndef CYD_LVGL_PREVIEW
portMUX_TYPE snapshotMux = portMUX_INITIALIZER_UNLOCKED;
ControllerSnapshot sharedSnapshot = {0, LINK_WAITING, {}, {}, 0, 0, 0, 0, 0, 0, false};
TaskHandle_t managerTaskHandle = nullptr;
bool managerStarted = false;
volatile bool managerSuspended = false;
bool everPublished = false;
uint32_t lastPublishedAtMs = 0;
// A backend switch is performed by the telemetry task, not by the caller: it
// has to wait for the old backend's worker to disconnect and hand the radio
// back, and blocking the UI thread for that is what the reboot used to avoid.
volatile bool switchRequested = false;
volatile ControllerBackendId switchPreviousId = CONTROLLER_ID_VESC_UART;
// The controller wizard borrows the radio the same way, and for the same
// reason: only one backend can hold the client slot. Requested and cancelled
// by the UI, carried out by the telemetry task.
enum RadioHandover : uint8_t { HANDOVER_NONE, HANDOVER_REQUESTED, HANDOVER_DONE, HANDOVER_CANCELLED };
volatile RadioHandover handoverState = HANDOVER_NONE;
volatile bool setupScreenOpen = false;

TelemetryFieldMask batteryAvailability(const BatteryStats &stats) {
  TelemetryFieldMask mask = TELEMETRY_FIELD_TRIP_ENERGY | TELEMETRY_FIELD_REGEN_ENERGY |
                            TELEMETRY_FIELD_LIFETIME_ENERGY | TELEMETRY_FIELD_LIFETIME_DISTANCE |
                            TELEMETRY_FIELD_EQUIVALENT_CYCLES | TELEMETRY_FIELD_LEARNED_SAMPLES;
  if (stats.tripWhPerKm > 0.0F) mask |= TELEMETRY_FIELD_RIDE_EFFICIENCY;
  if (stats.lifetimeWhPerKm > 0.0F) mask |= TELEMETRY_FIELD_LIFETIME_EFFICIENCY;
  if (stats.rangeKm >= 0) mask |= TELEMETRY_FIELD_RANGE;
  if (stats.packMilliOhm > 0.0F) mask |= TELEMETRY_FIELD_PACK_RESISTANCE;
  if (stats.learnedCapacityAh > 0.0F) mask |= TELEMETRY_FIELD_LEARNED_CAPACITY;
  return mask;
}

void publishSample(ControllerSample sample) {
  ControllerSnapshot previous;
  portENTER_CRITICAL(&snapshotMux);
  previous = sharedSnapshot;
  portEXIT_CRITICAL(&snapshotMux);

  const bool startingRide = previous.link != LINK_LIVE;
  if (startingRide) batteryStatsStartRide();
  if (sample.energy.available) {
    if (sample.energy.rebaseDistance) batteryStatsRebaseDistance(sample.energy.odometerKm);
    const VescCounters counters = {sample.energy.ampHours, sample.energy.ampHoursCharged,
                                  sample.energy.wattHours, sample.energy.wattHoursCharged,
                                  sample.energy.odometerKm, sample.energy.voltage, sample.energy.current};
    batteryStatsUpdate(counters);
  }

  BatteryStats battery = sample.energy.available ? batteryStatsLive() : BatteryStats{};
  if (!sample.energy.available) {
    battery.socPercent = telemetryHas(sample.available, TELEMETRY_FIELD_BATTERY_SOC)
                             ? sample.values.batteryPercent : -1;
    battery.rangeKm = -1;
  }
  TelemetryFieldMask available = sample.available;
  TelemetryFieldMask derived = sample.derived & available;
  if (sample.energy.available) {
    const TelemetryFieldMask batteryMask = batteryAvailability(battery);
    available |= batteryMask;
    derived |= batteryMask;
    if (battery.socPercent >= 0) {
      sample.values.batteryPercent = battery.socPercent;
      available |= TELEMETRY_FIELD_BATTERY_SOC;
      derived |= TELEMETRY_FIELD_BATTERY_SOC;
    }
  }

  ControllerSnapshot next = {};
  next.sampledAtMs = sample.sampledAtMs;
  next.link = LINK_LIVE;
  next.values = sample.values;
  next.battery = battery;
  next.available = available;
  next.derived = derived & available;
  next.faultCode = telemetryHas(available, TELEMETRY_FIELD_FAULT) ? sample.faultCode : 0;
  next.rideMode = telemetryHas(available, TELEMETRY_FIELD_RIDE_MODE) ? sample.rideMode : 0;
  next.firmwareMajor = sample.firmwareMajor;
  next.firmwareMinor = sample.firmwareMinor;
  next.firmwareKnown = sample.firmwareKnown;

  portENTER_CRITICAL(&snapshotMux);
  sharedSnapshot = next;
  everPublished = true;
  lastPublishedAtMs = sample.sampledAtMs;
  portEXIT_CRITICAL(&snapshotMux);

  if (!dashboardDemoModeEnabled) rideLoggerSample(next.values, next.battery, next.faultCode, next.available);
}

void updateStaleness(uint32_t now) {
  bool checkpoint = false;
  portENTER_CRITICAL(&snapshotMux);
  const TelemetryLink target = !everPublished ? LINK_WAITING
                              : now - lastPublishedAtMs > VESC_STALE_AFTER_MS ? LINK_LOST
                                                                            : LINK_LIVE;
  if (sharedSnapshot.link == LINK_LIVE && target == LINK_LOST) checkpoint = true;
  sharedSnapshot.link = target;
  if (target != LINK_LIVE) {
    sharedSnapshot.faultCode = 0;
    sharedSnapshot.firmwareKnown = false;
  }
  portEXIT_CRITICAL(&snapshotMux);
  if (checkpoint) batteryStatsCheckpoint();
}

// Up to five seconds for the old backend to go quiet. A BLE disconnect is a
// queued command answered by the peer, so it is not instant; five seconds is
// long enough for a peer that answers and short enough that a peer which has
// gone away does not hang the switch.
const uint8_t kSwitchQuiesceAttempts = 100;
const uint32_t kSwitchQuiescePollMs = 50;

void performPendingSwitch() {
  const ControllerBackend *previous = controllerBackendById(switchPreviousId);
  if (previous) {
    if (previous->quiesceForFirmwareUpdate) {
      uint8_t attempt = 0;
      while (attempt < kSwitchQuiesceAttempts && !previous->quiesceForFirmwareUpdate()) {
        vTaskDelay(pdMS_TO_TICKS(kSwitchQuiescePollMs));
        attempt++;
      }
      if (attempt == kSwitchQuiesceAttempts)
        Serial.println("Controller switch: previous backend did not release the radio");
    }
    if (previous->stop) previous->stop();
  }
  // Telemetry from the old backend must not survive into the new one's screen.
  portENTER_CRITICAL(&snapshotMux);
  sharedSnapshot = ControllerSnapshot{0, LINK_WAITING, {}, {}, 0, 0, 0, 0, 0, 0, false};
  everPublished = false;
  portEXIT_CRITICAL(&snapshotMux);
  switchRequested = false;
  controllerManagerResume();
  Serial.printf("Controller backend switched to %s\n", activeControllerBackend()->name);
}

void performRadioHandover() {
  const ControllerBackend *active = activeControllerBackend();
  if (active && active->quiesceForFirmwareUpdate) {
    uint8_t attempt = 0;
    while (attempt < kSwitchQuiesceAttempts && !active->quiesceForFirmwareUpdate()) {
      vTaskDelay(pdMS_TO_TICKS(kSwitchQuiescePollMs));
      attempt++;
    }
    if (attempt == kSwitchQuiesceAttempts)
      Serial.println("Controller setup: active backend did not release the radio");
  }
  if (active && active->stop) active->stop();
  // A cancel that arrived while this was waiting wins: the next pass resumes.
  if (handoverState == HANDOVER_REQUESTED) handoverState = HANDOVER_DONE;
}

// A Bluetooth controller that is switched off drops the link, and nothing
// else would ever bring it back: the saved device is only dialled at boot and
// after a radio handover. Keep dialling it while the link is down so a
// controller that powers up again reconnects on its own.
//
// A pending connect is effectively listening: it completes on the
// controller's first advertisement, or gives up after the backend's
// five-second timeout. Dialling again almost at once after that keeps the
// display listening nearly all the time, so a controller that powers up is
// picked up within about one advertising interval instead of waiting out a
// gap between attempts. Setup screens and radio handovers pause this.
constexpr uint32_t kReconnectIntervalMs = 500;
constexpr uint32_t kReconnectCheckMs = 250;
// The boot dial is queued by begin(), but the link reads idle until the worker
// has started the Bluetooth stack and picked it up, which can take longer than
// the retry interval. Retrying in that window would only stack more dials
// behind it in the worker's small queue.
constexpr uint32_t kBootDialGraceMs = 3000;
uint32_t nextReconnectCheckMs = 0;
uint32_t nextReconnectMs = 0;

void serviceReconnect(const ControllerBackend *backend, uint32_t now) {
  if ((int32_t)(now - nextReconnectCheckMs) < 0) return;
  nextReconnectCheckMs = now + kReconnectCheckMs;
  if (!backend->reconnectSaved || !backend->linkStatus) return;
  const ControllerLinkStatus link = backend->linkStatus();
  const bool down = link.state == CONTROLLER_LINK_IDLE || link.state == CONTROLLER_LINK_FAILED;
  // The wizard and diagnostics own the link while open, and a handover means
  // the radio is being lent to another backend.
  if (!link.savedDevice || !down || setupScreenOpen || handoverState != HANDOVER_NONE) {
    nextReconnectMs = now + kReconnectIntervalMs;
    return;
  }
  if ((int32_t)(now - nextReconnectMs) < 0) return;
  nextReconnectMs = now + kReconnectIntervalMs;
  backend->reconnectSaved();
}

void controllerTelemetryTask(void *) {
  for (;;) {
    if (switchRequested) performPendingSwitch();
    if (handoverState == HANDOVER_REQUESTED) performRadioHandover();
    if (handoverState == HANDOVER_CANCELLED) {
      handoverState = HANDOVER_NONE;
      controllerManagerResume();
    }
    // Re-read every pass: the selection can change under a running task now
    // that switching no longer restarts the display.
    const ControllerBackend *backend = activeControllerBackend();
    if (!managerSuspended && backend && backend->poll) {
      ControllerSample sample = {};
      const ControllerPollResult result = backend->poll(sample);
      if (result == CONTROLLER_POLL_UPDATED) publishSample(sample);
      else if (result == CONTROLLER_POLL_FAILED) {
        portENTER_CRITICAL(&snapshotMux);
        sharedSnapshot.link = everPublished ? LINK_LOST : LINK_WAITING;
        sharedSnapshot.faultCode = 0;
        portEXIT_CRITICAL(&snapshotMux);
      }
      updateStaleness(millis());
      serviceReconnect(backend, millis());
      portENTER_CRITICAL(&snapshotMux);
      const bool live = sharedSnapshot.link == LINK_LIVE;
      portEXIT_CRITICAL(&snapshotMux);
      // Samples are what drive the logger, so without them an open ride would
      // never reach its stop timer or notice logging being switched off.
      if (!live) rideLoggerTelemetryLost();
    } else if (!bluetoothEnabled && backend && backend->transport == CONTROLLER_TRANSPORT_BLE) {
      // Bluetooth switched off under a live link: nothing will poll or time
      // the link out, so drop it here -- otherwise the dashboard keeps the
      // last sample frozen as if it were live. Repeated each pass because a
      // poll already in flight may still publish once after the stop.
      portENTER_CRITICAL(&snapshotMux);
      if (sharedSnapshot.link == LINK_LIVE) {
        sharedSnapshot.link = LINK_LOST;
        sharedSnapshot.faultCode = 0;
        sharedSnapshot.firmwareKnown = false;
      }
      portEXIT_CRITICAL(&snapshotMux);
      rideLoggerTelemetryLost();
    }
    vTaskDelay(pdMS_TO_TICKS(backend ? max<uint16_t>(10, backend->pollIntervalMs) : 100));
  }
}
#endif

}  // namespace

ControllerBackendId controllerBackendIdFor(ControllerType type, ControllerConnection connection) {
  if (type == CONTROLLER_FARDRIVER) return CONTROLLER_ID_FARDRIVER_BLE;
  return connection == CONTROLLER_CONNECTION_BLE ? CONTROLLER_ID_VESC_BLE : CONTROLLER_ID_VESC_UART;
}

void controllerSelectionForId(ControllerBackendId id, ControllerType &type, ControllerConnection &connection) {
  switch (id) {
    case CONTROLLER_ID_VESC_BLE:
      type = CONTROLLER_VESC;
      connection = CONTROLLER_CONNECTION_BLE;
      break;
    case CONTROLLER_ID_FARDRIVER_BLE:
      type = CONTROLLER_FARDRIVER;
      connection = CONTROLLER_CONNECTION_BLE;
      break;
    case CONTROLLER_ID_VESC_UART:
    default:
      type = CONTROLLER_VESC;
      connection = CONTROLLER_CONNECTION_UART;
      break;
  }
}

uint8_t controllerBackendCount() { return sizeof(kBackends) / sizeof(kBackends[0]); }

const ControllerBackend *controllerBackendById(ControllerBackendId id) {
  for (uint8_t i = 0; i < controllerBackendCount(); i++)
    if (kBackends[i].id == id) return &kBackends[i];
  return nullptr;
}

const ControllerBackend *activeControllerBackend() {
  const ControllerBackend *backend = controllerBackendById(controllerBackendIdFor(controllerType, controllerConnection));
  return backend ? backend : &kBackends[0];
}

const ControllerCapabilities &controllerCapabilities() { return activeControllerBackend()->caps; }
const char *controllerTypeName() { return activeControllerBackend()->name; }
bool controllerUsesVescBle() { return activeControllerBackend()->id == CONTROLLER_ID_VESC_BLE; }
bool controllerUsesFarDriverBle() { return activeControllerBackend()->id == CONTROLLER_ID_FARDRIVER_BLE; }
bool controllerUsesBluetooth() { return activeControllerBackend()->transport == CONTROLLER_TRANSPORT_BLE; }

void controllerResetAllSettings() {
  for (uint8_t i = 0; i < controllerBackendCount(); i++)
    if (kBackends[i].resetSettings) kBackends[i].resetSettings();
}

#ifndef CYD_LVGL_PREVIEW
void controllerManagerBegin() {
  if (managerStarted) return;
  const ControllerBackend *backend = activeControllerBackend();
  // With Bluetooth off, a Bluetooth backend is never begun: the task still
  // starts so that turning it on later only has to resume.
  const bool radioBlocked = !bluetoothEnabled && controllerUsesBluetooth();
  if (backend && backend->begin && !radioBlocked) backend->begin();
  nextReconnectMs = millis() + kBootDialGraceMs;
  managerSuspended = radioBlocked;
  managerStarted = true;
  if (!backend || xTaskCreatePinnedToCore(controllerTelemetryTask, "controller", backend->taskStackBytes,
                                          nullptr, 1, &managerTaskHandle, 0) != pdPASS) {
    managerStarted = false;
    managerTaskHandle = nullptr;
    portENTER_CRITICAL(&snapshotMux);
    sharedSnapshot.link = LINK_WAITING;
    portEXIT_CRITICAL(&snapshotMux);
    Serial.println("Could not start controller manager task");
  }
}

bool controllerManagerSwitchTo(ControllerType type, ControllerConnection connection) {
  const ControllerBackendId wanted = controllerBackendIdFor(type, connection);
  const ControllerBackendId current = activeControllerBackend()->id;
  if (wanted == current) return false;
  if (!managerStarted) {
    controllerType = type;
    controllerConnection = connection;
    return false;
  }
  // Stop polling before the selection moves, so the task cannot poll a backend
  // that has not been begun yet. The task picks the request up on its next pass.
  managerSuspended = true;
  // The switch subsumes any handover the wizard was holding: it quiesces the
  // old backend itself and resumes on the new one.
  handoverState = HANDOVER_NONE;
  switchPreviousId = current;
  controllerType = type;
  controllerConnection = connection;
  switchRequested = true;
  return true;
}

bool controllerManagerRequestRadioFor(ControllerBackendId wanted) {
  if (!managerStarted) return true;
  if (wanted == activeControllerBackend()->id) {
    // Setting up the backend that is already running: it owns the radio, and
    // anything borrowed for a previous choice in this wizard goes back.
    controllerManagerReleaseRadioRequest();
    return true;
  }
  if (handoverState == HANDOVER_DONE) return true;
  // Stop polling first: the active backend must not resume its link between
  // the release and the new backend claiming the slot.
  managerSuspended = true;
  if (handoverState != HANDOVER_REQUESTED) handoverState = HANDOVER_REQUESTED;
  return false;
}

void controllerManagerReleaseRadioRequest() {
  if (handoverState == HANDOVER_NONE || handoverState == HANDOVER_CANCELLED) return;
  handoverState = HANDOVER_CANCELLED;
}
void controllerManagerSetSetupOpen(bool open) { setupScreenOpen = open; }

void controllerManagerStop() {
  managerSuspended = true;
  const ControllerBackend *backend = activeControllerBackend();
  if (backend && backend->stop) backend->stop();
}

void controllerManagerResume() {
  const ControllerBackend *backend = activeControllerBackend();
  if (!managerStarted) {
    controllerManagerBegin();
    return;
  }
  if (!bluetoothEnabled && controllerUsesBluetooth()) {
    managerSuspended = true;
    return;
  }
  if (backend && backend->begin) backend->begin();
  if (backend && backend->linkStatus && backend->reconnectSaved) {
    const ControllerLinkStatus status = backend->linkStatus();
    if (status.savedDevice && status.state == CONTROLLER_LINK_IDLE) backend->reconnectSaved();
  }
  // The resume dialled already; the periodic retry only follows if it fails.
  nextReconnectMs = millis() + kBootDialGraceMs;
  managerSuspended = false;
}

void controllerManagerApplyBluetoothEnabled() {
  if (!controllerUsesBluetooth()) return;
  if (bluetoothEnabled) controllerManagerResume();
  else controllerManagerStop();
}

bool controllerQuiesceForFirmwareUpdate() {
  managerSuspended = true;
  const ControllerBackend *backend = activeControllerBackend();
  return !backend || !backend->quiesceForFirmwareUpdate || backend->quiesceForFirmwareUpdate();
}

bool controllerQuiesceForCompanion() {
  // UART does not contend for the ESP32 Bluetooth controller, so keep its
  // telemetry task running throughout a phone session.
  if (!controllerUsesBluetooth()) return true;
  managerSuspended = true;
  const ControllerBackend *backend = activeControllerBackend();
  return !backend || !backend->quiesceForFirmwareUpdate || backend->quiesceForFirmwareUpdate();
}

ControllerSnapshot controllerSnapshot() {
  ControllerSnapshot snapshot;
  portENTER_CRITICAL(&snapshotMux);
  snapshot = sharedSnapshot;
  portEXIT_CRITICAL(&snapshotMux);
  if (dashboardDemoModeEnabled) {
    snapshot.sampledAtMs = millis();
    snapshot.link = LINK_LIVE;
    snapshot.values = makeDummyValues(true);
    snapshot.battery = makeDemoBatteryStats(true);
    snapshot.available = TELEMETRY_FIELDS_ALL;
    snapshot.derived = TELEMETRY_FIELDS_ALL;
    snapshot.faultCode = 0;
    snapshot.rideMode = 0;
    snapshot.firmwareKnown = false;
  }
  return snapshot;
}

BatteryStats getBatteryStats() {
  return controllerSnapshot().battery;
}

bool getLiveDashboardValues(DashboardValues &values) {
  const ControllerSnapshot snapshot = controllerSnapshot();
  values = snapshot.link == LINK_LIVE ? snapshot.values : DashboardValues{};
  return snapshot.link == LINK_LIVE;
}

TelemetryLink telemetryLinkState() {
  return controllerSnapshot().link;
}
uint8_t telemetryRideMode() {
  const ControllerSnapshot snapshot = controllerSnapshot();
  return snapshot.link == LINK_LIVE && telemetryHas(snapshot.available, TELEMETRY_FIELD_RIDE_MODE)
             ? snapshot.rideMode : 0;
}
bool telemetrySpeedFromController() {
  const ControllerSnapshot snapshot = controllerSnapshot();
  return snapshot.link == LINK_LIVE && telemetryHas(snapshot.available, TELEMETRY_FIELD_SPEED) &&
         !telemetryHas(snapshot.derived, TELEMETRY_FIELD_SPEED);
}
bool telemetryFirmwareVersion(uint8_t &major, uint8_t &minor) {
  const ControllerSnapshot snapshot = controllerSnapshot();
  major = snapshot.firmwareMajor;
  minor = snapshot.firmwareMinor;
  return snapshot.link == LINK_LIVE && snapshot.firmwareKnown;
}
uint8_t telemetryFaultCode() {
  const ControllerSnapshot snapshot = controllerSnapshot();
  return snapshot.link == LINK_LIVE && telemetryHas(snapshot.available, TELEMETRY_FIELD_FAULT)
             ? snapshot.faultCode : 0;
}
#else
namespace {
TelemetryFieldMask previewAvailableFields = TELEMETRY_FIELDS_ALL;
TelemetryFieldMask previewDerivedFields = TELEMETRY_FIELDS_ALL;
}

void previewSetControllerTelemetryFields(TelemetryFieldMask available, TelemetryFieldMask derived) {
  previewAvailableFields = available;
  previewDerivedFields = derived & available;
}

void controllerManagerBegin() {}
bool controllerManagerSwitchTo(ControllerType type, ControllerConnection connection) {
  controllerType = type;
  controllerConnection = connection;
  return false;
}
bool controllerManagerRequestRadioFor(ControllerBackendId) { return true; }
void controllerManagerReleaseRadioRequest() {}
void controllerManagerStop() {}
void controllerManagerResume() {}
void controllerManagerApplyBluetoothEnabled() {}
void controllerManagerSetSetupOpen(bool) {}
bool controllerQuiesceForFirmwareUpdate() { return true; }
bool controllerQuiesceForCompanion() { return true; }

ControllerSnapshot controllerSnapshot() {
  ControllerSnapshot snapshot = {};
  if (dashboardDemoModeEnabled) {
    snapshot.sampledAtMs = millis();
    snapshot.link = LINK_LIVE;
    snapshot.values = makeDummyValues(true);
    snapshot.battery = makeDemoBatteryStats(true);
    snapshot.available = TELEMETRY_FIELDS_ALL;
    snapshot.derived = TELEMETRY_FIELDS_ALL;
    return snapshot;
  }
  snapshot.sampledAtMs = millis();
  snapshot.link = telemetryLinkState();
  getLiveDashboardValues(snapshot.values);
  snapshot.battery = getBatteryStats();
  snapshot.available = snapshot.link == LINK_LIVE ? previewAvailableFields : 0;
  snapshot.derived = snapshot.link == LINK_LIVE ? previewDerivedFields : 0;
  snapshot.faultCode = telemetryFaultCode();
  snapshot.rideMode = telemetryRideMode();
  snapshot.firmwareKnown = telemetryFirmwareVersion(snapshot.firmwareMajor, snapshot.firmwareMinor);
  return snapshot;
}
#endif
