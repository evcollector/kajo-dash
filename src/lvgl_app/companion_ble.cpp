#include "companion_ble.h"

#include <NimBLEDevice.h>

#include "app_state.h"
#include "config.h"
#include "controller_manager.h"
#include "firmware_update_ble.h"

namespace {

constexpr uint8_t kProtocolVersion = 2;
constexpr uint8_t kIdentityMessage = 1;
constexpr uint8_t kCommandIdentity = 1;
constexpr uint8_t kCommandEndSession = 2;
constexpr uint8_t kCommandRideCatalog = 3;
constexpr uint8_t kCommandRideSeries = 4;
constexpr uint8_t kCommandRideFile = 5;
constexpr uint8_t kCommandReadTheme = 6;
constexpr uint8_t kCommandApplyTheme = 7;
constexpr uint8_t kCommandEnterUpdateMode = 8;
constexpr uint32_t kPrepareTimeoutMs = 8000;
constexpr uint32_t kAdvertisingTimeoutMs = 60000;
constexpr uint32_t kConnectedTimeoutMs = 300000;
constexpr size_t kIdentityBytes = 48;
constexpr size_t kMaximumResponseBytes = 244;
constexpr size_t kMaximumControlBytes = 20;

CompanionBleStatus sharedStatus = {};
portMUX_TYPE statusMux = portMUX_INITIALIZER_UNLOCKED;
NimBLEServer *server = nullptr;
NimBLECharacteristic *responseCharacteristic = nullptr;
volatile bool active = false;
volatile bool stopRequested = false;
bool serverStarted = false;
bool controllerPaused = false;
volatile bool phoneConnected = false;
uint32_t stateStartedMs = 0;
uint32_t sessionDeadlineMs = 0;
portMUX_TYPE commandMux = portMUX_INITIALIZER_UNLOCKED;
volatile bool commandPending = false;
volatile bool updateModeRequested = false;
uint32_t updateModeAtMs = 0;
uint8_t pendingCommand[kMaximumControlBytes] = {};
size_t pendingCommandBytes = 0;

void writeU32Le(uint8_t *bytes, uint32_t value) {
  bytes[0] = static_cast<uint8_t>(value);
  bytes[1] = static_cast<uint8_t>(value >> 8);
  bytes[2] = static_cast<uint8_t>(value >> 16);
  bytes[3] = static_cast<uint8_t>(value >> 24);
}

void writeU16Le(uint8_t *bytes, uint16_t value) {
  bytes[0] = static_cast<uint8_t>(value);
  bytes[1] = static_cast<uint8_t>(value >> 8);
}

uint32_t readU32Le(const uint8_t *bytes) {
  return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) | ((uint32_t)bytes[2] << 16) |
         ((uint32_t)bytes[3] << 24);
}

void publishStatus(CompanionBleState state, const char *message) {
  portENTER_CRITICAL(&statusMux);
  sharedStatus.state = state;
  sharedStatus.controllerPaused = controllerPaused;
  sharedStatus.controllerUsesBle = controllerUsesBluetooth();
  sharedStatus.revision++;
  snprintf(sharedStatus.message, sizeof(sharedStatus.message), "%s", message ? message : "");
  portEXIT_CRITICAL(&statusMux);
}

void writeIdentity(bool notify) {
  if (!responseCharacteristic) return;
  uint8_t payload[kIdentityBytes] = {'K', 'A', 'J', 'C'};
  payload[4] = kProtocolVersion;
  payload[5] = kIdentityMessage;
  payload[6] = (controllerPaused ? 0x01 : 0) | (controllerUsesBluetooth() ? 0x02 : 0);
  payload[7] = static_cast<uint8_t>(displayPanelProfile);
  payload[8] = static_cast<uint8_t>(controllerType);
  payload[9] = static_cast<uint8_t>(controllerConnection);
  const size_t nameLength = strnlen(vehicleName, sizeof(vehicleName));
  payload[10] = static_cast<uint8_t>(nameLength);
  payload[11] = static_cast<uint8_t>(dashboardMode);
  writeU32Le(payload + 12, static_cast<uint32_t>(CYD_FIRMWARE_VERSION_CODE));
  const uint64_t chipId = ESP.getEfuseMac();
  for (uint8_t i = 0; i < 6; i++) payload[16 + i] = static_cast<uint8_t>(chipId >> (i * 8));
  memcpy(payload + 22, vehicleName, min(nameLength, static_cast<size_t>(24)));
  responseCharacteristic->setValue(payload, sizeof(payload));
  if (notify && sharedStatus.state == COMPANION_BLE_CONNECTED) responseCharacteristic->notify();
}

