// Host regression tests for the Fujifilm app-level registration gate.
//
// NimBLE link establishment and ATT acknowledgements are deliberately not
// treated as registration. The virtual peer can answer every GATT operation,
// withhold CHR_NOT1_UUID, and replay a callback from a previous session.

#include <chrono>
#include <iostream>
#include <thread>

#include "Device.h"
#include "FujifilmBasic.h"
#include "FujifilmSecure.h"
#include "FujifilmVirtualCamera.h"
#include "NimBLEDevice.h"
#include "TestSyncController.h"
#include "protocol/FujifilmProtocol.h"

const char *LOG_TAG = "furble-registration-gate-test";

namespace {

int failures = 0;

void check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    failures++;
  }
}

void init() {
  NimBLEDevice::resetMock();
  Furble::TestSync::reset();
  Furble::Device::init(Furble::TxPower::P3);
}

// The registration gate is open exactly between the subscription going live
// and the confirmation poll giving up. Every injection below has to land inside
// that window, and a fixed sleep cannot promise it does: on a loaded host the
// connecting thread may not have entered the wait at all, so the injection goes
// nowhere and the connect then fails or succeeds on host timing rather than on
// the behaviour under test. Park the connecting thread on the
// "fujifilm_registration_wait" sync point instead, inject while it is parked,
// and release it.
constexpr uint32_t kParkTimeoutMs = 30000;
constexpr const char *kRegistrationPoint = "fujifilm_registration_wait";

bool awaitRegistrationWait() {
  const bool parked = Furble::TestSync::awaitArrival(kRegistrationPoint, kParkTimeoutMs);
  check(parked, "the connect attempt parks in the registration wait");
  return parked;
}

void testRegistrationPayloads() {
  using Furble::FujifilmProtocol::isRegistrationNotification;
  const uint8_t captured[] = {0x01, 0x00};
  const uint8_t legacy[] = {0x02, 0x00};
  const uint8_t malformed[] = {0x01, 0x01};
  const uint8_t unrelated[] = {0x03, 0x00};
  check(isRegistrationNotification(captured, sizeof(captured)),
        "captured X100VI registration payload is accepted");
  check(isRegistrationNotification(legacy, sizeof(legacy)),
        "legacy Basic registration payload is accepted");
  check(!isRegistrationNotification(malformed, sizeof(malformed)),
        "malformed registration payload is rejected");
  check(!isRegistrationNotification(unrelated, sizeof(unrelated)),
        "unrelated registration payload is rejected");
  check(!isRegistrationNotification(nullptr, 2), "empty registration callback cannot confirm");
}

void testBasicConfirmationAndReset() {
  init();
  Furble::Host::FujifilmVirtualCamera peer;
  NimBLEDevice::setMockPeer(&peer);
  const auto advertisement = peer.advertisement();
  Furble::FujifilmBasic camera(&advertisement);

  check(camera.connect(Furble::TxPower::P3, 1000),
        "Basic connect requires and receives registration confirmation");
  check(peer.configured(), "Basic peer records the 01 00 registration notification");
  camera.disconnect();

  // A later reconnect must clear the old confirmation. Replay the callback
  // retained from the prior NimBLE client while the new attempt is waiting.
  peer.setWithholdRegistration(true);
  bool connected = true;
  Furble::TestSync::armBarrier(kRegistrationPoint, kParkTimeoutMs);
  std::thread reconnect([&]() { connected = camera.connect(Furble::TxPower::P3, 1000); });
  if (awaitRegistrationWait()) {
    check(peer.emitStaleRegistration(), "virtual peer can replay a stale callback");
    Furble::TestSync::release(kRegistrationPoint);
  }
  reconnect.join();
  check(!connected, "stale registration callback cannot confirm a reconnect");
  check(!camera.isConnected() && !peer.connected(),
        "Basic timeout tears down the link after registration is withheld");

  // These payloads are delivered through the production callback, not merely
  // checked by the protocol helper. None may confirm the dedicated event.
  for (const auto &payload : {
           std::vector<uint8_t> {},
            std::vector<uint8_t> {0x01},
            std::vector<uint8_t> {0x01, 0x01},
           std::vector<uint8_t> {0x03, 0x00}
  }) {
    peer.setWithholdRegistration(false);
    peer.setRegistrationPayload(payload);
    Furble::FujifilmBasic malformed(&advertisement);
    check(!malformed.connect(Furble::TxPower::P3, 1000),
          "malformed or unrelated CHR_NOT1 payload cannot confirm registration");
  }
  peer.setRegistrationPayload({0x01, 0x00});

  // A link loss while the gate is polling must abort immediately rather than
  // waiting for the full registration deadline.
  peer.setWithholdRegistration(true);
  Furble::FujifilmBasic dropped(&advertisement);
  bool dropConnected = true;
  Furble::TestSync::armBarrier(kRegistrationPoint, kParkTimeoutMs);
  std::thread dropAttempt([&]() { dropConnected = dropped.connect(Furble::TxPower::P3, 1000); });
  if (awaitRegistrationWait()) {
    auto *client = NimBLEDevice::lastClient();
    check(client != nullptr, "link-drop test has a live mock client");
    if (client != nullptr) {
      client->mockDropLink(0x08, true);
    }
    Furble::TestSync::release(kRegistrationPoint);
  }
  dropAttempt.join();
  check(!dropConnected, "link loss aborts registration polling");
}

