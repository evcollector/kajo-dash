#include "fardriver_ble.h"

#include <ctype.h>
#include <NimBLEDevice.h>
#include <Preferences.h>

#include "controller_manager.h"
#include "fardriver_protocol.h"

namespace {

enum CommandType : uint8_t {
  COMMAND_SCAN,
  COMMAND_CONNECT,
  COMMAND_CONNECT_SAVED,
  COMMAND_DISCONNECT,
  COMMAND_FORGET,
  COMMAND_RELEASE,
};

struct Command {
  CommandType type;
  uint8_t deviceIndex;
};

static FarDriverBleStatus status = {};
static portMUX_TYPE statusMux = portMUX_INITIALIZER_UNLOCKED;
static QueueHandle_t commandQueue = nullptr;
static TaskHandle_t workerTaskHandle = nullptr;
static bool nimbleInitialized = false;
static NimBLEClient *client = nullptr;
// NimBLE is built with a single client slot (CONFIG_BT_NIMBLE_MAX_CONNECTIONS),
// so the other controller backend cannot create its own until this one hands
// the slot back. Released means "no client object exists here", which is the
// state a fresh boot starts in.
static volatile bool radioReleased = true;
static volatile bool releasePending = false;
static Preferences preferences;
static uint8_t persistedAddressType = 0;
static char persistedServiceUuid[40] = "";
static char persistedCharacteristicUuid[40] = "";
static bool persistedIdentityValid = false;

static void copyText(char *destination, size_t size, const char *source) {
  if (!destination || size == 0) return;
  snprintf(destination, size, "%s", source ? source : "");
}

static bool containsIgnoreCase(const std::string &text, const char *needle) {
  std::string lowered = text;
  for (char &c : lowered) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
  return lowered.find(needle) != std::string::npos;
}

static void setState(FarDriverBleState state, const char *message) {
  portENTER_CRITICAL(&statusMux);
  status.state = state;
  copyText(status.message, sizeof(status.message), message);
  status.linkRevision++;
  status.revision++;
  portEXIT_CRITICAL(&statusMux);
}

// Only ever touched from NimBLE's host task, apart from the reset before
// subscription, so the stream needs no lock of its own; only the published
// snapshots do.
static FarDriverFrameStream rxStream;

static FarDriverTelemetry telemetry = {0, 0.0F, 0.0F, 0, 0, 0, 0, -1,
                                       false, false, false, false, false};
static portMUX_TYPE dataMux = portMUX_INITIALIZER_UNLOCKED;

static void publishFrame(const uint8_t *frame, void *) {
  char hex[64];
  size_t used = 0;
  for (size_t i = 0; i < FARDRIVER_FRAME_LENGTH && used + 3 < sizeof(hex); i++)
    used += snprintf(hex + used, sizeof(hex) - used, i == 0 ? "%02X" : " %02X", frame[i]);
  portENTER_CRITICAL(&statusMux);
  copyText(status.lastPacketHex, sizeof(status.lastPacketHex), hex);
  status.lastPacketLength = FARDRIVER_FRAME_LENGTH;
  // Six-bit id; below 0x37 it indexes the controller's memory-address table, so
  // watching it step is how you confirm the documented rotation is happening.
  status.lastFrameId = frame[1] & 0x3F;
  status.frameCount++;
  status.revision++;
  portEXIT_CRITICAL(&statusMux);
  portENTER_CRITICAL(&dataMux);
  if (farDriverDecodeFrame(frame, telemetry)) telemetry.updatedAtMs = millis();
  portEXIT_CRITICAL(&dataMux);
}

static void notificationCallback(NimBLERemoteCharacteristic *, uint8_t *data, size_t length, bool) {
  const FarDriverFrameStream::Counts counts = rxStream.push(data, length, publishFrame, nullptr);
  if (!counts.discardedBytes && !counts.crcFailures) return;
  portENTER_CRITICAL(&statusMux);
  status.discardedBytes += counts.discardedBytes;
  status.crcFailCount += counts.crcFailures;
  status.revision++;
  portEXIT_CRITICAL(&statusMux);
}

class DiagnosticClientCallbacks : public NimBLEClientCallbacks {
  void onConnect(NimBLEClient *) override {}

