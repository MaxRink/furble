#include <array>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include <NimBLEAdvertisedDevice.h>
#include <NimBLEDevice.h>

#include "Camera.h"
#include "CameraList.h"
#include "Device.h"
#include "DJIOsmo.h"
#include "MockNimBLE.h"
#include "advertisement_preferences_stub.h"
#include "protocol/CameraListProtocol.h"

const char *LOG_TAG = "advertisement-dispatch";

namespace {

class DJIProtocolPeer final: public NimBLEMockPeer {
 public:
  explicit DJIProtocolPeer(const NimBLEAddress &address) : m_Address(address) {}

  const std::vector<uint8_t> &firstRequest() const { return m_FirstRequest; }
  void clearFirstRequest() { m_FirstRequest.clear(); }

  bool acceptConnection(NimBLEClient &, const NimBLEAddress &address) override {
    return address == m_Address;
  }
  void disconnect(NimBLEClient &, int) override {}
  bool hasService(const NimBLEUUID &service) const override { return service == serviceUuid(); }
  bool hasCharacteristic(const NimBLEUUID &service,
                         const NimBLEUUID &characteristic) const override {
    return service == serviceUuid()
           && (characteristic == notifyUuid() || characteristic == writeUuid());
  }
  bool discoverCharacteristic(NimBLEClient &,
                              const NimBLEUUID &,
                              const NimBLEUUID &) override {
    return true;
  }
  bool canWrite(const NimBLEUUID &service, const NimBLEUUID &characteristic) const override {
    return service == serviceUuid() && characteristic == writeUuid();
  }
  bool write(NimBLEClient &client,
             const NimBLEUUID &service,
             const NimBLEUUID &characteristic,
             const std::vector<uint8_t> &value,
             bool) override {
    if (service != serviceUuid() || characteristic != writeUuid()) {
      return false;
    }
    if (m_FirstRequest.empty() && value.size() >= 15 && value[12] == 0x00
        && value[13] == 0x19) {
      m_FirstRequest = value;
      // Answer the production handshake so connect() reaches the actual DJI
      // request write and can be checked below.
      if (m_Callback) {
        auto response = connectionResponse();
        m_Callback(m_Remote, response.data(), response.size(), false);
      }
    }
    (void)client;
    return true;
  }
  NimBLEAttValue read(NimBLEClient &, const NimBLEUUID &, const NimBLEUUID &) override { return {}; }
  bool subscribe(NimBLEClient &,
                 const NimBLEUUID &service,
                 const NimBLEUUID &characteristic,
                 bool,
                 NimBLERemoteCharacteristic *remote,
                 const NimBLENotifyCallback &callback,
                 bool) override {
    if (service != serviceUuid() || characteristic != notifyUuid()) {
      return false;
    }
    m_Remote = remote;
    m_Callback = callback;
    return true;
  }
  bool secureConnection(NimBLEClient &) override { return true; }
  bool updateConnectionParams(NimBLEClient &, uint16_t, uint16_t, uint16_t, uint16_t) override {
    return true;
  }
  int getRssi() const override { return 0; }

 private:
  static NimBLEUUID serviceUuid() { return NimBLEUUID(0xFFF0); }
  static NimBLEUUID notifyUuid() { return NimBLEUUID(0xFFF4); }
  static NimBLEUUID writeUuid() { return NimBLEUUID(0xFFF5); }

  static uint16_t crc16(const uint8_t *data, size_t length) {
    uint16_t crc = 0x3AA3;
    for (size_t i = 0; i < length; ++i) {
      crc ^= data[i];
      for (uint8_t bit = 0; bit < 8; ++bit) {
        crc = (crc & 1U) != 0 ? static_cast<uint16_t>((crc >> 1) ^ 0xA001U)
                              : static_cast<uint16_t>(crc >> 1);
      }
    }
    return crc;
  }

  static uint32_t crc32(const uint8_t *data, size_t length) {
    uint32_t crc = 0x00003AA3;
    for (size_t i = 0; i < length; ++i) {
      crc ^= data[i];
      for (uint8_t bit = 0; bit < 8; ++bit) {
        crc = (crc & 1U) != 0 ? (crc >> 1) ^ 0xEDB88320U : crc >> 1;
      }
    }
    return crc;
  }

  static std::vector<uint8_t> connectionResponse() {
    std::vector<uint8_t> frame(51, 0);
    frame[0] = 0xAA;
    frame[1] = static_cast<uint8_t>(frame.size());
    frame[12] = 0x00;
    frame[13] = 0x19;
    frame[14] = 0x00;
    frame[15] = 0x00;
    frame[16] = 0xFF;
    frame[17] = 0x44;
    frame[40] = 2;
    frame[10] = static_cast<uint8_t>(crc16(frame.data(), 10));
    frame[11] = static_cast<uint8_t>(crc16(frame.data(), 10) >> 8);
    const uint32_t crc = crc32(frame.data(), frame.size() - 4);
    for (size_t i = 0; i < 4; ++i) {
      frame[frame.size() - 4 + i] = static_cast<uint8_t>(crc >> (8 * i));
    }
    return frame;
  }

