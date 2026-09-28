#pragma once

#include <Arduino.h>

constexpr uint8_t FIRMWARE_UPDATE_PROTOCOL_VERSION = 3;
constexpr uint8_t FIRMWARE_UPDATE_TARGET_CYD_ESP32 = 1;
constexpr uint16_t FIRMWARE_UPDATE_FLAG_ZLIB_SECTORS = 0x0001;
constexpr size_t FIRMWARE_UPDATE_MANIFEST_BYTES = 84;
constexpr size_t FIRMWARE_UPDATE_SIGNATURE_BYTES = 64;

#ifndef CYD_FIRMWARE_VERSION_CODE
#define CYD_FIRMWARE_VERSION_CODE 0UL
#endif

struct FirmwareUpdateManifest {
  uint8_t protocolVersion;
  uint8_t target;
  uint16_t flags;
  uint32_t versionCode;
  uint32_t imageSize;
  uint32_t transportSize;
  uint8_t imageSha256[32];
  uint8_t transportSha256[32];
};

enum FirmwareManifestError : int32_t {
  FIRMWARE_MANIFEST_OK = 0,
  FIRMWARE_MANIFEST_ERR_KEY_NOT_CONFIGURED = -1100,
  FIRMWARE_MANIFEST_ERR_FORMAT = -1101,
  FIRMWARE_MANIFEST_ERR_PROTOCOL = -1102,
  FIRMWARE_MANIFEST_ERR_TARGET = -1103,
  FIRMWARE_MANIFEST_ERR_DOWNGRADE = -1104,
  FIRMWARE_MANIFEST_ERR_SIGNATURE = -1105,
};

bool firmwareUpdateSigningKeyConfigured();
bool firmwareUpdateDecodeManifest(const uint8_t *bytes, size_t length, FirmwareUpdateManifest &manifest);
void firmwareUpdateEncodeManifest(const FirmwareUpdateManifest &manifest,
                                  uint8_t bytes[FIRMWARE_UPDATE_MANIFEST_BYTES]);
FirmwareManifestError firmwareUpdateVerifyManifest(const FirmwareUpdateManifest &manifest,
                                                   const uint8_t signature[FIRMWARE_UPDATE_SIGNATURE_BYTES],
                                                   bool allowDowngrade = false);
