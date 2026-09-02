// In-memory Preferences for the CameraList dispatch test.
//
// CameraList::save(), remove() and isSaved() are a store round trip, so a
// no-op stub cannot tell a saved camera from a scan result. This keeps one
// namespace of blobs in a map, which is all those three paths need, without
// pulling the ESP-IDF NVS stub and its conflicting FreeRTOS headers into a
// test that runs against the NimBLE double.
#include "Preferences.h"

#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace {

std::map<std::string, std::vector<uint8_t>> g_Store;

}  // namespace

namespace Furble {
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
  return g_Store.erase(key) > 0;
}
size_t Preferences::put(const char *key, const void *value, size_t bytes) {
  const auto *first = static_cast<const uint8_t *>(value);
  g_Store[key] = std::vector<uint8_t>(first, first + bytes);
  return bytes;
}
size_t Preferences::put(const char *, const char *) {
  return 0;
}
size_t Preferences::get(const char *key, void *buf, size_t maxLen) {
  const auto entry = g_Store.find(key);
  if (entry == g_Store.end()) {
    return 0;
  }
  const size_t bytes = std::min(maxLen, entry->second.size());
  memcpy(buf, entry->second.data(), bytes);
  return bytes;
}
bool Preferences::isKey(const char *key) {
  return g_Store.count(key) > 0;
}
size_t Preferences::getBytesLength(const char *key) {
  const auto entry = g_Store.find(key);
  return entry == g_Store.end() ? 0 : entry->second.size();
}
size_t Preferences::freeEntries() {
  return 0;
}
}  // namespace Furble
