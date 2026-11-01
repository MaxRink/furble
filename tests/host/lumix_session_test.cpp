// LUMIX session handshake regression.
//
// The official app sends Confirm 01, waits for the control readiness
// notification, then Start 02. A bare ShutterOn 04 is refused by a body that
// requires the session gate. 01 and 02 are never focus commands.

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "Device.h"
#include "Lumix.h"
#include "LumixVirtualCamera.h"
#include "NimBLEDevice.h"

const char *LOG_TAG = "lumix-session-test";

namespace {

bool check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
  }
  return condition;
}

bool hasCommand(const std::vector<Furble::Host::LumixVirtualCamera::Write> &writes,
                uint8_t command) {
  for (const auto &write : writes) {
    if (write.payload.size() == 1 && write.payload[0] == command) {
      return true;
    }
  }
  return false;
}

}  // namespace

int main() {
  NimBLEDevice::resetMock();
  Furble::Device::init(ESP_PWR_LVL_P3);

  Furble::Host::LumixVirtualCamera peer;
  NimBLEDevice::setMockPeer(&peer);
  const auto advertisement = peer.advertisement();
  Furble::Lumix camera(&advertisement);

  bool passed = check(camera.connect(ESP_PWR_LVL_P3, 2000),
                      "LUMIX connect completes the Confirm and Start handshake");
  passed &= check(peer.confirmed(), "LUMIX Confirm 01 reaches the camera");
  passed &= check(peer.sessionStarted(), "LUMIX Start 02 starts the session");

  const auto &connectWrites = peer.writes();
  passed &= check(hasCommand(connectWrites, 0x01), "LUMIX connect writes Confirm 01");
  passed &= check(hasCommand(connectWrites, 0x02), "LUMIX connect writes Start 02");

  peer.clearEvents();
  camera.focusPress();
  camera.focusRelease();
  passed &=
      check(peer.writes().empty(), "LUMIX focus press and release emit no session or focus write");

  peer.clearEvents();
  camera.shutterPress();
  passed &=
      check(peer.writes().size() == 1 && peer.writes()[0].payload == std::vector<uint8_t> {0x04},
            "LUMIX shutter press writes ShutterOn 04");
  passed &= check(peer.shutterHeld(), "LUMIX shutter press is accepted inside the session");

  camera.shutterRelease();
  passed &=
      check(peer.writes().size() == 2 && peer.writes()[1].payload == std::vector<uint8_t> {0x05},
            "LUMIX shutter release writes ShutterOff 05");

  const Furble::Camera::gps_t gps = {52.5, 13.4, 34.0, 8};
  const Furble::Camera::timesync_t time = {2026, 11, 1, 12, 34, 56, 0};
  camera.updateGeoData(gps, time);
  passed &= check(peer.lastGps().size() == 16, "LUMIX GPS packet is written");

  peer.clearEvents();
  camera.disconnect();
  passed &= check(peer.gpsStopped(), "LUMIX disconnect sends the GPS stop packet");
  passed &= check(hasCommand(peer.writes(), 0x03), "LUMIX disconnect writes Finish 03");
  passed &= check(peer.sessionFinished(), "LUMIX Finish 03 closes the session");

  NimBLEDevice::resetMock();
  if (!passed) {
    return 1;
  }
  std::printf("LUMIX session handshake passed\n");
  return 0;
}