  void onDisconnect(NimBLEClient *, int reason) override {
    char message[48];
    snprintf(message, sizeof(message), "Disconnected (reason %d)", reason);
    portENTER_CRITICAL(&statusMux);
    status.connectedAddress[0] = '\0';
    status.serviceUuid[0] = '\0';
    status.characteristicUuid[0] = '\0';
    status.connectedRssi = 0;
    portEXIT_CRITICAL(&statusMux);
    setState(FARDRIVER_BLE_IDLE, message);
  }
};

static DiagnosticClientCallbacks clientCallbacks;

class DiagnosticScanCallbacks : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice *advertisedDevice) override {
    const std::string address = advertisedDevice->getAddress().toString();
    const std::string name = advertisedDevice->haveName() ? advertisedDevice->getName() : std::string();
    const bool likely = advertisedDevice->isAdvertisingService(NimBLEUUID("FFE0")) ||
                        containsIgnoreCase(name, "fardriver") || containsIgnoreCase(name, "far driver") ||
                        containsIgnoreCase(name, "yuanqu") || containsIgnoreCase(name, "controldm");

    portENTER_CRITICAL(&statusMux);
    uint8_t index = status.deviceCount;
    for (uint8_t i = 0; i < status.deviceCount; i++) {
      if (strncmp(status.devices[i].address, address.c_str(), sizeof(status.devices[i].address)) == 0) {
        index = i;
        break;
      }
    }
    if (index < FARDRIVER_BLE_MAX_DEVICES) {
      const char *displayName = name.empty() ? "Unnamed BLE device" : name.c_str();
      char clippedName[sizeof(status.devices[index].name)] = {};
      copyText(clippedName, sizeof(clippedName), displayName);
      const bool added = index == status.deviceCount;
      const bool identityChanged = !added &&
          (strncmp(status.devices[index].name, clippedName, sizeof(status.devices[index].name)) ||
           status.devices[index].likelyFarDriver != likely);
      if (added) status.deviceCount++;
      FarDriverBleDeviceInfo &entry = status.devices[index];
      copyText(entry.name, sizeof(entry.name), clippedName);
      copyText(entry.address, sizeof(entry.address), address.c_str());
      entry.rssi = advertisedDevice->getRSSI();
      entry.addressType = advertisedDevice->getAddress().getType();
      entry.likelyFarDriver = likely;
      if (added || identityChanged) {
        status.linkRevision++;
        status.revision++;
      }
    }
    portEXIT_CRITICAL(&statusMux);
  }

  void onScanEnd(const NimBLEScanResults &results, int reason) override {
    char message[48];
    snprintf(message, sizeof(message), "%d device%s found (scan %d)", results.getCount(),
             results.getCount() == 1 ? "" : "s", reason);
    setState(FARDRIVER_BLE_IDLE, message);
  }
};

static DiagnosticScanCallbacks scanCallbacks;

static void ensureNimbleInitialized() {
  if (nimbleInitialized) return;
  if (!NimBLEDevice::isInitialized()) NimBLEDevice::init("KAJO-Dash");
  NimBLEDevice::setPower(3);
  NimBLEScan *scan = NimBLEDevice::getScan();
  scan->setScanCallbacks(&scanCallbacks, false);
  scan->setActiveScan(true);
  scan->setInterval(100);
  scan->setWindow(60);
  nimbleInitialized = true;
}

