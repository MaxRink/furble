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

  /**
   * Queue one IR shutter trigger without blocking the UI task.
   *
   * A queue send with a zero tick timeout and nothing else: no rail mutex, no
   * I2C bus, no wait. The IR task takes this client's hold on the board's EXT_5V
   * rail when it dequeues the request and releases it after transmit() returns,
   * so a GPS duty-cycle RAIL_OFF cannot cut the carrier mid-frame and a queued
   * request cannot cut a burst already in flight (plans/364).
   */
  void fire(void);

  /** Queue one IR shutter trigger with an explicit protocol. */
  void fire(protocol_t protocol);

 private:
  IR() = default;

  static void taskEntry(void *param);
  void task(void);
  bool createChannel(void);
  void releasePin(void);
  void transmit(protocol_t protocol);
  void transmitSymbols(uint32_t carrier_hz, const rmt_symbol_word_t *symbols, size_t symbol_count);

  gpio_num_t m_Pin = GPIO_NUM_NC;
  QueueHandle_t m_Queue = nullptr;
  TaskHandle_t m_Task = nullptr;
  rmt_channel_handle_t m_Channel = nullptr;
  rmt_encoder_handle_t m_Encoder = nullptr;
  bool m_Initialized = false;
  // The pad is muxed to the RMT signal only while a frame runs once the shared
  // pin hand-over has returned it to the GPIO output path.
  bool m_PinReleased = false;
  // The emitter lights on a low pin on the StickC and StickC Plus, so the RMT
  // output is inverted there and the symbol levels stay in mark/space form.
  bool m_MarkLow = false;
};
}  // namespace Furble

#endif
