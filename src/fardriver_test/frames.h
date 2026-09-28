// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdint.h>

namespace fake_fardriver {
constexpr unsigned kFrameBytes = 16;

struct Sample {
  int16_t voltageDeci, currentQuarters, rawRpm, motorC, escC;
  uint8_t soc, gear;
};
constexpr Sample kStationary = {520, 0, 0, 35, 30, 75, 1};
constexpr uint32_t kRideDurationMs = 60000;

inline Sample rideSample(uint32_t elapsedMs) {
  struct Key { uint32_t ms; Sample value; };
  static const Key keys[] = {
      {0, kStationary}, {5000, kStationary},
      {15000, {500, 100, 300, 42, 34, 74, 2}},
      {25000, {490, 160, 600, 55, 42, 72, 3}},
      {35000, {510, 72, 450, 60, 45, 71, 3}},
      {40000, {520, 0, 400, 57, 42, 71, 3}},
      {50000, {530, -40, 100, 48, 37, 72, 2}},
      {55000, kStationary}, {kRideDurationMs, kStationary},
  };
  const uint32_t t = elapsedMs % kRideDurationMs;
  unsigned i = 0;
  while (i + 1 < sizeof(keys) / sizeof(keys[0]) && t >= keys[i + 1].ms) i++;
  const Key &a = keys[i], &b = keys[i + 1];
  auto mix = [&](int start, int end) -> int16_t {
    return start + (int32_t(end - start) * int32_t(t - a.ms)) / int32_t(b.ms - a.ms);
  };
  return {mix(a.value.voltageDeci, b.value.voltageDeci),
          mix(a.value.currentQuarters, b.value.currentQuarters),
          mix(a.value.rawRpm, b.value.rawRpm), mix(a.value.motorC, b.value.motorC),
          mix(a.value.escC, b.value.escC), uint8_t(mix(a.value.soc, b.value.soc)), a.value.gear};
}

inline void write16(uint8_t *destination, int16_t value) {
  const uint16_t bits = static_cast<uint16_t>(value);
  destination[0] = bits & 255;
  destination[1] = bits >> 8;
}

// Synthetic fixtures for KAJO's current FarDriver decoder, not a claim of
// compatibility with every real controller. Four frame IDs map to E2/E8/D6/F4.
inline void makeFrame(unsigned sequence, uint8_t (&frame)[kFrameBytes], const Sample &sample = kStationary) {
  for (auto &byte : frame) byte = 0;
  frame[0] = 0xAA;
  switch (sequence % 4) {
    case 0:  // E2: gear and electrical RPM / 4
      frame[1] = 0x80;
      frame[2] = (sample.gear - 1) << 2;
      write16(frame + 8, sample.rawRpm);
      break;
    case 1:  // E8: decivolts and signed quarter-amps
      frame[1] = 0x81;
      write16(frame + 2, sample.voltageDeci);
      write16(frame + 6, sample.currentQuarters);
      break;
    case 2:  // D6: 30 C controller temperature at payload offset 10
      frame[1] = 0x80 | 51;
      write16(frame + 12, sample.escC);
      break;
    case 3:  // F4: 35 C motor temperature, 75% charge
      frame[1] = 0x80 | 53;
      write16(frame + 2, sample.motorC);
      frame[5] = sample.soc;
      break;
  }
  uint16_t crc = 0x7F3C;
  for (unsigned i = 0; i < kFrameBytes - 2; i++) {
    crc ^= frame[i];
    for (unsigned bit = 0; bit < 8; bit++)
      crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : crc >> 1;
  }
  frame[14] = crc & 0xFF;
  frame[15] = crc >> 8;
}
}  // namespace fake_fardriver
