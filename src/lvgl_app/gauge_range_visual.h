#pragma once

#include <cstdint>

// The learned ceiling changes in readable steps. Move only its presentation
// scale between those steps so a steady reading does not jump across a dial.
struct GaugeRangeVisual {
  static constexpr uint32_t kTransitionMs = 900;

  int shown = 0;
  int from = 0;
  int target = 0;
  uint32_t startedAt = 0;

  void reset(int ceiling) {
    shown = from = target = ceiling > 0 ? ceiling : 1;
    startedAt = 0;
  }

  int advance(int ceiling, uint32_t now) {
    if (ceiling < 1) ceiling = 1;
    if (!shown || ceiling < target) {
      // Resets, manual limits and source changes must take effect immediately.
      reset(ceiling);
      return shown;
    }
    if (ceiling > target) {
      from = shown;
      target = ceiling;
      startedAt = now;
    }
    if (shown == target) return shown;

    const uint32_t elapsed = now - startedAt;  // also works across millis wrap
    if (elapsed >= kTransitionMs) {
      shown = target;
    } else {
      const float t = static_cast<float>(elapsed) / kTransitionMs;
      const float eased = t * t * (3.0F - 2.0F * t);
      const int next = from + static_cast<int>((target - from) * eased + 0.5F);
      if (next > shown) shown = next;
    }
    return shown;
  }
};

// Short damping for moving indicators. Readout text continues to use the live
// sample; a long gap or a power sign change does not leave a stale indicator.
struct GaugeValueVisual {
  static constexpr uint32_t kResponseMs = 160;

  float shown = 0;
  uint32_t lastAt = 0;
  bool ready = false;

  void reset(float value, uint32_t now) {
    shown = value;
    lastAt = now;
    ready = true;
  }

  float advance(float value, uint32_t now, bool resetAtSignChange = false) {
    if (!ready || (resetAtSignChange && ((value < 0 && shown > 0) || (value > 0 && shown < 0)))) {
      reset(resetAtSignChange && ready ? 0.0F : value, now);
    }
    const uint32_t elapsed = now - lastAt;
    lastAt = now;
    if (elapsed >= 600) {
      shown = value;
    } else if (elapsed) {
      const float weight = static_cast<float>(elapsed) / (kResponseMs + elapsed);
      shown += (value - shown) * weight;
      if (shown - value < 0.05F && value - shown < 0.05F) shown = value;
    }
    return shown;
  }
};
