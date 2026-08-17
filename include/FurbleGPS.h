#ifndef FURBLE_GPS_H
#define FURBLE_GPS_H

#include <array>
#include <atomic>
#include <limits>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <driver/uart.h>

#include <lvgl.h>

#include <Camera.h>
#include <TinyGPS++.h>

#include <atomic>
#include <cstdint>
#include <mutex>

#include "FurblePower.h"

namespace Furble {
class GPS {
 public:
  enum source_t {
    SOURCE_NONE,
    SOURCE_UART,
    SOURCE_COMPANION,
  };

  typedef struct {
    Camera::gps_t gps;
    Camera::timesync_t timesync;
    uint32_t age_ms;
    bool position_valid;
    bool time_valid;
    bool altitude_valid;
  } external_fix_t;

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
  bool setExternalFix(const external_fix_t &fix);
  void clearExternalFix(void);

  TinyGPSPlus &get(void);
  source_t getSource(void) const;
  uint8_t getSatellites(void) const;

  void reset(void);
  void task(void);

  /** Fix interval in milliseconds for each rate setting. */
  static constexpr const std::array<uint16_t, 5> RATE_MS = {0, 1000, 500, 200, 100};

  /** Highest valid constellation setting. */
  static constexpr const uint8_t CONSTELLATION_MAX = 7;

  /** Receiver power policies. */
  static constexpr const uint8_t POWER_ALWAYS_ON = 0;
  static constexpr const uint8_t POWER_STANDBY = 1;
  static constexpr const uint8_t POWER_RAIL_CYCLE = 2;

  /** Supported standby intervals, in seconds. */
  static constexpr const std::array<uint8_t, 4> DUTY_SECONDS = {0, 5, 10, 15};

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

  /** GPS power and lock state. */
  enum class cycle_state_t : uint8_t {
    DISABLED,
    ACQUIRING,
    MEASURING,
    BURST,
    WAITING,
    STANDBY,
    RAIL_OFF,
    RESYNC,
    PERMANENT_LOCK,
  };

  void enable(void);
  void disable(void);
  void serviceSerial(void);
  void serviceConfig(void);
  void serviceCycle(void);
  void update(void);
  bool wiredFixIsFresh(void);

  void acquirePowerLock(void);
  void releasePowerLock(void);
  void beginBurst(uint32_t now);
  void finishBurst(uint32_t now);
  void beginWindow(uint32_t now);
  void enterStandby(uint32_t now);
  void enterRailOff(uint32_t now);
  void beginResync(uint32_t now);
  void finishMeasurement(void);
  void enterPermanentLock(void);
  TickType_t cycleWait(uint32_t now) const;

  uint8_t powerPolicy(void) const;
  uint8_t dutySeconds(void) const;
  uint32_t gpsRateInterval(void) const;
  bool dutyCycleEnabled(void) const;

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

  std::atomic<bool> m_Enabled = false;
  bool m_HasFix = false;
  std::atomic<uint8_t> m_Source {SOURCE_NONE};
  std::atomic<uint8_t> m_Satellites {0};
  external_fix_t m_ExternalFix = {};
  uint64_t m_ExternalFixReceivedMs = 0;
  bool m_HasExternalFix = false;
  mutable std::mutex m_ExternalMutex;
  TinyGPSPlus m_GPS;

  uint8_t m_PowerPolicy = POWER_ALWAYS_ON;
  uint8_t m_DutySeconds = 0;
  cycle_state_t m_CycleState = cycle_state_t::DISABLED;
  bool m_BurstActive = false;
  bool m_DutyWake = false;
  bool m_HavePrediction = false;
  uint32_t m_BurstStart = 0;
  uint32_t m_LastSentence = 0;
  uint32_t m_NextBurst = 0;
  uint32_t m_WakeDeadline = 0;
  uint32_t m_ExpectedInterval = 0;
  uint32_t m_Window = 50;
  uint32_t m_BurstFailed = 0;
  uint32_t m_MeasureDeadline = 0;
  uint32_t m_ResyncDeadline = 0;
  uint32_t m_LastBurstStart = 0;
  std::array<uint32_t, 5> m_PeriodSamples = {};
  size_t m_PeriodCount = 0;
  uint8_t m_ConsecutiveBadBursts = 0;
  uint8_t m_CleanBursts = 0;
  uint32_t m_LastLocationAge = std::numeric_limits<uint32_t>::max();
  std::atomic<uint32_t> m_BurstSequence = 0;
  std::atomic<uint32_t> m_FixSequence = 0;
  std::atomic<uint32_t> m_PushedSequence = 0;
  std::atomic<bool> m_CycleRequest = false;

  // serialises the cycle state between the GPS task and enable() or disable(),
  // never held across PMIC, UART or other blocking hardware setup
  std::mutex m_CycleMutex;

#if defined(FURBLE_M5STICKS3)
  // held only during a receive burst or the burst acquisition window
  std::mutex m_PowerLockMutex;
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
