// AddressSanitizer regression for the Ricoh secure-timeout reconnect crash.
//
// Hardware scenario (M5StickS3, RICOH GR IV bonded at 34:90:EA:BB:7D:73): a
// saved reconnect brings the link up ("Connected", "Ricoh securing"), then the
// physical link dies and secureConnection() fails with rc=520 (0x200 | HCI
// 0x08 Connection Timeout). The BLE_GAP_EVENT_DISCONNECT for that dead link is
// still queued on the NimBLE host task when Camera::connect() enters its
// failed-connect reclaim. The reclaim sees a live client (m_Connected is true,
// isConnected() still true because the conn handle is only cleared by the
// queued event). The broken ordering armed setSelfDelete(true, false) first
// and only then called _disconnect(). The moment the arm lands, the host task
// may consume the queued event: onDisconnect fires and NimBLE frees the armed
// client inline (NimBLEClient.cpp:1084-1098 -> NimBLEDevice.cpp:373 delete).
// Ricoh::_disconnect() then dereferences the freed client: the on-device
// LoadProhibited right after "Camera connect failed".
//
// The mock hook mockMarkLinkDeadEventPending() models the queued event; the
// shared SecureTimeoutPeer decorator fails secureConnection() with the link
// left "connected" and the event pending, exactly as rc=520 leaves it. On the
// unfixed ordering ASan aborts with heap-use-after-free in
// NimBLEClient::isConnected() called from Ricoh::_disconnect(). With the
// reordered reclaim (terminate first, arm self-delete last) the connect fails
// cleanly and no client leaks.

#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>

#include "Camera.h"
#include "Device.h"
#include "NimBLEDevice.h"
#include "Ricoh.h"
#include "RicohVirtualCamera.h"
#include "SecureTimeoutPeer.h"

const char *LOG_TAG = "furble-host";

namespace {

int g_Failures = 0;

bool check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "  FAIL: " << message << '\n';
    g_Failures++;
  }
  return condition;
}

// Serialized form matching Ricoh::ricoh_t, so the camera is constructed on the
// PairType::SAVED path (a bonded saved reconnect, as on the crashing device).
struct SavedRicoh {
  char name[64];
  uint64_t address;
  uint8_t type;
};

constexpr uint64_t kAddress = 0x3490EABB7D73ULL;

bool testSecureTimeoutReclaimDoesNotCrash() {
  std::cout << "test: a bonded Ricoh secure timeout with a queued disconnect event "
               "unwinds the failed connect with no use-after-free\n";
  const int before = g_Failures;
  NimBLEDevice::resetMock();
  Furble::Device::init(ESP_PWR_LVL_P3);
  NimBLEDevice::setBonded(true);  // bonded(before)=yes, pairType=saved

  Furble::Host::RicohVirtualCamera::Config config;
  config.advertisement_name = "RICOH GR IV";
  config.gatt_model = "RICOH GR IV";
  config.address = NimBLEAddress(kAddress, 0);
  config.camera_bonded = true;
  Furble::Host::RicohVirtualCamera inner(config);
  Furble::Host::SecureTimeoutPeer peer(inner);
  NimBLEDevice::setMockPeer(&peer);

  SavedRicoh saved = {};
  std::strncpy(saved.name, "RICOH GR IV", sizeof(saved.name) - 1);
  saved.address = kAddress;
  saved.type = 0;
  auto camera = std::make_shared<Furble::Ricoh>(&saved, sizeof(saved));

  const bool connected = camera->connect(ESP_PWR_LVL_P3, 1000);

  check(!connected, "the secure timeout makes connect() report failure");
  check(!camera->isConnected(), "the camera is left disconnected, not a zombie");
  check(NimBLEDevice::liveClientCount() == 0,
        "the failed connect reclaims the client, none leak from the pool");

  NimBLEDevice::resetMock();
  return g_Failures == before;
}

}  // namespace

int main() {
  testSecureTimeoutReclaimDoesNotCrash();

  const int status = (g_Failures == 0) ? 0 : 1;
  if (status == 0) {
    std::cout << "ricoh secure-timeout harness: PASS\n";
  } else {
    std::cout << "ricoh secure-timeout harness: FAIL (" << g_Failures << " checks)\n";
  }
  return status;
}
