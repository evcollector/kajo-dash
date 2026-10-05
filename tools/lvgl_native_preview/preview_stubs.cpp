#include "app_state.h"
#include "fardriver_ble.h"
#include "firmware_update_ble.h"
#include "companion_ble.h"
#include "Preferences.h"
#include "vesc_ble.h"
#include "screens.h"
#include "ride_replay_core.h"
#include <algorithm>
#include <cmath>
#include <cstring>

static uint32_t previewMillis = 3200;
static char previewDisplayModuleInfo[56] = "ESP32 rev 1 | 4 MB flash";

extern "C" uint32_t millis(void) {
  return previewMillis;
}

void previewSetMillis(uint32_t value) {
  previewMillis = value;
}

uint32_t previewMillisNow() {
  return previewMillis;
}

void applyDisplayBrightness() {}
void applyDisplayPanelProfile() {}
void applyDisplayPanelTuning() {}
void saveDisplayPanelTuning() {}
void loadDisplayPanelProfile() {}
void saveDisplayPanelProfile() {}
void autoDetectDisplayPanelProfile(bool) {}
void previewSetDisplayModuleInfo(const char *info) {
  if (!info || !info[0]) return;
  snprintf(previewDisplayModuleInfo, sizeof(previewDisplayModuleInfo), "%s", info);
}
void displayModuleInfo(char *buffer, size_t size) { snprintf(buffer, size, "%s", previewDisplayModuleInfo); }
void displayControllerInfo(char *buffer, size_t size) { snprintf(buffer, size, "D3 00009341 | 04 00000000"); }
static int previewLightRaw = 420;
static int previewLightTarget = 72;
int lightSensorRaw() { return previewLightRaw; }
int lightSensorTargetPct() { return previewLightTarget; }
void previewSetLightSensor(int raw, int targetPercent) {
  previewLightRaw = constrain(raw, 0, 4095);
  previewLightTarget = constrain(targetPercent, 0, 100);
}
bool runTouchCalibration() { return true; }
bool runRecoveryCalibration() { return true; }
// The host preview has no panel to hold and drives every screen directly, so
// the recovery gesture must never arm here.
bool recoveryInputHeld() { return false; }
void touchInputArmAfterRecovery() {}
void requestControllerRestart() {}

static FirmwareUpdateBleStatus previewFirmwareUpdate = {
    FIRMWARE_UPDATE_BLE_LOCKED, false, false, false, 0, 0, 0, 0, 0, 0, 1,
    "Release signing key not configured"};
