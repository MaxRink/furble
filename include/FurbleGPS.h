#ifndef FURBLE_GPS_H
#define FURBLE_GPS_H

#include <array>
#include <atomic>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <driver/uart.h>

#include <lvgl.h>

#include <TinyGPS++.h>

#include "FurblePower.h"

namespace Furble {
class GPS {
 public:
  static GPS &getInstance();

  GPS(GPS const &) = delete;
  GPS(GPS &&) = delete;
  GPS &operator=(GPS const &) = delete;
  GPS &operator=(GPS &&) = delete;

  static void init(void);

  void setIcon(lv_obj_t *icon);
  bool isEnabled(void) const;
  void reloadSetting(void);
  void startService(void);

  TinyGPSPlus &get(void);

  void reset(void);
  void task(void);

  /** Fix interval in milliseconds for each rate setting. */
  static constexpr const std::array<uint16_t, 5> RATE_MS = {0, 1000, 500, 200, 100};

  /** Highest valid constellation setting. */
  static constexpr const uint8_t CONSTELLATION_MAX = 7;

  /** Restart the receiver, 0 hot, 1 warm, 2 cold. */
  void restart(uint8_t mode);

  /** Capture raw NMEA sentences for the debug page. */
  void setCapture(bool capture);

  /** Get the captured NMEA sentences, oldest first. */
  std::vector<std::string> getSentences(void);

 private:
  GPS() {};

  static constexpr const size_t BUFFER_SIZE = 256;
  static constexpr const int QUEUE_SIZE = 32;
  static constexpr const uint16_t SERVICE_MS = 1000;
  static constexpr const uint32_t MAX_AGE_MS = 30 * 1000;
  static constexpr const char *POWER_LOCK_OWNER = "gps";

  /** How long to wait for the receiver before sending the configuration. */
  static constexpr const uint32_t SETTLE_MS = 3000;

  /** How long to wait for a command to leave the UART. */
  static constexpr const uint32_t TX_MS = 100;

  /** Number of raw NMEA sentences kept for the debug page. */
  static constexpr const size_t SENTENCES = 8;

  /** Longest raw NMEA sentence kept for the debug page. */
  static constexpr const size_t SENTENCE_LEN = 96;

  void enable(void);
  void disable(void);
  void serviceSerial(void);
  void serviceConfig(void);
  void update(void);

  /** XOR checksum of every character between '$' and '*'. */
  static uint8_t checksum(const std::string &payload);

  /** Frame the payload as NMEA and write it to the receiver. */
  void sendCommand(const std::string &payload);

  /** Send the $PCAS configuration commands for the current settings. */
  void configure(void);

  /** Store raw NMEA sentences while the debug page is open. */
  void captureSentences(const char *data, size_t length);

  uart_port_t m_UART = UART_NUM_2;

  lv_obj_t *m_Icon = NULL;
  const lv_image_dsc_t *m_IconSymbol = NULL;
  lv_timer_t *m_Timer = NULL;

  TaskHandle_t m_Task = NULL;
  QueueHandle_t m_Queue = NULL;

  bool m_Enabled = false;
  bool m_HasFix = false;
  TinyGPSPlus m_GPS;

#if defined(FURBLE_M5STICKS3)
  // held while the receiver is powered, the ESP32S3 UART needs it
  std::optional<Power::Lock> m_PowerLock;
#endif

  std::atomic<bool> m_ConfigPending = false;
  uint32_t m_ConfigStart = 0;
  uint32_t m_ConfigChars = 0;

  std::atomic<bool> m_Capture = false;
  std::mutex m_CaptureMutex;
  std::array<std::string, SENTENCES> m_Sentences;
  size_t m_SentenceNext = 0;
  std::string m_Partial;
};
}  // namespace Furble

extern "C" {
void gps_task(void *param);
}

#endif
