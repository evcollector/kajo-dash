#pragma once

// VESC UART wire protocol: framing, CRC, and the payload parsers this firmware
// needs. Deliberately free of Arduino and Stream dependencies so the same code
// compiles for the device and for the host parity/regression tests.
//
// Frame layout (payloads <= 255 bytes, the only form a VESC sends for the
// commands used here):
//
//   0x02 | length | payload[length] | crc_hi | crc_lo | 0x03
//
// The CRC is CRC-16/XMODEM over the payload only. Payload byte 0 is the
// command id; every parser below indexes from 1 for that reason.

#include <stddef.h>
#include <stdint.h>

enum : uint8_t {
  VESC_COMM_FW_VERSION = 0,
  VESC_COMM_GET_VALUES = 4,
  VESC_COMM_FORWARD_CAN = 34,
  VESC_COMM_GET_VALUES_SETUP = 47,
};

// Longest frame this module builds: CAN-forward prefix plus one command id.
enum : size_t { VESC_MAX_COMMAND_FRAME = 8 };

// The COMM_GET_VALUES fields this firmware consumes. The packet carries more
// (avg_id, avg_iq, duty cycle, pid position, controller id); they are skipped
// in place rather than stored.
struct VescValues {
  float tempMosfet;
  float tempMotor;
  float avgMotorCurrent;
  float avgInputCurrent;
  float rpm;
  float inpVoltage;
  float ampHours;
  float ampHoursCharged;
  float wattHours;
  float wattHoursCharged;
  int32_t tachometerAbs;
  uint8_t faultCode;
};

// The COMM_GET_VALUES_SETUP fields used when the controller reports its own
// speed and distance, so wheel size and pole pairs are not needed.
struct VescSetupValues {
  float speedMs;
  float distanceAbsM;
  float voltage;
  float tempFet;
};

uint16_t vescCrc16(const uint8_t *buf, size_t len);

// Builds the payload for a command, optionally wrapped in a CAN forward.
// Returns the payload length, or 0 if `cap` is too small.
size_t vescBuildCommandPayload(uint8_t command, uint8_t canId, uint8_t *out, size_t cap);

// Wraps a payload in start/length/CRC/stop. Returns the frame length, or 0 if
// the payload does not fit `cap`.
size_t vescBuildFrame(const uint8_t *payload, uint8_t payloadLen, uint8_t *out, size_t cap);

// Parsers. Each validates the command id and a minimum length before reading,
// and leaves `out` untouched when it returns false.
bool vescParseValues(const uint8_t *payload, size_t len, VescValues &out);
bool vescParseSetupValues(const uint8_t *payload, size_t len, VescSetupValues &out);
bool vescParseFwVersion(const uint8_t *payload, size_t len, uint8_t &major, uint8_t &minor);
