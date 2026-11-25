// Host conformance for the shared LED/IR pin hand-over (plan 350).
//
// The production src/FurblePinShare.cpp, src/FurbleFeedback.cpp and
// src/FurbleIR.cpp are compiled unchanged. Only the GPIO pad, the RMT
// peripheral and the board identity are doubles, so the arbitration itself is
// what these cases measure:
//
//   shared     the Plus2 puts the red LED and the IR emitter on G19, and both
//              hang off an NPN stage, so the pin is active high and the RMT
//              image is not inverted. A blink during a frame must not touch the
//              pin, the pin must come back from the RMT signal afterwards, and
//              the LED level Feedback asked for mid-frame must be the one
//              restored.
//   separate   the StickC puts the LED on G10 and the emitter on G9, so the
//              hand-over must stay completely inert.
//
// The GPIO double does not model the output matrix: gpio_set_level() always
// reaches the pad here. That is deliberate, because it keeps a regression
// visible. If Feedback writes the pin during a frame, the level changes in the
// double exactly as it would be lost on the device.

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "FurbleFeedback.h"
#include "FurbleIR.h"
#include "FurbleSettings.h"
#include "M5Unified.h"
#include "boundary_host.h"
#include "driver/gpio.h"
#include "driver/rmt_tx.h"
#include "freertos/FreeRTOS.h"
#include "nvs.h"

namespace {

int g_Failures = 0;

void check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "SHARED-PIN FAIL: " << message << '\n';
    g_Failures++;
  }
}

std::mutex g_Mutex;
std::condition_variable g_Condition;
bool g_BlockTransmit = false;
bool g_ReleaseTransmit = false;
bool g_TransmitEntered = false;
std::atomic<int> g_Transmits {0};
std::atomic<int> g_SwitchGpio {0};
std::atomic<uint32_t> g_InvertOut {std::numeric_limits<uint32_t>::max()};
std::vector<uint32_t> g_Carriers;

void initialize(uint8_t output) {
  nvs_test_reset();
  Furble::Settings::init();
  Furble::Settings::save<uint8_t>(Furble::Settings::FB_OUTPUT, output);
  Furble::Settings::save<uint8_t>(Furble::Settings::FB_EVENTS, 0x0f);
  Furble::Settings::save<uint8_t>(Furble::Settings::FB_VOLUME, 50);
  Furble::Settings::save<bool>(Furble::Settings::IR, true);
  Furble::Settings::save<uint8_t>(Furble::Settings::IR_PROTO,
                                  static_cast<uint8_t>(Furble::IR::protocol_t::NIKON));
}

bool waitForBlockedTransmit(void) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  std::unique_lock<std::mutex> lock(g_Mutex);
  return g_Condition.wait_until(lock, deadline,
                                [] { return g_TransmitEntered && g_BlockTransmit; });
}

bool waitForTransmits(int count) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (std::chrono::steady_clock::now() < deadline) {
    if (g_Transmits.load() >= count) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  return g_Transmits.load() >= count;
}

// The hand-over runs after rmt_disable(), so the pad is polled rather than
// sampled once the transmit was recorded.
bool waitForPad(int pin, int pad) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (std::chrono::steady_clock::now() < deadline) {
    if (BoundaryHost::gpioPad(pin) == pad) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  return BoundaryHost::gpioPad(pin) == pad;
}

void releaseTransmit(void) {
  {
    const std::lock_guard<std::mutex> lock(g_Mutex);
    g_ReleaseTransmit = true;
  }
  g_Condition.notify_all();
}

