#include <algorithm>
#include <cstring>

#include "CanonSmartVirtualCamera.h"
#include "WriteJournal.h"

namespace Furble {
namespace Host {
namespace {

// Canon vendor UUIDs, all on the d8492fffa821 base (spec/canon.md section 3).
const NimBLEUUID PRIMARY_SERVICE {0x00010000, 0x0000, 0x1000, 0x0000d8492fffa821};
const NimBLEUUID POWER_CHARACTERISTIC {0x00010005, 0x0000, 0x1000, 0x0000d8492fffa821};
const NimBLEUUID PAIRING_CHARACTERISTIC {0x00010006, 0x0000, 0x1000, 0x0000d8492fffa821};
const NimBLEUUID IDENTITY_CHARACTERISTIC {0x0001000a, 0x0000, 0x1000, 0x0000d8492fffa821};
const NimBLEUUID CAPABILITY_CHARACTERISTIC {0x0001000b, 0x0000, 0x1000, 0x0000d8492fffa821};
const NimBLEUUID WIFI_INDICATION_CHARACTERISTIC {0x0001000c, 0x0000, 0x1000, 0x0000d8492fffa821};
const NimBLEUUID WIFI_SERVICE {0x00020000, 0x0000, 0x1000, 0x0000d8492fffa821};
const NimBLEUUID HANDOVER_STATE_CHARACTERISTIC {0x00020001, 0x0000, 0x1000, 0x0000d8492fffa821};
const NimBLEUUID HANDOVER_CONTROL_CHARACTERISTIC {0x00020002, 0x0000, 0x1000, 0x0000d8492fffa821};
const NimBLEUUID AP_CONFIGURATION_CHARACTERISTIC {0x00020003, 0x0000, 0x1000, 0x0000d8492fffa821};
const NimBLEUUID HANDOVER_RESERVED4_CHARACTERISTIC {0x00020004, 0x0000, 0x1000, 0x0000d8492fffa821};
const NimBLEUUID HANDOVER_RESERVED5_CHARACTERISTIC {0x00020005, 0x0000, 0x1000, 0x0000d8492fffa821};
const NimBLEUUID HANDOVER_RESERVED6_CHARACTERISTIC {0x00020006, 0x0000, 0x1000, 0x0000d8492fffa821};
const NimBLEUUID MODE_SERVICE {0x00030000, 0x0000, 0x1000, 0x0000d8492fffa821};
const NimBLEUUID FEATURE_STATUS_CHARACTERISTIC {0x00030001, 0x0000, 0x1000, 0x0000d8492fffa821};
const NimBLEUUID ERROR_CHARACTERISTIC {0x00030002, 0x0000, 0x1000, 0x0000d8492fffa821};
const NimBLEUUID MODE_CHARACTERISTIC {0x00030010, 0x0000, 0x1000, 0x0000d8492fffa821};
const NimBLEUUID MODE_STATE_CHARACTERISTIC {0x00030011, 0x0000, 0x1000, 0x0000d8492fffa821};
const NimBLEUUID BUTTON_CHARACTERISTIC {0x00030020, 0x0000, 0x1000, 0x0000d8492fffa821};
const NimBLEUUID BUTTON_STATE_CHARACTERISTIC {0x00030021, 0x0000, 0x1000, 0x0000d8492fffa821};
const NimBLEUUID SHUTTER_CHARACTERISTIC {0x00030030, 0x0000, 0x1000, 0x0000d8492fffa821};
const NimBLEUUID SHOOT_STATE_CHARACTERISTIC {0x00030031, 0x0000, 0x1000, 0x0000d8492fffa821};
const NimBLEUUID GPS_SERVICE {0x00040000, 0x0000, 0x1000, 0x0000d8492fffa821};
const NimBLEUUID GPS_REQUEST_CHARACTERISTIC {0x00040001, 0x0000, 0x1000, 0x0000d8492fffa821};
const NimBLEUUID GPS_CONTROL_CHARACTERISTIC {0x00040002, 0x0000, 0x1000, 0x0000d8492fffa821};
const NimBLEUUID GPS_STATE_CHARACTERISTIC {0x00040003, 0x0000, 0x1000, 0x0000d8492fffa821};
const NimBLEUUID LIVESTREAM_SERVICE {0x00080000, 0x0000, 0x1000, 0x0000d8492fffa821};
const NimBLEUUID LIVESTREAM_FEATURE_CHARACTERISTIC {0x00080001, 0x0000, 0x1000, 0x0000d8492fffa821};
const NimBLEUUID LIVESTREAM_REQUEST_CHARACTERISTIC {0x00080002, 0x0000, 0x1000, 0x0000d8492fffa821};
const NimBLEUUID LIVESTREAM_STATUS_CHARACTERISTIC {0x00080003, 0x0000, 0x1000, 0x0000d8492fffa821};
const NimBLEUUID DEVICE_INFORMATION_SERVICE {0x0000180a, 0x0000, 0x1000, 0x800000805f9b34fb};
const NimBLEUUID MANUFACTURER_CHARACTERISTIC {0x00002a29, 0x0000, 0x1000, 0x800000805f9b34fb};
const NimBLEUUID MODEL_CHARACTERISTIC {0x00002a24, 0x0000, 0x1000, 0x800000805f9b34fb};
const NimBLEUUID FIRMWARE_CHARACTERISTIC {0x00002a26, 0x0000, 0x1000, 0x800000805f9b34fb};
const NimBLEUUID SOFTWARE_CHARACTERISTIC {0x00002a28, 0x0000, 0x1000, 0x800000805f9b34fb};
const NimBLEUUID SERIAL_CHARACTERISTIC {0x00002a25, 0x0000, 0x1000, 0x800000805f9b34fb};

// A registration the camera files when it accepts a pair request and the
// harness did not supply one. This is a harness value, not a captured Canon
// value; the corpus does not record a camera-assigned client UUID.
constexpr std::array<uint8_t, 16> DEFAULT_CLIENT_UUID = {
    0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20};

constexpr uint16_t BUTTON_NAV_MASK = 0x013f;
constexpr uint16_t BUTTON_MASK_MAX = 0x01ff;

bool isPrintableAscii(const std::vector<uint8_t> &value, size_t offset) {
  if (value.size() <= offset) {
    return false;
  }
  for (size_t i = offset; i < value.size(); i++) {
    if ((value[i] < 0x20) || (value[i] > 0x7e)) {
      return false;
    }
  }
  return true;
}

}  // namespace

CanonSmartVirtualCamera::CanonSmartVirtualCamera() : CanonSmartVirtualCamera(Config {}) {}

CanonSmartVirtualCamera::CanonSmartVirtualCamera(const Config &config) : m_Config(config) {
  const std::lock_guard<std::recursive_mutex> lock(m_AdvertisementMutex);
  m_Power = m_Config.power_state;
  m_Registered = false;
  for (const uint8_t byte : m_Config.client_uuid) {
    m_Registered = m_Registered || (byte != 0x00);
  }
  m_ClientUuid = m_Config.client_uuid;
}

CanonSmartVirtualCamera::~CanonSmartVirtualCamera() = default;

bool CanonSmartVirtualCamera::matches(const NimBLEUUID &left, const NimBLEUUID &right) {
  return left == right;
}

std::string CanonSmartVirtualCamera::key(const NimBLEUUID &service,
                                         const NimBLEUUID &characteristic) {
  return service.toString() + "/" + characteristic.toString();
}

NimBLEAdvertisedDevice CanonSmartVirtualCamera::advertisement() const {
  const std::lock_guard<std::recursive_mutex> lock(m_AdvertisementMutex);
  NimBLEAdvertisedDevice device;
  device.setAddress(m_Config.address);
  device.setName(m_Config.name);
  device.setRSSI(-52);

  // The Smart matcher is the advertised 00010000 service (section 2.6). A body
  // with no Smart role does not advertise it, so the model gate can fail before
  // any connection is attempted.
  if (m_Config.smart_supported) {
    device.addServiceUUID(PRIMARY_SERVICE);
  }

  // Manufacturer data, section 2.1. Company ID 0x01A9 is Canon Inc. and is
  // A9 01 on air. A non-zero registered client selects the 6-byte ADV-02 form,
  // otherwise the 19-byte ADV-01 form carries the all-zero client UUID.
  std::vector<uint8_t> manufacturer = {0xa9, 0x01, 0x01,
                                       static_cast<uint8_t>(m_Config.product_id & 0xff),
                                       static_cast<uint8_t>((m_Config.product_id >> 8) & 0xff)};
  if (m_Registered) {
    manufacturer.push_back(m_ClientUuid[0]);
    manufacturer.push_back(m_ClientUuid[1]);
    // The suffix bytes are the first two of the stored client UUID. How the
    // app fills the UUID high bytes from a 2-byte suffix is unknown (U-10), so
    // the peer only reproduces the observed 6-byte form.
    // Status byte bits 2:1 carry power. Bit 0 is unnamed and stays zero.
    manufacturer.push_back(static_cast<uint8_t>((m_Power & 0x03) << 1));
  } else {
    manufacturer.insert(manufacturer.end(), 16, 0x00);
  }
  device.setManufacturerData(manufacturer.data(), manufacturer.size());
  return device;
}

const CanonSmartVirtualCamera::Config &CanonSmartVirtualCamera::config() const {
  return m_Config;
}

const std::vector<CanonSmartVirtualCamera::Write> &CanonSmartVirtualCamera::writes() const {
  return m_Writes;
}

const std::vector<CanonSmartVirtualCamera::Notification> &CanonSmartVirtualCamera::notifications()
    const {
  return m_Notifications;
}

void CanonSmartVirtualCamera::clearEvents() {
  m_Writes.clear();
  m_Notifications.clear();
}

bool CanonSmartVirtualCamera::connected() const {
  return m_Connected;
}

bool CanonSmartVirtualCamera::cameraBonded() const {
  return m_Config.camera_bonded;
}

bool CanonSmartVirtualCamera::registered() const {
  return m_Registered;
}

bool CanonSmartVirtualCamera::committed() const {
  return m_Committed;
}

bool CanonSmartVirtualCamera::secured() const {
  return m_Secured;
}

const std::array<uint8_t, 16> &CanonSmartVirtualCamera::registeredClient() const {
  return m_ClientUuid;
}

const std::string &CanonSmartVirtualCamera::clientName() const {
  return m_ClientName;
}

void CanonSmartVirtualCamera::setCameraBonded(bool bonded) {
  m_Config.camera_bonded = bonded;
}

void CanonSmartVirtualCamera::setRegistered(const std::array<uint8_t, 16> &client) {
  const std::lock_guard<std::recursive_mutex> lock(m_AdvertisementMutex);
  m_ClientUuid = client;
  m_Config.client_uuid = client;
  m_Registered = true;
  for (const uint8_t byte : client) {
    if (byte != 0x00) {
      return;
    }
  }
  m_Registered = false;
}

void CanonSmartVirtualCamera::removeCameraBond() {
  const std::lock_guard<std::recursive_mutex> lock(m_AdvertisementMutex);
  m_Config.camera_bonded = false;
  m_Config.client_uuid = {};
  m_ClientUuid = {};
  m_ClientName.clear();
  m_Registered = false;
  m_Committed = false;
}

uint8_t CanonSmartVirtualCamera::powerState() const {
  return m_Power;
}

void CanonSmartVirtualCamera::setPowerState(uint8_t power) {
  const std::lock_guard<std::recursive_mutex> lock(m_AdvertisementMutex);
  m_Power = static_cast<uint8_t>(power & 0x03);
}

uint8_t CanonSmartVirtualCamera::modeState() const {
  return m_ModeState;
}

bool CanonSmartVirtualCamera::afHeld() const {
  return m_AfHeld;
}

bool CanonSmartVirtualCamera::shutterHeld() const {
  return m_ShutterHeld;
}

bool CanonSmartVirtualCamera::movieRecording() const {
  return m_MovieRecording;
}

int CanonSmartVirtualCamera::zoomDirection() const {
  return m_ZoomDirection;
}

bool CanonSmartVirtualCamera::gpsEnabled() const {
  return m_GpsState == 0x02;
}

uint8_t CanonSmartVirtualCamera::gpsState() const {
  return m_GpsState;
}

uint8_t CanonSmartVirtualCamera::gpsSource() const {
  return m_GpsSource;
}

bool CanonSmartVirtualCamera::gpsRequestPending() const {
  return m_GpsRequestPending;
}

const std::vector<uint8_t> &CanonSmartVirtualCamera::lastGpsPacket() const {
  return m_LastGpsPacket;
}

uint16_t CanonSmartVirtualCamera::lastButtonMask() const {
  return m_ButtonMask;
}

uint8_t CanonSmartVirtualCamera::lastPressType() const {
  return m_PressType;
}

uint8_t CanonSmartVirtualCamera::lastModeCommand() const {
  return m_LastModeCommand;
}

uint8_t CanonSmartVirtualCamera::lastHandoverCommand() const {
  return m_LastHandoverCommand;
}

bool CanonSmartVirtualCamera::livestreamRequested() const {
  return m_LivestreamRequested;
}

bool CanonSmartVirtualCamera::autoPowerOffInhibited() const {
  return m_AutoPowerOffInhibited;
}

size_t CanonSmartVirtualCamera::rejectedWrites() const {
  return m_RejectedWrites;
}

size_t CanonSmartVirtualCamera::withheldResponses() const {
  return m_WithheldResponses;
}

void CanonSmartVirtualCamera::setFault(Fault fault) {
  m_Fault = fault;
}

CanonSmartVirtualCamera::Fault CanonSmartVirtualCamera::fault() const {
  return m_Fault;
}

void CanonSmartVirtualCamera::clearFault() {
  m_Fault = Fault::None;
}

void CanonSmartVirtualCamera::setScripted(bool scripted) {
  m_Scripted = scripted;
}

bool CanonSmartVirtualCamera::scripted() const {
  return m_Scripted;
}

void CanonSmartVirtualCamera::scriptNextCommand(Fault fault) {
  m_Script.push_back(fault);
}

size_t CanonSmartVirtualCamera::scriptedRemaining() const {
  return m_Script.size();
}

void CanonSmartVirtualCamera::clearScript() {
  m_Script.clear();
}

CanonSmartVirtualCamera::Fault CanonSmartVirtualCamera::consumeFault() {
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

bool CanonSmartVirtualCamera::shootingMode() const {
  return (m_Power == 0x01) && ((m_ModeState == 0x04) || (m_ModeState == 0x05));
}

bool CanonSmartVirtualCamera::emitNotification(const NimBLEUUID &service,
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

bool CanonSmartVirtualCamera::emitPairResult(uint8_t result) {
  return emitNotification(PRIMARY_SERVICE, PAIRING_CHARACTERISTIC, {result}, true);
}

bool CanonSmartVirtualCamera::emitPowerState(uint8_t power) {
  return emitNotification(PRIMARY_SERVICE, POWER_CHARACTERISTIC, {power});
}

bool CanonSmartVirtualCamera::emitModeState(uint8_t state) {
  return emitNotification(MODE_SERVICE, MODE_STATE_CHARACTERISTIC, {state});
}

bool CanonSmartVirtualCamera::emitGpsState(uint8_t state, uint8_t source) {
  return emitNotification(GPS_SERVICE, GPS_STATE_CHARACTERISTIC, {state, source}, true);
}

bool CanonSmartVirtualCamera::emitGpsRequest(uint8_t flags, uint8_t capability) {
  return emitNotification(GPS_SERVICE, GPS_REQUEST_CHARACTERISTIC, {flags, capability});
}

bool CanonSmartVirtualCamera::emitError(uint8_t category, uint8_t code) {
  return emitNotification(MODE_SERVICE, ERROR_CHARACTERISTIC, {category, code});
}

bool CanonSmartVirtualCamera::emitButtonState(const std::vector<uint8_t> &payload) {
  return emitNotification(MODE_SERVICE, BUTTON_STATE_CHARACTERISTIC, payload);
}

bool CanonSmartVirtualCamera::emitShootState(const std::vector<uint8_t> &payload) {
  return emitNotification(MODE_SERVICE, SHOOT_STATE_CHARACTERISTIC, payload);
}

bool CanonSmartVirtualCamera::emitWifiIndication(const std::vector<uint8_t> &payload) {
  return emitNotification(PRIMARY_SERVICE, WIFI_INDICATION_CHARACTERISTIC, payload, true);
}

bool CanonSmartVirtualCamera::emitApConfiguration(const std::vector<uint8_t> &payload) {
  return emitNotification(WIFI_SERVICE, AP_CONFIGURATION_CHARACTERISTIC, payload, true);
}

bool CanonSmartVirtualCamera::emitLivestreamStatus(const std::vector<uint8_t> &payload) {
  return emitNotification(LIVESTREAM_SERVICE, LIVESTREAM_STATUS_CHARACTERISTIC, payload, true);
}

bool CanonSmartVirtualCamera::acceptConnection(NimBLEClient &client, const NimBLEAddress &address) {
  if (m_Connected || (address != m_Config.address)) {
    return false;
  }
  m_Client = &client;
  m_Connected = true;
  m_Secured = false;
  m_Subscriptions.clear();
  return true;
}

void CanonSmartVirtualCamera::disconnect(NimBLEClient &client, int reason) {
  (void)reason;
  if (m_Client == &client) {
    m_Client = nullptr;
    m_Connected = false;
    m_Secured = false;
    m_Subscriptions.clear();
  }
}

bool CanonSmartVirtualCamera::hasService(const NimBLEUUID &service) const {
  if (!m_Config.smart_supported) {
    return false;
  }
  return matches(service, PRIMARY_SERVICE) || (m_Config.has_wifi && matches(service, WIFI_SERVICE))
         || matches(service, MODE_SERVICE) || (m_Config.has_gps && matches(service, GPS_SERVICE))
         || (m_Config.has_livestream && matches(service, LIVESTREAM_SERVICE))
         || matches(service, DEVICE_INFORMATION_SERVICE);
}

bool CanonSmartVirtualCamera::hasCharacteristic(const NimBLEUUID &service,
                                                const NimBLEUUID &characteristic) const {
  if (!m_Config.smart_supported) {
    return false;
  }
  if (matches(service, PRIMARY_SERVICE)) {
    return matches(characteristic, POWER_CHARACTERISTIC)
           || matches(characteristic, PAIRING_CHARACTERISTIC)
           || matches(characteristic, IDENTITY_CHARACTERISTIC)
           || matches(characteristic, CAPABILITY_CHARACTERISTIC)
           || matches(characteristic, WIFI_INDICATION_CHARACTERISTIC);
  }
  if (matches(service, WIFI_SERVICE)) {
    return matches(characteristic, HANDOVER_STATE_CHARACTERISTIC)
           || matches(characteristic, HANDOVER_CONTROL_CHARACTERISTIC)
           || matches(characteristic, AP_CONFIGURATION_CHARACTERISTIC)
           || matches(characteristic, HANDOVER_RESERVED4_CHARACTERISTIC)
           || matches(characteristic, HANDOVER_RESERVED5_CHARACTERISTIC)
           || matches(characteristic, HANDOVER_RESERVED6_CHARACTERISTIC);
  }
  if (matches(service, MODE_SERVICE)) {
    return matches(characteristic, FEATURE_STATUS_CHARACTERISTIC)
           || matches(characteristic, ERROR_CHARACTERISTIC)
           || matches(characteristic, MODE_CHARACTERISTIC)
           || matches(characteristic, MODE_STATE_CHARACTERISTIC)
           || matches(characteristic, BUTTON_CHARACTERISTIC)
           || matches(characteristic, BUTTON_STATE_CHARACTERISTIC)
           || matches(characteristic, SHUTTER_CHARACTERISTIC)
           || matches(characteristic, SHOOT_STATE_CHARACTERISTIC);
  }
  if (matches(service, GPS_SERVICE)) {
    return matches(characteristic, GPS_REQUEST_CHARACTERISTIC)
           || matches(characteristic, GPS_CONTROL_CHARACTERISTIC)
           || matches(characteristic, GPS_STATE_CHARACTERISTIC);
  }
  if (matches(service, LIVESTREAM_SERVICE)) {
    return matches(characteristic, LIVESTREAM_FEATURE_CHARACTERISTIC)
           || matches(characteristic, LIVESTREAM_REQUEST_CHARACTERISTIC)
           || matches(characteristic, LIVESTREAM_STATUS_CHARACTERISTIC);
  }
  if (matches(service, DEVICE_INFORMATION_SERVICE)) {
    return matches(characteristic, MANUFACTURER_CHARACTERISTIC)
           || matches(characteristic, MODEL_CHARACTERISTIC)
           || matches(characteristic, FIRMWARE_CHARACTERISTIC)
           || matches(characteristic, SOFTWARE_CHARACTERISTIC)
           || matches(characteristic, SERIAL_CHARACTERISTIC);
  }
  return false;
}

bool CanonSmartVirtualCamera::discoverCharacteristic(NimBLEClient &client,
                                                     const NimBLEUUID &service,
                                                     const NimBLEUUID &characteristic) {
  (void)client;
  return hasCharacteristic(service, characteristic);
}

bool CanonSmartVirtualCamera::canWrite(const NimBLEUUID &service,
                                       const NimBLEUUID &characteristic) const {
  if (!m_Config.smart_supported) {
    return false;
  }
  if (matches(service, PRIMARY_SERVICE)) {
    return matches(characteristic, PAIRING_CHARACTERISTIC)
           || matches(characteristic, IDENTITY_CHARACTERISTIC);
  }
  if (matches(service, MODE_SERVICE)) {
    return matches(characteristic, MODE_CHARACTERISTIC)
           || matches(characteristic, BUTTON_CHARACTERISTIC)
           || matches(characteristic, SHUTTER_CHARACTERISTIC);
  }
  if (matches(service, GPS_SERVICE)) {
    return matches(characteristic, GPS_CONTROL_CHARACTERISTIC);
  }
  if (matches(service, WIFI_SERVICE)) {
    return matches(characteristic, HANDOVER_CONTROL_CHARACTERISTIC);
  }
  return matches(service, LIVESTREAM_SERVICE)
         && matches(characteristic, LIVESTREAM_REQUEST_CHARACTERISTIC);
}

bool CanonSmartVirtualCamera::write(NimBLEClient &client,
                                    const NimBLEUUID &service,
                                    const NimBLEUUID &characteristic,
                                    const std::vector<uint8_t> &value,
                                    bool response) {
  const std::lock_guard<std::mutex> journal(Host::peerWriteJournalMutex());
  // Pairing and identity writes change the advertised identity; the radio
  // task may be building the advertisement at the same time.
  const std::lock_guard<std::recursive_mutex> lock(m_AdvertisementMutex);
  if (!m_Connected || (m_Client != &client) || !canWrite(service, characteristic)) {
    return false;
  }

  const Fault fault = consumeFault();
  const bool suppress_response = (fault == Fault::Timeout);
  if (suppress_response) {
    m_WithheldResponses++;
  }
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

  Write event;
  event.service = service.toString();
  event.characteristic = characteristic.toString();
  event.payload = value;
  event.response = response;
  m_Writes.push_back(event);

  return handleWrite(client, service, characteristic, value, suppress_response);
}

bool CanonSmartVirtualCamera::handleWrite(NimBLEClient &client,
                                          const NimBLEUUID &service,
                                          const NimBLEUUID &characteristic,
                                          const std::vector<uint8_t> &value,
                                          bool suppress_response) {
  (void)client;

  // CS-01, the 00010006 pairing name request. The documented payload is 01 plus
  // a 1 to 16 byte US-ASCII name, 17 bytes maximum. Only 02 accepts (PS-01).
  if (matches(service, PRIMARY_SERVICE) && matches(characteristic, PAIRING_CHARACTERISTIC)) {
    if ((value.size() < 2) || (value.size() > 17) || (value[0] != 0x01)
        || !isPrintableAscii(value, 1)) {
      return false;
    }

    // A second request from a registered client has no documented camera
    // response (section 7.5), so it is refused unless the harness opts in.
    if (m_Registered && !m_Config.accept_pair_while_registered) {
      return false;
    }

    m_ClientName.assign(value.begin() + 1, value.end());

    if (m_Config.pairing_accept) {
      if (!m_Registered) {
        std::array<uint8_t, 16> client = m_Config.client_uuid;
        bool assigned = false;
        for (const uint8_t byte : client) {
          assigned = assigned || (byte != 0x00);
        }
        m_ClientUuid = assigned ? client : DEFAULT_CLIENT_UUID;
        m_Config.client_uuid = m_ClientUuid;
        m_Registered = true;
      }
      if (!suppress_response) {
        emitPairResult(0x02);
      }
    } else if (!suppress_response) {
      emitPairResult(0x03);
    }
    return true;
  }

  // CS-02 to CS-11, the 0001000a identity commands.
  if (matches(service, PRIMARY_SERVICE) && matches(characteristic, IDENTITY_CHARACTERISTIC)) {
    if (value.empty()) {
      return false;
    }
    switch (value[0]) {
      case 0x01:
        // Commit or completion. The semantics are unknown (U-05) but every
        // implementation writes it, so it is accepted without inventing one.
        if (value.size() != 1) {
          return false;
        }
        m_Committed = true;
        return true;
      case 0x02:
        // Failure marker listed in the app table, never observed (U-06).
        return m_Config.accept_identity_failure_marker && (value.size() == 1);
      case 0x03:
        if (value.size() != 17) {
          return false;
        }
        std::copy(value.begin() + 1, value.end(), m_ClientUuid.begin());
        m_Config.client_uuid = m_ClientUuid;
        m_Registered = true;
        return true;
      case 0x04:
        if ((value.size() < 2) || (value.size() > 17) || !isPrintableAscii(value, 1)) {
          return false;
        }
        m_ClientName.assign(value.begin() + 1, value.end());
        m_Registered = true;
        return true;
      case 0x05:
        if (value.size() != 2) {
          return false;
        }
        // Only 05 02, Android, is documented. 01 and 03 are unknown (U-47).
        return (value[1] == 0x02) || m_Config.allow_unproven_client_types;
      case 0x06:
      case 0x07:
      case 0x08:
      case 0x0c:
        // Model name, bandwidth and AP bandwidth requests. The responses are
        // not documented (U-07), so the write is accepted and nothing is
        // invented.
        return value.size() == 1;
      case 0x0a:
        if ((value.size() != 2) || ((value[1] != 0x01) && (value[1] != 0x02))) {
          return false;
        }
        m_AutoPowerOffInhibited = (value[1] == 0x01);
        return true;
      default:
        return false;
    }
  }

  // CS-14, mode control. Values 01 to 06 are documented; the mode state values
  // the camera reports are 01 off, 03 play, 04 record and 05 record movie.
  if (matches(service, MODE_SERVICE) && matches(characteristic, MODE_CHARACTERISTIC)) {
    if (value.size() != 1) {
      return false;
    }
    const uint8_t before_power = m_Power;
    const uint8_t before_mode = m_ModeState;
    switch (value[0]) {
      case 0x01:
        m_ModeState = 0x03;
        break;
      case 0x02:
      case 0x03:
      case 0x06:
        // 02 record start, 03 any start (wake) and 06 record force start all
        // leave the camera in a shooting mode. The exact mode after a 03 wake
        // is not documented, so the peer reports the record state.
        m_ModeState = 0x04;
        break;
      case 0x04:
        m_ModeState = 0x01;
        m_MovieRecording = false;
        m_ZoomDirection = 0;
        break;
      case 0x05:
        m_ModeState = 0x01;
        m_Power = 0x02;
        m_MovieRecording = false;
        m_ZoomDirection = 0;
        break;
      default:
        return false;
    }
    m_LastModeCommand = value[0];
    if (!suppress_response) {
      if (m_Power != before_power) {
        emitPowerState(m_Power);
      }
      if (m_ModeState != before_mode) {
        emitModeState(m_ModeState);
      }
    }
    return true;
  }

  // CS-15, the 4-byte remote button frame. Byte 0 to 2 are a little-endian
  // mask, byte 3 is the press type. Mask bits above 0x100 are not demonstrated
  // (U-25) and are refused unless the harness opts in.
  if (matches(service, MODE_SERVICE) && matches(characteristic, BUTTON_CHARACTERISTIC)) {
    if (value.size() != 4) {
      return false;
    }
    if ((value[3] != 0x40) && (value[3] != 0x80) && (value[3] != 0xc0)) {
      return false;
    }
    const uint32_t mask = static_cast<uint32_t>(value[0]) | (static_cast<uint32_t>(value[1]) << 8)
                          | (static_cast<uint32_t>(value[2]) << 16);
    if ((mask & ~static_cast<uint32_t>(BUTTON_MASK_MAX)) != 0) {
      return m_Config.allow_unproven_button_bits;
    }
    if (m_Power != 0x01) {
      return false;
    }
    // The navigation bits and the slideshow bit are documented on the playback
    // view. Tele and wide are accepted in any powered mode: the corpus uses
    // them for playback zoom but does not prove a shooting-mode restriction.
    const uint16_t navigation = static_cast<uint16_t>(mask) & BUTTON_NAV_MASK;
    if ((navigation != 0) && (m_ModeState != 0x03)) {
      return false;
    }
    m_ButtonMask = static_cast<uint16_t>(mask);
    m_PressType = value[3];
    return true;
  }

  // CS-16 to CS-27, the shutter, focus, movie and zoom characteristic.
  if (matches(service, MODE_SERVICE) && matches(characteristic, SHUTTER_CHARACTERISTIC)) {
    if ((value.size() != 2) || (value[0] != 0x00)) {
      return false;
    }
    switch (value[1]) {
      case 0x01:
        // AF plus release on. This is the combined path Furble's shutter press
        // uses, not the split AF path.
        if (!shootingMode()) {
          return false;
        }
        m_AfHeld = true;
        m_ShutterHeld = true;
        return true;
      case 0x02:
        m_AfHeld = false;
        m_ShutterHeld = false;
        return true;
      case 0x03:
        if (!shootingMode()) {
          return false;
        }
        m_AfHeld = true;
        return true;
      case 0x04:
        m_AfHeld = false;
        return true;
      case 0x05:
        if (!shootingMode()) {
          return false;
        }
        m_ShutterHeld = true;
        return true;
      case 0x06:
        m_ShutterHeld = false;
        return true;
      case 0x10:
        if (!shootingMode()) {
          return false;
        }
        m_MovieRecording = true;
        m_ModeState = 0x05;
        if (!suppress_response) {
          emitModeState(m_ModeState);
        }
        return true;
      case 0x11:
        m_MovieRecording = false;
        if (m_ModeState == 0x05) {
          m_ModeState = 0x04;
          if (!suppress_response) {
            emitModeState(m_ModeState);
          }
        }
        return true;
      case 0x20:
        if (!shootingMode()) {
          return false;
        }
        m_ZoomDirection = 1;
        return true;
      case 0x21:
        if (m_ZoomDirection == 1) {
          m_ZoomDirection = 0;
        }
        return true;
      case 0x22:
        if (!shootingMode()) {
          return false;
        }
        m_ZoomDirection = -1;
        return true;
      case 0x23:
        if (m_ZoomDirection == -1) {
          m_ZoomDirection = 0;
        }
        return true;
      default:
        return false;
    }
  }

  // CS-29 to CS-35, the GPS control and location characteristic. The 8-byte
  // control commands are documented. The one-byte enable is not (U-18).
  if (matches(service, GPS_SERVICE) && matches(characteristic, GPS_CONTROL_CHARACTERISTIC)) {
    if (value.size() == 20) {
      // CS-34, the 20-byte location packet. Header 04, direction bytes, three
      // float32 values and a uint32 timestamp.
      if ((value[0] != 0x04) || ((value[1] != 'N') && (value[1] != 'S'))
          || ((value[6] != 'E') && (value[6] != 'W'))
          || ((value[11] != '+') && (value[11] != '-') && (value[11] != 0x00))) {
        return false;
      }
      // A packet is only consumed after the camera asked for GPS data, which
      // the corpus states as state WANTED or a pending request.
      if (m_GpsState != 0x02) {
        return false;
      }
      m_LastGpsPacket = value;
      return true;
    }
    if (value.size() == 8) {
      switch (value[0]) {
        case 0x01:
        case 0x02:
          m_GpsState = 0x02;
          break;
        case 0x03:
          m_GpsState = 0x01;
          break;
        case 0x05:
          // Acknowledgement of the camera's data request.
          m_GpsRequestPending = false;
          return true;
        case 0x06:
          if (value[1] > 0x04) {
            return false;
          }
          m_GpsSource = value[1];
          break;
        default:
          return false;
      }
      if (!suppress_response) {
        emitGpsState(m_GpsState, m_GpsSource);
      }
      return true;
    }
    if ((value.size() == 1) && (value[0] == 0x01)) {
      if (!m_Config.accept_one_byte_gps_enable) {
        return false;
      }
      m_GpsState = 0x02;
      if (!suppress_response) {
        emitGpsState(m_GpsState, m_GpsSource);
      }
      return true;
    }
    return false;
  }

  // CS-39 to CS-43, the Wi-Fi handover commands. Only the first opcode byte is
  // documented; the payload beyond it is unknown, so it is not parsed.
  if (matches(service, WIFI_SERVICE) && matches(characteristic, HANDOVER_CONTROL_CHARACTERISTIC)) {
    if (value.empty()) {
      return false;
    }
    if ((value[0] != 0x01) && (value[0] != 0x02) && (value[0] != 0x03) && (value[0] != 0x04)
        && (value[0] != 0x0a)) {
      return false;
    }
    m_LastHandoverCommand = value[0];
    return true;
  }

  // CS-44, request the livestream status. 01 is the only documented byte. The
  // status arrives as an indication whose layout is unknown, so nothing is
  // emitted here.
  if (matches(service, LIVESTREAM_SERVICE)
      && matches(characteristic, LIVESTREAM_REQUEST_CHARACTERISTIC)) {
    if ((value.size() != 1) || (value[0] != 0x01)) {
      return false;
    }
    m_LivestreamRequested = true;
    return true;
  }

  return false;
}

NimBLEAttValue CanonSmartVirtualCamera::read(NimBLEClient &client,
                                             const NimBLEUUID &service,
                                             const NimBLEUUID &characteristic) {
  if (!m_Connected || (m_Client != &client) || !hasCharacteristic(service, characteristic)) {
    return {};
  }
  if (matches(service, PRIMARY_SERVICE) && matches(characteristic, POWER_CHARACTERISTIC)) {
    // PS-02, the 1-byte power state.
    return NimBLEAttValue({m_Power});
  }
  if (matches(service, PRIMARY_SERVICE) && matches(characteristic, CAPABILITY_CHARACTERISTIC)) {
    // PS-03, the 4-byte little-endian capability mask.
    return NimBLEAttValue(m_Config.capability.data(), m_Config.capability.size());
  }
  if (matches(service, GPS_SERVICE) && matches(characteristic, GPS_REQUEST_CHARACTERISTIC)) {
    // PS-05. Byte 0 flags, bit 1 is the data request. Byte 1 is the source
    // capability mask.
    const uint8_t flags = m_GpsRequestPending ? 0x02 : 0x00;
    return NimBLEAttValue({flags, m_Config.gps_source_capability});
  }
  if (matches(service, GPS_SERVICE) && matches(characteristic, GPS_STATE_CHARACTERISTIC)) {
    // PS-06, state then source.
    return NimBLEAttValue({m_GpsState, m_GpsSource});
  }
  if (matches(service, DEVICE_INFORMATION_SERVICE)) {
    if (matches(characteristic, MANUFACTURER_CHARACTERISTIC)) {
      return NimBLEAttValue(m_Config.manufacturer);
    }
    if (matches(characteristic, MODEL_CHARACTERISTIC)) {
      return NimBLEAttValue(m_Config.name);
    }
    if (matches(characteristic, FIRMWARE_CHARACTERISTIC)) {
      return NimBLEAttValue(m_Config.firmware);
    }
    if (matches(characteristic, SOFTWARE_CHARACTERISTIC)) {
      return NimBLEAttValue(m_Config.software);
    }
    if (matches(characteristic, SERIAL_CHARACTERISTIC)) {
      return NimBLEAttValue(m_Config.serial);
    }
  }

  // Reads whose payload layout the corpus does not document. The peer returns
  // the configured bytes when a test has them, and an empty value otherwise,
  // rather than inventing a layout.
  if (matches(service, WIFI_SERVICE) && matches(characteristic, HANDOVER_STATE_CHARACTERISTIC)) {
    return NimBLEAttValue(m_Config.handover_state);
  }
  if (matches(service, MODE_SERVICE) && matches(characteristic, FEATURE_STATUS_CHARACTERISTIC)) {
    return NimBLEAttValue(m_Config.feature_status);
  }
  if (matches(service, LIVESTREAM_SERVICE)
      && matches(characteristic, LIVESTREAM_FEATURE_CHARACTERISTIC)) {
    return NimBLEAttValue(m_Config.livestream_feature);
  }
  return {};
}

bool CanonSmartVirtualCamera::subscribe(NimBLEClient &client,
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

bool CanonSmartVirtualCamera::secureConnection(NimBLEClient &client) {
  if (!m_Connected || (m_Client != &client)) {
    return false;
  }

  if (m_Fault == Fault::StaleBond) {
    // The camera deleted the pairing, so the encryption handshake dies with
    // the link and the disconnect event is still queued: the rc=520 class.
    client.mockMarkLinkDeadEventPending(520);
    return false;
  }

  if (!m_Config.accept_numeric_comparison) {
    return false;
  }

  const bool local_bonded = NimBLEDevice::isBonded(m_Config.address);
  if (local_bonded != m_Config.camera_bonded) {
    // One side kept the pairing and the other deleted it. The camera refuses
    // the dead keys; recovery needs the pairing deleted on the central too.
    return false;
  }
  if (!local_bonded) {
    NimBLEDevice::setBondedAddress(m_Config.address);
    m_Config.camera_bonded = true;
  }
  m_Secured = true;
  return true;
}

bool CanonSmartVirtualCamera::updateConnectionParams(NimBLEClient &client,
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

int CanonSmartVirtualCamera::getRssi() const {
  return -52;
}

const NimBLEUUID &CanonSmartVirtualCamera::primaryServiceUUID() {
  return PRIMARY_SERVICE;
}

const NimBLEUUID &CanonSmartVirtualCamera::powerCharacteristicUUID() {
  return POWER_CHARACTERISTIC;
}

const NimBLEUUID &CanonSmartVirtualCamera::pairingCharacteristicUUID() {
  return PAIRING_CHARACTERISTIC;
}

const NimBLEUUID &CanonSmartVirtualCamera::identityCharacteristicUUID() {
  return IDENTITY_CHARACTERISTIC;
}

const NimBLEUUID &CanonSmartVirtualCamera::capabilityCharacteristicUUID() {
  return CAPABILITY_CHARACTERISTIC;
}

const NimBLEUUID &CanonSmartVirtualCamera::wifiIndicationCharacteristicUUID() {
  return WIFI_INDICATION_CHARACTERISTIC;
}

const NimBLEUUID &CanonSmartVirtualCamera::wifiServiceUUID() {
  return WIFI_SERVICE;
}

const NimBLEUUID &CanonSmartVirtualCamera::modeServiceUUID() {
  return MODE_SERVICE;
}

const NimBLEUUID &CanonSmartVirtualCamera::modeCharacteristicUUID() {
  return MODE_CHARACTERISTIC;
}

const NimBLEUUID &CanonSmartVirtualCamera::modeStateCharacteristicUUID() {
  return MODE_STATE_CHARACTERISTIC;
}

const NimBLEUUID &CanonSmartVirtualCamera::buttonCharacteristicUUID() {
  return BUTTON_CHARACTERISTIC;
}

const NimBLEUUID &CanonSmartVirtualCamera::shutterCharacteristicUUID() {
  return SHUTTER_CHARACTERISTIC;
}

const NimBLEUUID &CanonSmartVirtualCamera::gpsServiceUUID() {
  return GPS_SERVICE;
}

const NimBLEUUID &CanonSmartVirtualCamera::gpsRequestCharacteristicUUID() {
  return GPS_REQUEST_CHARACTERISTIC;
}

const NimBLEUUID &CanonSmartVirtualCamera::gpsControlCharacteristicUUID() {
  return GPS_CONTROL_CHARACTERISTIC;
}

const NimBLEUUID &CanonSmartVirtualCamera::gpsStateCharacteristicUUID() {
  return GPS_STATE_CHARACTERISTIC;
}

const NimBLEUUID &CanonSmartVirtualCamera::livestreamServiceUUID() {
  return LIVESTREAM_SERVICE;
}

const NimBLEUUID &CanonSmartVirtualCamera::deviceInformationServiceUUID() {
  return DEVICE_INFORMATION_SERVICE;
}

}  // namespace Host
}  // namespace Furble
