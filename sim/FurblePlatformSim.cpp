#include <cstdlib>

#include <M5GFX.h>
#include <M5Unified.h>
#include <SDL2/SDL.h>

#include <driver/uart.h>

#include "FurblePlatform.h"
#include "FurblePower.h"
#include "Scan.h"
#include "clock.h"
#include "driver.h"

namespace Furble {

Platform &Platform::getInstance(void) {
  static Platform instance;

  if (!instance.m_Init) {
    Power::init();
    Power::getInstance().configure(Platform::CPU_MAX_FREQ_DEFAULT_MHZ);
    auto config = M5.config();
    config.clear_display = true;
    config.internal_imu = false;
    config.internal_spk = false;
    config.internal_mic = false;
    config.pmic_button = true;
    M5.begin(config);

    // Map keyboard letters to the same emulated button GPIOs M5Unified's PC
    // build reads (BtnA=39, BtnB=38, BtnC=37, BtnPWR=36), so an interactive
    // session with a visible SDL window can drive the physical buttons furble
    // handles on hardware. The M5GFX SDL panel already maps the arrow keys to
    // these pins; the letters just make the button identity explicit. This path
    // needs the SDL event pump, so it only works interactively: headless
    // scenarios drive the buttons through the `btn` command, which injects on
    // the UI task via UI::simPressButton instead of toggling pins.
    lgfx::Panel_sdl::addKeyCodeMapping(SDLK_a, 39);  // BtnA
    lgfx::Panel_sdl::addKeyCodeMapping(SDLK_b, 38);  // BtnB
    lgfx::Panel_sdl::addKeyCodeMapping(SDLK_c, 37);  // BtnC (Core only)
    lgfx::Panel_sdl::addKeyCodeMapping(SDLK_p, 36);  // BtnPWR (Stick only)

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
  furble_sim_uart_update();
  Scan::getInstance().update();
  Sim::driverTick();
}

void Platform::restart(void) {
  // The host simulator has no reset vector; ending the process is the closest
  // equivalent for scripted runs.
  std::_Exit(0);
}

bool Platform::powerOff(void) {
  std::_Exit(0);
  return true;
}

void Platform::watchdogEnable(bool) {}

void Platform::watchdogFeed(void) {}

void Platform::setDisplayOff(bool) {}

void Platform::setCPUMaxFreq(uint8_t mhz) {
  m_CPUMaxFreqMHz = isCPUMaxFreqValid(mhz) ? mhz : CPU_MAX_FREQ_DEFAULT_MHZ;
}

uint8_t Platform::getCPUMaxFreq(void) const {
  return m_CPUMaxFreqMHz;
}

bool Platform::isCPUMaxFreqValid(uint8_t mhz) {
  for (const uint8_t freq : CPU_MAX_FREQ_MHZ) {
    if (freq == mhz) {
      return true;
    }
  }
  return false;
}

Platform::pm_config_t Platform::getPMConfig(void) {
  return {m_CPUMaxFreqMHz, 80, false};
}

void Platform::dumpPMLocks(void) {}

bool Platform::hasTicklessIdle(void) {
  return false;
}

const Platform::battery_caps_t &Platform::getBatteryCaps(void) {
  static const battery_caps_t caps = {true, true, false, true};
  return caps;
}

Platform::battery_t Platform::readBattery(void) {
  return {80, 4000, 0, false};
}

Platform::battery_sample_t Platform::sampleBattery(void) {
  // The sim battery reading is fixed, so the smoothed means just mirror it.
  const battery_t battery = readBattery();
  m_BatterySample.battery = battery;
  m_BatterySample.meanLevel = battery.level;
  m_BatterySample.meanVoltage = battery.voltage;
  m_BatterySample.meanCurrent = battery.current;
  m_BatterySample.displayLevel = battery.level;
  return m_BatterySample;
}

uint16_t Platform::getBatteryCapacity(void) {
  return 250;
}

uint32_t Platform::getBatteryFailCount(void) {
  return 0;
}

}  // namespace Furble
