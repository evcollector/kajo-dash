#include "firmware_update_ble.h"

#include <algorithm>

#include <NimBLEDevice.h>
#include <esp32/rom/miniz.h>
#include <mbedtls/sha256.h>

#include "controller_manager.h"
#include "companion_ble.h"
#include "firmware_update.h"
#include "firmware_update_auth.h"

namespace {

constexpr uint8_t COMMAND_BEGIN = 1;
constexpr uint8_t COMMAND_SIGNATURE = 2;
constexpr uint8_t COMMAND_COMMIT_MANIFEST = 3;
constexpr uint8_t COMMAND_FINISH = 4;
constexpr uint8_t COMMAND_ABORT = 5;
constexpr uint8_t COMMAND_REBOOT = 6;
constexpr uint8_t COMMAND_STATUS = 7;
constexpr uint8_t COMMAND_COMMIT_SECTOR = 8;
constexpr uint16_t kFirmwareUpdateMtu = 247;
constexpr uint16_t kFirmwareUpdateDataLength = 251;
constexpr size_t kMaximumControlPacketBytes = 64;
constexpr size_t kMaximumDataPacketBytes = kFirmwareUpdateMtu - 3;
constexpr size_t kFirmwarePayloadBytes = kMaximumDataPacketBytes - sizeof(uint32_t);
constexpr size_t kMaximumPacketBytes = kMaximumDataPacketBytes;
constexpr uint32_t kPrepareTimeoutMs = 8000;
constexpr uint32_t kTransferRateSampleMs = 500;
constexpr uint32_t kCancelRebootDelayMs = 1500;
constexpr uint32_t kVerifiedRebootDelayMs = 5000;
constexpr size_t kRawSectorBytes = 4096;
constexpr size_t kMaximumCompressedSectorBytes = kRawSectorBytes + 16;

enum PacketKind : uint8_t {
  PACKET_CONTROL,
  PACKET_DATA,
  PACKET_RESET_SECTOR,
  PACKET_CONFIRM_DOWNGRADE,
  PACKET_CANCEL_FROM_DISPLAY,
};

struct UpdatePacket {
  PacketKind kind;
  uint8_t length;
  uint8_t bytes[kMaximumPacketBytes];
};

FirmwareUpdateBleStatus sharedStatus = {};
portMUX_TYPE statusMux = portMUX_INITIALIZER_UNLOCKED;
QueueHandle_t packetQueue = nullptr;
SemaphoreHandle_t updateMutex = nullptr;
TaskHandle_t workerHandle = nullptr;
NimBLEServer *server = nullptr;
NimBLECharacteristic *statusCharacteristic = nullptr;
volatile bool updateModeActive = false;
volatile bool serverStarted = false;
uint32_t prepareStartedMs = 0;
uint32_t rebootAtMs = 0;
uint32_t transferRateSampleMs = 0;
uint32_t transferRateSampleBytes = 0;
FirmwareUpdateManifest pendingManifest = {};
uint8_t pendingManifestBytes[FIRMWARE_UPDATE_MANIFEST_BYTES] = {};
bool manifestByteReceived[FIRMWARE_UPDATE_MANIFEST_BYTES] = {};
uint8_t pendingSignature[FIRMWARE_UPDATE_SIGNATURE_BYTES] = {};
uint64_t signatureByteMask = 0;
uint8_t compressedSector[kMaximumCompressedSectorBytes] = {};
uint8_t rawSector[kRawSectorBytes] = {};
// tinfl_decompressor is roughly 11 KiB on ESP32. Keep it in static RAM;
// tinfl_decompress_mem_to_mem() would place this state on the update task's
// stack and reset the board on the first compressed sector.
tinfl_decompressor sectorInflater = {};
size_t compressedSectorBytes = 0;
uint32_t acceptedTransportBytes = 0;
mbedtls_sha256_context transportShaContext;
bool transportShaActive = false;
volatile bool cancelPending = false;

void writeU32Le(uint8_t *bytes, uint32_t value) {
  bytes[0] = static_cast<uint8_t>(value);
  bytes[1] = static_cast<uint8_t>(value >> 8);
  bytes[2] = static_cast<uint8_t>(value >> 16);
  bytes[3] = static_cast<uint8_t>(value >> 24);
}

uint32_t readU32Le(const uint8_t *bytes) {
  return static_cast<uint32_t>(bytes[0]) | static_cast<uint32_t>(bytes[1]) << 8 |
         static_cast<uint32_t>(bytes[2]) << 16 | static_cast<uint32_t>(bytes[3]) << 24;
}

uint16_t readU16Le(const uint8_t *bytes) {
  return static_cast<uint16_t>(bytes[0]) | static_cast<uint16_t>(bytes[1]) << 8;
}

void initializeStatus() {
  if (sharedStatus.revision != 0) return;
  sharedStatus.keyConfigured = firmwareUpdateSigningKeyConfigured();
  sharedStatus.state = sharedStatus.keyConfigured ? FIRMWARE_UPDATE_BLE_READY : FIRMWARE_UPDATE_BLE_LOCKED;
  snprintf(sharedStatus.message, sizeof(sharedStatus.message), "%s",
           sharedStatus.keyConfigured ? "Ready to enter update mode" : "Release signing key not configured");
  sharedStatus.revision = 1;
}

void publishStatus(FirmwareUpdateBleState state, const char *message, int32_t error = FIRMWARE_UPDATE_BLE_OK) {
  const uint32_t now = millis();
  portENTER_CRITICAL(&statusMux);
  initializeStatus();
  if (state == FIRMWARE_UPDATE_BLE_RECEIVING && pendingManifest.transportSize > 0) {
    if (transferRateSampleMs == 0 || acceptedTransportBytes < transferRateSampleBytes) {
      transferRateSampleMs = now;
      transferRateSampleBytes = acceptedTransportBytes;
      sharedStatus.transferBytesPerSecond = 0;
    } else {
      const uint32_t elapsedMs = now - transferRateSampleMs;
      const uint32_t receivedSinceSample = acceptedTransportBytes - transferRateSampleBytes;
      if (elapsedMs >= kTransferRateSampleMs && receivedSinceSample > 0) {
        const uint32_t instantaneousRate = static_cast<uint32_t>(
            (static_cast<uint64_t>(receivedSinceSample) * 1000 + elapsedMs / 2) / elapsedMs);
        sharedStatus.transferBytesPerSecond =
            sharedStatus.transferBytesPerSecond == 0
                ? instantaneousRate
                : (sharedStatus.transferBytesPerSecond * 3 + instantaneousRate + 2) / 4;
        transferRateSampleMs = now;
        transferRateSampleBytes = acceptedTransportBytes;
      }
    }
  }
  sharedStatus.state = state;
  sharedStatus.expectedOffset = acceptedTransportBytes;
  sharedStatus.totalBytes = pendingManifest.transportSize;
  sharedStatus.imageBytes = pendingManifest.imageSize;
  sharedStatus.lastError = error;
  snprintf(sharedStatus.message, sizeof(sharedStatus.message), "%s", message ? message : "");
  sharedStatus.revision++;
  if (sharedStatus.revision == 0) sharedStatus.revision = 1;
  portEXIT_CRITICAL(&statusMux);
}

void encodeStatus(uint8_t bytes[28]) {
  const FirmwareUpdateBleStatus status = firmwareUpdateBleStatus();
  bytes[0] = FIRMWARE_UPDATE_PROTOCOL_VERSION;
  bytes[1] = static_cast<uint8_t>(status.state);
  bytes[2] = (status.keyConfigured ? 0x01 : 0) | (status.clientConnected ? 0x02 : 0) |
             (status.manifestAccepted ? 0x04 : 0);
  bytes[3] = 0;
  writeU32Le(bytes + 4, status.expectedOffset);
  writeU32Le(bytes + 8, status.totalBytes);
  writeU32Le(bytes + 12, static_cast<uint32_t>(status.lastError));
  writeU32Le(bytes + 16, status.versionCode);
  writeU32Le(bytes + 20, status.imageBytes);
  writeU32Le(bytes + 24, status.revision);
}

void notifyStatus() {
  if (!statusCharacteristic) return;
  uint8_t bytes[28];
  encodeStatus(bytes);
  statusCharacteristic->setValue(bytes, sizeof(bytes));
  if (firmwareUpdateBleStatus().clientConnected) statusCharacteristic->notify();
}

void publishVerifiedAndScheduleReboot() {
  portENTER_CRITICAL(&statusMux);
  if (rebootAtMs == 0) rebootAtMs = millis() + kVerifiedRebootDelayMs;
  portEXIT_CRITICAL(&statusMux);
  publishStatus(FIRMWARE_UPDATE_BLE_READY_TO_REBOOT, "Firmware verified - restarting in 5 seconds");
}

void clearManifest() {
  if (transportShaActive) {
    mbedtls_sha256_free(&transportShaContext);
    transportShaActive = false;
  }
  memset(&pendingManifest, 0, sizeof(pendingManifest));
  memset(pendingManifestBytes, 0, sizeof(pendingManifestBytes));
  memset(manifestByteReceived, 0, sizeof(manifestByteReceived));
  memset(pendingSignature, 0, sizeof(pendingSignature));
  signatureByteMask = 0;
  compressedSectorBytes = 0;
  acceptedTransportBytes = 0;
  portENTER_CRITICAL(&statusMux);
  sharedStatus.manifestAccepted = false;
  sharedStatus.transferBytesPerSecond = 0;
  sharedStatus.versionCode = 0;
  sharedStatus.imageBytes = 0;
  transferRateSampleMs = 0;
  transferRateSampleBytes = 0;
  portEXIT_CRITICAL(&statusMux);
}

void handleBegin(const UpdatePacket &packet) {
  if (firmwareUpdateStatus().state == FIRMWARE_UPDATE_RECEIVING) {
    publishStatus(FIRMWARE_UPDATE_BLE_RECEIVING, "Transfer already active", FIRMWARE_UPDATE_ERR_BUSY);
    return;
  }
  if (packet.length < 3) {
    publishStatus(FIRMWARE_UPDATE_BLE_ERROR, "Invalid manifest fragment", FIRMWARE_MANIFEST_ERR_FORMAT);
    return;
  }
  const uint8_t offset = packet.bytes[1];
  const size_t fragmentLength = packet.length - 2;
  if (offset >= sizeof(pendingManifestBytes) || fragmentLength > sizeof(pendingManifestBytes) - offset) {
    publishStatus(FIRMWARE_UPDATE_BLE_ERROR, "Manifest fragment out of range", FIRMWARE_MANIFEST_ERR_FORMAT);
    return;
  }
  if (offset == 0) {
    clearManifest();
  }
  memcpy(pendingManifestBytes + offset, packet.bytes + 2, fragmentLength);
  memset(manifestByteReceived + offset, 1, fragmentLength);
  bool complete = true;
  for (bool received : manifestByteReceived) complete = complete && received;
  if (!complete) {
    publishStatus(FIRMWARE_UPDATE_BLE_CONNECTED, "Manifest fragment received");
    return;
  }
  if (!firmwareUpdateDecodeManifest(pendingManifestBytes, sizeof(pendingManifestBytes), pendingManifest)) {
    publishStatus(FIRMWARE_UPDATE_BLE_ERROR, "Invalid update manifest", FIRMWARE_MANIFEST_ERR_FORMAT);
    return;
  }
  portENTER_CRITICAL(&statusMux);
  sharedStatus.manifestAccepted = false;
  sharedStatus.versionCode = pendingManifest.versionCode;
  portEXIT_CRITICAL(&statusMux);
  publishStatus(FIRMWARE_UPDATE_BLE_CONNECTED, "Manifest received - awaiting signature");
}

void handleSignature(const UpdatePacket &packet) {
  if (packet.length < 3) {
    publishStatus(FIRMWARE_UPDATE_BLE_ERROR, "Invalid signature fragment", FIRMWARE_MANIFEST_ERR_FORMAT);
    return;
  }
  const uint8_t offset = packet.bytes[1];
  const size_t fragmentLength = packet.length - 2;
  if (offset >= sizeof(pendingSignature) || fragmentLength > sizeof(pendingSignature) - offset) {
    publishStatus(FIRMWARE_UPDATE_BLE_ERROR, "Signature fragment out of range", FIRMWARE_MANIFEST_ERR_FORMAT);
    return;
  }
  memcpy(pendingSignature + offset, packet.bytes + 2, fragmentLength);
  for (size_t i = 0; i < fragmentLength; i++) signatureByteMask |= UINT64_C(1) << (offset + i);
  publishStatus(FIRMWARE_UPDATE_BLE_CONNECTED, "Signature fragment received");
}

void acceptPendingManifest(bool allowDowngrade) {
  for (bool received : manifestByteReceived) {
    if (!received) {
      publishStatus(FIRMWARE_UPDATE_BLE_ERROR, "Firmware manifest incomplete", FIRMWARE_MANIFEST_ERR_FORMAT);
      return;
    }
  }
  if (signatureByteMask != UINT64_MAX) {
    publishStatus(FIRMWARE_UPDATE_BLE_ERROR, "Firmware signature incomplete",
                  FIRMWARE_UPDATE_BLE_ERR_SIGNATURE_INCOMPLETE);
    return;
  }
  const FirmwareManifestError verified =
      firmwareUpdateVerifyManifest(pendingManifest, pendingSignature, allowDowngrade);
  if (verified == FIRMWARE_MANIFEST_ERR_DOWNGRADE) {
    publishStatus(FIRMWARE_UPDATE_BLE_CONFIRM_DOWNGRADE,
                  "Signed older firmware - confirm downgrade on display", verified);
    return;
  }
  if (verified != FIRMWARE_MANIFEST_OK) {
    publishStatus(FIRMWARE_UPDATE_BLE_ERROR, "Firmware signature rejected", verified);
    return;
  }
  if (!firmwareUpdateBegin(pendingManifest.imageSize, pendingManifest.imageSha256)) {
    const FirmwareUpdateStatus core = firmwareUpdateStatus();
    publishStatus(FIRMWARE_UPDATE_BLE_ERROR, "Unable to open OTA partition", core.lastError);
    return;
  }
  mbedtls_sha256_init(&transportShaContext);
  transportShaActive = true;
  if (mbedtls_sha256_starts_ret(&transportShaContext, 0) != 0) {
    firmwareUpdateAbort();
    clearManifest();
    publishStatus(FIRMWARE_UPDATE_BLE_ERROR, "Unable to start transport verification",
                  FIRMWARE_UPDATE_BLE_ERR_TRANSPORT_HASH);
    return;
  }
  portENTER_CRITICAL(&statusMux);
  sharedStatus.manifestAccepted = true;
  portEXIT_CRITICAL(&statusMux);
  publishStatus(FIRMWARE_UPDATE_BLE_RECEIVING, "Signed manifest accepted - send firmware");
}

void handleCommitManifest() { acceptPendingManifest(false); }

void handleCommitSector(const UpdatePacket &packet) {
  if (packet.length != 11 || firmwareUpdateStatus().state != FIRMWARE_UPDATE_RECEIVING) {
    publishStatus(FIRMWARE_UPDATE_BLE_ERROR, "Invalid compressed sector command",
                  FIRMWARE_UPDATE_BLE_ERR_COMMAND);
    return;
  }
  const uint32_t offset = readU32Le(packet.bytes + 1);
  const uint16_t sectorLength = readU16Le(packet.bytes + 5);
  const uint32_t expectedCrc = readU32Le(packet.bytes + 7);
  if (offset != acceptedTransportBytes || sectorLength != compressedSectorBytes || sectorLength < 3 ||
      sectorLength > sizeof(compressedSector)) {
    compressedSectorBytes = 0;
    publishStatus(FIRMWARE_UPDATE_BLE_RECEIVING, "Compressed sector framing rejected",
                  FIRMWARE_UPDATE_ERR_OFFSET);
    return;
  }
  const uint32_t actualCrc = static_cast<uint32_t>(
      mz_crc32(MZ_CRC32_INIT, compressedSector, compressedSectorBytes));
  if (actualCrc != expectedCrc) {
    compressedSectorBytes = 0;
    publishStatus(FIRMWARE_UPDATE_BLE_RECEIVING, "Compressed sector CRC failed - retrying",
                  FIRMWARE_UPDATE_BLE_ERR_SECTOR_CRC);
    return;
  }
  const uint16_t compressedLength = readU16Le(compressedSector);
  if (compressedLength + 2 != compressedSectorBytes) {
    compressedSectorBytes = 0;
    publishStatus(FIRMWARE_UPDATE_BLE_RECEIVING, "Compressed sector length rejected",
                  FIRMWARE_UPDATE_BLE_ERR_COMPRESSION);
    return;
  }
  const FirmwareUpdateStatus core = firmwareUpdateStatus();
  const size_t expectedOutputLength = std::min<size_t>(kRawSectorBytes, core.totalBytes - core.receivedBytes);
  size_t inputLength = compressedLength;
  size_t outputLength = sizeof(rawSector);
  tinfl_init(&sectorInflater);
  const tinfl_status inflateResult = tinfl_decompress(
      &sectorInflater, compressedSector + 2, &inputLength, rawSector, rawSector, &outputLength,
      TINFL_FLAG_PARSE_ZLIB_HEADER | TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
  if (inflateResult != TINFL_STATUS_DONE || inputLength != compressedLength || outputLength == 0 ||
      outputLength != expectedOutputLength) {
    compressedSectorBytes = 0;
    publishStatus(FIRMWARE_UPDATE_BLE_RECEIVING, "Compressed sector could not be decoded",
                  FIRMWARE_UPDATE_BLE_ERR_COMPRESSION);
    return;
  }
  if (!firmwareUpdateWrite(core.receivedBytes, rawSector, outputLength)) {
    compressedSectorBytes = 0;
    publishStatus(FIRMWARE_UPDATE_BLE_ERROR, "Firmware sector write failed", firmwareUpdateStatus().lastError);
    return;
  }
  if (!transportShaActive ||
      mbedtls_sha256_update_ret(&transportShaContext, compressedSector, compressedSectorBytes) != 0) {
    compressedSectorBytes = 0;
    firmwareUpdateAbort();
    publishStatus(FIRMWARE_UPDATE_BLE_ERROR, "Transport verification failed",
                  FIRMWARE_UPDATE_BLE_ERR_TRANSPORT_HASH);
    return;
  }
  acceptedTransportBytes += compressedSectorBytes;
  compressedSectorBytes = 0;
  publishStatus(FIRMWARE_UPDATE_BLE_RECEIVING, "Receiving compressed signed firmware");
}

void handleControl(const UpdatePacket &packet) {
  if (packet.length == 0) return;
  switch (packet.bytes[0]) {
    case COMMAND_BEGIN:
      handleBegin(packet);
      break;
    case COMMAND_SIGNATURE:
      handleSignature(packet);
      break;
    case COMMAND_COMMIT_MANIFEST:
      handleCommitManifest();
      break;
    case COMMAND_COMMIT_SECTOR:
      handleCommitSector(packet);
      break;
    case COMMAND_FINISH: {
      const FirmwareUpdateStatus core = firmwareUpdateStatus();
      if (core.state == FIRMWARE_UPDATE_READY_TO_REBOOT) {
        publishVerifiedAndScheduleReboot();
      } else if (core.state != FIRMWARE_UPDATE_RECEIVING) {
        publishStatus(FIRMWARE_UPDATE_BLE_ERROR, "No firmware transfer to finish", FIRMWARE_UPDATE_BLE_ERR_COMMAND);
      } else if (compressedSectorBytes != 0 || acceptedTransportBytes != pendingManifest.transportSize ||
                 !transportShaActive) {
        publishStatus(FIRMWARE_UPDATE_BLE_RECEIVING, "Compressed transfer is incomplete",
                      FIRMWARE_UPDATE_ERR_INCOMPLETE);
      } else {
        uint8_t actualTransportDigest[32] = {};
        const int hashResult = mbedtls_sha256_finish_ret(&transportShaContext, actualTransportDigest);
        mbedtls_sha256_free(&transportShaContext);
        transportShaActive = false;
        if (hashResult != 0 || memcmp(actualTransportDigest, pendingManifest.transportSha256, 32) != 0) {
          firmwareUpdateAbort();
          publishStatus(FIRMWARE_UPDATE_BLE_ERROR, "Compressed transport hash rejected",
                        FIRMWARE_UPDATE_BLE_ERR_TRANSPORT_HASH);
        } else if (firmwareUpdateFinish()) {
          publishVerifiedAndScheduleReboot();
        } else {
          publishStatus(FIRMWARE_UPDATE_BLE_ERROR, "Firmware validation failed", firmwareUpdateStatus().lastError);
        }
      }
      break;
    }
    case COMMAND_ABORT:
      cancelPending = true;
      firmwareUpdateAbort();
      clearManifest();
      publishStatus(FIRMWARE_UPDATE_BLE_CANCELLED, "Upload cancelled by phone - restarting",
                    FIRMWARE_UPDATE_BLE_ERR_CANCELLED);
      rebootAtMs = millis() + kCancelRebootDelayMs;
      break;
    case COMMAND_REBOOT:
      firmwareUpdateBleRequestReboot();
      break;
    case COMMAND_STATUS:
      break;
    default:
      publishStatus(FIRMWARE_UPDATE_BLE_ERROR, "Unknown update command", FIRMWARE_UPDATE_BLE_ERR_COMMAND);
      break;
  }
  notifyStatus();
}

void handleData(const UpdatePacket &packet) {
  if (cancelPending) return;
  const FirmwareUpdateStatus beforeWrite = firmwareUpdateStatus();
  const size_t payloadLength = packet.length >= sizeof(uint32_t) ? packet.length - sizeof(uint32_t) : 0;
  const uint32_t offset = packet.length >= sizeof(uint32_t) ? readU32Le(packet.bytes) : UINT32_MAX;
  if (beforeWrite.state != FIRMWARE_UPDATE_RECEIVING || payloadLength == 0 ||
      payloadLength > kFirmwarePayloadBytes || offset != acceptedTransportBytes + compressedSectorBytes ||
      payloadLength > sizeof(compressedSector) - compressedSectorBytes ||
      payloadLength > pendingManifest.transportSize - offset) {
    compressedSectorBytes = 0;
    publishStatus(FIRMWARE_UPDATE_BLE_RECEIVING, "Compressed packet rejected - retry sector",
                  FIRMWARE_UPDATE_ERR_SIZE);
    notifyStatus();
    return;
  }
  memcpy(compressedSector + compressedSectorBytes, packet.bytes + 4, payloadLength);
  compressedSectorBytes += payloadLength;
}

void workerTask(void *) {
  UpdatePacket packet = {};
  for (;;) {
    if (xQueueReceive(packetQueue, &packet, portMAX_DELAY) != pdTRUE) continue;
    if (!updateModeActive || !updateMutex || xSemaphoreTake(updateMutex, portMAX_DELAY) != pdTRUE) continue;
    if (updateModeActive) {
      if (packet.kind == PACKET_RESET_SECTOR)
        compressedSectorBytes = 0;
      else if (packet.kind == PACKET_CONTROL)
        handleControl(packet);
      else if (packet.kind == PACKET_CONFIRM_DOWNGRADE) {
        acceptPendingManifest(true);
        // Physical confirmation is not a GATT control write, so it does not
        // pass through handleControl() and its final notifyStatus() call. Push
        // the transition explicitly instead of leaving the characteristic at
        // the cached CONFIRM_DOWNGRADE value.
        notifyStatus();
      } else if (packet.kind == PACKET_CANCEL_FROM_DISPLAY) {
        cancelPending = true;
        xQueueReset(packetQueue);
        firmwareUpdateAbort();
        clearManifest();
        publishStatus(FIRMWARE_UPDATE_BLE_CANCELLED, "Upload cancelled on display - restarting",
                      FIRMWARE_UPDATE_BLE_ERR_CANCELLED);
        notifyStatus();
        rebootAtMs = millis() + kCancelRebootDelayMs;
      } else
        handleData(packet);
    }
    xSemaphoreGive(updateMutex);
  }
}

void enqueuePacket(PacketKind kind, NimBLECharacteristic *characteristic) {
  if (!updateModeActive || cancelPending) return;
  const auto value = characteristic->getValue();
  const size_t maximumBytes = kind == PACKET_CONTROL ? kMaximumControlPacketBytes : kMaximumDataPacketBytes;
  if (!packetQueue || value.size() == 0 || value.size() > maximumBytes) {
    publishStatus(FIRMWARE_UPDATE_BLE_ERROR, "Update packet queue or size error", FIRMWARE_UPDATE_BLE_ERR_QUEUE);
    return;
  }
  UpdatePacket packet = {};
  packet.kind = kind;
  packet.length = static_cast<uint8_t>(value.size());
  memcpy(packet.bytes, value.data(), value.size());
  if (xQueueSend(packetQueue, &packet, 0) != pdTRUE)
    publishStatus(FIRMWARE_UPDATE_BLE_RECEIVING, "Uploader is sending too quickly", FIRMWARE_UPDATE_BLE_ERR_QUEUE);
}

class ControlCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic *characteristic, NimBLEConnInfo &) override {
    enqueuePacket(PACKET_CONTROL, characteristic);
  }
};

class DataCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic *characteristic, NimBLEConnInfo &) override {
    enqueuePacket(PACKET_DATA, characteristic);
  }
};

class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer *activeServer, NimBLEConnInfo &connection) override {
    if (!updateModeActive) {
      activeServer->disconnect(connection.getConnHandle());
      return;
    }
    activeServer->setDataLen(connection.getConnHandle(), kFirmwareUpdateDataLength);
    activeServer->updateConnParams(connection.getConnHandle(), 6, 12, 0, 200);
    portENTER_CRITICAL(&statusMux);
    sharedStatus.clientConnected = true;
    portEXIT_CRITICAL(&statusMux);
    const FirmwareUpdateStatus core = firmwareUpdateStatus();
    const FirmwareUpdateBleStatus ble = firmwareUpdateBleStatus();
    if (ble.state == FIRMWARE_UPDATE_BLE_CANCELLED)
      publishStatus(FIRMWARE_UPDATE_BLE_CANCELLED, "Upload cancelled - restarting",
                    FIRMWARE_UPDATE_BLE_ERR_CANCELLED);
    else if (ble.state == FIRMWARE_UPDATE_BLE_CONFIRM_DOWNGRADE)
      publishStatus(FIRMWARE_UPDATE_BLE_CONFIRM_DOWNGRADE,
                    "Signed older firmware - confirm downgrade on display", FIRMWARE_MANIFEST_ERR_DOWNGRADE);
    else if (core.state == FIRMWARE_UPDATE_READY_TO_REBOOT)
      publishStatus(FIRMWARE_UPDATE_BLE_READY_TO_REBOOT, "Firmware verified - restarting automatically");
    else
      publishStatus(core.state == FIRMWARE_UPDATE_RECEIVING ? FIRMWARE_UPDATE_BLE_RECEIVING
                                                            : FIRMWARE_UPDATE_BLE_CONNECTED,
                    "Uploader connected");
    notifyStatus();
  }

  void onDisconnect(NimBLEServer *, NimBLEConnInfo &, int) override {
    portENTER_CRITICAL(&statusMux);
    sharedStatus.clientConnected = false;
    portEXIT_CRITICAL(&statusMux);
    // Only committed sectors are resumable. Clear queued writes and serialize
    // partial-sector cleanup through the update worker.
    if (packetQueue) {
      xQueueReset(packetQueue);
      UpdatePacket resetPacket = {};
      resetPacket.kind = PACKET_RESET_SECTOR;
      xQueueSend(packetQueue, &resetPacket, 0);
    }
    if (!updateModeActive) return;
    const FirmwareUpdateStatus core = firmwareUpdateStatus();
    const FirmwareUpdateBleStatus ble = firmwareUpdateBleStatus();
    if (ble.state == FIRMWARE_UPDATE_BLE_CONFIRM_DOWNGRADE)
      publishStatus(FIRMWARE_UPDATE_BLE_CONFIRM_DOWNGRADE,
                    "Signed older firmware - confirm downgrade on display", FIRMWARE_MANIFEST_ERR_DOWNGRADE);
    else if (core.state == FIRMWARE_UPDATE_READY_TO_REBOOT)
      publishStatus(FIRMWARE_UPDATE_BLE_READY_TO_REBOOT, "Firmware verified - restarting automatically");
    else
      publishStatus(core.state == FIRMWARE_UPDATE_RECEIVING ? FIRMWARE_UPDATE_BLE_RECEIVING
                                                            : FIRMWARE_UPDATE_BLE_ADVERTISING,
                    core.state == FIRMWARE_UPDATE_RECEIVING ? "Uploader disconnected - transfer may resume"
                                                            : "Advertising for signed updater");
  }
};

