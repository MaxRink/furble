#ifndef FURBLE_HOST_MQTT_INTERVAL_H
#define FURBLE_HOST_MQTT_INTERVAL_H

#include <cstdint>

namespace SpinValue {

enum unit_t : uint8_t {
  UNIT_MS,
  UNIT_SEC,
  UNIT_MIN,
  UNIT_NIL,
  UNIT_INF,
};

struct nvs_t {
  uint32_t value;
  unit_t unit;
};

}  // namespace SpinValue

struct interval_t {
  SpinValue::nvs_t count;
  SpinValue::nvs_t wait;
  SpinValue::nvs_t shutter;
  SpinValue::nvs_t delay;
};

#endif
