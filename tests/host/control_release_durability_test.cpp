// Shutter and focus release durability across a full per-target command queue
// (plan 374).
//
// Control::Target::sendCommand() posts a camera command with a zero timeout, so
// a full per-target queue used to drop it and only log "Failed to send command
// to target." For a shutter or focus RELEASE that is the wrong command to lose:
// its PRESS has already been delivered, the camera is holding the button, and
// nothing times the hold out. Not the firmware (shutterRelease() has exactly
// one caller, the target task) and not a vendor driver (Fujifilm, Canon Remote
// and Smart, Nikon Remote, Sony and Lumix write a release byte pair or a
// release opcode; Ricoh and DJI ignore release because their press is a single
// complete capture). Two of the five producers already retry a rejected release
// for exactly this reason: include/FurbleMQTT.h says "A rejected shutter or
// focus release leaves the camera held".
//
// A target task blocks in gattWrite() with no local timeout while the peer
// stalls, and the UI loop (priority 1) keeps producing behind it. Eight slots
// is under a second of that at the intervalometer's 100 ms tick, so the queue
// does fill in the field. This test forces the same state deterministically
// instead of waiting for a stall: the "target_command_complete" sync point
// parks the target task after a delivered press, so every later command has to
// wait in the queue behind it.
//
// Steps:
//   1. connect, reach ACTIVE, and park the target task after one shutter press,
//      so the camera is holding a full press and nothing can drain the queue,
//   2. fill the queue with 8 more presses: all accepted, and the 9th refused,
//      which pins that the request path is still bounded by the queue,
//   3. send the shutter release while the queue is full. It must be accepted,
//      and it must displace a waiting request rather than grow the queue,
//   4. send the focus release in the same full state: the release set has two
//      members and both must survive,
//   5. unpark the target task and let it drain. The last shutter value the peer
//      received must be the release, not the press,
//   6. repeat a plain press and release on an empty queue, so the ordinary path
//      is pinned as unchanged,
//   7. park once more, queue a teardown, and prove a release can no longer be
//      accepted: it must not be able to evict the undroppable CMD_DISCONNECT.
//
// Mutation proof: with the pre-fix sendCommand() step 3 fails (pdFALSE and a
// logged drop) and step 5 fails (lastShutterValue() stays kShutterFullPress,
// which is the shutter held). Restoring the old `if (ret != pdTRUE)` block
// alone fails both.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include "Camera.h"
#include "Device.h"
#include "FujifilmBasic.h"
#include "FujifilmVirtualCamera.h"
#include "NimBLEDevice.h"

#include "FurbleControl.h"
#include "FurbleSettings.h"
#include "TestSyncController.h"

const char *LOG_TAG = "furble-release-durability";

namespace {

using Furble::Control;
using Furble::Host::FujifilmVirtualCamera;

int g_Failures = 0;

bool check(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "  FAIL: " << message << '\n';
    g_Failures++;
  }
  return condition;
}

uint32_t nowMs() {
  return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::steady_clock::now().time_since_epoch())
                                   .count());
}

bool waitForState(Control::state_t want, uint32_t timeout_ms) {
  auto &control = Control::getInstance();
  const uint32_t start = nowMs();
  while ((nowMs() - start) < timeout_ms) {
    if (control.getState() == want) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  return control.getState() == want;
}

// Fill the per-target queue with `count` requests. Every send must be accepted
// while the target task is parked, because nothing is draining it.
int fillQueue(Control::cmd_t cmd, int count) {
  auto &control = Control::getInstance();
  int accepted = 0;
  for (int i = 0; i < count; i++) {
    if (control.sendCommand(cmd) == pdTRUE) {
      accepted++;
    }
  }
  return accepted;
}

}  // namespace