// Mirrors updateModeActive in firmware_update_ble.cpp: raised by a successful
// start, cleared by stop. The Bluetooth Link screen watches this to hand the
// display over to the update screen when the phone asks for update mode.
static bool previewFirmwareUpdateActive = false;
bool firmwareUpdateBleStart() {
  if (!previewFirmwareUpdate.keyConfigured || previewFirmwareUpdate.state != FIRMWARE_UPDATE_BLE_READY) return false;
  previewFirmwareUpdate.state = FIRMWARE_UPDATE_BLE_PREPARING;
  snprintf(previewFirmwareUpdate.message, sizeof(previewFirmwareUpdate.message), "%s",
           "Stopping logging and controller Bluetooth");
  previewFirmwareUpdate.revision++;
  previewFirmwareUpdateActive = true;
  return true;
}
void firmwareUpdateBleStop() { previewFirmwareUpdateActive = false; }
void firmwareUpdateBleService() {}
bool firmwareUpdateBleConfirmDowngrade() { return true; }
bool firmwareUpdateBleCancelFromDisplay() { return true; }
void firmwareUpdateBleRequestReboot() {}
FirmwareUpdateBleStatus firmwareUpdateBleStatus() { return previewFirmwareUpdate; }
bool firmwareUpdateBleActive() { return previewFirmwareUpdateActive; }
// On hardware the phone raises this over the companion link, which makes
// companionBleService() start update mode. Nothing here runs that service, so
// tests set the flag directly to stand in for the phone's request.
void previewSetFirmwareUpdateActive(bool active) { previewFirmwareUpdateActive = active; }
void previewSetFirmwareUpdateState(FirmwareUpdateBleState state, bool configured, uint32_t received,
                                   uint32_t total, const char *message) {
  previewFirmwareUpdate.state = state;
  previewFirmwareUpdate.keyConfigured = configured;
  previewFirmwareUpdate.clientConnected = state == FIRMWARE_UPDATE_BLE_CONNECTED ||
                                          state == FIRMWARE_UPDATE_BLE_CONFIRM_DOWNGRADE ||
                                          state == FIRMWARE_UPDATE_BLE_RECEIVING;
  previewFirmwareUpdate.manifestAccepted = state == FIRMWARE_UPDATE_BLE_RECEIVING ||
                                           state == FIRMWARE_UPDATE_BLE_READY_TO_REBOOT;
  previewFirmwareUpdate.expectedOffset = received;
  previewFirmwareUpdate.totalBytes = total;
  previewFirmwareUpdate.imageBytes = configured ? 1599553 : 0;
  previewFirmwareUpdate.transferBytesPerSecond =
      state == FIRMWARE_UPDATE_BLE_RECEIVING || state == FIRMWARE_UPDATE_BLE_READY_TO_REBOOT ? 5325 : 0;
  previewFirmwareUpdate.versionCode = configured ? 5 : 0;
  previewFirmwareUpdate.lastError = 0;
  snprintf(previewFirmwareUpdate.message, sizeof(previewFirmwareUpdate.message), "%s", message);
  previewFirmwareUpdate.revision++;
}

static CompanionBleStatus previewCompanion = {COMPANION_BLE_OFF, false, true, 0, 1, "Companion mode off"};
bool companionBleStart() { return true; }
void companionBleStop() {}
void companionBleService() {}
bool companionBleActive() { return previewCompanion.state != COMPANION_BLE_OFF; }
CompanionBleStatus companionBleStatus() { return previewCompanion; }
void previewSetCompanionState(CompanionBleState state, bool paused, uint32_t seconds, const char *message) {
  previewCompanion.state = state;
  previewCompanion.controllerPaused = paused;
  previewCompanion.secondsRemaining = seconds;
  previewCompanion.revision++;
  snprintf(previewCompanion.message, sizeof(previewCompanion.message), "%s", message);
}

void farDriverBleBegin() {}
void farDriverBleStartScan() {}
void farDriverBleConnect(uint8_t) {}
void farDriverBleReconnectSaved() {}
void farDriverBleDisconnect() {}
void farDriverBleForget() {}
void farDriverBleReleaseRadio() {}
bool farDriverBleRadioReleased() { return true; }
void farDriverBleResetSettings() {
  Preferences preferences;
  preferences.begin("fardriver", false);
  preferences.clear();
  preferences.end();
}
FarDriverTelemetry farDriverTelemetry() {
  FarDriverTelemetry telemetry = {};
  return telemetry;
}

static uint32_t previewFarDriverRevision = 1;
static uint32_t previewFarDriverLinkRevision = 1;
static bool previewFarDriverConnected = true;
void previewFarDriverReceivePacket() { previewFarDriverRevision++; }
void previewSetFarDriverConnected(bool connected) {
  previewFarDriverConnected = connected;
  previewFarDriverLinkRevision++;
  previewFarDriverRevision++;
}

