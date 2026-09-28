#include "firmware_update.h"

#include <esp_ota_ops.h>
#include <mbedtls/sha256.h>

namespace {

static constexpr uint32_t kBootValidationMs = 10000;
static FirmwareUpdateStatus sharedStatus = {};
static portMUX_TYPE statusMux = portMUX_INITIALIZER_UNLOCKED;
static const esp_partition_t *targetPartition = nullptr;
static esp_ota_handle_t otaHandle = 0;
static mbedtls_sha256_context shaContext;
static bool shaActive = false;
static uint8_t expectedDigest[32] = {};
static bool bootSelectionStaged = false;
static bool bootValidationPending = false;
static uint32_t bootValidationStartedMs = 0;
static uint32_t lastValidationAttemptMs = 0;

static void publishStatus(FirmwareUpdateState state, uint32_t total, uint32_t received, int32_t error) {
  portENTER_CRITICAL(&statusMux);
  sharedStatus.state = state;
  sharedStatus.totalBytes = total;
  sharedStatus.receivedBytes = received;
  sharedStatus.lastError = error;
  portEXIT_CRITICAL(&statusMux);
}

static void releaseDigest() {
  if (!shaActive) return;
  mbedtls_sha256_free(&shaContext);
  shaActive = false;
}

static void abandonTransfer(int32_t error) {
  const FirmwareUpdateStatus previous = firmwareUpdateStatus();
  if (otaHandle != 0) {
    esp_ota_abort(otaHandle);
    otaHandle = 0;
  }
  releaseDigest();
  targetPartition = nullptr;
  publishStatus(FIRMWARE_UPDATE_ERROR, previous.totalBytes, previous.receivedBytes, error);
}

}  // namespace

// initArduino() calls this weak hook before setup(). Returning true keeps an
// ESP_OTA_IMG_PENDING_VERIFY image pending so this application can perform a
// real health check instead of confirming it before the display even starts.
extern "C" bool verifyRollbackLater() { return true; }

bool firmwareUpdateBegin(uint32_t imageSize, const uint8_t expectedSha256[32]) {
  const FirmwareUpdateStatus current = firmwareUpdateStatus();
  if (current.state == FIRMWARE_UPDATE_RECEIVING || bootSelectionStaged) {
    publishStatus(current.state, current.totalBytes, current.receivedBytes, FIRMWARE_UPDATE_ERR_BUSY);
    return false;
  }

  firmwareUpdateAbort();
  targetPartition = esp_ota_get_next_update_partition(nullptr);
  if (!targetPartition || imageSize == 0 || imageSize > targetPartition->size || !expectedSha256) {
    publishStatus(FIRMWARE_UPDATE_ERROR, imageSize, 0, FIRMWARE_UPDATE_ERR_SIZE);
    return false;
  }

  esp_err_t result = esp_ota_begin(targetPartition, imageSize, &otaHandle);
  if (result != ESP_OK) {
    otaHandle = 0;
    targetPartition = nullptr;
    publishStatus(FIRMWARE_UPDATE_ERROR, imageSize, 0, result);
    return false;
  }

  memcpy(expectedDigest, expectedSha256, sizeof(expectedDigest));
  mbedtls_sha256_init(&shaContext);
  shaActive = true;
  result = mbedtls_sha256_starts_ret(&shaContext, 0);
  if (result != 0) {
    abandonTransfer(result);
    return false;
  }

  publishStatus(FIRMWARE_UPDATE_RECEIVING, imageSize, 0, FIRMWARE_UPDATE_OK);
  return true;
}

bool firmwareUpdateWrite(uint32_t offset, const uint8_t *data, size_t length) {
  FirmwareUpdateStatus current = firmwareUpdateStatus();
  if (current.state != FIRMWARE_UPDATE_RECEIVING || otaHandle == 0 || !shaActive) return false;
  if (offset != current.receivedBytes) {
    publishStatus(current.state, current.totalBytes, current.receivedBytes, FIRMWARE_UPDATE_ERR_OFFSET);
    return false;
  }
  if (!data || length == 0 || length > current.totalBytes - current.receivedBytes) {
    abandonTransfer(FIRMWARE_UPDATE_ERR_SIZE);
    return false;
  }

  const esp_err_t writeResult = esp_ota_write(otaHandle, data, length);
  if (writeResult != ESP_OK) {
    abandonTransfer(writeResult);
    return false;
  }
  const int hashResult = mbedtls_sha256_update_ret(&shaContext, data, length);
  if (hashResult != 0) {
    abandonTransfer(hashResult);
    return false;
  }

  current.receivedBytes += static_cast<uint32_t>(length);
  publishStatus(FIRMWARE_UPDATE_RECEIVING, current.totalBytes, current.receivedBytes, FIRMWARE_UPDATE_OK);
  return true;
}

