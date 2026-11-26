// Simulated wall clock and the modelled RTC chip.
//
// Furble reads wall time from two places. TimeKeeper owns the clock and, on
// device, seeds it from the RTC chip through M5.Rtc. Consumers that only need
// a date, UI::Intervalometer among them, call std::time() directly. The host
// has no I2C bus, so M5.Rtc can never come up in the SDL build and this file
// stands in for the chip at both places:
//
// * time() returns the modelled chip's register reading, which is what
//   settimeofday() would have installed on a board after an RTC read. With no
//   chip modelled it stays on the host clock, so every existing scenario and
//   capture run is unchanged.
// * furble_sim_system_time_us()/furble_sim_set_system_time_us() are the
//   production simulator hooks TimeKeeper already used for the system clock.
// * furble_sim_rtc_*() are the chip itself: presence, the backup cell, one
//   calendar read and one write.
//
// The model lives in sim/hardware_model.cpp; this file is only the seam.

#include <time.h>

#include <cstdint>

#include "hardware_model.h"

namespace {

uint64_t modelEpochUs(void) {
  const uint64_t seconds = Furble::Sim::hwRtcEpochSeconds();
  return seconds * 1000000ULL;
}

}  // namespace

// The system wall clock. Furble's TimeKeeper reads it through the hooks below
// and everything else, including UI::Intervalometer, through std::time().
extern "C" time_t time(time_t *result) {
  time_t value = 0;
  if (Furble::Sim::hwRtcPresent()) {
    value = static_cast<time_t>(Furble::Sim::hwRtcEpochSeconds());
  } else {
    // No chip modelled: keep the host clock, as the simulator always did.
    timespec now = {};
    if (clock_gettime(CLOCK_REALTIME, &now) == 0) {
      value = static_cast<time_t>(now.tv_sec);
    }
  }
  if (result != nullptr) {
    *result = value;
  }
  return value;
}

// TimeKeeper's system clock. The host has no settimeofday() effect other
// boards would see, so the model owns the value.
extern "C" uint64_t furble_sim_system_time_us(void) {
  return modelEpochUs();
}

extern "C" void furble_sim_set_system_time_us(uint64_t epochUs) {
  Furble::Sim::hwRtcWrite(epochUs);
}

extern "C" bool furble_sim_rtc_present(void) {
  return Furble::Sim::hwRtcPresent();
}

extern "C" bool furble_sim_rtc_battery_backed(void) {
  return Furble::Sim::hwRtcBatteryBacked();
}

extern "C" bool furble_sim_rtc_read(uint64_t *epoch_us, bool *volt_low) {
  if (epoch_us == nullptr || volt_low == nullptr) {
    return false;
  }
  return Furble::Sim::hwRtcRead(*epoch_us, *volt_low);
}

extern "C" void furble_sim_rtc_write(uint64_t epoch_us) {
  Furble::Sim::hwRtcWrite(epoch_us);
}