FarDriverBleStatus farDriverBleStatus() {
  FarDriverBleStatus status = {};
  status.state = previewFarDriverConnected ? FARDRIVER_BLE_CONNECTED : FARDRIVER_BLE_IDLE;
  status.deviceCount = 1;
  snprintf(status.devices[0].name, sizeof(status.devices[0].name), "FarDriver TEST");
  snprintf(status.devices[0].address, sizeof(status.devices[0].address), "A4:C1:38:72:19:EF");
  status.devices[0].rssi = -48;
  status.devices[0].likelyFarDriver = true;
  status.savedDevice = true;
  snprintf(status.savedAddress, sizeof(status.savedAddress), "A4:C1:38:72:19:EF");
  snprintf(status.connectedAddress, sizeof(status.connectedAddress), "A4:C1:38:72:19:EF");
  snprintf(status.serviceUuid, sizeof(status.serviceUuid), "0000ffe0-0000-1000-8000-00805f9b34fb");
  snprintf(status.characteristicUuid, sizeof(status.characteristicUuid), "0000ffec-0000-1000-8000-00805f9b34fb");
  snprintf(status.lastPacketHex, sizeof(status.lastPacketHex), "AA 82 03 00 00 00 00 00 7D 00 00 00 00 00 D9 1D");
  snprintf(status.message, sizeof(status.message), "Connected - notifications active");
  status.frameCount = 1841 + previewFarDriverRevision;
  status.crcFailCount = 0;
  status.discardedBytes = 0;
  status.lastPacketLength = 16;
  status.lastFrameId = 0x03;
  status.connectedRssi = -48;
  status.linkRevision = previewFarDriverLinkRevision;
  status.revision = previewFarDriverRevision;
  return status;
}

VescBleStream vescBleStream;
void VescBleStream::begin() {}
void VescBleStream::startScan() {}
void VescBleStream::connectDevice(uint8_t) {}
void VescBleStream::reconnectSaved() {}
void VescBleStream::disconnectDevice() {}
void VescBleStream::releaseRadio() {}
bool VescBleStream::radioReleased() const { return true; }
void VescBleStream::forgetDevice() {}
void VescBleStream::resetSettings() {
  Preferences preferences;
  preferences.begin("vescBle", false);
  preferences.clear();
  preferences.end();
}
VescBleStatus VescBleStream::status() {
  VescBleStatus status = {};
  status.state = VESC_BLE_IDLE;
  status.deviceCount = 2;
  snprintf(status.message, sizeof(status.message), "Select a Bluetooth device");
  snprintf(status.devices[0].name, sizeof(status.devices[0].name), "VESC Express");
  snprintf(status.devices[0].address, sizeof(status.devices[0].address), "C4:7C:8D:21:40:9A");
  status.devices[0].rssi = -43;
  status.devices[0].likelyVesc = true;
  snprintf(status.devices[1].name, sizeof(status.devices[1].name), "VESC BLE UART");
  snprintf(status.devices[1].address, sizeof(status.devices[1].address), "A8:03:2A:77:19:EF");
  status.devices[1].rssi = -61;
  status.devices[1].likelyVesc = true;
  status.deviceCount = 6;
  snprintf(status.devices[2].name, sizeof(status.devices[2].name), "Living Room TV");
  snprintf(status.devices[2].address, sizeof(status.devices[2].address), "80:47:86:12:33:7A");
  status.devices[2].rssi = -52;
  snprintf(status.devices[3].name, sizeof(status.devices[3].name), "Portable Speaker");
  snprintf(status.devices[3].address, sizeof(status.devices[3].address), "F0:5C:77:09:DE:11");
  status.devices[3].rssi = -67;
  snprintf(status.devices[4].name, sizeof(status.devices[4].name), "Unnamed BLE device");
  snprintf(status.devices[4].address, sizeof(status.devices[4].address), "35:E1:4A:66:C2:08");
  status.devices[4].rssi = -75;
  snprintf(status.devices[5].name, sizeof(status.devices[5].name), "Smart Sensor");
  snprintf(status.devices[5].address, sizeof(status.devices[5].address), "B2:3D:91:A0:08:42");
  status.devices[5].rssi = -82;
  status.revision = 1;
  return status;
}
int VescBleStream::available() { return 0; }
int VescBleStream::read() { return -1; }
int VescBleStream::peek() { return -1; }
void VescBleStream::flush() {}
size_t VescBleStream::write(uint8_t) { return 1; }
size_t VescBleStream::write(const uint8_t *, size_t size) { return size; }

