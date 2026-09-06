#include <algorithm>
#include <limits>

#include <M5Unified.h>
#include <TinyGPS++.h>
#include <esp_timer.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <ctime>
#include <utility>

#if !defined(FURBLE_NO_DISPLAY)
#include <lvgl.h>

#include "icons.h"
#endif

#include "FurbleConsole.h"
#include "FurbleControl.h"
#include "FurbleGPS.h"
#include "FurbleGPSHold.h"
#include "FurbleGPX.h"
#include "FurblePlatform.h"
#include "FurblePower.h"
#include "FurbleSD.h"
#include "FurbleSettings.h"
#include "FurbleTimeKeeper.h"
#include "FurbleTypes.h"
#include "Preferences.h"

namespace {

constexpr size_t BINARY_HEADER_SIZE = 6;
constexpr size_t BINARY_CHECKSUM_SIZE = 4;
constexpr size_t BINARY_FRAME_OVERHEAD = BINARY_HEADER_SIZE + BINARY_CHECKSUM_SIZE;
constexpr uint8_t BINARY_SYNC_0 = 0xBA;
constexpr uint8_t BINARY_SYNC_1 = 0xCE;
constexpr uint32_t NAVX_MASK_NAV_SYSTEM = 1 << 8;
constexpr uint8_t NAVX_NAV_SYSTEM_OFFSET = 6;

uint16_t readU16(const uint8_t *data) {
  return static_cast<uint16_t>(data[0]) | (static_cast<uint16_t>(data[1]) << 8);
}

uint32_t readU32(const uint8_t *data) {
  return static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8)
         | (static_cast<uint32_t>(data[2]) << 16) | (static_cast<uint32_t>(data[3]) << 24);
}

void writeU16(uint8_t *data, uint16_t value) {
  data[0] = static_cast<uint8_t>(value & 0xff);
  data[1] = static_cast<uint8_t>((value >> 8) & 0xff);
}

void writeU32(uint8_t *data, uint32_t value) {
  data[0] = static_cast<uint8_t>(value & 0xff);
  data[1] = static_cast<uint8_t>((value >> 8) & 0xff);
  data[2] = static_cast<uint8_t>((value >> 16) & 0xff);
  data[3] = static_cast<uint8_t>((value >> 24) & 0xff);
}

template <typename T>
void writeValue(std::vector<uint8_t> &data, size_t offset, const T &value) {
  std::memcpy(data.data() + offset, &value, sizeof(value));
}

bool validCoordinate(double value, double min, double max) {
  return std::isfinite(value) && (value >= min) && (value <= max);
}

}  // namespace

#if defined(FURBLE_SIM)
#include "power_profiler.h"
#define FURBLE_SIM_GPS_STATE(state) Furble::Sim::profilerSetGpsState(state)
#define FURBLE_SIM_TIMER_FIRE(name) Furble::Sim::profilerTimerFire(name)
#else
#define FURBLE_SIM_GPS_STATE(state) ((void)0)
#define FURBLE_SIM_TIMER_FIRE(name) ((void)0)
#endif

void gps_task(void *param) {
  Furble::GPS *gps = static_cast<Furble::GPS *>(param);
  gps->task();
}

