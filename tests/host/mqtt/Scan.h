#ifndef FURBLE_HOST_MQTT_SCAN_H
#define FURBLE_HOST_MQTT_SCAN_H

namespace Furble {

class Scan {
 public:
  static Scan &getInstance(void) {
    static Scan scan;
    return scan;
  }

  void stop(void) { m_StopCount++; }
  unsigned int stopCount(void) const { return m_StopCount; }

 private:
  unsigned int m_StopCount = 0;
};

}  // namespace Furble

#endif