void runShared(void) {
  using Furble::Feedback;

  initialize(Feedback::OUTPUT_LIGHT);
  M5.board = m5::board_t::board_M5StickCPlus2;
  M5.Speaker = m5::Speaker_Class {};
  M5.Power = m5::Power_Class {};

  Feedback::init();
  Furble::IR::init();
  auto &feedback = Feedback::getInstance();
  check(Furble::IR::getInstance().isSupported(), "IR is not supported on the Plus2 row");
  check(BoundaryHost::gpioDirection(19) == GPIO_MODE_OUTPUT,
        "the Plus2 LED pin was not configured");
  // The Plus2 LED and emitter share an NPN stage, so the pin is active high and
  // "off" is a low level. Driving it high at boot would key the emitter through
  // the 22 ohm resistor, which is the defect plan 350 fixes.
  check(BoundaryHost::gpioLevel(19) == 0, "the active-high LED was not left off at boot");

  // Hold the frame inside rmt_transmit() so the test can act while IR owns the
  // pin. Nothing else runs on the UI task here, so the pattern is driven only
  // by the explicit timer passes below.
  g_BlockTransmit = true;
  Furble::IR::getInstance().fire();
  check(waitForBlockedTransmit(), "the IR frame never reached the blocking transmit");
  check(BoundaryHost::gpioPad(19) == BoundaryHost::GPIO_PAD_PERIPHERAL,
        "the RMT channel does not own the pad during a frame");

  // A blink during the frame must be remembered, not written, and must not
  // block on the transfer: the arbitration lock is never held across the RMT
  // wait. Feedback::signal() returning above is half the evidence, the pin
  // level is the other half.
  feedback.signal(Feedback::SHUTTER_FIRED);
  check(BoundaryHost::gpioLevel(19) == 0, "a blink wrote the shared pin during an IR frame");
  check(g_InvertOut.load() == 0, "the Plus2 RMT output was inverted");
  std::cout << "pin.shared frame=blocked blink=suppressed level=0 invert_out=0\n";

  releaseTransmit();
  check(waitForTransmits(1), "the blocked IR frame did not complete");
  check(waitForPad(19, BoundaryHost::GPIO_PAD_GPIO), "the pin was not handed back after a frame");
  check(BoundaryHost::gpioLevel(19) == 1,
        "the LED level requested during the frame was not restored");
  std::cout << "pin.shared handover=returned restored_level=1\n";

  // The restored level belongs to the still-running blink, so the next timer
  // pass ends it on schedule.
  BoundaryHost::advanceTick(50);
  BoundaryHost::serviceTimers();
  check(BoundaryHost::gpioLevel(19) == 0, "the blink did not turn the LED off after the frame");

  // A second frame re-selects the RMT signal because the first one returned the
  // pad, and returns it again afterwards.
  g_BlockTransmit = false;
  Furble::IR::getInstance().fire();
  check(waitForTransmits(2), "the second IR frame did not transmit");
  check(g_SwitchGpio.load() == 1, "the second frame did not reattach the RMT signal");
  check(waitForPad(19, BoundaryHost::GPIO_PAD_GPIO),
        "the pin was not handed back after the second frame");
  check(BoundaryHost::gpioLevel(19) == 0, "the LED level was not restored after the second frame");
  std::cout << "pin.shared frames=2 switch_gpio=1 restored=1\n";
}

void runSeparate(void) {
  using Furble::Feedback;

  initialize(Feedback::OUTPUT_LIGHT);
  M5.board = m5::board_t::board_M5StickC;
  M5.Speaker = m5::Speaker_Class {};
  M5.Power = m5::Power_Class {};

  Feedback::init();
  Furble::IR::init();
  auto &feedback = Feedback::getInstance();
  check(BoundaryHost::gpioDirection(10) == GPIO_MODE_OUTPUT,
        "the StickC LED pin was not configured");
  check(BoundaryHost::gpioLevel(10) == 1, "the active-low LED was not left off at boot");

  Furble::IR::getInstance().fire();
  check(waitForTransmits(1), "the StickC IR frame did not transmit");
  check(g_SwitchGpio.load() == 0, "a board with separate pins ran the pin hand-over");
  check(g_InvertOut.load() == 1, "the StickC RMT output is not inverted");
  check(BoundaryHost::gpioPad(10) == BoundaryHost::GPIO_PAD_GPIO,
        "the LED pin was handed to the IR peripheral");
  check(BoundaryHost::gpioLevel(10) == 1, "the LED pin changed during an IR frame");

  // Feedback keeps full control of its own pin on this board.
  feedback.signal(Feedback::SHUTTER_FIRED);
  check(BoundaryHost::gpioLevel(10) == 0, "the blink did not drive the dedicated LED pin");
  BoundaryHost::advanceTick(50);
  BoundaryHost::serviceTimers();
  check(BoundaryHost::gpioLevel(10) == 1, "the blink did not turn the LED off");
  std::cout << "pin.separate frames=1 switch_gpio=0 blink=unchanged\n";
}

}  // namespace

