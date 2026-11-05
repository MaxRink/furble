#include "FurbleBootTimeline.h"

#include <atomic>
#include <cinttypes>
#include <cstdio>

#if defined(FURBLE_BOOT_TIMELINE_HOST_TEST) || defined(FURBLE_SIM)
extern "C" uint32_t furbleBootTimelineHostResetMs(void);
extern "C" int64_t furbleBootTimelineHostNowUs(void);
#else
#include <esp_cpu.h>
#include <esp_log.h>
#include <esp_rom_sys.h>
#include <esp_timer.h>
#endif

namespace Furble {

namespace {

#if !defined(FURBLE_BOOT_TIMELINE_HOST_TEST) && !defined(FURBLE_SIM)
constexpr const char *LOG_TAG = "boot";
#endif

#if defined(FURBLE_BOOT_TIMELINE_HOST_TEST) || defined(FURBLE_SIM)
uint32_t resetTimestampMs(void) {
  return furbleBootTimelineHostResetMs();
}

int64_t nowUs(void) {
  return furbleBootTimelineHostNowUs();
}
#else
/**
 * The scheduler is not running when app_main() calls begin(), so this reads
 * the free-running CPU cycle counter, which starts at reset. It includes the
 * ROM bootloader and the PSRAM memory test that esp_timer and esp_log_timestamp
 * cannot see.
 */
uint32_t resetTimestampMs(void) {
  return BootTimeline::cycleCountToMs(esp_cpu_get_cycle_count(), esp_rom_get_cpu_ticks_per_us());
}

int64_t nowUs(void) {
  return esp_timer_get_time();
}
#endif

// Zero means unset. A recorded value stores nowUs() + 1, so an app_main mark at
// exactly zero microseconds is still distinguishable from an unset slot.
std::atomic<uint64_t> s_At[BootTimeline::COUNT] = {};
std::atomic<uint32_t> s_ResetToAppMs {0};
std::atomic<bool> s_Begun {false};
std::atomic<bool> s_Logged {false};

}  // namespace

void BootTimeline::begin(void) {
  bool expected = false;
  if (!s_Begun.compare_exchange_strong(expected, true, std::memory_order_relaxed)) {
    return;
  }

  s_ResetToAppMs.store(resetTimestampMs(), std::memory_order_relaxed);
  mark(APP_MAIN);
}

void BootTimeline::mark(mark_t mark) {
  if (static_cast<size_t>(mark) >= COUNT) {
    return;
  }

  const uint64_t candidate = static_cast<uint64_t>(nowUs()) + 1U;
  uint64_t expected = 0;
  s_At[mark].compare_exchange_strong(expected, candidate, std::memory_order_relaxed);
}

bool BootTimeline::recorded(mark_t mark) {
  if (static_cast<size_t>(mark) >= COUNT) {
    return false;
  }
  return s_At[mark].load(std::memory_order_relaxed) != 0;
}

int64_t BootTimeline::at(mark_t mark) {
  if (static_cast<size_t>(mark) >= COUNT) {
    return -1;
  }
  const uint64_t value = s_At[mark].load(std::memory_order_relaxed);
  return (value == 0) ? -1 : static_cast<int64_t>(value - 1U);
}

uint32_t BootTimeline::resetToAppMs(void) {
  return s_ResetToAppMs.load(std::memory_order_relaxed);
}

const char *BootTimeline::label(mark_t mark) {
  switch (mark) {
    case APP_MAIN:
      return "app_main";
    case PSRAM_READY:
      return "psram_ready";
    case SETTINGS_DONE:
      return "settings_done";
    case CRASH_LOG_DONE:
      return "crash_log_done";
    case NETIF_DONE:
      return "netif_done";
    case PLATFORM_DONE:
      return "platform_done";
    case DISPLAY_READY:
      return "display_ready";
    case PMIC_READY:
      return "pmic_ready";
    case BOOT_SPLASH_SHOWN:
      return "splash_shown";
    case IR_DONE:
      return "ir_done";
    case FEEDBACK_DONE:
      return "feedback_done";
    case SD_DONE:
      return "sd_done";
    case CPU_POLICY_DONE:
      return "cpu_policy_done";
    case BLE_DONE:
      return "ble_done";
    case WEBUI_EARLY_DONE:
      return "webui_early_done";
    case COMPANION_DONE:
      return "companion_done";
    case MQTT_INIT_DONE:
      return "mqtt_init_done";
    case CONTROL_TASK_DONE:
      return "control_task_done";
    case WIFI_STARTED:
      return "wifi_started";
    case CONSOLE_READY:
      return "console_ready";
    case BOOT_SPLASH_DONE:
      return "splash_done";
    case UI_CTOR_START:
      return "ui_ctor_start";
    case UI_MENU_START:
      return "ui_menu_start";
    case UI_MENU_DONE:
      return "ui_menu_done";
    case UI_CTOR_DONE:
      return "ui_ctor_done";
    case UI_FIRST_FRAME:
      return "ui_first_frame";
    case SERVICES_READY:
      return "services_ready";
    case WIFI_ASSOC:
      return "wifi_assoc";
    case WIFI_IP:
      return "wifi_ip";
    case NTP_SYNCED:
      return "ntp_synced";
    case MQTT_CONNECTED:
      return "mqtt_connected";
    case COUNT:
      break;
  }
  return "unknown";
}

size_t BootTimeline::summary(char *buffer, size_t size) {
  if ((buffer == nullptr) || (size == 0)) {
    return 0;
  }

  size_t used = 0;
  for (uint8_t index = 0; index < static_cast<uint8_t>(COUNT); index++) {
    const mark_t mark = static_cast<mark_t>(index);
    const int64_t stamp = at(mark);
    if (stamp < 0) {
      continue;
    }

    const int written = std::snprintf(buffer + used, size - used, "%s%s=%" PRId64,
                                      (used == 0) ? "" : " ", label(mark), stamp / 1000);
    if ((written < 0) || (static_cast<size_t>(written) >= (size - used))) {
      used = size - 1;
      break;
    }
    used += static_cast<size_t>(written);
  }
  buffer[used] = '\0';
  return used;
}

void BootTimeline::logSummary(void) {
  if (s_Logged.exchange(true, std::memory_order_relaxed)) {
    return;
  }

  char line[768] = {};
  (void)summary(line, sizeof(line));
#if defined(FURBLE_BOOT_TIMELINE_HOST_TEST) || defined(FURBLE_SIM)
  std::printf("boot: reset_to_app=%u %s\n", static_cast<unsigned>(resetToAppMs()), line);
#else
  ESP_LOGI(LOG_TAG, "timeline reset_to_app=%ums %s", static_cast<unsigned>(resetToAppMs()), line);
#endif
}

void BootTimeline::print(void) {
  std::printf("boot.origin: app_main\n");
  std::printf("boot.reset_to_app_ms: %u\n", static_cast<unsigned>(resetToAppMs()));
  std::printf("boot.reset_note: cycle counter, includes ROM bootloader and PSRAM init\n");

  uint8_t order[COUNT] = {};
  size_t count = 0;
  for (uint8_t index = 0; index < static_cast<uint8_t>(COUNT); index++) {
    const mark_t mark = static_cast<mark_t>(index);
    if (!recorded(mark)) {
      continue;
    }

    const int64_t stamp = at(mark);
    size_t position = count;
    while ((position > 0) && (at(static_cast<mark_t>(order[position - 1])) > stamp)) {
      order[position] = order[position - 1];
      position--;
    }
    order[position] = index;
    count++;
  }

  int64_t previous = 0;
  for (size_t index = 0; index < count; index++) {
    const mark_t mark = static_cast<mark_t>(order[index]);
    const int64_t stamp = at(mark);
    std::printf("mark.%s.at_ms: %" PRId64 "\n", label(mark), stamp / 1000);
    std::printf("mark.%s.delta_ms: %" PRId64 "\n", label(mark), (stamp - previous) / 1000);
    previous = stamp;
  }

  bool first = true;
  std::printf("boot.not_recorded:");
  for (uint8_t index = 0; index < static_cast<uint8_t>(COUNT); index++) {
    const mark_t mark = static_cast<mark_t>(index);
    if (recorded(mark)) {
      continue;
    }
    std::printf("%s%s", first ? " " : ",", label(mark));
    first = false;
  }
  std::printf("\n");
}

}  // namespace Furble