static void startScan() {
  ensureNimbleInitialized();
  NimBLEScan *scan = NimBLEDevice::getScan();
  // Another setup backend may have used the shared NimBLE scanner since this
  // worker was initialized. Always reclaim our callback before a new scan.
  scan->setScanCallbacks(&scanCallbacks, false);
  scan->stop();
  scan->clearResults();
  portENTER_CRITICAL(&statusMux);
  memset(status.devices, 0, sizeof(status.devices));
  status.deviceCount = 0;
  status.state = FARDRIVER_BLE_SCANNING;
  copyText(status.message, sizeof(status.message), "Scanning for BLE controllers...");
  status.linkRevision++;
  status.revision++;
  portEXIT_CRITICAL(&statusMux);
  if (!scan->start(5000, false, true)) setState(FARDRIVER_BLE_ERROR, "Unable to start BLE scan");
}

static NimBLERemoteCharacteristic *findNotificationCharacteristic(NimBLEClient *activeClient,
                                                                  NimBLERemoteService **selectedService) {
  NimBLERemoteService *service = activeClient->getService(NimBLEUUID("FFE0"));
  if (service) {
    NimBLERemoteCharacteristic *characteristic = service->getCharacteristic(NimBLEUUID("FFEC"));
    if (characteristic && (characteristic->canNotify() || characteristic->canIndicate())) {
      *selectedService = service;
      return characteristic;
    }
  }

  if (!activeClient->discoverAttributes()) return nullptr;
  const std::vector<NimBLERemoteService *> &services = activeClient->getServices(false);
  for (NimBLERemoteService *candidateService : services) {
    const std::vector<NimBLERemoteCharacteristic *> &characteristics = candidateService->getCharacteristics(true);
    for (NimBLERemoteCharacteristic *characteristic : characteristics) {
      if (characteristic->canNotify() || characteristic->canIndicate()) {
        *selectedService = candidateService;
        return characteristic;
      }
    }
  }
  return nullptr;
}

