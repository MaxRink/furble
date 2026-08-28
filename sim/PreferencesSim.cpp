#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "Preferences.h"

namespace Furble {
namespace {

struct Value {
  std::vector<uint8_t> bytes;
  bool string_value = false;
};

std::map<std::string, Value> values;
std::mutex values_mutex;
bool loaded = false;

std::string preferencesPath(void) {
  const char *path = std::getenv("FURBLE_SIM_PREFS");
  return path == nullptr ? ".pio/furble-sim-preferences.bin" : path;
}

void loadValues(void) {
  if (loaded) {
    return;
  }
  loaded = true;

  std::ifstream file(preferencesPath(), std::ios::binary);
  uint32_t count = 0;
  if (!file || !file.read(reinterpret_cast<char *>(&count), sizeof(count))) {
    return;
  }

  for (uint32_t i = 0; i < count; ++i) {
    uint32_t key_size = 0;
    uint32_t value_size = 0;
    uint8_t string_value = 0;
    if (!file.read(reinterpret_cast<char *>(&key_size), sizeof(key_size))
        || !file.read(reinterpret_cast<char *>(&value_size), sizeof(value_size))
        || !file.read(reinterpret_cast<char *>(&string_value), sizeof(string_value))) {
      values.clear();
      return;
    }

    std::string key(key_size, '\0');
    Value value;
    value.bytes.resize(value_size);
    value.string_value = string_value != 0;
    if (!file.read(key.data(), key.size())
        || (value_size > 0
            && !file.read(reinterpret_cast<char *>(value.bytes.data()), value_size))) {
      values.clear();
      return;
    }
    values.emplace(std::move(key), std::move(value));
  }
}

void saveValues(void) {
  const std::string path = preferencesPath();
  const size_t separator = path.find_last_of("/\\");
  if (separator != std::string::npos) {
    const std::string directory = path.substr(0, separator);
    std::string command = "mkdir -p \"" + directory + "\"";
    std::system(command.c_str());
  }

  const std::string temporaryPath = path + ".tmp";
  std::ofstream file(temporaryPath, std::ios::binary | std::ios::trunc);
  const uint32_t count = static_cast<uint32_t>(values.size());
  file.write(reinterpret_cast<const char *>(&count), sizeof(count));
  for (const auto &entry : values) {
    const uint32_t key_size = static_cast<uint32_t>(entry.first.size());
    const uint32_t value_size = static_cast<uint32_t>(entry.second.bytes.size());
    const uint8_t string_value = entry.second.string_value ? 1 : 0;
    file.write(reinterpret_cast<const char *>(&key_size), sizeof(key_size));
    file.write(reinterpret_cast<const char *>(&value_size), sizeof(value_size));
    file.write(reinterpret_cast<const char *>(&string_value), sizeof(string_value));
    file.write(entry.first.data(), entry.first.size());
    if (value_size > 0) {
      file.write(reinterpret_cast<const char *>(entry.second.bytes.data()), value_size);
    }
  }
  file.close();
  std::rename(temporaryPath.c_str(), path.c_str());
}

std::string fullKey(uint32_t handle, const char *key) {
  return std::to_string(handle) + ":" + key;
}

uint32_t handleFor(const char *name) {
  static std::map<std::string, uint32_t> handles;
  static uint32_t next = 1;
  const auto found = handles.find(name == nullptr ? "" : name);
  if (found != handles.end()) {
    return found->second;
  }
  const uint32_t handle = next++;
  handles.emplace(name == nullptr ? "" : name, handle);
  return handle;
}

template <typename T>
size_t putValue(uint32_t handle, const char *key, const T &value) {
  if (key == nullptr) {
    return 0;
  }
  std::lock_guard<std::mutex> lock(values_mutex);
  loadValues();
  Value stored;
  stored.bytes.resize(sizeof(T));
  std::memcpy(stored.bytes.data(), &value, sizeof(T));
  values[fullKey(handle, key)] = std::move(stored);
  saveValues();
  return sizeof(T);
}

template <typename T>
T getValue(uint32_t handle, const char *key, T defaultValue) {
  if (key == nullptr) {
    return defaultValue;
  }
  std::lock_guard<std::mutex> lock(values_mutex);
  loadValues();
  const auto found = values.find(fullKey(handle, key));
  if (found == values.end() || found->second.bytes.size() != sizeof(T)) {
    return defaultValue;
  }
  std::memcpy(&defaultValue, found->second.bytes.data(), sizeof(T));
  return defaultValue;
}

}  // namespace

Preferences::Preferences() : _handle(0), _started(false), _readOnly(false) {}

Preferences::~Preferences() {
  end();
}

bool Preferences::begin(const char *name, bool readOnly, const char *) {
  if (_started) {
    return false;
  }
  // handleFor mutates a shared static map, and loadValues touches the shared
  // value store. Both must run under values_mutex or two threads opening
  // preferences at once (the UI constructor and the GPS task both call
  // Settings::load during startup) race and corrupt the map.
  std::lock_guard<std::mutex> lock(values_mutex);
  _handle = handleFor(name);
  _readOnly = readOnly;
  _started = true;
  loadValues();
  return true;
}

void Preferences::end() {
  _started = false;
}

bool Preferences::clear() {
  if (!_started || _readOnly) {
    return false;
  }
  std::lock_guard<std::mutex> lock(values_mutex);
  loadValues();
  const std::string prefix = std::to_string(_handle) + ":";
  for (auto it = values.begin(); it != values.end();) {
    if (it->first.compare(0, prefix.size(), prefix) == 0) {
      it = values.erase(it);
    } else {
      ++it;
    }
  }
  saveValues();
  return true;
}

bool Preferences::remove(const char *key) {
  if (!_started || _readOnly || key == nullptr) {
    return false;
  }
  std::lock_guard<std::mutex> lock(values_mutex);
  loadValues();
  values.erase(fullKey(_handle, key));
  saveValues();
  return true;
}

template <>
size_t Preferences::put(const char *key, bool value) {
  return putValue(_handle, key, static_cast<uint8_t>(value ? 1 : 0));
}

template <>
size_t Preferences::put(const char *key, uint8_t value) {
  return putValue(_handle, key, value);
}

template <>
size_t Preferences::put(const char *key, uint32_t value) {
  return putValue(_handle, key, value);
}

template <>
size_t Preferences::put(const char *key, uint16_t value) {
  return putValue(_handle, key, value);
}

template <>
size_t Preferences::put(const char *key, std::string value) {
  return put(key, value.c_str());
}

size_t Preferences::put(const char *key, const char *value) {
  if (!_started || _readOnly || key == nullptr || value == nullptr) {
    return 0;
  }
  std::lock_guard<std::mutex> lock(values_mutex);
  loadValues();
  Value stored;
  stored.string_value = true;
  stored.bytes.assign(value, value + std::strlen(value) + 1);
  values[fullKey(_handle, key)] = std::move(stored);
  saveValues();
  return std::strlen(value);
}

size_t Preferences::put(const char *key, const void *value, size_t len) {
  if (!_started || _readOnly || key == nullptr || value == nullptr || len == 0) {
    return 0;
  }
  std::lock_guard<std::mutex> lock(values_mutex);
  loadValues();
  Value stored;
  stored.bytes.resize(len);
  std::memcpy(stored.bytes.data(), value, len);
  values[fullKey(_handle, key)] = std::move(stored);
  saveValues();
  return len;
}

template <>
uint8_t Preferences::get(const char *key, uint8_t defaultValue) {
  return getValue(_handle, key, defaultValue);
}

template <>
bool Preferences::get(const char *key, bool defaultValue) {
  return get<uint8_t>(key, defaultValue ? 1 : 0) != 0;
}

template <>
uint32_t Preferences::get(const char *key, uint32_t defaultValue) {
  return getValue(_handle, key, defaultValue);
}

template <>
uint16_t Preferences::get(const char *key, uint16_t defaultValue) {
  return getValue(_handle, key, defaultValue);
}

template <>
std::string Preferences::get(const char *key, std::string defaultValue) {
  if (!_started || key == nullptr) {
    return defaultValue;
  }
  std::lock_guard<std::mutex> lock(values_mutex);
  loadValues();
  const auto found = values.find(fullKey(_handle, key));
  if (found == values.end() || !found->second.string_value) {
    return defaultValue;
  }
  return std::string(reinterpret_cast<const char *>(found->second.bytes.data()));
}

size_t Preferences::get(const char *key, void *buffer, size_t maxLen) {
  if (!_started || key == nullptr) {
    return 0;
  }
  std::lock_guard<std::mutex> lock(values_mutex);
  loadValues();
  const auto found = values.find(fullKey(_handle, key));
  if (found == values.end()) {
    return 0;
  }
  const size_t size = found->second.bytes.size();
  if (buffer == nullptr || maxLen < size) {
    return size;
  }
  std::memcpy(buffer, found->second.bytes.data(), size);
  return size;
}

bool Preferences::isKey(const char *key) {
  if (!_started || key == nullptr) {
    return false;
  }
  std::lock_guard<std::mutex> lock(values_mutex);
  loadValues();
  return values.find(fullKey(_handle, key)) != values.end();
}

size_t Preferences::getBytesLength(const char *key) {
  if (!_started || key == nullptr) {
    return 0;
  }
  std::lock_guard<std::mutex> lock(values_mutex);
  loadValues();
  const auto found = values.find(fullKey(_handle, key));
  return found == values.end() ? 0 : found->second.bytes.size();
}

size_t Preferences::freeEntries() {
  return 1000;
}

}  // namespace Furble
