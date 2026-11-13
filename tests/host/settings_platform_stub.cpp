#include "FurblePlatform.h"

namespace Furble {

Platform &Platform::getInstance() {
  static Platform instance;
  return instance;
}

void Platform::restart(void) {}

// Platform is a PowerBackend, so the constructor needs its vtable. These
// targets never exercise the power path; every member reports no hardware.
Platform::battery_caps_t Platform::getBatteryCaps(void) {
  return {false, false, false, false};
}
Platform::battery_t Platform::readBattery(void) {
  return {0, 0, 0, false};
}
PowerBackend::BatterySource Platform::getBatterySource(void) {
  return BatterySource::NONE;
}
uint16_t Platform::getBatteryCapacity(void) {
  return 0;
}
bool Platform::setRail(Rail, bool) {
  return false;
}
uint8_t Platform::getPWRClickCount(void) {
  return 0;
}
bool Platform::powerOff(void) {
  return false;
}
bool Platform::canTimedWake(void) {
  return false;
}
bool Platform::powerOffUntil(uint32_t) {
  return false;
}
bool Platform::consumeTimedWake(void) {
  return false;
}

}  // namespace Furble
