#pragma once
#include <cmath>
#include <cstdint>

// No allocation or UI dependency. Ceilings only grow until explicitly reset.
struct GaugeRangeTracker {
  int ceiling = 0;
  uint32_t since = 0, last = 0;
  unsigned samples = 0;
  static int readableCeiling(float value) {
    static const int steps[] = {10, 15, 20, 25, 30, 40, 50, 60, 75, 80, 100};
    int decade = 1;
    while (value > 100 * decade && decade < 10000) decade *= 10;
    for (int step : steps) if (step * decade >= value) return step * decade;
    return 100 * decade;
  }
  bool observe(float value, uint32_t now, uint32_t dwell, float limit) {
    if (!std::isfinite(value) || value < 0 || value > limit || value < ceiling * .9F) {
      samples = 0; return false;
    }
    if (samples && now == last) return false;
    if (!samples || now - last > 1500) { since = now; samples = 0; }
    last = now; ++samples;
    // Large overruns still require corroboration: never learn one bad packet.
    const uint32_t required = value > ceiling * 1.3F && dwell < 3000 ? 100 : dwell;
    if (samples < 2 || now - since < required) return false;
    const int next = readableCeiling(value * 1.2F);
    samples = 0;
    if (next <= ceiling) return false;
    ceiling = next; return true;
  }
};