ControlCallbacks controlCallbacks;
DataCallbacks dataCallbacks;
ServerCallbacks serverCallbacks;

bool startServer() {
  if (!NimBLEDevice::isInitialized()) NimBLEDevice::init("KAJO-Dash Firmware Update");
  NimBLEDevice::setDeviceName("KAJO-Dash Firmware Update");
  NimBLEDevice::setPower(3);
  NimBLEDevice::setMTU(kFirmwareUpdateMtu);

  if (!server) {
    server = NimBLEDevice::createServer();
    if (!server) return false;
    NimBLEService *service = server->createService(FIRMWARE_UPDATE_SERVICE_UUID);
    if (!service) return false;
    NimBLECharacteristic *control = service->createCharacteristic(
        FIRMWARE_UPDATE_CONTROL_UUID, NIMBLE_PROPERTY::WRITE, kMaximumControlPacketBytes);
    NimBLECharacteristic *data = service->createCharacteristic(
        FIRMWARE_UPDATE_DATA_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR, kMaximumDataPacketBytes);
    statusCharacteristic = service->createCharacteristic(
        FIRMWARE_UPDATE_STATUS_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY, 28);
    if (!control || !data || !statusCharacteristic) return false;
    control->setCallbacks(&controlCallbacks);
    data->setCallbacks(&dataCallbacks);
    if (!server->start()) return false;
  }
  server->setCallbacks(&serverCallbacks, false);
  server->advertiseOnDisconnect(true);

  NimBLEAdvertising *advertising = NimBLEDevice::getAdvertising();
  if (!advertising) return false;
  advertising->reset();
  advertising->setName("KAJO-Dash Firmware Update");
  advertising->addServiceUUID(FIRMWARE_UPDATE_SERVICE_UUID);
  advertising->enableScanResponse(true);
  if (!NimBLEDevice::startAdvertising()) return false;
  serverStarted = true;
  notifyStatus();
  return true;
}

}  // namespace

