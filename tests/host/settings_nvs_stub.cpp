#include "nvs.h"
#include "nvs_flash.h"

#include <cstring>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace {

struct Value {
  nvs_test_value_type_t type;
  std::vector<uint8_t> bytes;
};

struct Handle {
  std::string name;
  bool read_only;
  std::map<std::string, std::optional<Value>> pending;
};

std::map<std::string, std::map<std::string, Value>> storage;
std::map<nvs_handle_t, Handle> handles;
nvs_handle_t next_handle = 1;
size_t commit_count = 0;
size_t commit_attempt_count = 0;
size_t set_call_count = 0;
size_t fail_set_at = 0;
size_t fail_commit_at = 0;

bool validKey(const char *key) {
  return key != nullptr && key[0] != '\0' && std::strlen(key) <= 15;
}

Handle *getHandle(nvs_handle_t handle) {
  const auto it = handles.find(handle);
  return it == handles.end() ? nullptr : &it->second;
}

Value *getValue(nvs_handle_t handle, const char *key) {
  Handle *opened = getHandle(handle);
  if (opened == nullptr || !validKey(key)) {
    return nullptr;
  }

  const auto pending_it = opened->pending.find(key);
  if (pending_it != opened->pending.end()) {
    return pending_it->second.has_value() ? &pending_it->second.value() : nullptr;
  }

  const auto namespace_it = storage.find(opened->name);
  if (namespace_it == storage.end()) {
    return nullptr;
  }
  const auto value_it = namespace_it->second.find(key);
  return value_it == namespace_it->second.end() ? nullptr : &value_it->second;
}

bool stageValue(Handle *opened, const char *key, Value value) {
  set_call_count++;
  if ((fail_set_at != 0) && (set_call_count == fail_set_at)) {
    return false;
  }
  opened->pending[key] = std::move(value);
  return true;
}

template <typename T>
esp_err_t setInteger(nvs_handle_t handle, const char *key, nvs_test_value_type_t type, T value) {
  Handle *opened = getHandle(handle);
  if (opened == nullptr) {
    return ESP_ERR_NVS_INVALID_HANDLE;
  }
  if (!validKey(key)) {
    return ESP_ERR_NVS_INVALID_NAME;
  }
  if (opened->read_only) {
    return ESP_ERR_NVS_INVALID_HANDLE;
  }

  Value entry = {type, std::vector<uint8_t>(sizeof(T))};
  std::memcpy(entry.bytes.data(), &value, sizeof(T));
  return stageValue(opened, key, std::move(entry)) ? ESP_OK : ESP_FAIL;
}

template <typename T>
esp_err_t getInteger(nvs_handle_t handle, const char *key, nvs_test_value_type_t type, T *value) {
  Value *entry = getValue(handle, key);
  if (entry == nullptr) {
    return ESP_ERR_NVS_NOT_FOUND;
  }
  if (entry->type != type || entry->bytes.size() != sizeof(T)) {
    return ESP_ERR_NVS_TYPE_MISMATCH;
  }
  std::memcpy(value, entry->bytes.data(), sizeof(T));
  return ESP_OK;
}

esp_err_t openNamespace(const char *name, int open_mode, nvs_handle_t *out_handle) {
  if ((name == nullptr) || (name[0] == '\0') || (out_handle == nullptr)) {
    return ESP_ERR_NVS_INVALID_NAME;
  }

  storage.try_emplace(name);
  const nvs_handle_t handle = next_handle++;
  handles.emplace(handle, Handle {name, open_mode == NVS_READONLY, {}});
  *out_handle = handle;
  return ESP_OK;
}

}  // namespace

