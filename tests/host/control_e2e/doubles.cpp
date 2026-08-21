// Implementations of the slim Platform, Power and Settings doubles the real
// Control links against in the end-to-end harness.

#include <array>
#include <atomic>

#include <NimBLEDevice.h>  // esp_power_level_t

#include "freertos/FreeRTOS.h"

#include "FurblePlatform.h"
#include "FurblePower.h"
#include "FurbleSettings.h"

namespace Furble {

// Platform ------------------------------------------------------------------

Platform &Platform::getInstance() {
  static Platform instance;
  return instance;
}

uint32_t Platform::tick(void) {
  return xTaskGetTickCount();
}

void Platform::watchdogFeed(void) {}

// Power ---------------------------------------------------------------------

Power &Power::getInstance() {
  static Power instance;
  return instance;
}

void Power::acquire(LockType type, const char *) {
  m_Counts[static_cast<size_t>(type)].fetch_add(1);
}

void Power::release(LockType type, const char *) {
  m_Counts[static_cast<size_t>(type)].fetch_sub(1);
}

uint32_t Power::getCount(LockType type) const {
  return m_Counts[static_cast<size_t>(type)].load();
}

// Settings ------------------------------------------------------------------

namespace {
std::array<std::atomic<bool>, 5> g_Bools = {};
}  // namespace

void Settings::init(void) {}

void Settings::setBool(type_t type, bool value) {
  g_Bools[static_cast<size_t>(type)].store(value);
}

template <>
bool Settings::load<bool>(type_t type) {
  return g_Bools[static_cast<size_t>(type)].load();
}

template <>
esp_power_level_t Settings::load<esp_power_level_t>(type_t) {
  return ESP_PWR_LVL_P3;
}

template <>
void Settings::save<bool>(type_t type, const bool &value) {
  g_Bools[static_cast<size_t>(type)].store(value);
}

}  // namespace Furble
