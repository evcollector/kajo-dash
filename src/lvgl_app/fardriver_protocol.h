#pragma once

// FarDriver BLE wire protocol: frame recovery, CRC and the telemetry decoder.
// Deliberately free of Arduino and NimBLE dependencies so the same code runs on
// the device and in the host tests against the bench sender's frames.
//
// The controller streams 16-byte frames of its own accord:
//
//   0xAA | id | payload[12] | crc_lo | crc_hi
//
// The low six bits of id index a table of controller memory addresses; the
// payload is whatever lives at that address.

#include <stddef.h>
#include <stdint.h>

enum : size_t { FARDRIVER_FRAME_LENGTH = 16 };

// Values decoded from the controller's own broadcast. Nothing is requested: the
// controller rotates through its memory addresses unprompted, so each field is
// refreshed whenever the frame carrying it comes round again.
//
// Raw as reported. Turning rawRpm into a road speed needs the wheel, gearing
// and pole count, which live with the rest of the drivetrain maths rather than
// in the transport.
struct FarDriverTelemetry {
  uint32_t updatedAtMs;    // 0 until a frame this struct understands has arrived
  float voltage;           // V
  float current;           // A, negative while regenerating
  int16_t rawRpm;          // x4 gives the electrical rate
  uint8_t gear;            // 1..4 as reported, 0 while unknown
  int16_t motorTemp;       // degrees C
  int16_t controllerTemp;  // degrees C
  int8_t socPercent;       // -1 while unknown
  bool haveElectrical;     // the voltage/current frame has been seen
  bool haveMotion;         // the rpm/gear frame has been seen
  bool haveMotorTemp;      // the motor-temperature frame has been seen
  bool haveControllerTemp; // the controller-temperature frame has been seen
  bool haveSoc;            // a valid controller state-of-charge was seen
};

// Checks the trailing little-endian CRC over everything before it.
bool farDriverCrcValid(const uint8_t *data, size_t length);

// Applies one CRC-checked frame to telemetry. Returns true when the frame
// carried a reading this display uses, so the caller can stamp updatedAtMs; a
// frame for another address, or one whose values are out of bounds, returns
// false and leaves telemetry untouched.
bool farDriverDecodeFrame(const uint8_t *frame, FarDriverTelemetry &telemetry);

// A BLE notification is not a frame boundary: one may hold part of a frame, a
// whole frame, or the tail of one plus the head of the next depending on the
// negotiated MTU. Bytes are buffered and frames recovered by anchoring on the
// magic byte and confirming the checksum.
class FarDriverFrameStream {
 public:
  struct Counts {
    uint32_t discardedBytes;  // thrown away while resynchronising or on overflow
    uint32_t crcFailures;     // windows that looked like a frame but failed the CRC
  };
  using FrameFn = void (*)(const uint8_t *frame, void *context);

  // Appends a notification and hands every complete, valid frame to onFrame.
  // Anything shorter than a frame stays for the next call to complete.
  Counts push(const uint8_t *data, size_t length, FrameFn onFrame, void *context);
  void reset() { used_ = 0; }

 private:
  static constexpr size_t kCapacity = 256;  // comfortably over any notification size
  uint8_t buffer_[kCapacity];
  size_t used_ = 0;
};
