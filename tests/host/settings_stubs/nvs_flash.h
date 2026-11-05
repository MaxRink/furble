#ifndef FURBLE_HOST_SETTINGS_NVS_FLASH_H
#define FURBLE_HOST_SETTINGS_NVS_FLASH_H

#include "nvs.h"

#ifndef ESP_ERR_NVS_BASE
#define ESP_ERR_NVS_BASE 0x1100
#endif
#ifndef ESP_ERR_NVS_NO_FREE_PAGES
#define ESP_ERR_NVS_NO_FREE_PAGES (ESP_ERR_NVS_BASE + 10)
#endif
#ifndef ESP_ERR_NVS_NEW_VERSION_FOUND
#define ESP_ERR_NVS_NEW_VERSION_FOUND (ESP_ERR_NVS_BASE + 11)
#endif

extern "C" {
esp_err_t nvs_flash_init(void);
esp_err_t nvs_flash_erase(void);
esp_err_t nvs_flash_init_partition(const char *partition_label);
}

#define ESP_ERROR_CHECK(expression) \
  do {                              \
    if ((expression) != ESP_OK) {   \
      __builtin_abort();            \
    }                               \
  } while (false)

#endif
