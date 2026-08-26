#ifndef FURBLE_HOST_SETTINGS_NVS_H
#define FURBLE_HOST_SETTINGS_NVS_H

#include <cstddef>
#include <cstdint>

using esp_err_t = int32_t;
using nvs_handle_t = uint32_t;

struct nvs_stats_t {
  size_t free_entries;
};

enum {
  ESP_OK = 0,
  ESP_FAIL = -1,
  ESP_ERR_NVS_BASE = 0x1100,
  ESP_ERR_NVS_NOT_FOUND = ESP_ERR_NVS_BASE + 1,
  ESP_ERR_NVS_TYPE_MISMATCH = ESP_ERR_NVS_BASE + 2,
  ESP_ERR_NVS_INVALID_LENGTH = ESP_ERR_NVS_BASE + 12,
  ESP_ERR_NVS_INVALID_NAME = ESP_ERR_NVS_BASE + 6,
  ESP_ERR_NVS_INVALID_HANDLE = ESP_ERR_NVS_BASE + 7,
};

enum {
  NVS_READONLY = 0,
  NVS_READWRITE = 1,
};

// This is intentionally a test-only view of the typed mock store. The
// production Preferences API does not expose its NVS type, so the test uses
// this to verify that each setting took the same typed NVS path as firmware.
enum nvs_test_value_type_t {
  NVS_TEST_INVALID,
  NVS_TEST_I8,
  NVS_TEST_U8,
  NVS_TEST_I16,
  NVS_TEST_U16,
  NVS_TEST_I32,
  NVS_TEST_U32,
  NVS_TEST_I64,
  NVS_TEST_U64,
  NVS_TEST_STRING,
  NVS_TEST_BLOB,
};

extern "C" {
esp_err_t nvs_open(const char *name, int open_mode, nvs_handle_t *out_handle);
esp_err_t nvs_open_from_partition(const char *partition_label,
                                  const char *name,
                                  int open_mode,
                                  nvs_handle_t *out_handle);
void nvs_close(nvs_handle_t handle);
esp_err_t nvs_commit(nvs_handle_t handle);
esp_err_t nvs_erase_all(nvs_handle_t handle);
esp_err_t nvs_erase_key(nvs_handle_t handle, const char *key);

esp_err_t nvs_set_i8(nvs_handle_t handle, const char *key, int8_t value);
esp_err_t nvs_set_u8(nvs_handle_t handle, const char *key, uint8_t value);
esp_err_t nvs_set_i16(nvs_handle_t handle, const char *key, int16_t value);
esp_err_t nvs_set_u16(nvs_handle_t handle, const char *key, uint16_t value);
esp_err_t nvs_set_i32(nvs_handle_t handle, const char *key, int32_t value);
esp_err_t nvs_set_u32(nvs_handle_t handle, const char *key, uint32_t value);
esp_err_t nvs_set_i64(nvs_handle_t handle, const char *key, int64_t value);
esp_err_t nvs_set_u64(nvs_handle_t handle, const char *key, uint64_t value);
esp_err_t nvs_set_str(nvs_handle_t handle, const char *key, const char *value);
esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *value, size_t length);

esp_err_t nvs_get_i8(nvs_handle_t handle, const char *key, int8_t *value);
esp_err_t nvs_get_u8(nvs_handle_t handle, const char *key, uint8_t *value);
esp_err_t nvs_get_i16(nvs_handle_t handle, const char *key, int16_t *value);
esp_err_t nvs_get_u16(nvs_handle_t handle, const char *key, uint16_t *value);
esp_err_t nvs_get_i32(nvs_handle_t handle, const char *key, int32_t *value);
esp_err_t nvs_get_u32(nvs_handle_t handle, const char *key, uint32_t *value);
esp_err_t nvs_get_i64(nvs_handle_t handle, const char *key, int64_t *value);
esp_err_t nvs_get_u64(nvs_handle_t handle, const char *key, uint64_t *value);
esp_err_t nvs_get_str(nvs_handle_t handle, const char *key, char *value, size_t *length);
esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *value, size_t *length);

esp_err_t nvs_get_stats(const char *part_name, nvs_stats_t *stats);

void nvs_test_reset(void);
nvs_test_value_type_t nvs_test_value_type(const char *name, const char *key);
size_t nvs_test_commit_count(void);
}

#endif