static void connectTo(const FarDriverBleDeviceInfo &device) {
  ensureNimbleInitialized();
  NimBLEDevice::getScan()->stop();
  setState(FARDRIVER_BLE_CONNECTING, "Connecting to selected device...");
  if (!client) {
    client = NimBLEDevice::createClient();
    // The other controller backend may still own the single configured client
    // slot. Report the failure instead of dereferencing nullptr and rebooting.
    if (!client) {
      setState(FARDRIVER_BLE_ERROR, "Bluetooth radio busy; try again");
      return;
    }
    radioReleased = false;
    client->setClientCallbacks(&clientCallbacks, false);
    client->setConnectTimeout(5000);
  } else if (client->isConnected()) {
    // disconnect() only queues the request; the link is not down until the
    // peer answers, and connecting again before then fails outright. Waiting
    // here is what makes reconnecting from the setup list work without a
    // restart -- a fixed short delay is not enough for a real controller.
    client->disconnect();
    for (uint8_t waited = 0; waited < 25 && client->isConnected(); waited++) delay(20);
  }

  const NimBLEAddress address(device.address, device.addressType);
  if (!client->connect(address, true, false, false)) {
    setState(FARDRIVER_BLE_ERROR, "Connection failed");
    return;
  }

  setState(FARDRIVER_BLE_DISCOVERING, "Discovering notification service...");
  NimBLERemoteService *service = nullptr;
  NimBLERemoteCharacteristic *characteristic = findNotificationCharacteristic(client, &service);
  if (!service || !characteristic) {
    client->disconnect();
    setState(FARDRIVER_BLE_ERROR, "No notifying characteristic found");
    return;
  }

  // Reset before subscription: notifications may arrive as soon as the CCCD
  // write completes. Never reset the host task's buffer after it starts parsing.
  rxStream.reset();
  portENTER_CRITICAL(&dataMux);
  telemetry = {};
  telemetry.socPercent = -1;
  portEXIT_CRITICAL(&dataMux);
  portENTER_CRITICAL(&statusMux);
  status.frameCount = 0;
  status.crcFailCount = 0;
  status.discardedBytes = 0;
  status.lastPacketLength = 0;
  status.lastFrameId = 0;
  status.lastPacketHex[0] = '\0';
  portEXIT_CRITICAL(&statusMux);

  const bool subscribed = characteristic->canNotify()
                              ? characteristic->subscribe(true, notificationCallback)
                              : characteristic->subscribe(false, notificationCallback);
  if (!subscribed) {
    client->disconnect();
    setState(FARDRIVER_BLE_ERROR, "Notification subscription failed");
    return;
  }

  const std::string serviceUuid = service->getUUID().toString();
  const std::string characteristicUuid = characteristic->getUUID().toString();
  // getRssi() sends an HCI command and waits for the controller to answer.
  // portENTER_CRITICAL disables interrupts and takes a spinlock, so blocking
  // inside it starves the very interrupts the reply needs and panics the core.
  // Read it first and only copy the value under the lock.
  const int8_t connectedRssi = static_cast<int8_t>(client->getRssi());
  portENTER_CRITICAL(&statusMux);
  const bool identityChanged =
      !persistedIdentityValid || strncmp(status.savedAddress, device.address, sizeof(status.savedAddress)) != 0 ||
      persistedAddressType != device.addressType ||
      strncmp(persistedServiceUuid, serviceUuid.c_str(), sizeof(persistedServiceUuid)) != 0 ||
      strncmp(persistedCharacteristicUuid, characteristicUuid.c_str(), sizeof(persistedCharacteristicUuid)) != 0;
  copyText(status.connectedAddress, sizeof(status.connectedAddress), device.address);
  copyText(status.serviceUuid, sizeof(status.serviceUuid), serviceUuid.c_str());
  copyText(status.characteristicUuid, sizeof(status.characteristicUuid), characteristicUuid.c_str());
  status.connectedRssi = connectedRssi;
  status.savedDevice = true;
  copyText(status.savedAddress, sizeof(status.savedAddress), device.address);
  portEXIT_CRITICAL(&statusMux);

  if (identityChanged && preferences.begin("fardriver", false)) {
    const bool savedAddress = preferences.putString("addr", device.address) == strlen(device.address);
    const bool savedType = preferences.putUChar("addrType", device.addressType) == sizeof(device.addressType);
    const bool savedService = preferences.putString("service", serviceUuid.c_str()) == serviceUuid.length();
    const bool savedCharacteristic =
        preferences.putString("char", characteristicUuid.c_str()) == characteristicUuid.length();
    preferences.end();
    if (savedAddress && savedType && savedService && savedCharacteristic) {
      portENTER_CRITICAL(&statusMux);
      persistedAddressType = device.addressType;
      copyText(persistedServiceUuid, sizeof(persistedServiceUuid), serviceUuid.c_str());
      copyText(persistedCharacteristicUuid, sizeof(persistedCharacteristicUuid), characteristicUuid.c_str());
      persistedIdentityValid = true;
      portEXIT_CRITICAL(&statusMux);
    }
  }
  setState(FARDRIVER_BLE_CONNECTED, "Connected - waiting for notifications");
}

static void connectDevice(uint8_t deviceIndex) {
  FarDriverBleDeviceInfo device = {};
  portENTER_CRITICAL(&statusMux);
  if (deviceIndex < status.deviceCount) device = status.devices[deviceIndex];
  portEXIT_CRITICAL(&statusMux);
  if (device.address[0] == '\0') {
    setState(FARDRIVER_BLE_ERROR, "Selected BLE device is no longer available");
    return;
  }
  connectTo(device);
}

// The saved identity, not a list slot: a scan replaces the list, so slot 0 is
// only the saved controller until the first scan after boot.
static void connectSaved() {
  // A retry queued while the link was coming up must not tear it down again.
  if (client && client->isConnected()) return;
  FarDriverBleDeviceInfo device = {};
  portENTER_CRITICAL(&statusMux);
  const bool saved = status.savedDevice && status.savedAddress[0] != '\0';
  if (saved) {
    copyText(device.address, sizeof(device.address), status.savedAddress);
    device.addressType = persistedAddressType;
  }
  portEXIT_CRITICAL(&statusMux);
  if (saved) connectTo(device);
}

