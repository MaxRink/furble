// Host UART shim for the console command suite.
//
// The GPS transmit path is a plain uart_write_bytes(), so the double records
// every sentence the 'gps send' command emits and lets a test assert the
// checksum the command computed.
#ifndef FURBLE_HOST_CONSOLE_DRIVER_UART_H
#define FURBLE_HOST_CONSOLE_DRIVER_UART_H

#include <cstddef>
#include <cstdint>

#include "esp_err.h"

typedef enum {
  UART_NUM_0 = 0,
  UART_NUM_1 = 1,
  UART_NUM_2 = 2,
} uart_port_t;

int uart_write_bytes(uart_port_t port, const void *source, size_t length);
int uart_read_bytes(uart_port_t port, void *buffer, uint32_t length, uint32_t ticks_to_wait);
esp_err_t uart_driver_install(uart_port_t port,
                              int rx_buffer_size,
                              int tx_buffer_size,
                              int queue_size,
                              void *queue,
                              int flags);

#endif
