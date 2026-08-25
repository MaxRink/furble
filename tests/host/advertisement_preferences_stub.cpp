#include "Preferences.h"

#include <algorithm>
#include <cstring>
#include <iterator>
#include <map>
#include <string>
#include <vector>

namespace Furble {
namespace {
std::map<std::string, std::vector<uint8_t>> g_Storage;
}

Preferences::Preferences() : _handle(0), _started(false), _readOnly(false) {}
Preferences::~Preferences() = default;
bool Preferences::begin(const char *name, bool readOnly, const char *) {
  _handle = reinterpret_cast<uintptr_t>(name);
  _started = name != nullptr;
  _readOnly = readOnly;
  return true;
}
void Preferences::end() {
  _started = false;
  _handle = 0;
}
bool Preferences::clear() {
  if (!_started || _readOnly)
    return false;
  const std::string prefix = std::to_string(_handle) + ":";
  for (auto it = g_Storage.begin(); it != g_Storage.end();) {
    it = it->first.rfind(prefix, 0) == 0 ? g_Storage.erase(it) : std::next(it);
  }
  return true;
}
bool Preferences::remove(const char *key) {
  if (!_started || _readOnly || !validNvsKey(key))
    return false;
  return g_Storage.erase(std::to_string(_handle) + ":" + key) != 0;
}
size_t Preferences::put(const char *key, const void *value, size_t bytes) {
  if (!_started || _readOnly || !validNvsKey(key) || value == nullptr)
    return 0;
  const auto *begin = static_cast<const uint8_t *>(value);
  g_Storage[std::to_string(_handle) + ":" + key] = {begin, begin + bytes};
  return bytes;
}
size_t Preferences::put(const char *key, const char *value) {
  return value == nullptr ? 0 : put(key, value, std::strlen(value) + 1);
}
size_t Preferences::get(const char *key, void *value, size_t bytes) {
  if (!_started || !validNvsKey(key) || value == nullptr)
    return 0;
  const auto found = g_Storage.find(std::to_string(_handle) + ":" + key);
  if (found == g_Storage.end())
    return 0;
  const size_t copied = std::min(bytes, found->second.size());
  std::memcpy(value, found->second.data(), copied);
  return copied;
}
bool Preferences::isKey(const char *key) {
  return _started && validNvsKey(key)
         && g_Storage.find(std::to_string(_handle) + ":" + key) != g_Storage.end();
}
size_t Preferences::getBytesLength(const char *key) {
  if (!_started || !validNvsKey(key))
    return 0;
  const auto found = g_Storage.find(std::to_string(_handle) + ":" + key);
  return found == g_Storage.end() ? 0 : found->second.size();
}
size_t Preferences::freeEntries() {
  return 0;
}
}  // namespace Furble
