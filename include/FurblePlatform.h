#ifndef FURBLE_PLATFORM_H
#define FURBLE_PLATFORM_H

#include <array>

#include <esp_err.h>

#include <M5PM1.h>

namespace Furble {
class Platform {
 public:
  /** Selectable maximum CPU frequencies in MHz. */
  static constexpr std::array<uint8_t, 3> CPU_MAX_FREQ_MHZ = {80, 160, 240};

  /** Default maximum CPU frequency in MHz. */
  static constexpr uint8_t CPU_MAX_FREQ_DEFAULT_MHZ = 160;

  static Platform &getInstance();

  Platform(Platform const &) = delete;
  Platform(Platform &&) = delete;
  Platform &operator=(Platform const &) = delete;
  Platform &operator=(Platform &&) = delete;

  static void init(void);

  /**
   * Get time since boot in milliseconds.
   */
  uint32_t tick(void);

  /**
   * Get debounced power button click count.
   */
  uint8_t getPWRClickCount(void);

  /**
   * Update platform specific state.
   */
  void update(void);

  /**
   * Power off the device.
   */
  void powerOff(void);

  /**
   * Set the maximum CPU frequency in MHz.
   *
   * Unsupported values fall back to the default. Use getCPUMaxFreq() to read
   * back what was actually applied.
   */
  void setCPUMaxFreq(uint8_t mhz);

  /**
   * Get the maximum CPU frequency in MHz.
   */
  uint8_t getCPUMaxFreq(void) const;

 private:
  Platform() {};

  /**
   * Is the frequency one we are prepared to ask for?
   */
  static bool isCPUMaxFreqValid(uint8_t mhz);

  // Power button click streak threshold
  const uint8_t PWR_CLICK_THRESHOLD_MS = 20;

  M5PM1 m_M5PM1;

  bool m_Init = false;
  bool m_PMICHack = false;
  uint8_t m_CPUMaxFreqMHz = CPU_MAX_FREQ_DEFAULT_MHZ;
  uint8_t m_PMICClickCount = 0;
  uint32_t m_PMICClickTime = 0;
};
}  // namespace Furble

#endif
