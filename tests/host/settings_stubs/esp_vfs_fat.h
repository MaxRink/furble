#ifndef FURBLE_HOST_SETTINGS_ESP_VFS_FAT_H
#define FURBLE_HOST_SETTINGS_ESP_VFS_FAT_H

#include "nvs.h"
#include "sdmmc_cmd.h"

struct esp_vfs_fat_mount_config_t {
  bool format_if_mount_failed;
  int max_files;
  int allocation_unit_size;
};

inline esp_err_t esp_vfs_fat_sdspi_mount(const char *,
                                         sdmmc_host_t *,
                                         sdspi_device_config_t *,
                                         esp_vfs_fat_mount_config_t *,
                                         sdmmc_card_t **card) {
  if (card != nullptr) {
    *card = nullptr;
  }
  return ESP_FAIL;
}

inline esp_err_t esp_vfs_fat_sdcard_unmount(const char *, sdmmc_card_t *) {
  return ESP_OK;
}

inline esp_err_t esp_vfs_fat_info(const char *, uint64_t *total_bytes, uint64_t *free_bytes) {
  if (total_bytes != nullptr) {
    *total_bytes = 0;
  }
  if (free_bytes != nullptr) {
    *free_bytes = 0;
  }
  return ESP_OK;
}

#endif
