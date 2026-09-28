#include "fardriver_protocol.h"

#include <string.h>

namespace {

constexpr uint8_t kFrameMagic = 0xAA;

// Frame ids below 0x37 index this table to reach a controller memory address.
// Two independent reverse-engineering efforts publish the same 55 entries; they
// disagree only at index 3 (0x00 vs 0xE4), which is not an address decoded here.
const uint8_t kFlashReadAddr[55] = {
    0xE2, 0xE8, 0xEE, 0x00, 0x06, 0x0C, 0x12, 0xE2, 0xE8, 0xEE, 0x18, 0x1E, 0x24, 0x2A,
    0xE2, 0xE8, 0xEE, 0x30, 0x5D, 0x63, 0x69, 0xE2, 0xE8, 0xEE, 0x7C, 0x82, 0x88, 0x8E,
    0xE2, 0xE8, 0xEE, 0x94, 0x9A, 0xA0, 0xA6, 0xE2, 0xE8, 0xEE, 0xAC, 0xB2, 0xB8, 0xBE,
    0xE2, 0xE8, 0xEE, 0xC4, 0xCA, 0xD0, 0xE2, 0xE8, 0xEE, 0xD6, 0xDC, 0xF4, 0xFA,
};

inline uint16_t u16le(const uint8_t *d, size_t i) {
  return static_cast<uint16_t>(d[i] | (static_cast<uint16_t>(d[i + 1]) << 8));
}
inline int16_t s16le(const uint8_t *d, size_t i) { return static_cast<int16_t>(u16le(d, i)); }

}  // namespace

// The two-table form documented for these controllers seeds a = 0x3C, b = 0x7F
// and covers every byte but the trailing pair, which it stores low byte first.
// That is the standard reflected CRC-16 (poly 0xA001) with a non-standard seed,
// so the bitwise loop below is equivalent and needs no tables.
bool farDriverCrcValid(const uint8_t *data, size_t length) {
  if (!data || length < 4) return false;
  uint16_t crc = 0x7F3C;
  for (size_t i = 0; i < length - 2; i++) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; bit++) {
      crc = (crc & 1U) ? static_cast<uint16_t>((crc >> 1) ^ 0xA001U)
                       : static_cast<uint16_t>(crc >> 1);
    }
  }
  return crc == static_cast<uint16_t>((static_cast<uint16_t>(data[length - 1]) << 8) | data[length - 2]);
}

// Bounds are deliberately wider than any bike this display is likely to meet.
// They are here to reject a frame whose layout is not what we assume, not to
// second-guess the controller: a reading that survives is passed through as-is.
bool farDriverDecodeFrame(const uint8_t *frame, FarDriverTelemetry &telemetry) {
  const uint8_t id = frame[1] & 0x3F;
  if (id >= sizeof(kFlashReadAddr)) return false;
  const uint8_t *d = frame + 2;  // 12 payload bytes

  switch (kFlashReadAddr[id]) {
    case 0xE8: {  // electrical
      const float volts = u16le(d, 0) / 10.0F;
      const float amps = s16le(d, 4) / 4.0F;
      if (volts > 200.0F || amps < -400.0F || amps > 400.0F) return false;
      telemetry.voltage = volts;
      telemetry.current = amps;
      telemetry.haveElectrical = true;
      return true;
    }
    case 0xE2: {  // motion
      telemetry.rawRpm = s16le(d, 6);
      telemetry.gear = static_cast<uint8_t>(((d[0] >> 2) & 0x03) + 1);
      telemetry.haveMotion = true;
      return true;
    }
    case 0xD6: {  // controller temperature
      const int16_t temp = s16le(d, 10);
      if (temp < -40 || temp > 200) return false;
      telemetry.controllerTemp = temp;
      telemetry.haveControllerTemp = true;
      return true;
    }
    case 0xF4: {  // motor temperature and state of charge
      const int16_t temp = s16le(d, 0);
      const uint8_t soc = d[3];
      if (temp >= -40 && temp <= 250) {
        telemetry.motorTemp = temp;
        telemetry.haveMotorTemp = true;
      }
      if (soc <= 100) {
        telemetry.socPercent = static_cast<int8_t>(soc);
        telemetry.haveSoc = true;
      }
      return true;
    }
    default:
      return false;  // an address this display has no use for
  }
}

FarDriverFrameStream::Counts FarDriverFrameStream::push(const uint8_t *data, size_t length, FrameFn onFrame,
                                                        void *context) {
  Counts counts = {0, 0};
  if (!data || length == 0) return counts;

  if (length >= kCapacity) {  // keep the newest bytes; the rest are already lost
    counts.discardedBytes = static_cast<uint32_t>(used_ + length - kCapacity);
    memcpy(buffer_, data + length - kCapacity, kCapacity);
    used_ = kCapacity;
  } else {
    if (used_ + length > kCapacity) {
      const size_t drop = used_ + length - kCapacity;
      counts.discardedBytes = static_cast<uint32_t>(drop);
      memmove(buffer_, buffer_ + drop, used_ - drop);
      used_ -= drop;
    }
    memcpy(buffer_ + used_, data, length);
    used_ += length;
  }

  size_t scan = 0;
  while (used_ - scan >= FARDRIVER_FRAME_LENGTH) {
    const uint8_t *frame = buffer_ + scan;
    if (frame[0] != kFrameMagic) {
      scan++;
      counts.discardedBytes++;
      continue;
    }
    if (!farDriverCrcValid(frame, FARDRIVER_FRAME_LENGTH)) {
      // Magic in the right place but the checksum disagrees: either a stray
      // 0xAA inside a payload, or the frame layout is not what we assume.
      scan++;
      counts.discardedBytes++;
      counts.crcFailures++;
      continue;
    }
    onFrame(frame, context);
    scan += FARDRIVER_FRAME_LENGTH;
  }
  if (scan > 0 && scan < used_) memmove(buffer_, buffer_ + scan, used_ - scan);
  used_ -= scan;
  return counts;
}
