#include <algorithm>
#include <limits>

#include <M5Unified.h>
#include <TinyGPS++.h>
#include <esp_timer.h>
#include <lvgl.h>

#include <algorithm>
#include <cmath>

#include "icons.h"

#include "FurbleConsole.h"
#include "FurbleControl.h"
#include "FurbleGPS.h"
#include "FurblePlatform.h"
#include "FurblePower.h"
#include "FurbleSettings.h"

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
#if defined(FURBLE_M5STICKS3)
        // XTAL keeps the baud stable while DFS scales the APB clock
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
  m_Icon = icon;
}

void GPS::reset(void) {
  uart_flush(m_UART);
  xQueueReset(m_Queue);
}

void GPS::task(void) {
  while (true) {
    if (!m_Enabled) {
      releasePowerLock();
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }

    serviceCycle();
    serviceConfig();

    uart_event_t event;
    if (xQueueReceive(m_Queue, &event, cycleWait(Platform::getInstance().tick()))) {
      switch (event.type) {
        case UART_DATA:
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
          serviceSerial();
          break;
        default:
          ESP_LOGW(LOG_TAG, "unknown uart event type: %d", event.type);
          break;
      }
    }

    serviceConfig();
    serviceCycle();
  }
}

void GPS::enable(void) {
  const uint32_t baud = Settings::load<Settings::GPS_BAUD>();
  const uint8_t policy = powerPolicy();
  const uint8_t duty = dutySeconds();

  // park the GPS task first, m_Enabled gates every cycle entry point
  m_Enabled = false;

  uart_set_baudrate(m_UART, baud);
  reset();

  // power on, no mutex may be held across the PMIC access
  setRailPower(true);

  {
    // serialise against a cycle pass still running on the GPS task
    std::lock_guard<std::mutex> guard(m_CycleMutex);

    m_PowerPolicy = policy;
    m_DutySeconds = duty;
    m_CycleState = cycle_state_t::ACQUIRING;
    m_BurstActive = false;
    m_DutyWake = false;
    m_HavePrediction = false;
    m_BurstStart = 0;
    m_LastSentence = 0;
    m_NextBurst = 0;
    m_WakeDeadline = 0;
    m_ExpectedInterval = gpsRateInterval();
    m_Window = WINDOW_DEFAULT_MS;
    m_BurstFailed = 0;
    m_MeasureDeadline = 0;
    m_ResyncDeadline = 0;
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

    // the receiver is not ready for commands yet, ask the GPS task to
    // configure it once sentences arrive
    m_ConfigChars = m_GPS.charsProcessed();
    m_ConfigStart = Platform::getInstance().tick();
    m_ConfigPending = true;
  }

  m_Enabled = true;
  acquirePowerLock();
}

void GPS::acquirePowerLock(void) {
#if defined(FURBLE_M5STICKS3)
  if (!m_Enabled) {
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

    case cycle_state_t::DISABLED:
    case cycle_state_t::ACQUIRING:
    case cycle_state_t::PERMANENT_LOCK:
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
        enterPermanentLock();
      }
      break;

    case cycle_state_t::ACQUIRING:
      // re-assert the lock, a release raced by enable() must not leave the
      // receiver deaf under light sleep
      acquirePowerLock();
      break;

    case cycle_state_t::MEASURING:
      if (!m_BurstActive && (m_MeasureDeadline != 0) && tickReached(now, m_MeasureDeadline)) {
        finishMeasurement();
      }
      break;

    case cycle_state_t::RESYNC:
      if (!m_BurstActive && tickReached(now, m_ResyncDeadline)) {
        if (m_ExpectedInterval == 0) {
          enterPermanentLock();
        } else {
          m_NextBurst = m_LastBurstStart + m_ExpectedInterval;
          if (tickReached(now, m_NextBurst)) {
            m_NextBurst = now + m_ExpectedInterval;
          }
          m_HavePrediction = true;
          m_CycleState = cycle_state_t::WAITING;
          releasePowerLock();
        }
      }
      break;

    case cycle_state_t::DISABLED:
    case cycle_state_t::PERMANENT_LOCK:
      break;
  }
}

