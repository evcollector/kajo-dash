#include "vesc_protocol.h"
#include <math.h>

float vescEstimatePhasePeakVoltage(float batteryVoltage, float dutyCycle) {
  // VESC FOC: |Vdq| = Vbus * |duty| / sqrt(3), the peak of a phase-to-neutral sine,
  // the same kind of amplitude as the motor current the VESC reports.
  return batteryVoltage > 0.0F && isfinite(dutyCycle)
      ? batteryVoltage * fminf(1.0F, fabsf(dutyCycle)) / sqrtf(3.0F) : 0.0F;
}

namespace {

// CRC-16/XMODEM: polynomial 0x1021, zero init, no reflection, no final xor.
// The bitwise form costs eight shifts per byte, which at ~75 bytes and 20 Hz is
// far below the noise floor on a 240 MHz core, and avoids carrying a table.
const uint16_t kCrcPoly = 0x1021;

int16_t readInt16(const uint8_t *buf, size_t &index) {
  const uint16_t raw = (uint16_t)((uint16_t)buf[index] << 8 | (uint16_t)buf[index + 1]);
  index += 2;
  return (int16_t)raw;
}

int32_t readInt32(const uint8_t *buf, size_t &index) {
  const uint32_t raw = (uint32_t)buf[index] << 24 | (uint32_t)buf[index + 1] << 16 |
                       (uint32_t)buf[index + 2] << 8 | (uint32_t)buf[index + 3];
  index += 4;
  return (int32_t)raw;
}

float readFloat16(const uint8_t *buf, float scale, size_t &index) {
  return (float)readInt16(buf, index) / scale;
}

float readFloat32(const uint8_t *buf, float scale, size_t &index) {
  return (float)readInt32(buf, index) / scale;
}

// Payload length needed to reach the last field each parser reads.
const size_t kValuesMinLen = 56;   // through the fault byte, matching the
                                   // length the previous library accepted
const size_t kSetupMinLen = 55;    // through absolute distance
const size_t kFwVersionMinLen = 3; // command id, major, minor

}  // namespace

uint16_t vescCrc16(const uint8_t *buf, size_t len) {
  uint16_t crc = 0;
  for (size_t i = 0; i < len; i++) {
    crc ^= (uint16_t)((uint16_t)buf[i] << 8);
    for (int bit = 0; bit < 8; bit++)
      crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ kCrcPoly) : (uint16_t)(crc << 1);
  }
  return crc;
}

size_t vescBuildCommandPayload(uint8_t command, uint8_t canId, uint8_t *out, size_t cap) {
  const size_t needed = canId != 0 ? 3u : 1u;
  if (!out || cap < needed) return 0;
  size_t n = 0;
  if (canId != 0) {
    out[n++] = VESC_COMM_FORWARD_CAN;
    out[n++] = canId;
  }
  out[n++] = command;
  return n;
}

size_t vescBuildFrame(const uint8_t *payload, uint8_t payloadLen, uint8_t *out, size_t cap) {
  if (!payload || !out || payloadLen == 0) return 0;
  const size_t frameLen = (size_t)payloadLen + 5u;
  if (cap < frameLen) return 0;
  size_t n = 0;
  out[n++] = 2;
  out[n++] = payloadLen;
  for (uint8_t i = 0; i < payloadLen; i++) out[n++] = payload[i];
  const uint16_t crc = vescCrc16(payload, payloadLen);
  out[n++] = (uint8_t)(crc >> 8);
  out[n++] = (uint8_t)(crc & 0xFF);
  out[n++] = 3;
  return n;
}

bool vescParseValues(const uint8_t *payload, size_t len, VescValues &out) {
  if (!payload || len < kValuesMinLen || payload[0] != VESC_COMM_GET_VALUES) return false;

  size_t i = 1;
  VescValues v = {};
  v.tempMosfet = readFloat16(payload, 1e1F, i);
  v.tempMotor = readFloat16(payload, 1e1F, i);
  v.avgMotorCurrent = readFloat32(payload, 1e2F, i);
  v.avgInputCurrent = readFloat32(payload, 1e2F, i);
  i += 4;  // avg_id
  i += 4;  // avg_iq
  v.dutyCycle = readFloat16(payload, 1e3F, i);
  v.rpm = readFloat32(payload, 1e0F, i);
  v.inpVoltage = readFloat16(payload, 1e1F, i);
  v.ampHours = readFloat32(payload, 1e4F, i);
  v.ampHoursCharged = readFloat32(payload, 1e4F, i);
  v.wattHours = readFloat32(payload, 1e4F, i);
  v.wattHoursCharged = readFloat32(payload, 1e4F, i);
  i += 4;  // tachometer, signed; only the absolute count is used
  v.tachometerAbs = readInt32(payload, i);
  v.faultCode = payload[i];

  out = v;
  return true;
}

bool vescParseSetupValues(const uint8_t *payload, size_t len, VescSetupValues &out) {
  if (!payload || len < kSetupMinLen || payload[0] != VESC_COMM_GET_VALUES_SETUP) return false;

  size_t i = 1;
  VescSetupValues v = {};
  v.tempFet = readFloat16(payload, 1e1F, i);
  i += 2;   // motor temperature
  i += 4;   // average motor current
  i += 4;   // average input current
  i += 2;   // duty cycle
  i += 4;   // eRPM
  v.speedMs = readFloat32(payload, 1e3F, i);
  v.voltage = readFloat16(payload, 1e1F, i);
  i += 2;   // controller-reported charge level
  i += 4;   // amp hours
  i += 4;   // amp hours charged
  i += 4;   // watt hours
  i += 4;   // watt hours charged
  i += 4;   // distance this trip
  v.distanceAbsM = readFloat32(payload, 1e3F, i);

  out = v;
  return true;
}

bool vescParseFwVersion(const uint8_t *payload, size_t len, uint8_t &major, uint8_t &minor) {
  if (!payload || len < kFwVersionMinLen || payload[0] != VESC_COMM_FW_VERSION) return false;
  major = payload[1];
  minor = payload[2];
  return true;
}
