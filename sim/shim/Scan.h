#ifndef FURBLE_SIM_SCAN_H
#define FURBLE_SIM_SCAN_H

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

#include <CameraList.h>

namespace Furble {

class Scan {
 public:
  /** Scan duty cycle presets, mirrors lib/furble/Scan.h. */
  enum class Mode : uint8_t {
    FULL = 0,
    BALANCED = 1,
    LOW = 2,
  };

  static constexpr size_t MODE_COUNT = 3;

  static Scan &getInstance(void);
  ~Scan();

  void setMode(Mode mode);
  void setTimeout(uint32_t timeout);
  /** Install the simulator's cross-task start responsiveness probe. */
  void setStartProbe(std::function<void()> probe);
  /** Whether the most recent probed start waited for the UI lock. */
  bool startProbeBlocked(void) const;
  bool start(std::function<void(void *)> scan_callback,
             void *scan_result_private_data,
             std::function<void(void *)> scan_end_callback = nullptr);
  void stop(void);
  /** Stop scanning and join simulator-only callback probe workers. */
  void shutdown(void);
  bool isActive(void) const;
  size_t endCallbackCount(void) const;
  /** Number of explicit cancellation calls, for observation-purity checks. */
  size_t stopCount(void) const;
  /** Identifier of the simulated advertisement currently being drained. */
  size_t currentResultId(void) const;
  void clear(void);
  void processPendingCallbacks(void);
  void update(void);

 private:
  Scan() = default;

  bool m_Active = false;
  std::atomic<uint64_t> m_Generation {0};
  uint64_t m_Deadline = 0;
  bool m_HasDeadline = false;
  uint32_t m_Timeout = 0;
  size_t m_EndCallbackCount = 0;
  size_t m_StopCount = 0;
  size_t m_CurrentResultId = 0;
  std::function<void(void *)> m_ScanResultCallback;
  std::function<void(void *)> m_ScanEndCallback;
  void *m_ScanResultPrivateData = nullptr;
  std::function<void()> m_StartProbe;
  bool m_StartProbeBlocked = false;
  struct PendingEvent {
    uint64_t generation;
    bool end;
    size_t resultId;
  };
  mutable std::mutex m_Mutex;
  // Lifecycle and UI draining are serialized so a generation cannot change
  // between an event check and its callback. Recursive entry permits a UI
  // callback to request a restart or cancellation.
  std::recursive_mutex m_DispatchMutex;
  std::condition_variable m_WorkerDone;
  std::deque<PendingEvent> m_PendingEvents;
  std::thread m_Worker;
  std::vector<std::thread> m_ProbeWorkers;
  bool m_WorkerRunning = false;
};

}  // namespace Furble

#endif