  NimBLEAddress m_Address;
  NimBLERemoteCharacteristic *m_Remote = nullptr;
  NimBLENotifyCallback m_Callback;
  std::vector<uint8_t> m_FirstRequest;
};

#define CHECK(condition)                                                              \
  do {                                                                                \
    if (!(condition)) {                                                               \
      std::cerr << "check failed at line " << __LINE__ << ": " << #condition << '\n'; \
      return false;                                                                   \
    }                                                                                 \
  } while (false)

bool testDispatchAndDeduplication() {
  using Furble::Camera;
  Furble::Host::clearPreferences();
  Furble::Host::failNextBegin();
  CHECK(Furble::CameraList::savedSnapshot().empty());
  CHECK(Furble::CameraList::savedSnapshot().empty());

  Furble::CameraList::clear();
  CHECK(!Furble::CameraList::match(nullptr));
  CHECK(Furble::CameraList::size() == 0);

  const std::array<uint8_t, 8> lumix = {0x3a, 0x00, 0x07, 0x10, 0x20, 0x30, 0x40, 0x50};
  NimBLEAdvertisedDevice device;
  device.setAddress(NimBLEAddress(0x112233445566ULL));
  device.setManufacturerData(lumix.data(), lumix.size());
  device.addServiceUUID(NimBLEUUID("054ac620-3214-11e6-ac0d-0002a5d5c51b"));

  CHECK(Furble::CameraList::match(&device));
  CHECK(Furble::CameraList::size() == 1);
  CHECK(Furble::CameraList::last()->getType() == Camera::Type::PANASONIC_LUMIX);
  CHECK(!Furble::CameraList::match(&device));
  CHECK(Furble::CameraList::size() == 1);

  NimBLEAdvertisedDevice djiAdvertisement;
  djiAdvertisement.setAddress(NimBLEAddress(0x223344556677ULL));
  djiAdvertisement.setName("DJI Osmo Action 5 Pro");
  auto dji = std::make_shared<Furble::DJIOsmo>(&djiAdvertisement);
  CHECK(dji->getPairType() == Camera::PairType::NEW);

  const std::string djiKey =
      Furble::CameraListProtocol::addressKey(static_cast<uint64_t>(djiAdvertisement.getAddress()));
  Furble::Host::failNextPutForKey(djiKey.c_str());
  Furble::CameraList::save(dji);
  CHECK(dji->getPairType() == Camera::PairType::NEW);
  CHECK(Furble::CameraList::savedSnapshot().empty());

  DJIProtocolPeer peer(djiAdvertisement.getAddress());
  NimBLEDevice::setMockPeer(&peer);
  CHECK(dji->connect(ESP_PWR_LVL_P3, 1000));
  CHECK(peer.firstRequest().size() == 51);
  CHECK(peer.firstRequest()[40] == 0x01);
  dji->disconnect();

  Furble::CameraList::save(dji);
  CHECK(dji->getPairType() == Camera::PairType::SAVED);
  peer.clearFirstRequest();
  CHECK(dji->connect(ESP_PWR_LVL_P3, 1000));
  CHECK(peer.firstRequest().size() == 51);
  CHECK(peer.firstRequest()[40] == 0x00);
  dji->disconnect();

  // A failed index write must not replace the published saved catalog.
  Furble::Host::failNextPutForKey("index");
  Furble::CameraList::save(dji);
  CHECK(Furble::CameraList::savedSnapshot().size() == 1);

  // A failed index erase must retain both the catalog entry and its bond.
  const size_t bondsBeforeFailedRemove = NimBLEDevice::deleteBondCount();
  Furble::Host::failNextRemoveForKey("index");
  Furble::CameraList::remove(dji.get());
  CHECK(Furble::CameraList::savedSnapshot().size() == 1);
  CHECK(NimBLEDevice::deleteBondCount() == bondsBeforeFailedRemove);
  Furble::CameraList::remove(dji.get());
  CHECK(Furble::CameraList::savedSnapshot().empty());
  CHECK(NimBLEDevice::deleteBondCount() == bondsBeforeFailedRemove + 1);

  Furble::CameraList::clear();
  return true;
}

}  // namespace

int main() {
  NimBLEDevice::resetMock();
  Furble::Device::init(ESP_PWR_LVL_P3);
  if (!testDispatchAndDeduplication())
    return 1;
  std::cout << "PASS advertisement dispatch and deduplication\n";
  return 0;
}