static void disconnectDevice() {
  if (nimbleInitialized) NimBLEDevice::getScan()->stop();
  if (client && client->isConnected()) {
    client->disconnect();
  } else {
    setState(FARDRIVER_BLE_IDLE, "Disconnected");
  }
}

// Runs on the worker task only. Deleting the client from another task could
// free it while a connect is still using it, which is why this is a queued
// command rather than a direct call.
static void releaseRadio() {
  disconnectDevice();
  if (client) {
    NimBLEDevice::deleteClient(client);  // disconnects first if still connected
    client = nullptr;
  }
  radioReleased = true;
  releasePending = false;
  setState(FARDRIVER_BLE_IDLE, "Bluetooth radio released");
}

static void forgetDevice() {
  disconnectDevice();
  portENTER_CRITICAL(&statusMux);
  const bool identityWasPersisted = persistedIdentityValid || status.savedDevice;
  status.savedDevice = false;
  status.savedAddress[0] = '\0';
  persistedAddressType = 0;
  persistedServiceUuid[0] = '\0';
  persistedCharacteristicUuid[0] = '\0';
  persistedIdentityValid = false;
  status.linkRevision++;
  status.revision++;
  portEXIT_CRITICAL(&statusMux);
  if (identityWasPersisted && preferences.begin("fardriver", false)) {
    preferences.clear();
    preferences.end();
  }
}

static void workerTask(void *) {
  Command command = {};
  for (;;) {
    if (xQueueReceive(commandQueue, &command, portMAX_DELAY) != pdTRUE) continue;
    switch (command.type) {
      case COMMAND_SCAN:
        startScan();
        break;
      case COMMAND_CONNECT:
        connectDevice(command.deviceIndex);
        break;
      case COMMAND_CONNECT_SAVED:
        connectSaved();
        break;
      case COMMAND_DISCONNECT:
        disconnectDevice();
        break;
      case COMMAND_FORGET:
        forgetDevice();
        break;
      case COMMAND_RELEASE:
        releaseRadio();
        break;
    }
  }
}

static void queueCommand(CommandType type, uint8_t deviceIndex = 0) {
  farDriverBleBegin();
  if (!commandQueue) return;
  const Command command = {type, deviceIndex};
  if (xQueueSend(commandQueue, &command, 0) != pdTRUE)
    setState(FARDRIVER_BLE_ERROR, "BLE command queue is busy");
}

}  // namespace

void farDriverBleBegin() {
  if (commandQueue) return;
  memset(&status, 0, sizeof(status));
  status.state = FARDRIVER_BLE_IDLE;
  copyText(status.message, sizeof(status.message), "Ready to scan");
  preferences.begin("fardriver", true);
  const String savedAddress = preferences.getString("addr", "");
  const uint8_t savedAddressType = preferences.getUChar("addrType", 0);
  const String savedService = preferences.getString("service", "");
  const String savedCharacteristic = preferences.getString("char", "");
  preferences.end();
  if (!savedAddress.isEmpty()) {
    status.savedDevice = true;
    copyText(status.savedAddress, sizeof(status.savedAddress), savedAddress.c_str());
    persistedAddressType = savedAddressType;
    copyText(persistedServiceUuid, sizeof(persistedServiceUuid), savedService.c_str());
    copyText(persistedCharacteristicUuid, sizeof(persistedCharacteristicUuid), savedCharacteristic.c_str());
    persistedIdentityValid = !savedService.isEmpty() && !savedCharacteristic.isEmpty();
    // Slot zero doubles as the reconnect target. A normal scan replaces this
    // list, but boot can connect directly without spending five seconds
    // rediscovering an address the user already paired.
    status.deviceCount = 1;
    copyText(status.devices[0].name, sizeof(status.devices[0].name), "Saved FarDriver");
    copyText(status.devices[0].address, sizeof(status.devices[0].address), savedAddress.c_str());
    status.devices[0].addressType = savedAddressType;
    status.devices[0].likelyFarDriver = true;
  }
  status.linkRevision = 1;
  status.revision = 1;
  commandQueue = xQueueCreate(4, sizeof(Command));
  if (!commandQueue) {
    setState(FARDRIVER_BLE_ERROR, "Unable to create BLE command queue");
    return;
  }
  const BaseType_t created =
      xTaskCreatePinnedToCore(workerTask, "fardriver-ble", 4096, nullptr, 0, &workerTaskHandle, 0);
  if (created != pdPASS) {
    vQueueDelete(commandQueue);
    commandQueue = nullptr;
    setState(FARDRIVER_BLE_ERROR, "Could not start BLE worker");
    return;
  }
  if (status.savedDevice && controllerUsesFarDriverBle()) {
    const Command reconnect = {COMMAND_CONNECT, 0};
    xQueueSend(commandQueue, &reconnect, 0);
  }
}

