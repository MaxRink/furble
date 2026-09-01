#include <algorithm>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <MockNimBLE.h>

#include <FujifilmVirtualCamera.h>
#include <RicohVirtualCamera.h>

#include "CameraList.h"
#include "FurbleControl.h"

#include "ble_sim.h"
#include "driver.h"
#include "power_profiler.h"

namespace Furble {
namespace Sim {
namespace {

// Advertising cadence of the virtual radio. Fast enough that a scan page fills
// within one scripted wait, slow enough that a long scan does not flood the
// production handoff queue (Scan::MAX_PENDING_RESULTS).
constexpr uint32_t ADVERTISE_INTERVAL_MS = 100;

// Address block for the simulated cameras. Distinct per peer so the saved
// camera index, the CameraList duplicate guard and Control::addActive() all
// see separate devices.
constexpr uint64_t FUJIFILM_A_ADDRESS = 0x112233445566ULL;
constexpr uint64_t FUJIFILM_B_ADDRESS = 0x112233445577ULL;
constexpr uint64_t RICOH_ADDRESS = 0x223344556677ULL;

struct VirtualPeer {
  std::string name;
  NimBLEAddress address = NimBLEAddress {};
  NimBLEAdvertisedDevice advertisement;
  std::unique_ptr<Host::FujifilmVirtualCamera> fujifilm;
  std::unique_ptr<Host::RicohVirtualCamera> ricoh;
};

std::mutex peersMutex;
std::vector<std::unique_ptr<VirtualPeer>> peers;
size_t advertisementCount = 0;
bool radioRunning = false;

// Camera command counters. The simulator counts the commands that actually
// reach a camera task so a scenario can assert the shutter and focus paths
// fired, including which one the single-button dispatch chose.
std::mutex commandMutex;
uint32_t shutterPresses = 0;
uint32_t shutterReleases = 0;
uint32_t focusPresses = 0;
uint32_t focusReleases = 0;

VirtualPeer *peerForAddressLocked(const NimBLEAddress &address) {
  for (const auto &peer : peers) {
    if (peer->address == address) {
      return peer.get();
    }
  }
  return nullptr;
}

void addFujifilm(uint64_t address, const std::string &name, uint32_t flappyFailAttempts) {
  Host::FujifilmVirtualCamera::Config config;
  config.name = name;
  config.address = NimBLEAddress(address, 0);
  auto peer = std::make_unique<VirtualPeer>();
  peer->name = name;
  peer->address = config.address;
  peer->fujifilm = std::make_unique<Host::FujifilmVirtualCamera>(config);
  if (flappyFailAttempts > 0) {
    // Handshake failure budget only. The standby drop itself is scheduled by
    // the scenario on virtual time, not by the peer's wall-clock timer.
    peer->fujifilm->setFlappy(flappyFailAttempts, 0);
  }
  peer->advertisement = peer->fujifilm->advertisement();
  NimBLEDevice::setMockPeerForAddress(peer->address, peer->fujifilm.get());
  peers.push_back(std::move(peer));
}

void addRicoh(uint64_t address, const std::string &name, uint32_t flappyFailAttempts) {
  Host::RicohVirtualCamera::Config config;
  config.name = name;
  config.address = NimBLEAddress(address, 0);
  config.camera_bonded = true;
  auto peer = std::make_unique<VirtualPeer>();
  peer->name = name;
  peer->address = config.address;
  peer->ricoh = std::make_unique<Host::RicohVirtualCamera>(config);
  if (flappyFailAttempts > 0) {
    peer->ricoh->setFlappy(flappyFailAttempts, 0);
  }
  peer->advertisement = peer->ricoh->advertisement();
  NimBLEDevice::setMockPeerForAddress(peer->address, peer->ricoh.get());
  peers.push_back(std::move(peer));
}

// Materialize the registered peers through the production discovery path and
// persist them, so a scenario boots with saved cameras exactly as a device does
// after the user scanned and connected once.
void saveRegisteredPeers(void) {
  const std::lock_guard<std::mutex> lock(peersMutex);
  CameraList::clear();
  for (const auto &peer : peers) {
    if (!CameraList::match(&peer->advertisement)) {
      ESP_LOGW(LOG_TAG, "Simulated peer '%s' did not match any camera protocol",
               peer->name.c_str());
    }
  }
  for (size_t n = 0; n < CameraList::size(); n++) {
    CameraList::save(CameraList::get(n).get());
  }
  CameraList::clear();
}

void radioTask(void *) {
  bool scanning = false;
  uint32_t scanStart = 0;

  while (true) {
    vTaskDelay(pdMS_TO_TICKS(ADVERTISE_INTERVAL_MS));

    NimBLEScan *scan = NimBLEDevice::getScan();
    if (scan == nullptr || !scan->isScanning()) {
      scanning = false;
      continue;
    }

    if (!scanning) {
      scanning = true;
      scanStart = xTaskGetTickCount();
    }

    {
      const std::lock_guard<std::mutex> lock(peersMutex);
      for (const auto &peer : peers) {
        scan->emitResult(&peer->advertisement);
        advertisementCount++;
      }
    }

    // The controller owns the discovery timer on hardware. Model it here so a
    // bounded scan really ends and the production scan-end callback runs.
    const uint32_t duration = scan->durationMs();
    if (duration != 0 && (xTaskGetTickCount() - scanStart) >= pdMS_TO_TICKS(duration)) {
      scan->stop();
      scan->emitEnd(0);
      scanning = false;
    }
  }
}

}  // namespace

bool bleTopologyIsValid(const std::string &topology) {
  return topology == "none" || topology == "fuji" || topology == "fuji-pair"
         || topology == "fuji-ricoh-flappy";
}

void bleStartPeers(const std::string &topology) {
  {
    const std::lock_guard<std::mutex> lock(peersMutex);
    if (topology == "fuji") {
      addFujifilm(FUJIFILM_A_ADDRESS, "FUJIFILM X100VI", 0);
    } else if (topology == "fuji-pair") {
      addFujifilm(FUJIFILM_A_ADDRESS, "FUJIFILM X100VI", 0);
      addFujifilm(FUJIFILM_B_ADDRESS, "FUJIFILM X-S20", 0);
    } else if (topology == "fuji-ricoh-flappy") {
      // The 2026-08-28 pairing: one healthy camera plus a GR IV in BLE
      // standby that fails the security handshake the way a supervision
      // timeout does before it lets a connect through.
      addFujifilm(FUJIFILM_A_ADDRESS, "FUJIFILM X100VI", 0);
      addRicoh(RICOH_ADDRESS, "RICOH GR IV", 1);
    }
  }

  if (!radioRunning) {
    radioRunning = true;
    // Priority 3 matches the per-target camera tasks: the radio is peer work,
    // not control-plane work, so it must never outrank the control task.
    xTaskCreate(radioTask, "ble-radio", 4096, nullptr, 3, nullptr);
  }
}

void bleSaveRegisteredPeers(void) {
  saveRegisteredPeers();
}

void bleStopPeers(void) {
  const std::lock_guard<std::mutex> lock(peersMutex);
  for (const auto &peer : peers) {
    if (peer->fujifilm != nullptr) {
      peer->fujifilm->setFlappy(0, 0);
    }
    if (peer->ricoh != nullptr) {
      peer->ricoh->setFlappy(0, 0);
    }
  }
  peers.clear();
}

void bleSetConnectFail(bool fail) {
  NimBLEDevice::setConnectShouldFail(fail);
}

bool bleDropLink(int index, bool deliverCallback) {
  auto &control = Control::getInstance();
  const auto targets = control.getTargets();
  bool dropped = false;

  int position = 0;
  for (auto *target : targets) {
    const int current = position++;
    if (index >= 0 && current != index) {
      continue;
    }
    auto camera = target->getCamera();
    if (camera == nullptr || !camera->isConnected()) {
      continue;
    }

    NimBLEClient *client = NimBLEDevice::connectedClientForAddress(camera->getAddress());
    if (client != nullptr) {
      // Reason 0x08 is the HCI supervision timeout, the drop shape a camera
      // going out of range or into standby produces.
      client->mockDropLink(0x08, deliverCallback);
      dropped = true;
      continue;
    }

    // FauxNY has no radio, so there is no transport to sever. Clearing the
    // connection liveness is the equivalent observable: the link is gone and
    // the camera stays selected, so the control task reacts exactly as it does
    // to a real drop. resetConnectionState() deliberately leaves the active
    // flag alone; Camera::disconnect() would clear it and suppress reconnect.
    if (deliverCallback) {
      camera->resetConnectionState();
      dropped = true;
    }
  }

  return dropped;
}

bool blePeerStandbyDrop(int index) {
  auto &control = Control::getInstance();
  const auto targets = control.getTargets();
  bool dropped = false;

  int position = 0;
  for (auto *target : targets) {
    const int current = position++;
    if (index >= 0 && current != index) {
      continue;
    }
    auto camera = target->getCamera();
    if (camera == nullptr) {
      continue;
    }

    const std::lock_guard<std::mutex> lock(peersMutex);
    VirtualPeer *peer = peerForAddressLocked(camera->getAddress());
    if (peer == nullptr) {
      continue;
    }
    if (peer->ricoh != nullptr && peer->ricoh->triggerStandbyDrop()) {
      dropped = true;
    } else if (peer->fujifilm != nullptr && peer->fujifilm->triggerStandbyDrop()) {
      dropped = true;
    }
  }

  return dropped;
}

size_t bleAdvertisementCount(void) {
  const std::lock_guard<std::mutex> lock(peersMutex);
  return advertisementCount;
}

size_t blePeerCount(void) {
  const std::lock_guard<std::mutex> lock(peersMutex);
  return peers.size();
}

void noteCameraCommand(int cmd) {
  const std::lock_guard<std::mutex> lock(commandMutex);
  switch (static_cast<Control::cmd_t>(cmd)) {
    case Control::CMD_SHUTTER_PRESS:
      shutterPresses++;
      profilerRadioEvent("shutter_press");
      break;
    case Control::CMD_SHUTTER_RELEASE:
      shutterReleases++;
      profilerRadioEvent("shutter_release");
      break;
    case Control::CMD_FOCUS_PRESS:
      focusPresses++;
      profilerRadioEvent("focus_press");
      break;
    case Control::CMD_FOCUS_RELEASE:
      focusReleases++;
      profilerRadioEvent("focus_release");
      break;
    case Control::CMD_GPS_UPDATE:
      profilerRadioEvent("gps_update");
      break;
    default:
      break;
  }
}

uint32_t cameraShutterPresses(void) {
  const std::lock_guard<std::mutex> lock(commandMutex);
  return shutterPresses;
}

uint32_t cameraShutterReleases(void) {
  const std::lock_guard<std::mutex> lock(commandMutex);
  return shutterReleases;
}

uint32_t cameraFocusPresses(void) {
  const std::lock_guard<std::mutex> lock(commandMutex);
  return focusPresses;
}

uint32_t cameraFocusReleases(void) {
  const std::lock_guard<std::mutex> lock(commandMutex);
  return focusReleases;
}

}  // namespace Sim

#if defined(FURBLE_SIM)
void Control::simDropActiveLink(int index) {
  Sim::bleDropLink(index, /*deliverCallback=*/true);
}
#endif

}  // namespace Furble
