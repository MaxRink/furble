#ifndef FURBLE_BOUNDARY_DRIVER_RMT_TX_H
#define FURBLE_BOUNDARY_DRIVER_RMT_TX_H

#include <cstddef>
#include <cstdint>

#include "driver/gpio.h"
#include "esp_err.h"

#define RMT_CLK_SRC_DEFAULT 1
#define SOC_RMT_MEM_WORDS_PER_CHANNEL 48

struct rmt_channel_t;
using rmt_channel_handle_t = rmt_channel_t *;

struct rmt_encoder_t;
using rmt_encoder_handle_t = rmt_encoder_t *;

struct rmt_symbol_word_t {
  uint32_t duration0 = 0;
  uint32_t level0 = 0;
  uint32_t duration1 = 0;
  uint32_t level1 = 0;
};

struct rmt_tx_channel_config_t {
  gpio_num_t gpio_num = GPIO_NUM_NC;
  int clk_src = RMT_CLK_SRC_DEFAULT;
  uint32_t resolution_hz = 0;
  size_t mem_block_symbols = 0;
  size_t trans_queue_depth = 0;
};

struct rmt_copy_encoder_config_t {};

struct rmt_carrier_config_t {
  uint32_t frequency_hz = 0;
  float duty_cycle = 0.0F;
};

struct rmt_transmit_config_t {
  int loop_count = 0;
};

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t rmt_new_tx_channel(const rmt_tx_channel_config_t *config,
                             rmt_channel_handle_t *channel);
esp_err_t rmt_del_channel(rmt_channel_handle_t channel);
esp_err_t rmt_new_copy_encoder(const rmt_copy_encoder_config_t *config,
                               rmt_encoder_handle_t *encoder);
esp_err_t rmt_del_encoder(rmt_encoder_handle_t encoder);
esp_err_t rmt_apply_carrier(rmt_channel_handle_t channel,
                            const rmt_carrier_config_t *config);
esp_err_t rmt_enable(rmt_channel_handle_t channel);
esp_err_t rmt_disable(rmt_channel_handle_t channel);
esp_err_t rmt_transmit(rmt_channel_handle_t channel,
                       rmt_encoder_handle_t encoder,
                       const void *payload,
                       size_t payload_bytes,
                       const rmt_transmit_config_t *config);
esp_err_t rmt_tx_wait_all_done(rmt_channel_handle_t channel, int timeout_ms);

#ifdef __cplusplus
}
#endif

#endif
