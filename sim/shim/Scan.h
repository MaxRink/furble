#ifndef FURBLE_SIM_SCAN_H
#define FURBLE_SIM_SCAN_H

#include <cstdint>
#include <functional>

#include <CameraList.h>

namespace Furble {

class Scan {
 public:
  static Scan &getInstance(void);

  void start(std::function<void(void *)> scan_callback, void *scan_result_private_data);
  void stop(void);
  bool isActive(void) const;
  void clear(void);
  void update(void);

 private:
  Scan() = default;

  bool m_Active = false;
  bool m_ResultPending = false;
  std::function<void(void *)> m_ScanResultCallback;
  void *m_ScanResultPrivateData = nullptr;
};

}  // namespace Furble

#endif
