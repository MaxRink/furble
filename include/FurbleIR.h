#ifndef FURBLE_IR_H
#define FURBLE_IR_H

#include <cstddef>
#include <cstdint>

#include <driver/rmt_tx.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

namespace Furble {
class IR {
 public:
  enum class protocol_t : uint8_t {
    NIKON = 0,
    SONY = 1,
    CANON = 2,
    CANON_DELAYED = 3,
  };

  static IR &getInstance(void);

  /** Map a stored protocol value to a valid protocol, out of range means Nikon. */
  static protocol_t clampProtocol(uint8_t value);

  IR(IR const &) = delete;
  IR(IR &&) = delete;
  IR &operator=(IR const &) = delete;
  IR &operator=(IR &&) = delete;

  static void init(void);

  bool isSupported(void) const;

  /** Queue one IR shutter trigger without blocking the UI task. */
  void fire(void);

  /** Queue one IR shutter trigger with an explicit protocol. */
  void fire(protocol_t protocol);

 private:
  IR() = default;

  static void taskEntry(void *param);
  void task(void);
  bool createChannel(void);
  void transmit(protocol_t protocol);
  void transmitSymbols(uint32_t carrier_hz, const rmt_symbol_word_t *symbols, size_t symbol_count);

  gpio_num_t m_Pin = GPIO_NUM_NC;
  QueueHandle_t m_Queue = nullptr;
  TaskHandle_t m_Task = nullptr;
  rmt_channel_handle_t m_Channel = nullptr;
  rmt_encoder_handle_t m_Encoder = nullptr;
  bool m_Initialized = false;
};
}  // namespace Furble

#endif
