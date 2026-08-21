#ifndef FURBLE_SD_H
#define FURBLE_SD_H

#include <atomic>
#include <cstdint>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <sdmmc_cmd.h>

#include "FurbleGPX.h"

namespace Furble {
/**
 * SD card service.
 *
 * A dedicated low priority writer task owns the card mount state and the GPX
 * file handle. Every other task, including the LVGL, GPS and NimBLE tasks,
 * interacts only through the non-blocking request queue and the atomic state
 * accessors. No SD or file I/O ever runs outside the writer task.
 */
class SD {
 public:
  /** Asynchronous requests handled by the writer task. */
  enum class request_t : uint8_t {
    POINT,      /**< append a GPX track point */
    CLOSE,      /**< close the GPX track file */
    MOUNT,      /**< mount for the storage page and hold the card mounted */
    PAGE_LEAVE, /**< release the storage page hold */
    RELOAD,     /**< re-read the SD settings and apply them */
    EXPORT,     /**< export settings to the card */
    IMPORT,     /**< import settings from the card, restart on success */
    POWER_OFF,  /**< close and unmount for power off, signals completion */
  };

  /** Card state for the storage page, written by the writer task. */
  enum class card_state_t : uint8_t {
    UNMOUNTED,
    MOUNTED,
    FAILED,
  };

  static SD &getInstance(void);

  SD(SD const &) = delete;
  SD(SD &&) = delete;
  SD &operator=(SD const &) = delete;
  SD &operator=(SD &&) = delete;

  /** Probe whether this board has the M5Stack SD slot. */
  bool isSupported(void) const;

  /** Probe the card slot and start the writer task. */
  static void init(void);

  /** Queue a request for the writer task, drops when unsupported or full. */
  bool request(request_t request);

  /** Queue a GPX track point for the writer task, never blocks. */
  bool logPoint(const GPX::point_t &point);

  /** Close the track and unmount from the writer task, waits for completion. */
  void powerOff(void);

  /** Return the card state as last published by the writer task. */
  card_state_t cardState(void) const;

  /** Return the card capacity in MB, or zero when unavailable. */
  uint32_t capacityMB(void) const;

  /** Return the free filesystem space in MB, or zero when unavailable. */
  uint32_t freeMB(void) const;

  /** Change counter, bumped by the writer task after every state change. */
  uint32_t generation(void) const;

 private:
  SD() = default;

  typedef struct {
    request_t request;
    GPX::point_t point;
  } message_t;

  void taskLoop(void);
  void handleRequest(const message_t &message);
  void handlePoint(const GPX::point_t &point);
  void handleReload(void);
  void handleTransfer(bool import);

  bool mount(void);
  void unmount(void);
  void maybeUnmount(void);
  void publish(void);

  bool exportSettings(void);
  bool importSettings(void);

  bool m_Supported = false;
  QueueHandle_t m_Queue = nullptr;
  SemaphoreHandle_t m_PowerOffDone = nullptr;

  // writer task state, never touched from other tasks
  bool m_Mounted = false;
  bool m_MountFailed = false;
  sdmmc_card_t *m_Card = nullptr;
  bool m_LoggingEnabled = false;
  uint16_t m_PeriodSeconds = 5;
  bool m_UIHold = false;
  uint8_t m_Failures = 0;

  // published state, read by the UI task
  std::atomic<uint8_t> m_CardState {0};
  std::atomic<uint32_t> m_CapacityMB {0};
  std::atomic<uint32_t> m_FreeMB {0};
  std::atomic<uint32_t> m_Generation {0};
};
}  // namespace Furble

#endif
