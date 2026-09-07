// Host coverage for the transport-independent companion GATT service.
//
// MockCentral addresses the same characteristic UUIDs exposed by
// CompanionGatt, then routes the ATT-like operations through the production
// CompanionService transport seam. The downstream location and trigger paths
// use the real Control, Camera, Fujifilm protocol and MockNimBLE virtual peers.

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "CameraList.h"
#include "Device.h"
#include "FujifilmBasic.h"
#include "FujifilmVirtualCamera.h"
#include "FurbleCompanionAuth.h"
#include "FurbleCompanionService.h"
#include "FurbleControl.h"
#include "FurbleGPS.h"
#include "FurbleSettings.h"
#include "FurbleUI.h"
#include "NimBLEDevice.h"
#include "advertisement_preferences_stub.h"
#include "protocol/CameraListProtocol.h"
#include "protocol/FujifilmProtocol.h"
#include "protocol/ProvisionTLV.h"

namespace {

constexpr const char *LOCATION_UUID = "b57f4f5e-087b-4740-b71d-8262cf26ebbc";
constexpr const char *STATUS_UUID = "b57f4f60-087b-4740-b71d-8262cf26ebbc";
constexpr const char *SETTINGS_UUID = "b57f4f61-087b-4740-b71d-8262cf26ebbc";
constexpr const char *TRIGGER_UUID = "b57f4f62-087b-4740-b71d-8262cf26ebbc";
constexpr const char *CAMERAS_UUID = "b57f4f63-087b-4740-b71d-8262cf26ebbc";
constexpr const char *AUTH_UUID = "b57f4f6f-087b-4740-b71d-8262cf26ebbc";

using Furble::CompanionService;
using Furble::Control;
using Furble::Host::FujifilmVirtualCamera;

int g_Failures = 0;

void check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "  FAIL: " << message << '\n';
    g_Failures++;
  }
}

bool waitFor(const std::function<bool()> &predicate, uint32_t timeout_ms) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  while (std::chrono::steady_clock::now() < deadline) {
    if (predicate()) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  return predicate();
}

size_t shutterWriteCount(const FujifilmVirtualCamera &peer) {
  size_t count = 0;
  for (const auto &write : peer.writes()) {
    if (write.characteristic == FujifilmVirtualCamera::shutterCharacteristicUUID().toString()) {
      count++;
    }
  }
  return count;
}

bool sameGeotag(const FujifilmVirtualCamera &peer,
                const std::array<uint8_t, Furble::FujifilmProtocol::GEOTAG_BYTES> &expected) {
  const auto &actual = peer.lastGeotag();
  return actual.size() == expected.size()
         && std::equal(actual.begin(), actual.end(), expected.begin());
}

std::shared_ptr<Furble::FujifilmBasic> makeCamera(FujifilmVirtualCamera &peer) {
  NimBLEDevice::setMockPeer(&peer);
  const NimBLEAdvertisedDevice advertisement = peer.advertisement();
  return std::make_shared<Furble::FujifilmBasic>(&advertisement);
}

void startControlTask(void) {
  static bool started = false;
  if (started) {
    return;
  }
  started = true;
  auto &control = Control::getInstance();
  xTaskCreate(control_task, "control", 8192, &control, 4, nullptr);
}

void stopControl(Control &control) {
  control.disconnect();
  waitFor([&] { return control.getTargetCount() == 0; }, 3000);
}

class MockCentral final: public Furble::CompanionTransport {
 public:
  void attach(CompanionService &service) { m_Service = &service; }

  void connect(void) {
    m_Connected = true;
    m_Service->onConnected();
  }

  void disconnect(void) override {
    if (!m_Connected) {
      return;
    }
    m_Connected = false;
    m_Service->onDisconnected();
  }

  void setSecurity(bool encrypted, bool authenticated) {
    m_Encrypted = encrypted;
    m_Authenticated = authenticated;
  }

  bool write(const char *uuid, const std::vector<uint8_t> &value) {
    if (!m_Connected || uuid == nullptr || m_Service == nullptr) {
      return false;
    }
    if (std::strcmp(uuid, LOCATION_UUID) == 0) {
      m_Service->handleLocation(value.data(), value.size());
      return true;
    }
    if (std::strcmp(uuid, SETTINGS_UUID) == 0) {
      m_Service->handleSettings(value.data(), value.size());
      return true;
    }
    if (std::strcmp(uuid, TRIGGER_UUID) == 0) {
      m_Service->handleTrigger(value.data(), value.size());
      return true;
    }
    if (std::strcmp(uuid, CAMERAS_UUID) == 0) {
      m_Service->handleCameras(value.data(), value.size());
      return true;
    }
    if (std::strcmp(uuid, AUTH_UUID) == 0) {
      m_Service->handleAuth(value.data(), value.size());
      return true;
    }
    return false;
  }

  CompanionService::companion_status_t readStatus(const char *uuid) {
    m_HaveStatus = false;
    if (m_Connected && m_Service != nullptr && uuid != nullptr
        && std::strcmp(uuid, STATUS_UUID) == 0) {
      m_Service->notifyStatus(true);
    }
    return m_Status;
  }

  void clearEvents(void) {
    m_Indications.clear();
    m_CameraIndications.clear();
    m_CameraNotifications.clear();
    m_AuthIndications.clear();
    m_LastError = 0;
    m_HaveStatus = false;
  }

  bool isConnected(void) const override { return m_Connected; }

  bool isEncrypted(void) const override { return m_Encrypted; }

  bool isAuthenticated(void) const override { return m_Authenticated; }

  uint16_t getMaxPayload(void) const override { return 244; }

  void notify(uint8_t charId, const uint8_t *data, size_t len) override {
    if (charId == Furble::COMPANION_CHAR_CAMERAS && data != nullptr) {
      m_CameraNotifications.emplace_back(data, data + len);
      return;
    }
    if (charId != Furble::COMPANION_CHAR_STATUS || data == nullptr || len != sizeof(m_Status)) {
      return;
    }
    std::memcpy(&m_Status, data, sizeof(m_Status));
    m_HaveStatus = true;
  }

