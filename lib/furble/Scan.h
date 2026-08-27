#ifndef SCAN_H
#define SCAN_H

#include <array>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <vector>

#include <NimBLEAdvertisedDevice.h>
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
  /** Maximum number of advertisements retained between BLE and UI tasks. */
  static constexpr size_t MAX_PENDING_RESULTS = 32;
  static Scan &getInstance(void);

  ~Scan();

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
  void start(NimBLEScanCallbacks *pScanCallbacks, uint32_t duration, bool wantDuplicates = false);

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

  /**
   * Drain discovery events on the caller's task.
   *
   * This is the only path which matches advertisements and invokes discovery
   * callbacks. Call it from the UI task, never from the NimBLE host task.
   */
  void processPendingCallbacks(void);

  /** Number of advertisements dropped by the bounded handoff queue. */
  size_t droppedResultCount(void) const;

  void onResult(const NimBLEAdvertisedDevice *pDevice) override;

  void onScanEnd(const NimBLEScanResults &results, int reason) override;

 private:
  Scan();

  class CallbackProxy;

  enum class CallbackMode : uint8_t {
    IDLE,
    DISCOVERY,
    CUSTOM,
  };

  struct PendingEvent {
    enum class Type : uint8_t {
      RESULT,
      END,
    };

    Type type = Type::RESULT;
    uint64_t generation = 0;
    NimBLEAdvertisedDevice device;
    std::function<void(void *)> callback;
    void *privateData = nullptr;
  };

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
  void handleResult(uint64_t generation, const NimBLEAdvertisedDevice *pDevice);
  void handleScanEnd(uint64_t generation, const NimBLEScanResults &results, int reason);
  void expire(void);
  static uint64_t monotonicUs(void);

  NimBLEServer *m_Server = nullptr;
  NimBLEScan *m_Scan = nullptr;
  std::function<void(void *)> m_ScanResultCallback;
  std::function<void(void *)> m_ScanEndCallback;
  void *m_ScanResultPrivateData = nullptr;
  mutable std::mutex m_StateMutex;
  // Serialize lifecycle changes with UI-task dispatch. Dispatch can then
  // release the state mutex before running CameraList/UI callbacks without
  // allowing a concurrent start/stop to invalidate that dispatch. A recursive
  // mutex permits a callback to request a restart or cancellation.
  std::recursive_mutex m_DispatchMutex;
  std::condition_variable m_CallbackIdle;
  CallbackMode m_CallbackMode = CallbackMode::IDLE;
  NimBLEScanCallbacks *m_CustomCallbacks = nullptr;
  size_t m_CallbacksInFlight = 0;
  bool m_Active = false;
  uint64_t m_Generation = 0;
  uint64_t m_DeadlineUs = 0;
  std::deque<PendingEvent> m_PendingEvents;
  size_t m_DroppedResults = 0;
  // NimBLEScan::stop() synchronously quiesces host-task callbacks before it
  // returns. One stable proxy avoids an allocation on every scan start.
  std::unique_ptr<CallbackProxy> m_CallbackProxy;
  Mode m_Mode = Mode::FULL;
  uint32_t m_Timeout = 0;
};

}  // namespace Furble

#endif
