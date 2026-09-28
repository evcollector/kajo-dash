#include <cstdio>
#include <cstring>
#include "../../src/vesc_test/frames.h"
#include "../../src/lvgl_app/vesc_protocol.h"

namespace {

// Independently calculated: CRC-16/XMODEM over the payload of the stationary
// reply, worked out from the protocol description rather than from either
// implementation here.
constexpr uint16_t kStationaryValuesCrc = 0x594A;
constexpr uint16_t kStationarySetupCrc = 0x8D56;
// Covers the firmware-version payload including the device name, so renaming
// the fixture changes this value even when the frame length does not.
constexpr uint16_t kStationaryFwCrc = 0x5603;

bool framingOk(const uint8_t *frame, size_t length, uint8_t command, uint16_t expectedCrc) {
  if (length < 6 || frame[0] != 2 || frame[length - 1] != 3) return false;
  const size_t payloadLength = frame[1];
  if (payloadLength + 5 != length) return false;
  if (frame[2] != command) return false;
  const uint16_t crc = uint16_t(uint16_t(frame[length - 3]) << 8) | frame[length - 2];
  return crc == expectedCrc;
}

bool near(float value, float expected, float tolerance) {
  const float difference = value - expected;
  return (difference < 0 ? -difference : difference) <= tolerance;
}

}  // namespace

int main() {
  uint8_t frame[96];

  // 1. Fixed wire expectations for the stationary sample.
  size_t length = fake_vesc::makeValuesFrame(frame, sizeof(frame), fake_vesc::kStationary, {});
  if (length != 64 || !framingOk(frame, length, 4, kStationaryValuesCrc)) {
    std::fprintf(stderr, "VESC sender: stationary GET_VALUES frame mismatch (len %zu)\n", length);
    return 1;
  }
  length = fake_vesc::makeSetupFrame(frame, sizeof(frame), fake_vesc::kStationary, {});
  if (length != 63 || !framingOk(frame, length, 47, kStationarySetupCrc)) {
    std::fprintf(stderr, "VESC sender: stationary GET_VALUES_SETUP frame mismatch (len %zu)\n", length);
    return 1;
  }
  length = fake_vesc::makeFwVersionFrame(frame, sizeof(frame));
  if (length != 18 || !framingOk(frame, length, 0, kStationaryFwCrc)) {
    std::fprintf(stderr, "VESC sender: firmware version frame mismatch (len %zu)\n", length);
    return 1;
  }

  // 2. The receiver's own parsers must accept what the sender emits, across a
  // whole ride cycle. The sender's encoder and the firmware's decoder are
  // separate implementations, so agreement here is evidence, not a tautology.
  for (uint32_t ms = 0; ms < 120000; ms += 250) {
    const fake_vesc::Sample sample = fake_vesc::rideSample(ms);
    const fake_vesc::Totals totals = {12000, 500, 640000, 27000, 45000, 1234567, 1234567};

    length = fake_vesc::makeValuesFrame(frame, sizeof(frame), sample, totals);
    if (vescCrc16(frame + 2, frame[1]) != ((uint16_t(frame[length - 3]) << 8) | frame[length - 2])) return 1;
    VescValues values = {};
    if (!vescParseValues(frame + 2, frame[1], values)) {
      std::fprintf(stderr, "VESC sender: firmware rejected GET_VALUES at %u ms\n", unsigned(ms));
      return 1;
    }
    if (!near(values.inpVoltage, sample.voltageDeci / 10.0F, 0.05F) ||
        !near(values.tempMosfet, sample.tempFetDeci / 10.0F, 0.05F) ||
        !near(values.tempMotor, sample.tempMotorDeci / 10.0F, 0.05F) ||
        !near(values.avgInputCurrent, sample.inputCurrentCenti / 100.0F, 0.005F) ||
        !near(values.avgMotorCurrent, sample.motorCurrentCenti / 100.0F, 0.005F) ||
        !near(values.rpm, float(sample.erpm), 0.5F) ||
        values.tachometerAbs != totals.tachometerAbs || values.faultCode != sample.fault) {
      std::fprintf(stderr, "VESC sender: GET_VALUES decoded wrong at %u ms\n", unsigned(ms));
      return 1;
    }

    length = fake_vesc::makeSetupFrame(frame, sizeof(frame), sample, totals);
    VescSetupValues setup = {};
    if (!vescParseSetupValues(frame + 2, frame[1], setup)) {
      std::fprintf(stderr, "VESC sender: firmware rejected GET_VALUES_SETUP at %u ms\n", unsigned(ms));
      return 1;
    }
    if (!near(setup.speedMs, sample.speedMmPerS / 1000.0F, 0.002F) ||
        !near(setup.distanceAbsM, totals.odometerMm / 1000.0F, 0.002F)) {
      std::fprintf(stderr, "VESC sender: setup values decoded wrong at %u ms\n", unsigned(ms));
      return 1;
    }
    // The firmware cross-checks the two replies before trusting setup values,
    // and drops them for good when they disagree. A sender that failed this
    // would silently test only the eRPM fallback.
    if (!near(setup.voltage, values.inpVoltage, 1.0F) || !near(setup.tempFet, values.tempMosfet, 3.0F)) {
      std::fprintf(stderr, "VESC sender: replies disagree at %u ms\n", unsigned(ms));
      return 1;
    }
  }

  uint8_t major = 0, minor = 0;
  length = fake_vesc::makeFwVersionFrame(frame, sizeof(frame));
  if (!vescParseFwVersion(frame + 2, frame[1], major, minor) || major != 6 || minor != 2) return 1;

  std::puts("VESC sender: stationary fixtures and two complete ride cycles passed");
}