void writeError(uint8_t code) {
  const uint8_t payload[] = {'K', 'A', 'J', 'E', kProtocolVersion, 0xFF, code};
  responseCharacteristic->setValue(payload, sizeof(payload));
}

void writeRideCatalog(uint8_t page) {
  const RideLogCatalogStatus catalog = rideLoggerCatalogStatus();
  const RideLoggingStatus logging = rideLoggerStatus();
  uint8_t payload[kMaximumResponseBytes] = {'K', 'A', 'J', 'R', kProtocolVersion, 2};
  payload[6] = (catalog.loading ? 0x01 : 0) | (logging.cardReady ? 0x02 : 0);
  payload[8] = page;
  uint8_t eligible = 0;
  RideLogSummary summaries[RIDE_LOG_CATALOG_MAX];
  for (uint8_t i = 0; i < catalog.count && eligible < RIDE_LOG_CATALOG_MAX; i++) {
    RideLogSummary summary = {};
    if (!rideLoggerCatalogEntry(i, summary)) continue;
    if (logging.recording && summary.rideId == logging.rideId) continue;
    summaries[eligible++] = summary;
  }
  payload[7] = eligible;
  const uint8_t first = page * 8;
  const uint8_t count = first < eligible ? min<uint8_t>(8, eligible - first) : 0;
  payload[9] = count;
  size_t offset = 10;
  for (uint8_t i = 0; i < count; i++) {
    const RideLogSummary &summary = summaries[first + i];
    writeU32Le(payload + offset, summary.rideId);
    writeU32Le(payload + offset + 4, summary.durationSeconds);
    writeU32Le(payload + offset + 8, summary.distanceMeters);
    writeU32Le(payload + offset + 12, static_cast<uint32_t>(summary.netWhDeci));
    writeU32Le(payload + offset + 16, summary.regenWhDeci);
    writeU32Le(payload + offset + 20, summary.fileBytes);
    payload[offset + 24] = summary.sampleHz;
    payload[offset + 25] = summary.valid ? 1 : 0;
    offset += 26;
  }
  responseCharacteristic->setValue(payload, offset);
}

void writeRideSeries(const uint8_t *command, size_t size) {
  if (size < 15) return writeError(2);
  const uint32_t rideId = readU32Le(command + 1);
  const RideLogSeriesField field = static_cast<RideLogSeriesField>(command[5]);
  const uint32_t startSeconds = readU32Le(command + 6);
  const uint32_t endSeconds = readU32Le(command + 10);
  const uint8_t maximumPoints = min<uint8_t>(36, command[14]);
  RideLogSeriesPoint points[36];
  uint8_t count = 0;
  if ((field != RIDE_SERIES_SPEED && field != RIDE_SERIES_POWER) ||
      !rideLoggerReadSeries(rideId, field, startSeconds, endSeconds, maximumPoints, points, count))
    return writeError(3);
  uint8_t payload[kMaximumResponseBytes] = {'K', 'A', 'J', 'S', kProtocolVersion, 3, static_cast<uint8_t>(field), count};
  writeU32Le(payload + 8, rideId);
  size_t offset = 12;
  for (uint8_t i = 0; i < count; i++) {
    writeU16Le(payload + offset, points[i].elapsedSeconds);
    writeU32Le(payload + offset + 2, static_cast<uint32_t>(points[i].value));
    offset += 6;
  }
  responseCharacteristic->setValue(payload, offset);
}

