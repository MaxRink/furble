// Host test for the transmit power mapping between the app setting and the
// camera library.
//
// Control keeps the ESP-IDF esp_power_level_t for the setting and the adaptive
// policy, and lib/furble takes Furble::TxPower. This pins the mapping through
// the public Control API: P3, P6 and P9 reach the controller as 3, 6 and 9
// dBm, any other ESP level falls back to +3 dBm, and the saved setting maps to
// TxPower::P3 with the host settings double.

#include <iostream>

#include "Device.h"
#include "NimBLEDevice.h"

#include "FurbleControl.h"

const char *LOG_TAG = "furble-host";

namespace {

int g_Failures = 0;

void check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "  FAIL: " << message << '\n';
    g_Failures++;
  }
}

}  // namespace

int main() {
  NimBLEDevice::resetMock();
  Furble::Device::init(Furble::TxPower::P3);
  auto &control = Furble::Control::getInstance();

  check(Furble::Device::powerLevelToDbm(Furble::TxPower::P3) == 3, "TxPower::P3 is +3 dBm");
  check(Furble::Device::powerLevelToDbm(Furble::TxPower::P6) == 6, "TxPower::P6 is +6 dBm");
  check(Furble::Device::powerLevelToDbm(Furble::TxPower::P9) == 9, "TxPower::P9 is +9 dBm");

  control.setPower(ESP_PWR_LVL_P6);
  check(NimBLEDevice::getPower() == 6, "ESP_PWR_LVL_P6 reaches the controller as +6 dBm");
  control.setPower(ESP_PWR_LVL_P9);
  check(NimBLEDevice::getPower() == 9, "ESP_PWR_LVL_P9 reaches the controller as +9 dBm");
  control.setPower(ESP_PWR_LVL_P3);
  check(NimBLEDevice::getPower() == 3, "ESP_PWR_LVL_P3 reaches the controller as +3 dBm");
  control.setPower(ESP_PWR_LVL_P12);
  check(NimBLEDevice::getPower() == 3, "an unsupported ESP level falls back to +3 dBm");

  check(Furble::Control::savedTxPower() == Furble::TxPower::P3,
        "the saved P3 setting maps to TxPower::P3");

  if (g_Failures != 0) {
    std::cerr << g_Failures << " check(s) failed\n";
    return 1;
  }
  std::cout << "tx power mapping: PASS\n";
  return 0;
}
