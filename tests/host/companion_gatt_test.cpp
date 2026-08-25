// Host coverage for the transport-independent companion GATT service.
//
// MockCentral addresses the same characteristic UUIDs exposed by
// CompanionGatt, then routes the ATT-like operations through the production
// CompanionService transport seam. The downstream location and trigger paths
// use the real Control, Camera, Fujifilm protocol and MockNimBLE virtual peers.

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "CameraList.h"
#include "Device.h"
#include "FujifilmBasic.h"
#include "FujifilmVirtualCamera.h"
#include "FurbleCompanionService.h"
#include "FurbleControl.h"
#include "FurbleGPS.h"
#include "FurbleSettings.h"
#include "FurbleUI.h"
#include "NimBLEDevice.h"
#include "advertisement_preferences_stub.h"
#include "protocol/FujifilmProtocol.h"

namespace {

constexpr const char *LOCATION_UUID = "b57f4f5e-087b-4740-b71d-8262cf26ebbc";
constexpr const char *STATUS_UUID = "b57f4f60-087b-4740-b71d-8262cf26ebbc";
constexpr const char *SETTINGS_UUID = "b57f4f61-087b-4740-b71d-8262cf26ebbc";
constexpr const char *TRIGGER_UUID = "b57f4f62-087b-4740-b71d-8262cf26ebbc";
constexpr const char *CAMERAS_UUID = "b57f4f63-087b-4740-b71d-8262cf26ebbc";

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

  void disconnect(void) {
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
    m_CameraEvents.clear();
    m_HaveStatus = false;
  }

  void reenterCameraNotifyOnce(void) { m_ReenterCameraNotify = true; }

  bool isConnected(void) const override { return m_Connected; }

  bool isEncrypted(void) const override { return m_Encrypted; }

  bool isAuthenticated(void) const override { return m_Authenticated; }

  uint16_t getMaxPayload(void) const override { return 244; }

  void notify(uint8_t charId, const uint8_t *data, size_t len) override {
    if (data == nullptr) {
      return;
    }
    if (charId == Furble::COMPANION_CHAR_STATUS && len == sizeof(m_Status)) {
      std::memcpy(&m_Status, data, sizeof(m_Status));
      m_HaveStatus = true;
    } else if (charId == Furble::COMPANION_CHAR_CAMERAS) {
      m_CameraEvents.emplace_back(data, data + len);
      if (m_ReenterCameraNotify && (m_Service != nullptr)) {
        m_ReenterCameraNotify = false;
        m_Service->notifyCameras(false);
      }
    }
  }

  void indicate(uint8_t charId, const uint8_t *data, size_t len) override {
    if (data == nullptr) {
      return;
    }
    if (charId == Furble::COMPANION_CHAR_SETTINGS) {
      m_Indications.emplace_back(data, data + len);
    } else if (charId == Furble::COMPANION_CHAR_CAMERAS) {
      m_CameraEvents.emplace_back(data, data + len);
    }
  }

  void error(uint8_t, uint8_t) override {}

  bool haveStatus(void) const { return m_HaveStatus; }

  const std::vector<std::vector<uint8_t>> &indications(void) const { return m_Indications; }

  const std::vector<std::vector<uint8_t>> &cameraEvents(void) const { return m_CameraEvents; }

 private:
  CompanionService *m_Service = nullptr;
  bool m_Connected = false;
  bool m_Encrypted = true;
  bool m_Authenticated = true;
  bool m_HaveStatus = false;
  CompanionService::companion_status_t m_Status = {};
  std::vector<std::vector<uint8_t>> m_Indications;
  std::vector<std::vector<uint8_t>> m_CameraEvents;
  bool m_ReenterCameraNotify = false;
};

std::vector<uint8_t> bytesOf(const CompanionService::companion_fix_t &fix) {
  const auto *begin = reinterpret_cast<const uint8_t *>(&fix);
  return {begin, begin + sizeof(fix)};
}