void writeRideFile(const uint8_t *command, size_t size) {
  if (size < 10) return writeError(2);
  const uint32_t rideId = readU32Le(command + 1);
  const uint32_t requestedOffset = readU32Le(command + 5);
  const size_t requested = min<size_t>(220, command[9]);
  uint8_t payload[kMaximumResponseBytes] = {'K', 'A', 'J', 'F', kProtocolVersion, 4};
  size_t bytesRead = 0;
  uint32_t fileBytes = 0;
  if (!rideLoggerReadFileChunk(rideId, requestedOffset, payload + 20, requested, bytesRead, fileBytes))
    return writeError(4);
  writeU16Le(payload + 6, static_cast<uint16_t>(bytesRead));
  writeU32Le(payload + 8, rideId);
  writeU32Le(payload + 12, requestedOffset);
  writeU32Le(payload + 16, fileBytes);
  responseCharacteristic->setValue(payload, 20 + bytesRead);
}

void writeThemeConfiguration(uint8_t requestedMode) {
  if (requestedMode >= MODE_COUNT) return writeError(5);
  const DashboardMode mode = static_cast<DashboardMode>(requestedMode);
  // This also guarantees the profile array has been initialized before it is
  // copied into a companion response.
  dashboardDataSelection(mode, 0);
  const DashboardCustomization &custom = dashboardCustomizations[mode];
  uint8_t payload[13 + DASH_DATA_SLOTS_MAX] = {'K', 'A', 'J', 'T', kProtocolVersion, 5};
  payload[6] = requestedMode;
  payload[7] = dashboardMode == mode ? 1 : 0;
  payload[8] = custom.accent;
  payload[9] = custom.backgroundAccent;
  payload[10] = custom.gradientPosition;
  payload[11] = custom.flags;
  payload[12] = dashboardDataSlotCount(mode);
  memcpy(payload + 13, custom.data, DASH_DATA_SLOTS_MAX);
  responseCharacteristic->setValue(payload, sizeof(payload));
}

void applyThemeConfiguration(const uint8_t *command, size_t size) {
  static_assert(sizeof(DashboardCustomization) == 4 + DASH_DATA_SLOTS_MAX,
                "Companion theme packet must match DashboardCustomization");
  if (size != 2 + sizeof(DashboardCustomization)) return writeError(2);
  const uint8_t requestedMode = command[1];
  if (requestedMode >= MODE_COUNT) return writeError(5);

  DashboardCustomization incoming = {};
  memcpy(&incoming, command + 2, sizeof(incoming));
  const DashboardMode mode = static_cast<DashboardMode>(requestedMode);
  const uint8_t appearance = (incoming.flags >> 4) & 0x03;
  if (incoming.accent >= ACCENT_COUNT || incoming.backgroundAccent >= ACCENT_COUNT ||
      incoming.gradientPosition < 15 || incoming.gradientPosition > 85 || appearance < 1 || appearance > 3 ||
      (incoming.flags & 0xC0) != 0)
    return writeError(6);
  for (uint8_t slot = 0; slot < dashboardDataSlotCount(mode); slot++) {
    if (incoming.data[slot] >= DATA_COUNT) return writeError(6);
  }

  dashboardCustomizations[mode] = incoming;
  dashboardMode = mode;
  applyDashboardCustomization(mode);
  saveAppSettings();
  writeThemeConfiguration(requestedMode);
}

void requestUpdateMode() {
  const FirmwareUpdateBleStatus update = firmwareUpdateBleStatus();
  if (!update.keyConfigured) return writeError(7);
  const uint8_t payload[] = {'K', 'A', 'J', 'M', kProtocolVersion, 6, 1, 1};
  responseCharacteristic->setValue(payload, sizeof(payload));
  updateModeRequested = true;
  updateModeAtMs = millis() + 500;
  publishStatus(COMPANION_BLE_PREPARING, "Update requested - switching Bluetooth mode");
}