bool firmwareUpdateFinish() {
  const FirmwareUpdateStatus current = firmwareUpdateStatus();
  if (current.state != FIRMWARE_UPDATE_RECEIVING || otaHandle == 0 || !shaActive) return false;
  if (current.receivedBytes != current.totalBytes) {
    publishStatus(current.state, current.totalBytes, current.receivedBytes, FIRMWARE_UPDATE_ERR_INCOMPLETE);
    return false;
  }

  uint8_t actualDigest[32] = {};
  const int hashResult = mbedtls_sha256_finish_ret(&shaContext, actualDigest);
  releaseDigest();
  if (hashResult != 0) {
    abandonTransfer(hashResult);
    return false;
  }
  if (memcmp(actualDigest, expectedDigest, sizeof(actualDigest)) != 0) {
    abandonTransfer(FIRMWARE_UPDATE_ERR_HASH);
    return false;
  }

  esp_err_t result = esp_ota_end(otaHandle);
  otaHandle = 0;
  if (result != ESP_OK) {
    targetPartition = nullptr;
    publishStatus(FIRMWARE_UPDATE_ERROR, current.totalBytes, current.receivedBytes, result);
    return false;
  }
  result = esp_ota_set_boot_partition(targetPartition);
  targetPartition = nullptr;
  if (result != ESP_OK) {
    publishStatus(FIRMWARE_UPDATE_ERROR, current.totalBytes, current.receivedBytes, result);
    return false;
  }
  bootSelectionStaged = true;

  publishStatus(FIRMWARE_UPDATE_READY_TO_REBOOT, current.totalBytes, current.receivedBytes, FIRMWARE_UPDATE_OK);
  return true;
}

void firmwareUpdateAbort() {
  const FirmwareUpdateStatus current = firmwareUpdateStatus();
  if (otaHandle != 0) {
    esp_ota_abort(otaHandle);
    otaHandle = 0;
  }
  releaseDigest();
  targetPartition = nullptr;
  memset(expectedDigest, 0, sizeof(expectedDigest));

  // FINISH has already selected the inactive slot. Cancelling from that state
  // must explicitly restore the running image as the boot target.
  if (bootSelectionStaged) {
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_err_t result = running ? esp_ota_set_boot_partition(running) : ESP_ERR_NOT_FOUND;
    if (result != ESP_OK) {
      publishStatus(FIRMWARE_UPDATE_ERROR, current.totalBytes, current.receivedBytes, result);
      return;
    }
    bootSelectionStaged = false;
  }
  publishStatus(FIRMWARE_UPDATE_IDLE, 0, 0, FIRMWARE_UPDATE_OK);
}

FirmwareUpdateStatus firmwareUpdateStatus() {
  FirmwareUpdateStatus copy;
  portENTER_CRITICAL(&statusMux);
  copy = sharedStatus;
  portEXIT_CRITICAL(&statusMux);
  return copy;
}

void firmwareUpdateBootValidationBegin() {
  const esp_partition_t *running = esp_ota_get_running_partition();
  esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
  bootValidationPending = running && esp_ota_get_state_partition(running, &state) == ESP_OK &&
                          state == ESP_OTA_IMG_PENDING_VERIFY;
  if (!bootValidationPending) return;
  bootValidationStartedMs = millis();
  lastValidationAttemptMs = 0;
  Serial.println("OTA image pending validation; starting UI health window");
}

void firmwareUpdateBootValidationService() {
  if (!bootValidationPending) return;
  const uint32_t now = millis();
  if (now - bootValidationStartedMs < kBootValidationMs) return;
  if (lastValidationAttemptMs && now - lastValidationAttemptMs < 1000) return;
  lastValidationAttemptMs = now;
  const esp_err_t result = esp_ota_mark_app_valid_cancel_rollback();
  if (result == ESP_OK) {
    bootValidationPending = false;
    Serial.println("OTA image validated after UI health window");
  } else {
    Serial.printf("OTA image validation failed: %d\n", result);
  }
}
