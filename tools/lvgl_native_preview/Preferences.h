#pragma once

#include <string>

#include "Arduino.h"

// Selects the optional host persistence file before loadAppSettings() runs.
// An empty path keeps preferences in memory only. Reset removes only the exact
// file supplied by the simulator launcher.
void previewPreferencesConfigure(const char *path, bool reset);

class Preferences {
 public:
  bool begin(const char *name, bool readOnly = false);
  void end();
  void clear();
  bool isKey(const char *key) const;

  bool getBool(const char *key, bool value = false) const;
  uint8_t getUChar(const char *key, uint8_t value = 0) const;
  uint16_t getUShort(const char *key, uint16_t value = 0) const;
  uint32_t getUInt(const char *key, uint32_t value = 0) const;
  float getFloat(const char *key, float value = 0) const;
  String getString(const char *key, const char *value = "") const;
  size_t getBytesLength(const char *key) const;
  size_t getBytes(const char *key, void *value, size_t length) const;

  size_t putBool(const char *key, bool value);
  size_t putUChar(const char *key, uint8_t value);
  size_t putUShort(const char *key, uint16_t value);
  size_t putUInt(const char *key, uint32_t value);
  size_t putFloat(const char *key, float value);
  size_t putString(const char *key, const char *value);
  size_t putBytes(const char *key, const void *value, size_t length);

 private:
  std::string qualifiedKey(const char *key) const;

  std::string namespace_;
  bool readOnly_ = false;
  bool open_ = false;
};
