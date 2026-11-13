// Host conformance for the production IR driver (src/FurbleIR.cpp).
//
// The real driver is compiled unchanged. Only the RMT peripheral, the board
// identity and the FreeRTOS port are host doubles. The test records the exact
// symbol trains the production encoder hands to RMT, so protocol timing is
// asserted from the driver output, not from a copy of the encoder.

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

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
    std::cerr << "IR FAIL: " << message << '\n';
    g_Failures++;
  }
}

struct Capture {
  uint32_t carrier = 0;
  std::vector<rmt_symbol_word_t> symbols;
  bool enabled = false;
};

struct RmtChannel {
  bool enabled = false;
  uint32_t carrier = 0;
  int enables = 0;
  int disables = 0;
  int transmits = 0;
};

std::mutex g_Mutex;
std::condition_variable g_Condition;
std::vector<Capture> g_Captures;
RmtChannel *g_LastChannel = nullptr;
esp_err_t g_WaitResult = ESP_OK;
bool g_BlockTransmit = false;
bool g_ReleaseTransmit = false;
bool g_TransmitEntered = false;

}  // namespace

extern "C" {

struct rmt_channel_t {
  RmtChannel state;
};

struct rmt_encoder_t {
  int unused = 0;
};

esp_err_t rmt_new_tx_channel(const rmt_tx_channel_config_t *config, rmt_channel_handle_t *channel) {
  check(config != nullptr && channel != nullptr, "RMT channel config is null");
  check(config->resolution_hz == 1000000, "RMT resolution is not 1 MHz");
  check(config->mem_block_symbols == SOC_RMT_MEM_WORDS_PER_CHANNEL,
        "RMT memory block is not the SoC default");
  auto *handle = new rmt_channel_t;
  g_LastChannel = &handle->state;
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
  channel->state.carrier = config->frequency_hz;
  return ESP_OK;
}

esp_err_t rmt_enable(rmt_channel_handle_t channel) {
  if (channel == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }
  channel->state.enabled = true;
  channel->state.enables++;
  return ESP_OK;
}

esp_err_t rmt_disable(rmt_channel_handle_t channel) {
  if (channel == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }
  channel->state.enabled = false;
  channel->state.disables++;
  return ESP_OK;
}

esp_err_t rmt_transmit(rmt_channel_handle_t channel,
                       rmt_encoder_handle_t,
                       const void *payload,
                       size_t payload_bytes,
                       const rmt_transmit_config_t *) {
  if (channel == nullptr || payload == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }

  const auto *symbols = static_cast<const rmt_symbol_word_t *>(payload);
  const size_t count = payload_bytes / sizeof(rmt_symbol_word_t);

  if (g_BlockTransmit) {
    std::unique_lock<std::mutex> lock(g_Mutex);
    g_TransmitEntered = true;
    g_Condition.notify_all();
    g_Condition.wait(lock, [] { return g_ReleaseTransmit; });
  }

  {
    const std::lock_guard<std::mutex> lock(g_Mutex);
    channel->state.transmits++;
    Capture capture;
    capture.carrier = channel->state.carrier;
    capture.symbols.assign(symbols, symbols + count);
    capture.enabled = channel->state.enabled;
    g_Captures.push_back(std::move(capture));
  }
  g_Condition.notify_all();
  return ESP_OK;
}

esp_err_t rmt_tx_wait_all_done(rmt_channel_handle_t, int timeout_ms) {
  check(timeout_ms == 1000, "RMT transmit timeout is not the production 1000 ms");
  return g_WaitResult;
}

}  // extern "C"

namespace {

bool waitForCapture(size_t count) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  std::unique_lock<std::mutex> lock(g_Mutex);
  return g_Condition.wait_until(lock, deadline, [count] { return g_Captures.size() >= count; });
}

