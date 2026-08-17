#include <cstdlib>

#include <M5Unified.h>

#include "FurblePlatform.h"
#include "Scan.h"
#include "clock.h"
#include "driver.h"

namespace Furble {

Platform &Platform::getInstance(void) {
  static Platform instance;

  if (!instance.m_Init) {
    auto config = M5.config();
    config.clear_display = true;
    config.internal_imu = false;
    config.internal_spk = false;
    config.internal_mic = false;
    config.pmic_button = true;
    M5.begin(config);
    instance.m_Init = true;
  }

  return instance;
}

void Platform::init(void) {
  (void)getInstance();
}

uint32_t Platform::tick(void) {
  return Sim::clockMillis();
}

uint8_t Platform::getPWRClickCount(void) {
  return 0;
}

void Platform::update(void) {
  M5.update();
  Scan::getInstance().update();
  Sim::advanceClock(5);
  Sim::driverTick();
}

void Platform::powerOff(void) {
  std::exit(0);
}

void Platform::setSleep(bool) {}

}  // namespace Furble