void processPendingCommand() {
  uint8_t command[kMaximumControlBytes];
  size_t size = 0;
  portENTER_CRITICAL(&commandMux);
  if (commandPending) {
    size = pendingCommandBytes;
    memcpy(command, pendingCommand, size);
    commandPending = false;
  }
  portEXIT_CRITICAL(&commandMux);
  if (!size || sharedStatus.state != COMPANION_BLE_CONNECTED) return;
  if (command[0] == kCommandIdentity)
    writeIdentity(false);
  else if (command[0] == kCommandRideCatalog)
    writeRideCatalog(size > 1 ? command[1] : 0);
  else if (command[0] == kCommandRideSeries)
    writeRideSeries(command, size);
  else if (command[0] == kCommandRideFile)
    writeRideFile(command, size);
  else if (command[0] == kCommandReadTheme)
    size >= 2 ? writeThemeConfiguration(command[1]) : writeError(2);
  else if (command[0] == kCommandApplyTheme)
    applyThemeConfiguration(command, size);
  else if (command[0] == kCommandEnterUpdateMode)
    requestUpdateMode();
  else
    writeError(1);
}

class ControlCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic *characteristic, NimBLEConnInfo &) override {
    const auto value = characteristic->getValue();
    if (value.size() == 0) return;
    if (static_cast<uint8_t>(value[0]) == kCommandEndSession)
      stopRequested = true;
    else if (sharedStatus.state == COMPANION_BLE_CONNECTED) {
      sessionDeadlineMs = millis() + kConnectedTimeoutMs;
      portENTER_CRITICAL(&commandMux);
      pendingCommandBytes = min<size_t>(value.size(), sizeof(pendingCommand));
      memcpy(pendingCommand, value.data(), pendingCommandBytes);
      commandPending = true;
      portEXIT_CRITICAL(&commandMux);
    }
  }
};

class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer *activeServer, NimBLEConnInfo &connection) override {
    if (!active) {
      activeServer->disconnect(connection.getConnHandle());
      return;
    }
    activeServer->setDataLen(connection.getConnHandle(), 251);
    phoneConnected = true;
    sessionDeadlineMs = millis() + kConnectedTimeoutMs;
    if (controllerUsesBluetooth()) {
      stateStartedMs = millis();
      publishStatus(COMPANION_BLE_PREPARING, "Phone connected - pausing controller Bluetooth");
    } else {
      publishStatus(COMPANION_BLE_CONNECTED, "Phone connected - controller remains active");
      writeIdentity(true);
      rideLoggerSetStorageNeeded(true);
      rideLoggerRequestCatalog();
    }
  }

  void onDisconnect(NimBLEServer *, NimBLEConnInfo &, int) override {
    if (active) stopRequested = true;
  }
};

ControlCallbacks controlCallbacks;
ServerCallbacks serverCallbacks;

bool startServer() {
  if (!NimBLEDevice::isInitialized()) NimBLEDevice::init("KAJO Companion");
  NimBLEDevice::setDeviceName("KAJO Companion");
  NimBLEDevice::setPower(3);
  NimBLEDevice::setMTU(247);
  server = NimBLEDevice::createServer();
  if (!server) return false;
  server->setCallbacks(&serverCallbacks, false);
  if (!responseCharacteristic) {
    NimBLEService *service = server->createService(COMPANION_SERVICE_UUID);
    if (!service) return false;
    NimBLECharacteristic *control = service->createCharacteristic(
        COMPANION_CONTROL_UUID, NIMBLE_PROPERTY::WRITE, kMaximumControlBytes);
    responseCharacteristic = service->createCharacteristic(
        COMPANION_RESPONSE_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY, kMaximumResponseBytes);
    if (!control || !responseCharacteristic) return false;
    control->setCallbacks(&controlCallbacks);
  }
  if (!server->start()) return false;
  server->advertiseOnDisconnect(false);
  writeIdentity(false);
  NimBLEAdvertising *advertising = NimBLEDevice::getAdvertising();
  if (!advertising) return false;
  advertising->reset();
  // Scan response first: the name then goes there and the 128-bit service
  // UUID fits in the advertisement itself, where filtered scans see it.
  advertising->enableScanResponse(true);
  advertising->setName("KAJO Companion");
  advertising->addServiceUUID(COMPANION_SERVICE_UUID);
  if (!NimBLEDevice::startAdvertising()) return false;
  serverStarted = true;
  sessionDeadlineMs = millis() + kAdvertisingTimeoutMs;
  return true;
}

