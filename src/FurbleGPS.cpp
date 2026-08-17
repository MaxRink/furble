#include <algorithm>
#include <limits>

#include <M5Unified.h>
#include <TinyGPS++.h>
#include <esp_timer.h>

#include "icons.h"
#endif

#include <algorithm>
#include <cmath>

#if !defined(FURBLE_NO_DISPLAY)
#include <lvgl.h>

#include "icons.h"
#endif

#include <algorithm>
#include <cmath>

#include "FurbleConsole.h"
#include "FurbleControl.h"
#include "FurbleGPS.h"
#include "FurbleGPX.h"
#include "FurblePlatform.h"
#include "FurblePower.h"
#include "FurbleSD.h"
#include "FurbleSettings.h"
#include "FurbleTypes.h"
#include "FurbleWiFi.h"
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
    // Key the clock on the SoC, not the board: any S3 build must use XTAL
    // or DFS breaks the baud rate (see the root CLAUDE.md traps section).
#if SOC_UART_SUPPORT_XTAL_CLK
        // XTAL keeps the baud stable while DFS scales the APB clock
        .source_clk = UART_SCLK_XTAL,
#else
        // the original ESP32 has no XTAL UART clock, REF_TICK is its
        // DFS stable source
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
          serviceSerial();
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

  m_AidMode.store(Settings::load<Settings::GPS_ASSIST>());
  loadAidCache();
  m_ConfigQueue.clear();
  m_ConfigInFlight.reset();
  m_ConfigFallbackUsed = false;
  m_ConfigNavxAcked = false;
  m_NavxPayloadValid = false;
  {
    const std::lock_guard<std::mutex> lock(m_ConfigMutex);
    m_ConfigStatusCount = 0;
  }

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
  FURBLE_SIM_GPS_STATE("acquiring");
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
  FURBLE_SIM_GPS_STATE("tracking");
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
  FURBLE_SIM_GPS_STATE("tracking");
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
    sendCommand(command.fallback);
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

  m_ConfigQueue.clear();
  m_ConfigInFlight.reset();
  m_ConfigFallbackUsed = false;
  m_ConfigNavxAcked = false;
  m_NavxPayloadValid = false;
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

  if ((constellation > 0) && (constellation <= CONSTELLATION_MAX)) {
    const std::vector<uint8_t> query;
    enqueueConfig(CFG_CLASS, CFG_NAVX_ID, query, "PCAS04," + std::to_string(constellation), true);
  }
}

/** Configure the receiver once it is awake, then service one binary command. */
void GPS::serviceConfig(void) {
  if (!m_Enabled) {
    return;
  }

  if (m_ConfigPending) {
    const bool awake = m_GPS.charsProcessed() > m_ConfigChars;
    const bool expired = (Platform::getInstance().tick() - m_ConfigStart) > SETTLE_MS;
    if (!awake && !expired) {
      return;
    }

    m_ConfigPending = false;
    if (m_AidMode.load() != 0) {
      sendAidIni();
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
      || (timesync.day > 31) || (timesync.hour > 23) || (timesync.minute > 59)
      || (timesync.second > 59)) {
    return -1;
  }

  static constexpr std::array<uint8_t, 12> DAYS = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
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

  struct timeval tv = {
      .tv_sec = static_cast<time_t>(utc_seconds),
      .tv_usec = static_cast<suseconds_t>(timesync.centisecond * 10000),
  };
  settimeofday(&tv, nullptr);
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

void GPS::disable(void) {
  m_Enabled = false;
  FURBLE_SIM_GPS_STATE("off");
  m_ConfigPending = false;
  m_ConfigQueue.clear();
  m_ConfigInFlight.reset();
  m_ConfigNavxAcked = false;
  m_NavxPayloadValid = false;
  m_RxBuffer.clear();
  m_LastLoggedFix = 0;
  m_LastLoggedStamp = 0;

  // the SD writer task owns the track file, never touch it from here
  SD::getInstance().request(SD::request_t::CLOSE);

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
  m_AidMode.store(Settings::load<Settings::GPS_ASSIST>());
  reloadLogSettings();

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
#if defined(FURBLE_NO_DISPLAY)
  // No LVGL, so drive the same 1 s update() from an esp_timer. update() only
  // touches LVGL behind its own display guard, the geotag push code is shared.
  if (m_ServiceTimer != nullptr) {
    return;
  }

  const esp_timer_create_args_t args = {
      .callback = [](void *param) { static_cast<GPS *>(param)->update(); },
      .arg = this,
      .dispatch_method = ESP_TIMER_TASK,
      .name = "gps-service",
      .skip_unhandled_events = true,
  };
  ESP_ERROR_CHECK(esp_timer_create(&args, &m_ServiceTimer));
  ESP_ERROR_CHECK(esp_timer_start_periodic(m_ServiceTimer, SERVICE_MS * 1000ULL));
#else
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

  if (fix.accuracy_valid && (!std::isfinite(fix.accuracy_m) || (fix.accuracy_m < 0.0f))) {
    return false;
  }

  if (fix.time_valid
      && ((fix.timesync.year < 2000) || (fix.timesync.month < 1) || (fix.timesync.month > 12)
          || (fix.timesync.day < 1) || (fix.timesync.day > 31) || (fix.timesync.hour > 23)
          || (fix.timesync.minute > 59) || (fix.timesync.second > 60)
          || (fix.timesync.centisecond > 99))) {
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
  external_fix_t current = {};
  source_t source = SOURCE_NONE;
  uint8_t satellites = 0;
  bool altitudeValid = false;

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
    // NTP supplies time when synced. GPS still supplies the position.
    WiFi::getNtpTimesync(timesync);
    updateAidCache(dgps, timesync);
    satellites = static_cast<uint8_t>(
        std::min<uint32_t>(static_cast<uint32_t>(m_GPS.satellites.value()), 255u));
    current.gps = dgps;
    current.timesync = timesync;
    current.age_ms = static_cast<uint32_t>(m_GPS.location.age());
    current.position_valid = true;
    current.time_valid = true;
    current.altitude_valid = m_GPS.altitude.isValid();
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
      current = external;
      current.age_ms = static_cast<uint32_t>(external.age_ms + elapsed_ms);
    }
  }

  m_Source.store(static_cast<uint8_t>(source));
  m_Satellites.store(satellites);
  {
    const std::lock_guard<std::mutex> lock(m_FixMutex);
    m_CurrentFix = current;
    m_HasFix.store(source != SOURCE_NONE);
  }

  if (m_HasFix.load()) {
    Control::getInstance().updateGPS(dgps, timesync);

    const uint32_t fixSequence = m_FixSequence.load();
    if ((fixSequence != 0) && (fixSequence != m_PushedSequence.load())) {
      m_PushedSequence = fixSequence;
      if (dutyCycleEnabled()) {
        m_CycleRequest = true;
      }
    }

    // both fix sources are logged, the point is built from the normalized
    // dgps and timesync values and only queued, the SD writer task does the I/O
    if (m_LogEnabled.load()) {
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
#endif
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

bool GPS::getCurrentFix(external_fix_t &fix) const {
  const std::lock_guard<std::mutex> lock(m_FixMutex);
  if (!m_HasFix.load()) {
    return false;
  }

  fix = m_CurrentFix;
  return true;
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

    const uint32_t locationAge = m_GPS.location.age();
    if (m_GPS.location.isValid() && (locationAge < m_LastLocationAge)) {
      m_FixSequence = m_BurstSequence.load();
    }
    m_LastLocationAge = locationAge;

    m_LastSentence = Platform::getInstance().tick();
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
