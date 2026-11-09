#ifndef FURBLE_TASK_WATCHDOG_H
#define FURBLE_TASK_WATCHDOG_H

/**
 * Debug-only task watchdog subscription for the long-lived service loops
 * (MQTT owner, WebUI supervisor, companion service).
 *
 * The debug envs define FURBLE_TASK_WDT. A subscribed loop that stops feeding
 * for CONFIG_ESP_TASK_WDT_TIMEOUT_S makes ESP-IDF log the watchdog report with
 * a backtrace of every CPU. CONFIG_ESP_TASK_WDT_PANIC stays off until a
 * hardware soak shows no false trigger, so a report never reboots the device.
 * Release, simulator and host builds compile both calls to nothing.
 */
#if defined(FURBLE_TASK_WDT) && FURBLE_TASK_WDT
#include "esp_err.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#endif

namespace Furble {
namespace TaskWatchdog {

/** Subscribe the calling task. Call once, before its loop. */
inline void subscribe(void) {
#if defined(FURBLE_TASK_WDT) && FURBLE_TASK_WDT
  const esp_err_t err = esp_task_wdt_add(nullptr);
  if (err != ESP_OK) {
    ESP_LOGW("task_wdt", "Could not subscribe %s: %s", pcTaskGetName(nullptr),
             esp_err_to_name(err));
  }
#endif
}

/** Feed the watchdog from the calling task's loop; a no-op if unsubscribed. */
inline void feed(void) {
#if defined(FURBLE_TASK_WDT) && FURBLE_TASK_WDT
  if (esp_task_wdt_status(nullptr) == ESP_OK) {
    (void)esp_task_wdt_reset();
  }
#endif
}

}  // namespace TaskWatchdog
}  // namespace Furble

#endif
