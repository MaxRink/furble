#ifndef SCAN_H
#define SCAN_H

#include <array>
#include <vector>

#include <NimBLEScan.h>

#include "CameraList.h"
#include "FurbleTypes.h"

#ifndef FURBLE_VERSION
#define FURBLE_VERSION "unknown"
#endif

namespace Furble {
/**
 * BLE advertisement scanning class.
 *
 * Works in conjunction with Furble::Device class.
 */
class Scan: public NimBLEScanCallbacks {
 public:
  /**
   * Scan duty cycle presets.
   */
  enum class Mode : uint8_t {
    FULL = 0,      // 100 percent duty
    BALANCED = 1,  // 25 percent duty
    LOW = 2,       // 5 percent duty
  };

  static constexpr size_t MODE_COUNT = 3;

  static Scan &getInstance(void);

  Scan(Scan const &) = delete;
  Scan(Scan &&) = delete;
  Scan &operator=(Scan const &) = delete;
  Scan &operator=(Scan &&) = delete;

  /**
   * Set the duty cycle preset, applied on the next discovery scan.
   */
  void setMode(Mode mode);

  /**
   * Set the discovery scan timeout in seconds, zero scans until stopped.
   */
  void setTimeout(uint32_t timeout);

  /**
   * Start the scan for BLE advertisements with a callback function when a
   * matching reseult is encountered.
   *
   * The optional end callback fires when the scan stops by itself, it does not
   * fire on stop().
   */
  void start(std::function<void(void *)> scanCallback,
             void *scanResultPrivateData,
             std::function<void(void *)> scanEndCallback = nullptr);

  /**
   * Start scanning with a custom callback system.
   *
   * This is the pairing and reconnect path, it always scans at full duty.
   */
  void start(NimBLEScanCallbacks *pScanCallbacks, uint32_t duration);

  /**
   * Stop the scan.
   */
  void stop(void);

  /**
   * Scanning is active.
   */
  bool isActive(void) const;

  /**
   * Clear the scan list.
   */
  void clear(void);

  void onResult(const NimBLEAdvertisedDevice *pDevice) override;

  void onScanEnd(const NimBLEScanResults &results, int reason) override;

 private:
  Scan() {};

  /** Scan interval and window, both in milliseconds. */
  typedef struct {
    uint16_t interval;
    uint16_t window;
  } duty_t;

  static constexpr uint16_t HID_GENERIC_REMOTE = 0x180;

  static constexpr std::array<duty_t, MODE_COUNT> m_Duty = {
      {
       {6553, 6553},  // FULL
          {120, 30},     // BALANCED
          {1000, 50},    // LOW
      }
  };

  /**
   * Apply a duty cycle preset to the scanner.
   */
  void applyMode(Mode mode);

  NimBLEServer *m_Server = nullptr;
  NimBLEScan *m_Scan = nullptr;
  std::function<void(void *)> m_ScanResultCallback;
  std::function<void(void *)> m_ScanEndCallback;
  void *m_ScanResultPrivateData = nullptr;
  Mode m_Mode = Mode::FULL;
  uint32_t m_Timeout = 0;
};

}  // namespace Furble

#endif
