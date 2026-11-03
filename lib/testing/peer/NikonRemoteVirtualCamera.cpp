#include <algorithm>
#include <cstring>

#include "NikonRemoteVirtualCamera.h"
#include "WriteJournal.h"

namespace Furble {
namespace Host {
namespace {

// Nikon LSS service and the common set, spec/nikon.md section 3.1. The base is
// 3dd4-4255-8d62-6dc7b9bd5561 for every vendor characteristic.
const NimBLEUUID SERVICE {0x0000de00, 0x3dd4, 0x4255, 0x8d626dc7b9bd5561};
const NimBLEUUID AUTH_CHARACTERISTIC {0x00002000, 0x3dd4, 0x4255, 0x8d626dc7b9bd5561};
const NimBLEUUID POWER_CHARACTERISTIC {0x00002001, 0x3dd4, 0x4255, 0x8d626dc7b9bd5561};
const NimBLEUUID CLIENT_NAME_CHARACTERISTIC {0x00002002, 0x3dd4, 0x4255, 0x8d626dc7b9bd5561};
const NimBLEUUID SERVER_NAME_CHARACTERISTIC {0x00002003, 0x3dd4, 0x4255, 0x8d626dc7b9bd5561};
const NimBLEUUID CONNECTION_CONFIG_CHARACTERISTIC {0x00002004, 0x3dd4, 0x4255, 0x8d626dc7b9bd5561};
const NimBLEUUID CONNECTION_ESTABLISHMENT_CHARACTERISTIC {0x00002005, 0x3dd4, 0x4255,
                                                          0x8d626dc7b9bd5561};
const NimBLEUUID TIME_CHARACTERISTIC {0x00002006, 0x3dd4, 0x4255, 0x8d626dc7b9bd5561};
const NimBLEUUID LOCATION_CHARACTERISTIC {0x00002007, 0x3dd4, 0x4255, 0x8d626dc7b9bd5561};
const NimBLEUUID CONTROL_POINT_CHARACTERISTIC {0x00002008, 0x3dd4, 0x4255, 0x8d626dc7b9bd5561};
const NimBLEUUID FEATURE_CHARACTERISTIC {0x00002009, 0x3dd4, 0x4255, 0x8d626dc7b9bd5561};
const NimBLEUUID CABLE_CHARACTERISTIC {0x0000200a, 0x3dd4, 0x4255, 0x8d626dc7b9bd5561};
const NimBLEUUID SERIAL_CHARACTERISTIC {0x0000200b, 0x3dd4, 0x4255, 0x8d626dc7b9bd5561};
const NimBLEUUID BATTERY_CHARACTERISTIC {0x00002a19, 0x3dd4, 0x4255, 0x8d626dc7b9bd5561};

// Remote profile, branch 1 and branch 2 (section 4.1).
const NimBLEUUID REMOTE_INFO_CHARACTERISTIC {0x00002080, 0x3dd4, 0x4255, 0x8d626dc7b9bd5561};
const NimBLEUUID CAPTURE_STATUS_CHARACTERISTIC {0x00002081, 0x3dd4, 0x4255, 0x8d626dc7b9bd5561};
const NimBLEUUID KEY_MAP_CHARACTERISTIC {0x00002082, 0x3dd4, 0x4255, 0x8d626dc7b9bd5561};
const NimBLEUUID SHUTTER_CHARACTERISTIC {0x00002083, 0x3dd4, 0x4255, 0x8d626dc7b9bd5561};
const NimBLEUUID REMOTE_INDICATION_CHARACTERISTIC {0x00002084, 0x3dd4, 0x4255, 0x8d626dc7b9bd5561};
const NimBLEUUID REMOTE_INFO2_CHARACTERISTIC {0x00002085, 0x3dd4, 0x4255, 0x8d626dc7b9bd5561};
const NimBLEUUID REMOTE_INFO3_CHARACTERISTIC {0x00002086, 0x3dd4, 0x4255, 0x8d626dc7b9bd5561};
const NimBLEUUID PAIRING_CHARACTERISTIC {0x00002087, 0x3dd4, 0x4255, 0x8d626dc7b9bd5561};

const NimBLEUUID DEVICE_INFORMATION_SERVICE {0x0000180a, 0x0000, 0x1000, 0x800000805f9b34fb};
const NimBLEUUID MODEL_CHARACTERISTIC {0x00002a24, 0x0000, 0x1000, 0x800000805f9b34fb};
const NimBLEUUID FIRMWARE_CHARACTERISTIC {0x00002a26, 0x0000, 0x1000, 0x800000805f9b34fb};
const NimBLEUUID SOFTWARE_CHARACTERISTIC {0x00002a28, 0x0000, 0x1000, 0x800000805f9b34fb};

// Key 2 is the shutter key. Operation 2 is a short press, 3 a long press, 0 a
// no-op. Operation 1 and anything else fall through the handler without
// action. See analysis/nikon-b600-handlers.md section 4.4.
constexpr uint8_t SHUTTER_KEY = 0x02;
constexpr uint8_t SHORT_PRESS = 0x02;
constexpr uint8_t LONG_PRESS = 0x03;
constexpr uint8_t NO_OP = 0x00;

constexpr uint8_t CONNECTION_KNOWN_BITS = 0x83;

}  // namespace

NikonRemoteVirtualCamera::NikonRemoteVirtualCamera() : NikonRemoteVirtualCamera(Config {}) {}

NikonRemoteVirtualCamera::NikonRemoteVirtualCamera(const Config &config) : m_Config(config) {
  m_Paired = m_Config.paired;
}

NikonRemoteVirtualCamera::~NikonRemoteVirtualCamera() = default;

bool NikonRemoteVirtualCamera::matches(const NimBLEUUID &left, const NimBLEUUID &right) {
  return left == right;
}

std::string NikonRemoteVirtualCamera::key(const NimBLEUUID &service,
                                          const NimBLEUUID &characteristic) {
  return service.toString() + "/" + characteristic.toString();
}

NimBLEAdvertisedDevice NikonRemoteVirtualCamera::advertisement() const {
  const std::lock_guard<std::recursive_mutex> lock(m_AdvertisementMutex);
  NimBLEAdvertisedDevice device;
  device.setAddress(m_Config.address);
  device.setName(m_Config.name);
  device.setRSSI(-50);
  device.addServiceUUID(SERVICE);

  // A2: the reconnect form is 99 03, the four client ID bytes and the
  // capability byte. The pairing form carries no manufacturer data (A1).
  if (m_Paired) {
    std::vector<uint8_t> manufacturer = {0x99,
                                         0x03,
                                         m_Config.client_id[0],
                                         m_Config.client_id[1],
                                         m_Config.client_id[2],
                                         m_Config.client_id[3],
                                         m_Config.capability};
    device.setManufacturerData(manufacturer.data(), manufacturer.size());
  }
  return device;
}

const NikonRemoteVirtualCamera::Config &NikonRemoteVirtualCamera::config() const {
  return m_Config;
}

const std::vector<NikonRemoteVirtualCamera::Write> &NikonRemoteVirtualCamera::writes() const {
  return m_Writes;
}

const std::vector<NikonRemoteVirtualCamera::Notification> &NikonRemoteVirtualCamera::notifications()
    const {
  return m_Notifications;
}

void NikonRemoteVirtualCamera::clearEvents() {
  m_Writes.clear();
  m_Notifications.clear();
}

bool NikonRemoteVirtualCamera::connected() const {
  return m_Connected;
}

bool NikonRemoteVirtualCamera::paired() const {
  return m_Paired;
}

bool NikonRemoteVirtualCamera::cameraBonded() const {
  return m_Config.camera_bonded;
}

void NikonRemoteVirtualCamera::setCameraBonded(bool bonded) {
  m_Config.camera_bonded = bonded;
}

void NikonRemoteVirtualCamera::setPaired(bool paired) {
  m_Paired = paired;
  m_Config.paired = paired;
}

void NikonRemoteVirtualCamera::removeCameraBond() {
  m_Config.camera_bonded = false;
  m_Config.paired = false;
  m_Paired = false;
  m_PairingStage = 0;
  m_PairingTimestamp = {};
  m_PairingDevice = {};
}

uint8_t NikonRemoteVirtualCamera::pairingStage() const {
  return m_PairingStage;
}

const std::array<uint8_t, 8> &NikonRemoteVirtualCamera::pairingTimestamp() const {
  return m_PairingTimestamp;
}

const std::array<uint8_t, 8> &NikonRemoteVirtualCamera::pairingDevice() const {
  return m_PairingDevice;
}

size_t NikonRemoteVirtualCamera::shortPresses() const {
  return m_ShortPresses;
}

size_t NikonRemoteVirtualCamera::longPresses() const {
  return m_LongPresses;
}

size_t NikonRemoteVirtualCamera::noOps() const {
  return m_NoOps;
}

uint8_t NikonRemoteVirtualCamera::lastKeyId() const {
  return m_LastKeyId;
}

uint8_t NikonRemoteVirtualCamera::lastKeyOperation() const {
  return m_LastKeyOperation;
}

const std::vector<uint8_t> &NikonRemoteVirtualCamera::lastKeyMap() const {
  return m_LastKeyMap;
}

const std::vector<uint8_t> &NikonRemoteVirtualCamera::lastTimePacket() const {
  return m_LastTimePacket;
}

const std::vector<uint8_t> &NikonRemoteVirtualCamera::lastLocationPacket() const {
  return m_LastLocationPacket;
}

bool NikonRemoteVirtualCamera::controlPointWriteSeen() const {
  return m_ControlPointWriteSeen;
}

bool NikonRemoteVirtualCamera::smartAuthWriteRejected() const {
  return m_SmartAuthWriteRejected;
}

size_t NikonRemoteVirtualCamera::rejectedWrites() const {
  return m_RejectedWrites;
}

void NikonRemoteVirtualCamera::setFault(Fault fault) {
  m_Fault = fault;
}

NikonRemoteVirtualCamera::Fault NikonRemoteVirtualCamera::fault() const {
  return m_Fault;
}

void NikonRemoteVirtualCamera::clearFault() {
  m_Fault = Fault::None;
}

void NikonRemoteVirtualCamera::setScripted(bool scripted) {
  m_Scripted = scripted;
}

bool NikonRemoteVirtualCamera::scripted() const {
  return m_Scripted;
}

void NikonRemoteVirtualCamera::scriptNextCommand(Fault fault) {
  m_Script.push_back(fault);
}

size_t NikonRemoteVirtualCamera::scriptedRemaining() const {
  return m_Script.size();
}

void NikonRemoteVirtualCamera::clearScript() {
  m_Script.clear();
}

NikonRemoteVirtualCamera::Fault NikonRemoteVirtualCamera::consumeFault() {
  if (m_Scripted) {
    if (m_Script.empty()) {
      return Fault::None;
    }
    const Fault fault = m_Script.front();
    m_Script.pop_front();
    return fault;
  }
  return m_Fault;
}

bool NikonRemoteVirtualCamera::emitNotification(const NimBLEUUID &service,
                                                const NimBLEUUID &characteristic,
                                                const std::vector<uint8_t> &payload,
                                                bool indication) {
  const auto found = m_Subscriptions.find(key(service, characteristic));
  if ((found == m_Subscriptions.end()) || (found->second.callback == nullptr)) {
    return false;
  }

  Notification notification;
  notification.service = service.toString();
  notification.characteristic = characteristic.toString();
  notification.payload = payload;
  notification.indication = indication;
  m_Notifications.push_back(notification);

  std::vector<uint8_t> mutable_payload = payload;
  found->second.callback(found->second.remote,
                         mutable_payload.empty() ? nullptr : mutable_payload.data(),
                         mutable_payload.size(), !indication);
  return true;
}

bool NikonRemoteVirtualCamera::emitPairingStage(uint8_t stage) {
  std::vector<uint8_t> payload(17, 0x00);
  payload[0] = stage;
  return emitNotification(SERVICE, PAIRING_CHARACTERISTIC, payload, true);
}

bool NikonRemoteVirtualCamera::emitRemoteIndication(const std::vector<uint8_t> &payload) {
  if (payload.size() != 6) {
    return false;
  }
  return emitNotification(SERVICE, REMOTE_INDICATION_CHARACTERISTIC, payload, true);
}

bool NikonRemoteVirtualCamera::emitCaptureStatus(const std::vector<uint8_t> &payload) {
  if (!m_Config.full_profile || !m_Config.allow_capture_status_notification) {
    return false;
  }
  return emitNotification(SERVICE, CAPTURE_STATUS_CHARACTERISTIC, payload);
}

bool NikonRemoteVirtualCamera::emitControlPointRequest(uint16_t request) {
  return emitNotification(
      SERVICE, CONTROL_POINT_CHARACTERISTIC,
      {static_cast<uint8_t>(request & 0xff), static_cast<uint8_t>((request >> 8) & 0xff)});
}

bool NikonRemoteVirtualCamera::acceptConnection(NimBLEClient &client,
                                                const NimBLEAddress &address) {
  if (m_Connected || (address != m_Config.address)) {
    return false;
  }
  m_Client = &client;
  m_Connected = true;
  m_PairingStage = 0;
  m_Subscriptions.clear();
  return true;
}

void NikonRemoteVirtualCamera::disconnect(NimBLEClient &client, int reason) {
  (void)reason;
  if (m_Client == &client) {
    m_Client = nullptr;
    m_Connected = false;
    m_Subscriptions.clear();
  }
}

bool NikonRemoteVirtualCamera::hasRemoteSet() const {
  return m_Config.remote_role_supported;
}

bool NikonRemoteVirtualCamera::hasService(const NimBLEUUID &service) const {
  return matches(service, SERVICE) || matches(service, DEVICE_INFORMATION_SERVICE);
}

bool NikonRemoteVirtualCamera::hasCharacteristic(const NimBLEUUID &service,
                                                 const NimBLEUUID &characteristic) const {
  if (matches(service, SERVICE)) {
    // The common set 2000 to 200b plus Battery Level is added unconditionally
    // by the B600 builder, in both profiles.
    if (matches(characteristic, AUTH_CHARACTERISTIC)
        || matches(characteristic, POWER_CHARACTERISTIC)
        || matches(characteristic, CLIENT_NAME_CHARACTERISTIC)
        || matches(characteristic, SERVER_NAME_CHARACTERISTIC)
        || matches(characteristic, CONNECTION_CONFIG_CHARACTERISTIC)
        || matches(characteristic, CONNECTION_ESTABLISHMENT_CHARACTERISTIC)
        || matches(characteristic, TIME_CHARACTERISTIC)
        || matches(characteristic, LOCATION_CHARACTERISTIC)
        || matches(characteristic, CONTROL_POINT_CHARACTERISTIC)
        || matches(characteristic, FEATURE_CHARACTERISTIC)
        || matches(characteristic, CABLE_CHARACTERISTIC)
        || matches(characteristic, SERIAL_CHARACTERISTIC)
        || matches(characteristic, BATTERY_CHARACTERISTIC)) {
      return true;
    }
    if (!hasRemoteSet()) {
      return false;
    }
    if (matches(characteristic, REMOTE_INFO_CHARACTERISTIC)
        || matches(characteristic, KEY_MAP_CHARACTERISTIC)
        || matches(characteristic, SHUTTER_CHARACTERISTIC)
        || matches(characteristic, REMOTE_INDICATION_CHARACTERISTIC)
        || matches(characteristic, REMOTE_INFO3_CHARACTERISTIC)
        || matches(characteristic, PAIRING_CHARACTERISTIC)) {
      return true;
    }
    return m_Config.full_profile
           && (matches(characteristic, CAPTURE_STATUS_CHARACTERISTIC)
               || matches(characteristic, REMOTE_INFO2_CHARACTERISTIC));
  }
  if (matches(service, DEVICE_INFORMATION_SERVICE)) {
    // The B600 builder emits only 2a24, 2a26 and 2a28. SnapBridge binds nine,
    // but six of those are not in the camera image.
    return matches(characteristic, MODEL_CHARACTERISTIC)
           || matches(characteristic, FIRMWARE_CHARACTERISTIC)
           || matches(characteristic, SOFTWARE_CHARACTERISTIC);
  }
  return false;
}

bool NikonRemoteVirtualCamera::discoverCharacteristic(NimBLEClient &client,
                                                      const NimBLEUUID &service,
                                                      const NimBLEUUID &characteristic) {
  (void)client;
  return hasCharacteristic(service, characteristic);
}

bool NikonRemoteVirtualCamera::canWrite(const NimBLEUUID &service,
                                        const NimBLEUUID &characteristic) const {
  if (!matches(service, SERVICE)) {
    return false;
  }
  if (matches(characteristic, AUTH_CHARACTERISTIC) || matches(characteristic, POWER_CHARACTERISTIC)
      || matches(characteristic, CLIENT_NAME_CHARACTERISTIC)
      || matches(characteristic, SERVER_NAME_CHARACTERISTIC)
      || matches(characteristic, CONNECTION_CONFIG_CHARACTERISTIC)
      || matches(characteristic, CONNECTION_ESTABLISHMENT_CHARACTERISTIC)
      || matches(characteristic, TIME_CHARACTERISTIC)
      || matches(characteristic, LOCATION_CHARACTERISTIC)
      || matches(characteristic, CONTROL_POINT_CHARACTERISTIC)) {
    return true;
  }
  return hasRemoteSet()
         && (matches(characteristic, KEY_MAP_CHARACTERISTIC)
             || matches(characteristic, SHUTTER_CHARACTERISTIC)
             || matches(characteristic, PAIRING_CHARACTERISTIC));
}

bool NikonRemoteVirtualCamera::write(NimBLEClient &client,
                                     const NimBLEUUID &service,
                                     const NimBLEUUID &characteristic,
                                     const std::vector<uint8_t> &value,
                                     bool response) {
  const std::lock_guard<std::mutex> journal(Host::peerWriteJournalMutex());
  if (!m_Connected || (m_Client != &client) || !canWrite(service, characteristic)) {
    return false;
  }

  const Fault fault = consumeFault();
  switch (fault) {
    case Fault::None:
    case Fault::Timeout:
    case Fault::StaleBond:
      break;
    case Fault::Reject:
    case Fault::Busy:
    case Fault::WrongMode:
    case Fault::Malformed:
      m_RejectedWrites++;
      return false;
    case Fault::Disconnect:
      m_RejectedWrites++;
      client.mockDropLink(0x08, /*fire_callback=*/true);
      return false;
  }
  const bool suppress_response = (fault == Fault::Timeout);

  // Frame gates from the B600 receive callback, section 3.2.
  bool accepted = true;
  if (matches(characteristic, PAIRING_CHARACTERISTIC)) {
    accepted = (value.size() == 17);
  } else if (matches(characteristic, SHUTTER_CHARACTERISTIC)) {
    accepted = (value.size() == 2) && ((value[0] == SHUTTER_KEY) || m_Config.allow_unproven_keys);
  } else if (matches(characteristic, KEY_MAP_CHARACTERISTIC)) {
    accepted = (value.size() == 32) && m_Config.accept_key_map_write;
  } else if (matches(characteristic, AUTH_CHARACTERISTIC)) {
    // The Smart LSS exchange is modelled by NikonSmartVirtualCamera. Refusing
    // it here keeps a Remote peer from being mistaken for a Smart one.
    if (value.size() == 17) {
      m_SmartAuthWriteRejected = true;
    }
    accepted = false;
  } else if (matches(characteristic, POWER_CHARACTERISTIC)) {
    accepted = (value.size() == 1) && m_Config.allow_unproven_power_values;
  } else if (matches(characteristic, CLIENT_NAME_CHARACTERISTIC)
             || matches(characteristic, SERVER_NAME_CHARACTERISTIC)) {
    accepted = !value.empty() && (value.size() <= 0x7f);
  } else if (matches(characteristic, CONNECTION_CONFIG_CHARACTERISTIC)) {
    accepted = (value.size() == 102) && m_Config.accept_connection_configuration;
  } else if (matches(characteristic, CONNECTION_ESTABLISHMENT_CHARACTERISTIC)) {
    accepted = (value.size() == 1)
               && ((value[0] & static_cast<uint8_t>(~CONNECTION_KNOWN_BITS)) == 0
                   || m_Config.allow_unproven_connection_bits);
  } else if (matches(characteristic, TIME_CHARACTERISTIC)) {
    accepted = (value.size() == 10);
  } else if (matches(characteristic, LOCATION_CHARACTERISTIC)) {
    accepted = (value.size() == 41) && (value[24] != 0x00);
  } else if (matches(characteristic, CONTROL_POINT_CHARACTERISTIC)) {
    accepted = (value.size() == 2);
  }
  if (!accepted) {
    m_RejectedWrites++;
    return false;
  }

  Write event;
  event.service = service.toString();
  event.characteristic = characteristic.toString();
  event.payload = value;
  event.response = response;
  m_Writes.push_back(event);

  if (matches(characteristic, PAIRING_CHARACTERISTIC)) {
    return handlePairing(value, suppress_response);
  }
  if (matches(characteristic, SHUTTER_CHARACTERISTIC)) {
    m_LastKeyId = value[0];
    m_LastKeyOperation = value[1];
    if (suppress_response) {
      return true;
    }
    if (value[0] != SHUTTER_KEY) {
      // A firmware case exists for other key IDs, but no physical meaning is
      // proven, so the write is accepted and recorded with no action.
      return true;
    }
    switch (value[1]) {
      case SHORT_PRESS:
        m_ShortPresses++;
        return true;
      case LONG_PRESS:
        m_LongPresses++;
        return true;
      case NO_OP:
        // The handler returns without action. This is not a release.
        m_NoOps++;
        return true;
      default:
        return true;
    }
  }
  if (matches(characteristic, KEY_MAP_CHARACTERISTIC)) {
    m_LastKeyMap = value;
    return true;
  }
  return handleCommonWrite(characteristic, value);
}

bool NikonRemoteVirtualCamera::handlePairing(const std::vector<uint8_t> &value,
                                             bool suppress_response) {
  // m_Paired and m_Config.client_id feed advertisement(); the radio task may be
  // reading them while this runs on the camera task.
  const std::lock_guard<std::recursive_mutex> lock(m_AdvertisementMutex);
  m_PairingStage = value[0];
  switch (value[0]) {
    case 0x01:
      std::copy(value.begin() + 1, value.begin() + 9, m_PairingTimestamp.begin());
      std::copy(value.begin() + 9, value.begin() + 17, m_PairingDevice.begin());
      // The reconnect advertisement carries the device half of the ID the
      // central sent here, not the fixture value it was constructed with.
      std::copy(m_PairingDevice.begin(), m_PairingDevice.begin() + 4, m_Config.client_id.begin());
      if (!suppress_response) {
        emitPairingStage(0x02);
      }
      return true;
    case 0x03:
      // The handler sets the pairing state and sends 04 with sixteen zero
      // bytes. The serial copy targets the Smart auth buffer, not this
      // response.
      m_Paired = true;
      m_Config.paired = true;
      if (!suppress_response) {
        emitPairingStage(0x04);
      }
      return true;
    default:
      // Any other stage is logged and dropped by the handler. The GATT write
      // itself succeeds, so the client sees no stage response and its wait
      // expires.
      return true;
  }
}

bool NikonRemoteVirtualCamera::handleCommonWrite(const NimBLEUUID &characteristic,
                                                 const std::vector<uint8_t> &value) {
  if (matches(characteristic, POWER_CHARACTERISTIC)) {
    // One byte is forwarded to the power handler. The values are unknown, so
    // this only runs behind Config::allow_unproven_power_values.
    return true;
  }
  if (matches(characteristic, CLIENT_NAME_CHARACTERISTIC)) {
    m_ClientName.assign(value.begin(), value.end());
    return true;
  }
  if (matches(characteristic, SERVER_NAME_CHARACTERISTIC)) {
    return true;
  }
  if (matches(characteristic, CONNECTION_CONFIG_CHARACTERISTIC)) {
    return true;
  }
  if (matches(characteristic, CONNECTION_ESTABLISHMENT_CHARACTERISTIC)) {
    return true;
  }
  if (matches(characteristic, TIME_CHARACTERISTIC)) {
    m_LastTimePacket = value;
    return true;
  }
  if (matches(characteristic, LOCATION_CHARACTERISTIC)) {
    m_LastLocationPacket = value;
    return true;
  }
  if (matches(characteristic, CONTROL_POINT_CHARACTERISTIC)) {
    // The firmware receive path accepts the two-byte write and the application
    // handler rejects it with "Don't allow app clear control point". No state
    // change and no response.
    m_ControlPointWriteSeen = true;
    return true;
  }
  return false;
}

NimBLEAttValue NikonRemoteVirtualCamera::read(NimBLEClient &client,
                                              const NimBLEUUID &service,
                                              const NimBLEUUID &characteristic) {
  if (!m_Connected || (m_Client != &client) || !hasCharacteristic(service, characteristic)) {
    return {};
  }
  if (matches(service, SERVICE)) {
    if (matches(characteristic, BATTERY_CHARACTERISTIC)) {
      return NimBLEAttValue({m_Config.battery});
    }
    if (matches(characteristic, CLIENT_NAME_CHARACTERISTIC)) {
      return NimBLEAttValue(m_ClientName);
    }
    if (matches(characteristic, SERVER_NAME_CHARACTERISTIC)) {
      return NimBLEAttValue(m_Config.name);
    }
    if (matches(characteristic, POWER_CHARACTERISTIC)) {
      return NimBLEAttValue(m_Config.power_control);
    }
    if (matches(characteristic, CONNECTION_CONFIG_CHARACTERISTIC)) {
      return NimBLEAttValue(m_Config.connection_configuration);
    }
    if (matches(characteristic, CONNECTION_ESTABLISHMENT_CHARACTERISTIC)) {
      return NimBLEAttValue(m_Config.connection_establishment);
    }
    if (matches(characteristic, TIME_CHARACTERISTIC)) {
      return NimBLEAttValue(m_LastTimePacket);
    }
    if (matches(characteristic, LOCATION_CHARACTERISTIC)) {
      return NimBLEAttValue(m_LastLocationPacket);
    }
    if (matches(characteristic, CONTROL_POINT_CHARACTERISTIC)) {
      return NimBLEAttValue({0x00, 0x00});
    }
    if (matches(characteristic, FEATURE_CHARACTERISTIC)) {
      if (!m_Config.feature_bitmap_known) {
        return {};
      }
      return NimBLEAttValue(m_Config.feature_bitmap.data(), m_Config.feature_bitmap.size());
    }
    if (matches(characteristic, CABLE_CHARACTERISTIC)) {
      return NimBLEAttValue(m_Config.cable_attachment);
    }
    if (matches(characteristic, SERIAL_CHARACTERISTIC)) {
      return NimBLEAttValue(m_Config.serial.data(), m_Config.serial.size());
    }
    if (matches(characteristic, REMOTE_INFO_CHARACTERISTIC)) {
      return NimBLEAttValue(m_Config.remote_info);
    }
    if (matches(characteristic, CAPTURE_STATUS_CHARACTERISTIC)) {
      return NimBLEAttValue(m_Config.capture_status);
    }
    if (matches(characteristic, KEY_MAP_CHARACTERISTIC)) {
      return NimBLEAttValue(m_Config.key_map);
    }
    if (matches(characteristic, REMOTE_INDICATION_CHARACTERISTIC)) {
      return NimBLEAttValue(m_Config.key_state);
    }
    if (matches(characteristic, REMOTE_INFO2_CHARACTERISTIC)) {
      return NimBLEAttValue(m_Config.remote_info2);
    }
    if (matches(characteristic, REMOTE_INFO3_CHARACTERISTIC)) {
      return NimBLEAttValue(m_Config.remote_info3);
    }
    if (matches(characteristic, PAIRING_CHARACTERISTIC)) {
      return NimBLEAttValue(m_Config.pair_info);
    }
    // 2000 auth and 2009 feature are handled above or empty on purpose.
    return {};
  }
  if (matches(service, DEVICE_INFORMATION_SERVICE)) {
    if (matches(characteristic, MODEL_CHARACTERISTIC)) {
      return NimBLEAttValue(m_Config.model);
    }
    if (matches(characteristic, FIRMWARE_CHARACTERISTIC)) {
      return NimBLEAttValue(m_Config.firmware);
    }
    if (matches(characteristic, SOFTWARE_CHARACTERISTIC)) {
      return NimBLEAttValue(m_Config.software);
    }
  }
  return {};
}

bool NikonRemoteVirtualCamera::subscribe(NimBLEClient &client,
                                         const NimBLEUUID &service,
                                         const NimBLEUUID &characteristic,
                                         bool notification,
                                         NimBLERemoteCharacteristic *remote,
                                         const NimBLENotifyCallback &callback,
                                         bool response) {
  (void)response;
  if (!m_Connected || (m_Client != &client) || !hasCharacteristic(service, characteristic)
      || (callback == nullptr)) {
    return false;
  }

  Subscription subscription;
  subscription.client = &client;
  subscription.remote = remote;
  subscription.callback = callback;
  subscription.notification = notification;
  m_Subscriptions[key(service, characteristic)] = subscription;
  return true;
}

bool NikonRemoteVirtualCamera::secureConnection(NimBLEClient &client) {
  if (!m_Connected || (m_Client != &client)) {
    return false;
  }
  if (m_Fault == Fault::StaleBond) {
    client.mockMarkLinkDeadEventPending(520);
    return false;
  }
  if (!m_Config.accept_numeric_comparison) {
    return false;
  }
  if (!m_Config.require_bond) {
    // SMP parameters and whether an LE bond is required are unknown (U-36).
    // SnapBridge never requests one on the LSS path, so the default proceeds
    // without a bond.
    return true;
  }
  const bool local_bonded = NimBLEDevice::isBonded(m_Config.address);
  if (local_bonded != m_Config.camera_bonded) {
    return false;
  }
  if (!local_bonded) {
    NimBLEDevice::setBondedAddress(m_Config.address);
    m_Config.camera_bonded = true;
  }
  return true;
}

bool NikonRemoteVirtualCamera::updateConnectionParams(NimBLEClient &client,
                                                      uint16_t min_interval,
                                                      uint16_t max_interval,
                                                      uint16_t latency,
                                                      uint16_t timeout) {
  (void)min_interval;
  (void)max_interval;
  (void)latency;
  (void)timeout;
  return m_Connected && (m_Client == &client);
}

int NikonRemoteVirtualCamera::getRssi() const {
  return -50;
}

const NimBLEUUID &NikonRemoteVirtualCamera::serviceUUID() {
  return SERVICE;
}

const NimBLEUUID &NikonRemoteVirtualCamera::pairingCharacteristicUUID() {
  return PAIRING_CHARACTERISTIC;
}

const NimBLEUUID &NikonRemoteVirtualCamera::shutterCharacteristicUUID() {
  return SHUTTER_CHARACTERISTIC;
}

const NimBLEUUID &NikonRemoteVirtualCamera::remoteIndicationCharacteristicUUID() {
  return REMOTE_INDICATION_CHARACTERISTIC;
}

const NimBLEUUID &NikonRemoteVirtualCamera::keyMapCharacteristicUUID() {
  return KEY_MAP_CHARACTERISTIC;
}

const NimBLEUUID &NikonRemoteVirtualCamera::remoteInfoCharacteristicUUID() {
  return REMOTE_INFO_CHARACTERISTIC;
}

const NimBLEUUID &NikonRemoteVirtualCamera::remoteInfo2CharacteristicUUID() {
  return REMOTE_INFO2_CHARACTERISTIC;
}

const NimBLEUUID &NikonRemoteVirtualCamera::captureStatusCharacteristicUUID() {
  return CAPTURE_STATUS_CHARACTERISTIC;
}

const NimBLEUUID &NikonRemoteVirtualCamera::batteryCharacteristicUUID() {
  return BATTERY_CHARACTERISTIC;
}

const NimBLEUUID &NikonRemoteVirtualCamera::deviceInformationServiceUUID() {
  return DEVICE_INFORMATION_SERVICE;
}

}  // namespace Host
}  // namespace Furble
