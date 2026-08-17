#ifndef FURBLE_GPS_H
#define FURBLE_GPS_H

#include <array>
#include <atomic>
#include <deque>
#include <limits>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <driver/uart.h>

#if defined(FURBLE_NO_DISPLAY)
struct _lv_obj_t;
using lv_obj_t = _lv_obj_t;
#else
#include <lvgl.h>
#endif

#include <Camera.h>
#include <TinyGPS++.h>

#include <atomic>
#include <cstdint>
#include <mutex>

#include "FurbleGPSPowerCycle.h"
#include "FurbleMotion.h"
#include "FurblePower.h"

namespace Furble {
class GPS {
 public:
  enum source_t {
    SOURCE_NONE,
    SOURCE_UART,
    SOURCE_COMPANION,
  };

  enum config_state_t {
    CONFIG_QUEUED,
    CONFIG_SENT,
    CONFIG_ACKED,
    CONFIG_NACKED,
    CONFIG_TIMEOUT,
    CONFIG_FALLBACK,
  };

  typedef struct {
    uint8_t class_id;
    uint8_t message_id;
    config_state_t state;
    uint8_t attempts;
  } config_status_t;

  typedef struct {
    Camera::gps_t gps;
    Camera::timesync_t timesync;
    uint32_t age_ms;
    bool position_valid;
    bool time_valid;
    bool altitude_valid;
  } external_fix_t;

  /**
   * Coherent, thread-safe copy of the TinyGPSPlus fields used by the UI and
   * console. TinyGPSPlus accessors clear update flags, so callers must not
   * retain a reference to the parser or read fields outside this snapshot.
   */
  typedef struct {
    bool position_valid;
    bool date_valid;
    bool time_valid;
    bool altitude_valid;
    bool fix;
    uint32_t satellites;
    double latitude;
    double longitude;
    double altitude;
    double speed_kmph;
    double hdop;
    uint32_t location_age;
    uint32_t date_age;
    uint32_t time_age;
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint8_t centisecond;
    uint32_t chars_processed;
    uint32_t sentences_passed;
    uint32_t sentences_failed;
  } status_t;

  static GPS &getInstance();

  GPS(GPS const &) = delete;
  GPS(GPS &&) = delete;
  GPS &operator=(GPS const &) = delete;
  GPS &operator=(GPS &&) = delete;

  static void init(void);

  void setIcon(lv_obj_t *icon);
  bool isEnabled(void) const;
  /** True when the software motion detector is running. */
  bool isMotionEnabled(void) const;
  /** True when the enabled software motion detector reports stationary. */
  bool isStationary(void) const;
  void reloadSetting(void);

  /**
   * Refresh only the motion detector gate.
   *
   * Every GPS_MOTION write uses this instead of reloadSetting(). The detector
   * is advisory and owns no receiver state, so flipping it must not cost a
   * re-acquisition.
   */
  void reloadMotionSetting(void);

  /** Refresh the cached GPX logging settings from NVS. */
  void reloadLogSettings(void);
  void startService(void);

  /**
   * Run one service pass: refresh the fix and push geotag data to the camera.
   *
   * The display build calls this from an LVGL timer. The headless main loop
   * calls it directly on the same cadence, since it has no LVGL.
   */
  void update(void);

  bool setExternalFix(const external_fix_t &fix);
  void clearExternalFix(void);

  status_t getStatusSnapshot(void) const;
  source_t getSource(void) const;
  uint8_t getSatellites(void) const;

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
    DEGRADED,
  };

  struct cycle_status_t {
    bool degraded;
    uint32_t retries;
  };