bool firmwareUpdateBleStart() {
  if (companionBleActive()) return false;
  portENTER_CRITICAL(&statusMux);
  initializeStatus();
  const bool configured = sharedStatus.keyConfigured;
  portEXIT_CRITICAL(&statusMux);
  if (!configured || updateModeActive) return false;

  if (!updateMutex) updateMutex = xSemaphoreCreateMutex();
  if (!updateMutex) {
    publishStatus(FIRMWARE_UPDATE_BLE_ERROR, "Could not create update lock", FIRMWARE_UPDATE_BLE_ERR_QUEUE);
    return false;
  }
  xSemaphoreTake(updateMutex, portMAX_DELAY);
  clearManifest();
  firmwareUpdateAbort();
  xSemaphoreGive(updateMutex);
  rideLoggerSetSuspended(true);
  batteryStatsCheckpoint();
  cancelPending = false;
  updateModeActive = true;
  prepareStartedMs = millis();
  publishStatus(FIRMWARE_UPDATE_BLE_PREPARING, "Stopping logging and controller Bluetooth");
  return true;
}

void firmwareUpdateBleStop() {
  updateModeActive = false;
  serverStarted = false;
  if (server) server->advertiseOnDisconnect(false);
  if (NimBLEDevice::isInitialized()) NimBLEDevice::stopAdvertising();
  if (server && server->getConnectedCount()) {
    const std::vector<uint16_t> peers = server->getPeerDevices();
    for (uint16_t peer : peers) server->disconnect(peer);
  }
  if (packetQueue) xQueueReset(packetQueue);
  if (updateMutex) xSemaphoreTake(updateMutex, portMAX_DELAY);
  firmwareUpdateAbort();
  clearManifest();
  if (updateMutex) xSemaphoreGive(updateMutex);
  rideLoggerSetSuspended(false);
  controllerManagerResume();
  portENTER_CRITICAL(&statusMux);
  rebootAtMs = 0;
  portEXIT_CRITICAL(&statusMux);
  cancelPending = false;
  publishStatus(firmwareUpdateSigningKeyConfigured() ? FIRMWARE_UPDATE_BLE_READY : FIRMWARE_UPDATE_BLE_LOCKED,
                firmwareUpdateSigningKeyConfigured() ? "Ready to enter update mode"
                                                     : "Release signing key not configured");
}

