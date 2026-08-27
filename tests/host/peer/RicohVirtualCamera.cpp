#include "RicohVirtualCamera.h"

namespace Furble {
namespace Host {
namespace {

const NimBLEUUID INFO_SERVICE {0x9A5ED1C5, 0x74CC, 0x4C50, 0xB5B666A48E7CCFF1};
const NimBLEUUID MODEL_CHARACTERISTIC {0x35FE6272, 0x6AA5, 0x44D9, 0x88E1F09427F51A71};
const NimBLEUUID CAMERA_SERVICE {0x4B445988, 0xCAA0, 0x4DD3, 0x941D37B4F52ACA86};
const NimBLEUUID POWER_CHARACTERISTIC {0xB58CE84C, 0x0666, 0x4DE9, 0xBEC82D27B27B3211};
const NimBLEUUID OPERATION_MODE_CHARACTERISTIC {0x1452335A, 0xEC7F, 0x4877, 0xB8AB0F72E18BB295};
const NimBLEUUID SHOOTING_SERVICE {0x9F00F387, 0x8345, 0x4BBC, 0x8B92B87B52E3091A};
const NimBLEUUID SHOOTING_FLAVOR_CHARACTERISTIC {0xB29E6DE3, 0x1AEC, 0x48C1, 0x9D0502CEA57CE664};
const NimBLEUUID OPERATION_REQUEST_CHARACTERISTIC {0x559644B8, 0xE0BC, 0x4011, 0x929B5CF9199851E7};
const NimBLEUUID CAPTURE_STATUS_CHARACTERISTIC {0xB5589C08, 0xB5FD, 0x46F5, 0xBE7DAB1B8C074CAA};
const NimBLEUUID SELF_TIMER_CHARACTERISTIC {0x009A8E70, 0xB306, 0x4451, 0xB9437F54392EB971};
const NimBLEUUID BT_CONTROL_SERVICE {0x0F291746, 0x0C80, 0x4726, 0x87A73C501FD3B4B6};
const NimBLEUUID PAIRED_NAME_CHARACTERISTIC {0xFE3A32F8, 0xA189, 0x42DE, 0xA391BC81AE4DAA76};
const NimBLEUUID GPS_SERVICE {0x84A0DD62, 0xE8AA, 0x4D0F, 0x91DB819B6724C69E};
const NimBLEUUID GPS_CHARACTERISTIC {0x28F59D60, 0x8B8E, 0x4FCD, 0xA81F61BDB46595A9};
const NimBLEUUID LOCATION_SERVICE {0xF37F568F, 0x9071, 0x445D, 0xA9385441F2E82399};
const NimBLEUUID LOCATION_CHARACTERISTIC {0x9111CDD0, 0x9F01, 0x45C4, 0xA2D4E09E8FB0424D};

}  // namespace

RicohVirtualCamera::RicohVirtualCamera() : RicohVirtualCamera(Config {}) {}

RicohVirtualCamera::RicohVirtualCamera(const Config &config) : m_Config(config) {}

bool RicohVirtualCamera::matches(const NimBLEUUID &left, const NimBLEUUID &right) {
  return left == right;
}

NimBLEAdvertisedDevice RicohVirtualCamera::advertisement() const {
  NimBLEAdvertisedDevice device;
  device.setAddress(m_Config.address);
  device.setName(m_Config.name);
  device.setRSSI(-55);
  device.addServiceUUID(INFO_SERVICE);
  device.addServiceUUID(CAMERA_SERVICE);
  device.addServiceUUID(SHOOTING_SERVICE);
  device.addServiceUUID(BT_CONTROL_SERVICE);
  return device;
}

bool RicohVirtualCamera::cameraBonded() const {
  return m_Config.camera_bonded;
}

void RicohVirtualCamera::removeCameraBond() {
  m_Config.camera_bonded = false;
}

const std::vector<RicohVirtualCamera::Write> &RicohVirtualCamera::writes() const {
  return m_Writes;
}

void RicohVirtualCamera::clearEvents() {
  m_Writes.clear();
}

bool RicohVirtualCamera::acceptConnection(NimBLEClient &client, const NimBLEAddress &address) {
  if (m_Connected || address != m_Config.address) {
    return false;
  }
  m_Client = &client;
  m_Connected = true;
  return true;
}

void RicohVirtualCamera::disconnect(NimBLEClient &client, int reason) {
  (void)reason;
  if (m_Client == &client) {
    m_Client = nullptr;
    m_Connected = false;
  }
}

bool RicohVirtualCamera::hasService(const NimBLEUUID &service) const {
  return matches(service, INFO_SERVICE) || matches(service, CAMERA_SERVICE)
         || matches(service, SHOOTING_SERVICE) || matches(service, BT_CONTROL_SERVICE)
         || matches(service, GPS_SERVICE) || matches(service, LOCATION_SERVICE);
}

bool RicohVirtualCamera::hasCharacteristic(const NimBLEUUID &service,
                                           const NimBLEUUID &characteristic) const {
  if (matches(service, INFO_SERVICE))
    return matches(characteristic, MODEL_CHARACTERISTIC);
  if (matches(service, CAMERA_SERVICE)) {
    return matches(characteristic, POWER_CHARACTERISTIC)
           || matches(characteristic, OPERATION_MODE_CHARACTERISTIC);
  }
  if (matches(service, SHOOTING_SERVICE)) {
    return matches(characteristic, SHOOTING_FLAVOR_CHARACTERISTIC)
           || matches(characteristic, OPERATION_REQUEST_CHARACTERISTIC)
           || matches(characteristic, CAPTURE_STATUS_CHARACTERISTIC)
           || matches(characteristic, SELF_TIMER_CHARACTERISTIC);
  }
  if (matches(service, BT_CONTROL_SERVICE))
    return matches(characteristic, PAIRED_NAME_CHARACTERISTIC);
  if (matches(service, GPS_SERVICE))
    return matches(characteristic, GPS_CHARACTERISTIC);
  return matches(service, LOCATION_SERVICE) && matches(characteristic, LOCATION_CHARACTERISTIC);
}

bool RicohVirtualCamera::discoverCharacteristic(NimBLEClient &client,
                                                const NimBLEUUID &service,
                                                const NimBLEUUID &characteristic) {
  (void)client;
  return hasCharacteristic(service, characteristic);
}

bool RicohVirtualCamera::canWrite(const NimBLEUUID &service,
                                  const NimBLEUUID &characteristic) const {
  return (matches(service, SHOOTING_SERVICE)
          && (matches(characteristic, SHOOTING_FLAVOR_CHARACTERISTIC)
              || matches(characteristic, OPERATION_REQUEST_CHARACTERISTIC)))
         || (matches(service, GPS_SERVICE) && matches(characteristic, GPS_CHARACTERISTIC))
         || (matches(service, LOCATION_SERVICE)
             && matches(characteristic, LOCATION_CHARACTERISTIC));
}

bool RicohVirtualCamera::write(NimBLEClient &client,
                               const NimBLEUUID &service,
                               const NimBLEUUID &characteristic,
                               const std::vector<uint8_t> &value,
                               bool response) {
  (void)response;
  if (!m_Connected || m_Client != &client || !canWrite(service, characteristic))
    return false;
  m_Writes.push_back({service.toString(), characteristic.toString(), value});
  return true;
}

NimBLEAttValue RicohVirtualCamera::read(NimBLEClient &client,
                                        const NimBLEUUID &service,
                                        const NimBLEUUID &characteristic) {
  if (!m_Connected || m_Client != &client)
    return {};
  if (matches(service, INFO_SERVICE) && matches(characteristic, MODEL_CHARACTERISTIC)) {
    return NimBLEAttValue(m_Config.name);
  }
  if (matches(service, BT_CONTROL_SERVICE) && matches(characteristic, PAIRED_NAME_CHARACTERISTIC)) {
    return NimBLEAttValue("furble");
  }
  return NimBLEAttValue({0x00});
}

bool RicohVirtualCamera::subscribe(NimBLEClient &client,
                                   const NimBLEUUID &service,
                                   const NimBLEUUID &characteristic,
                                   bool notification,
                                   NimBLERemoteCharacteristic *remote,
                                   const NimBLENotifyCallback &callback,
                                   bool response) {
  (void)notification;
  (void)remote;
  (void)callback;
  (void)response;
  return m_Connected && m_Client == &client && hasCharacteristic(service, characteristic);
}

bool RicohVirtualCamera::secureConnection(NimBLEClient &client) {
  if (!m_Connected || m_Client != &client || !m_Config.accept_numeric_comparison)
    return false;
  const bool localBonded = NimBLEDevice::isBonded(m_Config.address);
  if (localBonded != m_Config.camera_bonded)
    return false;
  if (!localBonded) {
    NimBLEDevice::setBonded(true);
    m_Config.camera_bonded = true;
  }
  return true;
}

bool RicohVirtualCamera::updateConnectionParams(NimBLEClient &client,
                                                uint16_t min_interval,
                                                uint16_t max_interval,
                                                uint16_t latency,
                                                uint16_t timeout) {
  (void)min_interval;
  (void)max_interval;
  (void)latency;
  (void)timeout;
  return m_Connected && m_Client == &client;
}

int RicohVirtualCamera::getRssi() const {
  return -55;
}

const NimBLEUUID &RicohVirtualCamera::shootingFlavorCharacteristicUUID() {
  return SHOOTING_FLAVOR_CHARACTERISTIC;
}

const NimBLEUUID &RicohVirtualCamera::operationRequestCharacteristicUUID() {
  return OPERATION_REQUEST_CHARACTERISTIC;
}

}  // namespace Host
}  // namespace Furble