  void indicate(uint8_t charId, const uint8_t *data, size_t len) override {
    if (data == nullptr) {
      return;
    }
    if (charId == Furble::COMPANION_CHAR_CAMERAS) {
      m_CameraIndications.emplace_back(data, data + len);
      return;
    }
    if ((charId != Furble::COMPANION_CHAR_SETTINGS)
        && (charId != Furble::COMPANION_CHAR_AUTH)) {
      return;
    }
    if (data == nullptr) {
      return;
    }
    auto &indications = charId == Furble::COMPANION_CHAR_AUTH ? m_AuthIndications : m_Indications;
    indications.emplace_back(data, data + len);
  }

  void error(uint8_t charId, uint8_t attError) override {
    m_LastError = static_cast<uint16_t>(charId) << 8 | attError;
  }

  bool haveStatus(void) const { return m_HaveStatus; }

  const std::vector<std::vector<uint8_t>> &indications(void) const { return m_Indications; }

  const std::vector<std::vector<uint8_t>> &cameraIndications(void) const {
    return m_CameraIndications;
  }

  const std::vector<std::vector<uint8_t>> &cameraNotifications(void) const {
    return m_CameraNotifications;
  }
  const std::vector<std::vector<uint8_t>> &authIndications(void) const { return m_AuthIndications; }

  uint16_t lastError(void) const { return m_LastError; }

  bool wasDisconnected(void) const { return !m_Connected; }

 private:
  CompanionService *m_Service = nullptr;
  bool m_Connected = false;
  bool m_Encrypted = true;
  bool m_Authenticated = true;
  bool m_HaveStatus = false;
  CompanionService::companion_status_t m_Status = {};
  std::vector<std::vector<uint8_t>> m_Indications;
  std::vector<std::vector<uint8_t>> m_CameraIndications;
  std::vector<std::vector<uint8_t>> m_CameraNotifications;
  std::vector<std::vector<uint8_t>> m_AuthIndications;
  uint16_t m_LastError = 0;
};

// NimBLE can deliver a characteristic write while the disconnect callback is
// clearing the connection state. This transport only records the calls needed
// by the auth path and is intentionally safe for that race test.
class AuthRaceTransport final: public Furble::CompanionTransport {
 public:
  bool isConnected(void) const override { return m_Connected.load(); }
  bool isEncrypted(void) const override { return true; }
  bool isAuthenticated(void) const override { return true; }
  uint16_t getMaxPayload(void) const override { return 244; }
  void notify(uint8_t, const uint8_t *, size_t) override {}
  void indicate(uint8_t, const uint8_t *, size_t) override {
    const std::lock_guard<std::mutex> lock(m_Mutex);
    m_Indications++;
  }
  void error(uint8_t, uint8_t) override {}
  void disconnect(void) override { m_Connected.store(false); }

  size_t indications(void) const {
    const std::lock_guard<std::mutex> lock(m_Mutex);
    return m_Indications;
  }

 private:
  std::atomic<bool> m_Connected {true};
  mutable std::mutex m_Mutex;
  size_t m_Indications = 0;
};

std::vector<uint8_t> authResponse(const std::string &password, const std::vector<uint8_t> &nonce) {
  std::array<uint8_t, Furble::CompanionAuth::HMAC_SIZE> digest = {};
  if (nonce.size() != Furble::CompanionAuth::NONCE_SIZE
      || !Furble::companionHmacSha256(reinterpret_cast<const uint8_t *>(password.data()),
                                      password.size(), nonce.data(), nonce.size(), digest.data(),
                                      digest.size())) {
    return {};
  }
  std::vector<uint8_t> packet {CompanionService::AUTH_VERSION, CompanionService::AUTH_OP_PROOF};
  packet.insert(packet.end(), digest.begin(),
                digest.begin() + Furble::CompanionAuth::RESPONSE_SIZE);
  return packet;
}

std::vector<uint8_t> bytesOf(const CompanionService::companion_fix_t &fix) {
  const auto *begin = reinterpret_cast<const uint8_t *>(&fix);
  return {begin, begin + sizeof(fix)};
}

void testAuthDisconnectRace(void) {
  std::cout << "test: companion auth/disconnect callback race\n";
  Furble::Settings::save<std::string>(Furble::Settings::COMPANION_PASSWORD, "race password");
  AuthRaceTransport transport;
  CompanionService service {transport};
  const std::vector<uint8_t> begin {CompanionService::AUTH_VERSION,
                                    CompanionService::AUTH_OP_BEGIN};

  for (size_t iteration = 0; iteration < 500; iteration++) {
    service.onConnected();
    std::thread auth([&] { service.handleAuth(begin.data(), begin.size()); });
    std::thread disconnect([&] { service.onDisconnected(); });
    auth.join();
    disconnect.join();
    check(!service.isPasswordAuthenticated(),
          "disconnect race left the companion auth state authenticated");
  }
  check(transport.indications() > 0, "auth race did not exercise the transport indication path");
  Furble::Settings::save<std::string>(Furble::Settings::COMPANION_PASSWORD, "");
}

