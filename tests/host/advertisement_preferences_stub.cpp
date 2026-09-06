// In-memory stand-in for NVS.
//
// The original stub answered every read with absent. CameraList now persists
// stable camera ids and migrates a v1 index in place, so the host suites need
// storage that actually reads back what was written.

#include <algorithm>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "Preferences.h"
#include "advertisement_preferences_stub.h"

namespace {

std::map<std::string, std::vector<uint8_t>> g_Store;

}  // namespace

namespace Furble {

namespace Host {

void clearPreferences(void) {
  g_Store.clear();
}

size_t preferencesKeyCount(void) {
  return g_Store.size();
}

}  // namespace Host

Preferences::Preferences() : _handle(0), _started(false), _readOnly(false) {}
Preferences::~Preferences() = default;

bool Preferences::begin(const char *, bool readOnly, const char *) {
  _started = true;
  _readOnly = readOnly;
  return true;
}

void Preferences::end() {
  _started = false;
}

bool Preferences::clear() {
  g_Store.clear();
  return true;
}

bool Preferences::remove(const char *key) {
  if (key == nullptr) {
    return false;
  }
  return g_Store.erase(key) > 0;
}

size_t Preferences::put(const char *key, const void *value, size_t bytes) {
  if ((key == nullptr) || ((value == nullptr) && (bytes != 0))) {
    return 0;
  }
  const auto *data = static_cast<const uint8_t *>(value);
  g_Store[key].assign(data, data + bytes);
  return bytes;
}

size_t Preferences::put(const char *key, const char *value) {
  if ((key == nullptr) || (value == nullptr)) {
    return 0;
  }
  return put(key, value, std::strlen(value) + 1);
}

size_t Preferences::get(const char *key, void *buf, size_t maxLen) {
  if (key == nullptr) {
    return 0;
  }
  const auto found = g_Store.find(key);
  if (found == g_Store.end()) {
    return 0;
  }
  const size_t bytes = std::min(maxLen, found->second.size());
  if ((buf != nullptr) && (bytes != 0)) {
    std::memcpy(buf, found->second.data(), bytes);
  }
  return bytes;
}

bool Preferences::isKey(const char *key) {
  return (key != nullptr) && (g_Store.count(key) != 0);
}

size_t Preferences::getBytesLength(const char *key) {
  if (key == nullptr) {
    return 0;
  }
  const auto found = g_Store.find(key);
  return (found == g_Store.end()) ? 0 : found->second.size();
}

size_t Preferences::freeEntries() {
  return 1024;
}

}  // namespace Furble