void firmwareUpdateBleService() {
  portENTER_CRITICAL(&statusMux);
  const uint32_t scheduledReboot = rebootAtMs;
  portEXIT_CRITICAL(&statusMux);
  if (scheduledReboot && static_cast<int32_t>(millis() - scheduledReboot) >= 0) {
    ESP.restart();
    return;
  }
  if (!updateModeActive || serverStarted) return;
  const bool controllerReleased = controllerQuiesceForFirmwareUpdate();
  if (rideLoggerStatus().recording || !controllerReleased) {
    if (millis() - prepareStartedMs >= kPrepareTimeoutMs) {
      updateModeActive = false;
      rideLoggerSetSuspended(false);
      controllerManagerResume();
      publishStatus(FIRMWARE_UPDATE_BLE_ERROR, "Could not release Bluetooth or ride logger",
                    FIRMWARE_UPDATE_BLE_ERR_PREPARE_TIMEOUT);
    }
    return;
  }
  if (!packetQueue) packetQueue = xQueueCreate(32, sizeof(UpdatePacket));
  if (!packetQueue || (!workerHandle && xTaskCreatePinnedToCore(workerTask, "firmware-update", 6144, nullptr, 1,
                                                               &workerHandle, 0) != pdPASS)) {
    updateModeActive = false;
    rideLoggerSetSuspended(false);
    controllerManagerResume();
    publishStatus(FIRMWARE_UPDATE_BLE_ERROR, "Could not start update worker", FIRMWARE_UPDATE_BLE_ERR_QUEUE);
    return;
  }
  if (!startServer()) {
    updateModeActive = false;
    if (server) server->advertiseOnDisconnect(false);
    if (NimBLEDevice::isInitialized()) NimBLEDevice::stopAdvertising();
    rideLoggerSetSuspended(false);
    controllerManagerResume();
    publishStatus(FIRMWARE_UPDATE_BLE_ERROR, "Could not start update Bluetooth service",
                  FIRMWARE_UPDATE_BLE_ERR_SERVER);
    return;
  }
  publishStatus(FIRMWARE_UPDATE_BLE_ADVERTISING, "Advertising as KAJO-Dash Firmware Update");
  notifyStatus();
}

