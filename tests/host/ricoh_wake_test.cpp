#include <cstdint>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include <esp_timer.h>

#include "Device.h"
#include "NimBLEDevice.h"
#include "Ricoh.h"
#include "RicohVirtualCamera.h"

const char *LOG_TAG = "ricoh-wake-test";

namespace {

// Host build values, set by tests/host/CMakeLists.txt so a bounded wait costs
// milliseconds instead of seconds. The firmware keeps the Ricoh.h defaults.
constexpr uint32_t WAKE_PROBE_TIMEOUT_MS = FURBLE_RICOH_WAKE_PROBE_TIMEOUT_MS;
// Room for scheduling noise on a loaded CI host. The lower bound (the full
// budget is spent before a refusal) is the assertion that matters, so the upper
// bound only has to prove the wait is bounded at all.
constexpr uint32_t WAKE_SLACK_MS = 750;

constexpr uint8_t MODE_CAPTURE = 0x00;
constexpr uint8_t MODE_PLAYBACK = 0x01;
constexpr uint8_t MODE_OTHER = 0x03;
constexpr uint8_t MODE_POWER_OFF_TRANSFER = 0x04;

using Peer = Furble::Host::RicohVirtualCamera;

// One address per peer, and no bond carried in from the previous case. The mock
// bond store is process-wide, so a bond left behind would make the next body's
// security handshake fail before the case under test starts.
Peer::Config makeConfig(uint64_t address) {
  NimBLEDevice::setBonded(false);
  Peer::Config config;
  config.address = NimBLEAddress(address, 0);
  return config;
}

uint32_t nowMs(void) {
  return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
}

bool check(bool condition, const char *message) {
  if (!condition)
    std::cerr << "FAIL: " << message << '\n';
  return condition;
}

// Capture writes only. A wake repeats the connect path, which writes the
// paired-device identity, so the journal is not capture writes alone.
std::vector<Peer::Write> captureWrites(const std::vector<Peer::Write> &writes) {
  const std::string flavor = Peer::shootingFlavorCharacteristicUUID().toString();
  const std::string operation = Peer::operationRequestCharacteristicUUID().toString();
  std::vector<Peer::Write> filtered;
  for (const auto &write : writes) {
    if ((write.characteristic == flavor) || (write.characteristic == operation)) {
      filtered.push_back(write);
    }
  }
  return filtered;
}

bool checkCaptureWrites(const std::vector<Peer::Write> &writes, const char *label) {
  const std::string flavor = Peer::shootingFlavorCharacteristicUUID().toString();
  const std::string operation = Peer::operationRequestCharacteristicUUID().toString();
  const std::vector<Peer::Write> capture = captureWrites(writes);
  bool passed = check(capture.size() == 2, label);
  if (capture.size() == 2) {
    passed &=
        check(capture[0].characteristic == flavor && capture[0].payload == std::vector<uint8_t> {0},
              "wake then capture selects the immediate shooting flavor first");
    passed &= check(
        capture[1].characteristic == operation && capture[1].payload == std::vector<uint8_t> {1, 1},
        "wake then capture starts OperationRequest with AF second");
  }
  return passed;
}

bool checkWakeNotification(const Peer &peer) {
  bool operation_mode = false;
  bool camera_power = false;
  const std::string operation = Peer::cameraServiceUUID().toString() + "/"
                                + Peer::operationModeCharacteristicUUID().toString();
  const std::string power =
      Peer::cameraServiceUUID().toString() + "/" + Peer::powerCharacteristicUUID().toString();
  for (const auto &notification : peer.notifications()) {
    const std::string key = notification.service + "/" + notification.characteristic;
    if ((key == operation) && notification.payload == std::vector<uint8_t> {MODE_CAPTURE}) {
      operation_mode = true;
    }
    if ((key == power) && notification.payload == std::vector<uint8_t> {0x01}) {
      camera_power = true;
    }
  }
  return check(operation_mode, "wake announces OperationMode CAPTURE on the notification stream")
         && check(camera_power, "wake announces CameraPower ON on the notification stream");
}

}  // namespace

