#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include <driver/uart.h>

#include "FurbleSettings.h"
#include "clock.h"

namespace {

QueueHandle_t gpsQueue = nullptr;
size_t gpsOffset = 0;
std::vector<std::string> uartWrites;
std::deque<uint8_t> rxBytes;
bool gpsEventQueued = false;
bool rxEventQueued = false;
uint32_t gpsNextEventMillis = 0;
std::mutex gpsMutex;
std::string uartMode = "ack";

constexpr uint8_t SYNC_0 = 0xBA;
constexpr uint8_t SYNC_1 = 0xCE;

constexpr char gpsData[] =
    "$GPRMC,123519.00,A,4807.038,N,01131.000,E,22.678,0.0,230394,,,A*67\r\n"
    "$GPGGA,123519.00,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*69\r\n";

uint32_t gpsRatePeriodMillis(void) {
  constexpr uint32_t periods[] = {1000, 1000, 500, 200, 100};
  const uint8_t rate = Furble::Settings::load<Furble::Settings::GPS_RATE>();
  return rate < (sizeof(periods) / sizeof(periods[0])) ? periods[rate] : periods[0];
}

uint16_t readU16(const uint8_t *data) {
  return static_cast<uint16_t>(data[0]) | (static_cast<uint16_t>(data[1]) << 8);
}

uint32_t casicChecksum(uint8_t classId,
                       uint8_t messageId,
                       uint16_t length,
                       const uint8_t *payload) {
  uint32_t sum =
      (static_cast<uint32_t>(messageId) << 24) | (static_cast<uint32_t>(classId) << 16) | length;
  for (uint16_t offset = 0; offset < length; offset += 4) {
    uint32_t word = 0;
    for (uint16_t byte = 0; byte < 4 && offset + byte < length; byte++) {
      word |= static_cast<uint32_t>(payload[offset + byte]) << (byte * 8);
    }
    sum += word;
  }
  return sum;
}

std::vector<uint8_t> binaryFrame(uint8_t classId,
                                 uint8_t messageId,
                                 const std::vector<uint8_t> &payload) {
  const uint16_t length = static_cast<uint16_t>(payload.size());
  std::vector<uint8_t> frame = {
      SYNC_0,  SYNC_1,   static_cast<uint8_t>(length & 0xff), static_cast<uint8_t>(length >> 8),
      classId, messageId};
  frame.insert(frame.end(), payload.begin(), payload.end());
  const uint32_t sum = casicChecksum(classId, messageId, length, payload.data());
  frame.push_back(static_cast<uint8_t>(sum & 0xff));
  frame.push_back(static_cast<uint8_t>((sum >> 8) & 0xff));
  frame.push_back(static_cast<uint8_t>((sum >> 16) & 0xff));
  frame.push_back(static_cast<uint8_t>(sum >> 24));
  return frame;
}

void queueRxLocked(const std::vector<uint8_t> &bytes) {
  rxBytes.insert(rxBytes.end(), bytes.begin(), bytes.end());
  if (!rxEventQueued && gpsQueue != nullptr) {
    const uart_event_t event = {.type = UART_DATA, .size = rxBytes.size()};
    rxEventQueued = xQueueSend(gpsQueue, &event, 0) == pdTRUE;
  }
}

void queueAckLocked(const uint8_t *request, bool ack) {
  const uint16_t length = readU16(request + 2);
  if (length > 2048 || (length % 4) != 0) {
    return;
  }
  queueRxLocked(binaryFrame(0x05, ack ? 0x01 : 0x00, {request[4], request[5], 0, 0}));
  if (ack && request[4] == 0x06 && request[5] == 0x07 && length == 0) {
    queueRxLocked(binaryFrame(0x06, 0x07, std::vector<uint8_t>(44, 0)));
  }
}

void queueGpsEvent(QueueHandle_t queue) {
  {
    std::lock_guard<std::mutex> lock(gpsMutex);
    if (uartMode == "pause") {
      gpsQueue = queue;
      gpsOffset = sizeof(gpsData) - 1;
      rxBytes.clear();
      rxEventQueued = false;
      gpsEventQueued = false;
      gpsNextEventMillis = UINT32_MAX;
      return;
    }
    gpsQueue = queue;
    gpsOffset = 0;
    rxBytes.clear();
    rxEventQueued = false;
    gpsEventQueued = true;
    gpsNextEventMillis = Furble::Sim::clockMillis();
  }
  const uart_event_t event = {.type = UART_PATTERN_DET, .size = sizeof(gpsData) - 1};
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
  if (gpsQueue == nullptr || buffer == nullptr || length == 0) {
    return 0;
  }
  if (!rxBytes.empty()) {
    const size_t count = uartMode == "partial" ? 1 : std::min<size_t>(length, rxBytes.size());
    for (size_t i = 0; i < count; i++) {
      buffer[i] = rxBytes.front();
      rxBytes.pop_front();
    }
    rxEventQueued = false;
    if (!rxBytes.empty()) {
      const uart_event_t event = {.type = UART_DATA, .size = rxBytes.size()};
      rxEventQueued = xQueueSend(gpsQueue, &event, 0) == pdTRUE;
    }
    return static_cast<int>(count);
  }
  if (gpsOffset >= sizeof(gpsData) - 1) {
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
    const bool due = (uartMode != "pause") && (static_cast<int32_t>(now - gpsNextEventMillis) >= 0);
    if (gpsQueue != nullptr && !gpsEventQueued && rxBytes.empty()
        && gpsOffset >= sizeof(gpsData) - 1 && due) {
      queue = gpsQueue;
    }
  }
  if (queue != nullptr) {
    queueGpsEvent(queue);
  }
}

int uart_write_bytes(uart_port_t, const void *data, size_t length) {
  if (data == nullptr || length == 0) {
    return 0;
  }
  std::lock_guard<std::mutex> lock(gpsMutex);
  const std::string command(static_cast<const char *>(data), length);
  uartWrites.push_back(command);
  if (uartMode == "write-error") {
    return -1;
  }
  const auto *bytes = static_cast<const uint8_t *>(data);
  if (length >= 10 && bytes[0] == SYNC_0 && bytes[1] == SYNC_1) {
    if (uartMode == "timeout") {
      return static_cast<int>(length);
    }
    if (uartMode == "malformed") {
      auto malformed = binaryFrame(0x05, 0x01, {bytes[4], bytes[5], 0, 0});
      malformed.back() ^= 0xff;
      queueRxLocked(malformed);
    } else {
      queueAckLocked(bytes, uartMode != "nack");
    }
  }
  const std::string prefix = "PCAS12,";
  const size_t offset = command.find(prefix);
  if (offset != std::string::npos) {
    const size_t start = offset + prefix.size();
    const size_t end = command.find_first_not_of("0123456789", start);
    const uint32_t seconds = static_cast<uint32_t>(
        std::strtoul(command.substr(start, end - start).c_str(), nullptr, 10));
    if (seconds > 0 && gpsQueue != nullptr && !gpsEventQueued && gpsOffset >= sizeof(gpsData) - 1) {
      gpsNextEventMillis = Furble::Sim::clockMillis() + seconds * 1000;
    }
  }
  return static_cast<int>(length);
}

std::vector<std::string> furble_sim_uart_writes_snapshot(void) {
  std::lock_guard<std::mutex> lock(gpsMutex);
  return uartWrites;
}

std::vector<std::string> furble_sim_uart_take_writes(void) {
  std::lock_guard<std::mutex> lock(gpsMutex);
  std::vector<std::string> writes;
  writes.swap(uartWrites);
  return writes;
}

void furble_sim_uart_clear_writes(void) {
  std::lock_guard<std::mutex> lock(gpsMutex);
  uartWrites.clear();
}

void furble_sim_uart_set_mode(const char *mode) {
  std::lock_guard<std::mutex> lock(gpsMutex);
  uartMode = mode == nullptr ? "ack" : mode;
  if (uartMode == "pause") {
    gpsNextEventMillis = UINT32_MAX;
  } else if (gpsQueue != nullptr && rxBytes.empty() && gpsOffset >= sizeof(gpsData) - 1) {
    gpsNextEventMillis = Furble::Sim::clockMillis();
  }
}

void furble_sim_uart_inject_event(const char *eventName) {
  std::lock_guard<std::mutex> lock(gpsMutex);
  if (gpsQueue == nullptr || eventName == nullptr) {
    return;
  }
  static const std::array<std::pair<const char *, uart_event_type_t>, 7> events = {
      {
       {"data", UART_DATA},
       {"fifo", UART_FIFO_OVF},
       {"buffer", UART_BUFFER_FULL},
       {"break", UART_BREAK},
       {"parity", UART_PARITY_ERR},
       {"frame", UART_FRAME_ERR},
       {"pattern", UART_PATTERN_DET},
       }
  };
  for (const auto &entry : events) {
    if (entry.first == std::string(eventName)) {
      const uart_event_t event = {.type = entry.second, .size = 0};
      xQueueSend(gpsQueue, &event, 0);
      return;
    }
  }
}

esp_err_t uart_wait_tx_done(uart_port_t, TickType_t) {
  return ESP_OK;
}
