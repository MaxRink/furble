#include <algorithm>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <MockNimBLE.h>

#include <CanonRemoteVirtualCamera.h>
#include <CanonSmartVirtualCamera.h>
#include <DjiVirtualCamera.h>
#include <FujifilmVirtualCamera.h>
#include <LumixVirtualCamera.h>
#include <NikonRemoteVirtualCamera.h>
#include <NikonSmartVirtualCamera.h>
#include <RicohVirtualCamera.h>
#include <SonyVirtualCamera.h>

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
constexpr uint64_t CANON_SMART_ADDRESS = 0x334455667788ULL;
constexpr uint64_t CANON_REMOTE_ADDRESS = 0x334455667799ULL;
constexpr uint64_t NIKON_REMOTE_ADDRESS = 0x445566778899ULL;
constexpr uint64_t NIKON_SMART_ADDRESS = 0x4455667788aaULL;
constexpr uint64_t SONY_ADDRESS = 0x5566778899aaULL;
constexpr uint64_t LUMIX_ADDRESS = 0x66778899aabbULL;
constexpr uint64_t DJI_ADDRESS = 0x778899aabbccULL;

struct VirtualPeer {
  /** Current advertisement for this peer, including post-pairing state. */
  NimBLEAdvertisedDevice currentAdvertisement() const {
    if (fujifilm != nullptr) {
      return fujifilm->advertisement();
    }
    if (ricoh != nullptr) {
      return ricoh->advertisement();
    }
    if (canon_smart != nullptr) {
      return canon_smart->advertisement();
    }
    if (canon_remote != nullptr) {
      return canon_remote->advertisement();
    }
    if (nikon_remote != nullptr) {
      return nikon_remote->advertisement();
    }
    if (nikon_smart != nullptr) {
      return nikon_smart->advertisement();
    }
    if (sony != nullptr) {
      return sony->advertisement();
    }
    if (lumix != nullptr) {
      return lumix->advertisement();
    }
    if (dji != nullptr) {
      return dji->advertisement();
    }
    return advertisement;
  }

  std::string name;
  NimBLEAddress address = NimBLEAddress {};
  // Discovery snapshot used when the scenario seeds saved cameras. The radio
  // uses currentAdvertisement() so a peer that paired later advertises its
  // reconnect form.
  NimBLEAdvertisedDevice advertisement;
  NimBLEMockPeer *mock = nullptr;
  std::unique_ptr<Host::FujifilmVirtualCamera> fujifilm;
  std::unique_ptr<Host::RicohVirtualCamera> ricoh;
  std::unique_ptr<Host::CanonSmartVirtualCamera> canon_smart;
  std::unique_ptr<Host::CanonRemoteVirtualCamera> canon_remote;
  std::unique_ptr<Host::NikonRemoteVirtualCamera> nikon_remote;
  std::unique_ptr<Host::NikonSmartVirtualCamera> nikon_smart;
  std::unique_ptr<Host::SonyVirtualCamera> sony;
  std::unique_ptr<Host::LumixVirtualCamera> lumix;
  std::unique_ptr<Host::DjiVirtualCamera> dji;
};

std::mutex peersMutex;
std::vector<std::unique_ptr<VirtualPeer>> peers;
size_t advertisementCount = 0;
// Peers whose advertisement did not match any production camera parser when
// the scenario seeded its saved catalog.
size_t unmatchedPeerCount = 0;
// Guarded by peersMutex like the peer list it serves.
bool radioRunning = false;

// Every peer family journals its GATT writes, but the byte field differs: the
// transport vendors keep the wire payload in `payload`, while DJI keeps the
// assembled frame in `frame`. One reader keeps that vendor distinction here so
// the driver and the scenario files only ever name indexes and bytes.
template <typename Write>
const std::vector<uint8_t> &journaledBytes(const Write &write) {
  return write.payload;
}

