#include "Preferences.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <vector>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

namespace {

using Bytes = std::vector<uint8_t>;

constexpr char kMagic[] = "CYDPREF1";
constexpr uint32_t kMaximumEntries = 4096;
constexpr uint32_t kMaximumKeyBytes = 1024;
constexpr uint32_t kMaximumValueBytes = 16 * 1024 * 1024;

std::unordered_map<std::string, Bytes> values;
std::filesystem::path persistencePath;
bool configured = false;

template <typename Stream, typename T>
bool readScalar(Stream &in, T &value) {
  return static_cast<bool>(in.read(reinterpret_cast<char *>(&value), sizeof(value)));
}

template <typename Stream, typename T>
void writeScalar(Stream &out, const T &value) {
  out.write(reinterpret_cast<const char *>(&value), sizeof(value));
}

void loadStoreFrom(std::istream &in) {
  values.clear();
  char magic[sizeof(kMagic) - 1] = {};
  uint32_t count = 0;
  if (!in.read(magic, sizeof(magic)) || std::memcmp(magic, kMagic, sizeof(magic)) != 0 ||
      !readScalar(in, count) || count > kMaximumEntries) {
    values.clear();
    return;
  }

  for (uint32_t i = 0; i < count; ++i) {
    uint32_t keyLength = 0;
    uint32_t valueLength = 0;
    if (!readScalar(in, keyLength) || !readScalar(in, valueLength) || keyLength == 0 ||
        keyLength > kMaximumKeyBytes || valueLength > kMaximumValueBytes) {
      values.clear();
      return;
    }
    std::string key(keyLength, '\0');
    Bytes value(valueLength);
    if (!in.read(key.data(), key.size()) ||
        (valueLength && !in.read(reinterpret_cast<char *>(value.data()), value.size()))) {
      values.clear();
      return;
    }
    values.emplace(std::move(key), std::move(value));
  }
}

void saveStoreTo(std::ostream &out) {
  out.write(kMagic, sizeof(kMagic) - 1);
  const uint32_t count = static_cast<uint32_t>(values.size());
  writeScalar(out, count);
  for (const auto &[key, value] : values) {
    const uint32_t keyLength = static_cast<uint32_t>(key.size());
    const uint32_t valueLength = static_cast<uint32_t>(value.size());
    writeScalar(out, keyLength);
    writeScalar(out, valueLength);
    out.write(key.data(), key.size());
    if (!value.empty()) out.write(reinterpret_cast<const char *>(value.data()), value.size());
  }
}

#ifdef __EMSCRIPTEN__
EM_JS(int, browserReadPreferences, (uint8_t *buffer, int capacity), {
  try {
    const encoded = localStorage.getItem('cyd.simulator.preferences.v1');
    if (!encoded) return 0;
    const raw = atob(encoded);
    if (!buffer || capacity < raw.length) return raw.length;
    for (let i = 0; i < raw.length; ++i) HEAPU8[buffer + i] = raw.charCodeAt(i);
    return raw.length;
  } catch (_) {
    return 0;
  }
});

EM_JS(void, browserWritePreferences, (const uint8_t *buffer, int length), {
  try {
    let raw = String();
    const chunk = 0x4000;
    for (let offset = 0; offset < length; offset += chunk) {
      raw += String.fromCharCode.apply(null, HEAPU8.subarray(buffer + offset, buffer + Math.min(length, offset + chunk)));
    }
    localStorage.setItem('cyd.simulator.preferences.v1', btoa(raw));
  } catch (_) {}
});

EM_JS(void, browserClearPreferences, (), {
  try { localStorage.removeItem('cyd.simulator.preferences.v1'); } catch (_) {}
});
#endif

void loadStore() {
  values.clear();
#ifdef __EMSCRIPTEN__
  const int length = browserReadPreferences(nullptr, 0);
  if (length <= 0) return;
  std::string bytes(static_cast<size_t>(length), '\0');
  if (browserReadPreferences(reinterpret_cast<uint8_t *>(bytes.data()), length) != length) return;
  std::istringstream in(bytes, std::ios::binary | std::ios::in);
  loadStoreFrom(in);
#else
  if (persistencePath.empty() || !std::filesystem::exists(persistencePath)) return;
  std::ifstream in(persistencePath, std::ios::binary);
  loadStoreFrom(in);
#endif
}

void saveStore() {
#ifdef __EMSCRIPTEN__
  std::ostringstream out(std::ios::binary | std::ios::out);
  saveStoreTo(out);
  const std::string bytes = out.str();
  browserWritePreferences(reinterpret_cast<const uint8_t *>(bytes.data()), static_cast<int>(bytes.size()));
#else
  if (persistencePath.empty()) return;
  if (!persistencePath.parent_path().empty()) std::filesystem::create_directories(persistencePath.parent_path());
  std::ofstream out(persistencePath, std::ios::binary | std::ios::trunc);
  if (!out) return;
  saveStoreTo(out);
#endif
}

const Bytes *findValue(const std::string &key) {
  const auto found = values.find(key);
  return found == values.end() ? nullptr : &found->second;
}

template <typename T>
T getValue(const std::string &key, T fallback) {
  const Bytes *stored = findValue(key);
  if (!stored || stored->size() != sizeof(T)) return fallback;
  T value;
  std::memcpy(&value, stored->data(), sizeof(value));
  return value;
}

}  // namespace

