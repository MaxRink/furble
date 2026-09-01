// Host Scan shim for the console command suite.
//
// 'scan list' reports whether a scan is running and then hands the printing to
// the UI task. Only the active flag is needed, and a test can set it.
#ifndef FURBLE_HOST_CONSOLE_SCAN_H
#define FURBLE_HOST_CONSOLE_SCAN_H

namespace Furble {

class Scan {
 public:
  static Scan &getInstance(void);

  bool isActive(void) const;
  void setActive(bool active);

 private:
  Scan() = default;

  bool m_Active = false;
};

}  // namespace Furble

#endif