void testPasswordLoadFailureDeniesPrivilegedWrites(void) {
  std::cout << "test: companion password load failure denies privileged writes\n";
  Furble::Settings::save<std::string>(Furble::Settings::COMPANION_PASSWORD, "");
  Furble::Settings::setPasswordLoadResult(false);
  MockCentral central;
  CompanionService service(central);
  central.attach(service);
  service.init();
  central.connect();
  central.setSecurity(true, true);
  central.clearEvents();

  check(central.write(SETTINGS_UUID, {2, 1, 1, 87}),
        "password-load failure reaches the privileged settings gate");
  check(!service.isPasswordAuthenticated(),
        "password-load failure does not authenticate the companion session");
  check(central.indications().empty() && central.authIndications().size() == 1
            && central.authIndications()[0].size() == Furble::CompanionService::AUTH_RESULT_SIZE
            && central.authIndications()[0][2] == Furble::CompanionService::AUTH_RESULT_REJECTED,
        "password-load failure rejects the privileged write");
  service.deinit();
  Furble::Settings::setPasswordLoadResult(true);

  MockCentral unsetCentral;
  CompanionService unsetService(unsetCentral);
  unsetCentral.attach(unsetService);
  unsetService.init();
  unsetCentral.connect();
  unsetCentral.setSecurity(true, true);
  Furble::Settings::setPasswordSaveResult(false);
  unsetCentral.clearEvents();
  check(unsetCentral.write(SETTINGS_UUID,
                           {2, Furble::ProvisionTLV::COMPANION_PASSWORD_WIRE_ID, 3, 'n', 'e', 'w'}),
        "failed initial password enable reaches the settings handler");
  check(Furble::Settings::load<std::string>(Furble::Settings::COMPANION_PASSWORD).empty(),
        "failed initial password enable leaves the credential unset");
  check(unsetCentral.indications().size() == 1 && unsetCentral.indications()[0].size() == 4
            && unsetCentral.indications()[0][0] == 4,
        "failed initial password enable cannot report success");
  unsetService.deinit();
  Furble::Settings::setPasswordSaveResult(true);
}