void previewPreferencesConfigure(const char *path, bool reset) {
  persistencePath = path && path[0] ? std::filesystem::absolute(path) : std::filesystem::path();
#ifdef __EMSCRIPTEN__
  if (reset) browserClearPreferences();
#else
  if (reset && !persistencePath.empty()) {
    std::error_code error;
    std::filesystem::remove(persistencePath, error);
  }
#endif
  configured = true;
  loadStore();
}

bool Preferences::begin(const char *name, bool readOnly) {
  if (!configured) previewPreferencesConfigure(nullptr, false);
  namespace_ = name ? name : "";
  readOnly_ = readOnly;
  open_ = !namespace_.empty();
  return open_;
}

void Preferences::end() {
  namespace_.clear();
  readOnly_ = false;
  open_ = false;
}

std::string Preferences::qualifiedKey(const char *key) const {
  if (!open_ || !key) return {};
  return namespace_ + '\x1f' + key;
}

void Preferences::clear() {
  if (!open_ || readOnly_) return;
  const std::string prefix = namespace_ + '\x1f';
  for (auto it = values.begin(); it != values.end();) {
    if (it->first.rfind(prefix, 0) == 0)
      it = values.erase(it);
    else
      ++it;
  }
  saveStore();
}

bool Preferences::isKey(const char *key) const {
  return findValue(qualifiedKey(key)) != nullptr;
}

bool Preferences::getBool(const char *key, bool value) const {
  return getValue(qualifiedKey(key), value);
}

uint8_t Preferences::getUChar(const char *key, uint8_t value) const {
  return getValue(qualifiedKey(key), value);
}

uint16_t Preferences::getUShort(const char *key, uint16_t value) const {
  return getValue(qualifiedKey(key), value);
}

uint32_t Preferences::getUInt(const char *key, uint32_t value) const {
  return getValue(qualifiedKey(key), value);
}

float Preferences::getFloat(const char *key, float value) const {
  return getValue(qualifiedKey(key), value);
}

String Preferences::getString(const char *key, const char *value) const {
  const Bytes *stored = findValue(qualifiedKey(key));
  if (!stored) return String(value);
  const std::string text(stored->begin(), stored->end());
  return String(text.c_str());
}

size_t Preferences::getBytesLength(const char *key) const {
  const Bytes *stored = findValue(qualifiedKey(key));
  return stored ? stored->size() : 0;
}

size_t Preferences::getBytes(const char *key, void *value, size_t length) const {
  const Bytes *stored = findValue(qualifiedKey(key));
  if (!stored || !value) return 0;
  const size_t copied = std::min(length, stored->size());
  std::memcpy(value, stored->data(), copied);
  return copied;
}

size_t Preferences::putBool(const char *key, bool value) {
  return putBytes(key, &value, sizeof(value));
}

size_t Preferences::putUChar(const char *key, uint8_t value) {
  return putBytes(key, &value, sizeof(value));
}

size_t Preferences::putUShort(const char *key, uint16_t value) {
  return putBytes(key, &value, sizeof(value));
}

size_t Preferences::putUInt(const char *key, uint32_t value) {
  return putBytes(key, &value, sizeof(value));
}

size_t Preferences::putFloat(const char *key, float value) {
  return putBytes(key, &value, sizeof(value));
}

size_t Preferences::putString(const char *key, const char *value) {
  return putBytes(key, value, value ? std::strlen(value) : 0);
}

size_t Preferences::putBytes(const char *key, const void *value, size_t length) {
  if (!open_ || readOnly_ || !key || (!value && length)) return 0;
  const auto *begin = static_cast<const uint8_t *>(value);
  values[qualifiedKey(key)] = length ? Bytes(begin, begin + length) : Bytes();
  saveStore();
  return length;
}