extern "C" {

struct rmt_channel_t {
  int unused = 0;
};

struct rmt_encoder_t {
  int unused = 0;
};

esp_err_t rmt_new_tx_channel(const rmt_tx_channel_config_t *config, rmt_channel_handle_t *channel) {
  check(config != nullptr && channel != nullptr, "RMT channel config is null");
  auto *handle = new rmt_channel_t;
  g_InvertOut = config->flags.invert_out;
  // rmt_new_tx_channel() routes the pad to the TX signal on the device, which
  // is what makes a plain GPIO write inert from then on.
  BoundaryHost::setGpioPadPeripheral(config->gpio_num);
  *channel = handle;
  return ESP_OK;
}

esp_err_t rmt_del_channel(rmt_channel_handle_t channel) {
  delete channel;
  return ESP_OK;
}

esp_err_t rmt_new_copy_encoder(const rmt_copy_encoder_config_t *, rmt_encoder_handle_t *encoder) {
  *encoder = new rmt_encoder_t;
  return ESP_OK;
}

esp_err_t rmt_del_encoder(rmt_encoder_handle_t encoder) {
  delete encoder;
  return ESP_OK;
}

esp_err_t rmt_apply_carrier(rmt_channel_handle_t channel, const rmt_carrier_config_t *config) {
  if (channel == nullptr || config == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }
  g_Carriers.push_back(config->frequency_hz);
  return ESP_OK;
}

esp_err_t rmt_tx_switch_gpio(rmt_channel_handle_t channel, gpio_num_t gpio_num, bool invert_out) {
  if (channel == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }
  if (invert_out) {
    return ESP_ERR_INVALID_ARG;
  }
  g_SwitchGpio++;
  BoundaryHost::setGpioPadPeripheral(gpio_num);
  return ESP_OK;
}

esp_err_t rmt_enable(rmt_channel_handle_t channel) {
  return channel == nullptr ? ESP_ERR_INVALID_ARG : ESP_OK;
}

esp_err_t rmt_disable(rmt_channel_handle_t channel) {
  return channel == nullptr ? ESP_ERR_INVALID_ARG : ESP_OK;
}

esp_err_t rmt_transmit(rmt_channel_handle_t channel,
                       rmt_encoder_handle_t,
                       const void *payload,
                       size_t payload_bytes,
                       const rmt_transmit_config_t *) {
  if (channel == nullptr || payload == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }
  if (payload_bytes == 0) {
    return ESP_ERR_INVALID_ARG;
  }

  if (g_BlockTransmit) {
    std::unique_lock<std::mutex> lock(g_Mutex);
    g_TransmitEntered = true;
    g_Condition.notify_all();
    g_Condition.wait(lock, [] { return g_ReleaseTransmit; });
  }

  g_Transmits++;
  return ESP_OK;
}

esp_err_t rmt_tx_wait_all_done(rmt_channel_handle_t, int timeout_ms) {
  check(timeout_ms == 1000, "RMT transmit timeout is not the production 1000 ms");
  return ESP_OK;
}

}  // extern "C"

int main(int argc, char **argv) {
  const std::string mode = argc > 1 ? argv[1] : "shared";

  if (mode == "shared") {
    runShared();
  } else if (mode == "separate") {
    runSeparate();
  } else {
    std::cerr << "unknown shared-pin boundary mode: " << mode << '\n';
    return 2;
  }

  furbleHostStopTasks();
  if (g_Failures != 0) {
    std::cerr << "shared_pin_test: " << g_Failures << " failure(s)\n";
    return 1;
  }
  std::cout << "shared_pin_test: PASS (" << mode << ")\n";
  return 0;
}
