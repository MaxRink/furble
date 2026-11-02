#include <algorithm>
#include <cstring>

#include "Blowfish.h"
#include "NikonSmartVirtualCamera.h"

namespace Furble {
namespace Host {
namespace {

// Nikon LSS service and characteristics, spec/nikon.md sections 3.1 and 4.2.
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
const NimBLEUUID CATEGORY_INFO_CHARACTERISTIC {0x00002080, 0x3dd4, 0x4255, 0x8d626dc7b9bd5561};
const NimBLEUUID CAPTURE_STATUS_CHARACTERISTIC {0x00002081, 0x3dd4, 0x4255, 0x8d626dc7b9bd5561};
const NimBLEUUID CONTROL_STATUS_CHARACTERISTIC {0x00002020, 0x3dd4, 0x4255, 0x8d626dc7b9bd5561};
const NimBLEUUID CONTROL_CHARACTERISTIC {0x00002021, 0x3dd4, 0x4255, 0x8d626dc7b9bd5561};
const NimBLEUUID BATTERY_CHARACTERISTIC {0x00002a19, 0x3dd4, 0x4255, 0x8d626dc7b9bd5561};

const NimBLEUUID DEVICE_INFORMATION_SERVICE {0x0000180a, 0x0000, 0x1000, 0x800000805f9b34fb};
const NimBLEUUID MODEL_CHARACTERISTIC {0x00002a24, 0x0000, 0x1000, 0x800000805f9b34fb};
const NimBLEUUID FIRMWARE_CHARACTERISTIC {0x00002a26, 0x0000, 0x1000, 0x800000805f9b34fb};
const NimBLEUUID SOFTWARE_CHARACTERISTIC {0x00002a28, 0x0000, 0x1000, 0x800000805f9b34fb};

constexpr uint8_t CONNECTION_KNOWN_BITS = 0x83;

// The eight Nikon salts, spec/nikon.md section 7.9. They are byte-identical in
// libLsSec-jni.so and in the B600 image.
constexpr std::array<std::array<uint8_t, 8>, 8> SALTS = {
    {
     {0x70, 0x40, 0x66, 0xe4, 0x04, 0x33, 0xd5, 0x52},
     {0xed, 0x4b, 0x8f, 0xac, 0x15, 0xf7, 0xe4, 0x7b},
     {0x24, 0x47, 0x1f, 0x11, 0x8b, 0x5e, 0xa1, 0xfc},
     {0x05, 0x96, 0x0c, 0x31, 0x2b, 0x8c, 0x7f, 0x41},
     {0xfd, 0xa5, 0x88, 0xc1, 0xeb, 0xa8, 0xb1, 0xf3},
     {0x99, 0x16, 0x60, 0x56, 0x1b, 0xd3, 0xd5, 0x50},
     {0xcd, 0x32, 0x68, 0x7f, 0xa9, 0xe2, 0x8a, 0x30},
     {0x2a, 0x8f, 0xe8, 0x34, 0xde, 0xc7, 0xeb, 0xf4},
     }
};

const std::vector<uint8_t> LSS_KEY = {0xff, 0xff, 0xaa, 0x55, 0x11, 0x22, 0x33, 0x00};

uint32_t readBe(const uint8_t *data) {
  return (static_cast<uint32_t>(data[0]) << 24) | (static_cast<uint32_t>(data[1]) << 16)
         | (static_cast<uint32_t>(data[2]) << 8) | static_cast<uint32_t>(data[3]);
}

void writeBe(uint8_t *out, uint32_t word) {
  out[0] = static_cast<uint8_t>((word >> 24) & 0xff);
  out[1] = static_cast<uint8_t>((word >> 16) & 0xff);
  out[2] = static_cast<uint8_t>((word >> 8) & 0xff);
  out[3] = static_cast<uint8_t>(word & 0xff);
}

/**
 * The LsSec Blowfish CBC-MAC, firmware-static at 0x8079e028.
 *
 * Each 8-byte block is read as two big-endian 32-bit words, XORed with the
 * chaining values 0x01020304 and 0x05060708 and run through the keyed Blowfish
 * context. The final pair is written big-endian. A partial block is padded with
 * 0xff; every input here is a whole 24-byte buffer.
 *
 * The scramble body is the same construction NikonSmart::SmartPairing uses, and
 * it reproduces the corpus test vectors:
 *   salt0 + N1 11..88 + N2 99..00 -> 68 15 6d f2 a0 4e 25 b1
 *   salt0 + N2 99..00 + N1 11..88 -> d4 3d 8a 99 a1 7f 92 67
 */
class LssMac final: private Blowfish {
 public:
  LssMac() : Blowfish(LSS_KEY) {}