void testCompanionGattFlow(void) {
  std::cout << "test: companion mock-central location/status/settings/trigger flow\n";
  NimBLEDevice::resetMock();
  Furble::Settings::setBool(Furble::Settings::SLEEP_CONN, false);
  Furble::Settings::setBool(Furble::Settings::TX_ADAPTIVE, false);
  Furble::Settings::setBool(Furble::Settings::RECON_BACKOFF, false);
  Furble::Settings::setBool(Furble::Settings::CONN_SAVER, false);
  Furble::Settings::setU8(Furble::Settings::BRIGHTNESS, 12);
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
  const NimBLEAdvertisedDevice firstAdvertisement = first.advertisement();
  const NimBLEAdvertisedDevice secondAdvertisement = second.advertisement();
  Furble::CameraList::clear();
  check(Furble::CameraList::match(&firstAdvertisement),
        "first virtual camera enters the shared CameraList");
  check(Furble::CameraList::match(&secondAdvertisement),
        "second virtual camera enters the shared CameraList");
  check(Furble::CameraList::size() == 2, "CameraList exposes both saved-camera candidates");
  auto firstCamera = Furble::CameraList::get(0);
  auto secondCamera = Furble::CameraList::get(1);
  Furble::CameraList::save(firstCamera.get());
  Furble::CameraList::save(secondCamera.get());
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

  central.clearEvents();
  check(central.write(CAMERAS_UUID, {0, 0xff}),
        "cameras characteristic accepts the list operation");
  check(central.cameraEvents().size() == 3,
        "camera list emits one record per saved camera plus an end marker");
  if (central.cameraEvents().size() == 3) {
    const auto &firstRecord = central.cameraEvents()[0];
    check(
        firstRecord.size() == sizeof(CompanionService::companion_camera_record_t) + firstRecord[7],
        "camera record framing is header plus bounded name bytes");
    check(firstRecord[0] == 0 && firstRecord[1] != 0xff,
          "camera list record carries an OK status and stable id");
    const auto &end = central.cameraEvents()[2];
    check(end.size() == sizeof(CompanionService::companion_camera_record_t) && end[0] == 0
              && end[1] == 0xff,
          "camera list terminates with the reserved id marker");
  }

  central.setSecurity(false, false);
  central.clearEvents();
  check(central.write(CAMERAS_UUID, {3, 1}),
        "unauthenticated cameras write reaches the service gate");
  service.notifyCameras(true);
  check(central.cameraEvents().empty(), "unauthenticated cameras write has no indication");

  central.setSecurity(true, true);
  central.clearEvents();
  Furble::Settings::setBool(Furble::Settings::MULTICONNECT, true);
  check(central.write(CAMERAS_UUID, {1, 0xfe}), "unknown camera operation is handled");
  check(central.cameraEvents().size() == 1 && central.cameraEvents()[0][0] == 1
            && central.cameraEvents()[0][1] == 0xfe,
        "unknown camera id returns the explicit UNKNOWN_ID status");

  central.clearEvents();
  central.reenterCameraNotifyOnce();
  check(central.write(CAMERAS_UUID, {3, 1}), "authenticated camera selection operation is handled");
  check(central.cameraEvents().size() >= 2,
        "selection returns an acknowledgement and a refreshed camera record");

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

  first.clearEvents();
  second.clearEvents();
  check(central.write(TRIGGER_UUID, {1, 1}), "trigger UUID accepts shutter press");
  check(central.write(TRIGGER_UUID, {1, 0}), "trigger UUID accepts shutter release");
  check(waitFor([&] { return shutterWriteCount(first) >= 4; }, 2000),
        "remote trigger fires the first connected virtual camera");
  check(waitFor([&] { return shutterWriteCount(second) >= 4; }, 2000),
        "remote trigger fires the second connected virtual camera");

  central.disconnect();
  service.deinit();
  stopControl(control);

  // Every mutating Preferences operation is a separate NVS transaction. The
  // Inject a reset at every actual write boundary and ensure the previous
  // committed generation survives an immediate reboot. A later successful
  // save must also be able to publish a new generation without losing it.
  auto verifyIndexWriteBoundary = [&](size_t boundary) {
    Furble::hostPreferencesClearStorage();
    Furble::CameraList::clear();
    Furble::CameraList::save(firstCamera.get());
    Furble::CameraList::save(secondCamera.get());
    check(Furble::CameraList::getSaveCount() == 2,
          "fault-injection setup creates a valid two-camera generation");

    Furble::hostPreferencesFailAfter(boundary);
    Furble::CameraList::save(firstCamera.get());
    Furble::hostPreferencesResetFaults();
    Furble::CameraList::load();
    const size_t recovered = Furble::CameraList::getSaveCount();
    check(recovered == 2, "interrupted index write recovers the prior generation");
    Furble::CameraList::save(firstCamera.get());
    Furble::CameraList::load();
    check(Furble::CameraList::getSaveCount() == 2,
          "subsequent save after reboot preserves the recovered generation");
  };
  for (size_t boundary = 1; boundary <= 6; boundary++) {
    verifyIndexWriteBoundary(boundary);
  }

  // Exercise the list lock under the same concurrent scan/snapshot pattern
  // used by the companion task. Duplicate advertisement results must still
  // collapse to one strong reference.
  Furble::CameraList::clear();
  std::vector<std::thread> scanners;
  for (size_t worker = 0; worker < 4; worker++) {
    scanners.emplace_back([&firstAdvertisement] {
      for (size_t iteration = 0; iteration < 100; iteration++) {
        (void)Furble::CameraList::match(&firstAdvertisement);
        (void)Furble::CameraList::snapshot();
      }
    });
  }
  for (auto &scanner : scanners) {
    scanner.join();
  }
  check(Furble::CameraList::snapshot().size() == 1,
        "concurrent scan results remain deduplicated under list locking");
  Furble::hostPreferencesClearStorage();
  NimBLEDevice::resetMock();
}

}  // namespace

int main(void) {
  FurbleHostTaskScope taskScope;
  testCompanionGattFlow();
  if (g_Failures != 0) {
    std::cerr << "companion mock-central tests: " << g_Failures << " FAILED\n";
    return 1;
  }
  std::cout << "companion mock-central tests: PASS\n";
  return 0;
}
