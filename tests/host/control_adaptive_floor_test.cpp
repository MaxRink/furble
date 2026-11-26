// Host regression for the adaptive transmit floors while the WiFi station is up.
//
// BLE and WiFi share one 2.4 GHz front end. The adaptive policy is open loop
// against its own actuator: it measures the camera's RSSI, so it never sees the
// interference a WiFi burst causes on our transmit side. Stepping to the bottom
// of the range while the station is connected therefore buys a busy-channel
// retry rather than an energy saving (plans/96 D2, W-18 in
// docs/power/wifi-coex.md, plans/97-followups.md Group 1 batch 7).
//
// The policy reads a probe the WiFi service installs, so this drives the probe
// directly and asserts the level the radio is asked for. Red-first evidence:
// with no floor in sampleAdaptivePower() the connected-station arm steps down to
// the bottom exactly like the idle one.
//
// What a device must confirm: with the station connected and the link quiet,
// the shutter median and worst-case latency and the missed-event count at the
// floored level, against the same session with the floor off. The host can only
// pin the decision, not the radio outcome.

#include <chrono>
#include <cstdint>
#include <functional>
#include <iostream>
#include <memory>
#include <thread>

#include "Camera.h"
#include "Device.h"
#include "FujifilmBasic.h"
#include "FujifilmVirtualCamera.h"
#include "NimBLEDevice.h"

#include "FurbleControl.h"
#include "FurbleSettings.h"

const char *LOG_TAG = "furble-host";

namespace {

using Furble::Control;

int g_Failures = 0;

bool check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "  FAIL: " << message << '\n';
    g_Failures++;
  }
  return condition;
}

void ensureControlTask() {
  static bool started = false;
  if (started) {
    return;
  }
  started = true;
  // Through the shim, exactly as main() starts it on device: the shim owns the
  // thread and furbleHostStopTasks() joins it before this process exits.
  xTaskCreate(control_task, "control", 8192, &Control::getInstance(), 4, nullptr);
}

bool stationConnected = false;

bool stationProbe(void) {
  return stationConnected;
}

// One sample per call. The cadence is a compile-time host value, so the small
// sleep is only what keeps the tick counter ahead of the sample interval.
void sample(Control &control, unsigned count) {
  for (unsigned i = 0; i < count; i++) {
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    control.sampleAdaptivePowerForTest();
  }
}

void run(Furble::Host::FujifilmVirtualCamera &peer) {
  NimBLEDevice::resetMock();
  Furble::Device::init(Furble::TxPower::P3);
  ensureControlTask();

  NimBLEDevice::setMockPeer(&peer);
  const NimBLEAdvertisedDevice advertisement = peer.advertisement();
  auto camera = std::make_shared<Furble::FujifilmBasic>(&advertisement);

  auto &control = Control::getInstance();
  control.addActive(camera);
  if (!check(control.getTargetCount() == 1, "the camera becomes an active target")) {
    return;
  }
  if (!check(camera->connect(Furble::TxPower::P3, 1000), "the camera connects")) {
    return;
  }

  // The shim's adaptive flag is off on a fresh device, so this is the only place
  // the policy turns on.
  Furble::hostAdaptiveEnabled() = true;
  Control::setWifiConnectedProbe(nullptr);
  stationConnected = false;

  // Cap the range at +9 dBm so there is room to step down twice. The Fujifilm
  // peer reports -42 dBm, which is above the step-down threshold.
  control.setPower(ESP_PWR_LVL_P9);
  check(NimBLEDevice::getPower() == 9, "the cap reaches the radio");

  sample(control, 3);
  check(NimBLEDevice::getPower() == 6, "an idle station lets the policy step down once");
  sample(control, 3);
  check(NimBLEDevice::getPower() == 3, "an idle station lets the policy reach the bottom step");

  // The station comes up. A strong RSSI never asks for a step up, so the clamp
  // has to act on its own rather than wait for a decision that never comes.
  Control::setWifiConnectedProbe(&stationProbe);
  stationConnected = true;
  sample(control, 1);
  check(NimBLEDevice::getPower() == 6, "a connected station raises a link that is below the floor");

  sample(control, 6);
  check(NimBLEDevice::getPower() == 6, "the floor holds one step above the bottom");

  // The station drops: the policy is free to return to the bottom.
  stationConnected = false;
  sample(control, 3);
  check(NimBLEDevice::getPower() == 3, "the floor is released when the station goes away");

  // The floor never overrides the user's own cap. With a +3 dBm cap there is no
  // step to raise, and asking the radio for more than the setting allows is what
  // the cap exists to prevent.
  control.setPower(ESP_PWR_LVL_P3);
  stationConnected = true;
  sample(control, 4);
  check(NimBLEDevice::getPower() == 3, "the floor cannot exceed the configured cap");

  Control::setWifiConnectedProbe(nullptr);
  stationConnected = false;
  control.disconnect();
  NimBLEDevice::resetMock();
}

}  // namespace

int main() {
  // The peer outlives the joined tasks: a target task still reading a camera
  // whose peer was already freed is the very use-after-free this harness must
  // not manufacture.
  Furble::Host::FujifilmVirtualCamera peer;
  // Stop and join every shim task before this scope ends, so no firmware task
  // is still running when static destruction frees what it reads.
  FurbleHostTaskScope taskScope;

  run(peer);

  const int status = (g_Failures == 0) ? 0 : 1;
  if (status == 0) {
    std::cout << "control-adaptive-floor: PASS\n";
  } else {
    std::cerr << "control-adaptive-floor: " << g_Failures << " FAILED\n";
  }
  return status;
}
