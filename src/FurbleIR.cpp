#include <array>

#include <M5Unified.h>
#include <esp_log.h>
#include <soc/soc_caps.h>

#include "FurbleIR.h"
#include "FurbleSettings.h"
#include "FurbleTypes.h"

namespace Furble {
namespace {

constexpr uint32_t RMT_RESOLUTION_HZ = 1000000;

/** Give up on a stalled transmission instead of holding the APB lock forever. */
constexpr int RMT_TX_TIMEOUT_MS = 1000;

// Nikon ML-L3 timing source: SB-Projects and Gough Lui, both listed in
// plans/22-ir-remote-trigger.md.
constexpr uint32_t NIKON_CARRIER_HZ = 38000;
constexpr uint32_t NIKON_HEADER_MARK_US = 2000;
constexpr uint32_t NIKON_HEADER_SPACE_US = 27830;
constexpr uint32_t NIKON_PULSE1_MARK_US = 390;
constexpr uint32_t NIKON_PULSE1_SPACE_US = 1580;
constexpr uint32_t NIKON_PULSE2_MARK_US = 410;
constexpr uint32_t NIKON_PULSE2_SPACE_US = 3580;
constexpr uint32_t NIKON_STOP_MARK_US = 400;
// The sources disagree on whether 63.2 ms is the gap between frames or the
// frame repeat period. Treat it as the gap for now, waveform verification on
// hardware will settle it.
constexpr uint32_t NIKON_FRAME_GAP_US = 63000;
constexpr uint32_t NIKON_GAP_PART_US = NIKON_FRAME_GAP_US / 2;

// Sony SIRC timing source: Ed Cheung and Ken Shirriff, both listed in
// plans/22-ir-remote-trigger.md.
constexpr uint32_t SONY_CARRIER_HZ = 40000;
constexpr uint32_t SONY_REPEAT_INTERVAL_US = 45000;
constexpr uint32_t SONY_ADDRESS = 0x1E3A;
constexpr uint32_t SONY_COMMAND = 0x2D;

// Canon RC-1 timing source: the TU Berlin reverse engineering notes, listed
// in plans/22-ir-remote-trigger.md. The Canon RC-6 handset uses this same
// two-burst scheme. It is not the Philips RC-6 protocol.
constexpr uint32_t CANON_CARRIER_HZ = 32700;
constexpr uint32_t CANON_BURST_US = 489;
constexpr uint32_t CANON_IMMEDIATE_GAP_US = 7330;
constexpr uint32_t CANON_DELAYED_GAP_US = 5360;

rmt_symbol_word_t symbol(uint32_t duration0, uint32_t level0, uint32_t duration1, uint32_t level1) {
  rmt_symbol_word_t result = {};
  result.duration0 = duration0;
  result.level0 = level0;
  result.duration1 = duration1;
  result.level1 = level1;
  return result;
}

std::array<rmt_symbol_word_t, 13> encodeNikon(void) {
  // The 63 ms spaces are split because one RMT duration is limited to 32767 us.
  return {
      symbol(NIKON_HEADER_MARK_US, 1, NIKON_HEADER_SPACE_US, 0),
      symbol(NIKON_PULSE1_MARK_US, 1, NIKON_PULSE1_SPACE_US, 0),
      symbol(NIKON_PULSE2_MARK_US, 1, NIKON_PULSE2_SPACE_US, 0),
      symbol(NIKON_STOP_MARK_US, 1, NIKON_GAP_PART_US, 0),
      symbol(NIKON_GAP_PART_US, 0, NIKON_HEADER_MARK_US, 1),
      symbol(NIKON_HEADER_SPACE_US, 0, NIKON_PULSE1_MARK_US, 1),
      symbol(NIKON_PULSE1_SPACE_US, 0, NIKON_PULSE2_MARK_US, 1),
      symbol(NIKON_PULSE2_SPACE_US, 0, NIKON_STOP_MARK_US, 1),
      symbol(NIKON_GAP_PART_US, 0, NIKON_GAP_PART_US, 0),
      symbol(NIKON_HEADER_MARK_US, 1, NIKON_HEADER_SPACE_US, 0),
      symbol(NIKON_PULSE1_MARK_US, 1, NIKON_PULSE1_SPACE_US, 0),
      symbol(NIKON_PULSE2_MARK_US, 1, NIKON_PULSE2_SPACE_US, 0),
      symbol(NIKON_STOP_MARK_US, 1, 1, 0),
  };
}

std::array<rmt_symbol_word_t, 63> encodeSony(void) {
  constexpr uint32_t header_mark_us = 2400;
  constexpr uint32_t header_space_us = 600;
  constexpr uint32_t zero_mark_us = 600;
  constexpr uint32_t one_mark_us = 1200;
  constexpr size_t bits_per_frame = 20;
  constexpr size_t symbols_per_frame = bits_per_frame + 1;

  std::array<rmt_symbol_word_t, symbols_per_frame * 3> symbols = {};
  const uint32_t data = SONY_COMMAND | (SONY_ADDRESS << 7);
  uint32_t frame_duration_us = header_mark_us + header_space_us;

  for (size_t bit = 0; bit < bits_per_frame; bit++) {
    const bool one = (data & (1UL << bit)) != 0;
    frame_duration_us += (one ? one_mark_us : zero_mark_us) + header_space_us;
  }

  const uint32_t repeat_space_us = SONY_REPEAT_INTERVAL_US - (frame_duration_us - header_space_us);

  for (size_t frame = 0; frame < 3; frame++) {
    const size_t offset = frame * symbols_per_frame;
    symbols[offset] = symbol(header_mark_us, 1, header_space_us, 0);

    for (size_t bit = 0; bit < bits_per_frame; bit++) {
      const bool one = (data & (1UL << bit)) != 0;
      const uint32_t space_us =
          (frame < 2 && bit == bits_per_frame - 1) ? repeat_space_us : header_space_us;
      symbols[offset + bit + 1] = symbol(one ? one_mark_us : zero_mark_us, 1, space_us, 0);
    }
  }

  return symbols;
}

std::array<rmt_symbol_word_t, 2> encodeCanon(bool delayed) {
  const uint32_t gap_us = delayed ? CANON_DELAYED_GAP_US : CANON_IMMEDIATE_GAP_US;
  return {
      symbol(CANON_BURST_US, 1, gap_us - CANON_BURST_US, 0),
      symbol(CANON_BURST_US, 1, 1, 0),
  };
}

gpio_num_t pinForBoard(m5::board_t board) {
  switch (board) {
    case m5::board_t::board_M5StickC:
    case m5::board_t::board_M5StickCPlus:
      // G9 is only free because these boards run the flash in DIO mode.
      // QIO would claim GPIO 9/10 as SD2/SD3.
      return static_cast<gpio_num_t>(9);
    case m5::board_t::board_M5StickCPlus2:
      return static_cast<gpio_num_t>(19);
    case m5::board_t::board_M5StickS3:
      return static_cast<gpio_num_t>(46);
    case m5::board_t::board_M5Stack:
    case m5::board_t::board_M5StackCore2:
    default:
      return GPIO_NUM_NC;
  }
}

}  // namespace

IR &IR::getInstance(void) {
  static IR instance;
  return instance;
}

IR::protocol_t IR::clampProtocol(uint8_t value) {
  if (value > static_cast<uint8_t>(protocol_t::CANON_DELAYED)) {
    return protocol_t::NIKON;
  }
  return static_cast<protocol_t>(value);
}

void IR::init(void) {
  auto &instance = getInstance();
  if (instance.m_Initialized) {
    return;
  }

  instance.m_Initialized = true;
  instance.m_Pin = pinForBoard(M5.getBoard());
  if (!instance.isSupported()) {
    ESP_LOGI(LOG_TAG, "IR trigger is not supported on this board.");
    return;
  }

  instance.m_Queue = xQueueCreate(1, sizeof(protocol_t));
  if (instance.m_Queue == nullptr) {
    ESP_LOGE(LOG_TAG, "Failed to create IR trigger queue.");
    return;
  }

  BaseType_t result = xTaskCreate(taskEntry, "ir", 4096, &instance, 3, &instance.m_Task);
  if (result != pdPASS) {
    ESP_LOGE(LOG_TAG, "Failed to create IR trigger task.");
    vQueueDelete(instance.m_Queue);
    instance.m_Queue = nullptr;
  }
}

bool IR::isSupported(void) const {
  return m_Pin != GPIO_NUM_NC;
}

void IR::fire(void) {
  fire(clampProtocol(Settings::load<Settings::IR_PROTO>()));
}

void IR::fire(protocol_t protocol) {
  if (!isSupported() || !Settings::load<Settings::IR>() || m_Queue == nullptr) {
    return;
  }

  if (xQueueSend(m_Queue, &protocol, 0) != pdTRUE) {
    ESP_LOGW(LOG_TAG, "IR trigger already pending.");
  }
}

void IR::taskEntry(void *param) {
  static_cast<IR *>(param)->task();
}

void IR::task(void) {
  protocol_t protocol;
  while (true) {
    if (xQueueReceive(m_Queue, &protocol, portMAX_DELAY) == pdTRUE) {
      transmit(protocol);
    }
  }
}

bool IR::createChannel(void) {
  if (m_Channel != nullptr) {
    return true;
  }

  rmt_tx_channel_config_t tx = {};
  tx.gpio_num = m_Pin;
  tx.clk_src = RMT_CLK_SRC_DEFAULT;
  tx.resolution_hz = RMT_RESOLUTION_HZ;
  // One hardware memory block, 64 symbols on the ESP32 and 48 on the S3. The
  // driver rejects anything smaller. Longer trains (the 63 symbol Sony frame)
  // ping-pong through the block via the threshold interrupt.
  tx.mem_block_symbols = SOC_RMT_MEM_WORDS_PER_CHANNEL;
  tx.trans_queue_depth = 4;

  esp_err_t err = rmt_new_tx_channel(&tx, &m_Channel);
  if (err != ESP_OK) {
    ESP_LOGE(LOG_TAG, "Failed to create IR RMT channel (%s).", esp_err_to_name(err));
    m_Channel = nullptr;
    return false;
  }

  rmt_copy_encoder_config_t copy = {};
  err = rmt_new_copy_encoder(&copy, &m_Encoder);
  if (err != ESP_OK) {
    ESP_LOGE(LOG_TAG, "Failed to create IR RMT encoder (%s).", esp_err_to_name(err));
    rmt_del_channel(m_Channel);
    m_Channel = nullptr;
    return false;
  }

  return true;
}

void IR::transmitSymbols(uint32_t carrier_hz,
                         const rmt_symbol_word_t *symbols,
                         size_t symbol_count) {
  if (!createChannel()) {
    return;
  }

  rmt_carrier_config_t carrier = {};
  carrier.frequency_hz = carrier_hz;
  carrier.duty_cycle = 0.33;

  esp_err_t err = rmt_apply_carrier(m_Channel, &carrier);
  if (err != ESP_OK) {
    ESP_LOGE(LOG_TAG, "Failed to apply IR carrier (%s).", esp_err_to_name(err));
    return;
  }

  err = rmt_enable(m_Channel);
  if (err != ESP_OK) {
    ESP_LOGE(LOG_TAG, "Failed to enable IR RMT channel (%s).", esp_err_to_name(err));
    return;
  }

  rmt_transmit_config_t transmit_config = {};
  transmit_config.loop_count = 0;
  err = rmt_transmit(m_Channel, m_Encoder, symbols, symbol_count * sizeof(rmt_symbol_word_t),
                     &transmit_config);
  if (err == ESP_OK) {
    // A bounded wait, then disable regardless. The driver holds its APB lock
    // between enable and disable, so a hardware stall must not pin it forever.
    err = rmt_tx_wait_all_done(m_Channel, RMT_TX_TIMEOUT_MS);
  }

  esp_err_t disable_err = rmt_disable(m_Channel);
  if (err != ESP_OK) {
    ESP_LOGE(LOG_TAG, "Failed to transmit IR frame (%s).", esp_err_to_name(err));
  } else if (disable_err != ESP_OK) {
    ESP_LOGE(LOG_TAG, "Failed to disable IR RMT channel (%s).", esp_err_to_name(disable_err));
  }
}

void IR::transmit(protocol_t protocol) {
  switch (protocol) {
    case protocol_t::NIKON:
    {
      const auto symbols = encodeNikon();
      transmitSymbols(NIKON_CARRIER_HZ, symbols.data(), symbols.size());
      break;
    }
    case protocol_t::SONY:
    {
      const auto symbols = encodeSony();
      transmitSymbols(SONY_CARRIER_HZ, symbols.data(), symbols.size());
      break;
    }
    case protocol_t::CANON:
    case protocol_t::CANON_DELAYED:
    {
      const auto symbols = encodeCanon(protocol == protocol_t::CANON_DELAYED);
      transmitSymbols(CANON_CARRIER_HZ, symbols.data(), symbols.size());
      break;
    }
  }
}
}  // namespace Furble
