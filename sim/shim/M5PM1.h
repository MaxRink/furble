#ifndef FURBLE_SIM_M5PM1_H
#define FURBLE_SIM_M5PM1_H

#include <cstdint>

class M5PM1 {
 public:
  static constexpr int M5PM1_OK = 0;

  int begin(void *) { return M5PM1_OK; }
  int setSingleResetDisable(bool) { return M5PM1_OK; }
  int setDoubleOffDisable(bool) { return M5PM1_OK; }
  int setDownloadLock(bool) { return M5PM1_OK; }
  int shutdown(void) { return M5PM1_OK; }
  int btnGetState(bool *pressed) {
    if (pressed != nullptr) {
      *pressed = false;
    }
    return M5PM1_OK;
  }
};

#endif