// Captures show logging switched on; the firmware itself boots with it off.
static RideLoggingStatus previewLogging = {RIDE_LOG_ON, true, false, false, 5, 12, 8, 0, 0, 1,
                                           7340032, 15931539456ULL};
void rideLoggerBegin() {}
void rideLoggerSample(const DashboardValues &, const BatteryStats &, uint8_t, uint32_t) {}
RideLoggingStatus rideLoggerStatus() { return previewLogging; }
void rideLoggerToggleEnabled() {
  previewLogging.mode = previewLogging.mode == RIDE_LOG_OFF ? RIDE_LOG_ON : RIDE_LOG_OFF;
}
void rideLoggerCycleRate() {
  previewLogging.sampleHz = previewLogging.sampleHz == 1 ? 2 : previewLogging.sampleHz == 2 ? 5 :
                                previewLogging.sampleHz == 5 ? 10 : 1;
}
void rideLoggerSetSuspended(bool) {}
void rideLoggerSetStorageNeeded(bool) {}
void rideLoggerResetSettings(bool deleteRideLogs) {
  previewLogging.mode = RIDE_LOG_OFF;
  previewLogging.sampleHz = 5;
  previewLogging.recording = false;
  Preferences preferences;
  preferences.begin("logger", false);
  preferences.putUChar("mode", (uint8_t)RIDE_LOG_OFF);
  preferences.putUChar("rate", 5);
  if (deleteRideLogs) preferences.putUInt("nextRide", 1);
  preferences.end();
  if (deleteRideLogs) rideLoggerDeleteAll();
}
static const RideLogSummary kPreviewRideLogs[] = {
    {12, 2843, 21800, 3275, 184, 54720, 5, true, false},
    {11, 1964, 14700, 2310, 92, 38210, 5, true, true},  // recorded in demo mode
    {10, 4120, 32600, 4984, 211, 78140, 10, true, false},
    {9, 935, 6100, 1042, 35, 19420, 5, true, false},
    {8, 0, 0, 0, 0, 128, 5, false, false},
};
static RideLogSummary previewRideLogs[5];
static RideLogCatalogStatus previewCatalog = {false, 5, 1};
[[maybe_unused]] static const bool previewRideLogsSeeded = (memcpy(previewRideLogs, kPreviewRideLogs, sizeof(previewRideLogs)), true);
void previewRestoreRideLogs() {
  memcpy(previewRideLogs, kPreviewRideLogs, sizeof(previewRideLogs));
  previewCatalog.count=5;previewCatalog.revision++;previewLogging.rideCount=5;
}
bool rideLoggerDeleteRide(uint32_t rideId) {
  uint8_t kept = 0;
  for (uint8_t i = 0; i < previewCatalog.count; i++)
    if (previewRideLogs[i].rideId != rideId) previewRideLogs[kept++] = previewRideLogs[i];
  if (kept == previewCatalog.count) return true;
  previewCatalog.count = kept; previewCatalog.revision++; previewLogging.rideCount = kept;
  return true;
}
// The host has no card: a wipe finishes at once, as if it removed the listed
// rides and their folder. previewSetWipeStatus() pins a stage for captures.
static RideLogWipeStatus previewWipe = {RideLogWipeStatus::Idle, 0, 0};
static bool previewWipePinned = false;
bool rideLoggerWipeCard() {
  previewLogging.rideCount = 0;
  previewCatalog.count = 0;
  previewCatalog.revision++;
  if (!previewWipePinned) previewWipe = {RideLogWipeStatus::Done, 6, 6};
  return true;
}
RideLogWipeStatus rideLoggerWipeStatus() { return previewWipe; }
void previewSetWipeStatus(RideLogWipeStatus::State state, uint32_t removed, uint32_t total, bool pinned) {
  previewWipe = {state, removed, total};
  previewWipePinned = pinned;
}
void rideLoggerDeleteAll() {
  previewLogging.rideCount = 0;
  previewCatalog.count = 0;
  previewCatalog.revision++;
}
void rideLoggerRequestCatalog() { previewCatalog.revision++; }
RideLogCatalogStatus rideLoggerCatalogStatus() { return previewCatalog; }
bool rideLoggerCatalogEntry(uint8_t index, RideLogSummary &entry) {
  if (index >= previewCatalog.count) return false;
  entry = previewRideLogs[index];
  return true;
}
bool rideLoggerReadSeries(uint32_t, RideLogSeriesField, uint32_t, uint32_t, uint8_t maximumPoints,
                          RideLogSeriesPoint *points, uint8_t &pointCount) {
  pointCount = min<uint8_t>(maximumPoints, 12);
  for (uint8_t i = 0; i < pointCount; i++) points[i] = {static_cast<uint16_t>(i * 30), i * 20};
  return true;
}
// Generate the same CRC-protected V3 bytes read from an SD card. The native
// UI therefore exercises the production parser and seek path, not fake samples.
bool rideLoggerReadFileChunk(uint32_t rideId, uint32_t offset, uint8_t *out, size_t capacity, size_t &bytesRead, uint32_t &fileBytes) {
  bytesRead=0; fileBytes=0;
  if(!previewLogging.cardReady || rideId<9 || rideId>12) return false;
  constexpr uint32_t count=14216;
  constexpr uint32_t kHeader=ride_replay::kHeaderBytes, kRecord=ride_replay::kRecordBytes;
  fileBytes=kHeader+count*kRecord;
  if(offset>=fileBytes) return true;
  auto put16=[](uint8_t *p,uint16_t v){p[0]=v;p[1]=v>>8;};
  auto put32=[](uint8_t *p,uint32_t v){for(int i=0;i<4;i++)p[i]=v>>(i*8);};
  while(bytesRead<capacity && offset<fileBytes) {
    uint8_t block[kRecord]={}; unsigned length,within;
    if(offset<kHeader) {
      length=kHeader;within=offset;memcpy(block,"KAJL",4);
      put16(block+4,ride_replay::kLogVersion);put16(block+6,kHeader);put16(block+8,kRecord);
      block[10]=5;put32(block+12,rideId);
      put16(block+30,ride_replay::crc16(block,30));
    } else {
      uint32_t index=(offset-kHeader)/kRecord,time=index*200;
      float t=time/1000.0f;
      float speed=22+10*sinf(t/240)+5*sinf(t/63)+2*sinf(t/13);
      float power=180+140*sinf(t/240)+95*sinf(t/39)+35*sinf(t/7);
      float volts=41.0f-4.0f*sinf(t/1800)-0.18f*sinf(t/75);
      float progress=t/2843.0f;
      float amps=power/volts;
      // Phase current exceeds pack current the further the motor is from full
      // duty, so it peaks at low speed rather than at peak power.
      float phase=amps*(3.0f-2.0f*std::min(1.0f,std::max(0.0f,speed)/34.0f));
      float motor=31+24*progress+2*sinf(t/90), controller=29+13*progress+sinf(t/120);
      length=kRecord;within=(offset-kHeader)%kRecord;
      put32(block,time);put32(block+8,int32_t(power));put32(block+12,uint32_t(t*22/3.6f));
      put16(block+24,int16_t(speed*10));put16(block+26,uint16_t(volts*100));put16(block+28,int16_t(amps*10));
      put16(block+30,int16_t(phase*10));
      put16(block+32,int16_t(motor*10));put16(block+34,int16_t(controller*10));block[36]=uint8_t(92-47*progress);
      // Speed, power, voltage, pack and phase current, both temperatures, trip
      // and battery; voltage drops out for 20 s to exercise the gap rendering.
      put32(block+38,t>=1400 && t<1420 ? 0x100047B:0x100047F);
      put16(block+42,ride_replay::crc16(block,42));
    }
    size_t take=std::min(capacity-bytesRead,size_t(length-within));
    memcpy(out+bytesRead,block+within,take);bytesRead+=take;offset+=take;
  }
  return true;
}
void rideLoggerReleaseRead() {}
void previewSetLoggingState(RideLoggingMode mode, bool recording) {
  previewLogging.mode = mode;
  previewLogging.recording = recording;
}
void previewSetCardState(bool ready, bool checking) {
  previewLogging.cardReady = ready;
  previewLogging.cardChecking = checking;
  previewLogging.revision++;
}

