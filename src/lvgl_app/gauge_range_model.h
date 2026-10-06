#pragma once
#include <cmath>
#include <cstdint>

// No allocation or UI dependency. A range's ceiling is the largest confirmed reading of the last
// kWindowMinutes of riding time, never below its default. A reading above the ceiling pushes it
// up at once (to the exact peak, rounded up to a whole unit), so a dial that is pegged stays
// pegged and nothing visibly jumps. When the old peak ages out of the window the ceiling falls
// back, but only while the reading is low, so a needle is never moved by the shrink.
struct GaugeRangeTracker {
  static constexpr int kWindowMinutes = 15;
  static constexpr uint32_t kMinuteMs = 60000;
  static constexpr uint32_t kMaxGapMs = 1500;  // a longer gap in the telemetry is not ride time

  int ceiling = 0;    // the scale the dial uses; 0 until initialised
  int floorValue = 0; // the default scale: never go below it
  uint32_t smoothMs = 0;  // time constant for slow quantities (efficiency); 0 reads the confirmed value
  bool seeded = false;
  bool started = false;
  float previous = -1;  // previous valid reading, for confirmation
  float filtered = 0;
  uint32_t last = 0;
  uint32_t rideMs = 0;
  int32_t buckets[kWindowMinutes] = {};  // peak per minute of riding, as a ring

  int minuteSlot() const { return static_cast<int>((rideMs / kMinuteMs) % kWindowMinutes); }

  // Carries a peak learned earlier (the saved speed) into the window as if just seen.
  void seed(int value) {
    seeded = true;
    if (value > ceiling) ceiling = value;
    if (value > buckets[minuteSlot()]) buckets[minuteSlot()] = value;
  }

  // Returns true when the ceiling changed. `riding` is whether the vehicle is moving: only moving
  // time ages the window, so a stop does not shrink the scale.
  bool observe(float value, uint32_t now, bool riding, float limit) {
    if (started && now == last) return false;  // the same packet again
    const uint32_t elapsed = started ? now - last : 0;  // also works across millis wrap
    started = true;
    last = now;
    if (elapsed > kMaxGapMs) previous = -1;  // a gap restarts confirmation
    const uint32_t step = elapsed > kMaxGapMs ? 0 : elapsed;
    if (riding && step) {
      const uint32_t before = rideMs / kMinuteMs;
      rideMs += step;
      if (rideMs / kMinuteMs != before) buckets[minuteSlot()] = 0;  // a new minute reuses the oldest slot
    }

    const bool valid = std::isfinite(value) && value >= 0 && value <= limit;
    // One packet never counts: a reading must hold for two samples, so the smaller of the two is used.
    float confirmed = -1;
    if (valid && previous >= 0) confirmed = value < previous ? value : previous;
    previous = valid ? value : -1;
    float reading = confirmed;
    if (confirmed >= 0 && smoothMs) {
      filtered += (confirmed - filtered) * static_cast<float>(step) / static_cast<float>(smoothMs + step);
      reading = filtered;
    }
    if (reading >= 0) {
      const int peak = static_cast<int>(std::ceil(reading));
      if (peak > buckets[minuteSlot()]) buckets[minuteSlot()] = peak;
    }

    int windowPeak = 0;
    for (int i = 0; i < kWindowMinutes; ++i) windowPeak = buckets[i] > windowPeak ? buckets[i] : windowPeak;
    const int target = windowPeak > floorValue ? windowPeak : floorValue;
    if (target > ceiling) { ceiling = target; return true; }
    // Shrink only a worthwhile amount, and only while the reading is well below the new scale.
    if (target < ceiling && reading >= 0 && target * 100 <= ceiling * 85 && reading < target * 0.6F) {
      ceiling = target;
      return true;
    }
    return false;
  }
};
