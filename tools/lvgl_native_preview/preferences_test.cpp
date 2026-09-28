#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>

#include "Preferences.h"

namespace {

bool check(bool condition, const char *message) {
  if (condition) return true;
  std::cerr << "preferences test failed: " << message << '\n';
  return false;
}

}  // namespace

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "usage: cyd_preferences_test <state-file>\n";
    return 2;
  }

  const std::filesystem::path stateFile = std::filesystem::absolute(argv[1]);
  previewPreferencesConfigure(stateFile.string().c_str(), true);

  Preferences app;
  if (!check(app.begin("app", false), "open writable namespace")) return 1;
  const std::array<uint8_t, 5> profile = {3, 1, 4, 1, 5};
  if (!check(app.putBool("configured", true) == sizeof(bool), "write bool") ||
      !check(app.putUChar("accent", 7) == sizeof(uint8_t), "write byte") ||
      !check(app.putUShort("wheel", 254) == sizeof(uint16_t), "write ushort") ||
      !check(app.putUInt("baud", 115200) == sizeof(uint32_t), "write uint") ||
      !check(app.putFloat("capacity", 18.5F) == sizeof(float), "write float") ||
      !check(app.putString("vehicle", "Bench board") == 11, "write string") ||
      !check(app.putBytes("profile", profile.data(), profile.size()) == profile.size(), "write blob")) {
    return 1;
  }
  app.end();

  Preferences other;
  if (!check(other.begin("other", false), "open second namespace") ||
      !check(other.putUInt("sentinel", 42) == sizeof(uint32_t), "write second namespace")) {
    return 1;
  }
  other.end();

  // Reload from disk, rather than relying on the current process's map.
  previewPreferencesConfigure(stateFile.string().c_str(), false);
  if (!check(app.begin("app", true), "reopen read-only namespace")) return 1;
  std::array<uint8_t, 5> restored = {};
  if (!check(app.getBool("configured", false), "read bool") ||
      !check(app.getUChar("accent", 0) == 7, "read byte") ||
      !check(app.getUShort("wheel", 0) == 254, "read ushort") ||
      !check(app.getUInt("baud", 0) == 115200, "read uint") ||
      !check(app.getFloat("capacity", 0.0F) == 18.5F, "read float") ||
      !check(std::strcmp(app.getString("vehicle", "").c_str(), "Bench board") == 0, "read string") ||
      !check(app.getBytesLength("profile") == profile.size(), "read blob length") ||
      !check(app.getBytes("profile", restored.data(), restored.size()) == restored.size(), "read blob") ||
      !check(restored == profile, "blob contents")) {
    return 1;
  }
  if (!check(app.putUInt("blocked", 1) == 0, "read-only namespace rejects writes")) return 1;
  app.end();

  if (!check(app.begin("app", false), "reopen namespace for clear")) return 1;
  app.clear();
  if (!check(!app.isKey("configured"), "clear current namespace")) return 1;
  app.end();
  if (!check(other.begin("other", true), "reopen preserved namespace") ||
      !check(other.getUInt("sentinel", 0) == 42, "clear preserves other namespace")) {
    return 1;
  }

  std::cout << "host preferences persistence passed: " << stateFile.string() << '\n';
  return 0;
}
