#ifndef FURBLE_SIM_DRIVER_UART_H
#define FURBLE_SIM_DRIVER_UART_H

#include <cstddef>
#include <cstdint>

#include <esp_err.h>
#include <freertos/FreeRTOS.h>

typedef int uart_port_t;

typedef enum {
  UART_DATA = 0,
  UART_FIFO_OVF,
  UART_BUFFER_FULL,
  UART_BREAK,
  UART_PARITY_ERR,
  UART_FRAME_ERR,
  UART_PATTERN_DET,
} uart_event_type_t;

typedef struct {
  uart_event_type_t type;
  size_t size;
} uart_event_t;

typedef struct {
  int baud_rate;
  int data_bits;
  int parity;
  int stop_bits;
  int flow_ctrl;
  uint8_t rx_flow_ctrl_thresh;
  int source_clk;
  uint32_t flags;
} uart_config_t;

#define UART_NUM_2 2
#define UART_DATA_8_BITS 8
#define UART_PARITY_DISABLE 0
#define UART_STOP_BITS_1 1
#define UART_HW_FLOWCTRL_DISABLE 0
#define UART_SCLK_DEFAULT 0
#define UART_SCLK_REF_TICK 1
#define UART_SCLK_XTAL 2
#define UART_PIN_NO_CHANGE (-1)

esp_err_t uart_driver_install(uart_port_t uart_num,
                              int rx_buffer_size,
                              int tx_buffer_size,
                              int queue_size,
                              QueueHandle_t *uart_queue,
                              int interrupt_flags);
esp_err_t uart_param_config(uart_port_t uart_num, const uart_config_t *config);
esp_err_t uart_set_pin(uart_port_t uart_num, int tx, int rx, int rts, int cts);
esp_err_t uart_enable_pattern_det_baud_intr(uart_port_t uart_num,
                                            char pattern,
                                            int pattern_chr_num,
                                            int chr_tout,
                                            int post_idle,
                                            int pre_idle);
esp_err_t uart_pattern_queue_reset(uart_port_t uart_num, int queue_size);
esp_err_t uart_flush(uart_port_t uart_num);
esp_err_t uart_set_baudrate(uart_port_t uart_num, uint32_t baudrate);
int uart_read_bytes(uart_port_t uart_num,
                    uint8_t *buffer,
                    uint32_t length,
                    TickType_t ticks_to_wait);
int uart_write_bytes(uart_port_t uart_num, const void *buffer, size_t length);
esp_err_t uart_wait_tx_done(uart_port_t uart_num, TickType_t ticks_to_wait);

void furble_sim_uart_update(void);

#ifdef __cplusplus
#include <string>
#include <vector>

// Simulator test hooks, not part of the ESP-IDF API. The fake UART captures
// every uart_write_bytes payload so scripts can assert on $PCAS sends.
const std::vector<std::string> &furble_sim_uart_writes(void);
void furble_sim_uart_clear_writes(void);
void furble_sim_uart_set_mode(const char *mode);
void furble_sim_uart_inject_event(const char *event);
#endif

#endif
