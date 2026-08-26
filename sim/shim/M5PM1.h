#ifndef FURBLE_SIM_M5PM1_H
#define FURBLE_SIM_M5PM1_H

#include <algorithm>
#include <cstdint>

#include "clock.h"

inline constexpr int M5PM1_OK = 0;
inline constexpr int M5PM1_ERROR = -1;
inline constexpr uint8_t M5PM1_GPIO_NUM_0 = 0;

class M5PM1 {
  struct PersistentState {
    PersistentState()
        : downloadLocked(false),
          watchdogArmed(false),
          watchdogExpired(false),
          watchdogSeconds(0),
          watchdogDeadline(0) {}

    bool downloadLocked;
    bool watchdogArmed;
    bool watchdogExpired;
    uint8_t watchdogSeconds;
    uint32_t watchdogDeadline;
  };

  struct Faults {
    Faults()
        : failSetWatchdog(false),
          failReadWatchdog(false),
          failSetDownloadLock(false),
          failReadDownloadLock(false) {}

    bool failSetWatchdog;
    bool failReadWatchdog;
    bool failSetDownloadLock;
    bool failReadDownloadLock;
  };

 public:
  /** Reset the retained PMIC model between independent host tests. */
  static void resetPersistentStateForTest(void) {
    m_Persistent = PersistentState();
    m_Faults = Faults();
  }

  /** Inject one-shot I2C failures for negative-path host tests. */
  static void failNextForTest(bool set_watchdog,
                              bool read_watchdog,
                              bool set_download_lock,
                              bool read_download_lock) {
    m_Faults.failSetWatchdog = set_watchdog;
    m_Faults.failReadWatchdog = read_watchdog;
    m_Faults.failSetDownloadLock = set_download_lock;
    m_Faults.failReadDownloadLock = read_download_lock;
  }

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
    if (m_Idle) {
      return access([]() {});
    }
    if (consumeFault(m_Faults.failSetDownloadLock)) {
      return M5PM1_ERROR;
    }
    return access([value]() { m_Persistent.downloadLocked = value; });
  }

  int getDownloadLock(bool *value) {
    if (m_Idle) {
      return access([]() {});
    }
    if (consumeFault(m_Faults.failReadDownloadLock)) {
      return M5PM1_ERROR;
    }
    return access([value]() {
      if (value != nullptr) {
        *value = m_Persistent.downloadLocked;
      }
    });
  }

  int wdtSet(uint8_t timeout_seconds) {
    if (m_Idle) {
      return access([]() {});
    }
    if (consumeFault(m_Faults.failSetWatchdog)) {
      return M5PM1_ERROR;
    }
    return access([timeout_seconds]() {
      m_Persistent.watchdogSeconds = timeout_seconds;
      m_Persistent.watchdogArmed = timeout_seconds != 0;
      m_Persistent.watchdogExpired = false;
      m_Persistent.watchdogDeadline = Furble::Sim::clockMillis() + timeout_seconds * 1000U;
    });
  }

  int wdtFeed(void) {
    return access([]() {
      if (m_Persistent.watchdogArmed
          && Furble::Sim::clockDeadlineReached(Furble::Sim::clockMillis(),
                                               m_Persistent.watchdogDeadline)) {
        m_Persistent.watchdogExpired = true;
        return;
      }
      m_Persistent.watchdogDeadline =
          Furble::Sim::clockMillis() + m_Persistent.watchdogSeconds * 1000U;
    });
  }

  int wdtGetCount(uint8_t *count) {
    if (m_Idle) {
      return access([]() {});
    }
    if (consumeFault(m_Faults.failReadWatchdog)) {
      return M5PM1_ERROR;
    }
    return access([count]() {
      if (count == nullptr) {
        return;
      }
      if (!m_Persistent.watchdogArmed
          || Furble::Sim::clockDeadlineReached(Furble::Sim::clockMillis(),
                                               m_Persistent.watchdogDeadline)) {
        *count = 0;
        return;
      }
      const uint32_t remaining = m_Persistent.watchdogDeadline - Furble::Sim::clockMillis();
      *count = static_cast<uint8_t>(std::min<uint32_t>(255, (remaining + 999) / 1000));
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
      m_Persistent.watchdogArmed = false;
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
    return m_Persistent.watchdogExpired
           || (m_Persistent.watchdogArmed
               && Furble::Sim::clockDeadlineReached(Furble::Sim::clockMillis(),
                                                    m_Persistent.watchdogDeadline));
  }

  bool timerExpired(void) const {
    return m_TimerArmed
           && Furble::Sim::clockDeadlineReached(Furble::Sim::clockMillis(), m_TimerDeadline);
  }

  bool isShutdown(void) const { return m_Shutdown; }

  bool downloadRecoveryUnlocked(void) const { return !m_Persistent.downloadLocked; }

 private:
  static bool consumeFault(bool &fault) {
    if (!fault) {
      return false;
    }
    fault = false;
    return true;
  }

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
  bool m_TimerArmed = false;
  uint32_t m_TimerDeadline = 0;
  uint32_t m_BeginMillis = 0;

  inline static PersistentState m_Persistent;
  inline static Faults m_Faults;
};

#endif
