#ifndef FURBLE_SIM_M5PM1_H
#define FURBLE_SIM_M5PM1_H

#include <algorithm>
#include <cstdint>

#include "clock.h"

inline constexpr int M5PM1_OK = 0;
inline constexpr int M5PM1_ERROR = -1;
inline constexpr uint8_t M5PM1_GPIO_NUM_0 = 0;

class M5PM1 {
 public:
  int begin(void *) {
    m_Shutdown = false;
    m_Idle = true;
    m_BeginMillis = Furble::Sim::clockMillis();
    return M5PM1_OK;
  }

  int setSingleResetDisable(bool value) {
    return access([this, value]() { m_SingleResetDisabled = value; });
  }

  int setDoubleOffDisable(bool value) {
    return access([this, value]() { m_DoubleOffDisabled = value; });
  }

  int setDownloadLock(bool value) {
    return access([this, value]() { m_DownloadLocked = value; });
  }

  int wdtSet(uint8_t timeout_seconds) {
    return access([this, timeout_seconds]() {
      m_WatchdogSeconds = timeout_seconds;
      m_WatchdogArmed = timeout_seconds != 0;
      m_WatchdogExpired = false;
      m_WatchdogDeadline = Furble::Sim::clockMillis() + timeout_seconds * 1000U;
    });
  }

  int wdtFeed(void) {
    return access([this]() {
      if (m_WatchdogArmed && Furble::Sim::clockMillis() >= m_WatchdogDeadline) {
        m_WatchdogExpired = true;
        return;
      }
      m_WatchdogDeadline = Furble::Sim::clockMillis() + m_WatchdogSeconds * 1000U;
    });
  }

  int timerSet(uint32_t seconds) {
    return access([this, seconds]() {
      m_TimerArmed = seconds != 0;
      m_TimerDeadline = Furble::Sim::clockMillis() + seconds * 1000U;
    });
  }

  int shutdown(void) {
    return access([this]() {
      m_Shutdown = true;
      m_WatchdogArmed = false;
      m_TimerArmed = false;
    });
  }

  int readVbat(uint16_t *millivolts) {
    return access([this, millivolts]() {
      if (millivolts != nullptr) {
        constexpr uint32_t DISCHARGE_PERIOD_MS = 3600000;
        constexpr uint16_t START_MILLIVOLTS = 4000;
        constexpr uint16_t END_MILLIVOLTS = 3300;
        const uint32_t elapsed = Furble::Sim::clockMillis() - m_BeginMillis;
        const uint32_t drop =
            std::min<uint32_t>(static_cast<uint32_t>(START_MILLIVOLTS - END_MILLIVOLTS),
                               elapsed * (START_MILLIVOLTS - END_MILLIVOLTS) / DISCHARGE_PERIOD_MS);
        *millivolts = static_cast<uint16_t>(START_MILLIVOLTS - drop);
      }
    });
  }

  int gpioGetInput(uint8_t, uint8_t *value) {
    return access([value]() {
      if (value != nullptr) {
        *value = 1;
      }
    });
  }

  int btnGetState(bool *pressed) {
    const int result = access([]() {});
    if (pressed != nullptr) {
      *pressed = false;
    }
    return result;
  }

  bool watchdogExpired(void) const {
    return m_WatchdogExpired
           || (m_WatchdogArmed && Furble::Sim::clockMillis() >= m_WatchdogDeadline);
  }

  bool timerExpired(void) const {
    return m_TimerArmed && Furble::Sim::clockMillis() >= m_TimerDeadline;
  }

  bool isShutdown(void) const { return m_Shutdown; }

 private:
  template <typename Callback>
  int access(Callback callback) {
    // The first transaction after the PMIC has entered idle only wakes it.
    if (m_Idle) {
      m_Idle = false;
      return M5PM1_ERROR;
    }
    if (m_Shutdown) {
      return M5PM1_ERROR;
    }
    callback();
    m_Idle = true;
    return M5PM1_OK;
  }

  bool m_Idle = true;
  bool m_Shutdown = false;
  bool m_SingleResetDisabled = false;
  bool m_DoubleOffDisabled = false;
  bool m_DownloadLocked = false;
  bool m_WatchdogArmed = false;
  bool m_WatchdogExpired = false;
  uint8_t m_WatchdogSeconds = 0;
  uint32_t m_WatchdogDeadline = 0;
  bool m_TimerArmed = false;
  uint32_t m_TimerDeadline = 0;
  uint32_t m_BeginMillis = 0;
};

#endif