void testCompanionGattFlow(void) {
  std::cout << "test: companion mock-central location/status/settings/trigger flow\n";
  NimBLEDevice::resetMock();
  Furble::Settings::setBool(Furble::Settings::SLEEP_CONN, false);
  Furble::Settings::setBool(Furble::Settings::TX_ADAPTIVE, false);
  Furble::Settings::setBool(Furble::Settings::RECON_BACKOFF, false);
  Furble::Settings::setBool(Furble::Settings::CONN_SAVER, false);
  Furble::Settings::setU8(Furble::Settings::BRIGHTNESS, 12);
  Furble::Settings::save<std::string>(Furble::Settings::COMPANION_PASSWORD, "test companion");
  Furble::GPS::getInstance().clearExternalFix();
  Furble::Host::setBatteryStatus(83, 4095, -123, 5000, true);
  Furble::Device::init(ESP_PWR_LVL_P3);
  startControlTask();

  FujifilmVirtualCamera::Config firstConfig;
  firstConfig.name = "FUJIFILM X100VI A";
  firstConfig.address = NimBLEAddress(0x112233445501ULL, 0);
  firstConfig.token = {0x11, 0x22, 0x33, 0x44};
  FujifilmVirtualCamera::Config secondConfig;
  secondConfig.name = "FUJIFILM X100VI B";
  secondConfig.address = NimBLEAddress(0x112233445502ULL, 0);
  secondConfig.token = {0x55, 0x66, 0x77, 0x88};

  FujifilmVirtualCamera first(firstConfig);
  FujifilmVirtualCamera second(secondConfig);
  auto firstCamera = makeCamera(first);
  auto secondCamera = makeCamera(second);
  NimBLEDevice::setMockPeerForAddress(first.config().address, &first);
  NimBLEDevice::setMockPeerForAddress(second.config().address, &second);

  auto &control = Control::getInstance();
  control.addActive(firstCamera);
  control.addActive(secondCamera);
  check(control.getTargetCount() == 2, "two virtual cameras become Control targets");
  control.connectAll(true);
  check(waitFor([&] { return control.getState() == Control::STATE_ACTIVE; }, 5000),
        "both cameras reach the active Control state");
  check(waitFor([&] { return control.getConnectedTargetCount() == 2; }, 1000),
        "both virtual cameras are connected");

  MockCentral central;
  CompanionService service(central);
  central.attach(service);
  service.init();
  central.connect();

  service.beginPairing(314159);
  check(service.hasPendingPairing(), "numeric-comparison pairing raises the password gate");
  check(service.getPendingPairingPin() == 314159, "pairing gate exposes the pending PIN");
  service.confirmPairing(true);
  check(!service.hasPendingPairing(), "accepting the pairing gate clears the pending PIN");

  const auto status = central.readStatus(STATUS_UUID);
  check(central.haveStatus(), "status UUID read returns a status packet");
  check(status.version == 1, "status wire version is reported");
  check(status.battery_percent == 83, "status reports the battery percentage");
  check(status.battery_mv == 4095, "status reports the battery voltage");
  check(status.battery_ma == -123, "status reports the battery current");
  check(status.power_flags == 0x03, "status reports charging and VBUS flags");
  check(status.camera_total == 2, "status reports both selected cameras");
  check(status.camera_connected == 2, "status reports both connected cameras");
  check(status.control_state == 4, "status reports the active Control state");

  check(first.requestGeotag(), "first camera accepts a geotag request");
  check(second.requestGeotag(), "second camera accepts a geotag request");
  first.clearEvents();
  second.clearEvents();

  CompanionService::companion_fix_t fix = {};
  fix.version = 1;
  fix.flags = 0x07;
  fix.satellites = 11;
  fix.accuracy_m = 4;
  fix.latitude = 52.5200123;
  fix.longitude = 13.404954;
  fix.altitude = 34.5;
  fix.year = 2026;
  fix.month = 8;
  fix.day = 23;
  fix.hour = 19;
  fix.minute = 5;
  fix.second = 12;
  fix.centisecond = 34;
  fix.age_ms = 250;
  check(central.write(LOCATION_UUID, bytesOf(fix)), "location UUID accepts a fix write");

  const auto external = Furble::GPS::getInstance().getExternalFix();
  check(external.position_valid && external.time_valid && external.altitude_valid,
        "location write stores all valid geodata flags");
  check(external.gps.latitude == fix.latitude && external.gps.longitude == fix.longitude
            && external.gps.altitude == fix.altitude && external.gps.satellites == fix.satellites,
        "location write stores the companion coordinates and satellites");
  check(Furble::GPS::getInstance().getSource() == Furble::GPS::SOURCE_COMPANION,
        "location write selects the companion GPS source");
  Furble::GPS::getInstance().update();

  const auto expectedGeotag = Furble::FujifilmProtocol::encodeGeotag(
      {fix.latitude, fix.longitude, fix.altitude, fix.year, fix.month, fix.day, fix.hour,
       fix.minute, fix.second});
  check(waitFor([&] { return sameGeotag(first, expectedGeotag); }, 2000),
        "location reaches Control and updates the first camera geodata");
  check(waitFor([&] { return sameGeotag(second, expectedGeotag); }, 2000),
        "location reaches Control and updates the second camera geodata");

  central.setSecurity(false, false);
  central.clearEvents();
  check(central.write(SETTINGS_UUID, {2, 1, 1, 87}),
        "settings UUID routes an unauthenticated write through the service gate");
  check(Furble::Settings::load<uint8_t>(Furble::Settings::BRIGHTNESS) == 12,
        "unauthenticated settings TLV cannot change FurbleSettings");
  check(central.indications().empty(), "unauthenticated settings TLV produces no response");
  const size_t firstShutterBeforeGate = shutterWriteCount(first);
  check(central.write(TRIGGER_UUID, {1, 1}),
        "trigger UUID routes an unauthenticated write through the service gate");
  check(shutterWriteCount(first) == firstShutterBeforeGate,
        "unauthenticated trigger cannot fire a camera");

  central.setSecurity(true, true);
  central.clearEvents();
  check(central.write(SETTINGS_UUID, {2, 1, 1, 87}),
        "encrypted settings write routes through the password gate");
  check(Furble::Settings::load<uint8_t>(Furble::Settings::BRIGHTNESS) == 12,
        "encrypted but unauthenticated settings cannot change FurbleSettings");
  check(central.indications().empty() && central.authIndications().size() == 1
            && central.authIndications()[0].size() == Furble::CompanionService::AUTH_RESULT_SIZE
            && central.authIndications()[0][2] == Furble::CompanionService::AUTH_RESULT_REJECTED
            && central.lastError() == 0,
        "password gate reports a rejected write on the Auth characteristic");

  central.clearEvents();
  check(central.write(AUTH_UUID, {Furble::CompanionService::AUTH_VERSION,
                                  Furble::CompanionService::AUTH_OP_BEGIN}),
        "AUTH begin reaches the companion service");
  check(central.authIndications().size() == 1
            && central.authIndications()[0].size() == Furble::CompanionService::AUTH_CHALLENGE_SIZE
            && central.authIndications()[0][0] == Furble::CompanionService::AUTH_VERSION
            && central.authIndications()[0][1] == Furble::CompanionService::AUTH_OP_BEGIN,
        "AUTH begin returns one fresh nonce");
  const auto nonce = central.authIndications().empty()
                         ? std::vector<uint8_t> {}
                         : std::vector<uint8_t>(central.authIndications()[0].begin() + 2,
                                                central.authIndications()[0].end());
  auto wrong = authResponse("wrong password", nonce);
  check(central.write(AUTH_UUID, wrong), "wrong HMAC reaches the companion service");
  check(central.authIndications().size() == 2
            && central.authIndications()[1].size() == Furble::CompanionService::AUTH_RESULT_SIZE
            && central.authIndications()[1][2] == Furble::CompanionService::AUTH_RESULT_REJECTED,
        "wrong HMAC is rejected");
  check(!service.isPasswordAuthenticated(), "wrong HMAC does not authenticate the session");

  auto replay = authResponse("test companion", nonce);
  check(central.write(AUTH_UUID, replay), "replayed response reaches the companion service");
  check(central.authIndications().size() == 3
            && central.authIndications()[2].size() == Furble::CompanionService::AUTH_RESULT_SIZE
            && central.authIndications()[2][2] == Furble::CompanionService::AUTH_RESULT_REJECTED,
        "replayed response for a consumed nonce is rejected");
  check(!service.isPasswordAuthenticated(), "replayed response does not authenticate the session");

  central.clearEvents();
  check(central.write(AUTH_UUID, {Furble::CompanionService::AUTH_VERSION,
                                  Furble::CompanionService::AUTH_OP_BEGIN}),
        "a failed authentication can request a new nonce");
  const auto freshNonce = central.authIndications().empty()
                              ? std::vector<uint8_t> {}
                              : std::vector<uint8_t>(central.authIndications()[0].begin() + 2,
                                                     central.authIndications()[0].end());
  const auto correct = authResponse("test companion", freshNonce);
  check(central.write(AUTH_UUID, correct), "correct HMAC reaches the companion service");
  check(
      central.authIndications().size() == 2
          && central.authIndications()[1].size() == Furble::CompanionService::AUTH_RESULT_SIZE
          && central.authIndications()[1][2] == Furble::CompanionService::AUTH_RESULT_AUTHENTICATED,
      "correct HMAC authenticates the session");
  check(service.isPasswordAuthenticated(), "service reports the authenticated session");

  const std::string oldPassword = "test companion";
  const std::string failedRotation = "failed rotation";
  std::vector<uint8_t> failedRotatePassword {2, Furble::ProvisionTLV::COMPANION_PASSWORD_WIRE_ID,
                                             static_cast<uint8_t>(failedRotation.size())};
  failedRotatePassword.insert(failedRotatePassword.end(), failedRotation.begin(),
                              failedRotation.end());
  Furble::Settings::setPasswordSaveResult(false);
  central.clearEvents();
  check(central.write(SETTINGS_UUID, failedRotatePassword),
        "failed password rotation reaches the settings handler");
  check(Furble::Settings::load<std::string>(Furble::Settings::COMPANION_PASSWORD) == oldPassword,
        "failed password rotation preserves the previous credential");
  check(!service.isPasswordAuthenticated(),
        "failed password rotation revokes the authenticated session");
  check(central.indications().size() == 1 && central.indications()[0].size() == 4
            && central.indications()[0][0] == 4,
        "failed password rotation returns a rejected setting response");
  Furble::Settings::setPasswordSaveResult(true);

  central.clearEvents();
  check(central.write(AUTH_UUID, {Furble::CompanionService::AUTH_VERSION,
                                  Furble::CompanionService::AUTH_OP_BEGIN}),
        "old password can request a fresh challenge after failed rotation");
  const auto failedRotationNonce =
      central.authIndications().empty()
          ? std::vector<uint8_t> {}
          : std::vector<uint8_t>(central.authIndications()[0].begin() + 2,
                                 central.authIndications()[0].end());
  check(central.write(AUTH_UUID, authResponse(oldPassword, failedRotationNonce)),
        "old password re-authenticates after failed rotation");
  check(service.isPasswordAuthenticated(),
        "old password restores authentication after failed rotation");

  central.clearEvents();
  const std::vector<uint8_t> malformedPassword {
      2, Furble::ProvisionTLV::COMPANION_PASSWORD_WIRE_ID, 3, 'a', 0, 'b'};
  check(central.write(SETTINGS_UUID, malformedPassword),
        "malformed password setting reaches the companion service");
  check(
      Furble::Settings::load<std::string>(Furble::Settings::COMPANION_PASSWORD) == "test companion",
      "embedded NUL password is not persisted");
  check(central.indications().size() == 1 && central.indications()[0].size() == 4
            && central.indications()[0][0] == 4,
        "embedded NUL password is rejected without changing the session");

  central.clearEvents();
  check(central.write(SETTINGS_UUID, {2, 1, 1, 87}),
        "settings UUID accepts an authenticated write TLV for Brightness");
  check(Furble::Settings::load<uint8_t>(Furble::Settings::BRIGHTNESS) == 87,
        "settings TLV changes the corresponding FurbleSettings value");
  check(central.indications().size() == 1 && central.indications()[0].size() == 4,
        "settings write returns one compact indication response");
  if (central.indications().size() == 1 && central.indications()[0].size() == 4) {
    const auto &response = central.indications()[0];
    check(response[0] == 0 && response[1] == 1 && response[2] == 1 && response[3] == 0,
          "settings response acknowledges the Brightness TLV");
  }

  central.clearEvents();
  const std::string rotatedPassword = "rotated companion";
  std::vector<uint8_t> rotatePassword {2, Furble::ProvisionTLV::COMPANION_PASSWORD_WIRE_ID,
                                       static_cast<uint8_t>(rotatedPassword.size())};
  rotatePassword.insert(rotatePassword.end(), rotatedPassword.begin(), rotatedPassword.end());
  check(central.write(SETTINGS_UUID, rotatePassword),
        "authenticated settings can rotate the companion password");
  check(!service.isPasswordAuthenticated(),
        "password rotation revokes the authenticated session before acknowledgement");
  check(central.indications().size() == 1 && central.indications()[0].size() == 4,
        "password rotation returns one compact acknowledgement");

  central.clearEvents();
  check(central.write(AUTH_UUID, {Furble::CompanionService::AUTH_VERSION,
                                  Furble::CompanionService::AUTH_OP_BEGIN}),
        "password rotation issues a fresh challenge");
  const auto rotatedNonce = central.authIndications().empty()
                                ? std::vector<uint8_t> {}
                                : std::vector<uint8_t>(central.authIndications()[0].begin() + 2,
                                                       central.authIndications()[0].end());
  check(central.write(AUTH_UUID, authResponse(rotatedPassword, rotatedNonce)),
        "rotated password response reaches the companion service");
  check(service.isPasswordAuthenticated(), "rotated password authenticates the session");

  Furble::Settings::setPasswordSaveResult(false);
  central.clearEvents();
  check(central.write(SETTINGS_UUID, {2, Furble::ProvisionTLV::COMPANION_PASSWORD_WIRE_ID, 0}),
        "failed password clear reaches the settings handler");
  check(
      Furble::Settings::load<std::string>(Furble::Settings::COMPANION_PASSWORD) == rotatedPassword,
      "failed password clear preserves the rotated credential");
  check(!service.isPasswordAuthenticated(),
        "failed password clear revokes the authenticated session");
  check(central.indications().size() == 1 && central.indications()[0].size() == 4
            && central.indications()[0][0] == 4,
        "failed password clear returns a rejected setting response");
  Furble::Settings::setPasswordSaveResult(true);

  central.clearEvents();
  check(central.write(AUTH_UUID, {Furble::CompanionService::AUTH_VERSION,
                                  Furble::CompanionService::AUTH_OP_BEGIN}),
        "failed clear permits a new challenge with the unchanged password");
  const auto clearFailureNonce =
      central.authIndications().empty()
          ? std::vector<uint8_t> {}
          : std::vector<uint8_t>(central.authIndications()[0].begin() + 2,
                                 central.authIndications()[0].end());
  check(central.write(AUTH_UUID, authResponse(rotatedPassword, clearFailureNonce))
            && service.isPasswordAuthenticated(),
        "unchanged password restores authentication after failed clear");

  first.clearEvents();
  second.clearEvents();
  check(central.write(TRIGGER_UUID, {1, 1}), "trigger UUID accepts shutter press");
  check(central.write(TRIGGER_UUID, {1, 0}), "trigger UUID accepts shutter release");
  check(waitFor([&] { return shutterWriteCount(first) >= 4; }, 2000),
        "remote trigger fires the first connected virtual camera");
  check(waitFor([&] { return shutterWriteCount(second) >= 4; }, 2000),
        "remote trigger fires the second connected virtual camera");

  central.disconnect();
  central.connect();
  central.setSecurity(true, true);
  central.clearEvents();
  check(!service.isPasswordAuthenticated(), "a new connection starts unauthenticated");
  check(central.write(SETTINGS_UUID, {2, 1, 1, 12}),
        "new-session settings write reaches the password gate");
  check(Furble::Settings::load<uint8_t>(Furble::Settings::BRIGHTNESS) == 87,
        "new-session unauthenticated settings cannot change FurbleSettings");
  check(central.authIndications().size() == 1
            && central.authIndications()[0].size() == Furble::CompanionService::AUTH_RESULT_SIZE
            && central.authIndications()[0][2] == Furble::CompanionService::AUTH_RESULT_REJECTED
            && central.lastError() == 0,
        "new-session settings report rejection on the Auth characteristic");
  central.clearEvents();
  check(central.write(AUTH_UUID, {Furble::CompanionService::AUTH_VERSION,
                                  Furble::CompanionService::AUTH_OP_BEGIN}),
        "new session can request a fresh nonce after disconnect");
  check(central.authIndications().size() == 1
            && central.authIndications()[0].size() == Furble::CompanionService::AUTH_CHALLENGE_SIZE,
        "new session receives a nonce instead of reusing the old challenge");
  central.disconnect();
  check(!service.isPasswordAuthenticated(), "disconnect clears password authentication");
  central.connect();
  central.setSecurity(true, true);
  central.clearEvents();
  check(central.write(AUTH_UUID, {Furble::CompanionService::AUTH_VERSION,
                                  Furble::CompanionService::AUTH_OP_BEGIN}),
        "failure-limit session can request a nonce");
  const auto failureNonce = central.authIndications().empty()
                                ? std::vector<uint8_t> {}
                                : std::vector<uint8_t>(central.authIndications()[0].begin() + 2,
                                                       central.authIndications()[0].end());
  const auto badResponse = authResponse("wrong password", failureNonce);
  for (uint8_t attempt = 0; attempt < Furble::CompanionAuth::MAX_FAILURES; attempt++) {
    check(central.write(AUTH_UUID, badResponse), "failed HMAC reaches the companion service");
    if (attempt + 1 < Furble::CompanionAuth::MAX_FAILURES) {
      check(central.write(AUTH_UUID, {Furble::CompanionService::AUTH_VERSION,
                                      Furble::CompanionService::AUTH_OP_BEGIN}),
            "failure-limit session issues a fresh nonce");
    }
  }
  check(!central.isConnected(), "three failed HMAC responses disconnect the central");
  check(central.authIndications().size() == 6
            && central.authIndications().back().size() == Furble::CompanionService::AUTH_RESULT_SIZE
            && central.authIndications().back()[2] == Furble::CompanionService::AUTH_RESULT_DROPPED,
        "failure-limit session reports a dropped authentication result");
  service.deinit();
  stopControl(control);
  NimBLEDevice::resetMock();
}