void GPS::beginBurst(uint32_t now) {
  if (m_BurstActive) {
    m_LastSentence = now;
    return;
  }

  acquirePowerLock();

  const cycle_state_t previous = m_CycleState;
  if (m_HavePrediction && (previous != cycle_state_t::RESYNC)
      && (tickDistance(now, m_NextBurst) > m_Window)) {
    beginResync(now);
  }

  m_BurstActive = true;
  m_BurstStart = now;
  m_LastSentence = now;
  m_BurstFailed = m_GPS.failedChecksum();
  m_BurstSequence++;

  if (m_LastBurstStart != 0) {
    if (m_PeriodCount < m_PeriodSamples.size()) {
      m_PeriodSamples[m_PeriodCount++] = now - m_LastBurstStart;
    }
  }
  m_LastBurstStart = now;

  if (m_CycleState == cycle_state_t::ACQUIRING) {
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

  const uint32_t failed = m_GPS.failedChecksum() - m_BurstFailed;
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

  if (m_CycleState == cycle_state_t::PERMANENT_LOCK) {
    return;
  }

  if (m_DutyWake) {
    m_DutyWake = false;

    if ((m_PowerPolicy == POWER_RAIL_CYCLE)
        && (m_PushedSequence.load() != m_BurstSequence.load())) {
      enterPermanentLock();
      return;
    }

    if (m_PowerPolicy == POWER_STANDBY) {
      m_ExpectedInterval = gpsRateInterval();
      if (m_ExpectedInterval == 0) {
        enterPermanentLock();
        return;
      }
    }
  }

  if (m_ConsecutiveBadBursts >= BAD_BURSTS_TO_RESYNC) {
    beginResync(now);
    return;
  }

  if (m_ExpectedInterval == 0) {
    enterPermanentLock();
    return;
  }

  m_NextBurst = m_BurstStart + m_ExpectedInterval;
  m_HavePrediction = true;
  m_CycleState = cycle_state_t::WAITING;
  m_WakeDeadline = 0;
  releasePowerLock();
}

void GPS::beginWindow(uint32_t now) {
  const cycle_state_t previous = m_CycleState;
  acquirePowerLock();

  if (previous == cycle_state_t::RAIL_OFF) {
    setRailPower(true);
    reset();
    m_ConfigChars = m_GPS.charsProcessed();
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
  setRailPower(false);
  releasePowerLock();
}

void GPS::beginResync(uint32_t now) {
  m_ExpectedInterval = (m_ExpectedInterval > 0) ? m_ExpectedInterval : 1000;
  m_ResyncDeadline = now + m_ExpectedInterval;
  m_HavePrediction = false;
  m_DutyWake = false;
  m_CycleState = cycle_state_t::RESYNC;
  acquirePowerLock();
}

void GPS::finishMeasurement(void) {
  if (m_PeriodCount < 2) {
    enterPermanentLock();
    return;
  }

  const size_t count = std::min(m_PeriodCount, m_PeriodSamples.size());
  decltype(m_PeriodSamples) periods = {};
  std::copy_n(m_PeriodSamples.begin(), count, periods.begin());
  std::sort(periods.begin(), periods.begin() + count);

  const uint32_t median = periods[count / 2];
  const uint32_t tolerance = std::max<uint32_t>(50, median / 10);
  if ((periods[count - 1] - periods[0]) > tolerance) {
    enterPermanentLock();
    return;
  }

  m_ExpectedInterval = median;
  m_Window = std::min(m_Window, std::max(WINDOW_MIN_MS, median / 2));
  m_NextBurst = m_LastBurstStart + m_ExpectedInterval;
  m_HavePrediction = true;
  m_MeasureDeadline = 0;
  m_CycleState = cycle_state_t::WAITING;
  releasePowerLock();
}

void GPS::enterPermanentLock(void) {
  m_HavePrediction = false;
  m_WakeDeadline = 0;
  m_CycleState = cycle_state_t::PERMANENT_LOCK;
  acquirePowerLock();
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

  uart_write_bytes(m_UART, command.data(), command.size());
  uart_wait_tx_done(m_UART, pdMS_TO_TICKS(TX_MS));
}

/** Restart the receiver, 0 hot, 1 warm, 2 cold. */
void GPS::restart(uint8_t mode) {
  if (!m_Enabled || (mode > 2)) {
    return;
  }

  sendCommand("PCAS10," + std::to_string(mode));
}

/**
 * Send the $PCAS configuration commands.
 *
 * Every setting defaults to 'do not send', so a device which has never been
 * configured leaves the receiver at its own defaults.
 */
void GPS::configure(void) {
  const uint8_t rate = Settings::load<Settings::GPS_RATE>();
  const bool prune = Settings::load<Settings::GPS_NMEA>();
  const uint8_t constellation = Settings::load<Settings::GPS_CONSTEL>();

  // prune first, the receiver cannot sustain the fast rates while it still
  // emits every sentence
  if (prune) {
    // GGA for altitude, satellites and fix quality, RMC for date, time and
    // position, nothing else is used
    sendCommand("PCAS03,1,0,0,0,1,0,0,0");
  }

  if ((rate > 0) && (rate < RATE_MS.size())) {
    sendCommand("PCAS02," + std::to_string(RATE_MS[rate]));
  }

  if ((constellation > 0) && (constellation <= CONSTELLATION_MAX)) {
    sendCommand("PCAS04," + std::to_string(constellation));
  }
}

/** Configure the receiver once it is awake, or give up and try anyway. */
void GPS::serviceConfig(void) {
  if (!m_Enabled || !m_ConfigPending) {
    return;
  }

  const bool awake = m_GPS.charsProcessed() > m_ConfigChars;
  const bool expired = (Platform::getInstance().tick() - m_ConfigStart) > SETTLE_MS;
  if (!awake && !expired) {
    return;
  }

  m_ConfigPending = false;
  configure();
}

void GPS::disable(void) {
  m_Enabled = false;

  {
    // serialise against a cycle pass still running on the GPS task
    std::lock_guard<std::mutex> guard(m_CycleMutex);
    m_ConfigPending = false;
    m_CycleRequest = false;
    m_BurstActive = false;
    m_CycleState = cycle_state_t::DISABLED;
  }

  // power off, no mutex may be held across the PMIC access
  setRailPower(false);

  releasePowerLock();
}

/** Refresh the setting from NVS. */
void GPS::reloadSetting(void) {
  if (Settings::load<Settings::GPS>()) {
    enable();
  } else {
    disable();
  }
}

/** Is GPS enabled? */
bool GPS::isEnabled(void) const {
  return m_Enabled;
}

/** Start timer event to service/update GPS. */
void GPS::startService(void) {
  if (m_Timer != NULL) {
    return;
  }

  m_Timer = lv_timer_create(
      [](lv_timer_t *timer) {
        auto *gps = static_cast<GPS *>(lv_timer_get_user_data(timer));
        gps->update();
      },
      SERVICE_MS, this);
}

bool GPS::setExternalFix(const external_fix_t &fix) {
  if (fix.position_valid
      && ((!std::isfinite(fix.gps.latitude)) || (!std::isfinite(fix.gps.longitude))
          || (fix.gps.latitude < -90.0) || (fix.gps.latitude > 90.0) || (fix.gps.longitude < -180.0)
          || (fix.gps.longitude > 180.0))) {
    return false;
  }

  if (fix.time_valid
      && ((fix.timesync.month < 1) || (fix.timesync.month > 12) || (fix.timesync.day < 1)
          || (fix.timesync.day > 31) || (fix.timesync.hour > 23) || (fix.timesync.minute > 59)
          || (fix.timesync.second > 60) || (fix.timesync.centisecond > 99))) {
    return false;
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
}

/** Send GPS data updates to the control task. */
void GPS::update(void) {
  const uint64_t now_ms = esp_timer_get_time() / 1000;
  Camera::gps_t dgps = {};
  Camera::timesync_t timesync = {};
  source_t source = SOURCE_NONE;
  uint8_t satellites = 0;

  if (wiredFixIsFresh()) {
    source = SOURCE_UART;
    dgps = {
        m_GPS.location.lat(),
        m_GPS.location.lng(),
        m_GPS.altitude.meters(),
        m_GPS.satellites.value(),
    };
    timesync = {
        m_GPS.date.year(),   m_GPS.date.month(),  m_GPS.date.day(),         m_GPS.time.hour(),
        m_GPS.time.minute(), m_GPS.time.second(), m_GPS.time.centisecond(),
    };
    satellites = static_cast<uint8_t>(
        std::min<uint32_t>(static_cast<uint32_t>(m_GPS.satellites.value()), 255u));
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
    }
  }

  m_Source.store(static_cast<uint8_t>(source));
  m_Satellites.store(satellites);
  m_HasFix = (source != SOURCE_NONE);

  if (m_HasFix) {
    Control::getInstance().updateGPS(dgps, timesync);

    const uint32_t fixSequence = m_FixSequence.load();
    if ((fixSequence != 0) && (fixSequence != m_PushedSequence.load())) {
      m_PushedSequence = fixSequence;
      if (dutyCycleEnabled()) {
        m_CycleRequest = true;
      }
    }
  }

  // setting the source invalidates the image and forces a decode, only do it
  // when the icon actually changes
  const lv_image_dsc_t *symbol = &icon_location_disabled;
  if (source == SOURCE_UART) {
    symbol = &icon_my_location;
  } else if (source == SOURCE_COMPANION) {
    symbol = &icon_location_searching;
  }
  if ((m_Icon != NULL) && (m_IconSymbol != symbol)) {
    m_IconSymbol = symbol;
    lv_image_set_src(m_Icon, symbol);
  }
}

bool GPS::wiredFixIsFresh(void) {
  return m_Enabled && (m_GPS.location.age() < MAX_AGE_MS) && m_GPS.location.isValid()
         && (m_GPS.date.age() < MAX_AGE_MS) && m_GPS.date.isValid()
         && (m_GPS.time.age() < MAX_AGE_MS) && m_GPS.time.isValid()
         && (m_GPS.location.FixQuality() != TinyGPSLocation::Quality::Invalid);
}

GPS::source_t GPS::getSource(void) const {
  return static_cast<source_t>(m_Source.load());
}

uint8_t GPS::getSatellites(void) const {
  return m_Satellites.load();
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
    Console::gpsRaw(reinterpret_cast<const char *>(buffer.data()), bytes);
    m_GPS.encode(reinterpret_cast<char *>(buffer.data()), bytes);

    const uint32_t locationAge = m_GPS.location.age();
    if (m_GPS.location.isValid() && (locationAge < m_LastLocationAge)) {
      m_FixSequence = m_BurstSequence.load();
    }
    m_LastLocationAge = locationAge;

    m_LastSentence = Platform::getInstance().tick();
    captureSentences(reinterpret_cast<char *>(buffer.data()), bytes);
  }
}

/** Split the raw stream into sentences for the debug page. */
void GPS::captureSentences(const char *data, size_t length) {
  if (!m_Capture) {
    return;
  }

  std::lock_guard<std::mutex> lock(m_CaptureMutex);

  for (size_t i = 0; i < length; i++) {
    const char c = data[i];

    if ((c == '\r') || (c == '\n')) {
      if (!m_Partial.empty()) {
        m_Sentences[m_SentenceNext] = m_Partial;
        m_SentenceNext = (m_SentenceNext + 1) % m_Sentences.size();
        m_Partial.clear();
      }
    } else if (m_Partial.size() < SENTENCE_LEN) {
      m_Partial += c;
    }
  }
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

TinyGPSPlus &GPS::get(void) {
  return m_GPS;
}

}  // namespace Furble