bool firmwareUpdateBleConfirmDowngrade() {
  if (!updateModeActive || firmwareUpdateBleStatus().state != FIRMWARE_UPDATE_BLE_CONFIRM_DOWNGRADE ||
      !packetQueue)
    return false;
  UpdatePacket packet = {};
  packet.kind = PACKET_CONFIRM_DOWNGRADE;
  return xQueueSend(packetQueue, &packet, 0) == pdTRUE;
}

bool firmwareUpdateBleCancelFromDisplay() {
  if (!updateModeActive || !packetQueue || cancelPending) return false;
  cancelPending = true;
  UpdatePacket packet = {};
  packet.kind = PACKET_CANCEL_FROM_DISPLAY;
  if (xQueueSendToFront(packetQueue, &packet, 0) == pdTRUE) return true;
  cancelPending = false;
  return false;
}

void firmwareUpdateBleRequestReboot() {
  if (firmwareUpdateStatus().state != FIRMWARE_UPDATE_READY_TO_REBOOT) return;
  const uint32_t now = millis();
  portENTER_CRITICAL(&statusMux);
  const bool schedule = rebootAtMs == 0 || static_cast<int32_t>(rebootAtMs - now) > 350;
  if (schedule) rebootAtMs = now + 350;
  portEXIT_CRITICAL(&statusMux);
  if (!schedule) return;
  publishStatus(FIRMWARE_UPDATE_BLE_READY_TO_REBOOT, "Restarting into verified firmware");
  notifyStatus();
}

FirmwareUpdateBleStatus firmwareUpdateBleStatus() {
  FirmwareUpdateBleStatus copy;
  portENTER_CRITICAL(&statusMux);
  initializeStatus();
  copy = sharedStatus;
  portEXIT_CRITICAL(&statusMux);
  return copy;
}

bool firmwareUpdateBleActive() { return updateModeActive; }