void finishSession(const char *message, bool resumeController = true) {
  active = false;
  stopRequested = false;
  phoneConnected = false;
  sessionDeadlineMs = 0;
  if (NimBLEDevice::isInitialized()) NimBLEDevice::stopAdvertising();
  if (server) {
    for (uint16_t handle : server->getPeerDevices()) server->disconnect(handle);
  }
  if (controllerPaused && resumeController) controllerManagerResume();
  controllerPaused = false;
  rideLoggerSetStorageNeeded(false);
  publishStatus(COMPANION_BLE_OFF, message);
}

}  // namespace

bool companionBleStart() {
  if (active) return true;
  if (firmwareUpdateBleActive()) {
    publishStatus(COMPANION_BLE_ERROR, "Firmware update mode is active");
    return false;
  }
  active = true;
  stopRequested = false;
  phoneConnected = false;
  commandPending = false;
  updateModeRequested = false;
  updateModeAtMs = 0;
  serverStarted = false;
  controllerPaused = false;
  stateStartedMs = millis();
  publishStatus(COMPANION_BLE_PREPARING, "Preparing phone connection");
  return true;
}

void companionBleStop() {
  if (active) stopRequested = true;
}

void companionBleService() {
  if (updateModeRequested) {
    // A client may disconnect immediately after reading the acknowledgement.
    // Keep controller BLE paused until the scheduled OTA handoff owns it.
    if (static_cast<int32_t>(millis() - updateModeAtMs) < 0) return;
    updateModeRequested = false;
    updateModeAtMs = 0;
    const bool controllerWasPaused = controllerPaused;
    finishSession("Switching to firmware update mode", false);
    if (!firmwareUpdateBleStart()) {
      if (controllerWasPaused) controllerManagerResume();
      publishStatus(COMPANION_BLE_ERROR, "Could not start firmware update mode");
    }
    return;
  }
  if (stopRequested) {
    finishSession("Companion session ended");
    return;
  }
  if (!active) return;
  CompanionBleStatus status = companionBleStatus();
  if (status.state == COMPANION_BLE_PREPARING) {
    if (phoneConnected && controllerUsesBluetooth()) {
      controllerPaused = true;
      if (!controllerQuiesceForCompanion()) {
        if (millis() - stateStartedMs >= kPrepareTimeoutMs) {
          finishSession("Could not pause controller Bluetooth");
          publishStatus(COMPANION_BLE_ERROR, "Could not pause controller Bluetooth");
        }
        return;
      }
      publishStatus(COMPANION_BLE_CONNECTED, "Phone connected - controller Bluetooth paused");
      writeIdentity(true);
      rideLoggerSetStorageNeeded(true);
      rideLoggerRequestCatalog();
      return;
    }
    if (phoneConnected) return;
    if (!serverStarted && !startServer()) {
      finishSession("Could not start Companion Bluetooth");
      publishStatus(COMPANION_BLE_ERROR, "Could not start Companion Bluetooth");
      return;
    }
    publishStatus(COMPANION_BLE_ADVERTISING, "Ready for phone connection");
  }
  if (status.state == COMPANION_BLE_CONNECTED) processPendingCommand();
  if (sessionDeadlineMs && static_cast<int32_t>(millis() - sessionDeadlineMs) >= 0)
    finishSession(status.state == COMPANION_BLE_CONNECTED ? "Phone session timed out" : "Connection window expired");
}

bool companionBleActive() { return active; }

CompanionBleStatus companionBleStatus() {
  CompanionBleStatus copy;
  portENTER_CRITICAL(&statusMux);
  copy = sharedStatus;
  const uint32_t deadline = sessionDeadlineMs;
  portEXIT_CRITICAL(&statusMux);
  copy.secondsRemaining = deadline && static_cast<int32_t>(deadline - millis()) > 0
                              ? (deadline - millis() + 999) / 1000
                              : 0;
  return copy;
}
