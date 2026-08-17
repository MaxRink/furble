#include <algorithm>
#include <cstring>

#include <driver/uart.h>

namespace {

QueueHandle_t gpsQueue = nullptr;
size_t gpsOffset = 0;

constexpr char gpsData[] =
    "$GPRMC,123519.00,A,4807.038,N,01131.000,E,0.0,0.0,230394,,,A*5E\r\n"
    "$GPGGA,123519.00,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*69\r\n";

void queueGpsEvent(QueueHandle_t queue) {
  gpsQueue = queue;
  gpsOffset = 0;
  uart_event_t event = {.type = UART_PATTERN_DET, .size = sizeof(gpsData) - 1};
  xQueueSend(queue, &event, 0);
}

}  // namespace

esp_err_t uart_driver_install(uart_port_t, int, int, int, QueueHandle_t *uart_queue, int) {
  gpsQueue = xQueueCreate(32, sizeof(uart_event_t));
  if (uart_queue != nullptr) {
    *uart_queue = gpsQueue;
  }
  furble_sim_queue_set_reset_callback(gpsQueue, queueGpsEvent);
  queueGpsEvent(gpsQueue);
  return ESP_OK;
}

esp_err_t uart_param_config(uart_port_t, const uart_config_t *) {
  return ESP_OK;
}

esp_err_t uart_set_pin(uart_port_t, int, int, int, int) {
  return ESP_OK;
}

esp_err_t uart_enable_pattern_det_baud_intr(uart_port_t, char, int, int, int, int) {
  return ESP_OK;
}

esp_err_t uart_pattern_queue_reset(uart_port_t, int) {
  return ESP_OK;
}

esp_err_t uart_flush(uart_port_t) {
  return ESP_OK;
}

esp_err_t uart_set_baudrate(uart_port_t, uint32_t) {
  return ESP_OK;
}

int uart_read_bytes(uart_port_t, uint8_t *buffer, uint32_t length, TickType_t) {
  if (gpsQueue == nullptr || buffer == nullptr || gpsOffset >= sizeof(gpsData) - 1) {
    return 0;
  }

  const size_t remaining = (sizeof(gpsData) - 1) - gpsOffset;
  const size_t count = std::min<size_t>(length, remaining);
  std::memcpy(buffer, gpsData + gpsOffset, count);
  gpsOffset += count;
  if (gpsOffset >= sizeof(gpsData) - 1) {
    queueGpsEvent(gpsQueue);
  }
  return static_cast<int>(count);
}