  std::array<uint8_t, 8> compute(const std::array<uint32_t, 6> &words) const {
    uint32_t left = 0x01020304;
    uint32_t right = 0x05060708;
    uint32_t in_left = 0;
    uint32_t in_right = 0;
    for (size_t i = 0; i < words.size(); i += 2) {
      in_left = words[i] ^ left;
      in_right = words[i + 1] ^ right;
      scramble(&in_left, &in_right);
      left = in_left;
      right = in_right;
    }
    std::array<uint8_t, 8> result {};
    writeBe(result.data(), in_left);
    writeBe(result.data() + 4, in_right);
    return result;
  }

 private:
  void scramble(uint32_t *p_left, uint32_t *p_right) const {
    uint32_t left = *p_left;
    uint32_t right = *p_right;
    uint32_t tmp = 0;
    for (size_t i = 0; i < N; i++) {
      tmp = left ^ m_P[i];
      left = f(tmp) ^ right;
      right = tmp;
    }
    *p_left = tmp ^ m_P[N + 1];
    *p_right = left ^ m_P[N];
  }
};

const LssMac &lssMac() {
  static const LssMac instance;
  return instance;
}

std::array<uint8_t, 8> computeHash(const std::array<uint8_t, 8> &salt,
                                   const std::array<uint8_t, 8> &first,
                                   const std::array<uint8_t, 8> &second) {
  const std::array<uint32_t, 6> words = {readBe(salt.data()),   readBe(salt.data() + 4),
                                         readBe(first.data()),  readBe(first.data() + 4),
                                         readBe(second.data()), readBe(second.data() + 4)};
  return lssMac().compute(words);
}

}  // namespace

NikonSmartVirtualCamera::NikonSmartVirtualCamera() : NikonSmartVirtualCamera(Config {}) {}

NikonSmartVirtualCamera::NikonSmartVirtualCamera(const Config &config) : m_Config(config) {
  m_Paired = m_Config.paired;
}

NikonSmartVirtualCamera::~NikonSmartVirtualCamera() = default;

bool NikonSmartVirtualCamera::matches(const NimBLEUUID &left, const NimBLEUUID &right) {
  return left == right;
}

std::string NikonSmartVirtualCamera::key(const NimBLEUUID &service,
                                         const NimBLEUUID &characteristic) {
  return service.toString() + "/" + characteristic.toString();
}

NimBLEAdvertisedDevice NikonSmartVirtualCamera::advertisement() const {
  const std::lock_guard<std::recursive_mutex> lock(m_AdvertisementMutex);
  NimBLEAdvertisedDevice device;
  device.setAddress(m_Config.address);
  device.setName(m_Config.name);
  device.setRSSI(-46);
  device.addServiceUUID(SERVICE);
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

const NikonSmartVirtualCamera::Config &NikonSmartVirtualCamera::config() const {
  return m_Config;
}

const std::vector<NikonSmartVirtualCamera::Write> &NikonSmartVirtualCamera::writes() const {
  return m_Writes;
}

const std::vector<NikonSmartVirtualCamera::Notification> &NikonSmartVirtualCamera::notifications()
    const {
  return m_Notifications;
}

void NikonSmartVirtualCamera::clearEvents() {
  m_Writes.clear();
  m_Notifications.clear();
}

bool NikonSmartVirtualCamera::connected() const {
  return m_Connected;
}

bool NikonSmartVirtualCamera::paired() const {
  return m_Paired;
}

bool NikonSmartVirtualCamera::authenticated() const {
  return m_Authenticated;
}

bool NikonSmartVirtualCamera::cameraBonded() const {
  return m_Config.camera_bonded;
}

void NikonSmartVirtualCamera::setCameraBonded(bool bonded) {
  m_Config.camera_bonded = bonded;
}

void NikonSmartVirtualCamera::setPaired(bool paired) {
  m_Paired = paired;
  m_Config.paired = paired;
}

void NikonSmartVirtualCamera::removeCameraBond() {
  m_Config.camera_bonded = false;
  m_Config.paired = false;
  m_Paired = false;
  m_Authenticated = false;
  m_AuthStage = 0;
  m_ClientNonce = {};
  m_DeviceId = {};
  m_HashRejected = false;
}

uint8_t NikonSmartVirtualCamera::authStage() const {
  return m_AuthStage;
}

const std::array<uint8_t, 8> &NikonSmartVirtualCamera::clientNonce() const {
  return m_ClientNonce;
}

const std::array<uint8_t, 8> &NikonSmartVirtualCamera::deviceId() const {
  return m_DeviceId;
}

const std::array<uint8_t, 8> &NikonSmartVirtualCamera::cameraNonce() const {
  return m_Config.camera_nonce;
}

uint8_t NikonSmartVirtualCamera::saltIndex() const {
  return m_SaltIndex;
}

bool NikonSmartVirtualCamera::hashRejected() const {
  return m_HashRejected;
}

bool NikonSmartVirtualCamera::timeSynced() const {
  return m_TimeSynced;
}

const std::vector<uint8_t> &NikonSmartVirtualCamera::lastTimePacket() const {
  return m_LastTimePacket;
}

const std::vector<uint8_t> &NikonSmartVirtualCamera::lastLocationPacket() const {
  return m_LastLocationPacket;
}

bool NikonSmartVirtualCamera::controlPointWriteSeen() const {
  return m_ControlPointWriteSeen;
}

uint16_t NikonSmartVirtualCamera::controlRequest() const {
  return m_ControlRequest;
}

bool NikonSmartVirtualCamera::remoteControlMode() const {
  return m_RemoteControlMode;
}

const std::vector<std::array<uint8_t, 2>> &NikonSmartVirtualCamera::keyEvents() const {
  return m_KeyEvents;
}

size_t NikonSmartVirtualCamera::captureGatedRejections() const {
  return m_CaptureGatedRejections;
}

bool NikonSmartVirtualCamera::classicRequested() const {
  return m_ClassicRequested;
}

size_t NikonSmartVirtualCamera::rejectedWrites() const {
  return m_RejectedWrites;
}

void NikonSmartVirtualCamera::setFault(Fault fault) {
  m_Fault = fault;
}

NikonSmartVirtualCamera::Fault NikonSmartVirtualCamera::fault() const {
  return m_Fault;
}

void NikonSmartVirtualCamera::clearFault() {
  m_Fault = Fault::None;
}

void NikonSmartVirtualCamera::setScripted(bool scripted) {
  m_Scripted = scripted;
}

bool NikonSmartVirtualCamera::scripted() const {
  return m_Scripted;
}

void NikonSmartVirtualCamera::scriptNextCommand(Fault fault) {
  m_Script.push_back(fault);
}

size_t NikonSmartVirtualCamera::scriptedRemaining() const {
  return m_Script.size();
}

void NikonSmartVirtualCamera::clearScript() {
  m_Script.clear();
}

NikonSmartVirtualCamera::Fault NikonSmartVirtualCamera::consumeFault() {
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

std::array<uint8_t, 8> NikonSmartVirtualCamera::expectedStage2Hash() const {
  // Camera stage 2 hashes salt + N2 + N1.
  return computeHash(SALTS[m_SaltIndex], m_Config.camera_nonce, m_ClientNonce);
}

std::array<uint8_t, 8> NikonSmartVirtualCamera::expectedStage3Hash(
    const std::array<uint8_t, 8> &nonce) const {
  // Client stage 3 hashes salt + N1 + N2.
  return computeHash(SALTS[m_SaltIndex], nonce, m_Config.camera_nonce);
}

bool NikonSmartVirtualCamera::emitNotification(const NimBLEUUID &service,
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

bool NikonSmartVirtualCamera::emitAuthStage(uint8_t stage) {
  if (m_AuthStage == 0) {
    return false;
  }
  std::vector<uint8_t> payload(17, 0x00);
  payload[0] = stage;
  if (stage == 0x02) {
    std::copy(m_Config.camera_nonce.begin(), m_Config.camera_nonce.end(), payload.begin() + 1);
    const std::array<uint8_t, 8> hash = expectedStage2Hash();
    std::copy(hash.begin(), hash.end(), payload.begin() + 9);
  } else if (stage == 0x04) {
    std::copy(m_Config.camera_nonce.begin(), m_Config.camera_nonce.end(), payload.begin() + 1);
    std::copy(m_Config.serial.begin(), m_Config.serial.end(), payload.begin() + 9);
  } else {
    return false;
  }
  return emitNotification(SERVICE, AUTH_CHARACTERISTIC, payload, true);
}

bool NikonSmartVirtualCamera::emitControlPointRequest(uint16_t request) {
  m_ControlRequest = request;
  return emitNotification(
      SERVICE, CONTROL_POINT_CHARACTERISTIC,
      {static_cast<uint8_t>(request & 0xff), static_cast<uint8_t>((request >> 8) & 0xff)});
}

bool NikonSmartVirtualCamera::emitTimeRequest() {
  // Bits 0 to 3, time request on.
  return emitControlPointRequest(0x0001);
}

bool NikonSmartVirtualCamera::emitLocationRequest(bool with_gps) {
  // Bits 4 to 7, 1 on or 2 on with GPS.
  return emitControlPointRequest(with_gps ? 0x0020 : 0x0010);
}

bool NikonSmartVirtualCamera::emitConnectionRequest() {
  // Bits 8 to 11, connection request on.
  return emitControlPointRequest(0x0100);
}

bool NikonSmartVirtualCamera::emitControlStatus(const std::vector<uint8_t> &payload) {
  if (!m_Config.has_control_service) {
    return false;
  }
  return emitNotification(SERVICE, CONTROL_STATUS_CHARACTERISTIC, payload, true);
}

bool NikonSmartVirtualCamera::emitControlResponse(uint8_t opcode, uint8_t code) {
  if (!m_Config.has_control_service) {
    return false;
  }
  return emitNotification(SERVICE, CONTROL_CHARACTERISTIC, {0x04, 0x00, opcode, code}, true);
}

bool NikonSmartVirtualCamera::acceptConnection(NimBLEClient &client, const NimBLEAddress &address) {
  if (m_Connected || (address != m_Config.address)) {
    return false;
  }
  m_Client = &client;
  m_Connected = true;
  m_AuthStage = 0;
  // Authentication belongs to one link. A reconnect must authenticate again
  // rather than inherit the previous session's success.
  m_Authenticated = false;
  m_HashRejected = false;
  m_Subscriptions.clear();
  return true;
}

void NikonSmartVirtualCamera::disconnect(NimBLEClient &client, int reason) {
  (void)reason;
  if (m_Client == &client) {
    m_Client = nullptr;
    m_Connected = false;
    m_Subscriptions.clear();
  }
}

bool NikonSmartVirtualCamera::hasService(const NimBLEUUID &service) const {
  return matches(service, SERVICE) || matches(service, DEVICE_INFORMATION_SERVICE);
}

bool NikonSmartVirtualCamera::hasCharacteristic(const NimBLEUUID &service,
                                                const NimBLEUUID &characteristic) const {
  if (matches(service, SERVICE)) {
    const bool common = matches(characteristic, POWER_CHARACTERISTIC)
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
                        || matches(characteristic, BATTERY_CHARACTERISTIC);
    if (common) {
      return true;
    }
    if (!m_Config.smart_role_supported) {
      return false;
    }
    if (matches(characteristic, AUTH_CHARACTERISTIC)
        || matches(characteristic, CATEGORY_INFO_CHARACTERISTIC)
        || matches(characteristic, CAPTURE_STATUS_CHARACTERISTIC)) {
      return true;
    }
    return m_Config.has_control_service
           && (matches(characteristic, CONTROL_STATUS_CHARACTERISTIC)
               || matches(characteristic, CONTROL_CHARACTERISTIC));
  }
  if (matches(service, DEVICE_INFORMATION_SERVICE)) {
    return matches(characteristic, MODEL_CHARACTERISTIC)
           || matches(characteristic, FIRMWARE_CHARACTERISTIC)
           || matches(characteristic, SOFTWARE_CHARACTERISTIC);
  }
  return false;
}

bool NikonSmartVirtualCamera::discoverCharacteristic(NimBLEClient &client,
                                                     const NimBLEUUID &service,
                                                     const NimBLEUUID &characteristic) {
  (void)client;
  return hasCharacteristic(service, characteristic);
}

bool NikonSmartVirtualCamera::canWrite(const NimBLEUUID &service,
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
  return m_Config.has_control_service && matches(characteristic, CONTROL_CHARACTERISTIC);
}

bool NikonSmartVirtualCamera::write(NimBLEClient &client,
                                    const NimBLEUUID &service,
                                    const NimBLEUUID &characteristic,
                                    const std::vector<uint8_t> &value,
                                    bool response) {
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
  if (matches(characteristic, AUTH_CHARACTERISTIC)) {
    accepted = m_Config.smart_role_supported && (value.size() == 17);
  } else if (matches(characteristic, POWER_CHARACTERISTIC)) {
    accepted = (value.size() == 1) && m_Config.allow_unproven_power_values;
  } else if (matches(characteristic, CLIENT_NAME_CHARACTERISTIC)
             || matches(characteristic, SERVER_NAME_CHARACTERISTIC)) {
    accepted = !value.empty() && (value.size() <= 0x7f);
  } else if (matches(characteristic, CONNECTION_CONFIG_CHARACTERISTIC)) {
    accepted = (value.size() == 102) && m_Config.accept_connection_configuration;
  } else if (matches(characteristic, CONNECTION_ESTABLISHMENT_CHARACTERISTIC)) {
    const bool bits_known = (value.size() == 1)
                            && (((value[0] & static_cast<uint8_t>(~CONNECTION_KNOWN_BITS)) == 0)
                                || m_Config.allow_unproven_connection_bits);
    // Bit 1 asks for the Bluetooth Classic continuation the BLE half cannot
    // drive. Whether key events work without it is unknown (U-37), so the
    // write is refused unless the harness opts in.
    const bool classic_allowed =
        (value.size() == 1) && (((value[0] & 0x02) == 0) || m_Config.allow_classic_continuation);
    accepted = bits_known && classic_allowed;
  } else if (matches(characteristic, TIME_CHARACTERISTIC)) {
    accepted = (value.size() == 10);
  } else if (matches(characteristic, LOCATION_CHARACTERISTIC)) {
    accepted = (value.size() == 41) && (value[24] != 0x00);
  } else if (matches(characteristic, CONTROL_POINT_CHARACTERISTIC)) {
    accepted = (value.size() == 2);
  } else if (matches(characteristic, CONTROL_CHARACTERISTIC)) {
    accepted = true;
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

  if (matches(characteristic, AUTH_CHARACTERISTIC)) {
    return handleAuth(value, suppress_response);
  }
  if (matches(characteristic, CONTROL_POINT_CHARACTERISTIC)) {
    // The GATT layer accepts the two-byte write and the application handler
    // rejects it with "Don't allow app clear control point". No state change
    // and no response, which is why Furble's final 01 00 wait never completes.
    m_ControlPointWriteSeen = true;
    return true;
  }
  if (matches(characteristic, CONTROL_CHARACTERISTIC)) {
    if (suppress_response) {
      return true;
    }
    return handleControl(value);
  }
  if (matches(characteristic, POWER_CHARACTERISTIC)) {
    m_Config.power_control = value;
    return true;
  }
  if (matches(characteristic, CLIENT_NAME_CHARACTERISTIC)) {
    if (m_AuthStage == 0x01) {
      // The firmware blocks the device-name update while pairing is on.
      return true;
    }
    m_ClientName.assign(value.begin(), value.end());
    return true;
  }
  if (matches(characteristic, SERVER_NAME_CHARACTERISTIC)) {
    m_Config.camera_name.assign(value.begin(), value.end());
    return true;
  }
  if (matches(characteristic, CONNECTION_CONFIG_CHARACTERISTIC)) {
    m_Config.connection_config = value;
    return true;
  }
  if (matches(characteristic, CONNECTION_ESTABLISHMENT_CHARACTERISTIC)) {
    m_Config.connection_state = value;
    if ((value[0] & 0x02) != 0) {
      m_ClassicRequested = true;
    }
    return true;
  }
  if (matches(characteristic, TIME_CHARACTERISTIC)) {
    if (m_AuthStage == 0x01) {
      // "Pairing is on, don't sync time" (section 5).
      return true;
    }
    m_LastTimePacket = value;
    m_TimeSynced = true;
    return true;
  }
  if (matches(characteristic, LOCATION_CHARACTERISTIC)) {
    m_LastLocationPacket = value;
    return true;
  }
  return false;
}

bool NikonSmartVirtualCamera::handleAuth(const std::vector<uint8_t> &value,
                                         bool suppress_response) {
  // m_Paired and the stored device/client IDs feed advertisement(); the radio
  // task may be reading them while this runs on the camera task.
  const std::lock_guard<std::recursive_mutex> lock(m_AdvertisementMutex);
  const uint8_t previous = m_AuthStage;
  m_AuthStage = value[0];
  switch (value[0]) {
    case 0x01:
    {
      std::copy(value.begin() + 1, value.begin() + 9, m_ClientNonce.begin());
      std::copy(value.begin() + 9, value.begin() + 17, m_DeviceId.begin());

      // The firmware compares the incoming device ID with the stored one when
      // a pairing already exists and aborts with "Unknown device ID".
      //
      // Field offsets differ between the two corpus sources: spec/nikon.md
      // section 5.2 puts N1 in bytes 1 to 8 and the client device ID D in
      // bytes 9 to 16, while analysis/nikon-b600-handlers.md section 5.1
      // describes the comparison against bytes 1 to 8. Furble sends a fresh
      // random nonce in bytes 1 to 8 on every connect, so only the spec field
      // map can support a reconnect gate. The peer follows the spec and treats
      // bytes 9 to 16 as D.
      if (m_Paired
          && (std::memcmp(m_DeviceId.data(), m_Config.device_id.data(), m_DeviceId.size()) != 0)) {
        m_AuthStage = 0;
        return false;
      }
      // A fresh pairing stores the client device ID for later reconnects.
      m_Config.device_id = m_DeviceId;
      // The reconnect advertisement carries the device half of the ID, which
      // is the first four bytes of the eight-byte value sent in stage 1.
      std::copy(m_DeviceId.begin(), m_DeviceId.begin() + 4, m_Config.client_id.begin());
      m_SaltIndex = static_cast<uint8_t>(m_Config.salt_index & 0x07);
      m_HashRejected = false;
      if (!suppress_response) {
        emitAuthStage(0x02);
      }
      return true;
    }
    case 0x03:
    {
      if (previous != 0x01) {
        // A stage 3 without a prior stage 1 is an LsSec invalid state. The
        // camera handler returns an error and no indication.
        m_AuthStage = 0;
        return false;
      }
      std::array<uint8_t, 8> nonce {};
      std::copy(value.begin() + 1, value.begin() + 9, nonce.begin());
      const std::array<uint8_t, 8> expected = expectedStage3Hash(nonce);
      if (std::memcmp(expected.data(), value.data() + 9, expected.size()) != 0) {
        // Hash mismatch: 0xffffff9a internally, no indication.
        m_HashRejected = true;
        m_AuthStage = 0;
        return false;
      }
      m_Authenticated = true;
      m_Paired = true;
      m_Config.paired = true;
      if (!suppress_response) {
        emitAuthStage(0x04);
      }
      return true;
    }
    default:
      // Unknown stage: the handler logs and returns without an indication.
      m_AuthStage = 0;
      return true;
  }
}

bool NikonSmartVirtualCamera::handleControl(const std::vector<uint8_t> &value) {
  if (!m_Config.has_control_service) {
    return false;
  }
  if (m_Config.capture_gated_control) {
    // The 2021 request opcodes are app-static and the response data is largely
    // unknown (U-19 to U-23). Refuse the whole path until a test opts in.
    m_CaptureGatedRejections++;
    return false;
  }
  if (value.size() < 4) {
    return false;
  }
  const uint16_t size =
      static_cast<uint16_t>(value[0]) | static_cast<uint16_t>(static_cast<uint16_t>(value[1]) << 8);
  if ((size != value.size()) || (value[3] != 0x00)) {
    return false;
  }
  switch (value[2]) {
    case 0x01:
      // Feature request, 04 00 01 00.
      if (size != 4) {
        return false;
      }
      emitControlResponse(0x01, 0x01);
      return true;
    case 0x10:
      // Control configuration, 04 00 10 00.
      if (size != 4) {
        return false;
      }
      emitControlResponse(0x10, 0x01);
      return true;
    case 0x11:
    {
      // Remote control mode, 05 00 11 00 <mode>, mode 1 enter, 0 leave.
      if ((size != 5) || (value[4] > 1)) {
        return false;
      }
      m_RemoteControlMode = (value[4] == 0x01);
      emitControlResponse(0x11, 0x01);
      return true;
    }
    case 0x12:
    {
      // Key event, 06 00 12 00 <key> <operation>. Key codes 1 to 15, operation
      // 1 press and 0 release. Key events are only legal once remote control
      // mode is entered; the error code for an out-of-mode request is unknown,
      // so no response is invented.
      if (size != 6) {
        return false;
      }
      const uint8_t key = value[4];
      const uint8_t operation = value[5];
      if ((key < 0x01) || (key > 0x0f) || (operation > 0x01)) {
        return false;
      }
      if (!m_RemoteControlMode) {
        return true;
      }
      m_KeyEvents.push_back({key, operation});
      emitControlResponse(0x12, 0x01);
      return true;
    }
    case 0x13:
      // Shooting info, 04 00 13 00. The response data is unknown, only the
      // four-byte success frame is documented.
      if (size != 4) {
        return false;
      }
      emitControlResponse(0x13, 0x01);
      return true;
    case 0x14:
    {
      // Shooting event, function 1 interval, 2 time lapse, 3 focus shift. The
      // start, pause and stop control values are not in the corpus.
      if ((size != 6) || (value[4] < 0x01) || (value[4] > 0x03)) {
        return false;
      }
      if (!m_Config.allow_unproven_control_values) {
        return true;
      }
      emitControlResponse(0x14, 0x01);
      return true;
    }
    case 0x15:
      // Playback event, function and control values unknown.
      if (size != 6) {
        return false;
      }
      if (!m_Config.allow_unproven_control_values) {
        return true;
      }
      emitControlResponse(0x15, 0x01);
      return true;
    default:
      return false;
  }
}

NimBLEAttValue NikonSmartVirtualCamera::read(NimBLEClient &client,
                                             const NimBLEUUID &service,
                                             const NimBLEUUID &characteristic) {
  if (!m_Connected || (m_Client != &client) || !hasCharacteristic(service, characteristic)) {
    return {};
  }
  if (matches(service, SERVICE)) {
    if (matches(characteristic, BATTERY_CHARACTERISTIC)) {
      return NimBLEAttValue({m_Config.battery});
    }
    if (matches(characteristic, SERVER_NAME_CHARACTERISTIC)) {
      return NimBLEAttValue(m_Config.camera_name);
    }
    if (matches(characteristic, CLIENT_NAME_CHARACTERISTIC)) {
      return NimBLEAttValue(m_ClientName);
    }
    if (matches(characteristic, POWER_CHARACTERISTIC)) {
      return NimBLEAttValue(m_Config.power_control);
    }
    if (matches(characteristic, CONNECTION_CONFIG_CHARACTERISTIC)) {
      return NimBLEAttValue(m_Config.connection_config);
    }
    if (matches(characteristic, CONNECTION_ESTABLISHMENT_CHARACTERISTIC)) {
      return NimBLEAttValue(m_Config.connection_state);
    }
    if (matches(characteristic, TIME_CHARACTERISTIC)) {
      return NimBLEAttValue(m_LastTimePacket);
    }
    if (matches(characteristic, LOCATION_CHARACTERISTIC)) {
      return NimBLEAttValue(m_LastLocationPacket);
    }
    if (matches(characteristic, CONTROL_POINT_CHARACTERISTIC)) {
      return NimBLEAttValue({static_cast<uint8_t>(m_ControlRequest & 0xff),
                             static_cast<uint8_t>((m_ControlRequest >> 8) & 0xff)});
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
    if (matches(characteristic, CATEGORY_INFO_CHARACTERISTIC)) {
      return NimBLEAttValue(m_Config.category_info);
    }
    if (matches(characteristic, CAPTURE_STATUS_CHARACTERISTIC)) {
      return NimBLEAttValue(m_Config.capture_status);
    }
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

bool NikonSmartVirtualCamera::subscribe(NimBLEClient &client,
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

bool NikonSmartVirtualCamera::secureConnection(NimBLEClient &client) {
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
    // SnapBridge never creates an LE bond on the LSS path and the camera's SMP
    // parameters are unknown (U-36), so the default proceeds without one.
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

bool NikonSmartVirtualCamera::updateConnectionParams(NimBLEClient &client,
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

int NikonSmartVirtualCamera::getRssi() const {
  return -46;
}

const NimBLEUUID &NikonSmartVirtualCamera::serviceUUID() {
  return SERVICE;
}

const NimBLEUUID &NikonSmartVirtualCamera::authCharacteristicUUID() {
  return AUTH_CHARACTERISTIC;
}

const NimBLEUUID &NikonSmartVirtualCamera::controlPointCharacteristicUUID() {
  return CONTROL_POINT_CHARACTERISTIC;
}

const NimBLEUUID &NikonSmartVirtualCamera::timeCharacteristicUUID() {
  return TIME_CHARACTERISTIC;
}

const NimBLEUUID &NikonSmartVirtualCamera::locationCharacteristicUUID() {
  return LOCATION_CHARACTERISTIC;
}

const NimBLEUUID &NikonSmartVirtualCamera::featureCharacteristicUUID() {
  return FEATURE_CHARACTERISTIC;
}

const NimBLEUUID &NikonSmartVirtualCamera::categoryInfoCharacteristicUUID() {
  return CATEGORY_INFO_CHARACTERISTIC;
}

const NimBLEUUID &NikonSmartVirtualCamera::captureStatusCharacteristicUUID() {
  return CAPTURE_STATUS_CHARACTERISTIC;
}

const NimBLEUUID &NikonSmartVirtualCamera::controlStatusCharacteristicUUID() {
  return CONTROL_STATUS_CHARACTERISTIC;
}

const NimBLEUUID &NikonSmartVirtualCamera::controlCharacteristicUUID() {
  return CONTROL_CHARACTERISTIC;
}

const NimBLEUUID &NikonSmartVirtualCamera::batteryCharacteristicUUID() {
  return BATTERY_CHARACTERISTIC;
}

const NimBLEUUID &NikonSmartVirtualCamera::deviceInformationServiceUUID() {
  return DEVICE_INFORMATION_SERVICE;
}

}  // namespace Host
}  // namespace Furble
