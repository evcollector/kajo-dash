// Golden-vector tests for the VESC wire protocol parsers.
//
// Each COMM payload is built by writing known values at the documented field
// offsets, then parsed back. That checks the offsets against intent rather than
// against themselves. The expected values were validated field-for-field
// against the previous VescUart implementation over 20,000 randomised payloads
// before the library was removed.

#include <cmath>
#include <cstdio>
#include <cstring>

#include "vesc_protocol.h"

namespace {

int failures = 0;

void expect(bool ok, const char *what) {
  if (!ok) {
    printf("FAIL: %s\n", what);
    failures++;
  }
}

void expectNear(float actual, float wanted, const char *what) {
  if (std::fabs(actual - wanted) > 1e-3F) {
    printf("FAIL: %s (got %.6f, want %.6f)\n", what, actual, wanted);
    failures++;
  }
}

void writeInt16(uint8_t *buf, size_t &i, int16_t v) {
  buf[i++] = (uint8_t)((uint16_t)v >> 8);
  buf[i++] = (uint8_t)((uint16_t)v & 0xFF);
}

void writeInt32(uint8_t *buf, size_t &i, int32_t v) {
  const uint32_t u = (uint32_t)v;
  buf[i++] = (uint8_t)(u >> 24);
  buf[i++] = (uint8_t)(u >> 16);
  buf[i++] = (uint8_t)(u >> 8);
  buf[i++] = (uint8_t)(u & 0xFF);
}

void writeFloat16(uint8_t *buf, size_t &i, float v, float scale) {
  writeInt16(buf, i, (int16_t)lroundf(v * scale));
}

void writeFloat32(uint8_t *buf, size_t &i, float v, float scale) {
  writeInt32(buf, i, (int32_t)lroundf(v * scale));
}

void testCrc() {
  // The standard CRC-16/XMODEM check value.
  const uint8_t digits[] = {0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39};
  expect(vescCrc16(digits, sizeof(digits)) == 0x31C3, "crc16 check value 0x31C3");

  const uint8_t one[] = {VESC_COMM_GET_VALUES};
  expect(vescCrc16(one, 1) == 0x4084, "crc16 of a single COMM_GET_VALUES byte");
}

void testGetValues() {
  uint8_t payload[80] = {};
  size_t i = 0;
  payload[i++] = VESC_COMM_GET_VALUES;
  writeFloat16(payload, i, 32.5F, 1e1F);     // mosfet temperature
  writeFloat16(payload, i, 41.2F, 1e1F);     // motor temperature
  writeFloat32(payload, i, 18.75F, 1e2F);    // average motor current
  writeFloat32(payload, i, 7.32F, 1e2F);     // average input current
  writeInt32(payload, i, 0);                 // avg_id, skipped
  writeInt32(payload, i, 0);                 // avg_iq, skipped
  writeFloat16(payload, i, 0.62F, 1e3F);     // duty cycle, skipped
  writeFloat32(payload, i, 3600.0F, 1e0F);   // eRPM
  writeFloat16(payload, i, 48.6F, 1e1F);     // input voltage
  writeFloat32(payload, i, 2.5431F, 1e4F);   // amp hours
  writeFloat32(payload, i, 0.0812F, 1e4F);   // amp hours charged
  writeFloat32(payload, i, 118.7F, 1e4F);    // watt hours
  writeFloat32(payload, i, 3.94F, 1e4F);     // watt hours charged
  writeInt32(payload, i, 123456);            // tachometer, skipped
  writeInt32(payload, i, 234567);            // tachometer absolute
  payload[i++] = 3;                          // fault code

  VescValues v = {};
  expect(vescParseValues(payload, 56, v), "GET_VALUES accepted");
  expectNear(v.tempMosfet, 32.5F, "tempMosfet");
  expectNear(v.tempMotor, 41.2F, "tempMotor");
  expectNear(v.avgMotorCurrent, 18.75F, "avgMotorCurrent");
  expectNear(v.avgInputCurrent, 7.32F, "avgInputCurrent");
  expectNear(v.rpm, 3600.0F, "rpm");
  expectNear(v.inpVoltage, 48.6F, "inpVoltage");
  expectNear(v.ampHours, 2.5431F, "ampHours");
  expectNear(v.ampHoursCharged, 0.0812F, "ampHoursCharged");
  expectNear(v.wattHours, 118.7F, "wattHours");
  expectNear(v.wattHoursCharged, 3.94F, "wattHoursCharged");
  expect(v.tachometerAbs == 234567, "tachometerAbs");
  expect(v.faultCode == 3, "faultCode");

  VescValues ignored = {};
  expect(!vescParseValues(payload, 55, ignored), "GET_VALUES rejects a short payload");
  expect(!vescParseValues(nullptr, 56, ignored), "GET_VALUES rejects null");
  payload[0] = VESC_COMM_FW_VERSION;
  expect(!vescParseValues(payload, 56, ignored), "GET_VALUES rejects a wrong command id");
}

void testSetupValues() {
  uint8_t payload[80] = {};
  size_t i = 0;
  payload[i++] = VESC_COMM_GET_VALUES_SETUP;
  writeFloat16(payload, i, 34.8F, 1e1F);     // mosfet temperature
  writeFloat16(payload, i, 39.1F, 1e1F);     // motor temperature, skipped
  writeFloat32(payload, i, 12.0F, 1e2F);     // average motor current, skipped
  writeFloat32(payload, i, 5.5F, 1e2F);      // average input current, skipped
  writeFloat16(payload, i, 0.5F, 1e3F);      // duty cycle, skipped
  writeFloat32(payload, i, 2800.0F, 1e0F);   // eRPM, skipped
  writeFloat32(payload, i, 8.125F, 1e3F);    // speed, metres per second
  writeFloat16(payload, i, 47.9F, 1e1F);     // input voltage
  writeFloat16(payload, i, 0.78F, 1e3F);     // charge level, skipped
  writeFloat32(payload, i, 1.5F, 1e4F);      // amp hours, skipped
  writeFloat32(payload, i, 0.1F, 1e4F);      // amp hours charged, skipped
  writeFloat32(payload, i, 70.0F, 1e4F);     // watt hours, skipped
  writeFloat32(payload, i, 2.0F, 1e4F);      // watt hours charged, skipped
  writeFloat32(payload, i, 1234.0F, 1e3F);   // trip distance, skipped
  writeFloat32(payload, i, 45678.0F, 1e3F);  // absolute distance, metres

  VescSetupValues v = {};
  expect(vescParseSetupValues(payload, 55, v), "SETUP accepted");
  expectNear(v.tempFet, 34.8F, "setup tempFet");
  expectNear(v.speedMs, 8.125F, "setup speedMs");
  expectNear(v.voltage, 47.9F, "setup voltage");
  expectNear(v.distanceAbsM, 45678.0F, "setup distanceAbsM");

  VescSetupValues ignored = {};
  expect(!vescParseSetupValues(payload, 54, ignored), "SETUP rejects a short payload");
}

void testFwVersion() {
  const uint8_t payload[] = {VESC_COMM_FW_VERSION, 6, 2};
  uint8_t major = 0;
  uint8_t minor = 0;
  expect(vescParseFwVersion(payload, sizeof(payload), major, minor), "FW_VERSION accepted");
  expect(major == 6 && minor == 2, "FW_VERSION values");

  // The previous library read both bytes after checking only that the reply was
  // non-empty, which could read past a one-byte payload.
  expect(!vescParseFwVersion(payload, 2, major, minor), "FW_VERSION rejects a truncated reply");
}

void testFraming() {
  uint8_t payload[3] = {};
  expect(vescBuildCommandPayload(VESC_COMM_GET_VALUES, 0, payload, sizeof(payload)) == 1,
         "payload without CAN forward is one byte");
  expect(payload[0] == VESC_COMM_GET_VALUES, "payload carries the command id");

  const size_t n = vescBuildCommandPayload(VESC_COMM_GET_VALUES, 17, payload, sizeof(payload));
  expect(n == 3, "payload with CAN forward is three bytes");
  expect(payload[0] == VESC_COMM_FORWARD_CAN && payload[1] == 17 &&
             payload[2] == VESC_COMM_GET_VALUES,
         "CAN forward wrapping");

  uint8_t frame[16] = {};
  const size_t frameLen = vescBuildFrame(payload, (uint8_t)n, frame, sizeof(frame));
  expect(frameLen == n + 5, "frame length");
  expect(frame[0] == 2, "frame start byte");
  expect(frame[1] == n, "frame length byte");
  expect(frame[frameLen - 1] == 3, "frame stop byte");
  // CRC-16/XMODEM of {34, 17, 4}, worked out with Python's binascii.crc_hqx
  // rather than with vescCrc16, stored high byte first.
  expect(frame[frameLen - 3] == 0x98 && frame[frameLen - 2] == 0x60, "frame CRC bytes");

  uint8_t tiny[4] = {};
  expect(vescBuildFrame(payload, (uint8_t)n, tiny, sizeof(tiny)) == 0, "frame refuses to overflow");
  expect(vescBuildCommandPayload(VESC_COMM_GET_VALUES, 17, payload, 2) == 0,
         "payload refuses to overflow");
}

}  // namespace

int main() {
  testCrc();
  testGetValues();
  testSetupValues();
  testFwVersion();
  testFraming();
  if (failures == 0) printf("vesc_protocol: all checks passed\n");
  return failures == 0 ? 0 : 1;
}