  /**
   * Receiver state the fix snapshot does not carry.
   *
   * status_t reports the navigation solution. This reports the receiver around
   * it: which power cycle state it is in, whether that state is the degraded
   * retry and how many retries deep, how it is being duty cycled, the fix
   * interval it was configured for, how long ago it last spoke, and whether
   * assisted start data is loaded. The GPS Data page and the console `gps
   * status` command read this, so a receiver that is alive but not fixing can
   * be told apart from one that has gone quiet.
   *
   * The degraded fields duplicate cycle_status_t on purpose. A caller that
   * needs the cycle state and the retry count together must not take
   * m_CycleMutex twice, or it can render a torn composite: a fresh state beside
   * a stale retry count.
   */
  struct receiver_status_t {
    /** Power cycle state name, from cycleStateName(). Never null. */
    const char *cycle_state;
    /** Applied power policy, one of POWER_ALWAYS_ON, STANDBY, RAIL_CYCLE. */
    uint8_t power_policy;
    /** Applied duty cycle interval in seconds, 0 when duty cycling is off. */
    uint8_t duty_seconds;
    /** Configured fix interval in milliseconds, 0 for the receiver default. */
    uint16_t rate_ms;
    /** Milliseconds since the last received sentence, 0 when never seen. */
    uint32_t last_sentence_age_ms;
    /** Has any sentence been received since the receiver was enabled? */
    bool have_sentence;
    /** Assisted start mode setting, 0 when assisted start is off. */
    uint8_t aid_mode;
    /** Is a usable position and time cache loaded for assisted start? */
    bool aid_cache_valid;
    /** Is the cycle in the degraded retry state? */
    bool degraded;
    /** Consecutive degraded retry attempts since the last healthy recovery. */
    uint32_t retries;
  };

  cycle_status_t getCycleStatusSnapshot(void) const {
    const std::lock_guard<std::mutex> lock(m_CycleMutex);
    return {m_CycleState == cycle_state_t::DEGRADED, m_Degraded.failures()};
  }

  /**
   * Is the power cycle in the degraded retry state?
   *
   * The burst windowed power management enters this state when it cannot
   * predict the receiver burst timing. It no longer pins the lock, it releases
   * it and retries on a bounded backoff, so this only reports that reception is
   * poor, not that furble is stuck.
   */
  bool isDegraded(void) const { return getCycleStatusSnapshot().degraded; }

  /** Consecutive degraded retry attempts since the last healthy recovery. */
  uint32_t degradedRetries(void) const { return getCycleStatusSnapshot().retries; }

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

  /** Send a raw CASIC binary frame. */
  bool sendBinary(uint8_t class_id, uint8_t message_id, const std::vector<uint8_t> &payload);

  /** Inject the cached position and time as AID-INI. */
  bool sendAidIni(void);

  /** Get the most recent binary configuration status list. */
  std::vector<config_status_t> getConfigStatus(void) const;

  /** Get the printable name of a binary configuration state. */
  static const char *configStateName(config_state_t state);

  /** Get the printable name of a power cycle state. */
  static const char *cycleStateName(cycle_state_t state);

  /** Get the printable name of a fix source. */
  static const char *sourceName(source_t source);

  /**
   * Get the four character name of a fix source.
   *
   * The GPS Data page rows are budgeted to fourteen characters, so "companion"
   * does not fit. This is the only abbreviation of a source name in furble;
   * everything else prints sourceName().
   */
  static const char *sourceShortName(source_t source);

  /** Get a coherent copy of the receiver power, rate and assist state. */
  receiver_status_t getReceiverStatus(void) const;

 private:
  GPS() {};

  static constexpr const size_t BUFFER_SIZE = 256;
  static constexpr const int QUEUE_SIZE = 32;
  static constexpr const uint16_t SERVICE_MS = 1000;
  static constexpr const uint32_t MAX_AGE_MS = 30 * 1000;
  static constexpr const char *POWER_LOCK_OWNER = "gps";

  static constexpr uint8_t CFG_CLASS = 0x06;
  static constexpr uint8_t CFG_MSG_ID = 0x01;
  static constexpr uint8_t CFG_NAVX_ID = 0x07;
  static constexpr uint8_t ACK_CLASS = 0x05;
  static constexpr uint8_t ACK_NACK_ID = 0x00;
  static constexpr uint8_t ACK_ACK_ID = 0x01;
  static constexpr uint8_t AID_CLASS = 0x0B;
  static constexpr uint8_t AID_INI_ID = 0x01;
  static constexpr uint16_t MAX_BINARY_PAYLOAD = 2048;
  static constexpr uint32_t BINARY_ACK_TIMEOUT_MS = 300;
  static constexpr uint8_t BINARY_ATTEMPTS = 3;
  static constexpr size_t MAX_CONFIG_COMMANDS = 16;
  static constexpr size_t NAVX_PAYLOAD_SIZE = 44;
  static constexpr uint32_t AID_CACHE_MAGIC = 0x46524149;
  static constexpr uint8_t AID_CACHE_VERSION = 1;
  static constexpr uint32_t AID_CACHE_WRITE_MS = 10 * 60 * 1000;
  static constexpr uint32_t AID_CACHE_MAX_AGE_MS = 24 * 60 * 60 * 1000;
  static constexpr int64_t GPS_EPOCH_UNIX = 315964800;
  static constexpr int GPS_LEAP_SECONDS = 18;

