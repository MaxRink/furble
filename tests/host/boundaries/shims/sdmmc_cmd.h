#ifndef FURBLE_BOUNDARY_SDMMC_CMD_H
#define FURBLE_BOUNDARY_SDMMC_CMD_H

#include <cstdint>

struct sdmmc_csd_t {
  uint32_t capacity = 0;
  uint32_t sector_size = 512;
};

struct sdmmc_card_t {
  sdmmc_csd_t csd;
};

#endif
