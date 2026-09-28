#pragma once

#include <Arduino.h>

#include "fardriver_protocol.h"

enum FarDriverBleState : uint8_t {
  FARDRIVER_BLE_IDLE,
  FARDRIVER_BLE_SCANNING,
  FARDRIVER_BLE_CONNECTING,
  FARDRIVER_BLE_DISCOVERING,
  FARDRIVER_BLE_CONNECTED,
  FARDRIVER_BLE_ERROR,
};

constexpr uint8_t FARDRIVER_BLE_MAX_DEVICES = 12;

struct FarDriverBleDeviceInfo {
  char name[25];
  char address[19];
  int8_t rssi;
  uint8_t addressType;
  bool likelyFarDriver;
};

struct FarDriverBleStatus {
  FarDriverBleState state;
  FarDriverBleDeviceInfo devices[FARDRIVER_BLE_MAX_DEVICES];
  uint8_t deviceCount;
  bool savedDevice;
  char savedAddress[19];
  char connectedAddress[19];
  char serviceUuid[40];
  char characteristicUuid[40];
  char lastPacketHex[64];
  char message[56];
  // A notification is not a frame boundary, so the useful counts are about the
  // byte stream rather than about callbacks: frames recovered, windows that
  // looked like a frame but failed the checksum, and bytes thrown away while
  // resynchronising. On a healthy link the last two stay at zero, which is the
  // signal that the frame layout and CRC assumptions are right.
  uint32_t frameCount;
  uint32_t crcFailCount;
  uint32_t discardedBytes;
  uint16_t lastPacketLength;
  uint8_t lastFrameId;
  int8_t connectedRssi;
  // Discovery must not rebuild for packet/counter updates.
  uint32_t linkRevision;
  uint32_t revision;
};

FarDriverTelemetry farDriverTelemetry();

// The diagnostic backend is intentionally read-only. Commands are queued to a
// low-priority worker; BLE callbacks publish only this bounded status snapshot.
void farDriverBleBegin();
void farDriverBleStartScan();
void farDriverBleConnect(uint8_t deviceIndex);
// Connect to the saved controller by its stored address, whatever the device
// list currently holds. Does nothing when no controller is saved.
void farDriverBleReconnectSaved();
void farDriverBleDisconnect();
void farDriverBleForget();
// Hand the single NimBLE client slot back so the other controller backend can
// use it. Asynchronous, like every other command here: poll
// farDriverBleRadioReleased() for completion.
void farDriverBleReleaseRadio();
bool farDriverBleRadioReleased();
// Reset the persisted pairing and cancel any queued controller operation.
void farDriverBleResetSettings();
FarDriverBleStatus farDriverBleStatus();
