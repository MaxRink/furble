// Host UI shim for the console command suite.
//
// The real header pulls in LVGL and the whole widget tree. The console only
// ever queues a request for the UI task, so the double records every request
// and lets a test assert the exact request and argument a command produced.
// The request set matches the display build, which is the superset.
#ifndef FURBLE_UI_H
#define FURBLE_UI_H

#include <cstdint>
#include <mutex>

namespace Furble {

/** Serializes M5.Imu transactions between UI timers and debug console probes. */
extern std::mutex g_IMUMutex;

class UI {
 public:
  enum class Request {
    CONNECT,
    DISCONNECT,
    SCAN,
    CAMERAS,
    DELETE,
    PAIR,
    MULTI_SELECT,
    MULTI_DESELECT,
    MULTI_CLEAR,
    GPS_RELOAD,
    GPS_POWER,
    IR_RELOAD,
    FEEDBACK_RELOAD,
    FEEDBACK_TEST,
    PERF,
    AUDIT,
    PAGE,
    BACK,
    INTERVAL,
    BULB,
    DISPLAY_BRIGHTNESS,
    POWER_OFF,
    POWER_RELOAD,
    SD_RELOAD,
    DISPLAY_MODE,
  };

  static bool sendRequest(Request request, int32_t arg);
};

}  // namespace Furble

#endif
