#ifndef FURBLE_SIM_SCAN_H
#define FURBLE_SIM_SCAN_H

#include <cstdint>
#include <functional>

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

  void setMode(Mode mode);
  void setTimeout(uint32_t timeout);
  void start(std::function<void(void *)> scan_callback,
             void *scan_result_private_data,
             std::function<void(void *)> scan_end_callback = nullptr);
  void stop(void);
  bool isActive(void) const;
  size_t endCallbackCount(void) const;
  void clear(void);
  void update(void);

 private:
  Scan() = default;

  bool m_Active = false;
  bool m_ResultPending = false;
  size_t m_EndCallbackCount = 0;
  std::function<void(void *)> m_ScanResultCallback;
  std::function<void(void *)> m_ScanEndCallback;
  void *m_ScanResultPrivateData = nullptr;
};

}  // namespace Furble

#endif