int main() {
  NimBLEDevice::resetMock();
  Furble::Device::init(Furble::TxPower::P3);

  bool passed = true;

  // Successful wake. The body is in BLE standby when the capture is requested:
  // CameraPower ON and OperationMode BLE_STARTUP on a held link. Dropping and
  // re-establishing the link is the wake the plan records, so the shutter path
  // must relink, wait for a fresh CAPTURE read, and only then write.
  {
    Peer peer(makeConfig(0x223344556677ULL));
    peer.setSleepingBody(true, 150);
    NimBLEDevice::setMockPeer(&peer);
    const auto advertisement = peer.advertisement();
    Furble::Ricoh camera(&advertisement);
    if (!check(camera.connect(Furble::TxPower::P3, 1000),
               "wake test reaches the connected production path")) {
      return 1;
    }
    passed &= check(peer.sleeping(), "virtual GR IV starts in BLE standby");

    peer.clearEvents();
    const uint32_t started = nowMs();
    camera.shutterPress();
    const uint32_t elapsed = nowMs() - started;

    passed &= check(peer.reconnectCount() == 1, "a sleeping body is woken by one reconnect");
    passed &= check(peer.captureRequestsWhileAsleep() == 0,
                    "no capture write is attempted while the body is still in BLE standby");
    passed &= checkCaptureWrites(peer.writes(), "wake emits flavor then OperationRequest in order");
    passed &= check(!peer.sleeping(), "the body reports CAPTURE after the wake");
    passed &= check(peer.operationModeReadCount() >= 3,
                    "the wake polls a fresh OperationMode read instead of reading once");
    passed &= check(elapsed < (WAKE_PROBE_TIMEOUT_MS + WAKE_SLACK_MS),
                    "a successful wake completes inside the bounded wait");
    passed &= check(camera.isConnected(), "the link is up after a successful wake");
    passed &= checkWakeNotification(peer);

    // The body is awake now, so the next press captures on the held link with
    // no second relink.
    peer.clearEvents();
    camera.shutterPress();
    passed &= check(peer.reconnectCount() == 1, "an awake body is not woken again");
    passed &= checkCaptureWrites(peer.writes(), "a second press captures on the held link");

    camera.disconnect();
    NimBLEDevice::setMockPeer(nullptr);
  }

  // Wake timeout. This body never leaves BLE standby, so the bounded wait must
  // expire, the capture writes must not happen, and the camera must stay usable
  // rather than frozen or dropped.
  {
    Peer peer(makeConfig(0x223344556688ULL));
    peer.setSleepingBody(false, 0);
    NimBLEDevice::setMockPeer(&peer);
    const auto advertisement = peer.advertisement();
    Furble::Ricoh camera(&advertisement);
    if (!check(camera.connect(Furble::TxPower::P3, 1000),
               "wake timeout test reaches the connected production path")) {
      return 1;
    }

    peer.clearEvents();
    const uint32_t started = nowMs();
    camera.shutterPress();
    const uint32_t elapsed = nowMs() - started;

    passed &= check(peer.reconnectCount() == 1, "the wake still relinks a body that will not wake");
    passed &= check(peer.writes().empty() || captureWrites(peer.writes()).empty(),
                    "a wake timeout writes no capture command");
    passed &= check(peer.captureRequestsWhileAsleep() == 0,
                    "a wake timeout never writes a capture into BLE standby");
    passed &= check(elapsed >= WAKE_PROBE_TIMEOUT_MS,
                    "the wake waits the full probe budget before refusing");
    passed &= check(elapsed < (WAKE_PROBE_TIMEOUT_MS + WAKE_SLACK_MS),
                    "the wake probe budget is bounded");
    passed &= check(peer.sleeping(), "the body stays in BLE standby when the wake fails");
    passed &=
        check(camera.isConnected(), "a failed wake leaves the relinked connection up, not frozen");

    // Phase 1 refusal still holds on the relinked connection. A second press
    // retries the wake on that same connection instead of stacking links.
    peer.clearEvents();
    camera.shutterPress();
    passed &=
        check(captureWrites(peer.writes()).empty(), "phase 1 refusal holds after a failed wake");
    passed &= check(peer.reconnectCount() == 2,
                    "a repeated press retries the wake on the relinked connection");

    // The camera recovers on the held connection: no further relink, and the
    // capture writes go out.
    peer.setOperationMode(MODE_CAPTURE);
    peer.clearEvents();
    camera.shutterPress();
    passed &= check(peer.reconnectCount() == 2, "recovery reuses the relinked connection");
    passed &= checkCaptureWrites(peer.writes(), "capture works once the body reports CAPTURE");

    camera.disconnect();
    NimBLEDevice::setMockPeer(nullptr);
  }

  // A wake whose reconnect fails must fail closed: no capture write, no leaked
  // client, and the camera reported disconnected exactly as a failed connect
  // leaves it. This is the RELINK -> FAILED edge.
  {
    Peer peer(makeConfig(0x22334455bb00ULL));
    peer.setSleepingBody(true, 0);
    NimBLEDevice::setMockPeer(&peer);
    const auto advertisement = peer.advertisement();
    Furble::Ricoh camera(&advertisement);
    if (!check(camera.connect(Furble::TxPower::P3, 1000),
               "relink failure test reaches the connected production path")) {
      return 1;
    }
    peer.clearEvents();
    NimBLEDevice::setConnectShouldFail(true);
    camera.shutterPress();
    NimBLEDevice::setConnectShouldFail(false);
    passed &=
        check(captureWrites(peer.writes()).empty(), "a failed reconnect writes no capture command");
    passed &= check(peer.captureRequestsWhileAsleep() == 0,
                    "a failed reconnect never writes a capture into standby");
    passed &= check(!camera.isConnected(),
                    "a failed reconnect reclaims the client instead of keeping a stale link");
    passed &= check(peer.sleeping(), "a failed reconnect leaves the body in BLE standby");
    camera.disconnect();
    NimBLEDevice::setMockPeer(nullptr);
  }

  // A wake that runs while the user cancelled must stop in the probe instead of
  // burning the whole budget (plan 148 cancel contract), and must still refuse.
  {
    Peer peer(makeConfig(0x22334455cc00ULL));
    peer.setSleepingBody(false, 0);
    NimBLEDevice::setMockPeer(&peer);
    const auto advertisement = peer.advertisement();
    Furble::Ricoh camera(&advertisement);
    if (!check(camera.connect(Furble::TxPower::P3, 1000),
               "cancelled wake test reaches the connected production path")) {
      return 1;
    }
    peer.clearEvents();
    camera.cancelConnect();
    const uint32_t started = nowMs();
    camera.shutterPress();
    const uint32_t elapsed = nowMs() - started;
    passed &= check(captureWrites(peer.writes()).empty(), "a cancelled wake writes no capture");
    passed &= check(elapsed < (WAKE_PROBE_TIMEOUT_MS / 2),
                    "a cancelled wake stops in the probe instead of waiting out the budget");
    passed &= check(peer.reconnectCount() == 1, "a cancelled wake still relinked once");
    camera.clearConnectCancel();
    camera.disconnect();
    NimBLEDevice::setMockPeer(nullptr);
  }

  // Only BLE standby is a wake trigger. A body in another operation mode is a
  // refusal, and it must not be relinked.
  {
    const std::pair<uint8_t, const char *> modes[] = {
        {MODE_PLAYBACK,           "PLAYBACK"          },
        {MODE_OTHER,              "OTHER"             },
        {MODE_POWER_OFF_TRANSFER, "POWER_OFF_TRANSFER"},
    };
    for (const auto &mode : modes) {
      Peer peer(makeConfig(0x223344556699ULL + mode.first));
      NimBLEDevice::setMockPeer(&peer);
      const auto advertisement = peer.advertisement();
      Furble::Ricoh camera(&advertisement);
      if (!check(camera.connect(Furble::TxPower::P3, 1000),
                 "mode refusal test reaches the connected production path")) {
        return 1;
      }
      peer.setOperationMode(mode.first);
      peer.clearEvents();
      const uint32_t started = nowMs();
      camera.shutterPress();
      const uint32_t elapsed = nowMs() - started;
      passed &= check(peer.writes().empty(), "non-standby mode still refuses with zero writes");
      passed &= check(peer.reconnectCount() == 0,
                      "non-standby mode does not relink the camera to wake it");
      passed &=
          check(peer.captureRequestsWhileAsleep() == 0, "non-standby mode never writes a capture");
      passed &= check(elapsed < WAKE_PROBE_TIMEOUT_MS,
                      "a non-standby refusal returns without waiting for a wake");
      std::cout << "  (" << mode.second << " refused, " << elapsed << " ms)\n";
      camera.disconnect();
      NimBLEDevice::setMockPeer(nullptr);
    }
  }

  // A sleeping body whose OperationMode read fails is also a refusal: the wake
  // cannot confirm CAPTURE, so it must not write a capture.
  {
    Peer peer(makeConfig(0x22334455aa00ULL));
    peer.setSleepingBody(true, 0);
    NimBLEDevice::setMockPeer(&peer);
    const auto advertisement = peer.advertisement();
    Furble::Ricoh camera(&advertisement);
    if (!check(camera.connect(Furble::TxPower::P3, 1000),
               "read failure test reaches the connected production path")) {
      return 1;
    }
    peer.setOperationModeReadFails(true);
    peer.clearEvents();
    camera.shutterPress();
    passed &= check(captureWrites(peer.writes()).empty(),
                    "a failed OperationMode read refuses even on a waking body");
    passed &= check(peer.reconnectCount() == 0,
                    "a failed OperationMode read does not relink in the dark");
    passed &= check(peer.captureRequestsWhileAsleep() == 0,
                    "a failed OperationMode read never writes a capture");
    camera.disconnect();
    NimBLEDevice::setMockPeer(nullptr);
  }

  return passed ? 0 : 1;
}