bool waitForLog(const char *needle, uint32_t timeout_ms = 5000) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  while (std::chrono::steady_clock::now() < deadline) {
    if (BoundaryHost::logContains(needle)) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  return BoundaryHost::logContains(needle);
}

bool waitForBlockedTransmit() {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  std::unique_lock<std::mutex> lock(g_Mutex);
  return g_Condition.wait_until(lock, deadline,
                                [] { return g_TransmitEntered && g_BlockTransmit; });
}

const Capture &lastCapture(size_t index) {
  return g_Captures.at(index);
}

void initializeSettings(void) {
  nvs_test_reset();
  Furble::Settings::init();
  Furble::Settings::save<bool>(Furble::Settings::IR, true);
}

void runProtocols(void) {
  using Furble::IR;
  using Furble::Settings;

  initializeSettings();
  M5.board = m5::board_t::board_M5StickC;
  M5.Speaker.beginFailures = 0;

  IR::init();
  check(IR::getInstance().isSupported(), "IR is not supported on the M5StickC model");

  const struct {
    const char *name;
    uint8_t value;
    uint32_t carrier;
    size_t symbols;
  } cases[] = {
      {"nikon",         static_cast<uint8_t>(IR::protocol_t::NIKON),         38000, 13},
      {"sony",          static_cast<uint8_t>(IR::protocol_t::SONY),          40000, 63},
      {"canon",         static_cast<uint8_t>(IR::protocol_t::CANON),         32700, 2 },
      {"canon-delayed", static_cast<uint8_t>(IR::protocol_t::CANON_DELAYED), 32700, 2 },
      {"out-of-range",  9,                                                   38000, 13},
  };

  size_t expected = 0;
  for (const auto &test : cases) {
    Settings::save<uint8_t>(Settings::IR_PROTO, test.value);
    const size_t commits = nvs_test_commit_count();
    IR::getInstance().fire();
    expected++;
    check(waitForCapture(expected), "IR transmission did not reach the RMT double");
    const Capture &capture = lastCapture(expected - 1);
    check(capture.enabled, "RMT channel was not enabled before transmit");
    check(capture.carrier == test.carrier, "IR carrier does not match the protocol");
    check(capture.symbols.size() == test.symbols, "IR symbol count does not match the protocol");
    check(nvs_test_commit_count() == commits, "IR transmit wrote NVS");
    std::cout << "ir.transmit protocol=" << test.name << " carrier=" << capture.carrier
              << " symbols=" << capture.symbols.size() << '\n';

    if (std::strcmp(test.name, "nikon") == 0) {
      check(capture.symbols[0].duration0 == 2000 && capture.symbols[0].level0 == 1,
            "Nikon header mark is wrong");
      check(capture.symbols[0].duration1 == 27830 && capture.symbols[0].level1 == 0,
            "Nikon header space is wrong");
    }
    if (std::strcmp(test.name, "sony") == 0) {
      check(capture.symbols[0].duration0 == 2400 && capture.symbols[0].duration1 == 600,
            "Sony header is wrong");
      check(capture.symbols.back().duration0 == 1200 || capture.symbols.back().duration0 == 600,
            "Sony final data mark is outside the SIRC timing set");
    }
    if (std::strcmp(test.name, "canon") == 0) {
      check(capture.symbols[0].duration0 == 489, "Canon burst mark is wrong");
      check(capture.symbols[0].duration1 == 7330 - 489, "Canon immediate gap is wrong");
    }
    if (std::strcmp(test.name, "canon-delayed") == 0) {
      check(capture.symbols[0].duration1 == 5360 - 489, "Canon delayed gap is wrong");
    }
  }

  check(g_LastChannel != nullptr, "RMT channel was never created");
  check(g_LastChannel->disables == g_LastChannel->enables,
        "RMT channel was left enabled after a transmit");
  std::cout << "ir.channel enables=" << g_LastChannel->enables
            << " disables=" << g_LastChannel->disables << " transmits=" << g_LastChannel->transmits
            << '\n';
}

