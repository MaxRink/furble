// Host doubles for the production src/FurblePlatform.cpp rail target.
//
// The target compiles the whole production Platform against peripheral doubles.
// Only the lowest boundaries are replaced: the ESP-IDF services the platform
// calls (gpio, esp_pm, esp_sleep, esp_timer) and the four furble subsystems the
// rail path never reaches (Control, Feedback, SD, Power plus TimeKeeper). The
// M5PM1 driver and M5Unified are doubled in platform_shims/ and shims/.
//
// The rail accounting, the PWR_CFG 0x06 read-modify-write and the output-mode
// interlock are NOT doubled: they are the production code under test.

#include <cstdint>
#include <cstdio>

#include "FurbleControl.h"
#include "FurbleFeedback.h"
#include "FurblePower.h"
#include "FurbleSD.h"
#include "FurbleSettings.h"
#include "FurbleTimeKeeper.h"
#include "FurbleTypes.h"
#include "driver/gpio.h"
#include "esp_pm.h"
#include "esp_sleep.h"
#include "esp_timer.h"

const char *LOG_TAG = FURBLE_STR;

namespace {

int64_t g_NowUs = 0;
int g_GpioLevel = -1;
gpio_config_t g_LastGpioConfig = {};
uint32_t g_GpioConfigCalls = 0;

}  // namespace

// ---- ESP-IDF doubles --------------------------------------------------

int64_t esp_timer_get_time(void) {
  // Monotonic but not wall clock: the platform only divides it by 1000.
  g_NowUs += 1000;
  return g_NowUs;
}

esp_sleep_wakeup_cause_t esp_sleep_get_wakeup_cause(void) {
  return ESP_SLEEP_WAKEUP_UNDEFINED;
}

esp_err_t esp_sleep_enable_timer_wakeup(uint64_t) {
  return ESP_OK;
}

esp_err_t esp_sleep_enable_gpio_wakeup(void) {
  return ESP_OK;
}

esp_err_t esp_sleep_disable_wakeup_source(esp_sleep_wakeup_cause_t) {
  return ESP_OK;
}

void esp_deep_sleep_start(void) {}

esp_err_t esp_pm_configure(const esp_pm_config_t *) {
  return ESP_OK;
}

esp_err_t esp_pm_get_configuration(esp_pm_config_t *config) {
  if (config == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }
  config->max_freq_mhz = 160;
  config->min_freq_mhz = 40;
  config->light_sleep_enable = true;
  return ESP_OK;
}

esp_err_t esp_pm_dump_locks(FILE *) {
  return ESP_OK;
}

int gpio_install_isr_service(int) {
  return 0;
}

int gpio_set_direction(gpio_num_t, gpio_mode_t) {
  return 0;
}

int gpio_set_level(gpio_num_t, int level) {
  g_GpioLevel = level;
  return 0;
}

int gpio_get_level(gpio_num_t) {
  return g_GpioLevel;
}

esp_err_t gpio_config(const gpio_config_t *config) {
  g_GpioConfigCalls++;
  if (config != nullptr) {
    g_LastGpioConfig = *config;
  }
  return ESP_OK;
}

esp_err_t gpio_wakeup_enable(gpio_num_t, gpio_int_type_t) {
  return ESP_OK;
}

esp_err_t gpio_wakeup_disable(gpio_num_t) {
  return ESP_OK;
}

// ---- furble subsystem doubles ----------------------------------------

