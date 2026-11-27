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
// A release is never the command a full queue discards, and a queued release is
// never the command it displaces. The second half is the newer rule (plan 546):
// the head of the queue can be a release, not only a request, and the two
// release classes are not interchangeable on Lumix, Sony, Canon EOS Smart,
// Nikon Remote, Ricoh and DJI, where a focus release leaves the shutter held.
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
//   7. park again and build the shape the old eviction rule got wrong: a
//      delivered press, a shutter release behind it at the head of the queue,
//      and seven presses behind that. A focus release arriving at that full
//      queue must be refused, and it must leave the queued shutter release
//      alone. The peer counts accepted shutter writes, so the released camera
//      is visible even where both release classes write the same frame: seven
//      presses and the shutter release are 16 writes, seven presses and the
//      focus release would be 15,
//   8. park once more, queue a teardown, and prove that no command is accepted
//      once the teardown is queued. That check is deliberately made on a queue
//      with seven free slots, because a full queue refuses a press on its own
//      and would prove nothing about the disconnect rule.
//
// Mutation proof, each step failing for its own reason:
//   - the pre-fix sendCommand() (a release is dropped when the queue is full)
//     fails step 3 (pdFALSE) and step 5 (lastShutterValue stays
//     kShutterFullPress, which is the shutter held),
//   - the plan 546 shape (the head is received and re-sent without checking
//     what it is) passes steps 3 and 4 and fails step 7, 15 writes instead of
//     16,
//   - disabling the m_DisconnectPending check in Target::sendCommand() fails
//     step 8, and both of its checks, because the queue there has space.

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

  // Step 7: a release must not evict a queued release. Park again and build the
  // shape the old rule got wrong: a delivered press, the shutter release that
  // answers it at the head of the queue, and seven presses behind that. A focus
  // release arriving at that full queue must be refused, and the queued shutter
  // release must still reach the camera. The write count is what proves the
  // release survived, because Fujifilm's focus release writes the same frame as
  // a shutter release: seven presses are 14 writes and the shutter release is 2
  // more, 16, where seven presses and the focus release would be 15. The last
  // value then pins where the release went: it kept its place at the head, so
  // the drain ends on the last press, and a rule that appended it to the tail
  // would end on a release with the same count.
  Furble::TestSync::armBarrier("target_command_complete", 15000);
  check(control.sendCommand(Control::CMD_SHUTTER_PRESS) == pdTRUE, "press before the second park");
  check(Furble::TestSync::awaitArrival("target_command_complete", 5000),
        "target task parked again after delivering the press");
  const size_t evictionBaseline = peer.shutterAccepted();

  check(control.sendCommand(Control::CMD_SHUTTER_RELEASE) == pdTRUE,
        "the shutter release behind the press is queued");
  check(fillQueue(Control::CMD_SHUTTER_PRESS, 7) == 7, "seven presses fill the queue behind it");
  check(control.sendCommand(Control::CMD_FOCUS_RELEASE) == pdFALSE,
        "a focus release is refused rather than evicting the queued shutter release");

  Furble::TestSync::release("target_command_complete");
  check(control.waitForCameraCommands(10000), "the target queue drains after the refusal");
  const size_t evictionWrites = peer.shutterAccepted() - evictionBaseline;
  check(evictionWrites == 16,
        "the queued shutter release still reaches the camera: expected 16 accepted shutter "
        "writes after the press, saw "
            + std::to_string(evictionWrites));
  // The push-back also has to keep the release where it was, at the head, ahead
  // of the seven presses behind it. The last value the peer took is therefore a
  // press: a rule that appended the release at the tail instead would end this
  // drain on a release and pass the count above unchanged, which is the one
  // property the count cannot see.
  check(peer.lastShutterValue() == FujifilmVirtualCamera::kShutterFullPress,
        "the queued shutter release kept its place ahead of the presses behind it");

  // Step 8: once the teardown is queued no command is accepted. The queue holds
  // exactly one item at that point, the CMD_DISCONNECT the drain left at the
  // front, so seven of its eight slots are free: a press would be appended and a
  // release would be appended behind it. A refusal here therefore proves the
  // pending teardown and nothing else, which a full queue or an eviction could
  // not do. disconnect_teardown_queued is the point that fixes the moment, so
  // the first attempt is the one that has to be refused.
  Furble::TestSync::armBarrier("target_command_complete", 15000);
  check(control.sendCommand(Control::CMD_SHUTTER_PRESS) == pdTRUE, "press before the teardown");
  check(Furble::TestSync::awaitArrival("target_command_complete", 5000),
        "target task parked before the teardown");

  std::atomic<bool> disconnected {false};
  Furble::TestSync::armBarrier("disconnect_teardown_queued", 15000);
  std::thread disconnector([&control, &disconnected] { disconnected = control.disconnect(); });
  check(Furble::TestSync::awaitArrival("disconnect_teardown_queued", 5000),
        "the teardown is queued at the target");

  check(control.sendCommand(Control::CMD_SHUTTER_PRESS) == pdFALSE,
        "a press is refused while the queue has free slots");
  check(control.sendCommand(Control::CMD_SHUTTER_RELEASE) == pdFALSE,
        "a release is refused while the queue has free slots");

  Furble::TestSync::release("disconnect_teardown_queued");
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