void farDriverBleStartScan() { queueCommand(COMMAND_SCAN); }

void farDriverBleConnect(uint8_t deviceIndex) {
  portENTER_CRITICAL(&statusMux);
  if (deviceIndex < status.deviceCount) {
    status.state = FARDRIVER_BLE_CONNECTING;
    copyText(status.message, sizeof(status.message), "Connection queued...");
    status.linkRevision++;
    status.revision++;
  }
  portEXIT_CRITICAL(&statusMux);
  queueCommand(COMMAND_CONNECT, deviceIndex);
}

void farDriverBleReconnectSaved() { queueCommand(COMMAND_CONNECT_SAVED); }

void farDriverBleDisconnect() { queueCommand(COMMAND_DISCONNECT); }

void farDriverBleReleaseRadio() {
  // Idempotent: repeated calls while the worker is still working queue nothing.
  // Not gated on radioReleased, because a release also stops a running scan --
  // there is work to do even when no client was ever created.
  if (releasePending || !commandQueue) return;
  releasePending = true;
  queueCommand(COMMAND_RELEASE);
}

bool farDriverBleRadioReleased() { return radioReleased && !releasePending; }

void farDriverBleForget() { queueCommand(COMMAND_FORGET); }

void farDriverBleResetSettings() {
  // Reset persistence immediately even when this controller backend was not
  // started during the current boot.
  Preferences resetPreferences;
  resetPreferences.begin("fardriver", false);
  resetPreferences.clear();
  resetPreferences.end();
  portENTER_CRITICAL(&statusMux);
  status.savedDevice = false;
  status.savedAddress[0] = '\0';
  persistedAddressType = 0;
  persistedServiceUuid[0] = '\0';
  persistedCharacteristicUuid[0] = '\0';
  persistedIdentityValid = false;
  status.linkRevision++;
  status.revision++;
  portEXIT_CRITICAL(&statusMux);

  if (commandQueue) {
    // The reset invalidates any queued discovery/connect intent. FORGET runs
    // after a command already in progress and guarantees its result is not
    // saved again after the synchronous clear above.
    xQueueReset(commandQueue);
    const Command command = {COMMAND_FORGET, 0};
    if (xQueueSendToFront(commandQueue, &command, pdMS_TO_TICKS(100)) != pdTRUE)
      setState(FARDRIVER_BLE_ERROR, "Could not reset FarDriver BLE connection");
  }
}

FarDriverTelemetry farDriverTelemetry() {
  FarDriverTelemetry snapshot;
  portENTER_CRITICAL(&dataMux);
  snapshot = telemetry;
  portEXIT_CRITICAL(&dataMux);
  return snapshot;
}

FarDriverBleStatus farDriverBleStatus() {
  FarDriverBleStatus snapshot;
  portENTER_CRITICAL(&statusMux);
  snapshot = status;
  portEXIT_CRITICAL(&statusMux);
  return snapshot;
}
