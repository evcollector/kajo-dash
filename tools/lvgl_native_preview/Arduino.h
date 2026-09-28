#pragma once

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
#include <algorithm>
#include <string>

template <typename T>
constexpr T constrain(T value, T low, T high) {
  return value < low ? low : (value > high ? high : value);
}

template <typename T, typename U, typename V>
constexpr auto constrain(T value, U low, V high) {
  using R = decltype(value + low + high);
  const R v = value;
  const R lo = low;
  const R hi = high;
  return v < lo ? lo : (v > hi ? hi : v);
}

template <typename T>
constexpr const T &min(const T &a, const T &b) {
  return a < b ? a : b;
}

template <typename T>
constexpr const T &max(const T &a, const T &b) {
  return a > b ? a : b;
}

class String {
 public:
  String(const char *value = "") : value_(value ? value : "") {}
  const char *c_str() const { return value_.c_str(); }
  void toCharArray(char *out, size_t size) const {
    if (!size) return;
    strncpy(out, value_.c_str(), size - 1);
    out[size - 1] = '\0';
  }

 private:
  std::string value_;
};

extern "C" {
#endif

uint32_t millis(void);

#ifdef __cplusplus
}

inline long map(long value, long fromLow, long fromHigh, long toLow, long toHigh) {
  if (fromHigh == fromLow) return toLow;
  return (value - fromLow) * (toHigh - toLow) / (fromHigh - fromLow) + toLow;
}

inline float radians(float degrees) {
  return degrees * 0.01745329251994329577F;
}

inline char *dtostrf(double value, signed char width, unsigned char precision, char *out) {
  (void)width;
  snprintf(out, 48, "%.*f", static_cast<int>(precision), value);
  return out;
}
#endif

#define PROGMEM
#define pgm_read_ptr(address) (*(address))
#define memcpy_P(dest, src, size) memcpy((dest), (src), (size))
