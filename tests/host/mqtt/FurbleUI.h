#ifndef FURBLE_HOST_MQTT_UI_H
#define FURBLE_HOST_MQTT_UI_H

#include <cstdint>
#include <vector>

namespace Furble {

class UI {
 public:
  enum class Request {
    CONNECT_SAVED,
    DISCONNECT,
  };

  struct RequestRecord {
    Request request;
    int32_t arg;
  };

  static bool sendRequest(Request request, int32_t arg) {
    m_Requests.push_back({request, arg});
    return true;
  }

  static const std::vector<RequestRecord> &requests(void) { return m_Requests; }

  /** Same protocol values as the production intervalometer status. */
  static uint8_t getIntervalometerState(void) { return m_IntervalometerState; }
  static void setIntervalometerState(uint8_t state) { m_IntervalometerState = state; }

  static void reset(void) {
    m_Requests.clear();
    m_IntervalometerState = 0;
  }

 private:
  inline static std::vector<RequestRecord> m_Requests;
  inline static uint8_t m_IntervalometerState = 0;
};

}  // namespace Furble

#endif