const std::vector<uint8_t> &journaledBytes(const Host::DjiVirtualCamera::Write &write) {
  return write.frame;
}

template <typename Peer>
bool readWriteJournal(const std::unique_ptr<Peer> &peer, size_t *count, std::string *lastWrite) {
  if (peer == nullptr) {
    return false;
  }
  const auto &writes = peer->writes();
  *count = writes.size();
  lastWrite->clear();
  if (writes.empty()) {
    return true;
  }
  static constexpr char HEX[] = "0123456789abcdef";
  const auto &bytes = journaledBytes(writes.back());
  lastWrite->reserve(bytes.size() * 2);
  for (const uint8_t byte : bytes) {
    lastWrite->push_back(HEX[byte >> 4]);
    lastWrite->push_back(HEX[byte & 0x0f]);
  }
  return true;
}

// Camera command counters. The simulator counts the commands that actually
// reach a camera task so a scenario can assert the shutter and focus paths
// fired, including which one the single-button dispatch chose.
std::mutex commandMutex;
uint32_t shutterPresses = 0;
size_t idleTicks = 0;
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

void addFujifilm(uint64_t address,
                 const std::string &name,
                 uint32_t flappyFailAttempts,
                 bool secure = false) {
  Host::FujifilmVirtualCamera::Config config;
  config.name = name;
  config.address = NimBLEAddress(address, 0);
  // A Basic body advertises a rotating pairing token; a Secure body advertises
  // its serial, which is what the displayed name is composed from.
  config.secure = secure;
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
  peer->mock = peer->fujifilm.get();
  peers.push_back(std::move(peer));
}

// The 2026-09-02 X100VI stale-bond signature. furble still holds the local
// bond, the camera deleted its side of the pairing, and it is not in pairing
// mode, so every attempt gets the link up and the encryption handshake then
// times out and takes the link with it. This is the peer the reconnect loop ran
// against forever on the bench.
void addFujifilmSecureStale(uint64_t address, const std::string &name) {
  Host::FujifilmVirtualCamera::Config config;
  config.name = name;
  config.address = NimBLEAddress(address, 0);
  config.secure = true;
  auto peer = std::make_unique<VirtualPeer>();
  peer->name = name;
  peer->address = config.address;
  peer->fujifilm = std::make_unique<Host::FujifilmVirtualCamera>(config);
  peer->fujifilm->setSecureTimeouts(Host::FujifilmVirtualCamera::kSecureTimeoutAlways);
  peer->advertisement = peer->fujifilm->advertisement();
  NimBLEDevice::setMockPeerForAddress(peer->address, peer->fujifilm.get());
  peer->mock = peer->fujifilm.get();
  // The half that makes it a stale bond rather than a first pairing: the
  // central still has keys for a camera that has forgotten them.
  NimBLEDevice::setBonded(true);
  peers.push_back(std::move(peer));
}

void addRicoh(uint64_t address, const std::string &name, uint32_t flappyFailAttempts) {
  Host::RicohVirtualCamera::Config config;
  config.name = name;
  config.address = NimBLEAddress(address, 0);
  // Unbonded on both sides at boot, so the first handshake that gets through
  // bonds the pair exactly as it does the first time a GR is paired.
  config.camera_bonded = false;
  auto peer = std::make_unique<VirtualPeer>();
  peer->name = name;
  peer->address = config.address;
  peer->ricoh = std::make_unique<Host::RicohVirtualCamera>(config);
  if (flappyFailAttempts > 0) {
    peer->ricoh->setFlappy(flappyFailAttempts, 0);
  }
  peer->advertisement = peer->ricoh->advertisement();
  NimBLEDevice::setMockPeerForAddress(peer->address, peer->ricoh.get());
  peer->mock = peer->ricoh.get();
  peers.push_back(std::move(peer));
}

