#ifndef FURBLE_HEADLESS_DEVICE_H
#define FURBLE_HEADLESS_DEVICE_H

#include <string>

#include <esp_bt.h>

namespace Furble {
enum class TxPower : uint8_t { P3, P6, P9 };

class Device {
 public:
  static void init(TxPower power);
  static const std::string &getStringID(void);
};
}  // namespace Furble

#endif
