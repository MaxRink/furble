#ifndef FURBLE_BOOT_TIMELINE_H
#define FURBLE_BOOT_TIMELINE_H

#include <cstddef>
#include <cstdint>

namespace Furble {

/**
 * Fixed-size boot timeline.
 *
 * The first occurrence of every mark is kept. Storage is a static array of
 * atomics, so a mark costs one compare-and-swap and never allocates. The
 * console command and the one-line boot summary read the same array.
 *
 * The origin is esp_timer_get_time() at the top of app_main(). That timer is
 * initialised after the PSRAM boot init, so the ROM bootloader and the PSRAM
 * memory test are not part of the microsecond timeline. begin() also samples
 * the CPU cycle-counter timestamp, which does include both, and exposes it as
 * resetToAppMs(). Add it to a mark to estimate the time since reset.
 */
class BootTimeline {
 public:
  BootTimeline() = delete;
  ~BootTimeline() = delete;

  enum mark_t : uint8_t {
    APP_MAIN = 0,
    /** PSRAM is already initialised before app_main runs. */
    PSRAM_READY,
    SETTINGS_DONE,
    CRASH_LOG_DONE,
    NETIF_DONE,
    PLATFORM_DONE,
    DISPLAY_READY,
    PMIC_READY,
    BOOT_SPLASH_SHOWN,
    IR_DONE,
    FEEDBACK_DONE,
    SD_DONE,
    CPU_POLICY_DONE,
    BLE_DONE,
    WEBUI_EARLY_DONE,
    COMPANION_DONE,
    MQTT_INIT_DONE,
    CONTROL_TASK_DONE,
    WIFI_STARTED,
    CONSOLE_READY,
    BOOT_SPLASH_DONE,
    UI_CTOR_START,
    UI_MENU_START,
    UI_MENU_DONE,
    UI_CTOR_DONE,
    UI_FIRST_FRAME,
    SERVICES_READY,
    WIFI_ASSOC,
    WIFI_IP,
    NTP_SYNCED,
    MQTT_CONNECTED,
    COUNT
  };

  /** Record APP_MAIN and the cycle-counter timestamp. Later calls are no-ops. */
  static void begin(void);

  /** Record the first occurrence of a mark. Cheap and safe from any task. */
  static void mark(mark_t mark);

  /** True when the mark has been recorded. */
  static bool recorded(mark_t mark);

  /** Microseconds since app_main, or -1 when the mark is unset. */
  static int64_t at(mark_t mark);

  /** Cycle-counter milliseconds from reset to app_main, or 0 before begin(). */
  static uint32_t resetToAppMs(void);

  /** Short stable label for a mark, used by the console and the summary. */
  static const char *label(mark_t mark);

  /**
   * Format one line with every recorded mark as `label=ms`, in enum order.
   * Returns the number of bytes written, excluding the terminator.
   */
  static size_t summary(char *buffer, size_t size);

  /** Emit the single boot ESP_LOGI summary. Call more than once is a no-op. */
  static void logSummary(void);

  /** Print the full timeline, used by the `boot timing` console command. */
  static void print(void);
};

}  // namespace Furble

#endif  // FURBLE_BOOT_TIMELINE_H