void testSecureConfirmationAndTimeout() {
  init();
  Furble::Host::FujifilmVirtualCamera::Config config;
  config.secure = true;
  config.name = "FUJIFILM X100VI";
  Furble::Host::FujifilmVirtualCamera peer(config);
  NimBLEDevice::setMockPeer(&peer);
  const auto advertisement = peer.advertisement();
  Furble::FujifilmSecure camera(&advertisement);

  check(camera.connect(Furble::TxPower::P3, 1000),
        "Secure connect requires and receives X100VI registration confirmation");
  check(peer.configured(), "Secure peer records the captured 01 00 notification");
  camera.disconnect();

  peer.setWithholdRegistration(true);
  // Use a fresh camera object so this is a new-pair attempt. A saved Secure
  // reconnect intentionally scans first, which is a separate lifecycle path.
  Furble::FujifilmSecure withheld(&advertisement);
  const auto started = std::chrono::steady_clock::now();
  check(!withheld.connect(Furble::TxPower::P3, 1000),
        "Secure link-only connect fails when registration is withheld");
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - started);
  check(elapsed.count() < 1000, "registration timeout remains bounded in the host test");
}

// A saved-camera reconnect on the X100VI does not resend the dedicated
// registration confirmation. The camera goes straight to periodic geotag
// requests, so a valid request must confirm the gate. Hardware trace
// 2026-08-28: without this the reconnect stalls for the full deadline while
// 01 00 arrives on the geotag characteristic every 10 seconds.
void testGeotagRequestConfirmsReconnect() {
  init();
  Furble::Host::FujifilmVirtualCamera peer;
  NimBLEDevice::setMockPeer(&peer);
  const auto advertisement = peer.advertisement();

  peer.setWithholdRegistration(true);
  Furble::FujifilmBasic camera(&advertisement);
  bool connected = false;
  Furble::TestSync::armBarrier(kRegistrationPoint, kParkTimeoutMs);
  std::thread attempt([&]() { connected = camera.connect(Furble::TxPower::P3, 1000); });
  if (awaitRegistrationWait()) {
    check(peer.requestGeotag(), "virtual peer can send a geotag request mid-wait");
    Furble::TestSync::release(kRegistrationPoint);
  }
  attempt.join();
  check(connected, "geotag request confirms registration on a reconnect");
  camera.disconnect();

  Furble::Host::FujifilmVirtualCamera::Config config;
  config.secure = true;
  config.name = "FUJIFILM X100VI";
  Furble::Host::FujifilmVirtualCamera securePeer(config);
  NimBLEDevice::setMockPeer(&securePeer);
  const auto secureAdvertisement = securePeer.advertisement();

  securePeer.setWithholdRegistration(true);
  Furble::FujifilmSecure secureCamera(&secureAdvertisement);
  bool secureConnected = false;
  Furble::TestSync::armBarrier(kRegistrationPoint, kParkTimeoutMs);
  std::thread secureAttempt(
      [&]() { secureConnected = secureCamera.connect(Furble::TxPower::P3, 1000); });
  if (awaitRegistrationWait()) {
    check(securePeer.requestGeotag(), "secure virtual peer can send a geotag request mid-wait");
    Furble::TestSync::release(kRegistrationPoint);
  }
  secureAttempt.join();
  check(secureConnected, "geotag request confirms registration on a Secure reconnect");
  const Furble::Camera::gps_t gps = {52.5, 13.4, 34.0, 8};
  const Furble::Camera::timesync_t time = {
      .year = 2026,
      .month = 11,
      .day = 1,
      .hour = 12,
      .minute = 34,
      .second = 56,
      .centisecond = 0,
      .local = {2026, 11, 1, 14, 34, 56, 0},
      .tz_offset_minutes = 120,
      .tz_standard_offset_minutes = 60,
      .tz_dst_offset_minutes = 60,
      .dst = true,
  };
  secureCamera.updateGeoData(gps, time);
  check(securePeer.locationSyncValue() == 1,
        "the Secure geotag path enables AAB609C4 and reads the value back");

  // A refused clock write on a live link must leave the request pending so the
  // next GPS tick retries it instead of losing the camera's 01 00 request.
  check(securePeer.requestTimeSync(), "the Secure peer can request the clock");
  securePeer.failWrite(Furble::Host::FujifilmVirtualCamera::timeServiceUUID(),
                       Furble::Host::FujifilmVirtualCamera::utcTimeCharacteristicUUID());
  secureCamera.updateGeoData(gps, time);
  check(!securePeer.utcTimeWritten(), "a refused clock write is not recorded");
  securePeer.clearFaults();
  secureCamera.updateGeoData(gps, time);
  check(securePeer.utcTimeWritten(), "the clock request is retried after a refused write");
  const auto &clockPacket = securePeer.lastUtcTime();
  check((clockPacket.size() == 12) && (clockPacket[4] == 12) && (clockPacket[7] == 100)
            && (clockPacket[8] == 0) && (clockPacket[11] == 1),
        "the UTC packet keeps UTC fields and carries the standard offset and DST flag");
  secureCamera.disconnect();
}

