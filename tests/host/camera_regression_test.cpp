#include <chrono>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

#include "Device.h"
#include "FujifilmBasic.h"
#include "FujifilmSecure.h"
#include "FujifilmVirtualCamera.h"
#include "Nikon.h"
#include "NimBLEDevice.h"
#include "Ricoh.h"
#include "RicohVirtualCamera.h"

const char *LOG_TAG = "camera-regression";

namespace {
// Camera::m_ConnSaverIdleMs plus a margin. Kept in one place so the reason for
// the test's runtime is obvious, and so a change to the threshold shows up here
// as a compile-time neighbour rather than as a mysterious timeout.
constexpr uint32_t CONN_SAVER_IDLE_WAIT_MS = 10 * 1000 + 500;

bool check(bool condition, const char *message) {
  if (!condition)
    std::cerr << "FAIL: " << message << '\n';
  return condition;
}

bool testRegistrationTimeoutException() {
  NimBLEDevice::resetMock();
  Furble::Device::init(ESP_PWR_LVL_P3);
  Furble::Host::FujifilmVirtualCamera::Config config;
  config.secure = true;
  Furble::Host::FujifilmVirtualCamera peer(config);
  peer.setRequireLongConnParamsAfterIdentifier(true);
  NimBLEDevice::setMockPeer(&peer);
  const auto advertisement = peer.advertisement();
  Furble::FujifilmSecure camera(&advertisement);
  camera.setConnSaverEnabled(true);
  if (!check(camera.connect(ESP_PWR_LVL_P3, 1000), "Fujifilm registration completes"))
    return false;
  if (!check(peer.registrationConnParamsAccepted(),
             "over-cap peer request is accepted during registration"))
    return false;
  NimBLEClient *client = NimBLEDevice::lastClient();
  if (!check(client != nullptr, "registration leaves a client"))
    return false;
  if (!check(client->mockConnInfoReadCount() >= 2,
             "Secure registration waits for the asynchronous parameter update"))
    return false;
  if (!check(!client->mockConnParamUpdatePending(),
             "Secure registration observes the controller-applied parameters"))
    return false;
  ble_gap_upd_params overCap = {};
  overCap.itvl_min = 200;
  overCap.itvl_max = 240;
  overCap.supervision_timeout = 3200;
  if (!check(!client->mockPeerRequestConnParams(overCap),
             "same over-cap request is rejected after registration"))
    return false;
  const NimBLEConnInfo effective = client->getConnInfo();
  if (!check(effective.getConnInterval() >= BLE_GAP_INITIAL_CONN_ITVL_MIN
                 && effective.getConnInterval() <= BLE_GAP_INITIAL_CONN_ITVL_MAX
                 && effective.getConnLatency() == 1
                 && effective.getConnTimeout() == (2 * BLE_GAP_INITIAL_SUPERVISION_TIMEOUT),
             "exact fast profile is live after secure registration"))
    return false;
  camera.shutterPress();
  if (!check(!peer.writes().empty(), "shutter remains usable after timeout rejection"))
    return false;
  return check(camera.setConnProfile(Furble::Camera::ConnProfile::IDLE),
               "confirmed fast transition releases the peer override for idle");
}

// The connection saver's idle transition, which nothing tested deliberately.
//
// maybeSetIdle() is the only caller of setConnProfile(ConnProfile::IDLE) in
// production: the per-target task tick calls it, and it drops the link to the
// long interval once the session has been quiet for m_ConnSaverIdleMs. Every
// other host test sets CONN_SAVER false, so this path was only ever reached by
// accident, and a test-determinism fix elsewhere in this PR removed the
// accident. Covering it on purpose is the point of this test.
//
// It costs the real threshold in wall time, a little over ten seconds. That is
// deliberate. m_ConnSaverIdleMs is a static constexpr with no seam, the host
// harness has no virtual clock (esp_timer_get_time() is steady_clock in
// MockNimBLE), and the two ways to skip the wait are both worse than the wait:
// jumping the process-wide clock forward would perturb every other timeout in
// the test, and reaching m_LastConnActivityMs directly would mean a test-only
// setter in production Camera.h.
bool testConnSaverIdleTransition() {
  NimBLEDevice::resetMock();
  Furble::Device::init(ESP_PWR_LVL_P3);
  Furble::Host::FujifilmVirtualCamera::Config config;
  config.secure = true;
  Furble::Host::FujifilmVirtualCamera peer(config);
  NimBLEDevice::setMockPeer(&peer);
  const auto advertisement = peer.advertisement();
  Furble::FujifilmSecure camera(&advertisement);
  if (!check(camera.connect(ESP_PWR_LVL_P3, 1000), "the Secure camera connects"))
    return false;

  NimBLEClient *client = NimBLEDevice::lastClient();
  if (!check(client != nullptr, "the connect leaves a client"))
    return false;

  // What the controller has actually applied, not what was queued. The mock
  // holds one stale read after every update on purpose, so that a test cannot
  // request a profile and re-read it in the same breath and call that proof.
  // These assertions are about the live link, so they drain the pending update
  // first.
  const auto applied = [client]() {
    while (client->mockConnParamUpdatePending()) {
      client->getConnInfo();
    }
    return client->getConnInfo();
  };

  // Enabling the saver requests the fast profile and starts the idle timer.
  camera.setConnSaverEnabled(true);
  if (!check(applied().getConnLatency() == 1, "the saver starts in the fast profile"))
    return false;

  // Before the threshold, the tick must leave the link alone. Without this the
  // test would pass with the threshold removed altogether.
  camera.maybeSetIdle();
  if (!check(applied().getConnLatency() == 1,
             "a tick inside the quiet threshold leaves the fast profile alone"))
    return false;

  // Past the threshold, the same tick drops to the idle profile.
  std::this_thread::sleep_for(std::chrono::milliseconds(CONN_SAVER_IDLE_WAIT_MS));
  camera.maybeSetIdle();
  const NimBLEConnInfo idle = applied();
  if (!check(idle.getConnInterval() >= 200 && idle.getConnInterval() <= 240
                 && idle.getConnLatency() == 0,
             "a tick past the quiet threshold requests the idle profile"))
    return false;
  // getConnProfile() classifies the cached snapshot rather than the live link,
  // so refresh it first. The sleep above is well past the sampler's own one
  // second rate limit, so this read is not the one it drops.
  camera.updateConnStats();
  if (!check(camera.getConnProfile() == Furble::Camera::ConnProfile::IDLE,
             "and the camera reports itself idle"))
    return false;

  // Activity brings the link back. The user pressing the shutter must not wait
  // out an idle interval, so the fast profile is restored on the press.
  camera.noteConnActivity(true);
  if (!check(camera.setConnProfile(Furble::Camera::ConnProfile::FAST),
             "activity restores the fast profile"))
    return false;
  if (!check(applied().getConnLatency() == 1, "and the link carries it"))
    return false;

  // The activity also restarted the quiet timer, so the very next tick must not
  // undo it. This is the half that fails if the threshold is keyed off the
  // wrong timestamp rather than off the last activity.
  camera.maybeSetIdle();
  if (!check(applied().getConnLatency() == 1, "and the next tick does not immediately undo it"))
    return false;

  camera.disconnect();
  return true;
}

bool testRegistrationFastProfileTimeout() {
  NimBLEDevice::resetMock();
  Furble::Device::init(ESP_PWR_LVL_P3);
  Furble::Host::FujifilmVirtualCamera::Config config;
  config.secure = true;
  Furble::Host::FujifilmVirtualCamera peer(config);
  peer.setRequireLongConnParamsAfterIdentifier(true);
  NimBLEDevice::setMockPeer(&peer);
  NimBLEDevice::setConnParamApplyDelayReads(1000);
  const auto advertisement = peer.advertisement();
  Furble::FujifilmSecure camera(&advertisement);
  return check(!camera.connect(ESP_PWR_LVL_P3, 1000),
               "Secure registration is bounded when FAST never applies");
}

bool testSecureRequiredSubscriptionsUseResponses() {
  NimBLEDevice::resetMock();
  Furble::Device::init(ESP_PWR_LVL_P3);

  Furble::Host::FujifilmVirtualCamera::Config config;
  config.secure = true;
  Furble::Host::FujifilmVirtualCamera peer(config);
  NimBLEDevice::setMockPeer(&peer);
  const auto advertisement = peer.advertisement();
  Furble::FujifilmSecure camera(&advertisement);

  if (!check(camera.connect(ESP_PWR_LVL_P3, 1000),
             "Secure camera accepts its normal subscription sequence")) {
    return false;
  }

  if (!check(peer.subscriptionRequestedWithResponse(
                 Furble::Host::FujifilmVirtualCamera::configurationServiceUUID(),
                 Furble::Host::FujifilmVirtualCamera::configurationIndication1UUID()),
             "Secure indication 1 uses an acknowledged CCCD write")) {
    return false;
  }
  if (!check(peer.subscriptionRequestedWithResponse(
                 Furble::Host::FujifilmVirtualCamera::configurationServiceUUID(),
                 Furble::Host::FujifilmVirtualCamera::configurationIndication2UUID()),
             "Secure indication 2 uses an acknowledged CCCD write")) {
    return false;
  }

  camera.disconnect();
  NimBLEDevice::resetMock();
  return true;
}

bool testDelayedRegistrationParameterRequest() {
  NimBLEDevice::resetMock();
  Furble::Device::init(ESP_PWR_LVL_P3);
  Furble::Host::FujifilmVirtualCamera::Config config;
  config.secure = true;
  Furble::Host::FujifilmVirtualCamera peer(config);
  peer.setRequireLongConnParamsAfterIdentifier(true);
  peer.setDelayRegistrationConnParamsUntilFastRequest(true);
  NimBLEDevice::setMockPeer(&peer);
  const auto advertisement = peer.advertisement();
  Furble::FujifilmSecure camera(&advertisement);
  return check(camera.connect(ESP_PWR_LVL_P3, 1000) && peer.registrationConnParamsAccepted(),
               "delayed Secure registration parameters remain inside the narrow gate");
}

bool testRegistrationParameterRequestDuringSubscription() {
  NimBLEDevice::resetMock();
  Furble::Device::init(ESP_PWR_LVL_P3);
  Furble::Host::FujifilmVirtualCamera::Config config;
  config.secure = true;
  Furble::Host::FujifilmVirtualCamera peer(config);
  ble_gap_upd_params params = {};
  params.itvl_min = 24;
  params.itvl_max = 40;
  params.latency = 0;
  params.supervision_timeout = 2000;
  peer.requestConnParamsOnSubscribe(
      Furble::Host::FujifilmVirtualCamera::configurationServiceUUID(),
      Furble::Host::FujifilmVirtualCamera::configurationIndication1UUID(), params);
  NimBLEDevice::setMockPeer(&peer);
  const auto advertisement = peer.advertisement();
  Furble::FujifilmSecure camera(&advertisement);
  return check(camera.connect(ESP_PWR_LVL_P3, 1000),
               "Secure registration accepts a deferred parameter request during subscription");
}

bool testSecureFastProfileWaitsForShutterDiscovery() {
  NimBLEDevice::resetMock();
  Furble::Device::init(ESP_PWR_LVL_P3);
  Furble::Host::FujifilmVirtualCamera::Config config;
  config.secure = true;
  Furble::Host::FujifilmVirtualCamera peer(config);
  peer.setRejectFastBeforeShutterDiscovery(true);
  NimBLEDevice::setMockPeer(&peer);
  const auto advertisement = peer.advertisement();
  Furble::FujifilmSecure camera(&advertisement);
  return check(camera.connect(ESP_PWR_LVL_P3, 1000),
               "Secure fast profile is requested after shutter discovery");
}

bool testNullAndMissingIdentifierBoundaries() {
  NimBLEDevice::resetMock();
  Furble::Device::init(ESP_PWR_LVL_P3);
  Furble::Host::FujifilmVirtualCamera::Config secureConfig;
  secureConfig.secure = true;
  Furble::Host::FujifilmVirtualCamera securePeer(secureConfig);
  const auto secureAdvertisement = securePeer.advertisement();
  Furble::FujifilmSecure secure(&secureAdvertisement);
  static_cast<NimBLEScanCallbacks *>(&secure)->onResult(nullptr);

  NimBLEDevice::resetMock();
  Furble::Host::FujifilmVirtualCamera basicPeer;
  basicPeer.suppressCharacteristic(
      Furble::Host::FujifilmVirtualCamera::pairServiceUUID(),
      Furble::Host::FujifilmVirtualCamera::identifierCharacteristicUUID());
  NimBLEDevice::setMockPeer(&basicPeer);
  const auto basicAdvertisement = basicPeer.advertisement();
  Furble::FujifilmBasic basic(&basicAdvertisement);
  return check(!basic.connect(ESP_PWR_LVL_P3, 1000),
               "Fujifilm Basic rejects a missing identifier characteristic");
}

// Every user-facing message that names a camera puts the name on a line of its
// own, so a camera that advertised no name would open a message box with a
// blank first line. That is reachable: the already-saved refusal composes its
// text from a saved record, and a saved record carries whatever the body
// advertised, including nothing.
bool testUnnamedCameraDisplayName() {
  NimBLEDevice::resetMock();
  Furble::Device::init(ESP_PWR_LVL_P3);

  Furble::Host::FujifilmVirtualCamera::Config named;
  named.name = "FUJIFILM X100VI";
  Furble::Host::FujifilmVirtualCamera namedPeer(named);
  const auto namedAdvertisement = namedPeer.advertisement();
  Furble::FujifilmBasic namedCamera(&namedAdvertisement);
  if (!check(namedCamera.getDisplayName() == "FUJIFILM X100VI",
             "a named camera is displayed under its own name"))
    return false;

  Furble::Host::FujifilmVirtualCamera::Config unnamed;
  unnamed.name = "";
  Furble::Host::FujifilmVirtualCamera unnamedPeer(unnamed);
  const auto unnamedAdvertisement = unnamedPeer.advertisement();
  Furble::FujifilmBasic unnamedCamera(&unnamedAdvertisement);
  if (!check(unnamedCamera.getName().empty(), "the unnamed record really carries no name"))
    return false;
  return check(unnamedCamera.getDisplayName() == Furble::Camera::DISPLAY_NAME_FALLBACK,
               "an unnamed camera is displayed as the stand-in, never as a blank line");
}

bool testNullNikonCallbacks() {
  NimBLEDevice::resetMock();
  Furble::Device::init(ESP_PWR_LVL_P3);
  Furble::Host::FujifilmVirtualCamera peer;
  const auto advertisement = peer.advertisement();
  if (!check(!Furble::Nikon::matches(nullptr), "Nikon matcher rejects null"))
    return false;
  Furble::Nikon camera(&advertisement);
  auto *callbacks = static_cast<NimBLEScanCallbacks *>(&camera);
  callbacks->onResult(nullptr);
  return true;
}

bool testSecureRegistrationDropStopsGATT() {
  NimBLEDevice::resetMock();
  Furble::Device::init(ESP_PWR_LVL_P3);
  Furble::Host::FujifilmVirtualCamera::Config config;
  config.secure = true;
  Furble::Host::FujifilmVirtualCamera peer(config);
  peer.dropLinkOnSubscribe(Furble::Host::FujifilmVirtualCamera::configurationServiceUUID(),
                           Furble::Host::FujifilmVirtualCamera::configurationIndication1UUID());
  NimBLEDevice::setMockPeer(&peer);
  const auto advertisement = peer.advertisement();
  Furble::FujifilmSecure camera(&advertisement);
  if (!check(!camera.connect(ESP_PWR_LVL_P3, 1000),
             "Secure registration aborts when the peer drops during subscription"))
    return false;
  if (!check(!camera.isConnected() && !peer.connected(),
             "mid-registration drop leaves no connected session"))
    return false;
  if (!check(peer.accessAfterDrop() == 0, "mid-registration drop makes no subsequent GATT calls"))
    return false;
  for (const auto &write : peer.writes()) {
    if (!check(write.characteristic
                   != Furble::Host::FujifilmVirtualCamera::shutterCharacteristicUUID().toString(),
               "mid-registration drop does not reach shutter writes"))
      return false;
  }
  return true;
}

bool testRicohBondPolicy() {
  NimBLEDevice::resetMock();
  Furble::Device::init(ESP_PWR_LVL_P3);
  Furble::Host::RicohVirtualCamera peer;
  NimBLEDevice::setMockPeer(&peer);
  const auto advertisement = peer.advertisement();
  NimBLEDevice::setBonded(true);
  auto first = std::make_unique<Furble::Ricoh>(&advertisement);
  if (!check(!first->connect(ESP_PWR_LVL_P3, 1000),
             "stale local Ricoh bond fails fresh pairing once"))
    return false;
  if (!check(peer.cameraBonded() == false && NimBLEDevice::deleteBondCount() == 1u,
             "fresh Ricoh pairing clears only the stale local bond"))
    return false;

  auto retry = std::make_unique<Furble::Ricoh>(&advertisement);
  if (!check(retry->connect(ESP_PWR_LVL_P3, 1000),
             "Ricoh retry performs numeric-comparison pairing"))
    return false;
  retry->shutterPress();
  bool flavor = false;
  bool operation = false;
  for (const auto &write : peer.writes()) {
    flavor |= write.characteristic
              == Furble::Host::RicohVirtualCamera::shootingFlavorCharacteristicUUID().toString();
    operation |=
        write.characteristic
        == Furble::Host::RicohVirtualCamera::operationRequestCharacteristicUUID().toString();
  }
  if (!check(peer.cameraBonded() && flavor && operation, "Ricoh retry reaches the paired shutter"))
    return false;
  retry->disconnect();

  auto runSavedFailure = [&]() {
    NimBLEDevice::resetMock();
    Furble::Device::init(ESP_PWR_LVL_P3);
    Furble::Host::RicohVirtualCamera::Config config;
    config.camera_bonded = true;
    config.accept_numeric_comparison = false;
    Furble::Host::RicohVirtualCamera peer(config);
    NimBLEDevice::setMockPeer(&peer);
    const auto advertisement = peer.advertisement();
    NimBLEDevice::setBonded(true);
    Furble::Ricoh fresh(&advertisement);
    std::vector<uint8_t> data(fresh.getSerialisedBytes());
    if (!fresh.serialise(data.data(), data.size()))
      return false;
    Furble::Ricoh saved(data.data(), data.size());
    if (saved.connect(ESP_PWR_LVL_P3, 1000))
      return false;
    return NimBLEDevice::deleteBondCount() == 0u
           && NimBLEDevice::isBonded(advertisement.getAddress());
  };
  return check(runSavedFailure(), "saved Ricoh reconnect preserves its bond");
}
}  // namespace

int main() {
  return testRegistrationTimeoutException() && testRegistrationFastProfileTimeout()
                 && testSecureRequiredSubscriptionsUseResponses()
                 && testDelayedRegistrationParameterRequest()
                 && testRegistrationParameterRequestDuringSubscription()
                 && testSecureFastProfileWaitsForShutterDiscovery()
                 && testNullAndMissingIdentifierBoundaries() && testNullNikonCallbacks()
                 && testSecureRegistrationDropStopsGATT() && testRicohBondPolicy()
                 && testUnnamedCameraDisplayName() && testConnSaverIdleTransition()
             ? 0
             : 1;
}
