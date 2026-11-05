#ifndef FURBLE_BOUNDARY_ESP_VFS_FAT_H
#define FURBLE_BOUNDARY_ESP_VFS_FAT_H

#include <cstddef>
#include <cstdint>

#include "driver/sdspi_host.h"
#include "esp_err.h"
#include "sdmmc_cmd.h"

struct esp_vfs_fat_mount_config_t {
  bool format_if_mount_failed = false;
  int max_files = 4;
  int allocation_unit_size = 16 * 1024;
};

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t esp_vfs_fat_sdspi_mount(const char *base_path,
                                  const sdmmc_host_t *host_config,
                                  const sdspi_device_config_t *slot_config,
                                  const esp_vfs_fat_mount_config_t *mount_config,
                                  sdmmc_card_t **out_card);
esp_err_t esp_vfs_fat_sdcard_unmount(const char *base_path, sdmmc_card_t *card);
esp_err_t esp_vfs_fat_info(const char *base_path, uint64_t *out_total_bytes,
                           uint64_t *out_free_bytes);

#ifdef __cplusplus
}
#endif

#endif
