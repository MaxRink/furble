#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "FurblePlatform.h"
#include "FurbleTypes.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "lvgl.h"

// Keep the boundary shim aligned with the IDF esp_err.h values.
static_assert(ESP_ERR_NVS_NO_FREE_PAGES == 0x110a, "NVS no-free-pages value changed");
static_assert(ESP_ERR_NVS_NEW_VERSION_FOUND == 0x110b, "NVS version-found value changed");

const char *LOG_TAG = FURBLE_STR;

namespace {

struct GpioState {
  int direction = -1;
  int level = -1;
};

std::vector<GpioState> g_Gpios;

GpioState &gpioState(int pin) {
  if (pin < 0) {
    static GpioState unused;
    return unused;
  }
  if (g_Gpios.size() <= static_cast<size_t>(pin)) {
    g_Gpios.resize(static_cast<size_t>(pin) + 1);
  }
  return g_Gpios[static_cast<size_t>(pin)];
}

struct Timer {
  lv_timer_cb_t callback = nullptr;
  uint32_t period_ms = 0;
  void *user_data = nullptr;
  bool paused = false;
  uint32_t fires = 0;
};

std::vector<Timer *> g_Timers;
uint32_t g_TimerFires = 0;

uint32_t g_Tick = 0;
uint32_t g_WakeCalls = 0;
uint32_t g_PsramAllocations = 0;

}  // namespace

int gpio_install_isr_service(int flags) {
  (void)flags;
  return 0;
}

int gpio_set_direction(gpio_num_t pin, gpio_mode_t mode) {
  gpioState(pin).direction = static_cast<int>(mode);
  return 0;
}

int gpio_set_level(gpio_num_t pin, int level) {
  gpioState(pin).level = level;
  return 0;
}

lv_timer_t *lv_timer_create(lv_timer_cb_t callback, uint32_t period_ms, void *user_data) {
  auto *timer = new Timer {callback, period_ms, user_data, false, 0};
  g_Timers.push_back(timer);
  return reinterpret_cast<lv_timer_t *>(timer);
}

void lv_timer_resume(lv_timer_t *timer) {
  if (timer != nullptr) {
    reinterpret_cast<Timer *>(timer)->paused = false;
  }
}

void lv_timer_pause(lv_timer_t *timer) {
  if (timer != nullptr) {
    reinterpret_cast<Timer *>(timer)->paused = true;
  }
}

void *lv_timer_get_user_data(lv_timer_t *timer) {
  return timer == nullptr ? nullptr : reinterpret_cast<Timer *>(timer)->user_data;
}

namespace BoundaryHost {

void serviceTimers() {
  g_Tick += 10;
  for (auto *timer : g_Timers) {
    if (!timer->paused && timer->callback != nullptr) {
      timer->fires++;
      g_TimerFires++;
      timer->callback(reinterpret_cast<lv_timer_t *>(timer));
    }
  }
}

uint32_t timerFireCount() {
  return g_TimerFires;
}

void advanceTick(uint32_t ms) {
  g_Tick += ms;
}

uint32_t tick() {
  return g_Tick;
}

uint32_t wakeCalls() {
  return g_WakeCalls;
}

uint32_t psramAllocations() {
  return g_PsramAllocations;
}

int gpioDirection(int pin) {
  return gpioState(pin).direction;
}

int gpioLevel(int pin) {
  return gpioState(pin).level;
}

bool logContains(const char *needle) {
  if (needle == nullptr) {
    return false;
  }
  auto &store = detail::logStore();
  const std::lock_guard<std::mutex> lock(store.mutex);
  for (const auto &line : store.lines) {
    if (line.find(needle) != std::string::npos) {
      return true;
    }
  }
  return false;
}

}  // namespace BoundaryHost

namespace Furble {

Platform &Platform::getInstance() {
  static Platform instance;
  return instance;
}

uint32_t Platform::tick(void) {
  return BoundaryHost::tick();
}

void Platform::wakeM5PM1(void) {
  g_WakeCalls++;
}

void Platform::restart(void) {}

}  // namespace Furble

extern "C" BaseType_t xTaskCreateWithCaps(TaskFunction_t task,
                                          const char *name,
                                          uint32_t stack_size,
                                          void *parameter,
                                          UBaseType_t priority,
                                          TaskHandle_t *task_handle,
                                          uint32_t caps) {
  (void)caps;
  g_PsramAllocations++;
  const char *fail = std::getenv("FURBLE_BOUNDARY_PSRAM_FAIL");
  if ((fail != nullptr) && (std::strcmp(fail, "1") == 0)) {
    return pdFAIL;
  }
  return xTaskCreate(task, name, stack_size, parameter, priority, task_handle);
}