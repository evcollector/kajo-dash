#pragma once

#include <Arduino.h>

enum FirmwareUpdateState : uint8_t {
  FIRMWARE_UPDATE_IDLE,
  FIRMWARE_UPDATE_RECEIVING,
  FIRMWARE_UPDATE_READY_TO_REBOOT,
  FIRMWARE_UPDATE_ERROR,
};

enum FirmwareUpdateError : int32_t {
  FIRMWARE_UPDATE_OK = 0,
  FIRMWARE_UPDATE_ERR_BUSY = -1000,
  FIRMWARE_UPDATE_ERR_SIZE = -1001,
  FIRMWARE_UPDATE_ERR_OFFSET = -1002,
  FIRMWARE_UPDATE_ERR_HASH = -1003,
  FIRMWARE_UPDATE_ERR_INCOMPLETE = -1004,
};

struct FirmwareUpdateStatus {
  FirmwareUpdateState state;
  uint32_t totalBytes;
  uint32_t receivedBytes;
  int32_t lastError;
};

// Transport-independent inactive-slot writer. A later BLE service can feed it
// bounded chunks without owning partition, digest, or boot-selection logic.
bool firmwareUpdateBegin(uint32_t imageSize, const uint8_t expectedSha256[32]);
bool firmwareUpdateWrite(uint32_t offset, const uint8_t *data, size_t length);
bool firmwareUpdateFinish();
void firmwareUpdateAbort();
FirmwareUpdateStatus firmwareUpdateStatus();

// Keep a freshly updated application pending until setup and the LVGL loop
// have survived a short health window. A reset before confirmation lets the
// ESP-IDF bootloader return to the previous OTA slot.
void firmwareUpdateBootValidationBegin();
void firmwareUpdateBootValidationService();
