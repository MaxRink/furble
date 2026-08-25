#ifndef FURBLE_TEST_NVS_H
#define FURBLE_TEST_NVS_H

#include <stddef.h>
#include <stdint.h>

typedef uint32_t nvs_handle_t;
typedef int32_t esp_err_t;

constexpr esp_err_t ESP_OK = 0;
constexpr esp_err_t ESP_FAIL = -1;
constexpr esp_err_t ESP_ERR_NVS_NOT_FOUND = -2;
constexpr esp_err_t ESP_ERR_NVS_INVALID_LENGTH = -3;
constexpr uint8_t NVS_READWRITE = 0;

#ifdef __cplusplus
extern "C" {
#endif
esp_err_t nvs_open(const char *name, uint8_t mode, nvs_handle_t *handle);
void nvs_close(nvs_handle_t handle);
esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *out, size_t *length);
esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *value, size_t length);
esp_err_t nvs_commit(nvs_handle_t handle);
#ifdef __cplusplus
}

namespace FakeNvs {
void reset();
void reboot();
void failNextCommit();
void truncateSlot(uint8_t slot, size_t length);
}  // namespace FakeNvs
#endif

#endif