template <typename Peer>
void addGenericPeer(uint64_t address,
                    const std::string &name,
                    std::unique_ptr<Peer> VirtualPeer::*member) {
  typename Peer::Config config;
  config.name = name;
  config.address = NimBLEAddress(address, 0);
  auto peer = std::make_unique<VirtualPeer>();
  peer->name = name;
  peer->address = config.address;
  (*peer).*member = std::make_unique<Peer>(config);
  peer->mock = ((*peer).*member).get();
  peer->advertisement = ((*peer).*member)->advertisement();
  NimBLEDevice::setMockPeerForAddress(peer->address, peer->mock);
  peers.push_back(std::move(peer));
}

// Materialize the registered peers through the production discovery path and
// persist them, so a scenario boots with saved cameras exactly as a device does
// after the user scanned and connected once.
void saveRegisteredPeers(void) {
  const std::lock_guard<std::mutex> lock(peersMutex);
  CameraList::clear();
  unmatchedPeerCount = 0;
  size_t nikonDiscoveryOnly = 0;
  for (const auto &peer : peers) {
    if ((peer->nikon_remote != nullptr) || (peer->nikon_smart != nullptr)) {
      // A saved Nikon record carries the random pairing identity the client
      // generated during stage 1. A discovery-only seed never paired, so
      // persisting it would create a record the reconnect scan cannot match
      // (the advertisement has no client ID). Only a real pairing produces one.
      nikonDiscoveryOnly++;
      continue;
    }
    if (!CameraList::match(&peer->advertisement)) {
      unmatchedPeerCount++;
      ESP_LOGW(LOG_TAG, "Simulated peer '%s' did not match any camera protocol",
               peer->name.c_str());
    }
  }
  if (nikonDiscoveryOnly > 0) {
    ESP_LOGI(LOG_TAG,
             "Not persisting %u Nikon discovery-only peer(s): saved reconnect needs a paired ID",
             static_cast<unsigned>(nikonDiscoveryOnly));
  }
  for (size_t n = 0; n < CameraList::size(); n++) {
    CameraList::save(CameraList::get(n));
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
        NimBLEAdvertisedDevice advertisement = peer->currentAdvertisement();
        scan->emitResult(&advertisement);
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
  return topology == "none" || topology == "fuji" || topology == "fuji-secure"
         || topology == "fuji-pair" || topology == "fuji-ricoh-flappy"
         || topology == "fuji-secure-stale" || topology == "canon-smart"
         || topology == "canon-remote" || topology == "nikon-remote" || topology == "nikon-smart"
         || topology == "sony" || topology == "lumix" || topology == "dji" || topology == "ricoh"
         || topology == "all";
}

void bleStartPeers(const std::string &topology) {
  bool startRadio = false;
  {
    const std::lock_guard<std::mutex> lock(peersMutex);
    if (topology == "fuji") {
      addFujifilm(FUJIFILM_A_ADDRESS, "FUJIFILM X100VI", 0);
    } else if (topology == "fuji-secure") {
      addFujifilm(FUJIFILM_A_ADDRESS, "FUJIFILM X100VI", 0, /*secure=*/true);
    } else if (topology == "fuji-pair") {
      addFujifilm(FUJIFILM_A_ADDRESS, "FUJIFILM X100VI", 0);
      addFujifilm(FUJIFILM_B_ADDRESS, "FUJIFILM X-S20", 0);
    } else if (topology == "fuji-secure-stale") {
      addFujifilmSecureStale(FUJIFILM_A_ADDRESS, "FUJIFILM X100VI");
    } else if (topology == "fuji-ricoh-flappy") {
      // The 2026-08-28 pairing: one healthy camera plus a GR IV in BLE
      // standby that fails the security handshake the way a supervision
      // timeout does before it lets a connect through.
      addFujifilm(FUJIFILM_A_ADDRESS, "FUJIFILM X100VI", 0);
      addRicoh(RICOH_ADDRESS, "RICOH GR IV", 1);
    } else if (topology == "ricoh") {
      addRicoh(RICOH_ADDRESS, "RICOH GR IV", 0);
    } else if (topology == "canon-smart") {
      addGenericPeer(CANON_SMART_ADDRESS, "Canon EOS RP", &VirtualPeer::canon_smart);
    } else if (topology == "canon-remote") {
      addGenericPeer(CANON_REMOTE_ADDRESS, "Canon EOS RP", &VirtualPeer::canon_remote);
    } else if (topology == "nikon-remote") {
      addGenericPeer(NIKON_REMOTE_ADDRESS, "Nikon B600", &VirtualPeer::nikon_remote);
    } else if (topology == "nikon-smart") {
      addGenericPeer(NIKON_SMART_ADDRESS, "Nikon Z6III", &VirtualPeer::nikon_smart);
    } else if (topology == "sony") {
      addGenericPeer(SONY_ADDRESS, "Sony ZV-1F", &VirtualPeer::sony);
    } else if (topology == "lumix") {
      addGenericPeer(LUMIX_ADDRESS, "LUMIX S5II", &VirtualPeer::lumix);
    } else if (topology == "dji") {
      addGenericPeer(DJI_ADDRESS, "DJI Osmo Action 4", &VirtualPeer::dji);
    } else if (topology == "all") {
      addFujifilm(FUJIFILM_A_ADDRESS, "FUJIFILM X100VI", 0, /*secure=*/true);
      addRicoh(RICOH_ADDRESS, "RICOH GR IV", 0);
      addGenericPeer(CANON_SMART_ADDRESS, "Canon EOS RP", &VirtualPeer::canon_smart);
      addGenericPeer(CANON_REMOTE_ADDRESS, "Canon EOS RP", &VirtualPeer::canon_remote);
      addGenericPeer(NIKON_REMOTE_ADDRESS, "Nikon B600", &VirtualPeer::nikon_remote);
      addGenericPeer(NIKON_SMART_ADDRESS, "Nikon Z6III", &VirtualPeer::nikon_smart);
      addGenericPeer(SONY_ADDRESS, "Sony ZV-1F", &VirtualPeer::sony);
      addGenericPeer(LUMIX_ADDRESS, "LUMIX S5II", &VirtualPeer::lumix);
      addGenericPeer(DJI_ADDRESS, "DJI Osmo Action 4", &VirtualPeer::dji);
    }
    // Nothing advertises without peers, so a scenario that seeds no topology
    // pays for no radio task at all.
    startRadio = !peers.empty() && !radioRunning;
    if (startRadio) {
      radioRunning = true;
    }
  }

  if (startRadio) {
    // Priority 3 matches the per-target camera tasks: the radio is peer work,
    // not control-plane work, so it must never outrank the control task.
    xTaskCreate(radioTask, "ble-radio", 4096, nullptr, 3, nullptr);
  }
}

void bleSaveRegisteredPeers(void) {
  saveRegisteredPeers();
}

void bleSetMaxClients(size_t max) {
  NimBLEDevice::setMaxClients(max);
}

void bleSetDeferredClientDelete(bool enabled) {
  NimBLEDevice::setDeferredClientDelete(enabled);
}

bool bleSecureStallAborted(void) {
  const std::lock_guard<std::mutex> lock(peersMutex);
  for (const auto &peer : peers) {
    if (peer->fujifilm != nullptr && peer->fujifilm->secureStallWasAborted()) {
      return true;
    }
  }
  return false;
}

size_t bleLiveClientCount(void) {
  return NimBLEDevice::liveClientCount();
}

size_t blePairingAnswerCount(void) {
  return NimBLEDevice::mockPasskeyConfirmCount();
}

bool bleLastPairingAnswerAccepted(void) {
  return NimBLEDevice::mockLastPasskeyAccept();
}

void bleSetSecureStallMs(uint32_t ms) {
  // Run the stall on the virtual clock. The host suite keeps the wall clock,
  // where a real second is the point; here a real second would be dead time and
  // would not be deterministic. vTaskDelay parks the calling task on the plan
  // 158 scheduler, so the control task really is unavailable for the duration
  // while it still holds Camera::m_Mutex, which is the whole shape being
  // modelled.
  Host::setPeerStallFunction([](uint32_t slice) { vTaskDelay(pdMS_TO_TICKS(slice)); });

  const std::lock_guard<std::mutex> lock(peersMutex);
  for (const auto &peer : peers) {
    if (peer->fujifilm != nullptr) {
      peer->fujifilm->setSecureConnectionStallMs(ms);
    }
  }
}

void bleStopPeers(void) {
  const std::lock_guard<std::mutex> lock(peersMutex);
  radioRunning = false;
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
  const auto cameras = control.getTargetCameras();
  bool dropped = false;

  int position = 0;
  for (const auto &camera : cameras) {
    const int current = position++;
    if (index >= 0 && current != index) {
      continue;
    }
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
  const auto cameras = control.getTargetCameras();
  bool dropped = false;

  int position = 0;
  for (const auto &camera : cameras) {
    const int current = position++;
    if (index >= 0 && current != index) {
      continue;
    }
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

bool bleSetWithholdRegistration(bool withhold) {
  const std::lock_guard<std::mutex> lock(peersMutex);
  bool applied = false;
  for (const auto &peer : peers) {
    if (peer->fujifilm != nullptr) {
      peer->fujifilm->setWithholdRegistration(withhold);
      applied = true;
    }
  }
  return applied;
}

size_t bleAdvertisementCount(void) {
  const std::lock_guard<std::mutex> lock(peersMutex);
  return advertisementCount;
}

size_t targetIdleTicks(void) {
  const std::lock_guard<std::mutex> lock(commandMutex);
  return idleTicks;
}

size_t blePeerCount(void) {
  const std::lock_guard<std::mutex> lock(peersMutex);
  return peers.size();
}

bool blePeerWriteStats(size_t index, size_t *count, std::string *lastWrite) {
  if (count == nullptr || lastWrite == nullptr) {
    return false;
  }
  const std::lock_guard<std::mutex> lock(peersMutex);
  if (index >= peers.size()) {
    return false;
  }
  const auto &peer = peers[index];
  if (peer->fujifilm != nullptr) {
    return readWriteJournal(peer->fujifilm, count, lastWrite);
  }
  if (peer->ricoh != nullptr) {
    return readWriteJournal(peer->ricoh, count, lastWrite);
  }
  if (peer->canon_smart != nullptr) {
    return readWriteJournal(peer->canon_smart, count, lastWrite);
  }
  if (peer->canon_remote != nullptr) {
    return readWriteJournal(peer->canon_remote, count, lastWrite);
  }
  if (peer->nikon_remote != nullptr) {
    return readWriteJournal(peer->nikon_remote, count, lastWrite);
  }
  if (peer->nikon_smart != nullptr) {
    return readWriteJournal(peer->nikon_smart, count, lastWrite);
  }
  if (peer->sony != nullptr) {
    return readWriteJournal(peer->sony, count, lastWrite);
  }
  if (peer->lumix != nullptr) {
    return readWriteJournal(peer->lumix, count, lastWrite);
  }
  if (peer->dji != nullptr) {
    return readWriteJournal(peer->dji, count, lastWrite);
  }
  return false;
}

size_t bleUnmatchedPeerCount(void) {
  const std::lock_guard<std::mutex> lock(peersMutex);
  return unmatchedPeerCount;
}

size_t bleSavedCameraCount(void) {
  return CameraList::getSaveCount();
}

void noteCameraCommand(int cmd) {
  const std::lock_guard<std::mutex> lock(commandMutex);
  switch (static_cast<Control::cmd_t>(cmd)) {
    case Control::CMD_ERROR:
      // The queue-timeout idle tick. Used to prove the 250 ms target cadence.
      idleTicks++;
      break;
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
