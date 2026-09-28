#include "vesc_ble.h"

#include <ctype.h>
#include <NimBLEDevice.h>
#include <Preferences.h>

#include "controller_manager.h"

namespace {
constexpr char kService[] = "6e400001-b5a3-f393-e0a9-e50e24dcca9e";
constexpr char kRx[] = "6e400002-b5a3-f393-e0a9-e50e24dcca9e";
constexpr char kTx[] = "6e400003-b5a3-f393-e0a9-e50e24dcca9e";
constexpr size_t kRingSize = 512;
enum CommandType : uint8_t { CMD_SCAN, CMD_CONNECT, CMD_CONNECT_SAVED, CMD_DISCONNECT, CMD_FORGET, CMD_RELEASE };
struct Command { CommandType type; uint8_t index; };

VescBleStatus state = {};
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
QueueHandle_t queueHandle = nullptr;
NimBLEClient *client = nullptr;
// NimBLE is built with a single client slot, so the FarDriver backend cannot
// create its own until this one hands the slot back. Released means "no client
// object exists here", which is the state a fresh boot starts in.
volatile bool bleRadioReleased = true;
volatile bool bleReleasePending = false;
NimBLERemoteCharacteristic *rxCharacteristic = nullptr;
Preferences prefs;
uint8_t ring[kRingSize];
size_t ringHead = 0, ringTail = 0;
uint8_t persistedAddressType = 0;
bool persistedIdentityValid = false;

void copyText(char *dst, size_t size, const char *src) { snprintf(dst, size, "%s", src ? src : ""); }
bool containsIgnoreCase(const std::string &text, const char *needle) {
  std::string lowered = text;
  for (char &c : lowered) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
  return lowered.find(needle) != std::string::npos;
}
void setState(VescBleState next, const char *message) {
  portENTER_CRITICAL(&mux);
  state.state = next;
  copyText(state.message, sizeof(state.message), message);
  state.revision++;
  portEXIT_CRITICAL(&mux);
}

void notify(NimBLERemoteCharacteristic *, uint8_t *data, size_t length, bool) {
  portENTER_CRITICAL(&mux);
  for (size_t i = 0; i < length; i++) {
    const size_t next = (ringHead + 1) % kRingSize;
    if (next == ringTail) ringTail = (ringTail + 1) % kRingSize;
    ring[ringHead] = data[i];
    ringHead = next;
  }
  portEXIT_CRITICAL(&mux);
}

class ScanCallbacks : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice *device) override {
    const std::string address = device->getAddress().toString();
    const std::string advertisedName = device->haveName() ? device->getName() : std::string();
    const std::string name = advertisedName.empty() ? std::string("Unnamed BLE device") : advertisedName;
    const bool likely = device->isAdvertisingService(NimBLEUUID(kService)) || containsIgnoreCase(name, "vesc") ||
                        containsIgnoreCase(name, "express");
    portENTER_CRITICAL(&mux);
    uint8_t index = state.deviceCount;
    for (uint8_t i = 0; i < state.deviceCount; i++)
      if (!strncmp(state.devices[i].address, address.c_str(), sizeof(state.devices[i].address))) index = i;
    if (index < VESC_BLE_MAX_DEVICES) {
      char displayName[sizeof(state.devices[index].name)] = {};
      copyText(displayName, sizeof(displayName), name.c_str());
      const bool added = index == state.deviceCount;
      const bool identityChanged = !added &&
          (strncmp(state.devices[index].name, displayName, sizeof(state.devices[index].name)) ||
           state.devices[index].likelyVesc != likely);
      if (added) state.deviceCount++;
      copyText(state.devices[index].name, sizeof(state.devices[index].name), displayName);
      copyText(state.devices[index].address, sizeof(state.devices[index].address), address.c_str());
      state.devices[index].rssi = device->getRSSI();
      state.devices[index].addressType = device->getAddress().getType();
      state.devices[index].likelyVesc = likely;
      if (added || identityChanged) state.revision++;
    }
    portEXIT_CRITICAL(&mux);
  }
  void onScanEnd(const NimBLEScanResults &, int) override { setState(VESC_BLE_IDLE, "Select a Bluetooth device"); }
} scanCallbacks;

class ClientCallbacks : public NimBLEClientCallbacks {
  void onDisconnect(NimBLEClient *, int) override {
    rxCharacteristic = nullptr;
    setState(VESC_BLE_IDLE, "VESC Bluetooth disconnected");
  }
} clientCallbacks;

void initBle() {
  if (!NimBLEDevice::isInitialized()) NimBLEDevice::init("KAJO-Dash");
  NimBLEScan *scan = NimBLEDevice::getScan();
  scan->setScanCallbacks(&scanCallbacks, false);
  scan->setActiveScan(true);
  scan->setInterval(100);
  scan->setWindow(60);
}

