// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stddef.h>
#include <stdint.h>

// Synthetic replies for KAJO's current VESC decoder, not a claim of
// compatibility with every VESC firmware. Deliberately independent of
// src/lvgl_app/vesc_protocol.cpp: a sender that shared the receiver's encoder
// would agree with it even if both were wrong.
namespace fake_vesc {

enum : uint8_t { kCommandFwVersion = 0, kCommandGetValues = 4, kCommandForwardCan = 34, kCommandGetValuesSetup = 47 };

// Instantaneous readings. Everything cumulative (charge, energy, distance,
// tachometer) lives in Totals, because a real controller counts those up and
// never replays them.
struct Sample {
  int16_t tempFetDeci, tempMotorDeci, voltageDeci;
  int32_t motorCurrentCenti, inputCurrentCenti, erpm, speedMmPerS;
  uint8_t fault;
};

struct Totals {
  int32_t ampHours1e4, ampHoursCharged1e4, wattHours1e4, wattHoursCharged1e4;
  int32_t tachometerAbs, tripMm, odometerMm;
};

constexpr Sample kStationary = {300, 350, 5200, 0, 0, 0, 0, 0};
constexpr uint32_t kRideDurationMs = 60000;

inline Sample rideSample(uint32_t elapsedMs) {
  struct Key { uint32_t ms; Sample value; };
  static const Key keys[] = {
      {0, kStationary}, {5000, kStationary},
      {15000, {340, 420, 5000, 2500, 1800, 4200, 6900, 0}},
      {25000, {420, 550, 4900, 6000, 4400, 8400, 13900, 0}},
      {35000, {450, 600, 5100, 1800, 1300, 6300, 10400, 0}},
      {40000, {420, 570, 5200, 0, 0, 5600, 9200, 0}},
      {50000, {370, 480, 5300, -1000, -700, 1400, 2300, 0}},
      {55000, kStationary}, {kRideDurationMs, kStationary},
  };
  const uint32_t t = elapsedMs % kRideDurationMs;
  unsigned i = 0;
  while (i + 1 < sizeof(keys) / sizeof(keys[0]) && t >= keys[i + 1].ms) i++;
  const Key &a = keys[i], &b = keys[i + 1];
  auto mix = [&](int32_t start, int32_t end) -> int32_t {
    return start + int32_t((int64_t(end - start) * int64_t(t - a.ms)) / int32_t(b.ms - a.ms));
  };
  Sample out = a.value;
  out.tempFetDeci = int16_t(mix(a.value.tempFetDeci, b.value.tempFetDeci));
  out.tempMotorDeci = int16_t(mix(a.value.tempMotorDeci, b.value.tempMotorDeci));
  out.voltageDeci = int16_t(mix(a.value.voltageDeci, b.value.voltageDeci));
  out.motorCurrentCenti = mix(a.value.motorCurrentCenti, b.value.motorCurrentCenti);
  out.inputCurrentCenti = mix(a.value.inputCurrentCenti, b.value.inputCurrentCenti);
  out.erpm = mix(a.value.erpm, b.value.erpm);
  out.speedMmPerS = mix(a.value.speedMmPerS, b.value.speedMmPerS);
  return out;
}

// CRC-16/XMODEM over the payload only: poly 0x1021, zero seed, no reflection.
inline uint16_t crc16(const uint8_t *data, size_t length) {
  uint16_t crc = 0;
  for (size_t i = 0; i < length; i++) {
    crc ^= uint16_t(uint16_t(data[i]) << 8);
    for (unsigned bit = 0; bit < 8; bit++) crc = (crc & 0x8000u) ? uint16_t((crc << 1) ^ 0x1021) : uint16_t(crc << 1);
  }
  return crc;
}

// VESC integers are big-endian on the wire.
inline void put16(uint8_t *out, size_t &i, int16_t value) {
  out[i++] = uint8_t(uint16_t(value) >> 8);
  out[i++] = uint8_t(uint16_t(value) & 0xFF);
}
inline void put32(uint8_t *out, size_t &i, int32_t value) {
  const uint32_t bits = uint32_t(value);
  out[i++] = uint8_t(bits >> 24);
  out[i++] = uint8_t(bits >> 16);
  out[i++] = uint8_t(bits >> 8);
  out[i++] = uint8_t(bits);
}

// Wraps a payload as 0x02 | length | payload | crc_hi | crc_lo | 0x03.
inline size_t frame(const uint8_t *payload, size_t length, uint8_t *out, size_t capacity) {
  if (length == 0 || length > 255 || capacity < length + 5) return 0;
  size_t i = 0;
  out[i++] = 2;
  out[i++] = uint8_t(length);
  for (size_t n = 0; n < length; n++) out[i++] = payload[n];
  const uint16_t crc = crc16(payload, length);
  out[i++] = uint8_t(crc >> 8);
  out[i++] = uint8_t(crc & 0xFF);
  out[i++] = 3;
  return i;
}

inline size_t makeValuesFrame(uint8_t *out, size_t capacity, const Sample &sample, const Totals &totals) {
  uint8_t payload[59];
  size_t i = 0;
  payload[i++] = kCommandGetValues;
  put16(payload, i, sample.tempFetDeci);
  put16(payload, i, sample.tempMotorDeci);
  put32(payload, i, sample.motorCurrentCenti);
  put32(payload, i, sample.inputCurrentCenti);
  put32(payload, i, 0);  // avg_id
  put32(payload, i, 0);  // avg_iq
  put16(payload, i, int16_t(sample.erpm / 100));  // duty x1000, unread but plausible
  put32(payload, i, sample.erpm);
  put16(payload, i, sample.voltageDeci);
  put32(payload, i, totals.ampHours1e4);
  put32(payload, i, totals.ampHoursCharged1e4);
  put32(payload, i, totals.wattHours1e4);
  put32(payload, i, totals.wattHoursCharged1e4);
  put32(payload, i, totals.tachometerAbs);  // signed count; the receiver skips it
  put32(payload, i, totals.tachometerAbs);
  payload[i++] = sample.fault;
  put32(payload, i, 0);  // pid position
  payload[i++] = 0;      // controller id
  return frame(payload, i, out, capacity);
}

inline size_t makeSetupFrame(uint8_t *out, size_t capacity, const Sample &sample, const Totals &totals) {
  uint8_t payload[58];
  size_t i = 0;
  payload[i++] = kCommandGetValuesSetup;
  put16(payload, i, sample.tempFetDeci);
  put16(payload, i, sample.tempMotorDeci);
  put32(payload, i, sample.motorCurrentCenti);
  put32(payload, i, sample.inputCurrentCenti);
  put16(payload, i, int16_t(sample.erpm / 100));
  put32(payload, i, sample.erpm);
  put32(payload, i, sample.speedMmPerS);  // x1000 gives m/s
  put16(payload, i, sample.voltageDeci);
  put16(payload, i, 0);  // battery level
  put32(payload, i, totals.ampHours1e4);
  put32(payload, i, totals.ampHoursCharged1e4);
  put32(payload, i, totals.wattHours1e4);
  put32(payload, i, totals.wattHoursCharged1e4);
  put32(payload, i, totals.tripMm);
  put32(payload, i, totals.odometerMm);
  put32(payload, i, 0);  // pid position
  payload[i++] = sample.fault;
  payload[i++] = 0;  // controller id
  payload[i++] = 1;  // VESCs on the CAN bus
  return frame(payload, i, out, capacity);
}

inline size_t makeFwVersionFrame(uint8_t *out, size_t capacity, uint8_t major = 6, uint8_t minor = 2) {
  uint8_t payload[16];
  size_t i = 0;
  payload[i++] = kCommandFwVersion;
  payload[i++] = major;
  payload[i++] = minor;
  for (const char *name = "KAJO-TEST"; *name; name++) payload[i++] = uint8_t(*name);
  payload[i++] = 0;
  return frame(payload, i, out, capacity);
}

}  // namespace fake_vesc
