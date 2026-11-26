// Thread-sanitizer regression for the Control GPS fan-out.
//
// Control::updateGPS() runs on the task that drives the 1 Hz GPS service, the UI
// loop, and not on GPS::task(). It used to walk m_Targets with no lock, while
// addActive() reallocates that vector and disconnect() empties it under m_Mutex
// from the UI, companion and control tasks. A GPS fix landing on either
// mutation read the vector's own storage while it was being replaced, so the
// walk could follow a freed or reallocated buffer.
//
// This test is that overlap, run as a probe rather than an argument: one thread
// calls Control::updateGPS() in a loop while the main thread alternates
// addActive() and disconnect(). With the walk unlocked, ThreadSanitizer reports
// the vector mutation in addActive() against the iteration in updateGPS().
//
// run_tsan_race.sh is the fail-closed gate: every sanitizer warning and every
// non-zero child status fails, with no report-name filter or suppression.
//
// The test deliberately never reaches STATE_ACTIVE. The GPS values themselves
// (Target::m_GPS and m_Timesync, written here and read by the target task) are a
// separate pre-existing race that this fix does not touch, so keeping the
// session out of ACTIVE keeps the probe pointed at the vector.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <thread>

#include "Camera.h"
#include "Device.h"
#include "FauxNY.h"
#include "NimBLEDevice.h"

#include "FurbleControl.h"
#include "FurbleSettings.h"
#include "freertos/FreeRTOS.h"

const char *LOG_TAG = "furble-control-gps-race";

namespace {

using Furble::Control;

// Enough add/disconnect cycles that the GPS thread cannot miss every mutation
// window, and short enough to stay far inside the 180 s ctest budget.
constexpr int CYCLE_COUNT = 250;

}  // namespace

int main(void) {
  FurbleHostTaskScope taskScope;

  NimBLEDevice::resetMock();
  Furble::Device::init(Furble::TxPower::P3);
  Furble::Settings::setBool(Furble::Settings::SLEEP_CONN, false);
  Furble::Settings::setBool(Furble::Settings::TX_ADAPTIVE, false);
  Furble::Settings::setBool(Furble::Settings::RECON_BACKOFF, false);
  Furble::Settings::setBool(Furble::Settings::CONN_SAVER, false);

  auto &control = Control::getInstance();
  xTaskCreate(control_task, "control", 8192, &control, 4, nullptr);

  auto camera = std::make_shared<Furble::FauxNY>();

  std::atomic<bool> stop {false};
  std::atomic<uint64_t> fixes {0};
  std::atomic<bool> sawTarget {false};

  // The GPS service thread. A production fix arrives at 1 Hz from the UI loop;
  // this loops far faster so the overlap is certain rather than lucky, and
  // yields between fixes so it cannot starve the mutators.
  std::thread gps([&]() {
    Furble::Camera::gps_t gps = {};
    gps.latitude = -33.8688;
    gps.longitude = 151.2093;
    gps.altitude = 58.0;
    gps.satellites = 9;

    Furble::Camera::timesync_t timesync = {};
    timesync.year = 2026;
    timesync.month = 11;
    timesync.day = 14;
    timesync.hour = 3;
    timesync.minute = 7;
    timesync.second = 42;

    while (!stop.load(std::memory_order_acquire)) {
      control.updateGPS(gps, timesync);
      fixes.fetch_add(1, std::memory_order_relaxed);
      std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
  });

  bool passed = true;
  for (int cycle = 0; (cycle < CYCLE_COUNT) && passed; ++cycle) {
    control.addActive(camera);
    if (control.getTargetCount() == 0) {
      std::cerr << "addActive() did not add a target in cycle " << cycle << "\n";
      passed = false;
      break;
    }
    sawTarget.store(true, std::memory_order_release);

    if (!control.disconnect()) {
      std::cerr << "disconnect() did not complete in cycle " << cycle << "\n";
      passed = false;
      break;
    }
  }

  stop.store(true, std::memory_order_release);
  gps.join();

  // Join the firmware tasks before the doubles and Control's targets go away.
  furbleHostStopTasks();

  const uint64_t observedFixes = fixes.load(std::memory_order_relaxed);
  const bool observedTarget = sawTarget.load(std::memory_order_acquire);
  if (passed && !observedTarget) {
    std::cerr << "FAIL: no target was ever active, the probe exercised nothing\n";
    passed = false;
  }
  if (passed && (observedFixes == 0)) {
    std::cerr << "FAIL: no GPS fix reached Control::updateGPS()\n";
    passed = false;
  }
  if (!passed) {
    return 1;
  }

  // The sanitizer wrapper matches this line exactly.
  std::cout << "control-gps-race: PASS\n";
  std::cout << observedFixes << " fixes across " << CYCLE_COUNT << " add/disconnect cycles\n";
  return 0;
}
