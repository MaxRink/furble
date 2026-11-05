#include "FurbleCompanionDebug.h"

#if defined(FURBLE_BLE_DEBUG)

#include <NimBLEDevice.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstdio>

#include "FurbleCompanion.h"

namespace Furble {
namespace {

constexpr size_t STATUS_TEXT = 512;

const char *resetReasonName(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_POWERON:
      return "poweron";
    case ESP_RST_EXT:
      return "external";
    case ESP_RST_SW:
      return "software";
    case ESP_RST_PANIC:
      return "panic";
    case ESP_RST_INT_WDT:
      return "interrupt-wdt";
    case ESP_RST_TASK_WDT:
      return "task-wdt";
    case ESP_RST_WDT:
      return "wdt";
    case ESP_RST_DEEPSLEEP:
      return "deepsleep";
    case ESP_RST_BROWNOUT:
      return "brownout";
    case ESP_RST_SDIO:
      return "sdio";
    default:
      return "unknown";
  }
}

size_t formatStatus(char *out, size_t max) {
  const CompanionGatt &companion = CompanionGatt::getInstance();
  const int written = std::snprintf(
      out, max,
      "version: %s\n"
      "reset: %s (%d)\n"
      "uptime_ms: %llu\n"
      "tasks: %u\n"
      "heap.internal.free: %u\n"
      "heap.internal.largest: %u\n"
      "heap.spiram.free: %u\n"
      "heap.spiram.largest: %u\n"
      "bonds: %d\n"
      "ble.connections: %d\n"
      "companion.enabled: %d\n"
      "companion.connected: %d\n"
      "companion.encrypted: %d\n"
      "companion.authenticated: %d\n"
      "companion.bond: %d\n"
      "companion.window: %d\n"
      "companion.advertising: %d\n",
      FURBLE_VERSION, resetReasonName(esp_reset_reason()), static_cast<int>(esp_reset_reason()),
      static_cast<unsigned long long>(esp_timer_get_time() / 1000ULL),
      static_cast<unsigned>(uxTaskGetNumberOfTasks()),
      static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
      static_cast<unsigned>(
          heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
      static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)),
      static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)),
      NimBLEDevice::getNumBonds(),
      (NimBLEDevice::getServer() != nullptr) ? NimBLEDevice::getServer()->getConnectedCount() : 0,
      companion.isEnabled() ? 1 : 0, companion.isConnected() ? 1 : 0,
      companion.isEncrypted() ? 1 : 0, companion.isAuthenticated() ? 1 : 0,
      companion.hasBond() ? 1 : 0, companion.isPairingWindow() ? 1 : 0,
      companion.isAdvertising() ? 1 : 0);
  if (written < 0) {
    return 0;
  }
  return static_cast<size_t>(written) < max ? static_cast<size_t>(written) : (max - 1);
}

}  // namespace

CompanionDebug &CompanionDebug::getInstance(void) {
  static CompanionDebug instance;
  return instance;
}

void CompanionDebug::attach(NimBLEServer *server) {
  if ((server == nullptr) || m_Attached) {
    return;
  }

  // Read-only and deliberately unencrypted: the log/status must answer when
  // the host GATT cache or companion auth is broken. Debug image only.
  NimBLEService *service = server->createService(SERVICE_UUID);
  m_Status = service->createCharacteristic(STATUS_UUID, NIMBLE_PROPERTY::READ, STATUS_TEXT);
  m_Attached = true;
}

void CompanionDebug::update(void) {
  if (!m_Attached || (m_Status == nullptr)) {
    return;
  }

  char statusText[STATUS_TEXT] = {};
  const size_t statusLength = formatStatus(statusText, sizeof(statusText));
  m_Status->setValue(reinterpret_cast<const uint8_t *>(statusText), statusLength);
}

}  // namespace Furble

#endif  // FURBLE_BLE_DEBUG