  /** How long to wait for the receiver before sending the configuration. */
  static constexpr const uint32_t SETTLE_MS = 3000;

  /** How long to wait for a command to leave the UART. */
  static constexpr const uint32_t TX_MS = 100;

  /** Number of raw NMEA sentences kept for the debug page. */
  static constexpr const size_t SENTENCES = 8;

  /** Longest raw NMEA sentence kept for the debug page. */
  static constexpr const size_t SENTENCE_LEN = 96;

  typedef struct {
    uint32_t magic;
    uint8_t version;
    uint8_t reserved[3];
    double latitude;
    double longitude;
    double altitude;
    int64_t utc_seconds;
    uint32_t capture_tick_ms;
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint8_t centisecond;
    uint8_t flags;
    uint8_t reserved_tail[3];
  } aid_cache_t;

  typedef struct {
    uint8_t class_id;
    uint8_t message_id;
    std::vector<uint8_t> payload;
    std::string fallback;
    bool navx_query;
    size_t status_index;
  } binary_command_t;

  static_assert(sizeof(aid_cache_t) == 56, "AID cache layout changed");

  void enable(void);
  void disable(void);
  void serviceSerial(void);
  void serviceConfig(void);
  void serviceCycle(void);
  void processSerial(const uint8_t *data, size_t length);
  void processNmea(uint8_t *data, size_t length);
  void serviceBinary(const uint8_t *frame, size_t length);
  void syncMotionTimer(void);
  void updateMotion(void);
  void resetMotion(void);
  bool wiredFixIsFresh(const status_t &status) const;

  void acquirePowerLock(void);
  void releasePowerLock(void);
  void beginBurst(uint32_t now);
  void finishBurst(uint32_t now);
  void beginWindow(uint32_t now);
  void enterStandby(uint32_t now);
  void enterRailOff(uint32_t now);
  void beginResync(uint32_t now);
  void finishMeasurement(void);
  void enterDegraded(void);
  TickType_t cycleWait(uint32_t now) const;

  uint8_t powerPolicy(void) const;
  uint8_t dutySeconds(void) const;
  uint32_t gpsRateInterval(void) const;
  bool dutyCycleEnabled(void) const;

  /** XOR checksum of every character between '$' and '*'. */
  static uint8_t checksum(const std::string &payload);

  /** CASIC binary checksum. The checksum is the sum of little endian words. */
  static uint32_t casicChecksum(uint8_t class_id,
                                uint8_t message_id,
                                uint16_t length,
                                const uint8_t *payload);

  /** Frame the payload as NMEA and write it to the receiver. */
  void sendCommand(const std::string &payload);

  /** Frame and write one CASIC binary message. */
  bool sendBinaryFrame(uint8_t class_id, uint8_t message_id, const std::vector<uint8_t> &payload);

  /** Send the $PCAS configuration commands for the current settings. */
  void configure(void);

  void enqueueConfig(uint8_t class_id,
                     uint8_t message_id,
                     const std::vector<uint8_t> &payload,
                     const std::string &fallback,
                     bool navx_query = false,
                     bool front = false);
  void startConfigCommand(void);
  void finishConfigCommand(config_state_t state);
  void handleNavxResponse(const uint8_t *payload, size_t length);
  void completeNavxQuery(void);

  void loadAidCache(void);
  void updateAidCache(const Camera::gps_t &gps, const Camera::timesync_t &timesync);
  static int64_t toUnixSeconds(const Camera::timesync_t &timesync);

  /** Store raw NMEA sentences while the debug page is open. */
  void captureSentences(const char *data, size_t length);

