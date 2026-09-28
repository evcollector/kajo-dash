#pragma once

#include <Arduino.h>

enum CompanionBleState : uint8_t {
  COMPANION_BLE_OFF,
  COMPANION_BLE_PREPARING,
  COMPANION_BLE_ADVERTISING,
  COMPANION_BLE_CONNECTED,
  COMPANION_BLE_ERROR,
};

struct CompanionBleStatus {
  CompanionBleState state;
  bool controllerPaused;
  bool controllerUsesBle;
  uint32_t secondsRemaining;
  uint32_t revision;
  char message[64];
};

bool companionBleStart();
void companionBleStop();
void companionBleService();
bool companionBleActive();
CompanionBleStatus companionBleStatus();

constexpr const char *COMPANION_SERVICE_UUID = "7c7d7f00-2aa7-4f62-a497-6a9b02d14d00";
constexpr const char *COMPANION_CONTROL_UUID = "7c7d7f01-2aa7-4f62-a497-6a9b02d14d00";
constexpr const char *COMPANION_RESPONSE_UUID = "7c7d7f02-2aa7-4f62-a497-6a9b02d14d00";
