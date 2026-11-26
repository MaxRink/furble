#ifndef FURBLE_PLATFORM_SHIM_POWER_H
#define FURBLE_PLATFORM_SHIM_POWER_H

#include <cstdint>

#include <esp_err.h>
#include <esp_pm.h>

namespace Furble {
class Power {
 public:
  static constexpr const uint8_t CPU_MIN_FREQ_MHZ = 40;

  static Power &getInstance(void);
  static void init(void);
  esp_err_t configure(uint8_t max_freq_mhz);
};
}  // namespace Furble

#endif
