#include <string>

#include "Device.h"

namespace Furble {
namespace {

std::string deviceId = "furble-sim";

}  // namespace

void Device::init(esp_power_level_t) {}

const std::string Device::getStringID(void) {
  return deviceId;
}

}  // namespace Furble
