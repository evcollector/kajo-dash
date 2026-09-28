#include "config.h"
#include "firmware_update_auth.h"

#include <mbedtls/ecdsa.h>
#include <mbedtls/sha256.h>

#include "firmware_update_public_key.h"

namespace {

static uint16_t readU16Le(const uint8_t *bytes) {
  return static_cast<uint16_t>(bytes[0]) | static_cast<uint16_t>(bytes[1]) << 8;
}

static uint32_t readU32Le(const uint8_t *bytes) {
  return static_cast<uint32_t>(bytes[0]) | static_cast<uint32_t>(bytes[1]) << 8 |
         static_cast<uint32_t>(bytes[2]) << 16 | static_cast<uint32_t>(bytes[3]) << 24;
}

static void writeU16Le(uint8_t *bytes, uint16_t value) {
  bytes[0] = static_cast<uint8_t>(value);
  bytes[1] = static_cast<uint8_t>(value >> 8);
}

static void writeU32Le(uint8_t *bytes, uint32_t value) {
  bytes[0] = static_cast<uint8_t>(value);
  bytes[1] = static_cast<uint8_t>(value >> 8);
  bytes[2] = static_cast<uint8_t>(value >> 16);
  bytes[3] = static_cast<uint8_t>(value >> 24);
}

}  // namespace

bool firmwareUpdateSigningKeyConfigured() { return CYD_FIRMWARE_SIGNING_KEY_CONFIGURED == 1; }

bool firmwareUpdateDecodeManifest(const uint8_t *bytes, size_t length, FirmwareUpdateManifest &manifest) {
  if (!bytes || length != FIRMWARE_UPDATE_MANIFEST_BYTES || memcmp(bytes, "KAJU", 4) != 0) return false;
  manifest.protocolVersion = bytes[4];
  manifest.target = bytes[5];
  manifest.flags = readU16Le(bytes + 6);
  manifest.versionCode = readU32Le(bytes + 8);
  manifest.imageSize = readU32Le(bytes + 12);
  manifest.transportSize = readU32Le(bytes + 16);
  memcpy(manifest.imageSha256, bytes + 20, sizeof(manifest.imageSha256));
  memcpy(manifest.transportSha256, bytes + 52, sizeof(manifest.transportSha256));
  return true;
}

void firmwareUpdateEncodeManifest(const FirmwareUpdateManifest &manifest,
                                  uint8_t bytes[FIRMWARE_UPDATE_MANIFEST_BYTES]) {
  memcpy(bytes, "KAJU", 4);
  bytes[4] = manifest.protocolVersion;
  bytes[5] = manifest.target;
  writeU16Le(bytes + 6, manifest.flags);
  writeU32Le(bytes + 8, manifest.versionCode);
  writeU32Le(bytes + 12, manifest.imageSize);
  writeU32Le(bytes + 16, manifest.transportSize);
  memcpy(bytes + 20, manifest.imageSha256, sizeof(manifest.imageSha256));
  memcpy(bytes + 52, manifest.transportSha256, sizeof(manifest.transportSha256));
}

FirmwareManifestError firmwareUpdateVerifyManifest(const FirmwareUpdateManifest &manifest,
                                                   const uint8_t signature[FIRMWARE_UPDATE_SIGNATURE_BYTES],
                                                   bool allowDowngrade) {
  if (!firmwareUpdateSigningKeyConfigured()) return FIRMWARE_MANIFEST_ERR_KEY_NOT_CONFIGURED;
  if (!signature || manifest.flags != FIRMWARE_UPDATE_FLAG_ZLIB_SECTORS || manifest.imageSize == 0 ||
      manifest.transportSize == 0)
    return FIRMWARE_MANIFEST_ERR_FORMAT;
  if (manifest.protocolVersion != FIRMWARE_UPDATE_PROTOCOL_VERSION) return FIRMWARE_MANIFEST_ERR_PROTOCOL;
  if (manifest.target != FIRMWARE_UPDATE_TARGET_CYD_ESP32) return FIRMWARE_MANIFEST_ERR_TARGET;
  uint8_t manifestBytes[FIRMWARE_UPDATE_MANIFEST_BYTES];
  uint8_t digest[32];
  firmwareUpdateEncodeManifest(manifest, manifestBytes);
  if (mbedtls_sha256_ret(manifestBytes, sizeof(manifestBytes), digest, 0) != 0)
    return FIRMWARE_MANIFEST_ERR_SIGNATURE;

  mbedtls_ecdsa_context context;
  mbedtls_mpi r;
  mbedtls_mpi s;
  mbedtls_ecdsa_init(&context);
  mbedtls_mpi_init(&r);
  mbedtls_mpi_init(&s);
  int result = mbedtls_ecp_group_load(&context.grp, MBEDTLS_ECP_DP_SECP256R1);
  if (result == 0)
    result = mbedtls_ecp_point_read_binary(&context.grp, &context.Q, kFirmwareSigningPublicKey,
                                           sizeof(kFirmwareSigningPublicKey));
  if (result == 0) result = mbedtls_ecp_check_pubkey(&context.grp, &context.Q);
  if (result == 0) result = mbedtls_mpi_read_binary(&r, signature, 32);
  if (result == 0) result = mbedtls_mpi_read_binary(&s, signature + 32, 32);
  if (result == 0) result = mbedtls_ecdsa_verify(&context.grp, digest, sizeof(digest), &context.Q, &r, &s);
  mbedtls_mpi_free(&s);
  mbedtls_mpi_free(&r);
  mbedtls_ecdsa_free(&context);
  if (result != 0) return FIRMWARE_MANIFEST_ERR_SIGNATURE;
  if (!allowDowngrade && manifest.versionCode < static_cast<uint32_t>(CYD_FIRMWARE_VERSION_CODE))
    return FIRMWARE_MANIFEST_ERR_DOWNGRADE;
  return FIRMWARE_MANIFEST_OK;
}