extern "C" {

esp_err_t nvs_open(const char *name, int open_mode, nvs_handle_t *out_handle) {
  return openNamespace(name, open_mode, out_handle);
}

esp_err_t nvs_open_from_partition(const char *,
                                  const char *name,
                                  int open_mode,
                                  nvs_handle_t *out_handle) {
  return openNamespace(name, open_mode, out_handle);
}

void nvs_close(nvs_handle_t handle) {
  handles.erase(handle);
}

esp_err_t nvs_commit(nvs_handle_t handle) {
  if (getHandle(handle) == nullptr) {
    return ESP_ERR_NVS_INVALID_HANDLE;
  }
  commit_attempt_count++;
  if ((fail_commit_at != 0) && (commit_attempt_count == fail_commit_at)) {
    return ESP_FAIL;
  }
  auto *opened = getHandle(handle);
  auto &values = storage[opened->name];
  for (auto &[key, value] : opened->pending) {
    if (value.has_value()) {
      values[key] = std::move(value.value());
    } else {
      values.erase(key);
    }
  }
  opened->pending.clear();
  commit_count++;
  return ESP_OK;
}

esp_err_t nvs_erase_all(nvs_handle_t handle) {
  Handle *opened = getHandle(handle);
  if (opened == nullptr || opened->read_only) {
    return ESP_ERR_NVS_INVALID_HANDLE;
  }
  opened->pending.clear();
  for (const auto &entry : storage[opened->name]) {
    opened->pending[entry.first] = std::nullopt;
  }
  return ESP_OK;
}

esp_err_t nvs_erase_key(nvs_handle_t handle, const char *key) {
  Handle *opened = getHandle(handle);
  if (opened == nullptr) {
    return ESP_ERR_NVS_INVALID_HANDLE;
  }
  if (!validKey(key)) {
    return ESP_ERR_NVS_INVALID_NAME;
  }
  if (opened->read_only) {
    return ESP_ERR_NVS_INVALID_HANDLE;
  }

  if (getValue(handle, key) == nullptr) {
    return ESP_ERR_NVS_NOT_FOUND;
  }
  opened->pending[key] = std::nullopt;
  return ESP_OK;
}

esp_err_t nvs_set_i8(nvs_handle_t h, const char *k, int8_t v) {
  return setInteger(h, k, NVS_TEST_I8, v);
}
esp_err_t nvs_set_u8(nvs_handle_t h, const char *k, uint8_t v) {
  return setInteger(h, k, NVS_TEST_U8, v);
}
esp_err_t nvs_set_i16(nvs_handle_t h, const char *k, int16_t v) {
  return setInteger(h, k, NVS_TEST_I16, v);
}
esp_err_t nvs_set_u16(nvs_handle_t h, const char *k, uint16_t v) {
  return setInteger(h, k, NVS_TEST_U16, v);
}
esp_err_t nvs_set_i32(nvs_handle_t h, const char *k, int32_t v) {
  return setInteger(h, k, NVS_TEST_I32, v);
}
esp_err_t nvs_set_u32(nvs_handle_t h, const char *k, uint32_t v) {
  return setInteger(h, k, NVS_TEST_U32, v);
}
esp_err_t nvs_set_i64(nvs_handle_t h, const char *k, int64_t v) {
  return setInteger(h, k, NVS_TEST_I64, v);
}
esp_err_t nvs_set_u64(nvs_handle_t h, const char *k, uint64_t v) {
  return setInteger(h, k, NVS_TEST_U64, v);
}

esp_err_t nvs_set_str(nvs_handle_t handle, const char *key, const char *value) {
  Handle *opened = getHandle(handle);
  if (opened == nullptr) {
    return ESP_ERR_NVS_INVALID_HANDLE;
  }
  if (!validKey(key) || value == nullptr) {
    return ESP_ERR_NVS_INVALID_NAME;
  }
  if (opened->read_only) {
    return ESP_ERR_NVS_INVALID_HANDLE;
  }

  const size_t length = std::strlen(value) + 1;
  const Value entry = {
      NVS_TEST_STRING,
      std::vector<uint8_t>(value, value + length),
  };
  return stageValue(opened, key, entry) ? ESP_OK : ESP_FAIL;
}

esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *value, size_t length) {
  Handle *opened = getHandle(handle);
  if (opened == nullptr) {
    return ESP_ERR_NVS_INVALID_HANDLE;
  }
  if (!validKey(key) || value == nullptr || length == 0) {
    return ESP_ERR_NVS_INVALID_NAME;
  }
  if (opened->read_only) {
    return ESP_ERR_NVS_INVALID_HANDLE;
  }

  const auto *bytes = static_cast<const uint8_t *>(value);
  const Value entry = {
      NVS_TEST_BLOB,
      std::vector<uint8_t>(bytes, bytes + length),
  };
  return stageValue(opened, key, entry) ? ESP_OK : ESP_FAIL;
}

