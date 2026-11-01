#include <algorithm>
#include <cstdio>
#include <cstring>

#include "SonyVirtualCamera.h"

namespace Furble {
namespace Host {

namespace {

const NimBLEUUID CAMERA_SERVICE {0x8000cc00, 0xcc00, 0xffff, 0xffffffffffffffff};
const NimBLEUUID LOCATION_SERVICE {0x8000dd00, 0xdd00, 0xffff, 0xffffffffffffffff};
const NimBLEUUID PAIRING_SERVICE {0x8000ee00, 0xee00, 0xffff, 0xffffffffffffffff};
const NimBLEUUID CONTROL_SERVICE {0x8000ff00, 0xff00, 0xffff, 0xffffffffffffffff};
const NimBLEUUID DIRXTX_SERVICE {0x8000bb00, 0xbb00, 0xffff, 0xffffffffffffffff};

const NimBLEUUID CC02_CHARACTERISTIC {(uint16_t)0xcc02};
const NimBLEUUID CC03_CHARACTERISTIC {(uint16_t)0xcc03};
const NimBLEUUID CC05_CHARACTERISTIC {(uint16_t)0xcc05};
const NimBLEUUID CC06_CHARACTERISTIC {(uint16_t)0xcc06};
const NimBLEUUID CC07_CHARACTERISTIC {(uint16_t)0xcc07};
const NimBLEUUID CC08_CHARACTERISTIC {(uint16_t)0xcc08};
const NimBLEUUID CC09_CHARACTERISTIC {(uint16_t)0xcc09};
const NimBLEUUID CC0A_CHARACTERISTIC {(uint16_t)0xcc0a};
const NimBLEUUID CC0B_CHARACTERISTIC {(uint16_t)0xcc0b};
const NimBLEUUID CC0C_CHARACTERISTIC {(uint16_t)0xcc0c};
const NimBLEUUID CC0D_CHARACTERISTIC {(uint16_t)0xcc0d};
const NimBLEUUID CC0E_CHARACTERISTIC {(uint16_t)0xcc0e};
const NimBLEUUID CC0F_CHARACTERISTIC {(uint16_t)0xcc0f};
const NimBLEUUID CC10_CHARACTERISTIC {(uint16_t)0xcc10};
const NimBLEUUID CC13_CHARACTERISTIC {(uint16_t)0xcc13};
const NimBLEUUID DD01_CHARACTERISTIC {(uint16_t)0xdd01};
const NimBLEUUID DD11_CHARACTERISTIC {(uint16_t)0xdd11};
const NimBLEUUID DD21_CHARACTERISTIC {(uint16_t)0xdd21};
const NimBLEUUID DD30_CHARACTERISTIC {(uint16_t)0xdd30};
const NimBLEUUID DD31_CHARACTERISTIC {(uint16_t)0xdd31};
const NimBLEUUID DD32_CHARACTERISTIC {(uint16_t)0xdd32};
const NimBLEUUID DD33_CHARACTERISTIC {(uint16_t)0xdd33};
const NimBLEUUID EE01_CHARACTERISTIC {(uint16_t)0xee01};
const NimBLEUUID EE02_CHARACTERISTIC {(uint16_t)0xee02};
const NimBLEUUID EE03_CHARACTERISTIC {(uint16_t)0xee03};
const NimBLEUUID EE04_CHARACTERISTIC {(uint16_t)0xee04};
const NimBLEUUID FF01_CHARACTERISTIC {(uint16_t)0xff01};
const NimBLEUUID FF02_CHARACTERISTIC {(uint16_t)0xff02};

const NimBLEUUID BB01_CHARACTERISTIC {(uint16_t)0xbb01};
const NimBLEUUID BB02_CHARACTERISTIC {(uint16_t)0xbb02};
const NimBLEUUID BB03_CHARACTERISTIC {(uint16_t)0xbb03};
const NimBLEUUID BB04_CHARACTERISTIC {(uint16_t)0xbb04};
const NimBLEUUID BB05_CHARACTERISTIC {(uint16_t)0xbb05};
const NimBLEUUID BB06_CHARACTERISTIC {(uint16_t)0xbb06};
const NimBLEUUID BB08_CHARACTERISTIC {(uint16_t)0xbb08};
const NimBLEUUID BB21_CHARACTERISTIC {(uint16_t)0xbb21};

constexpr uint8_t ATT_APPLICATION_ERROR = 0x80;
constexpr uint8_t ATT_INSUFFICIENT_AUTHENTICATION = 0x05;
constexpr uint8_t ATT_INVALID_VALUE = 0x13;
constexpr uint8_t ATT_VALUE_NOT_ALLOWED = 0x85;
constexpr uint8_t ATT_PAIRING_COMMAND_ERROR = 0x90;
constexpr uint8_t ATT_LOCATION_UNAVAILABLE = 0x9d;
constexpr uint8_t ATT_LOCATION_NOT_LOCKED = 0x9e;
constexpr uint8_t ATT_LOCATION_LOCKED_BY_OTHER = 0x9f;

}  // namespace

SonyVirtualCamera::SonyVirtualCamera() : SonyVirtualCamera(Config {}) {}

SonyVirtualCamera::SonyVirtualCamera(const Config &config) : m_Config(config) {
  m_PowerOn = m_Config.camera_on;
  m_LockOwner = m_Config.lock_owner;
  m_TimeCorrection = m_Config.time_correction;
  m_AreaAdjustment = m_Config.area_adjustment;
}

SonyVirtualCamera::~SonyVirtualCamera() = default;

bool SonyVirtualCamera::matches(const NimBLEUUID &left, const NimBLEUUID &right) {
  return left == right;
}

std::string SonyVirtualCamera::key(const NimBLEUUID &service, const NimBLEUUID &characteristic) {
  return service.toString() + "/" + characteristic.toString();
}

uint16_t SonyVirtualCamera::readLE16(const std::vector<uint8_t> &value) {
  if (value.size() < 2) {
    return 0;
  }
  return static_cast<uint16_t>(value[0]) | (static_cast<uint16_t>(value[1]) << 8);
}

bool SonyVirtualCamera::validClockFields(uint8_t month,
                                         uint8_t day,
                                         uint8_t hour,
                                         uint8_t minute,
                                         uint8_t second) {
  return (month >= 1) && (month <= 12) && (day >= 1) && (day <= 31) && (hour <= 23)
         && (minute <= 59) && (second <= 59);
}

const char *SonyVirtualCamera::commandName(Command command) {
  switch (command) {
    case Command::PairingRegistration:
      return "EE01 pairing registration";
    case Command::PairingSavedHandle:
      return "EE02 saved pair handle";
    case Command::LocationLock:
      return "DD30 location lock";
    case Command::LocationEnable:
      return "DD31 location enable";
    case Command::LocationTimeCorrection:
      return "DD32 time correction";
    case Command::LocationAreaAdjustment:
      return "DD33 area adjustment";
    case Command::LocationUpdate91:
      return "DD11 location update 91";
    case Command::LocationUpdate95:
      return "DD11 location update 95";
    case Command::CameraPowerOff:
      return "CC02 power off";
    case Command::CameraApoAvoidance:
      return "CC02 APO avoidance";
    case Command::WifiOn:
      return "CC08 Wi-Fi on";
    case Command::ClockSet:
      return "CC13 clock set";
    case Command::Ff01FocusDown:
      return "FF01 focus down";
    case Command::Ff01FocusUp:
      return "FF01 focus up";
    case Command::Ff01ShutterDown:
      return "FF01 shutter down";
    case Command::Ff01ShutterUp:
      return "FF01 shutter up";
    case Command::Ff01Unknown:
      return "FF01 unknown opcode";
    case Command::DirxtxRecordStart:
      return "BB record start";
    case Command::DirxtxRecordStop:
      return "BB record stop";
    case Command::Unknown:
    default:
      return "unknown";
  }
}

bool SonyVirtualCamera::vendorPairingGate() const {
  return m_Config.pairing_enabled;
}

bool SonyVirtualCamera::centralModelGate() const {
  return m_Config.pairing_supported && m_Config.pairing_enabled && m_Config.legacy_bit1;
}

NimBLEAdvertisedDevice SonyVirtualCamera::advertisement() const {
  // Field layout from the Sony vendor decoder: company 0x012d, type 0x0003,
  // big-endian protocol version, two ASCII model bytes, then 3-byte tag
  // records. Furble reads tag 0x22 at offset 8 and tag 0x21 at offset 11, so
  // those two records come first. Tag 0x23 follows and a zero tag terminates
  // the vendor scan. The peer adds no service UUIDs.
  std::vector<uint8_t> manufacturer = {
      0x2d,
      0x01,
      0x03,
      0x00,
      static_cast<uint8_t>((m_Config.protocol_version >> 8) & 0xff),
      static_cast<uint8_t>(m_Config.protocol_version & 0xff),
      m_Config.model_code[0],
      m_Config.model_code[1],
  };

  uint8_t mode22 = 0;
  if (m_Config.pairing_supported) {
    mode22 |= 0x80;
  }
  if (m_Config.pairing_enabled) {
    mode22 |= 0x40;
  }
  if (m_Config.location_supported) {
    mode22 |= 0x20;
  }
  if (m_Config.location_enabled) {
    mode22 |= 0x10;
  }
  if (m_Config.legacy_bit1) {
    mode22 |= 0x02;
  }
  manufacturer.push_back(0x22);
  manufacturer.push_back(mode22);
  manufacturer.push_back(0x00);

  uint8_t mode21 = 0;
  if (m_Config.wireless_power_on) {
    mode21 |= 0x80;
  }
  if (m_Config.camera_on) {
    mode21 |= 0x40;
  }
  if (m_Config.wifi_handover_supported) {
    mode21 |= 0x20;
  }
  if (m_Config.wifi_handover_enabled) {
    mode21 |= 0x10;
  }
  manufacturer.push_back(0x21);
  manufacturer.push_back(mode21);
  manufacturer.push_back(0x00);

  uint8_t mode23 = 0;
  if (m_Config.remote_control_supported) {
    mode23 |= 0x80;
  }
  if (m_Config.remote_control_enabled) {
    mode23 |= 0x20;
  }
  if (m_Config.image_transfer_supported) {
    mode23 |= 0x10;
  }
  if (m_Config.image_transfer_enabled) {
    mode23 |= 0x04;
  }
  if (m_Config.push_transfer_supported) {
    mode23 |= 0x02;
  }
  if (m_Config.push_transfer_enabled) {
    mode23 |= 0x01;
  }
  uint8_t mode23Tail = 0;
  if (m_Config.background_transfer_supported) {
    mode23Tail |= 0x80;
  }
  if (m_Config.background_transfer_enabled) {
    mode23Tail |= 0x20;
  }
  manufacturer.push_back(0x23);
  manufacturer.push_back(mode23);
  manufacturer.push_back(mode23Tail);
  manufacturer.push_back(0x00);  // terminator for the vendor tag scan

  NimBLEAdvertisedDevice device;
  device.setAddress(m_Config.address);
  device.setName(m_Config.name);
  device.setManufacturerData(manufacturer.data(), manufacturer.size());
  device.setRSSI(-55);
  return device;
}

const SonyVirtualCamera::Config &SonyVirtualCamera::config() const {
  return m_Config;
}

bool SonyVirtualCamera::cameraBonded() const {
  return m_Config.camera_bonded;
}

bool SonyVirtualCamera::registered() const {
  return m_Registered;
}

bool SonyVirtualCamera::configured() const {
  return m_Registered;
}

bool SonyVirtualCamera::secured() const {
  return m_Secured;
}

bool SonyVirtualCamera::staleBond() const {
  return m_StaleBond;
}

bool SonyVirtualCamera::secureAttempted() const {
  return m_SecureAttempted;
}

bool SonyVirtualCamera::pairingInProgress() const {
  return m_PairingInProgress;
}

uint32_t SonyVirtualCamera::pairingResult() const {
  return m_PairingResult;
}

void SonyVirtualCamera::removeCameraBond() {
  // Camera-side deletion only. The phone-side NimBLE bond is deliberately
  // left in place so the next secureConnection() models the one-sided stale
  // bond that a camera initialization produces.
  m_Config.camera_bonded = false;
  m_Registered = false;
  m_PairingResult = 0;
  m_StaleBond = false;
  m_Secured = false;
}

bool SonyVirtualCamera::cameraPowerOn() const {
  return m_PowerOn;
}

bool SonyVirtualCamera::wifiOn() const {
  return m_WifiOn;
}

bool SonyVirtualCamera::apoAvoided() const {
  return m_ApoAvoided;
}

bool SonyVirtualCamera::focusHeld() const {
  return m_FocusHeld;
}

bool SonyVirtualCamera::shutterHeld() const {
  return m_ShutterHeld;
}

bool SonyVirtualCamera::recording() const {
  return m_Recording;
}

uint8_t SonyVirtualCamera::lockOwner() const {
  return m_LockOwner;
}

bool SonyVirtualCamera::locationLocked() const {
  return m_LockOwner == 1;
}

bool SonyVirtualCamera::locationTransferEnabled() const {
  return m_LocationTransferEnabled;
}

uint8_t SonyVirtualCamera::timeCorrection() const {
  return m_TimeCorrection;
}

uint8_t SonyVirtualCamera::areaAdjustment() const {
  return m_AreaAdjustment;
}

SonyVirtualCamera::LocationForm SonyVirtualCamera::lastLocationForm() const {
  return m_LastLocationForm;
}

const std::vector<uint8_t> &SonyVirtualCamera::lastLocation() const {
  return m_LastLocation;
}

const std::vector<uint8_t> &SonyVirtualCamera::lastClock() const {
  return m_LastClock;
}

uint8_t SonyVirtualCamera::lastAttError() const {
  return m_LastAttError;
}

uint16_t SonyVirtualCamera::lastFf01Opcode() const {
  return m_LastFf01Opcode;
}

SonyVirtualCamera::Fault SonyVirtualCamera::lastFault() const {
  return m_LastFault;
}

bool SonyVirtualCamera::savedPairHandleAccepted() const {
  return m_SavedPairHandleAccepted;
}

const std::vector<SonyVirtualCamera::Write> &SonyVirtualCamera::writes() const {
  return m_Writes;
}

const std::vector<SonyVirtualCamera::Notification> &SonyVirtualCamera::notifications() const {
  return m_Notifications;
}

void SonyVirtualCamera::clearEvents() {
  m_Writes.clear();
  m_Notifications.clear();
}

bool SonyVirtualCamera::connected() const {
  return m_Connected;
}

void SonyVirtualCamera::setScript(const std::vector<ScriptStep> &steps) {
  m_Script.assign(steps.begin(), steps.end());
  m_Scripted = true;
}

void SonyVirtualCamera::appendScriptStep(const ScriptStep &step) {
  m_Script.push_back(step);
  m_Scripted = true;
}

void SonyVirtualCamera::clearScript() {
  m_Script.clear();
  m_Scripted = false;
}

size_t SonyVirtualCamera::scriptRemaining() const {
  return m_Script.size();
}

void SonyVirtualCamera::setScripted(bool enabled) {
  m_Scripted = enabled;
}

bool SonyVirtualCamera::scripted() const {
  return m_Scripted;
}

void SonyVirtualCamera::scriptNextCommand(Fault fault) {
  m_Script.push_back({Command::Unknown, fault});
  m_Scripted = true;
}

size_t SonyVirtualCamera::scriptedRemaining() const {
  return m_Script.size();
}

void SonyVirtualCamera::setNextFault(Fault fault) {
  m_NextFault = fault;
}

SonyVirtualCamera::Fault SonyVirtualCamera::pendingFault() const {
  return m_NextFault;
}

void SonyVirtualCamera::clearFault() {
  m_NextFault = Fault::None;
}

void SonyVirtualCamera::clearFaults() {
  m_NextFault = Fault::None;
  m_Script.clear();
  m_Scripted = false;
}

SonyVirtualCamera::Fault SonyVirtualCamera::takeFault(Command command) {
  if (m_NextFault != Fault::None) {
    const Fault fault = m_NextFault;
    m_NextFault = Fault::None;
    return fault;
  }
  if (m_Scripted && !m_Script.empty()
      && ((m_Script.front().command == Command::Unknown)
          || (m_Script.front().command == command))) {
    const Fault fault = m_Script.front().fault;
    m_Script.pop_front();
    return fault;
  }
  return Fault::None;
}

void SonyVirtualCamera::noteAttError(uint8_t error) {
  m_LastAttError = error;
}

void SonyVirtualCamera::recordWrite(const NimBLEUUID &service,
                                    const NimBLEUUID &characteristic,
                                    Command command,
                                    const std::vector<uint8_t> &value,
                                    bool response,
                                    bool accepted) {
  Write write;
  write.service = service.toString();
  write.characteristic = characteristic.toString();
  write.command = commandName(command);
  write.payload = value;
  write.response = response;
  write.accepted = accepted;
  m_Writes.push_back(write);
}

bool SonyVirtualCamera::emitNotification(const NimBLEUUID &service,
                                         const NimBLEUUID &characteristic,
                                         const std::vector<uint8_t> &payload) {
  const auto found = m_Subscriptions.find(key(service, characteristic));
  if ((found == m_Subscriptions.end()) || (found->second.callback == nullptr)) {
    return false;
  }

  Notification notification;
  notification.service = service.toString();
  notification.characteristic = characteristic.toString();
  notification.payload = payload;
  m_Notifications.push_back(notification);

  std::vector<uint8_t> mutable_payload = payload;
  found->second.callback(found->second.remote,
                         mutable_payload.empty() ? nullptr : mutable_payload.data(),
                         mutable_payload.size(), found->second.notification);
  return true;
}

bool SonyVirtualCamera::emitFf02(uint8_t command, uint8_t value) {
  if (!m_Config.ff02_notifications) {
    return false;
  }
  return emitNotification(CONTROL_SERVICE, FF02_CHARACTERISTIC, {0x02, command, value});
}

bool SonyVirtualCamera::emitLocationAvailability(bool available) {
  const std::vector<uint8_t> payload = available ? std::vector<uint8_t> {0x03, 0x01, 0x03, 0x01}
                                                 : std::vector<uint8_t> {0x03, 0x01, 0x02, 0x00};
  return emitNotification(LOCATION_SERVICE, DD01_CHARACTERISTIC, payload);
}

bool SonyVirtualCamera::emitCc09Identifier(uint8_t identifier,
                                           const std::vector<uint8_t> &payload) {
  if (!m_Config.cc00_present) {
    return false;
  }
  std::vector<uint8_t> frame = {identifier};
  frame.insert(frame.end(), payload.begin(), payload.end());
  return emitNotification(CAMERA_SERVICE, CC09_CHARACTERISTIC, frame);
}

bool SonyVirtualCamera::emitCameraStatus(const std::vector<uint8_t> &payload) {
  if (!m_Config.cc00_present) {
    return false;
  }
  // The identifier and payload bytes are proven. The TLV length framing is
  // not, so the caller supplies the complete frame rather than this peer
  // inventing one.
  return emitNotification(CAMERA_SERVICE, CC09_CHARACTERISTIC, payload);
}

bool SonyVirtualCamera::emitPairingState(uint32_t value) {
  const std::vector<uint8_t> payload = {
      0x07,
      0x00,
      0x00,
      0x01,
      static_cast<uint8_t>((value >> 24) & 0xff),
      static_cast<uint8_t>((value >> 16) & 0xff),
      static_cast<uint8_t>((value >> 8) & 0xff),
      static_cast<uint8_t>(value & 0xff),
  };
  return emitNotification(PAIRING_SERVICE, EE03_CHARACTERISTIC, payload);
}

bool SonyVirtualCamera::emitPairingResult(uint32_t value) {
  const std::vector<uint8_t> payload = {
      0x07,
      0x00,
      0x00,
      0x02,
      static_cast<uint8_t>((value >> 24) & 0xff),
      static_cast<uint8_t>((value >> 16) & 0xff),
      static_cast<uint8_t>((value >> 8) & 0xff),
      static_cast<uint8_t>(value & 0xff),
  };
  return emitNotification(PAIRING_SERVICE, EE03_CHARACTERISTIC, payload);
}

void SonyVirtualCamera::resetSession() {
  m_Subscriptions.clear();
  m_PairingInProgress = false;
}

bool SonyVirtualCamera::acceptConnection(NimBLEClient &client, const NimBLEAddress &address) {
  if (m_Connected || (address != m_Config.address)) {
    return false;
  }
  m_Client = &client;
  m_Connected = true;
  m_Secured = false;
  m_StaleBond = false;
  m_LastAttError = 0;
  m_SavedPairHandleAccepted = false;
  resetSession();
  return true;
}

void SonyVirtualCamera::disconnect(NimBLEClient &client, int reason) {
  (void)reason;
  if (m_Client != &client) {
    return;
  }
  m_Client = nullptr;
  m_Connected = false;
  m_Secured = false;
  m_PairingInProgress = false;
  m_Subscriptions.clear();
  if (m_Config.release_lock_on_disconnect) {
    m_LockOwner = 0;
    m_LocationTransferEnabled = false;
    m_Config.location_enabled = false;
  }
}

bool SonyVirtualCamera::hasService(const NimBLEUUID &service) const {
  return (m_Config.cc00_present && matches(service, CAMERA_SERVICE))
         || (m_Config.location_service_present && matches(service, LOCATION_SERVICE))
         || (m_Config.pairing_service_present && matches(service, PAIRING_SERVICE))
         || (m_Config.control_service_present && matches(service, CONTROL_SERVICE))
         || (m_Config.dirxtx_service_present && matches(service, DIRXTX_SERVICE));
}

bool SonyVirtualCamera::knownDirxtxCharacteristic(const NimBLEUUID &characteristic) {
  return matches(characteristic, BB01_CHARACTERISTIC)
         || matches(characteristic, BB02_CHARACTERISTIC)
         || matches(characteristic, BB03_CHARACTERISTIC)
         || matches(characteristic, BB04_CHARACTERISTIC)
         || matches(characteristic, BB05_CHARACTERISTIC)
         || matches(characteristic, BB06_CHARACTERISTIC)
         || matches(characteristic, BB08_CHARACTERISTIC)
         || matches(characteristic, BB21_CHARACTERISTIC);
}

bool SonyVirtualCamera::hasCharacteristic(const NimBLEUUID &service,
                                          const NimBLEUUID &characteristic) const {
  if (matches(service, CAMERA_SERVICE) && m_Config.cc00_present) {
    return matches(characteristic, CC02_CHARACTERISTIC)
           || matches(characteristic, CC03_CHARACTERISTIC)
           || matches(characteristic, CC05_CHARACTERISTIC)
           || matches(characteristic, CC06_CHARACTERISTIC)
           || matches(characteristic, CC07_CHARACTERISTIC)
           || matches(characteristic, CC08_CHARACTERISTIC)
           || matches(characteristic, CC09_CHARACTERISTIC)
           || matches(characteristic, CC0A_CHARACTERISTIC)
           || matches(characteristic, CC0B_CHARACTERISTIC)
           || matches(characteristic, CC0C_CHARACTERISTIC)
           || matches(characteristic, CC0D_CHARACTERISTIC)
           || matches(characteristic, CC0E_CHARACTERISTIC)
           || matches(characteristic, CC0F_CHARACTERISTIC)
           || matches(characteristic, CC10_CHARACTERISTIC)
           || matches(characteristic, CC13_CHARACTERISTIC);
  }
  if (matches(service, LOCATION_SERVICE) && m_Config.location_service_present) {
    return matches(characteristic, DD01_CHARACTERISTIC)
           || matches(characteristic, DD11_CHARACTERISTIC)
           || matches(characteristic, DD21_CHARACTERISTIC)
           || matches(characteristic, DD30_CHARACTERISTIC)
           || matches(characteristic, DD31_CHARACTERISTIC)
           || matches(characteristic, DD32_CHARACTERISTIC)
           || matches(characteristic, DD33_CHARACTERISTIC);
  }
  if (matches(service, PAIRING_SERVICE) && m_Config.pairing_service_present) {
    return matches(characteristic, EE01_CHARACTERISTIC)
           || (m_Config.ee02_present && matches(characteristic, EE02_CHARACTERISTIC))
           || matches(characteristic, EE03_CHARACTERISTIC)
           || matches(characteristic, EE04_CHARACTERISTIC);
  }
  if (matches(service, CONTROL_SERVICE) && m_Config.control_service_present) {
    return matches(characteristic, FF01_CHARACTERISTIC)
           || matches(characteristic, FF02_CHARACTERISTIC);
  }
  if (matches(service, DIRXTX_SERVICE) && m_Config.dirxtx_service_present) {
    return knownDirxtxCharacteristic(characteristic);
  }
  return false;
}

bool SonyVirtualCamera::discoverCharacteristic(NimBLEClient &client,
                                               const NimBLEUUID &service,
                                               const NimBLEUUID &characteristic) {
  (void)client;
  return hasCharacteristic(service, characteristic);
}

bool SonyVirtualCamera::canWrite(const NimBLEUUID &service,
                                 const NimBLEUUID &characteristic) const {
  if (matches(service, CAMERA_SERVICE) && m_Config.cc00_present) {
    return matches(characteristic, CC02_CHARACTERISTIC)
           || matches(characteristic, CC08_CHARACTERISTIC)
           || matches(characteristic, CC13_CHARACTERISTIC);
  }
  if (matches(service, LOCATION_SERVICE) && m_Config.location_service_present) {
    return matches(characteristic, DD11_CHARACTERISTIC)
           || matches(characteristic, DD30_CHARACTERISTIC)
           || matches(characteristic, DD31_CHARACTERISTIC)
           || matches(characteristic, DD32_CHARACTERISTIC)
           || matches(characteristic, DD33_CHARACTERISTIC);
  }
  if (matches(service, PAIRING_SERVICE) && m_Config.pairing_service_present) {
    return matches(characteristic, EE01_CHARACTERISTIC)
           || (m_Config.ee02_present && matches(characteristic, EE02_CHARACTERISTIC));
  }
  if (matches(service, DIRXTX_SERVICE) && m_Config.dirxtx_service_present) {
    // The channel exists, but the per-characteristic role and opcode mapping
    // are unknown (spec U-25). Writes reach write() only so the unproven
    // record-control path can reject them, or accept them behind its switch.
    return knownDirxtxCharacteristic(characteristic);
  }
  return matches(service, CONTROL_SERVICE) && m_Config.control_service_present
         && matches(characteristic, FF01_CHARACTERISTIC);
}

bool SonyVirtualCamera::handleClock(const std::vector<uint8_t> &value) {
  if (value.size() != 13) {
    noteAttError(ATT_VALUE_NOT_ALLOWED);
    return false;
  }
  if ((value[0] != 0x12) || (value[1] != 0x00) || (value[2] != 0x00)) {
    noteAttError(ATT_VALUE_NOT_ALLOWED);
    return false;
  }
  const uint16_t year = static_cast<uint16_t>((static_cast<uint16_t>(value[3]) << 8) | value[4]);
  (void)year;
  if (!validClockFields(value[5], value[6], value[7], value[8], value[9])) {
    noteAttError(ATT_VALUE_NOT_ALLOWED);
    return false;
  }
  if (value[10] > 1) {
    noteAttError(ATT_VALUE_NOT_ALLOWED);
    return false;
  }
  const int8_t offsetHours = static_cast<int8_t>(value[11]);
  if ((offsetHours < 0) && !m_Config.accept_negative_clock_offset) {
    // The 13-byte layout is documented, but negative offset sign handling is
    // inference (spec U-19). Do not report success for an unproven encoding.
    noteAttError(ATT_VALUE_NOT_ALLOWED);
    return false;
  }
  if ((offsetHours > 23) || (value[12] > 59)) {
    noteAttError(ATT_VALUE_NOT_ALLOWED);
    return false;
  }
  m_LastClock = value;
  return true;
}

bool SonyVirtualCamera::handlePairingRegistration(const std::vector<uint8_t> &value) {
  if ((value.size() != 7) || (value[0] != 0x06)) {
    noteAttError(ATT_PAIRING_COMMAND_ERROR);
    return false;
  }
  const uint16_t command = static_cast<uint16_t>((static_cast<uint16_t>(value[1]) << 8) | value[2]);
  if ((command != 0x0801) && (command != 0x0802)) {
    noteAttError(ATT_PAIRING_COMMAND_ERROR);
    return false;
  }
  if ((value[3] != 0x00) || (value[4] != 0x00) || (value[5] != 0x00) || (value[6] != 0x00)) {
    noteAttError(ATT_PAIRING_COMMAND_ERROR);
    return false;
  }

  if (m_Registered && !m_Config.accept_repeat_registration) {
    m_PairingResult = 10;
    if (m_Config.pairing_state_notifications) {
      emitPairingResult(m_PairingResult);
    }
    return true;
  }

  m_PairingInProgress = true;
  if (m_Config.pairing_state_notifications) {
    for (const uint32_t state : m_Config.pairing_state_sequence) {
      emitPairingState(state);
    }
  }

  uint32_t result = m_Config.pairing_result;
  if (m_Config.reject_pairing && (result == 1)) {
    result = 3;
  }
  m_PairingResult = result;
  if (m_Config.pairing_state_notifications) {
    emitPairingResult(result);
  }

  m_PairingInProgress = false;
  m_Registered = (result == 1);
  if (m_Registered && m_Config.registration_creates_bond) {
    // Some vendor flows bond during the EE01 registration. The peer does not
    // assume that by default because the SMP exchange itself was not captured.
    m_Config.camera_bonded = true;
    NimBLEDevice::setBondedAddress(m_Config.address);
  }
  return true;
}

bool SonyVirtualCamera::handleLocationLock(const std::vector<uint8_t> &value) {
  if (value.size() != 1) {
    noteAttError(ATT_VALUE_NOT_ALLOWED);
    return false;
  }
  if (!m_Config.location_supported) {
    noteAttError(ATT_LOCATION_UNAVAILABLE);
    return false;
  }
  if ((value[0] != 0x00) && (value[0] != 0x01)) {
    noteAttError(ATT_VALUE_NOT_ALLOWED);
    return false;
  }
  if (value[0] == 0x01) {
    if (m_LockOwner == 2) {
      noteAttError(ATT_LOCATION_LOCKED_BY_OTHER);
      return false;
    }
    m_LockOwner = 1;
  } else {
    if (m_LockOwner == 2) {
      noteAttError(ATT_LOCATION_LOCKED_BY_OTHER);
      return false;
    }
    m_LockOwner = 0;
    m_LocationTransferEnabled = false;
    m_Config.location_enabled = false;
  }
  return true;
}

bool SonyVirtualCamera::handleLocationEnable(const std::vector<uint8_t> &value) {
  if (value.size() != 1) {
    noteAttError(ATT_VALUE_NOT_ALLOWED);
    return false;
  }
  if (!m_Config.location_supported) {
    noteAttError(ATT_LOCATION_UNAVAILABLE);
    return false;
  }
  if ((value[0] != 0x00) && (value[0] != 0x01)) {
    noteAttError(ATT_VALUE_NOT_ALLOWED);
    return false;
  }
  if (m_LockOwner != 1) {
    // The firmware requires the DD30 lock before DD31 accepts a value.
    noteAttError(ATT_LOCATION_NOT_LOCKED);
    return false;
  }
  m_LocationTransferEnabled = (value[0] == 0x01);
  m_Config.location_enabled = m_LocationTransferEnabled;
  emitLocationAvailability(m_LocationTransferEnabled);
  return true;
}

bool SonyVirtualCamera::handleLocationUpdate(const std::vector<uint8_t> &value) {
  if (!m_Config.location_supported || !m_LocationTransferEnabled) {
    noteAttError(ATT_LOCATION_UNAVAILABLE);
    return false;
  }
  if (m_LockOwner != 1) {
    noteAttError(ATT_LOCATION_NOT_LOCKED);
    return false;
  }

  const bool is95 = value.size() == 95;
  const bool is91 = value.size() == 91;
  if (!is95 && !is91) {
    noteAttError(ATT_VALUE_NOT_ALLOWED);
    return false;
  }
  if (is95 && m_Config.require_91_byte_location) {
    // The base form is the only form this body accepts (capture-gated).
    noteAttError(ATT_VALUE_NOT_ALLOWED);
    return false;
  }

  const uint8_t expectedLength = is95 ? 0x5d : 0x59;
  const uint8_t expectedByte5 = is95 ? 0x03 : 0x00;
  if ((value[0] != 0x00) || (value[1] != expectedLength) || (value[2] != 0x08) || (value[3] != 0x02)
      || (value[4] != 0xfc) || (value[5] != expectedByte5) || (value[6] != 0x00)
      || (value[7] != 0x00) || (value[8] != 0x10) || (value[9] != 0x10) || (value[10] != 0x10)) {
    noteAttError(ATT_VALUE_NOT_ALLOWED);
    return false;
  }
  if (!validClockFields(value[21], value[22], value[23], value[24], value[25])) {
    noteAttError(ATT_VALUE_NOT_ALLOWED);
    return false;
  }

  m_LastLocation = value;
  m_LastLocationForm = is95 ? LocationForm::Bytes95 : LocationForm::Bytes91;
  return true;
}

bool SonyVirtualCamera::handleFf01(const std::vector<uint8_t> &value) {
  if (!m_Config.allow_unproven_ff01) {
    // FF01 itself is firmware-confirmed, but the opcode parser was never
    // recovered and the four Furble values are public-source only.
    noteAttError(ATT_INVALID_VALUE);
    return false;
  }
  if (value.size() != 2) {
    noteAttError(ATT_VALUE_NOT_ALLOWED);
    return false;
  }

  const uint16_t opcode = readLE16(value);
  m_LastFf01Opcode = opcode;
  switch (opcode) {
    case 0x0701:
      m_FocusHeld = true;
      if (m_Config.ff02_notifications) {
        emitFf02(0x3f, 0x20);
      }
      return true;
    case 0x0601:
      m_FocusHeld = false;
      if (m_Config.ff02_notifications) {
        emitFf02(0x3f, 0x00);
      }
      return true;
    case 0x0901:
      m_ShutterHeld = true;
      if (m_Config.ff02_notifications) {
        emitFf02(0xa0, 0x20);
      }
      return true;
    case 0x0801:
      m_ShutterHeld = false;
      if (m_Config.ff02_notifications) {
        emitFf02(0xa0, 0x00);
      }
      return true;
    default:
      noteAttError(ATT_INVALID_VALUE);
      return false;
  }
}

bool SonyVirtualCamera::handleDirxtx(const std::vector<uint8_t> &value) {
  if (!m_Config.allow_unproven_dirxtx) {
    // The record-control bytes are app-static for livestreaming, while the
    // carrying DiRxTx characteristic and still-shutter role are unproven.
    noteAttError(ATT_INVALID_VALUE);
    return false;
  }
  if (value.size() != 1) {
    noteAttError(ATT_VALUE_NOT_ALLOWED);
    return false;
  }
  if (value[0] == 0x01) {
    m_Recording = true;
    return true;
  }
  if (value[0] == 0x02) {
    m_Recording = false;
    return true;
  }
  noteAttError(ATT_VALUE_NOT_ALLOWED);
  return false;
}

bool SonyVirtualCamera::write(NimBLEClient &client,
                              const NimBLEUUID &service,
                              const NimBLEUUID &characteristic,
                              const std::vector<uint8_t> &value,
                              bool response) {
  if (!m_Connected || (m_Client != &client) || !canWrite(service, characteristic)) {
    return false;
  }

  Command command = Command::Unknown;
  if (matches(service, CAMERA_SERVICE)) {
    if (matches(characteristic, CC02_CHARACTERISTIC)) {
      if (value == std::vector<uint8_t> {0x03, 0x08, 0x0d, 0x00}) {
        command = Command::CameraPowerOff;
      } else if (value == std::vector<uint8_t> {0x03, 0x08, 0x10, 0x00}) {
        command = Command::CameraApoAvoidance;
      }
    } else if (matches(characteristic, CC08_CHARACTERISTIC)) {
      command = Command::WifiOn;
    } else if (matches(characteristic, CC13_CHARACTERISTIC)) {
      command = Command::ClockSet;
    }
  } else if (matches(service, LOCATION_SERVICE)) {
    if (matches(characteristic, DD30_CHARACTERISTIC)) {
      command = Command::LocationLock;
    } else if (matches(characteristic, DD31_CHARACTERISTIC)) {
      command = Command::LocationEnable;
    } else if (matches(characteristic, DD32_CHARACTERISTIC)) {
      command = Command::LocationTimeCorrection;
    } else if (matches(characteristic, DD33_CHARACTERISTIC)) {
      command = Command::LocationAreaAdjustment;
    } else if (matches(characteristic, DD11_CHARACTERISTIC)) {
      command = value.size() == 91 ? Command::LocationUpdate91 : Command::LocationUpdate95;
    }
  } else if (matches(service, PAIRING_SERVICE)) {
    if (matches(characteristic, EE01_CHARACTERISTIC)) {
      command = Command::PairingRegistration;
    } else if (matches(characteristic, EE02_CHARACTERISTIC)) {
      command = Command::PairingSavedHandle;
    }
  } else if (matches(service, CONTROL_SERVICE) && matches(characteristic, FF01_CHARACTERISTIC)) {
    const uint16_t opcode = readLE16(value);
    switch (opcode) {
      case 0x0701:
        command = Command::Ff01FocusDown;
        break;
      case 0x0601:
        command = Command::Ff01FocusUp;
        break;
      case 0x0901:
        command = Command::Ff01ShutterDown;
        break;
      case 0x0801:
        command = Command::Ff01ShutterUp;
        break;
      default:
        command = Command::Ff01Unknown;
        break;
    }
  } else if (matches(service, DIRXTX_SERVICE)) {
    if (value.size() == 1 && value[0] == 0x01) {
      command = Command::DirxtxRecordStart;
    } else if (value.size() == 1 && value[0] == 0x02) {
      command = Command::DirxtxRecordStop;
    }
  }

  const Fault fault = takeFault(command);
  m_LastFault = fault;
  m_LastAttError = 0;

  if ((fault == Fault::Reject) || (fault == Fault::Busy) || (fault == Fault::WrongMode)) {
    noteAttError(fault == Fault::Reject ? ATT_INVALID_VALUE : ATT_APPLICATION_ERROR);
    recordWrite(service, characteristic, command, value, response, false);
    return false;
  }
  if (fault == Fault::StaleBond) {
    m_Config.camera_bonded = false;
    m_Registered = false;
    m_StaleBond = true;
    noteAttError(ATT_INSUFFICIENT_AUTHENTICATION);
    recordWrite(service, characteristic, command, value, response, false);
    return false;
  }
  if (fault == Fault::Timeout) {
    // The ATT write completes but no application response or notification is
    // produced, so a caller that waits for one times out.
    recordWrite(service, characteristic, command, value, response, true);
    return true;
  }

  bool accepted = false;
  if (command == Command::CameraPowerOff) {
    m_PowerOn = false;
    m_Config.camera_on = false;
    accepted = true;
  } else if (command == Command::CameraApoAvoidance) {
    m_ApoAvoided = true;
    accepted = true;
  } else if (command == Command::WifiOn) {
    if ((value.size() == 1) && (value[0] == 0x01)) {
      m_WifiOn = true;
      m_Config.wifi_handover_enabled = true;
      accepted = true;
      emitCc09Identifier(1, {0x02, 0x00});
    } else {
      noteAttError(ATT_VALUE_NOT_ALLOWED);
    }
  } else if (command == Command::ClockSet) {
    accepted = handleClock(value);
  } else if (command == Command::LocationLock) {
    accepted = handleLocationLock(value);
  } else if (command == Command::LocationEnable) {
    accepted = handleLocationEnable(value);
  } else if ((command == Command::LocationTimeCorrection)
             || (command == Command::LocationAreaAdjustment)) {
    if (value.size() != 1) {
      noteAttError(ATT_VALUE_NOT_ALLOWED);
    } else if ((value[0] != 0x00) && (value[0] != 0x01)) {
      noteAttError(ATT_VALUE_NOT_ALLOWED);
    } else {
      if (command == Command::LocationTimeCorrection) {
        m_TimeCorrection = value[0];
      } else {
        m_AreaAdjustment = value[0];
      }
      accepted = true;
    }
  } else if ((command == Command::LocationUpdate91) || (command == Command::LocationUpdate95)) {
    accepted = handleLocationUpdate(value);
  } else if (command == Command::PairingRegistration) {
    accepted = handlePairingRegistration(value);
  } else if (command == Command::PairingSavedHandle) {
    if (!m_Config.allow_unproven_ee02) {
      // EE02 exists in the firmware table, but its payload schema was not
      // recovered. Do not report the unproven write as accepted.
      noteAttError(ATT_INVALID_VALUE);
    } else if (value.size() != m_Config.saved_pair_handle.size()) {
      noteAttError(ATT_VALUE_NOT_ALLOWED);
    } else {
      m_Config.saved_pair_handle = {value[0], value[1], value[2], value[3]};
      m_SavedPairHandleAccepted = true;
      accepted = true;
    }
  } else if ((command == Command::Ff01FocusDown) || (command == Command::Ff01FocusUp)
             || (command == Command::Ff01ShutterDown) || (command == Command::Ff01ShutterUp)
             || (command == Command::Ff01Unknown)) {
    accepted = handleFf01(value);
  } else if ((command == Command::DirxtxRecordStart) || (command == Command::DirxtxRecordStop)) {
    accepted = handleDirxtx(value);
  } else {
    noteAttError(ATT_INVALID_VALUE);
  }

  recordWrite(service, characteristic, command, value, response, accepted);

  if (fault == Fault::Malformed) {
    // Run the command, then deliver a deliberately short FF02 frame so a
    // parser that trusts the documented three-byte layout fails visibly.
    emitNotification(CONTROL_SERVICE, FF02_CHARACTERISTIC, {0x02, 0x3f});
  }
  if (fault == Fault::Disconnect) {
    client.mockDropLink(0x13, /*fire_callback=*/true);
    return false;
  }
  return accepted;
}

NimBLEAttValue SonyVirtualCamera::read(NimBLEClient &client,
                                       const NimBLEUUID &service,
                                       const NimBLEUUID &characteristic) {
  if (!m_Connected || (m_Client != &client) || !hasCharacteristic(service, characteristic)) {
    return {};
  }

  if (matches(service, CAMERA_SERVICE)) {
    if (matches(characteristic, CC0A_CHARACTERISTIC)) {
      return NimBLEAttValue(m_Config.firmware_version);
    }
    if (matches(characteristic, CC0B_CHARACTERISTIC)) {
      return NimBLEAttValue(m_Config.model_name);
    }
    // CC06, CC07, CC0C, and CC0D carry Wi-Fi and camera information whose
    // value formats are unknown, so the peer does not fabricate a payload.
    return {};
  }
  if (matches(service, LOCATION_SERVICE)) {
    if (matches(characteristic, DD21_CHARACTERISTIC)) {
      // At least five bytes. Byte 4 bit 1 selects the 95-byte timezone form.
      const uint8_t flags = m_Config.timezone_supported ? 0x02 : 0x00;
      return NimBLEAttValue({0x00, 0x00, 0x00, 0x00, flags});
    }
    if (matches(characteristic, DD30_CHARACTERISTIC)) {
      // A lock held by another device still reads as locked. Only the owner
      // can release it, and a read does not prove ownership (spec U-15).
      return NimBLEAttValue(
          {m_LockOwner != 0 ? static_cast<uint8_t>(0x01) : static_cast<uint8_t>(0x00)});
    }
    if (matches(characteristic, DD31_CHARACTERISTIC)) {
      return NimBLEAttValue(
          {m_LocationTransferEnabled ? static_cast<uint8_t>(0x01) : static_cast<uint8_t>(0x00)});
    }
    if (matches(characteristic, DD32_CHARACTERISTIC)) {
      return NimBLEAttValue({m_TimeCorrection});
    }
    if (matches(characteristic, DD33_CHARACTERISTIC)) {
      return NimBLEAttValue({m_AreaAdjustment});
    }
    return {};
  }
  if (matches(service, PAIRING_SERVICE)) {
    if (matches(characteristic, EE04_CHARACTERISTIC)) {
      // TLV with a firmware length and bytes, then a model length and bytes.
      // The length field width is not captured (spec U-24), so this peer
      // uses one-byte lengths as an explicit test fixture.
      std::vector<uint8_t> info;
      const size_t firmwareLength = std::min<size_t>(m_Config.firmware_version.size(), 255);
      info.push_back(static_cast<uint8_t>(firmwareLength));
      info.insert(info.end(), m_Config.firmware_version.begin(),
                  m_Config.firmware_version.begin() + static_cast<std::ptrdiff_t>(firmwareLength));
      const size_t modelLength = std::min<size_t>(m_Config.model_name.size(), 255);
      info.push_back(static_cast<uint8_t>(modelLength));
      info.insert(info.end(), m_Config.model_name.begin(),
                  m_Config.model_name.begin() + static_cast<std::ptrdiff_t>(modelLength));
      return NimBLEAttValue(info);
    }
    if (matches(characteristic, EE02_CHARACTERISTIC) && m_Config.allow_unproven_ee02) {
      return NimBLEAttValue(std::vector<uint8_t>(m_Config.saved_pair_handle.begin(),
                                                 m_Config.saved_pair_handle.end()));
    }
    return {};
  }
  return {};
}

bool SonyVirtualCamera::subscribe(NimBLEClient &client,
                                  const NimBLEUUID &service,
                                  const NimBLEUUID &characteristic,
                                  bool notification,
                                  NimBLERemoteCharacteristic *remote,
                                  const NimBLENotifyCallback &callback,
                                  bool response) {
  if (!m_Connected || (m_Client != &client) || !hasCharacteristic(service, characteristic)
      || (callback == nullptr)) {
    return false;
  }

  Subscription subscription;
  subscription.client = &client;
  subscription.remote = remote;
  subscription.callback = callback;
  subscription.notification = notification;
  subscription.response = response;
  m_Subscriptions[key(service, characteristic)] = subscription;

  if (matches(service, LOCATION_SERVICE) && matches(characteristic, DD01_CHARACTERISTIC)
      && m_Config.emit_location_on_subscribe) {
    const bool available = m_Config.location_supported;
    emitLocationAvailability(available);
  }
  if (matches(service, CAMERA_SERVICE) && matches(characteristic, CC09_CHARACTERISTIC)
      && m_Config.emit_cc09_on_subscribe) {
    emitCc09Identifier(1, {m_Config.cc09_wifi_status, m_Config.cc09_wifi_error});
    emitCc09Identifier(2, {m_Config.cc09_image_transfer});
    emitCc09Identifier(3, {m_Config.cc09_remote_control});
  }
  if (matches(service, CONTROL_SERVICE) && matches(characteristic, FF02_CHARACTERISTIC)
      && m_Config.emit_ff02_on_subscribe && m_Config.ff02_notifications) {
    emitFf02(0x3f, m_FocusHeld ? static_cast<uint8_t>(0x20) : static_cast<uint8_t>(0x00));
    emitFf02(0xa0, m_ShutterHeld ? static_cast<uint8_t>(0x20) : static_cast<uint8_t>(0x00));
  }
  return true;
}

bool SonyVirtualCamera::secureConnection(NimBLEClient &client) {
  if (!m_Connected || (m_Client != &client)) {
    return false;
  }
  m_SecureAttempted = true;
  m_Secured = false;
  if (m_Config.reject_secure_connection || !m_Config.accept_numeric_comparison) {
    noteAttError(ATT_INSUFFICIENT_AUTHENTICATION);
    return false;
  }

  const bool localBonded = NimBLEDevice::isBonded(m_Config.address);
  if (localBonded != m_Config.camera_bonded) {
    // One side still holds the pairing and the other side deleted it. Sony
    // documents camera initialization as a deletion point and does not
    // document a recovery exchange, so the handshake fails as it does in
    // Furble, which has no stale-bond branch.
    m_StaleBond = true;
    noteAttError(ATT_INSUFFICIENT_AUTHENTICATION);
    if (m_Config.drop_on_stale_bond) {
      client.mockDropLink(0x05, /*fire_callback=*/true);
    }
    return false;
  }

  if (!localBonded) {
    NimBLEDevice::setBondedAddress(m_Config.address);
    m_Config.camera_bonded = true;
  }
  m_StaleBond = false;
  noteAttError(0);
  m_Secured = true;
  return true;
}

bool SonyVirtualCamera::updateConnectionParams(NimBLEClient &client,
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

int SonyVirtualCamera::getRssi() const {
  return -55;
}

const NimBLEUUID &SonyVirtualCamera::cameraServiceUUID() {
  return CAMERA_SERVICE;
}

const NimBLEUUID &SonyVirtualCamera::locationServiceUUID() {
  return LOCATION_SERVICE;
}

const NimBLEUUID &SonyVirtualCamera::pairingServiceUUID() {
  return PAIRING_SERVICE;
}

const NimBLEUUID &SonyVirtualCamera::controlServiceUUID() {
  return CONTROL_SERVICE;
}

const NimBLEUUID &SonyVirtualCamera::dirxtxServiceUUID() {
  return DIRXTX_SERVICE;
}

const NimBLEUUID &SonyVirtualCamera::cc02CharacteristicUUID() {
  return CC02_CHARACTERISTIC;
}

const NimBLEUUID &SonyVirtualCamera::cc08CharacteristicUUID() {
  return CC08_CHARACTERISTIC;
}

const NimBLEUUID &SonyVirtualCamera::cc09CharacteristicUUID() {
  return CC09_CHARACTERISTIC;
}

const NimBLEUUID &SonyVirtualCamera::cc0aCharacteristicUUID() {
  return CC0A_CHARACTERISTIC;
}

const NimBLEUUID &SonyVirtualCamera::cc0bCharacteristicUUID() {
  return CC0B_CHARACTERISTIC;
}

const NimBLEUUID &SonyVirtualCamera::cc13CharacteristicUUID() {
  return CC13_CHARACTERISTIC;
}

const NimBLEUUID &SonyVirtualCamera::dd01CharacteristicUUID() {
  return DD01_CHARACTERISTIC;
}

const NimBLEUUID &SonyVirtualCamera::dd11CharacteristicUUID() {
  return DD11_CHARACTERISTIC;
}

const NimBLEUUID &SonyVirtualCamera::dd21CharacteristicUUID() {
  return DD21_CHARACTERISTIC;
}

const NimBLEUUID &SonyVirtualCamera::dd30CharacteristicUUID() {
  return DD30_CHARACTERISTIC;
}

const NimBLEUUID &SonyVirtualCamera::dd31CharacteristicUUID() {
  return DD31_CHARACTERISTIC;
}

const NimBLEUUID &SonyVirtualCamera::dd32CharacteristicUUID() {
  return DD32_CHARACTERISTIC;
}

const NimBLEUUID &SonyVirtualCamera::dd33CharacteristicUUID() {
  return DD33_CHARACTERISTIC;
}

const NimBLEUUID &SonyVirtualCamera::ee01CharacteristicUUID() {
  return EE01_CHARACTERISTIC;
}

const NimBLEUUID &SonyVirtualCamera::ee02CharacteristicUUID() {
  return EE02_CHARACTERISTIC;
}

const NimBLEUUID &SonyVirtualCamera::ee03CharacteristicUUID() {
  return EE03_CHARACTERISTIC;
}

const NimBLEUUID &SonyVirtualCamera::ee04CharacteristicUUID() {
  return EE04_CHARACTERISTIC;
}

const NimBLEUUID &SonyVirtualCamera::ff01CharacteristicUUID() {
  return FF01_CHARACTERISTIC;
}

const NimBLEUUID &SonyVirtualCamera::ff02CharacteristicUUID() {
  return FF02_CHARACTERISTIC;
}

}  // namespace Host
}  // namespace Furble