void runPsramFallback(void) {
  using Furble::IR;
  using Furble::Settings;

  initializeSettings();
  M5.board = m5::board_t::board_M5StickC;
  setenv("FURBLE_BOUNDARY_PSRAM_FAIL", "1", 1);

  IR::init();
  check(BoundaryHost::psramAllocations() == 1, "the failing PSRAM allocator was not called");
  check(BoundaryHost::logContains("IR PSRAM stack unavailable, using internal RAM"),
        "the PSRAM fallback did not log the production warning");

  const size_t commits = nvs_test_commit_count();
  Settings::save<uint8_t>(Settings::IR_PROTO, static_cast<uint8_t>(IR::protocol_t::NIKON));
  IR::getInstance().fire();
  check(waitForCapture(1), "the fallback IR task did not transmit");
  check(g_Captures.size() == 1, "fallback produced an unexpected extra frame");
  check(nvs_test_commit_count() == commits + 1, "the test setup did not write exactly one setting");
  std::cout << "ir.stack psram=unavailable fallback=internal transmits=1\n";
}

void runQueueTimeout(void) {
  using Furble::IR;
  using Furble::Settings;

  initializeSettings();
  M5.board = m5::board_t::board_M5StickC;
  g_BlockTransmit = true;
  g_ReleaseTransmit = false;
  g_WaitResult = ESP_ERR_TIMEOUT;

  IR::init();
  Settings::save<uint8_t>(Settings::IR_PROTO, static_cast<uint8_t>(IR::protocol_t::SONY));
  IR::getInstance().fire();
  check(waitForBlockedTransmit(), "the first IR frame did not reach the blocking transmit");

  IR::getInstance().fire();
  IR::getInstance().fire();
  check(BoundaryHost::logContains("IR trigger already pending."),
        "a second pending trigger did not log the production queue-full warning");
  std::cout << "ir.queue full=1 dropped=1\n";

  {
    const std::lock_guard<std::mutex> lock(g_Mutex);
    g_ReleaseTransmit = true;
  }
  g_Condition.notify_all();
  check(waitForCapture(2), "the blocked IR frames did not complete");
  check(waitForLog("Failed to transmit IR frame (ESP_ERR_TIMEOUT)"),
        "the RMT timeout did not use the production failure log");
  std::cout << "ir.timeout wait_ms=1000 logged=1\n";

  check(g_LastChannel != nullptr, "RMT channel missing after timeout");
  check(g_LastChannel->disables == g_LastChannel->enables,
        "the RMT channel was left enabled after a timeout");
  check(g_Captures.size() == 2, "queue accounting transmitted an unexpected frame count");
}

void runUnsupported(void) {
  using Furble::IR;

  initializeSettings();
  M5.board = m5::board_t::board_M5Stack;
  IR::init();
  check(!IR::getInstance().isSupported(), "IR reported support on a board without an IR pin");
  check(BoundaryHost::logContains("IR trigger is not supported on this board."),
        "the unsupported board did not log the production message");
  IR::getInstance().fire();
  std::cout << "ir.unsupported board=core fire=no-op\n";
}

}  // namespace

int main(int argc, char **argv) {
  const std::string mode = argc > 1 ? argv[1] : "protocols";

  if (mode == "protocols") {
    runProtocols();
  } else if (mode == "psram-fallback") {
    runPsramFallback();
  } else if (mode == "queue-timeout") {
    runQueueTimeout();
  } else if (mode == "unsupported") {
    runUnsupported();
  } else {
    std::cerr << "unknown IR boundary mode: " << mode << '\n';
    return 2;
  }

  furbleHostStopTasks();
  if (g_Failures != 0) {
    std::cerr << "ir_driver_test: " << g_Failures << " failure(s)\n";
    return 1;
  }
  std::cout << "ir_driver_test: PASS (" << mode << ")\n";
  return 0;
}