esp_err_t nvs_get_i8(nvs_handle_t h, const char *k, int8_t *v) {
  return getInteger(h, k, NVS_TEST_I8, v);
}
esp_err_t nvs_get_u8(nvs_handle_t h, const char *k, uint8_t *v) {
  return getInteger(h, k, NVS_TEST_U8, v);
}
esp_err_t nvs_get_i16(nvs_handle_t h, const char *k, int16_t *v) {
  return getInteger(h, k, NVS_TEST_I16, v);
}
esp_err_t nvs_get_u16(nvs_handle_t h, const char *k, uint16_t *v) {
  return getInteger(h, k, NVS_TEST_U16, v);
}
esp_err_t nvs_get_i32(nvs_handle_t h, const char *k, int32_t *v) {
  return getInteger(h, k, NVS_TEST_I32, v);
}
esp_err_t nvs_get_u32(nvs_handle_t h, const char *k, uint32_t *v) {
  return getInteger(h, k, NVS_TEST_U32, v);
}
esp_err_t nvs_get_i64(nvs_handle_t h, const char *k, int64_t *v) {
  return getInteger(h, k, NVS_TEST_I64, v);
}
esp_err_t nvs_get_u64(nvs_handle_t h, const char *k, uint64_t *v) {
  return getInteger(h, k, NVS_TEST_U64, v);
}

esp_err_t nvs_get_str(nvs_handle_t handle, const char *key, char *value, size_t *length) {
  Value *entry = getValue(handle, key);
  if (entry == nullptr) {
    return ESP_ERR_NVS_NOT_FOUND;
  }
  if (entry->type != NVS_TEST_STRING || length == nullptr) {
    return ESP_ERR_NVS_TYPE_MISMATCH;
  }

  const size_t required = entry->bytes.size();
  if (value == nullptr) {
    *length = required;
    return ESP_OK;
  }
  if (*length < required) {
    *length = required;
    return ESP_ERR_NVS_INVALID_LENGTH;
  }
  std::memcpy(value, entry->bytes.data(), required);
  *length = required;
  return ESP_OK;
}

esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *value, size_t *length) {
  Value *entry = getValue(handle, key);
  if (entry == nullptr) {
    return ESP_ERR_NVS_NOT_FOUND;
  }
  if (entry->type != NVS_TEST_BLOB || length == nullptr) {
    return ESP_ERR_NVS_TYPE_MISMATCH;
  }

  const size_t required = entry->bytes.size();
  if (value == nullptr) {
    *length = required;
    return ESP_OK;
  }
  if (*length < required) {
    *length = required;
    return ESP_ERR_NVS_INVALID_LENGTH;
  }
  std::memcpy(value, entry->bytes.data(), required);
  *length = required;
  return ESP_OK;
}

esp_err_t nvs_get_stats(const char *, nvs_stats_t *stats) {
  if (stats == nullptr) {
    return ESP_ERR_NVS_INVALID_HANDLE;
  }
  stats->free_entries = 1000;
  return ESP_OK;
}

esp_err_t nvs_flash_init(void) {
  return ESP_OK;
}

esp_err_t nvs_flash_erase(void) {
  nvs_test_reset();
  return ESP_OK;
}

esp_err_t nvs_flash_init_partition(const char *) {
  return ESP_OK;
}

void nvs_test_reset(void) {
  storage.clear();
  handles.clear();
  next_handle = 1;
  commit_count = 0;
  commit_attempt_count = 0;
  set_call_count = 0;
  fail_set_at = 0;
  fail_commit_at = 0;
}

nvs_test_value_type_t nvs_test_value_type(const char *name, const char *key) {
  if (name == nullptr || key == nullptr) {
    return NVS_TEST_INVALID;
  }
  const auto namespace_it = storage.find(name);
  if (namespace_it == storage.end()) {
    return NVS_TEST_INVALID;
  }
  const auto value_it = namespace_it->second.find(key);
  return value_it == namespace_it->second.end() ? NVS_TEST_INVALID : value_it->second.type;
}

size_t nvs_test_commit_count(void) {
  return commit_count;
}

void nvs_test_fail_set_on(size_t nth_future_call) {
  fail_set_at = set_call_count + nth_future_call;
}

void nvs_test_fail_commit_on(size_t nth_future_call) {
  fail_commit_at = commit_attempt_count + nth_future_call;
}

}  // extern "C"
