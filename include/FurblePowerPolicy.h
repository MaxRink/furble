#ifndef FURBLE_POWER_POLICY_H
#define FURBLE_POWER_POLICY_H

#include <cstdint>

namespace Furble {
namespace PowerPolicy {

/** Portable form of the esp_pm_config_t fields that Furble sets. */
struct PmRequest {
  uint8_t max_freq_mhz;
  uint8_t min_freq_mhz;
  bool light_sleep_enable;
};

/**
 * Whether esp_pm_configure() may request automatic light sleep.
 *
 * ESP-IDF returns ESP_ERR_NOT_SUPPORTED when light_sleep_enable is true but
 * CONFIG_FREERTOS_USE_TICKLESS_IDLE is off (pm_impl.c, esp_pm_configure). The
 * Waveshare Ethernet config enables CONFIG_PM_ENABLE for DFS but not tickless
 * idle, so requesting light sleep there failed the whole call: no DFS was
 * applied and the boot fallback aborted. The request must follow the compiled
 * support, not an assumption about it.
 */
constexpr bool requestLightSleep(bool ticklessIdleCompiled) {
  return ticklessIdleCompiled;
}

/** Build the PM request from the requested frequency and compiled support. */
constexpr PmRequest makeRequest(uint8_t maxFreqMhz, uint8_t minFreqMhz, bool ticklessIdleCompiled) {
  return PmRequest {maxFreqMhz, minFreqMhz, requestLightSleep(ticklessIdleCompiled)};
}

}  // namespace PowerPolicy
}  // namespace Furble

#endif  // FURBLE_POWER_POLICY_H