namespace Furble {

Control &Control::getInstance(void) {
  static Control instance;
  return instance;
}

bool Control::disconnect(uint32_t, bool) {
  return true;
}

SD &SD::getInstance(void) {
  static SD instance;
  return instance;
}

bool SD::powerOff(void) {
  return true;
}

Feedback::output_t g_FeedbackOutput = Feedback::OUTPUT_OFF;

bool Feedback::outputIncludesSound(output_t output) {
  g_FeedbackOutput = output;
  return false;
}

TimeKeeper &TimeKeeper::getInstance(void) {
  static TimeKeeper instance;
  return instance;
}

namespace {

// The clock double's state. The rail target never enables the prediction, so
// its shutdown path writes no record and its adoptTimedWake() adopts nothing,
// which is what that target's comment says; `platform-timed-wake` sets both.
bool g_PredictionEnabled = false;
bool g_AdoptResult = false;
unsigned g_AdoptCalls = 0;
size_t g_AdoptLength = 0;
uint8_t g_AdoptBytes[TimeKeeperPolicy::RECORD_BYTES] = {};
uint32_t g_ProgrammedSeconds = 0;

}  // namespace

void TimeKeeperDouble::expectedPrediction(uint32_t programmed_seconds, uint8_t *buffer) {
  if (buffer == nullptr) {
    return;
  }
  // Not a valid TimeSample on purpose: the Platform only carries the record, so
  // a byte pattern with a tag and the armed interval proves the round trip
  // without pretending to be a decodable clock. The pattern is distinct in every
  // byte, so a shifted write cannot pass.
  for (size_t i = 0; i < TimeKeeperPolicy::RECORD_BYTES; i++) {
    buffer[i] = static_cast<uint8_t>(0x40 + i);
  }
  buffer[0] = 0xA5;
  buffer[1] = static_cast<uint8_t>(programmed_seconds & 0xFF);
  buffer[2] = static_cast<uint8_t>((programmed_seconds >> 8) & 0xFF);
}

void TimeKeeper::flush(void) {}

size_t TimeKeeper::prepareTimedWake(uint32_t programmed_seconds, uint8_t *buffer, size_t capacity) {
  g_ProgrammedSeconds = programmed_seconds;
  if (!g_PredictionEnabled || (buffer == nullptr) || (capacity < TimeKeeperPolicy::RECORD_BYTES)) {
    return 0;
  }
  TimeKeeperDouble::expectedPrediction(programmed_seconds, buffer);
  return TimeKeeperPolicy::RECORD_BYTES;
}

bool TimeKeeper::adoptTimedWake(const uint8_t *buffer, size_t length) {
  g_AdoptCalls++;
  g_AdoptLength = length;
  for (size_t i = 0; i < sizeof(g_AdoptBytes); i++) {
    g_AdoptBytes[i] = 0;
  }
  if ((buffer != nullptr) && (length <= sizeof(g_AdoptBytes))) {
    for (size_t i = 0; i < length; i++) {
      g_AdoptBytes[i] = buffer[i];
    }
  }
  return g_AdoptResult;
}

void TimeKeeper::setPredictionEnabled(bool enabled) {
  g_PredictionEnabled = enabled;
}

void TimeKeeper::setAdoptResult(bool result) {
  g_AdoptResult = result;
}

unsigned TimeKeeper::adoptCalls(void) {
  return g_AdoptCalls;
}

size_t TimeKeeper::lastAdoptLength(void) {
  return g_AdoptLength;
}

const uint8_t *TimeKeeper::lastAdoptBytes(void) {
  return g_AdoptBytes;
}

uint32_t TimeKeeper::lastProgrammedSeconds(void) {
  return g_ProgrammedSeconds;
}

void TimeKeeper::resetForTest(void) {
  g_PredictionEnabled = false;
  g_AdoptResult = false;
  g_AdoptCalls = 0;
  g_AdoptLength = 0;
  g_ProgrammedSeconds = 0;
  for (size_t i = 0; i < sizeof(g_AdoptBytes); i++) {
    g_AdoptBytes[i] = 0;
  }
}

Power &Power::getInstance(void) {
  static Power instance;
  return instance;
}

void Power::init(void) {}

esp_err_t Power::configure(uint8_t) {
  return ESP_OK;
}

template <>
bool Settings::load<bool>(Settings::type_t) {
  return false;
}

template <>
uint8_t Settings::load<uint8_t>(Settings::type_t) {
  return 0;
}

}  // namespace Furble