  uart_port_t m_UART = UART_NUM_2;

#if !defined(FURBLE_NO_DISPLAY)
  lv_obj_t *m_Icon = NULL;
  const lv_image_dsc_t *m_IconSymbol = NULL;
  // last applied degraded tint state, so the periodic poll only re-tints the
  // status icon on a change and never re-invalidates it every update
  bool m_IconDegraded = false;
  lv_timer_t *m_Timer = NULL;
  lv_timer_t *m_MotionTimer = NULL;
#endif

  TaskHandle_t m_Task = NULL;
  QueueHandle_t m_Queue = NULL;

  // Parks the GPS task while enable() or disable() resets task-owned UART,
  // parser, configuration, and cycle state. The task rechecks m_Enabled only
  // after acquiring this mutex so a stale pre-lock observation cannot enter a
  // service pass during a settings reload.
  std::mutex m_ServiceMutex;

  std::atomic<bool> m_Enabled = false;
  bool m_HasFix = false;
  std::atomic<bool> m_MotionEnabled {false};
  std::atomic<bool> m_MotionStationary {false};
  std::atomic<bool> m_MotionResetPending {false};
  // Owned by the LVGL task through the motion timer. Only the atomics above are
  // read from other tasks.
  Motion::Detector m_Motion;
  std::atomic<uint8_t> m_Source {SOURCE_NONE};
  std::atomic<uint8_t> m_Satellites {0};
  external_fix_t m_ExternalFix = {};
  uint64_t m_ExternalFixReceivedMs = 0;
  bool m_HasExternalFix = false;
  mutable std::mutex m_ExternalMutex;

  // cached GPX logging settings, no NVS reads on the periodic update path
  std::atomic<bool> m_LogEnabled = false;
  std::atomic<uint32_t> m_LogPeriodMs = 5000;
  uint32_t m_LastLoggedFix = 0;
  uint64_t m_LastLoggedStamp = 0;
  bool m_LogDropWarned = false;
  mutable TinyGPSPlus m_GPS;
  mutable std::mutex m_GPSMutex;

  uint8_t m_PowerPolicy = POWER_ALWAYS_ON;
  uint8_t m_DutySeconds = 0;
  // applied fix interval, cached from the rate setting at enable() so status
  // readers never perform an NVS read on a periodic path
  uint16_t m_RateMs = 0;
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
  // bounds a degraded retry probe, a stale prediction must not latch ACQUIRING
  uint32_t m_ProbeDeadline = 0;
  GpsDegradedRetry m_Degraded;
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
  mutable std::mutex m_CycleMutex;

#if defined(FURBLE_M5STICKS3)
  // held only during a receive burst or the burst acquisition window
  std::mutex m_PowerLockMutex;
  std::optional<Power::Lock> m_PowerLock;
#endif

  std::atomic<bool> m_ConfigPending = false;
  uint32_t m_ConfigStart = 0;
  uint32_t m_ConfigChars = 0;
  std::deque<binary_command_t> m_ConfigQueue;
  std::optional<binary_command_t> m_ConfigInFlight;
  std::array<config_status_t, MAX_CONFIG_COMMANDS> m_ConfigStatus = {};
  size_t m_ConfigStatusCount = 0;
  size_t m_ConfigInFlightStatus = 0;
  uint8_t m_ConfigAttempts = 0;
  uint32_t m_ConfigSent = 0;
  bool m_ConfigFallbackUsed = false;
  bool m_ConfigNavxAcked = false;
  std::array<uint8_t, NAVX_PAYLOAD_SIZE> m_NavxPayload = {};
  bool m_NavxPayloadValid = false;
  mutable std::mutex m_ConfigMutex;
  std::mutex m_TxMutex;

  std::vector<uint8_t> m_RxBuffer;

  std::atomic<uint8_t> m_AidMode = 0;
  aid_cache_t m_AidCache = {};
  bool m_AidCacheLoaded = false;
  bool m_AidCacheValid = false;
  bool m_AidCacheTickValid = false;
  bool m_AidCacheWriteValid = false;
  uint32_t m_AidCacheTick = 0;
  uint32_t m_AidCacheLastWrite = 0;
  mutable std::mutex m_AidMutex;

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