// Cameras characteristic ----------------------------------------------------

struct CameraRecord {
  CompanionService::companion_camera_t head;
  std::string name;
};

CameraRecord decodeCameraRecord(const std::vector<uint8_t> &bytes) {
  CameraRecord record = {};
  if (bytes.size() < sizeof(record.head)) {
    return record;
  }
  std::memcpy(&record.head, bytes.data(), sizeof(record.head));
  const size_t nameLength =
      std::min<size_t>(record.head.name_len, bytes.size() - sizeof(record.head));
  record.name.assign(reinterpret_cast<const char *>(bytes.data() + sizeof(record.head)),
                     nameLength);
  return record;
}

std::vector<Furble::Host::UIRequest> drainRequests(void) {
  return Furble::Host::takeUIRequests();
}

void testCompanionCameras(void) {
  std::cout << "test: companion cameras list, select, connect, disconnect and rate limit\n";
  NimBLEDevice::resetMock();
  Furble::Host::clearPreferences();
  Furble::Host::setUIRequestsAccepted(true);
  drainRequests();
  Furble::Settings::setBool(Furble::Settings::MULTICONNECT, true);
  Furble::Settings::setBool(Furble::Settings::TX_ADAPTIVE, false);
  Furble::Device::init(ESP_PWR_LVL_P3);

  FujifilmVirtualCamera::Config firstConfig;
  firstConfig.name = "FUJIFILM X-T5";
  firstConfig.address = NimBLEAddress(0x1122334455aaULL, 0);
  firstConfig.token = {0x11, 0x22, 0x33, 0x44};
  FujifilmVirtualCamera::Config secondConfig;
  secondConfig.name = "FUJIFILM X-S20";
  secondConfig.address = NimBLEAddress(0x1122334455bbULL, 0);
  secondConfig.token = {0x55, 0x66, 0x77, 0x88};

  FujifilmVirtualCamera firstPeer(firstConfig);
  FujifilmVirtualCamera secondPeer(secondConfig);
  auto firstCamera = makeCamera(firstPeer);
  auto secondCamera = makeCamera(secondPeer);
  Furble::CameraList::save(firstCamera);
  Furble::CameraList::save(secondCamera);
  const auto savedBeforeLoad = Furble::CameraList::savedSnapshot();
  check(savedBeforeLoad.size() == 2, "saved cameras are visible before an explicit list load");
  const uint8_t firstId = Furble::CameraList::getCameraId(savedBeforeLoad.at(0).get());
  const uint8_t secondId = Furble::CameraList::getCameraId(savedBeforeLoad.at(1).get());

  Furble::CameraList::clear();
  Furble::CameraList::addFauxNY();
  const auto savedAfterScan = Furble::CameraList::savedSnapshot();
  check(savedAfterScan.size() == 2,
        "a transient scan result does not hide saved cameras");
  check(Furble::CameraList::getCameraId(savedAfterScan.at(0).get()) == firstId
            && Furble::CameraList::getCameraId(savedAfterScan.at(1).get()) == secondId,
        "a transient scan does not change saved camera ids");
  Furble::CameraList::load();

  check(Furble::CameraList::size() == 2, "both saved cameras load back");
  check(Furble::CameraList::get(0).get() == savedBeforeLoad.at(0).get(),
        "load preserves the saved camera pointer identity");
  check(Furble::CameraList::get(1).get() == savedBeforeLoad.at(1).get(),
        "load preserves the second saved camera pointer identity");
  check(firstId != 0 && secondId != 0, "every saved camera gets a nonzero id");
  check(firstId != secondId, "saved camera ids are distinct");

  MockCentral central;
  CompanionService service(central);
  central.attach(service);
  Furble::Settings::save<std::string>(Furble::Settings::COMPANION_PASSWORD, "camera password");
  service.init();
  central.connect();
  central.setSecurity(true, true);

  // Capability advertises the cameras feature bit.
  const auto capability = CompanionService::getCapability();
  check(capability.version == CompanionService::CAPABILITY_VERSION,
        "capability record carries the capability version");
  check(capability.wire_version == CompanionService::WIRE_VERSION,
        "capability record carries the wire version");
  check((capability.features & CompanionService::FEATURE_SETTINGS_V2) != 0,
        "capability keeps the settings v2 feature bit");
  check((capability.features & CompanionService::FEATURE_CAMERAS) != 0,
        "capability advertises the cameras feature bit");

  // List.
  central.clearEvents();
  check(central.write(CAMERAS_UUID, {CompanionService::CAMERA_OP_LIST, 0xff}),
        "cameras UUID accepts a list request");
  check(central.cameraIndications().size() == 3,
        "list indicates one record per saved camera plus a terminator");
  const auto listFirst = decodeCameraRecord(central.cameraIndications().at(0));
  const auto listSecond = decodeCameraRecord(central.cameraIndications().at(1));
  const auto listEnd = decodeCameraRecord(central.cameraIndications().at(2));
  check(listFirst.name == firstConfig.name, "the first list record carries the camera name");
  check(listFirst.head.camera_id == firstId, "the first list record carries the stable id");
  check(listSecond.head.camera_id == secondId, "the second list record carries the stable id");
  check((listFirst.head.flags & CompanionService::CAMERA_FLAG_SAVED) != 0,
        "list records are marked saved");
  check(listFirst.head.state == CompanionService::CAMERA_IDLE,
        "an unconnected saved camera reports the idle state");
  check(listFirst.head.rssi == CompanionService::CAMERA_RSSI_UNKNOWN,
        "an unconnected saved camera reports an unknown rssi");
  check(listEnd.head.camera_id == 0xff && listEnd.head.status == CompanionService::CAMERA_OK,
        "the list terminates with the all-cameras id");

  // LIST is read-only, but every camera mutation requires the configured
  // password and must not reach the UI queue while this session is unauthenticated.
  const auto checkUnauthenticatedCameraMutation = [&](uint8_t operation, const char *message) {
    central.clearEvents();
    drainRequests();
    check(central.write(CAMERAS_UUID, {operation, firstId}), message);
    check(central.cameraIndications().empty(), "unauthenticated camera mutation has no camera response");
    check(central.authIndications().size() == 1
              && central.authIndications()[0].size() == CompanionService::AUTH_RESULT_SIZE
              && central.authIndications()[0][2] == CompanionService::AUTH_RESULT_REJECTED,
          "unauthenticated camera mutation is rejected on Auth");
    check(drainRequests().empty(), "unauthenticated camera mutation does not queue UI work");
  };
  checkUnauthenticatedCameraMutation(CompanionService::CAMERA_OP_SELECT,
                                     "unauthenticated camera select reaches the gate");
  checkUnauthenticatedCameraMutation(CompanionService::CAMERA_OP_DESELECT,
                                     "unauthenticated camera deselect reaches the gate");
  checkUnauthenticatedCameraMutation(CompanionService::CAMERA_OP_CONNECT,
                                     "unauthenticated camera connect reaches the gate");
  checkUnauthenticatedCameraMutation(CompanionService::CAMERA_OP_DISCONNECT,
                                     "unauthenticated camera disconnect reaches the gate");
  Furble::Settings::save<std::string>(Furble::Settings::COMPANION_PASSWORD, "");
  service.reloadPassword();

  // Select and deselect.
  central.clearEvents();
  check(central.write(CAMERAS_UUID, {CompanionService::CAMERA_OP_SELECT, firstId}),
        "cameras UUID accepts a select request");
  check(!central.cameraIndications().empty()
            && decodeCameraRecord(central.cameraIndications().at(0)).head.status
                   == CompanionService::CAMERA_OK,
        "select is acknowledged");
  check(Furble::CameraList::get(0)->isActive(), "select marks the camera as a connect target");
  check(!Furble::CameraList::get(1)->isActive(), "select leaves the other camera alone");

  central.clearEvents();
  check(central.write(CAMERAS_UUID, {CompanionService::CAMERA_OP_DESELECT, firstId}),
        "cameras UUID accepts a deselect request");
  check(!Furble::CameraList::get(0)->isActive(), "deselect clears the connect target");

  central.clearEvents();
  check(central.write(CAMERAS_UUID, {CompanionService::CAMERA_OP_SELECT, 0x7e}),
        "cameras UUID accepts a select for an unknown id");
  check(!central.cameraIndications().empty()
            && decodeCameraRecord(central.cameraIndications().at(0)).head.status
                   == CompanionService::CAMERA_UNKNOWN_ID,
        "selecting an unknown id is rejected as unknown");

  Furble::Settings::setBool(Furble::Settings::MULTICONNECT, false);
  central.clearEvents();
  check(central.write(CAMERAS_UUID, {CompanionService::CAMERA_OP_SELECT, firstId}),
        "cameras UUID accepts a select with multi-connect off");
  check(!central.cameraIndications().empty()
            && decodeCameraRecord(central.cameraIndications().at(0)).head.status
                   == CompanionService::CAMERA_REJECTED,
        "select is rejected while multi-connect is off");
  Furble::Settings::setBool(Furble::Settings::MULTICONNECT, true);

  // Connect routes through the UI request queue, never through a private path.
  Furble::CameraList::clear();
  Furble::CameraList::addFauxNY();
  drainRequests();
  central.clearEvents();
  check(central.write(CAMERAS_UUID, {CompanionService::CAMERA_OP_CONNECT, secondId}),
        "cameras UUID accepts a connect request");
  auto requests = drainRequests();
  check(requests.size() == 1, "connect queues exactly one UI request");
  check(!requests.empty() && requests.at(0).request == Furble::UI::Request::CONNECT_SAVED,
        "connect queues the UI saved-camera request");
  check(!requests.empty() && requests.at(0).arg == secondId,
        "connect passes the stable id of the requested camera");
  check(!central.cameraIndications().empty()
            && decodeCameraRecord(central.cameraIndications().at(0)).head.status
                   == CompanionService::CAMERA_OK,
        "connect is acknowledged");
  Furble::CameraList::load();

  central.clearEvents();
  check(central.write(CAMERAS_UUID, {CompanionService::CAMERA_OP_CONNECT, 0xff}),
        "cameras UUID accepts a connect-all request");
  requests = drainRequests();
  check(central.cameraIndications().size() == 1
            && decodeCameraRecord(central.cameraIndications().at(0)).head.status
                   == CompanionService::CAMERA_REJECTED,
        "connect-all with nothing selected is rejected");
  check(requests.empty(), "a rejected connect queues no UI request");

  check(central.write(CAMERAS_UUID, {CompanionService::CAMERA_OP_SELECT, secondId}),
        "select before connect-all");
  central.clearEvents();
  drainRequests();
  check(central.write(CAMERAS_UUID, {CompanionService::CAMERA_OP_CONNECT, 0xff}),
        "cameras UUID accepts a connect-all with a selection");
  requests = drainRequests();
  check(requests.size() == 1 && requests.at(0).request == Furble::UI::Request::CONNECT_SAVED
            && requests.at(0).arg == 0xff,
        "connect-all asks the UI task for the current selection by stable id");

  // Disconnect.
  central.clearEvents();
  drainRequests();
  check(central.write(CAMERAS_UUID, {CompanionService::CAMERA_OP_DISCONNECT, 0xff}),
        "cameras UUID accepts a disconnect request");
  requests = drainRequests();
  check(requests.size() == 1 && requests.at(0).request == Furble::UI::Request::DISCONNECT,
        "disconnect queues the UI disconnect request");
  check(!central.cameraIndications().empty()
            && decodeCameraRecord(central.cameraIndications().at(0)).head.status
                   == CompanionService::CAMERA_OK,
        "disconnect is acknowledged");

  Furble::Host::setUIRequestsAccepted(false);
  central.clearEvents();
  check(central.write(CAMERAS_UUID, {CompanionService::CAMERA_OP_DISCONNECT, 0xff}),
        "cameras UUID accepts a disconnect the UI cannot queue");
  check(!central.cameraIndications().empty()
            && decodeCameraRecord(central.cameraIndications().at(0)).head.status
                   == CompanionService::CAMERA_BUSY,
        "a full UI request queue answers busy");
  Furble::Host::setUIRequestsAccepted(true);
  drainRequests();

  // Malformed requests.
  central.clearEvents();
  check(central.write(CAMERAS_UUID, {CompanionService::CAMERA_OP_LIST}),
        "cameras UUID accepts a short request");
  check(!central.cameraIndications().empty()
            && decodeCameraRecord(central.cameraIndications().at(0)).head.status
                   == CompanionService::CAMERA_REJECTED,
        "a short request is rejected");
  central.clearEvents();
  check(central.write(CAMERAS_UUID, {0x55, 0xff}), "cameras UUID accepts an unknown op");
  check(!central.cameraIndications().empty()
            && decodeCameraRecord(central.cameraIndications().at(0)).head.status
                   == CompanionService::CAMERA_REJECTED,
        "an unknown op is rejected");

  // Steady-state rate limit, driven by the mock clock rather than a sleep.
  central.clearEvents();
  service.notifyCameras(true);
  check(central.cameraNotifications().size() == 2, "a forced batch notifies every camera");
  central.clearEvents();
  service.notifyCameras();
  check(central.cameraNotifications().empty(),
        "an unchanged batch inside the rate limit notifies nothing");
  Furble::CameraList::get(0)->setActive(!Furble::CameraList::get(0)->isActive());
  service.notifyCameras();
  check(central.cameraNotifications().empty(),
        "even a changed batch waits for the rate limit window");
  furble_host_advance_time(1000 * 1000);
  service.notifyCameras();
  check(central.cameraNotifications().size() == 1,
        "after the rate limit window only the changed camera is notified");
  central.clearEvents();
  furble_host_advance_time(1000 * 1000);
  service.notifyCameras();
  check(central.cameraNotifications().empty(),
        "an unchanged batch after the window still notifies nothing");

  // Ids survive a delete of another entry.
  const uint8_t survivorId = secondId;
  Furble::CameraList::remove(Furble::CameraList::get(0).get());
  Furble::CameraList::load();
  check(Furble::CameraList::size() == 1, "removing one camera leaves the other saved");
  check(Furble::CameraList::getCameraId(Furble::CameraList::get(0).get()) == survivorId,
        "a stable id survives the delete of an earlier entry");

  service.deinit();
  central.disconnect();
  Furble::CameraList::clear();
  Furble::Host::clearPreferences();
  NimBLEDevice::resetMock();
}

}  // namespace

int main(void) {
  FurbleHostTaskScope taskScope;
  testPasswordLoadFailureDeniesPrivilegedWrites();
  testAuthDisconnectRace();
  testCompanionGattFlow();
  testCompanionCameras();
  if (g_Failures != 0) {
    std::cerr << "companion mock-central tests: " << g_Failures << " FAILED\n";
    return 1;
  }
  std::cout << "companion mock-central tests: PASS\n";
  return 0;
}