int main(void) {
  FurbleHostTaskScope taskScope;

  NimBLEDevice::resetMock();
  Furble::TestSync::reset();
  Furble::Device::init(Furble::TxPower::P3);
  Furble::Settings::setBool(Furble::Settings::SLEEP_CONN, false);
  Furble::Settings::setBool(Furble::Settings::TX_ADAPTIVE, false);
  Furble::Settings::setBool(Furble::Settings::RECON_BACKOFF, false);
  Furble::Settings::setBool(Furble::Settings::CONN_SAVER, false);

  auto &control = Control::getInstance();
  xTaskCreate(control_task, "control", 8192, &control, 4, nullptr);

  FujifilmVirtualCamera peer;
  NimBLEDevice::setMockPeer(&peer);
  const NimBLEAdvertisedDevice advertisement = peer.advertisement();
  auto camera = std::make_shared<Furble::FujifilmBasic>(&advertisement);

  control.addActive(camera);
  control.connectAll(false);
  check(waitForState(Control::STATE_ACTIVE, 8000), "session reaches active");
  check(control.getConnectedTargetCount() == 1, "one connected target");
  check(peer.connected(), "peer link is up");

  // Step 1: park the target task after a delivered press. The park is the
  // stalled gattWrite() the field hits, without the wait.
  Furble::TestSync::armBarrier("target_command_complete", 15000);
  check(control.sendCommand(Control::CMD_SHUTTER_PRESS) == pdTRUE,
        "the first press is accepted by the empty queue");
  check(Furble::TestSync::awaitArrival("target_command_complete", 5000),
        "target task parked after delivering the press");
  check(peer.lastShutterValue() == FujifilmVirtualCamera::kShutterFullPress,
        "the camera is holding a full press while the task is parked");

  // Step 2: fill the queue behind the parked task. The length is
  // Target::m_QueueLength, 8.
  const int filled = fillQueue(Control::CMD_SHUTTER_PRESS, 8);
  check(filled == 8, "eight presses fill the per-target queue");
  check(control.sendCommand(Control::CMD_SHUTTER_PRESS) == pdFALSE,
        "a request is still bounded by the full queue");

  // Step 3: the release behind that press must get in. This is the defect.
  check(control.sendCommand(Control::CMD_SHUTTER_RELEASE) == pdTRUE,
        "shutter release is accepted by a full queue");

  // Step 4: the focus release is the other member of the release set, and the
  // queue the shutter release just refilled is full again.
  check(control.sendCommand(Control::CMD_FOCUS_RELEASE) == pdTRUE,
        "focus release is accepted by a full queue");

  // Step 5: drain. The peer's last shutter value is the camera's final button
  // state, so it must be the release.
  Furble::TestSync::release("target_command_complete");
  check(control.waitForCameraCommands(10000), "the target queue drains after the releases");
  check(peer.lastShutterValue() == FujifilmVirtualCamera::kShutterRelease,
        "the camera ends with the shutter released");

  // Step 6: the ordinary path is unchanged: a press and release pair on an
  // empty queue reaches the camera in order and ends released.
  check(control.sendCommand(Control::CMD_SHUTTER_PRESS) == pdTRUE, "press on an empty queue");
  check(control.waitForCameraCommands(5000), "the press drains");
  check(peer.lastShutterValue() == FujifilmVirtualCamera::kShutterFullPress,
        "the camera holds the press");
  check(control.sendCommand(Control::CMD_SHUTTER_RELEASE) == pdTRUE, "release on an empty queue");
  check(control.waitForCameraCommands(5000), "the release drains");
  check(peer.lastShutterValue() == FujifilmVirtualCamera::kShutterRelease, "the camera releases");

  // Step 7: the release rule must not be able to evict a queued teardown.
  // CMD_DISCONNECT is the one command the queue cannot lose: the target task
  // exits on it, and disconnect() would then wait out its cap. Once the
  // teardown is queued every other command is refused, so a release can never
  // take its slot.
  Furble::TestSync::armBarrier("target_command_complete", 15000);
  check(control.sendCommand(Control::CMD_SHUTTER_PRESS) == pdTRUE, "press before the teardown");
  check(Furble::TestSync::awaitArrival("target_command_complete", 5000),
        "target task parked before the teardown");

  std::atomic<bool> disconnected {false};
  std::thread disconnector([&control, &disconnected] { disconnected = control.disconnect(); });

  // Wait for the teardown to be queued. A press is refused at that point, but a
  // full queue would refuse it too, so the release below is what separates the
  // two: with the teardown queued and no teardown rule, it would be accepted by
  // evicting the disconnect.
  bool pressRefused = false;
  const uint32_t refusalStart = nowMs();
  while (!pressRefused && ((nowMs() - refusalStart) < 5000)) {
    pressRefused = control.sendCommand(Control::CMD_SHUTTER_PRESS) == pdFALSE;
    if (!pressRefused) {
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
  }
  check(pressRefused, "a press is refused once the teardown is queued");
  check(control.sendCommand(Control::CMD_SHUTTER_RELEASE) == pdFALSE,
        "a release cannot evict a queued teardown");

  Furble::TestSync::release("target_command_complete");
  disconnector.join();
  check(disconnected.load(), "the teardown reaches the target task and completes");
  check(waitForState(Control::STATE_IDLE, 5000), "disconnect returns to idle");

  // A timed-out barrier means a parked thread was never released. That is a
  // test bug and must fail loudly, never hang the suite.
  check(!Furble::TestSync::anyTimedOut(), "no sync point timed out");

  if (g_Failures != 0) {
    std::cout << "control-release-durability: FAIL (" << g_Failures << " checks)\n";
    return 1;
  }
  std::cout << "control-release-durability: PASS\n";
  return 0;
}
