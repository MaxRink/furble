#ifndef FURBLE_HEADLESS_UI_H
#define FURBLE_HEADLESS_UI_H

#include <cstdint>

#include "FurbleIMU.h"
#include "FurbleTypes.h"

#if defined(FURBLE_CONSOLE)
#include "FurbleRequestState.h"
#endif

namespace Furble {
class UI {
 public:
  enum class Request {
    CONNECT,
    CONNECT_SAVED,
    DISCONNECT,
    SCAN,
    CAMERAS,
    DELETE,
    GPS_RELOAD,
    GPS_POWER,
    IR_RELOAD,
    FEEDBACK_RELOAD,
    FEEDBACK_TEST,
    PERF,
    AUDIT,
  };

  static void init(void);
  static void shutdown(void);
  static bool sendRequest(Request request, int32_t arg);
#if defined(FURBLE_CONSOLE)
  using RequestResult = ::Furble::RequestResult;
  static bool sendRequest(Request request, int32_t arg, RequestResult *result);
#endif
  static void serviceRequests(void);

  static uint8_t getIntervalometerState(void);
  static uint16_t getIntervalometerRemaining(void);
  static void notifyGestureSettingsChanged(void) {}
};
}  // namespace Furble

#endif
