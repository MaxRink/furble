#ifndef FURBLE_SIM_DEVICE_H
#define FURBLE_SIM_DEVICE_H

#include <string>

#include <esp_bt.h>

namespace Furble {

class Device {
 public:
  static void init(esp_power_level_t power);
  static const std::string getStringID(void);
};

}  // namespace Furble

#endif
