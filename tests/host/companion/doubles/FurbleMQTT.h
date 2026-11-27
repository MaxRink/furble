#ifndef FURBLE_COMPANION_MQTT_H
#define FURBLE_COMPANION_MQTT_H

// Host double for the MQTT service, used by companion_gatt_test when the
// target defines FURBLE_MQTT. CompanionService reaches MQTT only through
// reloadSetting() after a setting that the broker reads has changed, so the
// double records that call instead of starting a broker session. Keep it
// inert when MQTT is off, exactly like include/FurbleMQTT.h, so the header can
// be included from either target configuration.

#if defined(FURBLE_MQTT) && FURBLE_MQTT

namespace Furble {

class MQTT {
 public:
  MQTT(MQTT const &) = delete;
  MQTT(MQTT &&) = delete;
  MQTT &operator=(MQTT const &) = delete;
  MQTT &operator=(MQTT &&) = delete;

  static MQTT &getInstance(void);

  /** Record one settings reload without touching a broker. */
  void reloadSetting(void);

  /** Number of reloadSetting() calls, so a test can assert the fan-out. */
  static unsigned reloadCount(void);

 private:
  MQTT() = default;
};

}  // namespace Furble

#endif

#endif