// The renderer captures the link overlay by forcing this, since there is no
// controller to unplug on the host.
static TelemetryLink previewLink = LINK_LIVE;
static uint8_t previewFault = 0;
static bool previewInteractive = false;
static DashboardValues previewDashboardValues = {
    25, 1000, 52.0F, 19.2F, 44.0F, 42, 38, 12.5F, 1250, 22.5F, 600, 75};

void previewSetInteractiveMode(bool enabled) {
  previewInteractive = enabled;
}

void previewSetTelemetryLink(TelemetryLink state) {
  previewLink = state;
}

TelemetryLink telemetryLinkState() {
  return previewLink;
}

void previewSetFaultCode(uint8_t code) {
  previewFault = code;
}

void previewSetDashboardValues(int speedKmh, int watts, float voltage, float current,
                               int motorTemp, int escTemp, int batteryPercent) {
  previewDashboardValues.speedKmh = constrain(speedKmh, 0, 300);
  previewDashboardValues.watts = constrain(watts, -20000, 50000);
  previewDashboardValues.voltage = constrain(voltage, 0.0F, 200.0F);
  previewDashboardValues.current = constrain(current, -500.0F, 500.0F);
  previewDashboardValues.motorCurrent = previewDashboardValues.current * 2.3F;
  previewDashboardValues.motorTemp = constrain(motorTemp, -40, 250);
  previewDashboardValues.escTemp = constrain(escTemp, -40, 250);
  previewDashboardValues.batteryPercent = constrain(batteryPercent, 0, 100);
  previewDashboardValues.avgSpeedKmh = previewDashboardValues.speedKmh * 0.72F;
}