void scan() {
  initBle();
  NimBLEScan *bleScan = NimBLEDevice::getScan();
  bleScan->stop(); bleScan->clearResults();
  portENTER_CRITICAL(&mux);
  memset(state.devices, 0, sizeof(state.devices)); state.deviceCount = 0;
  portEXIT_CRITICAL(&mux);
  setState(VESC_BLE_SCANNING, "Searching for VESC Bluetooth devices...");
  if (!bleScan->start(5000, false, true)) setState(VESC_BLE_ERROR, "Unable to start Bluetooth scan");
}

void connectTo(const VescBleDeviceInfo &device) {
  initBle();
  NimBLEDevice::getScan()->stop();
  setState(VESC_BLE_CONNECTING, "Connecting to VESC...");
  if (!client) {
    client = NimBLEDevice::createClient();
    // The FarDriver backend may still hold the single client slot.
    if (!client) { setState(VESC_BLE_ERROR, "Bluetooth radio busy; try again"); return; }
    bleRadioReleased = false;
    client->setClientCallbacks(&clientCallbacks, false);
    client->setConnectTimeout(5000);
  }
  if (client->isConnected()) client->disconnect();
  if (!client->connect(NimBLEAddress(device.address, device.addressType), true, false, false)) {
    setState(VESC_BLE_ERROR, "VESC Bluetooth connection failed"); return;
  }
  NimBLERemoteService *service = client->getService(NimBLEUUID(kService));
  NimBLERemoteCharacteristic *rx = service ? service->getCharacteristic(NimBLEUUID(kRx)) : nullptr;
  NimBLERemoteCharacteristic *tx = service ? service->getCharacteristic(NimBLEUUID(kTx)) : nullptr;
  if (!rx || !tx || !tx->subscribe(true, notify)) {
    client->disconnect(); setState(VESC_BLE_ERROR, "VESC UART service unavailable"); return;
  }
  rxCharacteristic = rx;
  portENTER_CRITICAL(&mux);
  const bool identityChanged = !persistedIdentityValid ||
                               strncmp(state.savedAddress, device.address, sizeof(state.savedAddress)) != 0 ||
                               persistedAddressType != device.addressType;
  copyText(state.connectedAddress, sizeof(state.connectedAddress), device.address);
  state.savedDevice = true; copyText(state.savedAddress, sizeof(state.savedAddress), device.address);
  ringHead = ringTail = 0;
  portEXIT_CRITICAL(&mux);
  if (identityChanged && prefs.begin("vescBle", false)) {
    const bool savedAddress = prefs.putString("addr", device.address) == strlen(device.address);
    const bool savedType = prefs.putUChar("addrType", device.addressType) == sizeof(device.addressType);
    prefs.end();
    if (savedAddress && savedType) {
      portENTER_CRITICAL(&mux);
      persistedAddressType = device.addressType;
      persistedIdentityValid = true;
      portEXIT_CRITICAL(&mux);
    }
  }
  setState(VESC_BLE_CONNECTED, "VESC Bluetooth connected");
}

void connectIndex(uint8_t index) {
  VescBleDeviceInfo device = {};
  portENTER_CRITICAL(&mux);
  if (index < state.deviceCount) device = state.devices[index];
  portEXIT_CRITICAL(&mux);
  if (!device.address[0]) { setState(VESC_BLE_ERROR, "Selected VESC is unavailable"); return; }
  connectTo(device);
}

// The saved identity, not a list slot: a scan replaces the list, so slot 0 is
// only the saved VESC until the first scan after boot.
void connectSaved() {
  // A retry queued while the link was coming up must not tear it down again.
  if (client && client->isConnected()) return;
  VescBleDeviceInfo device = {};
  portENTER_CRITICAL(&mux);
  const bool saved = state.savedDevice && state.savedAddress[0];
  if (saved) {
    copyText(device.address, sizeof(device.address), state.savedAddress);
    device.addressType = persistedAddressType;
  }
  portEXIT_CRITICAL(&mux);
  if (saved) connectTo(device);
}

void worker(void *) {
  Command cmd;
  for (;;) if (xQueueReceive(queueHandle, &cmd, portMAX_DELAY) == pdTRUE) {
    if (cmd.type == CMD_SCAN) scan();
    else if (cmd.type == CMD_CONNECT) connectIndex(cmd.index);
    else if (cmd.type == CMD_CONNECT_SAVED) connectSaved();
    else if (cmd.type == CMD_DISCONNECT) { if (client && client->isConnected()) client->disconnect(); }
    else if (cmd.type == CMD_RELEASE) {
      // Worker-only: deleting the client from another task could free it while
      // a connect is still using it.
      if (NimBLEDevice::isInitialized()) NimBLEDevice::getScan()->stop();
      if (client && client->isConnected()) client->disconnect();
      if (client) { NimBLEDevice::deleteClient(client); client = nullptr; }
      bleRadioReleased = true;
      bleReleasePending = false;
      setState(VESC_BLE_IDLE, "Bluetooth radio released");
    }
    else if (cmd.type == CMD_FORGET) {
      if (client && client->isConnected()) client->disconnect();
      portENTER_CRITICAL(&mux);
      const bool identityWasPersisted = persistedIdentityValid || state.savedDevice;
      state.savedDevice = false; state.savedAddress[0] = 0; persistedAddressType = 0;
      persistedIdentityValid = false; state.revision++;
      portEXIT_CRITICAL(&mux);
      if (identityWasPersisted && prefs.begin("vescBle", false)) { prefs.clear(); prefs.end(); }
    }
  }
}
void enqueue(CommandType type, uint8_t index = 0) {
  vescBleStream.begin();
  const Command cmd = {type, index};
  if (queueHandle) xQueueSend(queueHandle, &cmd, 0);
}
}  // namespace

