// Unit tests for the Fujifilm displayed-name derivation.
//
// Fujifilm advertises the bare model in the BLE local name, so the scan list
// and the saved list both showed "X100VI" while the camera's own Bluetooth
// screen showed a longer label. That label is never on the air. The advertised
// serial is, so the displayed name is the model plus the serial. These tests
// pin the pure derivation, the advertisement path, and the saved-entry path,
// and they prove the matcher acceptance rules are untouched.

#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "Device.h"
#include "FujifilmBasic.h"
#include "FujifilmSecure.h"
#include "FujifilmVirtualCamera.h"
#include "NimBLEDevice.h"
#include "protocol/FujifilmProtocol.h"

const char *LOG_TAG = "fujifilm-device-name";

namespace {

int g_failures = 0;

void check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    g_failures++;
  }
}

// The serial observed on the bench in the X100VI advertisement.
constexpr std::array<uint8_t, 5> BENCH_SERIAL = {'1', 'C', '4', 'F', '9'};

void testFormatSerial() {
  using Furble::FujifilmProtocol::formatSerial;

  check(formatSerial(BENCH_SERIAL) == "1C4F9", "an ASCII serial renders as its own characters");

  const std::array<uint8_t, 5> lower = {'a', 'b', '0', '9', 'z'};
  check(formatSerial(lower) == "ab09z", "lower case serial characters are accepted");

  const std::array<uint8_t, 5> binary = {0xde, 0xad, 0xbe, 0xef, 0x01};
  check(formatSerial(binary) == "DEADBEEF01", "a non text serial falls back to uppercase hex");

  // One byte outside the alphanumeric set is enough to make the whole field
  // binary. A partial mix must not be printed as half text.
  const std::array<uint8_t, 5> mixed = {'1', 'C', '4', 'F', 0x00};
  check(formatSerial(mixed) == "3143344600", "a partly printable serial falls back to hex");

  const std::array<uint8_t, 5> blank = {0x00, 0x00, 0x00, 0x00, 0x00};
  check(formatSerial(blank).empty(), "an unparsed serial renders as nothing");
}

void testDeviceName() {
  using Furble::FujifilmProtocol::deviceName;

  check(deviceName("X100VI", BENCH_SERIAL) == "X100VI 1C4F9",
        "the advertised model gains the serial");

  // Rebuilding a saved entry runs the same composition over a name that
  // already carries the serial. It must not grow a second suffix.
  check(deviceName("X100VI 1C4F9", BENCH_SERIAL) == "X100VI 1C4F9",
        "composing an already composed name is idempotent");

  const std::array<uint8_t, 5> blank = {0x00, 0x00, 0x00, 0x00, 0x00};
  check(deviceName("X100VI", blank) == "X100VI",
        "a camera with no parsed serial keeps the advertised name");

  check(deviceName("", BENCH_SERIAL) == "1C4F9",
        "a nameless advertisement still identifies the body by serial");

  check(deviceName("", blank).empty(), "nothing in gives nothing out");

  // A model whose name happens to end in the serial characters without the
  // separator is not the same string, so it still gains the suffix.
  check(deviceName("X1C4F", BENCH_SERIAL) == "X1C4F 1C4F9",
        "a near miss suffix is not mistaken for the serial");
}

Furble::Host::FujifilmVirtualCamera::Config secureConfig() {
  Furble::Host::FujifilmVirtualCamera::Config config;
  config.secure = true;
  config.name = "X100VI";
  config.serial = BENCH_SERIAL;
  return config;
}

void testAdvertisedCameraName() {
  NimBLEDevice::resetMock();
  Furble::Device::init(ESP_PWR_LVL_P3);
  Furble::Host::FujifilmVirtualCamera peer(secureConfig());
  const auto advertisement = peer.advertisement();

  check(Furble::FujifilmSecure::matches(&advertisement),
        "the bench advertisement is still accepted as a Secure camera");

  Furble::FujifilmSecure camera(&advertisement);
  check(camera.getName() == "X100VI 1C4F9", "a scanned Secure camera shows model and serial");
}

void testSavedCameraName() {
  NimBLEDevice::resetMock();
  Furble::Device::init(ESP_PWR_LVL_P3);
  Furble::Host::FujifilmVirtualCamera peer(secureConfig());
  const auto advertisement = peer.advertisement();
  Furble::FujifilmSecure scanned(&advertisement);

  std::vector<uint8_t> stored(scanned.getSerialisedBytes());
  check(scanned.serialise(stored.data(), stored.size()), "a Secure camera serialises");

  Furble::FujifilmSecure saved(stored.data(), stored.size());
  check(saved.getName() == "X100VI 1C4F9", "a saved Secure camera shows model and serial");

  // The stored record is unchanged in size and layout, so no NVS migration is
  // needed. The name field is the first MAX_NAME bytes of the record.
  check(stored.size() == scanned.getSerialisedBytes(), "the stored record size is unchanged");

  // An entry written before this change stored the bare advertised model. It
  // must gain the serial on load, without a re-pair, because the serial was
  // always stored alongside it.
  std::vector<uint8_t> legacy = stored;
  std::memset(legacy.data(), 0, MAX_NAME);
  std::memcpy(legacy.data(), "X100VI", std::strlen("X100VI"));
  Furble::FujifilmSecure upgraded(legacy.data(), legacy.size());
  check(upgraded.getName() == "X100VI 1C4F9", "a legacy saved entry gains the serial on load");
}

void testBasicNameUnchanged() {
  NimBLEDevice::resetMock();
  Furble::Device::init(ESP_PWR_LVL_P3);
  Furble::Host::FujifilmVirtualCamera::Config config;
  config.name = "X-T4";
  Furble::Host::FujifilmVirtualCamera peer(config);
  const auto advertisement = peer.advertisement();

  check(Furble::FujifilmBasic::matches(&advertisement),
        "the Basic advertisement is still accepted");

  // A Basic body advertises a rotating pairing token, not a serial, so there
  // is no stable identity to append. Its name stays exactly as advertised.
  Furble::FujifilmBasic camera(&advertisement);
  check(camera.getName() == "X-T4", "a Basic camera keeps the advertised name");
}

}  // namespace

int main() {
  testFormatSerial();
  testDeviceName();
  testAdvertisedCameraName();
  testSavedCameraName();
  testBasicNameUnchanged();

  if (g_failures > 0) {
    std::cerr << "fujifilm device name tests: " << g_failures << " FAILED\n";
    return 1;
  }
  std::cout << "fujifilm device name tests: PASS\n";
  return 0;
}