uint8_t telemetryFaultCode() {
  return previewLink == LINK_LIVE ? previewFault : 0;
}

// The renderer captures the controller-supplied-speed case, since that is the
// one the swapped setup field exists for.
// The preview fakes every other reading, so it fakes a reported gear too;
// otherwise the mode cell would render as a dash in every captured sheet.
uint8_t telemetryRideMode() {
  return previewLink == LINK_LIVE && controllerType == CONTROLLER_FARDRIVER ? 2 : 0;
}

bool telemetrySpeedFromController() {
  return previewLink == LINK_LIVE && controllerType != CONTROLLER_FARDRIVER;
}

// A representative version so the VESC page shows the detected-firmware case
// rather than the never-answered one.
bool telemetryFirmwareVersion(uint8_t &major, uint8_t &minor) {
  if (previewLink != LINK_LIVE) return false;
  major = 6;
  minor = 2;
  return true;
}

bool getLiveDashboardValues(DashboardValues &values) {
  if (dashboardDemoModeEnabled) {
    values = makeDummyValues();
    return true;
  }
  values = previewDashboardValues;
  if (previewInteractive) values.uptimeSeconds = previewMillis / 1000UL;
  return true;
}

// Representative numbers so the battery page renders with content; hardware
// accumulates these from the controller's counters.
BatteryStats getBatteryStats() {
  BatteryStats stats = {};
  stats.tripWh = previewInteractive ? previewDashboardValues.tripKm * 20.0F : 250.0F;
  stats.tripRegenWh = 15.0F;
  stats.tripKm = previewDashboardValues.tripKm;
  stats.tripWhPerKm = 20.0F;
  stats.lifetimeWh = 25000.0F;
  stats.lifetimeKm = 1250.0F;
  stats.lifetimeWhPerKm = 20.0F;
  stats.socPercent = previewDashboardValues.batteryPercent;
  stats.rangeKm = previewDashboardValues.batteryPercent / 2;
  stats.equivalentCycles = 12.4F;
  stats.packMilliOhm = 84.0F;
  stats.learnedCapacityAh = 18.4F;
  stats.learnedSamples = 3;
  return stats;
}