VescBleStream vescBleStream;

void VescBleStream::begin() {
  if (queueHandle) return;
  memset(&state, 0, sizeof(state)); state.state = VESC_BLE_IDLE; copyText(state.message, sizeof(state.message), "Ready");
  prefs.begin("vescBle", true); String address = prefs.getString("addr", ""); uint8_t type = prefs.getUChar("addrType", 0); prefs.end();
  if (!address.isEmpty()) {
    state.savedDevice = true; copyText(state.savedAddress, sizeof(state.savedAddress), address.c_str());
    persistedAddressType = type; persistedIdentityValid = true;
    state.deviceCount = 1; copyText(state.devices[0].name, sizeof(state.devices[0].name), "Saved VESC");
    copyText(state.devices[0].address, sizeof(state.devices[0].address), address.c_str()); state.devices[0].addressType = type;
  }
  state.revision = 1; queueHandle = xQueueCreate(4, sizeof(Command));
  if (!queueHandle || xTaskCreatePinnedToCore(worker, "vesc-ble", 4096, nullptr, 1, nullptr, 0) != pdPASS) {
    setState(VESC_BLE_ERROR, "Could not start VESC BLE worker"); return;
  }
  if (state.savedDevice && controllerUsesVescBle()) { const Command cmd = {CMD_CONNECT, 0}; xQueueSend(queueHandle, &cmd, 0); }
}
void VescBleStream::startScan() { enqueue(CMD_SCAN); }
void VescBleStream::connectDevice(uint8_t index) { enqueue(CMD_CONNECT, index); }
void VescBleStream::reconnectSaved() { enqueue(CMD_CONNECT_SAVED); }
void VescBleStream::disconnectDevice() { enqueue(CMD_DISCONNECT); }
void VescBleStream::releaseRadio() {
  // Idempotent: repeated calls while the worker is still working queue nothing.
  // Not gated on bleRadioReleased, because a release also stops a running scan
  // -- there is work to do even when no client was ever created.
  if (bleReleasePending || !queueHandle) return;
  bleReleasePending = true;
  enqueue(CMD_RELEASE);
}
bool VescBleStream::radioReleased() const { return bleRadioReleased && !bleReleasePending; }
void VescBleStream::forgetDevice() { enqueue(CMD_FORGET); }
void VescBleStream::resetSettings() {
  // Clear persistence synchronously so reset semantics do not depend on the
  // BLE worker existing or finding room in its command queue.
  Preferences resetPreferences;
  resetPreferences.begin("vescBle", false);
  resetPreferences.clear();
  resetPreferences.end();
  portENTER_CRITICAL(&mux);
  state.savedDevice = false;
  state.savedAddress[0] = 0;
  persistedAddressType = 0;
  persistedIdentityValid = false;
  state.revision++;
  portEXIT_CRITICAL(&mux);

  if (queueHandle) {
    // A reset supersedes scans and connects. The worker may currently be
    // finishing one command, but FORGET will run immediately afterwards and
    // disconnect any link that command established.
    xQueueReset(queueHandle);
    const Command command = {CMD_FORGET, 0};
    if (xQueueSendToFront(queueHandle, &command, pdMS_TO_TICKS(100)) != pdTRUE)
      setState(VESC_BLE_ERROR, "Could not reset VESC BLE connection");
  }
}
VescBleStatus VescBleStream::status() { portENTER_CRITICAL(&mux); VescBleStatus out = state; portEXIT_CRITICAL(&mux); return out; }
int VescBleStream::available() { portENTER_CRITICAL(&mux); int n = ringHead >= ringTail ? ringHead - ringTail : kRingSize - ringTail + ringHead; portEXIT_CRITICAL(&mux); return n; }
int VescBleStream::peek() { portENTER_CRITICAL(&mux); int value = ringHead == ringTail ? -1 : ring[ringTail]; portEXIT_CRITICAL(&mux); return value; }
int VescBleStream::read() { portENTER_CRITICAL(&mux); int value = -1; if (ringHead != ringTail) { value = ring[ringTail]; ringTail = (ringTail + 1) % kRingSize; } portEXIT_CRITICAL(&mux); return value; }
void VescBleStream::flush() {}
size_t VescBleStream::write(uint8_t value) { return write(&value, 1); }
size_t VescBleStream::write(const uint8_t *buffer, size_t size) {
  if (!rxCharacteristic || !client || !client->isConnected()) return 0;
  size_t sent = 0;
  while (sent < size) { const size_t chunk = min<size_t>(20, size - sent); if (!rxCharacteristic->writeValue(buffer + sent, chunk, false)) break; sent += chunk; }
  return sent;
}