// A drop during the location-sync enable write frees the self-deleting client
// and its cached services inline. The geotag service pointer must not be
// touched after that write fails.
void testLocationSyncDropOnWrite() {
  init();
  Furble::Host::FujifilmVirtualCamera::Config config;
  config.secure = true;
  config.name = "FUJIFILM X100VI";
  Furble::Host::FujifilmVirtualCamera peer(config);
  NimBLEDevice::setMockPeer(&peer);
  const auto advertisement = peer.advertisement();
  Furble::FujifilmSecure camera(&advertisement);
  check(camera.connect(Furble::TxPower::P3, 1000),
        "the Secure peer connects for the location-sync drop test");

  check(peer.requestGeotag(), "the Secure peer requests a geotag before the drop");
  peer.dropLinkOnWrite(Furble::Host::FujifilmVirtualCamera::secureNotificationServiceUUID(),
                       Furble::Host::FujifilmVirtualCamera::locationSyncCharacteristicUUID());
  const Furble::Camera::gps_t gps = {52.5, 13.4, 34.0, 8};
  const Furble::Camera::timesync_t time = {
      2026, 11, 1, 12, 34, 56, 0, {2026, 11, 1, 12, 34, 56, 0}
  };
  camera.updateGeoData(gps, time);

  check(!camera.isConnected(), "the location-sync write drop severs the link");
  check(peer.accessAfterDrop() == 0,
        "no cached service is touched after the location-sync write drops the link");
  camera.disconnect();
}

}  // namespace

int main() {
  testRegistrationPayloads();
  testBasicConfirmationAndReset();
  testSecureConfirmationAndTimeout();
  testGeotagRequestConfirmsReconnect();
  testLocationSyncDropOnWrite();
  check(!Furble::TestSync::anyTimedOut(), "no parked thread expired waiting for its release");
  NimBLEDevice::resetMock();
  if (failures != 0) {
    std::cerr << failures << " registration-gate checks failed\n";
    return 1;
  }
  std::cout << "fujifilm registration gate: PASS\n";
  return 0;
}
