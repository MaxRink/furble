// Host USB-Serial/JTAG shim for the console command suite.
//
// This is the StickS3 console transport, so the target builds the same branch
// the shipping debug image uses. Reads come from a test controlled byte queue,
// which is how a test types a command line at the real console task.
#ifndef FURBLE_HOST_CONSOLE_DRIVER_USB_SERIAL_JTAG_H
#define FURBLE_HOST_CONSOLE_DRIVER_USB_SERIAL_JTAG_H

#include <cstddef>
#include <cstdint>

#include "esp_err.h"

typedef struct {
  uint32_t tx_buffer_size;
  uint32_t rx_buffer_size;
} usb_serial_jtag_driver_config_t;

#define USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT() {.tx_buffer_size = 256, .rx_buffer_size = 256}

esp_err_t usb_serial_jtag_driver_install(usb_serial_jtag_driver_config_t *config);
int usb_serial_jtag_read_bytes(void *buffer, uint32_t length, uint32_t ticks_to_wait);

#endif
