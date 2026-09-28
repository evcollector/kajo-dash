#pragma once

#include <Arduino.h>

constexpr char FIRMWARE_UPDATE_SERVICE_UUID[] = "7c7d7e00-2aa7-4f62-a497-6a9b02d14d00";
constexpr char FIRMWARE_UPDATE_CONTROL_UUID[] = "7c7d7e01-2aa7-4f62-a497-6a9b02d14d00";
constexpr char FIRMWARE_UPDATE_DATA_UUID[] = "7c7d7e02-2aa7-4f62-a497-6a9b02d14d00";
constexpr char FIRMWARE_UPDATE_STATUS_UUID[] = "7c7d7e03-2aa7-4f62-a497-6a9b02d14d00";

enum FirmwareUpdateBleState : uint8_t {
  FIRMWARE_UPDATE_BLE_LOCKED,
  FIRMWARE_UPDATE_BLE_READY,
  FIRMWARE_UPDATE_BLE_PREPARING,
  FIRMWARE_UPDATE_BLE_ADVERTISING,
  FIRMWARE_UPDATE_BLE_CONNECTED,
  FIRMWARE_UPDATE_BLE_CONFIRM_DOWNGRADE,
  FIRMWARE_UPDATE_BLE_RECEIVING,
  FIRMWARE_UPDATE_BLE_READY_TO_REBOOT,
  FIRMWARE_UPDATE_BLE_ERROR,
  FIRMWARE_UPDATE_BLE_CANCELLED,
};

enum FirmwareUpdateBleError : int32_t {
  FIRMWARE_UPDATE_BLE_OK = 0,
  FIRMWARE_UPDATE_BLE_ERR_QUEUE = -1200,
  FIRMWARE_UPDATE_BLE_ERR_COMMAND = -1201,
  FIRMWARE_UPDATE_BLE_ERR_SIGNATURE_INCOMPLETE = -1202,
  FIRMWARE_UPDATE_BLE_ERR_PREPARE_TIMEOUT = -1203,
  FIRMWARE_UPDATE_BLE_ERR_SERVER = -1204,
  FIRMWARE_UPDATE_BLE_ERR_SECTOR_CRC = -1205,
  FIRMWARE_UPDATE_BLE_ERR_COMPRESSION = -1206,
  FIRMWARE_UPDATE_BLE_ERR_TRANSPORT_HASH = -1207,
  FIRMWARE_UPDATE_BLE_ERR_CANCELLED = -1208,
};

struct FirmwareUpdateBleStatus {
  FirmwareUpdateBleState state;
  bool keyConfigured;
  bool clientConnected;
  bool manifestAccepted;
  uint32_t expectedOffset;
  uint32_t totalBytes;
  uint32_t imageBytes;
  uint32_t transferBytesPerSecond;
  uint32_t versionCode;
  int32_t lastError;
  uint32_t revision;
  char message[64];
};

// Update mode can be entered from the basic Information screen or Developer
// Options. It suspends ride logging and disconnects a controller BLE client
// before starting the GATT server.
bool firmwareUpdateBleStart();
void firmwareUpdateBleStop();
void firmwareUpdateBleService();
bool firmwareUpdateBleConfirmDowngrade();
bool firmwareUpdateBleCancelFromDisplay();
void firmwareUpdateBleRequestReboot();
FirmwareUpdateBleStatus firmwareUpdateBleStatus();
bool firmwareUpdateBleActive();
