// Host conformance for the production CompanionGatt transport
// (src/FurbleCompanion.cpp) over the MockNimBLE server.
//
// The production enable/disable/restart path, bond persistence, reidentify
// address rotation and retained 4 KiB service task run unchanged. Only the
// NimBLE server/advertising API and the ESP timer are doubles.

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "CompanionHostSettings.h"
#include "Device.h"
#include "FurbleCompanion.h"
#include "FurbleCompanionService.h"
#include "FurbleWebUI.h"
#include "NimBLEAdvertising.h"
#include "NimBLEDevice.h"
#include "NimBLEScan.h"
#include "NimBLEServer.h"
#include "Preferences.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "protocol/ProvisionTLV.h"

namespace {

int g_Failures = 0;
int g_WebUIReloads = 0;

void check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "COMPANION FAIL: " << message << '\n';
    g_Failures++;
  }
}

// The service task runs on wall time and makes one pass per second while
// enabled, so allow a few passes before deciding.
template <typename Predicate>
bool waitFor(Predicate predicate) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (std::chrono::steady_clock::now() < deadline) {
    if (predicate()) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return predicate();
}

bool waitForNotifyWaiters(size_t expected) {
  return waitFor([expected]() { return furbleHostNotifyWaiters() == expected; });
}

NimBLEAddress address(uint64_t value) {
  return NimBLEAddress(value, 0);
}

