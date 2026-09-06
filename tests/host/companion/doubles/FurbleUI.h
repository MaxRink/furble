#ifndef FURBLE_UI_H
#define FURBLE_UI_H

#include <cstdint>
#include <vector>

namespace Furble {

class UI {
 public:
  /**
   * Mirror of the production request enum.
   *
   * Only CONNECT and DISCONNECT are exercised here: they are the operations the
   * companion cameras characteristic hands to the UI task. The double records
   * them and the test replays them through the real Control, which is what the
   * firmware request handler does.
   */
  enum class Request {
    CONNECT,
    DISCONNECT,
    SCAN,
    CAMERAS,
    GPS_RELOAD,
    GPS_POWER,
    IR_RELOAD,
    FEEDBACK_RELOAD,
    FEEDBACK_TEST,
    POWER_RELOAD,
    SD_RELOAD,
  };

  static bool sendRequest(Request request, int32_t arg);

  static int32_t getBatteryLevel(void);
  static int16_t getBatteryVoltage(void);
  static int32_t getBatteryCurrent(void);
  static int16_t getBatteryVBUSVoltage(void);
  static bool isBatteryCharging(void);
  static uint8_t getIntervalometerState(void);
  static uint16_t getIntervalometerRemaining(void);
  static void notifyGestureSettingsChanged(void) {}
};

namespace Host {

void setBatteryStatus(int32_t level, int16_t voltage, int32_t current, int16_t vbus, bool charging);

struct UIRequest {
  UI::Request request;
  int32_t arg;
};

/** Requests the companion service has queued for the UI task, oldest first. */
std::vector<UIRequest> takeUIRequests(void);

/** Reject queued requests, standing in for a full request queue. */
void setUIRequestsAccepted(bool accepted);

}  // namespace Host
}  // namespace Furble

#endif