namespace Furble {

namespace {
constexpr uint32_t BURST_GAP_MS = 75;
constexpr uint32_t WINDOW_DEFAULT_MS = 50;
constexpr uint32_t WINDOW_MIN_MS = 20;
constexpr uint32_t WINDOW_WIDEN_MS = 25;
constexpr uint32_t WINDOW_SHRINK_MS = 10;
constexpr uint8_t CLEAN_BURSTS_TO_SHRINK = 10;
constexpr uint8_t BAD_BURSTS_TO_RESYNC = 3;
constexpr uint32_t UNKNOWN_MEASURE_MS = 5000;
constexpr uint32_t MIN_WAKE_WAIT_MS = 5000;
constexpr uint32_t MIN_CYCLE_WAIT_MS = 10;

// how long the GPS task sleeps once the autobaud ladder has given up for good
constexpr uint32_t ABSENT_IDLE_WAIT_MS = 60 * 1000;
// how long a degraded retry keeps the lock and looks for a burst before it
// gives up again, a couple of 1 Hz frames plus slack
constexpr uint32_t DEGRADED_PROBE_MS = 6000;

/**
 * Switch the Port A 5 V rail through the PMIC.
 *
 * The M5PM1 sleeps after an I2C idle period and the first access after that
 * only wakes it, so read the rail state back and retry once on mismatch.
 */
void setRailPower(bool enable) {
  M5.Power.setExtOutput(enable, m5::ext_PA);
  if (M5.Power.getExtOutput() != enable) {
    M5.Power.setExtOutput(enable, m5::ext_PA);
  }
}

bool tickReached(uint32_t now, uint32_t target) {
  return static_cast<int32_t>(now - target) >= 0;
}

uint32_t tickUntil(uint32_t now, uint32_t target) {
  return tickReached(now, target) ? 0 : target - now;
}

uint32_t tickDistance(uint32_t first, uint32_t second) {
  const int32_t delta = static_cast<int32_t>(first - second);
  return delta < 0 ? static_cast<uint32_t>(-delta) : static_cast<uint32_t>(delta);
}

void wakeGPSService(QueueHandle_t queue) {
  if (queue == nullptr) {
    return;
  }
  // Settings transitions set m_Enabled false before sending this private
  // event. The task discards it under the service mutex, so it exists only to
  // cancel an idle UART receive without waiting for the next tick deadline.
  const uart_event_t wake = {.type = UART_DATA, .size = 0};
  xQueueSendToFront(queue, &wake, 0);
}

}  // namespace

GPS &GPS::getInstance() {
  static GPS instance;

  if (instance.m_Task == NULL) {
    //  RX=33, TX=32 for Module GPS v2.1, currently unsupported
    const int8_t tx = M5.getPin(m5::port_a_pin2);
    const int8_t rx = M5.getPin(m5::port_a_pin1);

    const int baud = Settings::load<Settings::GPS_BAUD>();
    const uart_config_t uart_config = {
        .baud_rate = (int)baud,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 0,
#if defined(FURBLE_M5STICKS3) || defined(CONFIG_IDF_TARGET_ESP32S3)
        // XTAL keeps the baud stable while DFS scales the APB clock. It is also
        // the only valid choice on the ESP32-S3, which has no REF_TICK source,
        // so the headless S3 profile lands here too.
        .source_clk = UART_SCLK_XTAL,
#else
        .source_clk = UART_SCLK_REF_TICK,
#endif
        .flags = {},
    };
    uart_driver_install(instance.m_UART, BUFFER_SIZE, 0, QUEUE_SIZE, &instance.m_Queue,
                        ESP_INTR_FLAG_IRAM);
    uart_param_config(instance.m_UART, &uart_config);
    uart_set_pin(instance.m_UART, tx, rx, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_enable_pattern_det_baud_intr(instance.m_UART, '\n', 1, 9, 0, 0);
    uart_pattern_queue_reset(instance.m_UART, QUEUE_SIZE);
    uart_flush(instance.m_UART);

    BaseType_t err = xTaskCreate(gps_task, LOG_TAG, 4096, &instance, 3, &instance.m_Task);
    if (err != pdTRUE) {
      ESP_LOGE(LOG_TAG, "Failed to create gps task.");
      abort();
    }
  }

  return instance;
}

void GPS::init(void) {
  getInstance().reloadSetting();
}

void GPS::setIcon(lv_obj_t *icon) {
#if !defined(FURBLE_NO_DISPLAY)
  m_Icon = icon;
#else
  (void)icon;
#endif
}

void GPS::reset(void) {
  uart_flush(m_UART);
  xQueueReset(m_Queue);
  m_RxBuffer.clear();
}

void GPS::task(void) {
  while (true) {
    std::unique_lock<std::mutex> serviceLock(m_ServiceMutex);
    if (!m_Enabled) {
      serviceLock.unlock();
      releasePowerLock();
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }

    // Autobaud detection and the no-receiver retry run ahead of the power
    // cycle. While the ladder probes, feed the serial parser so the sentence
    // counter can climb, but hold the cycle and configuration paths until a
    // receiver is found. When nothing answers, idle without spinning.
    serviceProbe();
    const auto rstate = static_cast<receiver_state_t>(m_ReceiverState.load());
    const bool absent = (rstate == receiver_state_t::ABSENT);
    const bool detecting = (rstate == receiver_state_t::DETECTING);

    if (!absent && !detecting) {
      serviceCycle();
      serviceConfig();
      servicePoll();
    }

    uart_event_t event;
    if (xQueueReceive(m_Queue, &event, cycleWait(Platform::getInstance().tick()))) {
      if (!m_Enabled) {
        continue;
      }
      switch (event.type) {
        case UART_DATA:
          if (!absent) {
            serviceSerial();
          }
          break;
        case UART_FIFO_OVF:
          ESP_LOGW(LOG_TAG, "GPS HW FIFO overflow");
          reset();
          break;
        case UART_BUFFER_FULL:
          ESP_LOGW(LOG_TAG, "GPS ring buffer full");
          reset();
          break;
        case UART_BREAK:
          ESP_LOGW(LOG_TAG, "GPS rx break");
          break;
        case UART_PARITY_ERR:
          ESP_LOGE(LOG_TAG, "GPS parity error");
          break;
        case UART_FRAME_ERR:
          ESP_LOGE(LOG_TAG, "GPS frame error");
          break;
        case UART_PATTERN_DET:
          if (!absent) {
            serviceSerial();
          }
          break;
        default:
          ESP_LOGW(LOG_TAG, "unknown uart event type: %d", event.type);
          break;
      }
    }

    if (!absent && !detecting) {
      serviceConfig();
      servicePoll();
      serviceCycle();
    }
  }
}

void GPS::resetAcquisition(uint32_t now) {
  m_CycleState = cycle_state_t::ACQUIRING;
  m_BurstActive = false;
  m_DutyWake = false;
  m_HavePrediction = false;
  m_BurstStart = 0;
  m_LastSentence = 0;
  m_NextBurst = 0;
  m_WakeDeadline = 0;
  m_ExpectedInterval = m_RateMs;
  m_Window = WINDOW_DEFAULT_MS;
  m_BurstFailed = 0;
  m_MeasureDeadline = 0;
  m_ResyncDeadline = 0;
  m_ProbeDeadline = 0;
  m_Degraded.reset();
  m_LastBurstStart = 0;
  m_PeriodSamples.fill(0);
  m_PeriodCount = 0;
  m_ConsecutiveBadBursts = 0;
  m_CleanBursts = 0;
  m_LastLocationAge = std::numeric_limits<uint32_t>::max();
  m_BurstSequence = 0;
  m_FixSequence = 0;
  m_PushedSequence = 0;
  m_CycleRequest = false;
  (void)now;
}

void GPS::enable(void) {
  const uint32_t baud = Settings::load<Settings::GPS_BAUD>();
  const uint8_t policy = powerPolicy();
  const uint8_t duty = dutySeconds();
  const uint32_t rate = gpsRateInterval();
  const bool autobaud = (baud == GPS_BAUD_AUTO);

  // park the GPS task first, m_Enabled gates every cycle entry point
  m_Enabled = false;
  wakeGPSService(m_Queue);
  const std::lock_guard<std::mutex> serviceLock(m_ServiceMutex);

  m_AidMode.store(Settings::load<Settings::GPS_ASSIST>());
  loadAidCache();
  loadEphemerisCache();
  m_ConfigQueue.clear();
  m_ConfigInFlight.reset();
  m_ConfigFallbackUsed = false;
  m_ConfigNavxAcked = false;
  m_NavxPayloadValid = false;
  m_EphPolled = false;
  m_EphPollActive = false;
  m_EphCapture = false;
  m_EphReplayArmed = false;
  m_MonHwPending = false;
  {
    const std::lock_guard<std::mutex> lock(m_ConfigMutex);
    m_ConfigStatusCount = 0;
  }

  // The M5Stack Unit GPS v1.1 ships at 115200, so a fresh Auto boot probes the
  // fast rate first. A stored fixed baud skips the ladder entirely.
  const uint32_t initialBaud = autobaud ? Casic::Autobaud::LADDER[0] : baud;
  uart_set_baudrate(m_UART, initialBaud);
  reset();

  // power on, no mutex may be held across the PMIC access
  setRailPower(true);

  {
    // serialise against a cycle pass still running on the GPS task
    std::lock_guard<std::mutex> guard(m_CycleMutex);

    m_PowerPolicy = policy;
    m_DutySeconds = duty;
    // cached so the status readers never reach NVS on a periodic path
    m_RateMs = static_cast<uint16_t>(rate);
    resetAcquisition(Platform::getInstance().tick());

    m_NoReceiverRetried = false;
    m_NoReceiverRetryAt = 0;

    if (autobaud) {
      // the ladder runs on the GPS task, arm it and hold configuration until a
      // receiver is found
      m_ReceiverState.store(static_cast<uint8_t>(receiver_state_t::DETECTING));
      m_DetectedBaud.store(0);
      m_ProbePending.store(true);
      m_ConfigPending = false;
    } else {
      m_ReceiverState.store(static_cast<uint8_t>(receiver_state_t::PRESENT));
      m_DetectedBaud.store(baud);
      m_ProbePending.store(false);

      // the receiver is not ready for commands yet, ask the GPS task to
      // configure it once sentences arrive
      m_ConfigChars = getStatusSnapshot().chars_processed;
      m_ConfigStart = Platform::getInstance().tick();
      m_ConfigPending = true;
    }
  }

  m_Enabled = true;
  FURBLE_SIM_GPS_STATE("acquiring");
  acquirePowerLock();
}

void GPS::serviceProbe(void) {
  if (!m_Enabled) {
    return;
  }

  const uint32_t now = Platform::getInstance().tick();

  if (m_ProbePending.exchange(false)) {
    const Casic::Autobaud::Action action =
        m_Autobaud.begin(now, getStatusSnapshot().sentences_passed);
    if (action.change) {
      uart_set_baudrate(m_UART, action.baud);
      reset();
    }
    m_ReceiverState.store(static_cast<uint8_t>(receiver_state_t::DETECTING));
    return;
  }

  if (static_cast<receiver_state_t>(m_ReceiverState.load()) == receiver_state_t::DETECTING) {
    const Casic::Autobaud::Action action =
        m_Autobaud.service(now, getStatusSnapshot().sentences_passed);
    if (action.change) {
      uart_set_baudrate(m_UART, action.baud);
      reset();
    }
    switch (m_Autobaud.state()) {
      case Casic::Autobaud::State::LOCKED:
        onProbeLocked(now);
        break;
      case Casic::Autobaud::State::NO_RECEIVER:
        onProbeFailed(now);
        break;
      default:
        break;
    }
    return;
  }

  // one retry after 60 s, then leave an absent unit alone
  if ((static_cast<receiver_state_t>(m_ReceiverState.load()) == receiver_state_t::ABSENT)
      && !m_NoReceiverRetried && tickReached(now, m_NoReceiverRetryAt)) {
    // Already on the GPS task, so start the ladder here rather than deferring
    // through m_ProbePending. Deferring would idle for a whole retry interval
    // before the first step ran, because an absent receiver has nothing else
    // to wake the task for.
    m_NoReceiverRetried = true;
    setRailPower(true);
    const Casic::Autobaud::Action action =
        m_Autobaud.begin(now, getStatusSnapshot().sentences_passed);
    uart_set_baudrate(m_UART, action.baud);
    reset();
    m_ReceiverState.store(static_cast<uint8_t>(receiver_state_t::DETECTING));
  }
}

void GPS::onProbeLocked(uint32_t now) {
  const uint32_t baud = m_Autobaud.baud();
  m_DetectedBaud.store(baud);
  ESP_LOGI(LOG_TAG, "GPS autobaud locked at %lu", static_cast<unsigned long>(baud));

  {
    std::lock_guard<std::mutex> guard(m_CycleMutex);
    resetAcquisition(now);
    m_ConfigChars = getStatusSnapshot().chars_processed;
    m_ConfigStart = now;
    m_ConfigPending = true;
  }

  // publish PRESENT last so the task resumes the cycle only once state is set
  m_ReceiverState.store(static_cast<uint8_t>(receiver_state_t::PRESENT));
  acquirePowerLock();
}

void GPS::onProbeFailed(uint32_t now) {
  ESP_LOGW(LOG_TAG, "GPS autobaud found no receiver");
  m_DetectedBaud.store(0);
  m_ConfigPending = false;

  {
    std::lock_guard<std::mutex> guard(m_CycleMutex);
    m_CycleState = cycle_state_t::DISABLED;
    m_BurstActive = false;
    m_CycleRequest = false;
  }

  // drop the rail so an absent unit does not hold power on, then idle
  setRailPower(false);
  releasePowerLock();
  m_NoReceiverRetryAt = now + NO_RECEIVER_RETRY_MS;
  m_ReceiverState.store(static_cast<uint8_t>(receiver_state_t::ABSENT));
}

void GPS::acquirePowerLock(void) {
#if defined(FURBLE_M5STICKS3)
  if (!gpsPowerLockRequired(m_Enabled.load(), m_CycleState == cycle_state_t::DEGRADED)) {
    return;
  }

  std::lock_guard<std::mutex> guard(m_PowerLockMutex);
  if (!m_PowerLock.has_value()) {
    m_PowerLock.emplace(Power::LockType::NO_LIGHT_SLEEP, POWER_LOCK_OWNER);
  }
#endif
}

void GPS::releasePowerLock(void) {
#if defined(FURBLE_M5STICKS3)
  std::lock_guard<std::mutex> guard(m_PowerLockMutex);
  m_PowerLock.reset();
#endif
}

uint8_t GPS::powerPolicy(void) const {
  const uint8_t policy = Settings::load<Settings::GPS_POWER>();
  return policy <= POWER_RAIL_CYCLE ? policy : POWER_ALWAYS_ON;
}

uint8_t GPS::dutySeconds(void) const {
  const uint8_t seconds = Settings::load<Settings::GPS_DUTY>();
  for (const uint8_t supported : DUTY_SECONDS) {
    if (supported == seconds) {
      return seconds;
    }
  }
  return 0;
}

uint32_t GPS::gpsRateInterval(void) const {
  const uint8_t rate = Settings::load<Settings::GPS_RATE>();
  return (rate < RATE_MS.size()) ? RATE_MS[rate] : 0;
}

bool GPS::dutyCycleEnabled(void) const {
  return ((m_PowerPolicy == POWER_STANDBY) || (m_PowerPolicy == POWER_RAIL_CYCLE))
         && (m_DutySeconds > 0);
}

TickType_t GPS::cycleWait(uint32_t now) const {
  uint32_t wait = 100;

  // Nothing answered the autobaud ladder. The rail is off and the power lock
  // is released, so the only thing left to wake for is the single retry. Ten
  // wakeups a second forever is exactly the drain plan 98b removed, so sleep
  // until the retry is due and then stay idle.
  if (static_cast<receiver_state_t>(m_ReceiverState.load()) == receiver_state_t::ABSENT) {
    const uint32_t idle =
        m_NoReceiverRetried ? ABSENT_IDLE_WAIT_MS : tickUntil(now, m_NoReceiverRetryAt);
    return pdMS_TO_TICKS(std::max(idle, MIN_CYCLE_WAIT_MS));
  }

  // a burst can be active in any state, wake in time for the idle gap check
  if (m_BurstActive) {
    wait = std::min(wait, tickUntil(now, m_LastSentence + BURST_GAP_MS));
  }

  switch (m_CycleState) {
    case cycle_state_t::WAITING:
    case cycle_state_t::STANDBY:
    case cycle_state_t::RAIL_OFF:
      if (m_HavePrediction) {
        wait = std::min(wait, tickUntil(now, m_NextBurst - m_Window));
      }
      break;

    case cycle_state_t::BURST:
      if (!m_BurstActive && (m_WakeDeadline != 0)) {
        wait = std::min(wait, tickUntil(now, m_WakeDeadline));
      }
      break;

    case cycle_state_t::MEASURING:
      if (!m_BurstActive && (m_MeasureDeadline != 0)) {
        wait = std::min(wait, tickUntil(now, m_MeasureDeadline));
      }
      break;

    case cycle_state_t::RESYNC:
      if (!m_BurstActive) {
        wait = std::min(wait, tickUntil(now, m_ResyncDeadline));
      }
      break;

    case cycle_state_t::ACQUIRING:
      // a degraded retry probe is time bounded, sleep until it expires
      if (!m_BurstActive && (m_ProbeDeadline != 0)) {
        wait = std::min(wait, tickUntil(now, m_ProbeDeadline));
      }
      break;

    case cycle_state_t::DEGRADED:
      // the lock is released here, sleep until the next retry is due
      wait = std::min(wait, tickUntil(now, m_Degraded.retryDeadline()));
      break;

    case cycle_state_t::DISABLED:
      break;
  }

  // a stale deadline must never turn the task loop into a busy spin
  return pdMS_TO_TICKS(std::max(wait, MIN_CYCLE_WAIT_MS));
}

void GPS::serviceCycle(void) {
  std::unique_lock<std::mutex> lock(m_CycleMutex, std::try_to_lock);
  if (!lock.owns_lock()) {
    // enable() or disable() is resetting the cycle state
    return;
  }

  if (!m_Enabled) {
    releasePowerLock();
    return;
  }

  const uint32_t now = Platform::getInstance().tick();

  // a burst can go idle in any state, the per state early returns in
  // finishBurst() decide what happens next
  if (m_BurstActive && tickReached(now, m_LastSentence + BURST_GAP_MS)) {
    finishBurst(now);
  }

  // The application update sets this only after it has accepted a valid fix.
  if (m_CycleRequest.exchange(false)) {
    // A valid fix received during a degraded retry is recovery, even when the
    // duty-cycle request immediately transitions into standby or rail-off.
    // Do not carry the old backoff into the next failure episode.
    m_Degraded.reset();
    acquirePowerLock();

    if ((m_PowerPolicy == POWER_STANDBY) && (m_DutySeconds > 0)) {
      sendCommand("PCAS12," + std::to_string(m_DutySeconds));
      enterStandby(now);
      return;
    }

    if ((m_PowerPolicy == POWER_RAIL_CYCLE) && (m_DutySeconds > 0)) {
      enterRailOff(now);
      return;
    }
  }

  switch (m_CycleState) {
    case cycle_state_t::WAITING:
    case cycle_state_t::STANDBY:
    case cycle_state_t::RAIL_OFF:
      if (m_HavePrediction && tickReached(now, m_NextBurst - m_Window)) {
        beginWindow(now);
      }
      break;

    case cycle_state_t::BURST:
      if (!m_BurstActive && (m_WakeDeadline != 0) && tickReached(now, m_WakeDeadline)) {
        enterDegraded();
      }
      break;

    case cycle_state_t::ACQUIRING:
      // re-assert the lock, a release raced by enable() must not leave the
      // receiver deaf under light sleep
      acquirePowerLock();
      // a degraded retry probe must not latch here when no burst arrives, give
      // up again on the bounded backoff instead of holding the lock forever
      if (!m_BurstActive && (m_ProbeDeadline != 0) && tickReached(now, m_ProbeDeadline)) {
        enterDegraded();
      }
      break;

    case cycle_state_t::MEASURING:
      if (!m_BurstActive && (m_MeasureDeadline != 0) && tickReached(now, m_MeasureDeadline)) {
        finishMeasurement();
      }
      break;

    case cycle_state_t::RESYNC:
      if (!m_BurstActive && tickReached(now, m_ResyncDeadline)) {
        if (m_ExpectedInterval == 0) {
          enterDegraded();
        } else {
          m_NextBurst = m_LastBurstStart + m_ExpectedInterval;
          if (tickReached(now, m_NextBurst)) {
            m_NextBurst = now + m_ExpectedInterval;
          }
          m_HavePrediction = true;
          m_CycleState = cycle_state_t::WAITING;
          // a healthy resync recovered the prediction, clear the degraded state
          m_Degraded.reset();
          releasePowerLock();
        }
      }
      break;

    case cycle_state_t::DEGRADED:
      // the backoff elapsed, wake the receiver and probe for a fresh burst. A
      // clean run recovers the duty cycle and drops the lock, a failure re-enters
      // enterDegraded() with a longer backoff, so the lock is never pinned.
      if (m_Degraded.retryDue(now)) {
        reset();
        m_ConfigChars = getStatusSnapshot().chars_processed;
        m_ConfigStart = now;
        m_ConfigPending = true;
        m_BurstActive = false;
        m_DutyWake = false;
        m_HavePrediction = false;
        m_ExpectedInterval = m_RateMs;
        m_ProbeDeadline = now + DEGRADED_PROBE_MS;
        m_CycleState = cycle_state_t::ACQUIRING;
        acquirePowerLock();
        FURBLE_SIM_GPS_STATE("acquiring");
      }
      break;

    case cycle_state_t::DISABLED:
      break;
  }
}

void GPS::beginBurst(uint32_t now) {
  if (m_BurstActive) {
    m_LastSentence = now;
    return;
  }

  // A late UART burst can arrive during the degraded backoff. Do not let that
  // event pin NO_LIGHT_SLEEP until the next scheduled retry. The retry path
  // changes to ACQUIRING before acquiring the lock.
  if (m_CycleState != cycle_state_t::DEGRADED) {
    acquirePowerLock();
  }

  const cycle_state_t previous = m_CycleState;
  if (m_HavePrediction && (previous != cycle_state_t::RESYNC)
      && (tickDistance(now, m_NextBurst) > m_Window)) {
    beginResync(now);
  }

  m_BurstActive = true;
  FURBLE_SIM_GPS_STATE("tracking");
  m_BurstStart = now;
  m_LastSentence = now;
  m_BurstFailed = getStatusSnapshot().sentences_failed;
  m_BurstSequence++;

  if (m_LastBurstStart != 0) {
    if (m_PeriodCount < m_PeriodSamples.size()) {
      m_PeriodSamples[m_PeriodCount++] = now - m_LastBurstStart;
    }
  }
  m_LastBurstStart = now;

  if (m_CycleState == cycle_state_t::ACQUIRING) {
    // a burst arrived, the acquisition or degraded retry probe succeeded
    m_ProbeDeadline = 0;
    if (m_ExpectedInterval == 0) {
      m_CycleState = cycle_state_t::MEASURING;
      m_MeasureDeadline = now + UNKNOWN_MEASURE_MS;
    } else {
      m_CycleState = cycle_state_t::BURST;
    }
  } else if ((m_CycleState == cycle_state_t::WAITING) || (m_CycleState == cycle_state_t::STANDBY)
             || (m_CycleState == cycle_state_t::RAIL_OFF)) {
    m_CycleState = cycle_state_t::BURST;
  }
}

void GPS::finishBurst(uint32_t now) {
  if (!m_BurstActive) {
    return;
  }

  m_BurstActive = false;

  const uint32_t failed = getStatusSnapshot().sentences_failed - m_BurstFailed;
  if (failed > 0) {
    m_Window += WINDOW_WIDEN_MS;
    const uint32_t interval = (m_ExpectedInterval > 0) ? m_ExpectedInterval : 1000;
    const uint32_t cap = std::max(WINDOW_MIN_MS, interval / 2);
    m_Window = std::min(m_Window, cap);
    m_ConsecutiveBadBursts++;
    m_CleanBursts = 0;
  } else {
    m_ConsecutiveBadBursts = 0;
    if (++m_CleanBursts >= CLEAN_BURSTS_TO_SHRINK) {
      m_Window = std::max(WINDOW_MIN_MS, m_Window - WINDOW_SHRINK_MS);
      m_CleanBursts = 0;
    }
  }

  if (m_CycleState == cycle_state_t::RESYNC) {
    return;
  }

  if (m_CycleState == cycle_state_t::MEASURING) {
    return;
  }

  if (m_CycleState == cycle_state_t::DEGRADED) {
    return;
  }

  if (m_DutyWake) {
    m_DutyWake = false;

    if ((m_PowerPolicy == POWER_RAIL_CYCLE)
        && (m_PushedSequence.load() != m_BurstSequence.load())) {
      enterDegraded();
      return;
    }

    if (m_PowerPolicy == POWER_STANDBY) {
      m_ExpectedInterval = m_RateMs;
      if (m_ExpectedInterval == 0) {
        enterDegraded();
        return;
      }
    }
  }

  if (m_ConsecutiveBadBursts >= BAD_BURSTS_TO_RESYNC) {
    beginResync(now);
    return;
  }

  if (m_ExpectedInterval == 0) {
    enterDegraded();
    return;
  }

  m_NextBurst = m_BurstStart + m_ExpectedInterval;
  m_HavePrediction = true;
  m_CycleState = cycle_state_t::WAITING;
  m_WakeDeadline = 0;
  // a clean predicted burst means reception recovered, clear any degraded state
  m_Degraded.reset();
  releasePowerLock();
}

void GPS::beginWindow(uint32_t now) {
  const cycle_state_t previous = m_CycleState;
  acquirePowerLock();

  if (previous == cycle_state_t::RAIL_OFF) {
    setRailPower(true);
    reset();
    m_ConfigChars = getStatusSnapshot().chars_processed;
    m_ConfigStart = now;
    m_ConfigPending = true;
  }

  m_BurstActive = false;
  m_DutyWake = (previous == cycle_state_t::STANDBY) || (previous == cycle_state_t::RAIL_OFF);
  m_WakeDeadline = now + std::max(MIN_WAKE_WAIT_MS, m_ExpectedInterval / 2);
  m_CycleState = cycle_state_t::BURST;
}

void GPS::enterStandby(uint32_t now) {
  m_BurstActive = false;
  m_DutyWake = false;
  m_ExpectedInterval = static_cast<uint32_t>(m_DutySeconds) * 1000;
  m_NextBurst = now + m_ExpectedInterval;
  m_HavePrediction = true;
  m_WakeDeadline = 0;
  m_CycleState = cycle_state_t::STANDBY;
  FURBLE_SIM_GPS_STATE("standby");
  releasePowerLock();
}

void GPS::enterRailOff(uint32_t now) {
  m_ConfigPending = false;
  m_BurstActive = false;
  m_DutyWake = false;
  m_ExpectedInterval = static_cast<uint32_t>(m_DutySeconds) * 1000;
  m_NextBurst = now + m_ExpectedInterval;
  m_HavePrediction = true;
  m_WakeDeadline = 0;
  m_CycleState = cycle_state_t::RAIL_OFF;
  FURBLE_SIM_GPS_STATE("off");
  setRailPower(false);
  releasePowerLock();
}

void GPS::beginResync(uint32_t now) {
  m_ExpectedInterval = (m_ExpectedInterval > 0) ? m_ExpectedInterval : 1000;
  m_ResyncDeadline = now + m_ExpectedInterval;
  m_HavePrediction = false;
  m_DutyWake = false;
  m_CycleState = cycle_state_t::RESYNC;
  FURBLE_SIM_GPS_STATE("acquiring");
  acquirePowerLock();
}

void GPS::finishMeasurement(void) {
  if (m_PeriodCount < 2) {
    enterDegraded();
    return;
  }

  const size_t count = std::min(m_PeriodCount, m_PeriodSamples.size());
  decltype(m_PeriodSamples) periods = {};
  std::copy_n(m_PeriodSamples.begin(), count, periods.begin());
  std::sort(periods.begin(), periods.begin() + count);

  const uint32_t median = periods[count / 2];
  const uint32_t tolerance = std::max<uint32_t>(50, median / 10);
  if ((periods[count - 1] - periods[0]) > tolerance) {
    enterDegraded();
    return;
  }

  m_ExpectedInterval = median;
  m_Window = std::min(m_Window, std::max(WINDOW_MIN_MS, median / 2));
  m_NextBurst = m_LastBurstStart + m_ExpectedInterval;
  m_HavePrediction = true;
  m_MeasureDeadline = 0;
  m_CycleState = cycle_state_t::WAITING;
  // a consistent measurement recovered the prediction, clear the degraded state
  m_Degraded.reset();
  releasePowerLock();
}

/**
 * Enter the degraded retry state.
 *
 * The burst windowed power management could not predict the receiver burst
 * timing, for example after a run of bad checksum bursts or a failed interval
 * measurement. This used to latch forever and pin the NO_LIGHT_SLEEP lock. It
 * now releases the lock and schedules a bounded retry, so a spell of poor
 * reception no longer costs the full lock current with no recovery. The GPS
 * task sleeps until the retry is due, then serviceCycle() re-acquires and probes
 * for a fresh burst. The backoff grows on repeated failures but is capped.
 */
void GPS::enterDegraded(void) {
  const uint32_t now = Platform::getInstance().tick();
  if (m_Degraded.enter(now)) {
    ESP_LOGW(LOG_TAG, "GPS burst prediction lost, degraded retry in %lu ms",
             static_cast<unsigned long>(m_Degraded.scheduledBackoff()));
  }
  m_HavePrediction = false;
  m_WakeDeadline = 0;
  m_ProbeDeadline = 0;
  m_CycleState = cycle_state_t::DEGRADED;
  FURBLE_SIM_GPS_STATE("degraded");
  releasePowerLock();
}

/** XOR of every character between '$' and '*', both excluded. */
uint8_t GPS::checksum(const std::string &payload) {
  uint8_t sum = 0;

  for (const char c : payload) {
    sum ^= static_cast<uint8_t>(c);
  }

  return sum;
}

/** Frame the payload as an NMEA sentence and send it to the receiver. */
void GPS::sendCommand(const std::string &payload) {
  std::array<char, 4> suffix;

  snprintf(suffix.data(), suffix.size(), "*%02X", checksum(payload));
  const std::string command = "$" + payload + suffix.data() + "\r\n";

  ESP_LOGI(LOG_TAG, "GPS command: %s", payload.c_str());

  const std::lock_guard<std::mutex> lock(m_TxMutex);
  uart_write_bytes(m_UART, command.data(), command.size());
  uart_wait_tx_done(m_UART, pdMS_TO_TICKS(TX_MS));
}

/**
 * Build the CASIC checksum from little endian four byte words.
 *
 * The CASIC specification puts the class before the message id in its formula.
 * The L76K formula and all recorded frames use the message id first.
 */
uint32_t GPS::casicChecksum(uint8_t class_id,
                            uint8_t message_id,
                            uint16_t length,
                            const uint8_t *payload) {
  uint32_t sum =
      (static_cast<uint32_t>(message_id) << 24) | (static_cast<uint32_t>(class_id) << 16) | length;

  for (uint16_t offset = 0; offset < length; offset += 4) {
    uint32_t word = 0;
    for (uint16_t byte = 0; (byte < 4) && ((offset + byte) < length); byte++) {
      word |= static_cast<uint32_t>(payload[offset + byte]) << (byte * 8);
    }
    sum += word;
  }

  return sum;
}

/** Frame and send one CASIC binary message. */
bool GPS::sendBinaryFrame(uint8_t class_id,
                          uint8_t message_id,
                          const std::vector<uint8_t> &payload) {
  if ((payload.size() > MAX_BINARY_PAYLOAD) || ((payload.size() % 4) != 0)) {
    ESP_LOGE(LOG_TAG, "GPS binary payload has invalid length: %u",
             static_cast<unsigned>(payload.size()));
    return false;
  }

  const uint16_t length = static_cast<uint16_t>(payload.size());
  std::vector<uint8_t> frame;
  frame.reserve(BINARY_FRAME_OVERHEAD + length);
  frame.push_back(BINARY_SYNC_0);
  frame.push_back(BINARY_SYNC_1);
  frame.push_back(static_cast<uint8_t>(length & 0xff));
  frame.push_back(static_cast<uint8_t>((length >> 8) & 0xff));
  frame.push_back(class_id);
  frame.push_back(message_id);
  frame.insert(frame.end(), payload.begin(), payload.end());

  const uint32_t sum = casicChecksum(class_id, message_id, length, payload.data());
  frame.push_back(static_cast<uint8_t>(sum & 0xff));
  frame.push_back(static_cast<uint8_t>((sum >> 8) & 0xff));
  frame.push_back(static_cast<uint8_t>((sum >> 16) & 0xff));
  frame.push_back(static_cast<uint8_t>((sum >> 24) & 0xff));

  const std::lock_guard<std::mutex> lock(m_TxMutex);
  const int written = uart_write_bytes(m_UART, frame.data(), frame.size());
  uart_wait_tx_done(m_UART, pdMS_TO_TICKS(TX_MS));
  if (written != static_cast<int>(frame.size())) {
    ESP_LOGE(LOG_TAG, "GPS binary write failed: %d of %u", written,
             static_cast<unsigned>(frame.size()));
    return false;
  }

  ESP_LOGI(LOG_TAG, "GPS binary: %02X %02X payload %u", class_id, message_id, length);
  return true;
}

bool GPS::sendBinary(uint8_t class_id, uint8_t message_id, const std::vector<uint8_t> &payload) {
  return sendBinaryFrame(class_id, message_id, payload);
}

/** Restart the receiver, 0 hot, 1 warm, 2 cold. */
void GPS::restart(uint8_t mode) {
  if (!m_Enabled || (mode > 2)) {
    return;
  }

  sendCommand("PCAS10," + std::to_string(mode));
}

const char *GPS::configStateName(config_state_t state) {
  switch (state) {
    case CONFIG_QUEUED:
      return "QUEUED";
    case CONFIG_SENT:
      return "SENT";
    case CONFIG_ACKED:
      return "ACKED";
    case CONFIG_NACKED:
      return "NACKED";
    case CONFIG_TIMEOUT:
      return "TIMEOUT";
    case CONFIG_FALLBACK:
      return "FALLBACK";
  }
  return "UNKNOWN";
}

const char *GPS::sourceName(source_t source) {
  switch (source) {
    case SOURCE_NONE:
      return "none";
    case SOURCE_UART:
      return "uart";
    case SOURCE_COMPANION:
      return "companion";
  }
  return "unknown";
}

const char *GPS::sourceShortName(source_t source) {
  switch (source) {
    case SOURCE_NONE:
      return "none";
    case SOURCE_UART:
      return "uart";
    case SOURCE_COMPANION:
      return "comp";
  }
  return "unkn";
}

const char *GPS::cycleStateName(cycle_state_t state) {
  switch (state) {
    case cycle_state_t::DISABLED:
      return "disabled";
    case cycle_state_t::ACQUIRING:
      return "acquiring";
    case cycle_state_t::MEASURING:
      return "measuring";
    case cycle_state_t::BURST:
      return "burst";
    case cycle_state_t::WAITING:
      return "waiting";
    case cycle_state_t::STANDBY:
      return "standby";
    case cycle_state_t::RAIL_OFF:
      return "rail_off";
    case cycle_state_t::RESYNC:
      return "resync";
    case cycle_state_t::DEGRADED:
      return "degraded";
  }
  return "unknown";
}

/**
 * Report the receiver power, rate and assist state.
 *
 * The cycle fields come from the same mutex getCycleStatusSnapshot() takes, and
 * from one acquisition of it, so they are a coherent set. The assist fields are
 * read afterwards, under the AID mutex, with the cycle mutex already released.
 * The two locks are never nested, so this adds no lock order edge to the GPS
 * task.
 */
GPS::receiver_status_t GPS::getReceiverStatus(void) const {
  receiver_status_t status = {};

  {
    const std::lock_guard<std::mutex> lock(m_CycleMutex);
    status.cycle_state = cycleStateName(m_CycleState);
    status.power_policy = m_PowerPolicy;
    status.duty_seconds = m_DutySeconds;
    status.rate_ms = m_RateMs;
    status.have_sentence = m_LastSentence != 0;
    // unsigned subtraction, so this stays correct across a tick wrap
    status.last_sentence_age_ms =
        status.have_sentence ? (Platform::getInstance().tick() - m_LastSentence) : 0;
    // read under the same acquisition as the state, a second one could pair a
    // fresh state with a stale retry count
    status.degraded = m_CycleState == cycle_state_t::DEGRADED;
    status.retries = m_Degraded.failures();
  }

  status.aid_mode = m_AidMode.load();
  {
    const std::lock_guard<std::mutex> lock(m_AidMutex);
    status.aid_cache_valid = m_AidCacheValid;
  }

  return status;
}

std::vector<GPS::config_status_t> GPS::getConfigStatus(void) const {
  std::vector<config_status_t> status;
  const std::lock_guard<std::mutex> lock(m_ConfigMutex);
  status.reserve(m_ConfigStatusCount);
  for (size_t i = 0; i < m_ConfigStatusCount; i++) {
    status.push_back(m_ConfigStatus[i]);
  }
  return status;
}

void GPS::enqueueConfig(uint8_t class_id,
                        uint8_t message_id,
                        const std::vector<uint8_t> &payload,
                        const std::string &fallback,
                        bool navx_query,
                        bool front) {
  size_t status_index;
  {
    const std::lock_guard<std::mutex> lock(m_ConfigMutex);
    if (m_ConfigStatusCount >= MAX_CONFIG_COMMANDS) {
      ESP_LOGE(LOG_TAG, "GPS configuration queue is full");
      return;
    }
    status_index = m_ConfigStatusCount++;
    m_ConfigStatus[status_index] = {
        class_id,
        message_id,
        CONFIG_QUEUED,
        0,
    };
  }

  binary_command_t command = {
      class_id, message_id, payload, fallback, navx_query, status_index,
  };
  if (front) {
    m_ConfigQueue.push_front(std::move(command));
  } else {
    m_ConfigQueue.push_back(std::move(command));
  }
}

void GPS::startConfigCommand(void) {
  if (m_ConfigInFlight.has_value() || m_ConfigQueue.empty()) {
    return;
  }

  m_ConfigInFlight = std::move(m_ConfigQueue.front());
  m_ConfigQueue.pop_front();
  m_ConfigAttempts = 1;
  m_ConfigSent = Platform::getInstance().tick();
  m_ConfigNavxAcked = false;
  m_NavxPayloadValid = false;

  {
    const std::lock_guard<std::mutex> lock(m_ConfigMutex);
    auto &status = m_ConfigStatus[m_ConfigInFlight->status_index];
    status.state = CONFIG_SENT;
    status.attempts = m_ConfigAttempts;
    m_ConfigInFlightStatus = m_ConfigInFlight->status_index;
  }

  if (!sendBinaryFrame(m_ConfigInFlight->class_id, m_ConfigInFlight->message_id,
                       m_ConfigInFlight->payload)) {
    ESP_LOGW(LOG_TAG, "GPS binary command write failed");
  }
}

void GPS::finishConfigCommand(config_state_t state) {
  if (!m_ConfigInFlight.has_value()) {
    return;
  }

  binary_command_t command = std::move(m_ConfigInFlight.value());
  m_ConfigInFlight.reset();
  m_ConfigAttempts = 0;
  m_ConfigNavxAcked = false;
  m_NavxPayloadValid = false;

  bool sendFallback = false;
  {
    const std::lock_guard<std::mutex> lock(m_ConfigMutex);
    auto &status = m_ConfigStatus[command.status_index];
    status.state = state;
    status.attempts =
        (state == CONFIG_ACKED || state == CONFIG_NACKED) ? status.attempts : BINARY_ATTEMPTS;
    sendFallback = (state != CONFIG_ACKED) && !command.fallback.empty() && !m_ConfigFallbackUsed;
  }

  if (sendFallback) {
    m_ConfigFallbackUsed = true;
    // a NAVX fallback can carry two sentences, one per masked field
    size_t start = 0;
    while (start <= command.fallback.size()) {
      const size_t nl = command.fallback.find('\n', start);
      const size_t count = (nl == std::string::npos) ? std::string::npos : (nl - start);
      const std::string sentence = command.fallback.substr(start, count);
      if (!sentence.empty()) {
        sendCommand(sentence);
      }
      if (nl == std::string::npos) {
        break;
      }
      start = nl + 1;
    }
    if (state == CONFIG_TIMEOUT) {
      const std::lock_guard<std::mutex> lock(m_ConfigMutex);
      m_ConfigStatus[command.status_index].state = CONFIG_FALLBACK;
    }
  }
}

/**
 * Build the acknowledged configuration queue.
 *
 * A fresh install has all PR14 settings disabled, so this queue stays empty.
 */
void GPS::configure(void) {
  const uint8_t rate = Settings::load<Settings::GPS_RATE>();
  const bool prune = Settings::load<Settings::GPS_NMEA>();
  const uint8_t constellation = Settings::load<Settings::GPS_CONSTEL>();
  const uint8_t platform = Settings::load<Settings::GPS_PLATFORM>();

  m_ConfigQueue.clear();
  m_ConfigInFlight.reset();
  m_ConfigFallbackUsed = false;
  m_ConfigNavxAcked = false;
  m_NavxPayloadValid = false;
  m_NavxConstelSet = false;
  m_NavxDyModelSet = false;
  {
    const std::lock_guard<std::mutex> lock(m_ConfigMutex);
    m_ConfigStatusCount = 0;
  }

  if (prune) {
    const std::array<uint8_t, 8> messages = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x08, 0x11};
    const std::array<uint8_t, 8> rates = {1, 0, 0, 0, 1, 0, 0, 0};
    for (size_t i = 0; i < messages.size(); i++) {
      const std::vector<uint8_t> payload = {0x4E, messages[i], rates[i], 0};
      enqueueConfig(CFG_CLASS, CFG_MSG_ID, payload, "PCAS03,1,0,0,0,1,0,0,0");
    }
  }

  if ((rate > 0) && (rate < RATE_MS.size())) {
    std::vector<uint8_t> payload(4, 0);
    writeU16(payload.data(), RATE_MS[rate]);
    enqueueConfig(CFG_CLASS, 0x04, payload, "PCAS02," + std::to_string(RATE_MS[rate]));
  }

  // CFG-NAVX carries both the constellation mask and the dynamic platform
  // model. Apply them in one query-modify-write so the 44 byte struct is only
  // rewritten once and never built from zeros.
  if ((constellation > 0) && (constellation <= CONSTELLATION_MAX)) {
    m_NavxConstelSet = true;
    m_NavxConstelVal = constellation;
  }
  // roller index 1 Portable, 2 Stationary, 3 Pedestrian, 4 Vehicle maps to the
  // dyModel wire values 0 Portable, 1 Static, 2 Walking, 3 Car.
  if ((platform >= 1) && (platform <= 4)) {
    m_NavxDyModelSet = true;
    m_NavxDyModelVal = static_cast<uint8_t>(platform - 1);
  }
  if (m_NavxConstelSet || m_NavxDyModelSet) {
    std::string fallback;
    if (m_NavxConstelSet) {
      fallback = "PCAS04," + std::to_string(m_NavxConstelVal);
    }
    if (m_NavxDyModelSet) {
      // $PCAS11 platform numbering is third-party attested only, provisional
      if (!fallback.empty()) {
        fallback += "\n";
      }
      fallback += "PCAS11," + std::to_string(m_NavxDyModelVal);
    }
    const std::vector<uint8_t> query;
    enqueueConfig(CFG_CLASS, CFG_NAVX_ID, query, fallback, true);
  }
}

/** Configure the receiver once it is awake, then service one binary command. */
void GPS::serviceConfig(void) {
  if (!m_Enabled) {
    return;
  }

  if (m_ConfigPending) {
    const bool awake = getStatusSnapshot().chars_processed > m_ConfigChars;
    const bool expired = (Platform::getInstance().tick() - m_ConfigStart) > SETTLE_MS;
    if (!awake && !expired) {
      return;
    }

    m_ConfigPending = false;
    if (m_AidMode.load() != 0) {
      // Assistance goes out before configuration, because a sentence prune
      // could remove messages the assistance path relies on.
      sendAidIni();
      armEphemerisReplay();
    }
    configure();
  }

  if (m_ConfigInFlight.has_value()) {
    const uint32_t now = Platform::getInstance().tick();
    if ((now - m_ConfigSent) < BINARY_ACK_TIMEOUT_MS) {
      return;
    }

    if (m_ConfigAttempts < BINARY_ATTEMPTS) {
      m_ConfigAttempts++;
      m_ConfigSent = now;
      m_ConfigNavxAcked = false;
      m_NavxPayloadValid = false;
      {
        const std::lock_guard<std::mutex> lock(m_ConfigMutex);
        auto &status = m_ConfigStatus[m_ConfigInFlightStatus];
        status.state = CONFIG_SENT;
        status.attempts = m_ConfigAttempts;
      }
      if (!sendBinaryFrame(m_ConfigInFlight->class_id, m_ConfigInFlight->message_id,
                           m_ConfigInFlight->payload)) {
        ESP_LOGW(LOG_TAG, "GPS binary retry write failed");
      }
    } else {
      finishConfigCommand(CONFIG_TIMEOUT);
    }
    return;
  }

  startConfigCommand();
}

int64_t GPS::toUnixSeconds(const Camera::timesync_t &timesync) {
  if ((timesync.year < 1970) || (timesync.month < 1) || (timesync.month > 12) || (timesync.day < 1)
      || (timesync.hour > 23) || (timesync.minute > 59) || (timesync.second > 59)) {
    return -1;
  }

  static constexpr std::array<uint8_t, 12> DAYS = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  const uint8_t monthDays = DAYS[timesync.month - 1]
                            + ((timesync.month == 2) && ((timesync.year % 4) == 0)
                               && (((timesync.year % 100) != 0) || ((timesync.year % 400) == 0)));
  if (timesync.day > monthDays) {
    return -1;
  }
  int64_t days = 0;
  for (uint32_t year = 1970; year < timesync.year; year++) {
    const bool leap = ((year % 4) == 0) && (((year % 100) != 0) || ((year % 400) == 0));
    days += leap ? 366 : 365;
  }
  for (uint8_t month = 1; month < timesync.month; month++) {
    days += DAYS[month - 1];
    if ((month == 2) && ((timesync.year % 4) == 0)
        && (((timesync.year % 100) != 0) || ((timesync.year % 400) == 0))) {
      days++;
    }
  }
  days += timesync.day - 1;

  return (days * 24 * 60 * 60) + (timesync.hour * 60 * 60) + (timesync.minute * 60)
         + timesync.second;
}

void GPS::loadAidCache(void) {
  std::lock_guard<std::mutex> lock(m_AidMutex);
  if (m_AidCacheLoaded) {
    return;
  }

  aid_cache_t cache = {};
  Preferences prefs;
  bool valid = false;
  if (prefs.begin(FURBLE_STR, true)) {
    if (prefs.isKey("gps_fix") && (prefs.get("gps_fix", &cache, sizeof(cache)) == sizeof(cache))) {
      const Camera::timesync_t timesync = {
          cache.year,   cache.month,  cache.day,         cache.hour,
          cache.minute, cache.second, cache.centisecond,
      };
      valid = (cache.magic == AID_CACHE_MAGIC) && (cache.version == AID_CACHE_VERSION)
              && validCoordinate(cache.latitude, -90.0, 90.0)
              && validCoordinate(cache.longitude, -180.0, 180.0)
              && (toUnixSeconds(timesync) == cache.utc_seconds);
    }
    prefs.end();
  }

  m_AidCacheLoaded = true;
  m_AidCacheValid = valid;
  m_AidCacheTickValid = false;
  m_AidCacheWriteValid = false;
  if (valid) {
    m_AidCache = cache;
  }
}

void GPS::updateAidCache(const Camera::gps_t &gps, const Camera::timesync_t &timesync) {
  if (m_AidMode.load() == 0) {
    return;
  }

  const int64_t utc_seconds = toUnixSeconds(timesync);
  if ((utc_seconds < GPS_EPOCH_UNIX) || !validCoordinate(gps.latitude, -90.0, 90.0)
      || !validCoordinate(gps.longitude, -180.0, 180.0)) {
    return;
  }

  const uint32_t now = Platform::getInstance().tick();
  aid_cache_t cache = {
      AID_CACHE_MAGIC,
      AID_CACHE_VERSION,
      {0, 0, 0},
      gps.latitude,
      gps.longitude,
      std::isfinite(gps.altitude) ? gps.altitude : 0.0,
      utc_seconds,
      now,
      static_cast<uint16_t>(timesync.year),
      static_cast<uint8_t>(timesync.month),
      static_cast<uint8_t>(timesync.day),
      static_cast<uint8_t>(timesync.hour),
      static_cast<uint8_t>(timesync.minute),
      static_cast<uint8_t>(timesync.second),
      static_cast<uint8_t>(timesync.centisecond),
      static_cast<uint8_t>(std::isfinite(gps.altitude) ? 1 : 0),
      {0, 0, 0},
  };

  bool write = false;
  {
    const std::lock_guard<std::mutex> lock(m_AidMutex);
    write = !m_AidCacheWriteValid || ((now - m_AidCacheLastWrite) >= AID_CACHE_WRITE_MS);
    m_AidCache = cache;
    m_AidCacheValid = true;
    m_AidCacheTickValid = true;
    m_AidCacheWriteValid = m_AidCacheWriteValid || write;
    m_AidCacheTick = now;
    if (write) {
      m_AidCacheLastWrite = now;
    }
  }

  if (write) {
    Preferences prefs;
    bool saved = false;
    if (prefs.begin(FURBLE_STR, false)) {
      saved = prefs.put("gps_fix", &cache, sizeof(cache)) == sizeof(cache);
      prefs.end();
    }
    if (!saved) {
      const std::lock_guard<std::mutex> lock(m_AidMutex);
      m_AidCacheWriteValid = false;
    }
  }
}

bool GPS::sendAidIni(void) {
  if (m_AidMode.load() == 0) {
    return false;
  }

  loadAidCache();

  aid_cache_t cache;
  bool tickValid;
  uint32_t captureTick;
  {
    const std::lock_guard<std::mutex> lock(m_AidMutex);
    if (!m_AidCacheValid) {
      return false;
    }
    cache = m_AidCache;
    tickValid = m_AidCacheTickValid;
    captureTick = m_AidCacheTick;
  }

  const uint32_t nowTick = Platform::getInstance().tick();
  uint32_t age = 0;
  int64_t utc_seconds = cache.utc_seconds;
  float pAcc = 50000.0f;
  float tAcc = 3600.0f;
  bool timeValid = true;
  if (tickValid) {
    age = nowTick - captureTick;
    if (age > AID_CACHE_MAX_AGE_MS) {
      return false;
    }
    utc_seconds += age / 1000;
    pAcc = std::max(10.0f, std::min(50000.0f, (age / 1000.0f) * 13.8889f));
    tAcc = 2.0f;
  } else {
    const int64_t wall = static_cast<int64_t>(time(nullptr));
    if (wall >= cache.utc_seconds) {
      const int64_t wall_age = wall - cache.utc_seconds;
      if (wall_age > (AID_CACHE_MAX_AGE_MS / 1000)) {
        return false;
      }
      age = static_cast<uint32_t>(wall_age) * 1000;
      utc_seconds = wall;
      pAcc = std::max(10.0f, std::min(50000.0f, (age / 1000.0f) * 13.8889f));
      tAcc = 3600.0f;
    } else {
      // A no-backup-rail cold reboot leaves the ESP32 clock unset (~1970), so
      // wall time trails the cached fix and the cache age is unknown. Do not
      // assert a valid time of unknown age: that would feed the receiver an
      // over-confident wrong time and hurt TTFF. Send position-only assist
      // instead. The cached location still narrows the search safely.
      timeValid = false;
      pAcc = 50000.0f;
      tAcc = 3600.0f;
    }
  }

  if (utc_seconds < GPS_EPOCH_UNIX) {
    return false;
  }

  const int64_t gps_seconds = utc_seconds - GPS_EPOCH_UNIX + GPS_LEAP_SECONDS;
  const uint16_t week = static_cast<uint16_t>(gps_seconds / (7 * 24 * 60 * 60));
  const double tow = static_cast<double>(gps_seconds % (7 * 24 * 60 * 60));
  const bool altitudeValid = (cache.flags & 1) != 0;
  uint8_t flags = static_cast<uint8_t>(0x23 | (altitudeValid ? 0 : 0x40));
  if (!timeValid) {
    // Clear B1 time valid so the receiver ignores tow and wn. The cached time
    // is of unknown age after a rail-cut reboot, so we send position-only
    // aiding rather than an over-confident wrong time.
    flags &= static_cast<uint8_t>(~0x02);
  }

  std::vector<uint8_t> payload(56, 0);
  writeValue(payload, 0, cache.latitude);
  writeValue(payload, 8, cache.longitude);
  writeValue(payload, 16, cache.altitude);
  writeValue(payload, 24, tow);
  const float zero = 0.0f;
  writeValue(payload, 32, zero);
  writeValue(payload, 36, pAcc);
  writeValue(payload, 40, tAcc);
  writeValue(payload, 44, zero);
  writeU32(payload.data() + 48, 0);
  writeU16(payload.data() + 52, week);
  payload[54] = 0;
  payload[55] = flags;

  const bool sent = sendBinaryFrame(AID_CLASS, AID_INI_ID, payload);
  if (sent) {
    ESP_LOGI(LOG_TAG, "GPS AID-INI sent, cache age %lu ms", static_cast<unsigned long>(age));
  }
  return sent;
}

void GPS::loadEphemerisCache(void) {
  m_EphReplay.clear();
  m_EphReplaySpans.clear();
  m_EphReplayIndex = 0;
  m_EphCaptureUtc = 0;

  if (Settings::load<Settings::GPS_ASSIST>() != 2) {
    return;
  }

  Preferences prefs;
  if (!prefs.begin(FURBLE_STR, true)) {
    return;
  }

  std::vector<uint8_t> blob;
  if (prefs.isKey("gps_eph")) {
    const size_t length = prefs.getBytesLength("gps_eph");
    if ((length > sizeof(eph_cache_header_t))
        && (length <= (sizeof(eph_cache_header_t) + EPH_MAX_BYTES))) {
      blob.resize(length);
      if (prefs.get("gps_eph", blob.data(), blob.size()) != blob.size()) {
        blob.clear();
      }
    }
  }
  prefs.end();

  if (blob.size() <= sizeof(eph_cache_header_t)) {
    return;
  }

  eph_cache_header_t header = {};
  std::memcpy(&header, blob.data(), sizeof(header));
  const size_t payloadBytes = blob.size() - sizeof(header);
  if ((header.magic != EPH_CACHE_MAGIC) || (header.version != EPH_CACHE_VERSION)
      || (header.byte_count != payloadBytes) || (header.frame_count == 0)) {
    return;
  }

  m_EphReplay.assign(blob.begin() + sizeof(header), blob.end());
  const std::vector<std::pair<size_t, size_t>> spans =
      Casic::splitFrames(m_EphReplay.data(), m_EphReplay.size());
  size_t framedBytes = 0;
  for (const auto &span : spans) {
    framedBytes += span.second;
  }
  if ((spans.size() != header.frame_count) || (framedBytes != m_EphReplay.size())) {
    m_EphReplay.clear();
    return;
  }
  m_EphCaptureUtc = header.capture_utc;
}

/** The UTC the receiver is reporting, or 0 when it is reporting none. */
int64_t GPS::receiverUtc(const status_t &status) {
  if (!status.date_valid || !status.time_valid) {
    return 0;
  }
  const Camera::timesync_t timesync = {
      status.year,   status.month,  status.day,         status.hour,
      status.minute, status.second, status.centisecond,
  };
  const int64_t utc = toUnixSeconds(timesync);
  return (utc < GPS_EPOCH_UNIX) ? 0 : utc;
}

void GPS::armEphemerisReplay(void) {
  m_EphReplayArmed = false;
  if ((m_AidMode.load() != 2) || m_EphReplay.empty()) {
    return;
  }

  // Do not decide yet. GPS ephemeris is valid for roughly four hours, and the
  // only clock that can bound that is the receiver's own: furble's kept clock
  // restores as the last persisted epoch plus the monotonic time since boot,
  // with no knowledge of how long the board was off, so a week unplugged reads
  // as a couple of minutes old. Arm here, and commit in servicePoll once the
  // receiver has reported a UTC of its own.
  m_EphReplayArmed = true;
  m_EphArmDateUpdates = m_DateUpdates;
  ESP_LOGI(LOG_TAG, "GPS ephemeris replay armed, waiting for receiver time");
}

/**
 * Commit or drop an armed ephemeris replay.
 *
 * Runs on the GPS task once a date has been committed after arming, so a date
 * left over from the previous session cannot answer for this one. A receiver
 * that never reports a time is a cold start with no clock at all: the cache age
 * cannot be established, so it is dropped rather than replayed blind.
 */
void GPS::serviceEphemerisArm(void) {
  if (!m_EphReplayArmed) {
    return;
  }

  // Only a date the receiver has sent since arming can bound the cache age, and
  // only RMC carries a date. A receiver with no clock, or one with RMC pruned,
  // keeps whatever date the previous session left in the parser while its time
  // ticks on from GGA, so neither the time nor the date value is evidence here.
  if (m_DateUpdates == m_EphArmDateUpdates) {
    // Nothing to decide against yet. The arm stands until the next enable,
    // which is the cold start case: replaying a cache whose age cannot be
    // established is the thing this is here to prevent.
    return;
  }
  const int64_t reported = receiverUtc(getStatusSnapshot());
  if (reported <= 0) {
    return;
  }

  const Casic::Eph::Freshness fresh =
      Casic::Eph::freshness(m_EphCaptureUtc, reported, EPH_CACHE_MAX_AGE_MS / 1000);

  if (fresh == Casic::Eph::Freshness::IMPLAUSIBLE) {
    // Not a verdict. A cold-start AT6668 reports a coarse time before it has
    // decoded TOW, so a receiver clock behind the capture means the receiver
    // has not finished waking, and it reports correctly seconds later. That is
    // the tier 2 case, so keep the arm standing and wait for the next date
    // rather than throwing the cache away on the first reading.
    m_EphArmDateUpdates = m_DateUpdates;
    return;
  }

  m_EphReplayArmed = false;
  if (fresh != Casic::Eph::Freshness::REPLAY) {
    ESP_LOGI(LOG_TAG, "GPS ephemeris cache is older than the receiver clock allows");
    m_EphReplay.clear();
    return;
  }

  m_EphReplaySpans = Casic::splitFrames(m_EphReplay.data(), m_EphReplay.size());
  m_EphReplayIndex = 0;
  m_EphReplayNext = Platform::getInstance().tick();
  ESP_LOGI(LOG_TAG, "GPS ephemeris replay armed, %u frames",
           static_cast<unsigned>(m_EphReplaySpans.size()));
}

void GPS::storeEphemeris(void) {
  std::vector<uint8_t> data;
  size_t frameCount = 0;
  {
    const std::lock_guard<std::mutex> lock(m_EphMutex);
    data = m_EphCollector.data();
    frameCount = m_EphCollector.frameCount();
    m_EphCollector.clear();
  }
  if (data.empty() || (frameCount == 0)) {
    return;
  }

  // TinyGPSPlus accessors clear update flags, so they are read only through the
  // locked snapshot. Touching m_GPS here would race the UI task.
  const int64_t utc = receiverUtc(getStatusSnapshot());
  if (utc <= 0) {
    return;
  }

  eph_cache_header_t header = {
      EPH_CACHE_MAGIC,
      EPH_CACHE_VERSION,
      {0, 0, 0},
      utc,
      Platform::getInstance().tick(),
      static_cast<uint16_t>(frameCount),
      static_cast<uint16_t>(data.size()),
  };

  std::vector<uint8_t> blob(sizeof(header) + data.size());
  std::memcpy(blob.data(), &header, sizeof(header));
  std::memcpy(blob.data() + sizeof(header), data.data(), data.size());

  Preferences prefs;
  bool saved = false;
  if (prefs.begin(FURBLE_STR, false)) {
    saved = prefs.put("gps_eph", blob.data(), blob.size()) == blob.size();
    prefs.end();
  }
  if (saved) {
    ESP_LOGI(LOG_TAG, "GPS ephemeris cached, %u frames %u bytes", static_cast<unsigned>(frameCount),
             static_cast<unsigned>(data.size()));
  }
}

/**
 * Tier 2 ephemeris poll and paced replay.
 *
 * Replay drains one stored frame per pass, so a 2.6 kB cache does not choke the
 * receiver's navigation loop. Polling runs once per session on the first stable
 * fix and at most once an hour after that.
 */
void GPS::servicePoll(void) {
  if (!m_Enabled) {
    return;
  }

  const uint32_t now = Platform::getInstance().tick();

  serviceEphemerisArm();

  // paced replay takes priority over polling
  if (!m_EphReplaySpans.empty() && (m_EphReplayIndex < m_EphReplaySpans.size())) {
    if (tickReached(now, m_EphReplayNext)) {
      const auto span = m_EphReplaySpans[m_EphReplayIndex];
      {
        const std::lock_guard<std::mutex> lock(m_TxMutex);
        uart_write_bytes(m_UART, m_EphReplay.data() + span.first, span.second);
        uart_wait_tx_done(m_UART, pdMS_TO_TICKS(TX_MS));
      }
      m_EphReplayIndex++;
      m_EphReplayNext = now + EPH_REPLAY_GAP_MS;
      if (m_EphReplayIndex >= m_EphReplaySpans.size()) {
        ESP_LOGI(LOG_TAG, "GPS ephemeris replay sent %u frames",
                 static_cast<unsigned>(m_EphReplaySpans.size()));
        m_EphReplaySpans.clear();
        m_EphReplay.clear();
        m_EphReplayIndex = 0;
      }
    }
    return;
  }

  if (m_AidMode.load() != 2) {
    return;
  }

  // close the capture window and persist whatever came back
  if (m_EphPollActive) {
    if (tickReached(now, m_EphPollDeadline)) {
      m_EphCapture = false;
      m_EphPollActive = false;
      storeEphemeris();
      m_EphPolled = true;
      m_EphLastPoll = now;
    }
    return;
  }

  // Same ownership rule: FixQuality() writes, so it is read under the snapshot
  // lock rather than off the parser.
  const status_t fixStatus = getStatusSnapshot();
  const bool stableFix = fixStatus.fix && (fixStatus.location_age < MAX_AGE_MS);
  const bool due = !m_EphPolled || ((now - m_EphLastPoll) >= EPH_CACHE_WRITE_MS);
  if (!stableFix || !due) {
    return;
  }
  // do not interleave a poll with an outstanding configuration command
  if (m_ConfigPending || m_ConfigInFlight.has_value() || !m_ConfigQueue.empty()) {
    return;
  }

  {
    const std::lock_guard<std::mutex> lock(m_EphMutex);
    m_EphCollector.clear();
  }

  const std::array<uint8_t, 3> ids = {Casic::Eph::EPH_ID, Casic::Eph::ION_ID, Casic::Eph::UTC_ID};
  for (const uint8_t id : ids) {
    const std::vector<uint8_t> payload = {
        Casic::Eph::CLASS,
        id,
        static_cast<uint8_t>(CFG_MSG_POLL_RATE & 0xff),
        static_cast<uint8_t>((CFG_MSG_POLL_RATE >> 8) & 0xff),
    };
    sendBinaryFrame(CFG_CLASS, CFG_MSG_ID, payload);
  }

  m_EphCapture = true;
  m_EphPollActive = true;
  m_EphPollDeadline = now + EPH_POLL_MS;
  ESP_LOGI(LOG_TAG, "GPS ephemeris poll sent");
}

void GPS::disable(void) {
  m_Enabled = false;
  wakeGPSService(m_Queue);
  const std::lock_guard<std::mutex> serviceLock(m_ServiceMutex);
  FURBLE_SIM_GPS_STATE("off");
  m_ConfigPending = false;
  m_ConfigQueue.clear();
  m_ConfigInFlight.reset();
  m_ConfigNavxAcked = false;
  m_NavxPayloadValid = false;
  m_RxBuffer.clear();
  m_LastLoggedFix = 0;
  m_LastLoggedStamp = 0;
  m_ProbePending.store(false);
  m_ReceiverState.store(static_cast<uint8_t>(receiver_state_t::UNKNOWN));
  m_DetectedBaud.store(0);
  m_EphCapture = false;
  m_EphPollActive = false;
  m_EphReplayArmed = false;
  m_MonHwPending = false;

  // the SD writer task owns the track file, never touch it from here
  SD::getInstance().request(SD::request_t::CLOSE);

  {
    // serialise against a cycle pass still running on the GPS task
    std::lock_guard<std::mutex> guard(m_CycleMutex);
    m_ConfigPending = false;
    m_CycleRequest = false;
    m_BurstActive = false;
    m_ProbeDeadline = 0;
    m_Degraded.reset();
    m_CycleState = cycle_state_t::DISABLED;
  }

  // power off, no mutex may be held across the PMIC access
  setRailPower(false);

  releasePowerLock();

  m_FixCache = {};
  m_HoldActive = false;
  m_HoldRemainingMs.store(0);
  m_Fix.store(static_cast<uint8_t>(Fix::NONE));
  m_Source.store(SOURCE_NONE);
  m_Satellites.store(0);
}

/** Refresh the setting from NVS. */
void GPS::reloadSetting(void) {
  m_AidMode.store(Settings::load<Settings::GPS_ASSIST>());
  reloadLogSettings();
  const uint8_t hold = Settings::load<Settings::GPS_HOLD>();
  m_HoldLimitMs.store(gpsHoldLimitMs(hold));
  m_Extrapolate.store(Settings::load<Settings::GPS_EXTRAP>());

  m_Enabled = Settings::load<Settings::GPS>();
  if (m_Enabled) {
    enable();
  } else {
    disable();
  }
}

/** Refresh the cached GPX logging settings from NVS. */
void GPS::reloadLogSettings(void) {
  m_LogEnabled = SD::getInstance().isSupported() && Settings::load<Settings::SD_GPX>();
  m_LogPeriodMs =
      static_cast<uint32_t>(Settings::clampGPXPeriod(Settings::load<Settings::GPX_PERIOD>()))
      * 1000UL;
}

/** Is GPS enabled? */
bool GPS::isEnabled(void) const {
  return m_Enabled;
}

/** Start timer event to service/update GPS. */
void GPS::startService(void) {
#if !defined(FURBLE_NO_DISPLAY)
  if (m_Timer != NULL) {
    return;
  }

  m_Timer = lv_timer_create(
      [](lv_timer_t *timer) {
        FURBLE_SIM_TIMER_FIRE("gps_service_timer");
        auto *gps = static_cast<GPS *>(lv_timer_get_user_data(timer));
        gps->update();
      },
      SERVICE_MS, this);
#endif
}

bool GPS::setExternalFix(const external_fix_t &fix) {
  if (fix.position_valid
      && ((!std::isfinite(fix.gps.latitude)) || (!std::isfinite(fix.gps.longitude))
          || (fix.gps.latitude < -90.0) || (fix.gps.latitude > 90.0) || (fix.gps.longitude < -180.0)
          || (fix.gps.longitude > 180.0))) {
    return false;
  }

  if (fix.time_valid) {
    // The public snapshot uses unsigned int fields, while the companion wire
    // representation narrows them. Reject values that cannot be represented
    // before converting so malformed direct callers cannot wrap into a valid
    // calendar tuple.
    if (fix.timesync.year > std::numeric_limits<uint16_t>::max()
        || fix.timesync.month > std::numeric_limits<uint8_t>::max()
        || fix.timesync.day > std::numeric_limits<uint8_t>::max()
        || fix.timesync.hour > std::numeric_limits<uint8_t>::max()
        || fix.timesync.minute > std::numeric_limits<uint8_t>::max()
        || fix.timesync.second > std::numeric_limits<uint8_t>::max()
        || fix.timesync.centisecond > std::numeric_limits<uint8_t>::max()) {
      return false;
    }
    uint64_t epoch_us = 0;
    if (!TimeKeeperPolicy::utcToEpochUs(
            static_cast<uint16_t>(fix.timesync.year), static_cast<uint8_t>(fix.timesync.month),
            static_cast<uint8_t>(fix.timesync.day), static_cast<uint8_t>(fix.timesync.hour),
            static_cast<uint8_t>(fix.timesync.minute), static_cast<uint8_t>(fix.timesync.second),
            static_cast<uint8_t>(fix.timesync.centisecond), epoch_us)) {
      return false;
    }
    (void)TimeKeeper::getInstance().updateUtc(
        TimeSource::COMPANION, static_cast<uint16_t>(fix.timesync.year),
        static_cast<uint8_t>(fix.timesync.month), static_cast<uint8_t>(fix.timesync.day),
        static_cast<uint8_t>(fix.timesync.hour), static_cast<uint8_t>(fix.timesync.minute),
        static_cast<uint8_t>(fix.timesync.second), static_cast<uint8_t>(fix.timesync.centisecond),
        5000);
  }

  const uint64_t now_ms = esp_timer_get_time() / 1000;
  const std::lock_guard<std::mutex> lock(m_ExternalMutex);
  m_ExternalFix = fix;
  m_ExternalFixReceivedMs = now_ms;
  m_HasExternalFix = true;
  return true;
}

void GPS::clearExternalFix(void) {
  const std::lock_guard<std::mutex> lock(m_ExternalMutex);
  m_ExternalFix = {};
  m_ExternalFixReceivedMs = 0;
  m_HasExternalFix = false;

  // Dropping the companion ends that fix source, so its cached position must
  // not keep being held. The cache belongs to the update() caller, so raise a
  // flag here and let update() clear it.
  m_ExternalDropped.store(true);
}

/** Send GPS data updates to the control task. */
void GPS::update(void) {
  const uint64_t now_ms = esp_timer_get_time() / 1000;
  const uint32_t now_tick = Platform::getInstance().tick();

  // A cached companion fix dies with its companion. A cached wired fix does not.
  if (m_ExternalDropped.exchange(false) && (m_FixCache.source == SOURCE_COMPANION)) {
    m_FixCache = {};
    m_HoldActive = false;
    m_HoldRemainingMs.store(0);
  }
  Camera::gps_t dgps = {};
  Camera::timesync_t timesync = {};
  source_t source = SOURCE_NONE;
  uint8_t satellites = 0;
  bool altitudeValid = false;
  const status_t status = getStatusSnapshot();
  // When the receiver produced this fix, not when update() got round to looking
  // at it. A fix is only declared stale after the freshness window, so anchoring
  // on now_tick would throw away up to that whole window: the held timestamp
  // would start a full window behind, and the projection would travel a full
  // window short.
  uint32_t fix_tick = now_tick;
  double course_deg = 0.0;
  double speed_mps = 0.0;
  bool course_valid = false;
  bool speed_valid = false;

  if (wiredFixIsFresh(status)) {
    source = SOURCE_UART;
    dgps = {
        status.latitude,
        status.longitude,
        status.altitude,
        status.satellites,
    };
    timesync = {
        status.year,   status.month,  status.day,         status.hour,
        status.minute, status.second, status.centisecond,
    };
    if (status.date_valid && status.time_valid) {
      (void)TimeKeeper::getInstance().updateUtc(
          TimeSource::GPS, static_cast<uint16_t>(timesync.year),
          static_cast<uint8_t>(timesync.month), static_cast<uint8_t>(timesync.day),
          static_cast<uint8_t>(timesync.hour), static_cast<uint8_t>(timesync.minute),
          static_cast<uint8_t>(timesync.second), static_cast<uint8_t>(timesync.centisecond), 1000);
    }
    updateAidCache(dgps, timesync);
    satellites = static_cast<uint8_t>(std::min<uint32_t>(status.satellites, 255u));
    altitudeValid = status.altitude_valid;
    fix_tick = now_tick - status.time_age;
    course_valid = m_GPS.course.isValid() && (m_GPS.course.age() < MAX_AGE_MS);
    speed_valid = m_GPS.speed.isValid() && (m_GPS.speed.age() < MAX_AGE_MS);
    if (course_valid) {
      course_deg = m_GPS.course.deg();
    }
    if (speed_valid) {
      speed_mps = m_GPS.speed.mps();
    }
  } else {
    external_fix_t external = {};
    uint64_t received_ms = 0;
    bool has_external = false;
    {
      const std::lock_guard<std::mutex> lock(m_ExternalMutex);
      external = m_ExternalFix;
      received_ms = m_ExternalFixReceivedMs;
      has_external = m_HasExternalFix;
    }

    const uint64_t elapsed_ms = now_ms - received_ms;
    if (has_external && external.position_valid && external.time_valid && (elapsed_ms < MAX_AGE_MS)
        && ((static_cast<uint64_t>(external.age_ms) + elapsed_ms) < MAX_AGE_MS)) {
      source = SOURCE_COMPANION;
      dgps = external.gps;
      if (!external.altitude_valid) {
        dgps.altitude = 0.0;
      }
      timesync = external.timesync;
      satellites = static_cast<uint8_t>(std::min<uint32_t>(external.gps.satellites, 255u));
      altitudeValid = external.altitude_valid;
      // The companion reports how old its fix already was when it sent it, on
      // top of how long ago it arrived here.
      fix_tick = now_tick - static_cast<uint32_t>(external.age_ms + elapsed_ms);
    }
  }

  Fix fix = Fix::NONE;
  if (source != SOURCE_NONE) {
    m_FixCache = {
        dgps,
        timesync,
        fix_tick,
        course_deg,
        speed_mps,
        source,
        course_valid && std::isfinite(course_deg),
        speed_valid && std::isfinite(speed_mps),
        true,
    };
    m_HoldActive = false;
    m_HoldRemainingMs.store(0);
    fix = Fix::LIVE;
  } else if (m_FixCache.valid) {
    if (!m_HoldActive) {
      m_HoldStartTick = now_tick;
      m_HoldActive = true;
    }

    const uint32_t hold_limit_ms = m_HoldLimitMs.load();
    const uint32_t hold_elapsed_ms = now_tick - m_HoldStartTick;
    Camera::timesync_t held_timesync = {};
    if (gpsHoldInBound(hold_elapsed_ms, hold_limit_ms)
        && gpsAdvanceUtc(m_FixCache.timesync, now_tick - m_FixCache.tick, held_timesync)) {
      dgps = m_FixCache.gps;
      timesync = held_timesync;
      source = m_FixCache.source;
      satellites =
          static_cast<uint8_t>(std::min<uint32_t>(static_cast<uint32_t>(dgps.satellites), 255u));

      // No motion source exists yet, so a stationary result is never assumed.
      // The reported speed is the only evidence of motion available here.
      if (gpsExtrapolateAllowed(m_Extrapolate.load(), m_FixCache.course_valid,
                                m_FixCache.speed_valid, m_FixCache.speed_mps)) {
        gpsExtrapolate(m_FixCache.gps.latitude, m_FixCache.gps.longitude, m_FixCache.course_deg,
                       m_FixCache.speed_mps,
                       gpsExtrapolateElapsedMs(now_tick - m_FixCache.tick, hold_elapsed_ms),
                       dgps.latitude, dgps.longitude);
      }

      m_HoldRemainingMs.store(gpsHoldRemainingMs(hold_elapsed_ms, hold_limit_ms));
      fix = Fix::HELD;
    }
  }

  if (fix != Fix::HELD) {
    m_HoldRemainingMs.store(0);
  }

  m_Source.store(static_cast<uint8_t>(source));
  m_Satellites.store(satellites);
  m_Fix.store(static_cast<uint8_t>(fix));

  if (fix != Fix::NONE) {
    Control::getInstance().updateGPS(dgps, timesync);

    const uint32_t fixSequence = m_FixSequence.load();
    if ((fixSequence != 0) && (fixSequence != m_PushedSequence.load())) {
      m_PushedSequence = fixSequence;
      if (dutyCycleEnabled()) {
        m_CycleRequest = true;
      }
    }

    // both fix sources are logged, the point is built from the normalized
    // dgps and timesync values and only queued, the SD writer task does the I/O.
    // A held or extrapolated fix is deliberately not logged: the camera geotag
    // wants the last known position, but a track log would record an hour of
    // synthetic points that no reader could tell from measured ones.
    if (m_LogEnabled.load() && (fix == Fix::LIVE)) {
      const uint32_t now = Platform::getInstance().tick();
      if ((m_LastLoggedFix == 0) || ((now - m_LastLoggedFix) >= m_LogPeriodMs.load())) {
        // a stale fix repeats its timestamp under duty cycling, log it once
        const uint64_t stamp =
            (((((static_cast<uint64_t>(timesync.year) * 16 + timesync.month) * 32 + timesync.day)
                   * 32
               + timesync.hour)
                  * 64
              + timesync.minute)
             * 64)
            + timesync.second;
        if (stamp != m_LastLoggedStamp) {
          const GPX::point_t point = {
              dgps.latitude,
              dgps.longitude,
              dgps.altitude,
              altitudeValid,
              dgps.satellites,
              static_cast<uint16_t>(timesync.year),
              static_cast<uint8_t>(timesync.month),
              static_cast<uint8_t>(timesync.day),
              static_cast<uint8_t>(timesync.hour),
              static_cast<uint8_t>(timesync.minute),
              static_cast<uint8_t>(timesync.second),
          };

          if (SD::getInstance().logPoint(point)) {
            m_LastLoggedFix = now;
            m_LastLoggedStamp = stamp;
            m_LogDropWarned = false;
          } else if (!m_LogDropWarned) {
            // warn once per run of drops, a full queue repeats every period
            m_LogDropWarned = true;
            ESP_LOGW(LOG_TAG, "GPX point dropped, SD writer queue is full.");
          }
        }
      }
    }
  }

#if !defined(FURBLE_NO_DISPLAY)
  // the degraded, self recovering cycle keeps retrying with the lock released.
  // Surface it on screen so a degraded GPS is not console only: a subtle warning
  // tint on the status icon, and, when there is no fix to show, the searching
  // glyph so a retrying GPS never looks switched off.
  const bool degraded = gpsIndicatorDegraded(m_Enabled.load(), isDegraded());

  // setting the source invalidates the image and forces a decode, only do it
  // when the icon actually changes
  const lv_image_dsc_t *symbol = &icon_location_disabled;
  if (fix == Fix::HELD || (fix == Fix::LIVE && source == SOURCE_COMPANION)) {
    symbol = &icon_location_searching;
  } else if (fix == Fix::LIVE) {
    symbol = &icon_my_location;
  } else if (degraded) {
    symbol = &icon_location_searching;
  }
  if ((m_Icon != NULL) && (m_IconSymbol != symbol)) {
    m_IconSymbol = symbol;
    lv_image_set_src(m_Icon, symbol);
  }

  // the tint is a local recolor override, guarded so the poll only touches the
  // style on a change. Clearing removes the local property so the theme recolor
  // returns, so recovery or a GPS off restores the normal icon.
  if ((m_Icon != NULL) && (m_IconDegraded != degraded)) {
    m_IconDegraded = degraded;
    if (degraded) {
      lv_obj_set_style_image_recolor(m_Icon, lv_color_hex(0xFFB300), 0);
      lv_obj_set_style_image_recolor_opa(m_Icon, LV_OPA_COVER, 0);
    } else {
      lv_obj_remove_local_style_prop(m_Icon, LV_STYLE_IMAGE_RECOLOR, 0);
      lv_obj_remove_local_style_prop(m_Icon, LV_STYLE_IMAGE_RECOLOR_OPA, 0);
    }
  }
#endif
}

bool GPS::wiredFixIsFresh(const status_t &status) const {
  return m_Enabled && status.position_valid && (status.location_age < MAX_AGE_MS)
         && status.date_valid && (status.date_age < MAX_AGE_MS) && status.time_valid
         && (status.time_age < MAX_AGE_MS) && status.fix;
}

GPS::source_t GPS::getSource(void) const {
  return static_cast<source_t>(m_Source.load());
}

uint8_t GPS::getSatellites(void) const {
  return m_Satellites.load();
}

GPS::Fix GPS::getFix(void) const {
  return static_cast<Fix>(m_Fix.load());
}

uint32_t GPS::getHoldRemainingMs(void) const {
  return m_HoldRemainingMs.load();
}

uint32_t GPS::getHoldLimitMs(void) const {
  return m_HoldLimitMs.load();
}

void GPS::completeNavxQuery(void) {
  if (!m_ConfigInFlight.has_value() || !m_ConfigInFlight->navx_query || !m_NavxPayloadValid
      || !m_ConfigNavxAcked) {
    return;
  }

  const size_t queryStatus = m_ConfigInFlight->status_index;
  std::vector<uint8_t> write(m_NavxPayload.begin(), m_NavxPayload.end());
  m_ConfigInFlight.reset();
  m_ConfigAttempts = 0;
  m_ConfigNavxAcked = false;
  m_NavxPayloadValid = false;
  {
    const std::lock_guard<std::mutex> lock(m_ConfigMutex);
    m_ConfigStatus[queryStatus].state = CONFIG_ACKED;
  }

  uint32_t mask = 0;
  std::string fallback;
  if (m_NavxConstelSet) {
    mask |= NAVX_MASK_NAV_SYSTEM;
    write[NAVX_NAV_SYSTEM_OFFSET] = m_NavxConstelVal;
    fallback = "PCAS04," + std::to_string(m_NavxConstelVal);
  }
  if (m_NavxDyModelSet) {
    mask |= NAVX_MASK_DY_MODEL;
    write[NAVX_DY_MODEL_OFFSET] = m_NavxDyModelVal;
    if (!fallback.empty()) {
      fallback += "\n";
    }
    fallback += "PCAS11," + std::to_string(m_NavxDyModelVal);
  }
  writeU32(write.data(), mask);
  enqueueConfig(CFG_CLASS, CFG_NAVX_ID, write, fallback, false, true);
}

void GPS::handleNavxResponse(const uint8_t *payload, size_t length) {
  if (!m_ConfigInFlight.has_value() || !m_ConfigInFlight->navx_query
      || (m_ConfigInFlight->class_id != CFG_CLASS) || (m_ConfigInFlight->message_id != CFG_NAVX_ID)
      || (length != NAVX_PAYLOAD_SIZE)) {
    return;
  }

  std::copy(payload, payload + length, m_NavxPayload.begin());
  m_NavxPayloadValid = true;
  completeNavxQuery();
}

void GPS::serviceBinary(const uint8_t *frame, size_t length) {
  if (length < BINARY_FRAME_OVERHEAD) {
    return;
  }

  const uint16_t payloadLength = readU16(frame + 2);
  if (length != (BINARY_FRAME_OVERHEAD + payloadLength)) {
    return;
  }

  const uint8_t class_id = frame[4];
  const uint8_t message_id = frame[5];
  const uint8_t *payload = frame + BINARY_HEADER_SIZE;

  // tier 2: store polled ephemeris frames verbatim for later replay
  if (m_EphCapture.load() && Casic::Eph::isEphemerisMessage(class_id, message_id)) {
    const std::lock_guard<std::mutex> lock(m_EphMutex);
    m_EphCollector.feed(frame, length);
    return;
  }

  // MON-HW interference snapshot, hardware-tuning-pending
  if (m_MonHwPending.load() && (class_id == MON_CLASS) && (message_id == MON_HW_ID)) {
    m_MonHwPending.store(false);
    const std::lock_guard<std::mutex> lock(m_MonHwMutex);
    m_MonHw = Casic::parseMonHw(payload, payloadLength);
    m_MonHwHave = true;
    m_MonHwRawLen = std::min(static_cast<size_t>(payloadLength), m_MonHwRaw.size());
    std::copy(payload, payload + m_MonHwRawLen, m_MonHwRaw.begin());
    return;
  }

  if ((class_id == ACK_CLASS) && ((message_id == ACK_ACK_ID) || (message_id == ACK_NACK_ID))
      && (payloadLength >= 2)) {
    if (m_ConfigInFlight.has_value() && (payload[0] == m_ConfigInFlight->class_id)
        && (payload[1] == m_ConfigInFlight->message_id)) {
      if ((message_id == ACK_ACK_ID) && m_ConfigInFlight->navx_query) {
        m_ConfigNavxAcked = true;
        {
          const std::lock_guard<std::mutex> lock(m_ConfigMutex);
          auto &status = m_ConfigStatus[m_ConfigInFlight->status_index];
          status.state = CONFIG_ACKED;
          status.attempts = m_ConfigAttempts;
        }
        completeNavxQuery();
      } else {
        finishConfigCommand(message_id == ACK_ACK_ID ? CONFIG_ACKED : CONFIG_NACKED);
      }
    }
    return;
  }

  if ((class_id == CFG_CLASS) && (message_id == CFG_NAVX_ID)
      && (payloadLength == NAVX_PAYLOAD_SIZE)) {
    handleNavxResponse(payload, payloadLength);
  }
}

void GPS::processNmea(uint8_t *data, size_t length) {
  if (length == 0) {
    return;
  }

  Console::gpsRaw(reinterpret_cast<const char *>(data), length);
  {
    const std::lock_guard<std::mutex> lock(m_GPSMutex);
    m_GPS.encode(reinterpret_cast<char *>(data), length);
    // The parser raises this only for a complete sentence whose checksum
    // passed, so it is both fragment safe and checksum safe where scanning the
    // raw bytes for "RMC" was neither. Read and consume it inside the same lock
    // the encode holds: the flag is cleared by any value accessor, so a status
    // snapshot taken between the two would eat it. Nothing else in furble reads
    // isUpdated(), so this owns the flag.
    if (m_GPS.date.isUpdated()) {
      m_DateUpdates++;
      (void)m_GPS.date.value();
    }
  }
  captureSentences(reinterpret_cast<const char *>(data), length);
}

void GPS::processSerial(const uint8_t *data, size_t length) {
  m_RxBuffer.insert(m_RxBuffer.end(), data, data + length);

  while (!m_RxBuffer.empty()) {
    size_t header = m_RxBuffer.size();
    for (size_t i = 0; i + 1 < m_RxBuffer.size(); i++) {
      if ((m_RxBuffer[i] == BINARY_SYNC_0) && (m_RxBuffer[i + 1] == BINARY_SYNC_1)) {
        header = i;
        break;
      }
    }

    if (header == m_RxBuffer.size()) {
      const size_t keep = (m_RxBuffer.back() == BINARY_SYNC_0) ? 1 : 0;
      const size_t nmea = m_RxBuffer.size() - keep;
      if (nmea > 0) {
        processNmea(m_RxBuffer.data(), nmea);
        m_RxBuffer.erase(m_RxBuffer.begin(), m_RxBuffer.begin() + nmea);
      }
      break;
    }

    if (header > 0) {
      processNmea(m_RxBuffer.data(), header);
      m_RxBuffer.erase(m_RxBuffer.begin(), m_RxBuffer.begin() + header);
      continue;
    }

    if (m_RxBuffer.size() < BINARY_HEADER_SIZE) {
      break;
    }

    const uint16_t payloadLength = readU16(m_RxBuffer.data() + 2);
    if ((payloadLength > MAX_BINARY_PAYLOAD) || ((payloadLength % 4) != 0)) {
      m_RxBuffer.erase(m_RxBuffer.begin());
      continue;
    }

    const size_t frameLength = BINARY_FRAME_OVERHEAD + payloadLength;
    if (m_RxBuffer.size() < frameLength) {
      break;
    }

    const uint8_t class_id = m_RxBuffer[4];
    const uint8_t message_id = m_RxBuffer[5];
    const uint32_t expected =
        casicChecksum(class_id, message_id, payloadLength, m_RxBuffer.data() + BINARY_HEADER_SIZE);
    const uint32_t received = readU32(m_RxBuffer.data() + BINARY_HEADER_SIZE + payloadLength);
    if (expected != received) {
      m_RxBuffer.erase(m_RxBuffer.begin());
      continue;
    }

    Console::gpsBinary(m_RxBuffer.data(), frameLength);
    serviceBinary(m_RxBuffer.data(), frameLength);
    m_RxBuffer.erase(m_RxBuffer.begin(), m_RxBuffer.begin() + frameLength);
  }
}

/** Read and decode the GPS data from serial port. */
void GPS::serviceSerial(void) {
  std::array<uint8_t, BUFFER_SIZE> buffer;

  std::unique_lock<std::mutex> lock(m_CycleMutex, std::try_to_lock);
  if (!lock.owns_lock() || !m_Enabled) {
    // enable() or disable() owns the cycle state, drop this pass
    return;
  }

  int bytes = uart_read_bytes(m_UART, buffer.data(), buffer.size(), 1);
  if (bytes > 0) {
    beginBurst(Platform::getInstance().tick());

    // processSerial demultiplexes NMEA and CASIC frames. NMEA bytes route
    // through processNmea, which does the console echo, TinyGPSPlus encode and
    // sentence capture. Burst and fix sequence tracking stay here so they run
    // once per read pass.
    processSerial(buffer.data(), bytes);

    const status_t status = getStatusSnapshot();
    if (status.fix && (status.location_age < m_LastLocationAge)) {
      m_FixSequence = m_BurstSequence.load();
    }
    m_LastLocationAge = status.location_age;

    m_LastSentence = Platform::getInstance().tick();
  }
}

/** Split the raw stream into sentences for the debug and satellite pages. */
void GPS::captureSentences(const char *data, size_t length) {
  const bool ring = m_Capture.load();
  const bool sats = m_SatCapture.load();
  if (!ring && !sats) {
    return;
  }

  std::lock_guard<std::mutex> lock(m_CaptureMutex);

  for (size_t i = 0; i < length; i++) {
    const char c = data[i];

    if ((c == '\r') || (c == '\n')) {
      if (!m_Partial.empty()) {
        if (ring) {
          m_Sentences[m_SentenceNext] = m_Partial;
          m_SentenceNext = (m_SentenceNext + 1) % m_Sentences.size();
        }
        if (sats) {
          feedSatellite(m_Partial);
        }
        m_Partial.clear();
      }
    } else if (m_Partial.size() < SENTENCE_LEN) {
      m_Partial += c;
    }
  }
}

void GPS::feedSatellite(const std::string &sentence) {
  std::lock_guard<std::mutex> lock(m_SatMutex);
  m_SatTable.feed(sentence);
}

void GPS::setSatelliteCapture(bool capture) {
  {
    std::lock_guard<std::mutex> lock(m_SatMutex);
    if (capture) {
      m_SatTable.clear();
    }
  }
  m_SatCapture.store(capture);

  if (!m_Enabled) {
    return;
  }

  if (capture) {
    // temporarily add GSA and GSV so the page has data to render
    sendCommand("PCAS03,1,0,1,1,1,0,0,0");
  } else if (Settings::load<Settings::GPS_NMEA>()) {
    // restore the user's pruned set
    sendCommand("PCAS03,1,0,0,0,1,0,0,0");
  } else {
    // restore the default full set
    sendCommand("PCAS03,1,1,1,1,1,1,1,1");
  }
}

GPS::satellite_report_t GPS::getSatelliteReport(void) {
  std::lock_guard<std::mutex> lock(m_SatMutex);
  satellite_report_t report;
  report.satellites = m_SatTable.satellites();
  report.dop = m_SatTable.dop();
  report.in_view = m_SatTable.inView();
  report.used = m_SatTable.used();
  return report;
}

void GPS::pollMonHw(void) {
  {
    std::lock_guard<std::mutex> lock(m_MonHwMutex);
    m_MonHwHave = false;
  }
  m_MonHwPending.store(true);

  // CFG-MSG poll of MON-HW at rate 0xFFFF asks for one copy
  const std::vector<uint8_t> payload = {
      MON_CLASS,
      MON_HW_ID,
      static_cast<uint8_t>(CFG_MSG_POLL_RATE & 0xff),
      static_cast<uint8_t>((CFG_MSG_POLL_RATE >> 8) & 0xff),
  };
  sendBinaryFrame(CFG_CLASS, CFG_MSG_ID, payload);
}

GPS::monhw_report_t GPS::getMonHw(void) {
  std::lock_guard<std::mutex> lock(m_MonHwMutex);
  monhw_report_t report;
  report.hw = m_MonHw;
  report.have = m_MonHwHave;
  report.raw = m_MonHwRaw;
  report.raw_length = m_MonHwRawLen;
  return report;
}

GPS::receiver_state_t GPS::getReceiverState(void) const {
  return static_cast<receiver_state_t>(m_ReceiverState.load());
}

uint32_t GPS::getDetectedBaud(void) const {
  return m_DetectedBaud.load();
}

const char *GPS::receiverStateName(receiver_state_t state) {
  switch (state) {
    case receiver_state_t::UNKNOWN:
      return "unknown";
    case receiver_state_t::DETECTING:
      return "detecting";
    case receiver_state_t::PRESENT:
      return "present";
    case receiver_state_t::ABSENT:
      return "absent";
  }
  return "unknown";
}

/** Start or stop raw NMEA sentence capture. */
void GPS::setCapture(bool capture) {
  std::lock_guard<std::mutex> lock(m_CaptureMutex);

  m_Capture = capture;
  if (!capture) {
    for (auto &sentence : m_Sentences) {
      sentence.clear();
    }
    m_SentenceNext = 0;
    m_Partial.clear();
  }
}

/** Get the captured raw NMEA sentences, oldest first. */
std::vector<std::string> GPS::getSentences(void) {
  std::vector<std::string> sentences;

  std::lock_guard<std::mutex> lock(m_CaptureMutex);

  for (size_t i = 0; i < m_Sentences.size(); i++) {
    const auto &sentence = m_Sentences[(m_SentenceNext + i) % m_Sentences.size()];
    if (!sentence.empty()) {
      sentences.push_back(sentence);
    }
  }

  return sentences;
}

GPS::status_t GPS::getStatusSnapshot(void) const {
  const std::lock_guard<std::mutex> lock(m_GPSMutex);
  TinyGPSPlus &gps = m_GPS;
  const TinyGPSLocation::Quality quality = gps.location.FixQuality();

  status_t status = {};
  status.position_valid = gps.location.isValid();
  status.date_valid = gps.date.isValid();
  status.time_valid = gps.time.isValid();
  status.altitude_valid = gps.altitude.isValid();
  status.fix = status.position_valid && (quality != TinyGPSLocation::Quality::Invalid);
  status.satellites = gps.satellites.value();
  status.latitude = gps.location.lat();
  status.longitude = gps.location.lng();
  status.altitude = gps.altitude.meters();
  status.speed_kmph = gps.speed.kmph();
  status.hdop = gps.hdop.hdop();
  status.location_age = gps.location.age();
  status.date_age = gps.date.age();
  status.time_age = gps.time.age();
  status.year = static_cast<uint16_t>(gps.date.year());
  status.month = gps.date.month();
  status.day = gps.date.day();
  status.hour = gps.time.hour();
  status.minute = gps.time.minute();
  status.second = gps.time.second();
  status.centisecond = gps.time.centisecond();
  status.chars_processed = gps.charsProcessed();
  status.sentences_passed = gps.passedChecksum();
  status.sentences_failed = gps.failedChecksum();
  return status;
}

}  // namespace Furble
