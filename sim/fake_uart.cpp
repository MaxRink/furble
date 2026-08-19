#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include <driver/uart.h>

#include "FurbleSettings.h"
#include "clock.h"

namespace {

QueueHandle_t gpsQueue = nullptr;
size_t gpsOffset = 0;
std::vector<std::string> uartWrites;
bool gpsEventQueued = false;
uint32_t gpsNextEventMillis = 0;
std::mutex gpsMutex;

// The RMC ground speed of 22.678 knots is 42.0 km/h, a known non-zero value so
// scenarios can assert the GPS Data page speed line. The position resolves to
// 48.11730 N, 11.51667 E, exercising the five decimal place coordinate render.
constexpr char gpsData[] =
    "$GPRMC,123519.00,A,4807.038,N,01131.000,E,22.678,0.0,230394,,,A*67\r\n"
    "$GPGGA,123519.00,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*69\r\n";

uint32_t gpsRatePeriodMillis(void) {
  constexpr uint32_t periods[] = {1000, 1000, 500, 200, 100};
  const uint8_t rate = Furble::Settings::load<Furble::Settings::GPS_RATE>();
  return rate < (sizeof(periods) / sizeof(periods[0])) ? periods[rate] : periods[0];
}

void queueGpsEvent(QueueHandle_t queue) {
  {
    std::lock_guard<std::mutex> lock(gpsMutex);
    gpsQueue = queue;
    gpsOffset = 0;
    gpsEventQueued = true;
    gpsNextEventMillis = Furble::Sim::clockMillis();
  }
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
  std::lock_guard<std::mutex> lock(gpsMutex);
  if (gpsQueue == nullptr || buffer == nullptr || gpsOffset >= sizeof(gpsData) - 1) {
    return 0;
  }

  const size_t remaining = (sizeof(gpsData) - 1) - gpsOffset;
  const size_t count = std::min<size_t>(length, remaining);
  std::memcpy(buffer, gpsData + gpsOffset, count);
  gpsOffset += count;
  if (gpsOffset >= sizeof(gpsData) - 1) {
    gpsEventQueued = false;
    gpsNextEventMillis = Furble::Sim::clockMillis() + gpsRatePeriodMillis();
  }
  return static_cast<int>(count);
}

void furble_sim_uart_update(void) {
  QueueHandle_t queue = nullptr;
  {
    std::lock_guard<std::mutex> lock(gpsMutex);
    const uint32_t now = Furble::Sim::clockMillis();
    const bool due = static_cast<int32_t>(now - gpsNextEventMillis) >= 0;
    if (gpsQueue != nullptr && !gpsEventQueued && gpsOffset >= sizeof(gpsData) - 1 && due) {
      queue = gpsQueue;
    }
  }

  if (queue != nullptr) {
    queueGpsEvent(queue);
  }
}

int uart_write_bytes(uart_port_t, const void *data, size_t length) {
  // Commands to the fake receiver are recorded for inspection, and standby
  // commands are honored so the receiver's next virtual burst follows its
  // requested duty interval.
  if (data != nullptr && length > 0) {
    const std::string command(static_cast<const char *>(data), length);
    uartWrites.push_back(command);
    const std::string prefix = "PCAS12,";
    const size_t offset = command.find(prefix);
    if (offset != std::string::npos) {
      const size_t start = offset + prefix.size();
      const size_t end = command.find_first_not_of("0123456789", start);
      const uint32_t seconds = static_cast<uint32_t>(
          std::strtoul(command.substr(start, end - start).c_str(), nullptr, 10));
      if (seconds > 0) {
        std::lock_guard<std::mutex> lock(gpsMutex);
        if (gpsQueue != nullptr && !gpsEventQueued && gpsOffset >= sizeof(gpsData) - 1) {
          gpsNextEventMillis = Furble::Sim::clockMillis() + seconds * 1000;
        }
      }
    }
  }
  return static_cast<int>(length);
}

const std::vector<std::string> &furble_sim_uart_writes(void) {
  return uartWrites;
}

void furble_sim_uart_clear_writes(void) {
  uartWrites.clear();
}

esp_err_t uart_wait_tx_done(uart_port_t, TickType_t) {
  return ESP_OK;
}