void run(void) {
  NimBLEDevice::resetMock();
  auto *server = NimBLEDevice::getServer();
  auto *advertising = NimBLEDevice::getAdvertising();

  // A prior test case must not leak GATT services, callbacks, started state or
  // counters into this one.
  auto *scratch = server->createService("1234");
  scratch->createCharacteristic("5678", NIMBLE_PROPERTY::READ);
  server->start();
  advertising->reset();
  advertising->start();
  check(server->serviceCount() == 1, "the reset probe did not create a service");
  check(server->started(), "the reset probe did not start the server");
  check(advertising->startCount() == 1, "the reset probe did not start advertising");
  NimBLEDevice::resetMock();
  check(server->serviceCount() == 0, "resetMock kept the GATT services");
  check(server->characteristicCount() == 0, "resetMock kept the GATT characteristics");
  check(!server->started(), "resetMock kept the server started");
  check(!advertising->isAdvertising() && advertising->startCount() == 0,
        "resetMock kept advertising state or counters");
  std::cout << "companion.reset_mock services=0 started=0 advertising=0\n";

  NimBLEDevice::init("furble-host");
  Furble::Device::init(ESP_PWR_LVL_P9);
  Furble::Settings::setBool(Furble::Settings::COMPANION, true);

  auto &companion = Furble::CompanionGatt::getInstance();
  const size_t tasksBefore = furbleHostTaskCreateCount();

  companion.init();
  check(companion.isEnabled(), "the companion did not enable from the stored setting");
  check(!companion.isAdvertising(), "the companion advertised before a window or bond");
  check(server->started(), "the GATT server did not start");
  check(server->serviceCount() == 2, "the companion did not create both GATT services");
  check(server->characteristicCount() == 9, "the companion characteristic count changed");
  check(furbleHostTaskCreateCount() == tasksBefore + 1,
        "the companion did not create exactly one service task");
  check(furbleHostLastTaskStackBytes() == 3072,
        "the companion service task stack is not the production 3 KiB");
  std::cout << "companion.enable services=2 characteristics=9 task=1 stack=3072\n";

  companion.reloadSetting(true);
  check(companion.isPairingWindow(), "the companion did not open the pairing window");
  check(companion.isAdvertising(), "the companion did not start pairing advertising");
  check(advertising->name() == Furble::Device::getStringID(),
        "the companion advertised the wrong device name");
  std::cout << "companion.pairing advertising=1 window=1\n";

  NimBLEConnInfo info;
  info.mockSetConnHandle(7);
  info.mockSetSecurity(false, false, false);
  info.mockSetAddress(address(0x112233445566ULL));
  info.mockSetIdAddress(address(0x112233445566ULL));
  server->mockConnect(info);
  check(companion.isConnected(), "an accepted companion link was not tracked");
  check(!companion.isAdvertising(), "advertising continued after the link connected");
  std::cout << "companion.connect handle=7 auth_started=1\n";

  NimBLEDevice::setBondedAddress(address(0x112233445566ULL));
  info.mockSetSecurity(true, true, true);
  server->mockAuthenticationComplete(info);
  check(companion.hasBond(), "the authenticated companion bond was not saved");
  {
    Furble::Preferences stored;
    check(stored.begin("furble", true), "the companion bond namespace did not open");
    check(stored.isKey("companion_addr"), "the companion bond address was not persisted");
    check(stored.isKey("companion_ty"), "the companion bond type was not persisted");
    stored.end();
  }
  check(Furble::Settings::load<bool>(Furble::Settings::COMPANION),
        "the authentication path changed the companion setting");
  std::cout << "companion.auth bonded=1 encrypted=1 authenticated=1\n";

  // A GATT password write must reach the WebUI credential gate through the
  // production service callback. This never calls WebUI::reloadSetting()
  // directly.
  auto *gatt = server->getService(NimBLEUUID(Furble::CompanionGatt::SERVICE_UUID));
  check(gatt != nullptr, "the companion GATT service was not found");
  auto *settings = gatt == nullptr
                       ? nullptr
                       : gatt->getCharacteristic(NimBLEUUID(Furble::CompanionGatt::SETTINGS_UUID));
  check(settings != nullptr, "the companion settings characteristic was not found");
  if (settings != nullptr) {
    const std::vector<uint8_t> setPassword {
        2, Furble::ProvisionTLV::COMPANION_PASSWORD_WIRE_ID, 3, 'n', 'e', 'w'};
    settings->setValue(setPassword.data(), setPassword.size());
    settings->callbacks()->onWrite(settings, info);
    check(g_WebUIReloads == 1, "a GATT password write did not reload the WebUI gate");
  }
  std::cout << "companion.password_gate webui_reloads=" << g_WebUIReloads << '\n';

  // Graceful disable must retain the service task and the GATT database.
  Furble::Settings::setBool(Furble::Settings::COMPANION, false);
  companion.reloadSetting(false);
  check(furble_host_fire_active_timer(), "the companion disable grace timer was not armed");
  check(!companion.isEnabled(), "the companion did not disable after the grace timer");
  check(!companion.isAdvertising(), "the companion advertised while disabled");
  check(furbleHostTaskCreateCount() == tasksBefore + 1,
        "disable freed and recreated the 4 KiB companion task");
  check(server->serviceCount() == 2 && server->characteristicCount() == 9,
        "disable dropped the retained GATT database");
  // A disabled companion must block on its task notification, not poll.
  check(waitForNotifyWaiters(1), "the disabled companion task did not block on its notification");
  std::cout << "companion.disable enabled=0 task_retained=1 gatt_retained=1 task_blocked=1\n";

  // Re-enable must reuse the retained task and reload the persisted bond.
  Furble::Settings::setBool(Furble::Settings::COMPANION, true);
  companion.reloadSetting(false);
  check(companion.isEnabled(), "the companion did not re-enable");
  check(companion.hasBond(), "re-enable did not reload the persisted companion bond");
  check(furbleHostTaskCreateCount() == tasksBefore + 1,
        "re-enable recreated the 4 KiB companion task");
  check(server->serviceCount() == 2 && server->characteristicCount() == 9,
        "re-enable duplicated the retained GATT database");
  check(waitForNotifyWaiters(0), "re-enable did not wake the blocked companion task");
  // An enable opens the fast reconnect window. The cadence policy itself is
  // covered by companion-advertising.
  check(waitFor([advertising]() {
          return advertising->isAdvertising()
                 && (advertising->interval() == Furble::CompanionAdvertising::FAST_INTERVAL);
        }),
        "re-enable did not start fast reconnect advertising");
  std::cout << "companion.restart enabled=1 bond=loaded task=retained gatt=retained woken=1\n";

  // The fast window closes after 60 s, and any later press reopens it. The UI
  // boosts on every physical press edge, whatever the display state.
  furble_host_advance_time(static_cast<int64_t>(Furble::CompanionAdvertising::FAST_WINDOW_MS + 1000)
                           * 1000);
  check(waitFor([advertising]() {
          return advertising->isAdvertising()
                 && (advertising->interval() == Furble::CompanionAdvertising::SLOW_INTERVAL);
        }),
        "the fast window did not fall back to slow reconnect advertising");
  companion.boostAdvertising();
  check(waitFor([advertising]() {
          return advertising->isAdvertising()
                 && (advertising->interval() == Furble::CompanionAdvertising::FAST_INTERVAL);
        }),
        "a press did not reopen fast reconnect advertising");
  std::cout << "companion.cadence window_closed=slow press=fast\n";

  const std::string oldAddress = NimBLEDevice::getAddress().toString();
  check(companion.reidentify(), "reidentify failed while the companion was enabled");
  check(!companion.hasBond(), "reidentify kept the old companion bond");
  check(NimBLEDevice::getNumBonds() == 0, "reidentify did not delete the host bond");
  check(companion.isPairingWindow() && companion.isAdvertising(),
        "reidentify did not reopen the pairing window");
  const std::string newAddress = NimBLEDevice::getAddress().toString();
  check(newAddress != oldAddress, "reidentify did not rotate the LE address");
  std::cout << "companion.reidentify address_changed=1 bond=0 window=1\n";

  // A second disable/re-enable after the bond rotation still retains everything.
  Furble::Settings::setBool(Furble::Settings::COMPANION, false);
  companion.reloadSetting(false);
  (void)furble_host_fire_active_timer();
  Furble::Settings::setBool(Furble::Settings::COMPANION, true);
  companion.reloadSetting(false);
  check(furbleHostTaskCreateCount() == tasksBefore + 1,
        "the second restart recreated the companion task");
  check(server->serviceCount() == 2 && server->characteristicCount() == 9,
        "the second restart duplicated the GATT database");
  std::cout << "companion.restart second=1 task=retained gatt=retained\n";

  companion.disableNow();
  check(!companion.isEnabled(), "disableNow left the companion enabled");
}

}  // namespace

namespace Furble {

WebUI &WebUI::getInstance(void) {
  static WebUI instance;
  return instance;
}

void WebUI::reloadSetting(void) {
  g_WebUIReloads++;
}

}  // namespace Furble

int main() {
  const char *prefs = std::getenv("FURBLE_SIM_PREFS");
  if (prefs != nullptr) {
    std::remove(prefs);
  }
  run();
  furbleHostStopTasks();
  if (g_Failures != 0) {
    std::cerr << "companion_gatt_transport_test: " << g_Failures << " failure(s)\n";
    return 1;
  }
  std::cout << "companion_gatt_transport_test: PASS\n";
  return 0;
}
