#include <algorithm>

#include "LumixVirtualCamera.h"

namespace Furble {
namespace Host {

namespace {

const NimBLEUUID SESSION_SERVICE {0x054ac620, 0x3214, 0x11e6, 0xac0d0002a5d5c51b};
const NimBLEUUID INFO_SERVICE {0x054ac620, 0x3214, 0x11e6, 0x00010002a5d5c51b};
const NimBLEUUID CONTROL_SERVICE {0x7be5faae, 0x475b, 0x11e7, 0xa91992ebcb67fe33};
const NimBLEUUID LOCATION_SERVICE {0x1d74afe0, 0x3214, 0x11e6, 0x8ab40002a5d5c51b};
const NimBLEUUID CLOCK_SERVICE {0x34738720, 0x3214, 0x11e6, 0xb66b0002a5d5c51b};
const NimBLEUUID WIFI_SERVICE {0x5ae57260, 0x3214, 0x11e6, 0x979e0002a5d5c51b};
const NimBLEUUID STATE_SERVICE {0x6b005f20, 0x3214, 0x11e6, 0xa5ac0002a5d5c51b};

const NimBLEUUID MEI0_CHARACTERISTIC {0x8d08a420, 0x3213, 0x11e6, 0x8aca0002a5d5c51b};
const NimBLEUUID DEVICE_NAME_CHARACTERISTIC {0xcd7a71a0, 0x3213, 0x11e6, 0x8f560002a5d5c51b};
const NimBLEUUID APS_KEY_CHARACTERISTIC {0x03e23a31, 0xa54c, 0x40fa, 0xa668de9acdc910bb};
const NimBLEUUID APS_IDENTITY_CHARACTERISTIC {0xbb5d2a8c, 0x5d3b, 0x43ad, 0xbfc3470cad55c1ca};
const NimBLEUUID APS_NAME_CHARACTERISTIC {0x7b61774f, 0xefb4, 0x44c6, 0xa66dc69e3b384c4a};
const NimBLEUUID WIFI_START_CHARACTERISTIC {0xe182ec40, 0x3213, 0x11e6, 0xab070002a5d5c51b};
const NimBLEUUID WIFI_ACK1_CHARACTERISTIC {0xe182ec41, 0x3213, 0x11e6, 0xab070002a5d5c51b};
const NimBLEUUID WIFI_ACK2_CHARACTERISTIC {0xe182ec42, 0x3213, 0x11e6, 0xab070002a5d5c51b};
const NimBLEUUID WIFI_ACK3_CHARACTERISTIC {0xe182ec43, 0x3213, 0x11e6, 0xab070002a5d5c51b};
const NimBLEUUID SSID_READ_CHARACTERISTIC {0xe206a5c0, 0x3214, 0x11e6, 0xafe40002a5d5c51b};
const NimBLEUUID CAMERA_NAME_CHARACTERISTIC {0xa09fad60, 0x3215, 0x11e6, 0xbdc00002a5d5c51b};
const NimBLEUUID AP_PASSWORD_CHARACTERISTIC {0xc97cf1a5, 0x3c03, 0x4290, 0x8c1b9e74b9500f54};
const NimBLEUUID CAMERA_INFO_CHARACTERISTIC {0x69e4d998, 0x54b7, 0x40f5, 0xa5f84cc236cd2347};
const NimBLEUUID CAPABILITY_CHARACTERISTIC {0x054ac621, 0x3214, 0x11e6, 0x00010002a5d5c51b};
const NimBLEUUID VENDOR_CHARACTERISTIC {0x054ac622, 0x3214, 0x11e6, 0x00010002a5d5c51b};
const NimBLEUUID MODEL_CHARACTERISTIC {0x054ac623, 0x3214, 0x11e6, 0x00010002a5d5c51b};
const NimBLEUUID CONTROL_CHARACTERISTIC {0x7be5fd56, 0x475b, 0x11e7, 0xa91992ebcb67fe33};
const NimBLEUUID CONTROL_READINESS_CHARACTERISTIC {0x7be5fe6e, 0x475b, 0x11e7, 0xa91992ebcb67fe33};
const NimBLEUUID GPS_CHARACTERISTIC {0xdaff1bc0, 0x3216, 0x11e6, 0x91c80002a5d5c51b};
const NimBLEUUID LOCATION_READINESS_CHARACTERISTIC {0x37701b80, 0x32a7, 0x11e6, 0xbb830002a5d5c51b};
const NimBLEUUID CLOCK_CHARACTERISTIC {0xead55e60, 0x3216, 0x11e6, 0xa42e0002a5d5c51b};
const NimBLEUUID CLOCK_REQUEST_CHARACTERISTIC {0x4cf487c0, 0x32a7, 0x11e6, 0xa50d0002a5d5c51b};
const NimBLEUUID CONNECT_TYPE_CHARACTERISTIC {0x0d6f1880, 0x3217, 0x11e6, 0xa4cb0002a5d5c51b};
const NimBLEUUID SSID_CHARACTERISTIC {0x18345be0, 0x3217, 0x11e6, 0xb56c0002a5d5c51b};
const NimBLEUUID SSID_ACK_CHARACTERISTIC {0x18345be1, 0x3217, 0x11e6, 0xb56c0002a5d5c51b};
const NimBLEUUID STATE_CHARACTERISTIC {0x48669da0, 0x3217, 0x11e6, 0xb08d0002a5d5c51b};
const NimBLEUUID UNKNOWN_STATE_CHARACTERISTIC {0x96c82c33, 0xe3b4, 0x4e80, 0x932715364827d281};

// A timezone offset a real body can report. The App and G9II layouts overlap
// when byte 7 is zero, so a value no timezone uses cannot decide the layout.
bool plausibleClockOffset(int16_t offset) {
  return (offset >= -12 * 60) && (offset <= 14 * 60) && ((offset % 15) == 0);
}

constexpr std::array<uint8_t, 10> MEI0_PREFIX = {
    0x4d, 0x45, 0x49, 0x30, 0x01, 0x00, 0x10, 0x00, 0x80, 0x01,
};

constexpr uint8_t ATT_APPLICATION_ERROR = 0x80;
constexpr uint8_t ATT_INSUFFICIENT_AUTHENTICATION = 0x05;
constexpr uint8_t ATT_INVALID_VALUE = 0x13;
constexpr uint8_t ATT_VALUE_NOT_ALLOWED = 0x85;

constexpr uint8_t CONTROL_CONFIRM = 0x01;
constexpr uint8_t CONTROL_START = 0x02;
constexpr uint8_t CONTROL_FINISH = 0x03;
constexpr uint8_t CONTROL_SHUTTER_ON = 0x04;
constexpr uint8_t CONTROL_SHUTTER_OFF = 0x05;
constexpr uint8_t CONTROL_REC_ON = 0x06;
constexpr uint8_t CONTROL_REC_OFF = 0x07;

constexpr uint8_t GPS_FIX_VALID = 0x41;
constexpr uint8_t GPS_FIX_STOP = 0x56;

}  // namespace

LumixVirtualCamera::LumixVirtualCamera() : LumixVirtualCamera(Config {}) {}

LumixVirtualCamera::LumixVirtualCamera(const Config &config) : m_Config(config) {
  const std::array<uint8_t, 4> zero = {0x00, 0x00, 0x00, 0x00};
  if (m_Config.aps_key_reads_zero || (m_Config.aps_key == zero)) {
    m_EffectiveApsKey = {0x90, 0x32, 0x78, 0x2c};
  } else {
    m_EffectiveApsKey = m_Config.aps_key;
  }
}

LumixVirtualCamera::~LumixVirtualCamera() = default;

bool LumixVirtualCamera::matches(const NimBLEUUID &left, const NimBLEUUID &right) {
  return left == right;
}

std::string LumixVirtualCamera::key(const NimBLEUUID &service, const NimBLEUUID &characteristic) {
  return service.toString() + "/" + characteristic.toString();
}

uint16_t LumixVirtualCamera::readLE16(const uint8_t *value) {
  return static_cast<uint16_t>(value[0]) | (static_cast<uint16_t>(value[1]) << 8);
}

uint32_t LumixVirtualCamera::readLE32(const uint8_t *value) {
  return static_cast<uint32_t>(value[0]) | (static_cast<uint32_t>(value[1]) << 8)
         | (static_cast<uint32_t>(value[2]) << 16) | (static_cast<uint32_t>(value[3]) << 24);
}

int16_t LumixVirtualCamera::readLEI16(const uint8_t *value) {
  return static_cast<int16_t>(readLE16(value));
}

bool LumixVirtualCamera::validClockFields(uint8_t month,
                                          uint8_t day,
                                          uint8_t hour,
                                          uint8_t minute,
                                          uint8_t second) {
  return (month >= 1) && (month <= 12) && (day >= 1) && (day <= 31) && (hour <= 23)
         && (minute <= 59) && (second <= 59);
}

bool LumixVirtualCamera::decodeApsBlock(const std::vector<uint8_t> &encoded,
                                        const std::array<uint8_t, 4> &key,
                                        std::vector<uint8_t> &decoded) {
  if (encoded.size() != 20) {
    return false;
  }
  decoded.assign(20, 0x00);
  for (size_t word = 0; word < 5; word++) {
    // Wire form is XOR then a four-byte word swap. Decode is the word swap
    // back, then the same key XOR. The app stores the key as a long; applying
    // its four bytes in the order the camera returned them is the documented
    // reading, but a nonzero-key capture is still needed to pin the byte order
    // (spec U-30). A zero key reduces this to the word swap, which the G9II
    // public constant confirms.
    const size_t base = word * 4;
    decoded[base + 0] = encoded[base + 3] ^ key[0];
    decoded[base + 1] = encoded[base + 2] ^ key[1];
    decoded[base + 2] = encoded[base + 1] ^ key[2];
    decoded[base + 3] = encoded[base + 0] ^ key[3];
  }
  return true;
}

bool LumixVirtualCamera::printableAscii(const std::vector<uint8_t> &value) {
  return std::all_of(value.begin(), value.end(),
                     [](uint8_t byte) { return (byte >= 0x20) && (byte <= 0x7e); });
}

const char *LumixVirtualCamera::commandName(Command command) {
  switch (command) {
    case Command::Mei0Identity:
      return "MEI0 identity";
    case Command::ControllerName:
      return "controller name";
    case Command::ApsIdentity:
      return "APS identity";
    case Command::ApsName:
      return "APS name";
    case Command::ControlConfirm:
      return "control confirm";
    case Command::ControlStart:
      return "control start";
    case Command::ControlFinish:
      return "control finish";
    case Command::ControlShutterOn:
      return "control shutter on";
    case Command::ControlShutterOff:
      return "control shutter off";
    case Command::ControlRecOn:
      return "control record on";
    case Command::ControlRecOff:
      return "control record off";
    case Command::GpsPush:
      return "GPS push";
    case Command::GpsStop:
      return "GPS stop";
    case Command::ClockWrite:
      return "clock write";
    case Command::SsidWrite:
      return "SSID write";
    case Command::ConnectType:
      return "Wi-Fi connect type";
    case Command::WifiStart:
      return "Wi-Fi start";
    case Command::Unknown:
    default:
      return "unknown";
  }
}

NimBLEAdvertisedDevice LumixVirtualCamera::advertisement() const {
  // The production matcher requires company 0x003a and the session service.
  // advertise_session_service can omit the service to model a body whose
  // advertisement does not carry it. The flags byte and five-byte address
  // fragment are parsed but not used by Furble; their semantics remain
  // unknown (spec U-02 and U-03).
  std::vector<uint8_t> manufacturer = {0x3a, 0x00, m_Config.advertisement_flags};
  manufacturer.insert(manufacturer.end(), m_Config.address_fragment.begin(),
                      m_Config.address_fragment.end());

  NimBLEAdvertisedDevice device;
  device.setAddress(m_Config.address);
  device.setName(m_Config.name);
  device.setManufacturerData(manufacturer.data(), manufacturer.size());
  device.setRSSI(-58);
  if (m_Config.advertise_session_service) {
    device.addServiceUUID(SESSION_SERVICE);
  }
  return device;
}

const LumixVirtualCamera::Config &LumixVirtualCamera::config() const {
  return m_Config;
}

bool LumixVirtualCamera::advertisesSessionService() const {
  return m_Config.advertise_session_service;
}

bool LumixVirtualCamera::apsPresent() const {
  return m_Config.aps_supported;
}

bool LumixVirtualCamera::cameraBonded() const {
  // No LE bond exists on the MEI0 or APS path.
  return false;
}

bool LumixVirtualCamera::staleBond() const {
  // There is no bond to become stale. A deleted camera-side registration is
  // represented by registered() and identityPath().
  return false;
}

bool LumixVirtualCamera::bondSupported() const {
  return false;
}

bool LumixVirtualCamera::registered() const {
  return m_Registered;
}

bool LumixVirtualCamera::configured() const {
  return m_Registered;
}

bool LumixVirtualCamera::secured() const {
  return m_SecureAccepted;
}

LumixVirtualCamera::IdentityPath LumixVirtualCamera::identityPath() const {
  return m_IdentityPath;
}

const std::vector<uint8_t> &LumixVirtualCamera::mei0Identity() const {
  return m_Mei0Identity;
}

const std::vector<uint8_t> &LumixVirtualCamera::apsIdentity() const {
  return m_ApsIdentity;
}

const std::string &LumixVirtualCamera::controllerName() const {
  return m_ControllerName;
}

const std::string &LumixVirtualCamera::writtenSsid() const {
  return m_WrittenSsid;
}

uint8_t LumixVirtualCamera::connectType() const {
  return m_ConnectType;
}

bool LumixVirtualCamera::wrongIdentityPath() const {
  return m_WrongIdentityPath;
}

void LumixVirtualCamera::removeRegistration() {
  m_Registered = false;
  m_IdentityPath = IdentityPath::None;
  m_Mei0Identity.clear();
  m_ApsIdentity.clear();
  m_ControllerName.clear();
  m_Confirmed = false;
  m_SessionStarted = false;
  m_SessionFinished = false;
  m_StaleSession = false;
  m_ShutterHeld = false;
  m_Recording = false;
}

void LumixVirtualCamera::removeCameraBond() {
  // LUMIX creates no LE bond, so there is no bond key to delete. The camera
  // menu registration list is the equivalent surface, and removeRegistration()
  // models that application record.
  removeRegistration();
}

bool LumixVirtualCamera::confirmed() const {
  return m_Confirmed;
}

bool LumixVirtualCamera::sessionStarted() const {
  return m_SessionStarted;
}

bool LumixVirtualCamera::sessionFinished() const {
  return m_SessionFinished;
}

bool LumixVirtualCamera::staleSession() const {
  return m_StaleSession;
}

bool LumixVirtualCamera::shutterHeld() const {
  return m_ShutterHeld;
}

bool LumixVirtualCamera::recording() const {
  return m_Recording;
}

uint8_t LumixVirtualCamera::lastControlCommand() const {
  return m_LastControlCommand;
}

bool LumixVirtualCamera::locationReady() const {
  return m_LocationReady;
}

bool LumixVirtualCamera::clockReady() const {
  return m_ClockReady;
}

bool LumixVirtualCamera::gpsStopped() const {
  return m_GpsStopped;
}

const std::vector<uint8_t> &LumixVirtualCamera::lastGps() const {
  return m_LastGps;
}

const std::vector<uint8_t> &LumixVirtualCamera::lastClock() const {
  return m_LastClock;
}

int16_t LumixVirtualCamera::clockOffsetMinutes() const {
  return m_ClockOffsetMinutes;
}

uint8_t LumixVirtualCamera::clockDst() const {
  return m_ClockDst;
}

LumixVirtualCamera::ClockLayout LumixVirtualCamera::lastClockLayout() const {
  return m_LastClockLayout;
}

uint8_t LumixVirtualCamera::lastAttError() const {
  return m_LastAttError;
}

bool LumixVirtualCamera::secureRequested() const {
  return m_SecureRequested;
}

LumixVirtualCamera::Fault LumixVirtualCamera::lastFault() const {
  return m_LastFault;
}

bool LumixVirtualCamera::secureAccepted() const {
  return m_SecureAccepted;
}

bool LumixVirtualCamera::identityChanged() const {
  return m_IdentityChanged;
}

const std::vector<LumixVirtualCamera::Write> &LumixVirtualCamera::writes() const {
  return m_Writes;
}

const std::vector<LumixVirtualCamera::Notification> &LumixVirtualCamera::notifications() const {
  return m_Notifications;
}

void LumixVirtualCamera::clearEvents() {
  m_Writes.clear();
  m_Notifications.clear();
}

bool LumixVirtualCamera::connected() const {
  return m_Connected;
}

void LumixVirtualCamera::setScript(const std::vector<ScriptStep> &steps) {
  m_Script.assign(steps.begin(), steps.end());
  m_Scripted = true;
}

void LumixVirtualCamera::appendScriptStep(const ScriptStep &step) {
  m_Script.push_back(step);
  m_Scripted = true;
}

void LumixVirtualCamera::clearScript() {
  m_Script.clear();
  m_Scripted = false;
}

size_t LumixVirtualCamera::scriptRemaining() const {
  return m_Script.size();
}

void LumixVirtualCamera::setScripted(bool enabled) {
  m_Scripted = enabled;
}

bool LumixVirtualCamera::scripted() const {
  return m_Scripted;
}

void LumixVirtualCamera::scriptNextCommand(Fault fault) {
  m_Script.push_back({Command::Unknown, fault});
  m_Scripted = true;
}

size_t LumixVirtualCamera::scriptedRemaining() const {
  return m_Script.size();
}

void LumixVirtualCamera::setNextFault(Fault fault) {
  m_NextFault = fault;
}

LumixVirtualCamera::Fault LumixVirtualCamera::pendingFault() const {
  return m_NextFault;
}

void LumixVirtualCamera::clearFault() {
  m_NextFault = Fault::None;
}

void LumixVirtualCamera::clearFaults() {
  m_NextFault = Fault::None;
  m_Script.clear();
  m_Scripted = false;
}

void LumixVirtualCamera::setStaleSession(bool stale) {
  m_StaleSession = stale;
  if (stale) {
    m_SessionStarted = true;
    m_SessionFinished = false;
  }
}

LumixVirtualCamera::Fault LumixVirtualCamera::takeFault(Command command) {
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

void LumixVirtualCamera::noteAttError(uint8_t error) {
  m_LastAttError = error;
}

void LumixVirtualCamera::recordWrite(const NimBLEUUID &service,
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

bool LumixVirtualCamera::emitNotification(const NimBLEUUID &service,
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

bool LumixVirtualCamera::emitLocationReadiness(uint8_t value) {
  if (value == 0x01) {
    m_LocationReady = true;
  } else if (value == 0x02) {
    m_LocationReady = false;
  }
  return emitNotification(LOCATION_SERVICE, LOCATION_READINESS_CHARACTERISTIC, {value});
}

bool LumixVirtualCamera::emitClockRequest(uint8_t value) {
  if (value == 0x01) {
    m_ClockReady = true;
  }
  return emitNotification(CLOCK_SERVICE, CLOCK_REQUEST_CHARACTERISTIC, {value});
}

bool LumixVirtualCamera::emitControlReadiness(uint8_t value) {
  return emitNotification(CONTROL_SERVICE, CONTROL_READINESS_CHARACTERISTIC, {value});
}

bool LumixVirtualCamera::emitSsidAcknowledgement(uint8_t value) {
  return emitNotification(WIFI_SERVICE, SSID_ACK_CHARACTERISTIC, {value});
}

void LumixVirtualCamera::resetSession() {
  m_Subscriptions.clear();
  m_LocationReady = false;
  m_ClockReady = false;
}

bool LumixVirtualCamera::acceptConnection(NimBLEClient &client, const NimBLEAddress &address) {
  if (m_Connected || (address != m_Config.address)) {
    return false;
  }
  m_Client = &client;
  m_Connected = true;
  m_SecureAccepted = false;
  m_LastAttError = 0;
  resetSession();
  return true;
}

void LumixVirtualCamera::disconnect(NimBLEClient &client, int reason) {
  (void)reason;
  if (m_Client != &client) {
    return;
  }
  if (m_SessionStarted && !m_SessionFinished) {
    // The app sends Finish on exit. An abrupt drop leaves the camera in the
    // started state, so the next session must Confirm again (inference from
    // the vendor flow, spec 7.6).
    m_StaleSession = true;
  }
  m_Client = nullptr;
  m_Connected = false;
  m_SecureAccepted = false;
  m_Subscriptions.clear();
  if (m_Config.release_on_disconnect) {
    m_Confirmed = false;
    m_SessionStarted = false;
    m_SessionFinished = false;
    m_StaleSession = false;
    m_ShutterHeld = false;
    m_Recording = false;
  }
}

bool LumixVirtualCamera::hasService(const NimBLEUUID &service) const {
  return matches(service, SESSION_SERVICE)
         || (m_Config.info_service_present && matches(service, INFO_SERVICE))
         || (m_Config.control_service_present && matches(service, CONTROL_SERVICE))
         || (m_Config.location_service_present && matches(service, LOCATION_SERVICE))
         || (m_Config.clock_service_present && matches(service, CLOCK_SERVICE))
         || (m_Config.wifi_service_present && matches(service, WIFI_SERVICE))
         || (m_Config.state_service_present && matches(service, STATE_SERVICE));
}

bool LumixVirtualCamera::hasCharacteristic(const NimBLEUUID &service,
                                           const NimBLEUUID &characteristic) const {
  if (matches(service, SESSION_SERVICE)) {
    if (matches(characteristic, MEI0_CHARACTERISTIC)
        || matches(characteristic, DEVICE_NAME_CHARACTERISTIC)) {
      return true;
    }
    if (m_Config.aps_supported
        && (matches(characteristic, APS_KEY_CHARACTERISTIC)
            || matches(characteristic, APS_IDENTITY_CHARACTERISTIC)
            || matches(characteristic, APS_NAME_CHARACTERISTIC))) {
      return true;
    }
    return matches(characteristic, WIFI_START_CHARACTERISTIC)
           || matches(characteristic, WIFI_ACK1_CHARACTERISTIC)
           || matches(characteristic, WIFI_ACK2_CHARACTERISTIC)
           || matches(characteristic, WIFI_ACK3_CHARACTERISTIC)
           || matches(characteristic, SSID_READ_CHARACTERISTIC)
           || matches(characteristic, CAMERA_NAME_CHARACTERISTIC)
           || matches(characteristic, AP_PASSWORD_CHARACTERISTIC)
           || matches(characteristic, CAMERA_INFO_CHARACTERISTIC);
  }
  if (matches(service, INFO_SERVICE) && m_Config.info_service_present) {
    return matches(characteristic, CAPABILITY_CHARACTERISTIC)
           || matches(characteristic, VENDOR_CHARACTERISTIC)
           || matches(characteristic, MODEL_CHARACTERISTIC);
  }
  if (matches(service, CONTROL_SERVICE) && m_Config.control_service_present) {
    return matches(characteristic, CONTROL_CHARACTERISTIC)
           || matches(characteristic, CONTROL_READINESS_CHARACTERISTIC);
  }
  if (matches(service, LOCATION_SERVICE) && m_Config.location_service_present) {
    return matches(characteristic, GPS_CHARACTERISTIC)
           || matches(characteristic, LOCATION_READINESS_CHARACTERISTIC);
  }
  if (matches(service, CLOCK_SERVICE) && m_Config.clock_service_present) {
    return matches(characteristic, CLOCK_CHARACTERISTIC)
           || matches(characteristic, CLOCK_REQUEST_CHARACTERISTIC);
  }
  if (matches(service, WIFI_SERVICE) && m_Config.wifi_service_present) {
    return matches(characteristic, CONNECT_TYPE_CHARACTERISTIC)
           || matches(characteristic, SSID_CHARACTERISTIC)
           || matches(characteristic, SSID_ACK_CHARACTERISTIC);
  }
  if (matches(service, STATE_SERVICE) && m_Config.state_service_present) {
    return matches(characteristic, STATE_CHARACTERISTIC)
           || matches(characteristic, UNKNOWN_STATE_CHARACTERISTIC);
  }
  return false;
}

bool LumixVirtualCamera::discoverCharacteristic(NimBLEClient &client,
                                                const NimBLEUUID &service,
                                                const NimBLEUUID &characteristic) {
  (void)client;
  return hasCharacteristic(service, characteristic);
}

bool LumixVirtualCamera::canWrite(const NimBLEUUID &service,
                                  const NimBLEUUID &characteristic) const {
  if (matches(service, SESSION_SERVICE)) {
    return matches(characteristic, MEI0_CHARACTERISTIC)
           || matches(characteristic, DEVICE_NAME_CHARACTERISTIC)
           || matches(characteristic, WIFI_START_CHARACTERISTIC)
           || (m_Config.aps_supported
               && (matches(characteristic, APS_IDENTITY_CHARACTERISTIC)
                   || matches(characteristic, APS_NAME_CHARACTERISTIC)));
  }
  if (matches(service, CONTROL_SERVICE) && m_Config.control_service_present) {
    return matches(characteristic, CONTROL_CHARACTERISTIC);
  }
  if (matches(service, LOCATION_SERVICE) && m_Config.location_service_present) {
    return matches(characteristic, GPS_CHARACTERISTIC);
  }
  if (matches(service, CLOCK_SERVICE) && m_Config.clock_service_present) {
    return matches(characteristic, CLOCK_CHARACTERISTIC);
  }
  if (matches(service, WIFI_SERVICE) && m_Config.wifi_service_present) {
    return matches(characteristic, CONNECT_TYPE_CHARACTERISTIC)
           || matches(characteristic, SSID_CHARACTERISTIC);
  }
  return false;
}

bool LumixVirtualCamera::handleMei0(const std::vector<uint8_t> &value) {
  if (value.size() != 16) {
    noteAttError(ATT_VALUE_NOT_ALLOWED);
    return false;
  }
  for (size_t i = 0; i < MEI0_PREFIX.size(); i++) {
    if (value[i] != MEI0_PREFIX[i]) {
      noteAttError(ATT_VALUE_NOT_ALLOWED);
      return false;
    }
  }
  if (m_Config.require_known_seed) {
    for (size_t i = 0; i < m_Config.mei0_seed.size(); i++) {
      if (value[10 + i] != m_Config.mei0_seed[i]) {
        noteAttError(ATT_VALUE_NOT_ALLOWED);
        return false;
      }
    }
  }
  if (m_Config.aps_supported && !m_Config.accept_plain_when_aps) {
    // LUMIX Sync selects APS whenever bb5d2a8c is present. The plain path is
    // the wrong generation for this body.
    m_WrongIdentityPath = true;
    noteAttError(ATT_APPLICATION_ERROR);
    return false;
  }

  m_IdentityChanged = !m_Mei0Identity.empty() && (m_Mei0Identity != value);
  m_Mei0Identity = value;
  m_IdentityPath = IdentityPath::Mei0;
  m_Registered = true;
  return true;
}

bool LumixVirtualCamera::handleApsIdentity(const std::vector<uint8_t> &value) {
  std::vector<uint8_t> decoded;
  if (!decodeApsBlock(value, m_EffectiveApsKey, decoded)) {
    noteAttError(ATT_VALUE_NOT_ALLOWED);
    return false;
  }
  if ((decoded[0] != 0x10) || (decoded[17] != 0xff) || (decoded[18] != 0xff)
      || (decoded[19] != 0xff)) {
    noteAttError(ATT_VALUE_NOT_ALLOWED);
    return false;
  }
  for (size_t i = 0; i < MEI0_PREFIX.size(); i++) {
    if (decoded[1 + i] != MEI0_PREFIX[i]) {
      noteAttError(ATT_VALUE_NOT_ALLOWED);
      return false;
    }
  }

  m_Mei0Identity.assign(decoded.begin() + 1, decoded.begin() + 17);
  m_ApsIdentity = decoded;
  m_IdentityPath = IdentityPath::Aps;
  m_Registered = true;
  return true;
}

bool LumixVirtualCamera::handleApsName(const std::vector<uint8_t> &value) {
  std::vector<uint8_t> decoded;
  if (!decodeApsBlock(value, m_EffectiveApsKey, decoded)) {
    noteAttError(ATT_VALUE_NOT_ALLOWED);
    return false;
  }
  const uint8_t length = decoded[0];
  if (length > 16) {
    noteAttError(ATT_VALUE_NOT_ALLOWED);
    return false;
  }
  for (size_t i = 0; i < 17; i++) {
    const uint8_t expected = (i == 0) ? length : ((i <= length) ? decoded[i] : 0x00);
    if (decoded[i] != expected) {
      noteAttError(ATT_VALUE_NOT_ALLOWED);
      return false;
    }
  }
  if ((decoded[17] != 0xff) || (decoded[18] != 0xff) || (decoded[19] != 0xff)) {
    noteAttError(ATT_VALUE_NOT_ALLOWED);
    return false;
  }
  std::vector<uint8_t> name(decoded.begin() + 1, decoded.begin() + 1 + length);
  if (!printableAscii(name)) {
    noteAttError(ATT_VALUE_NOT_ALLOWED);
    return false;
  }
  m_ControllerName.assign(name.begin(), name.end());
  return true;
}

bool LumixVirtualCamera::handleControl(const std::vector<uint8_t> &value) {
  if (value.size() != 1) {
    noteAttError(ATT_VALUE_NOT_ALLOWED);
    return false;
  }
  const uint8_t command = value[0];
  m_LastControlCommand = command;

  if (m_Config.busy) {
    noteAttError(ATT_APPLICATION_ERROR);
    return false;
  }

  switch (command) {
    case CONTROL_CONFIRM:
      m_Confirmed = true;
      m_StaleSession = false;
      m_SessionFinished = false;
      m_SessionStarted = false;
      m_ShutterHeld = false;
      m_Recording = false;
      // A body can expose the gate characteristic but never open it; Start is
      // then refused by the client.
      if (m_Config.control_readiness) {
        emitControlReadiness(0x00);
      }
      return true;
    case CONTROL_START:
      if (m_StaleSession) {
        // A prior session ended without Finish. Confirm is the documented
        // recovery step, so Start is refused until then.
        noteAttError(ATT_APPLICATION_ERROR);
        return false;
      }
      if (m_Config.require_confirm && !m_Confirmed && !m_Config.allow_start_without_confirm) {
        noteAttError(ATT_APPLICATION_ERROR);
        return false;
      }
      m_SessionStarted = true;
      m_SessionFinished = false;
      return true;
    case CONTROL_FINISH:
      if (!m_SessionStarted && !m_Config.allow_finish_without_start) {
        noteAttError(ATT_APPLICATION_ERROR);
        return false;
      }
      m_SessionStarted = false;
      m_SessionFinished = true;
      m_Confirmed = false;
      m_StaleSession = false;
      m_ShutterHeld = false;
      m_Recording = false;
      return true;
    case CONTROL_SHUTTER_ON:
      if (!m_SessionStarted && !m_Config.allow_shutter_without_start) {
        noteAttError(ATT_APPLICATION_ERROR);
        return false;
      }
      if (m_ShutterHeld) {
        noteAttError(ATT_APPLICATION_ERROR);
        return false;
      }
      m_ShutterHeld = true;
      return true;
    case CONTROL_SHUTTER_OFF:
      if (!m_ShutterHeld) {
        noteAttError(ATT_APPLICATION_ERROR);
        return false;
      }
      m_ShutterHeld = false;
      return true;
    case CONTROL_REC_ON:
      if (!m_SessionStarted) {
        noteAttError(ATT_APPLICATION_ERROR);
        return false;
      }
      if (m_Config.movie_semantics == MovieSemantics::Toggle) {
        m_Recording = !m_Recording;
      } else {
        m_Recording = true;
      }
      return true;
    case CONTROL_REC_OFF:
      if (!m_SessionStarted) {
        noteAttError(ATT_APPLICATION_ERROR);
        return false;
      }
      m_Recording = false;
      return true;
    default:
      // 00 and 08 to FF are undefined. 01 and 02 are Confirm and Start, not
      // focus press and release, so a focus call reaches this default.
      noteAttError(ATT_VALUE_NOT_ALLOWED);
      return false;
  }
}

bool LumixVirtualCamera::handleGps(const std::vector<uint8_t> &value) {
  if (value.size() != 16) {
    noteAttError(ATT_VALUE_NOT_ALLOWED);
    return false;
  }
  const uint8_t fix = value[14];
  if (fix == GPS_FIX_STOP) {
    if ((readLE32(value.data() + 4) != 0x7fffffffU) || (readLE32(value.data() + 8) != 0x7fffffffU)
        || (readLE16(value.data() + 12) != 0x7fff)) {
      noteAttError(ATT_VALUE_NOT_ALLOWED);
      return false;
    }
    m_LastGps = value;
    m_GpsStopped = true;
    return true;
  }
  if (fix != GPS_FIX_VALID) {
    noteAttError(ATT_VALUE_NOT_ALLOWED);
    return false;
  }
  if (m_Config.gps_requires_readiness && !m_LocationReady) {
    noteAttError(ATT_APPLICATION_ERROR);
    return false;
  }
  m_LastGps = value;
  m_GpsStopped = false;
  return true;
}

bool LumixVirtualCamera::handleClock(const std::vector<uint8_t> &value) {
  if (value.size() != 10) {
    noteAttError(ATT_VALUE_NOT_ALLOWED);
    return false;
  }
  if (m_Config.clock_requires_readiness && !m_ClockReady) {
    noteAttError(ATT_APPLICATION_ERROR);
    return false;
  }
  if (!validClockFields(value[2], value[3], value[4], value[5], value[6])) {
    noteAttError(ATT_VALUE_NOT_ALLOWED);
    return false;
  }

  ClockLayout layout = m_Config.clock_layout;
  bool parsed = false;
  int16_t offset = 0;
  uint8_t dst = 0;
  if (layout == ClockLayout::G9ii) {
    if (value[7] == 0x00) {
      const int16_t candidate = readLEI16(value.data() + 8);
      if (plausibleClockOffset(candidate)) {
        offset = candidate;
        dst = 0;
        parsed = true;
      }
    }
  } else {
    const int16_t candidate = readLEI16(value.data() + 7);
    dst = value[9];
    if ((dst <= 1) && plausibleClockOffset(candidate)) {
      offset = candidate;
      parsed = true;
    }
  }
  if (!parsed && m_Config.accept_app_clock_layout) {
    if (layout == ClockLayout::G9ii) {
      const int16_t candidate = readLEI16(value.data() + 7);
      dst = value[9];
      if ((dst <= 1) && plausibleClockOffset(candidate)) {
        offset = candidate;
        layout = ClockLayout::App;
        parsed = true;
      }
    } else if (value[7] == 0x00) {
      const int16_t candidate = readLEI16(value.data() + 8);
      if (plausibleClockOffset(candidate)) {
        offset = candidate;
        dst = 0;
        layout = ClockLayout::G9ii;
        parsed = true;
      }
    }
  }
  if (!parsed) {
    noteAttError(ATT_VALUE_NOT_ALLOWED);
    return false;
  }

  m_LastClock = value;
  m_ClockOffsetMinutes = offset;
  m_ClockDst = dst;
  m_LastClockLayout = layout;
  return true;
}

bool LumixVirtualCamera::handleWifi(const NimBLEUUID &characteristic,
                                    const std::vector<uint8_t> &value) {
  if (matches(characteristic, SSID_CHARACTERISTIC)) {
    if (value.size() != 32) {
      noteAttError(ATT_VALUE_NOT_ALLOWED);
      return false;
    }
    m_WrittenSsid.assign(value.begin(),
                         std::find(value.begin(), value.end(), static_cast<uint8_t>(0x00)));
    return true;
  }
  if (matches(characteristic, CONNECT_TYPE_CHARACTERISTIC)) {
    if (value.size() != 1) {
      noteAttError(ATT_VALUE_NOT_ALLOWED);
      return false;
    }
    if ((value[0] != 0x01) && (value[0] != 0x02) && (value[0] != 0x03)) {
      noteAttError(ATT_VALUE_NOT_ALLOWED);
      return false;
    }
    m_ConnectType = value[0];
    if (value[0] == 0x03) {
      emitSsidAcknowledgement(0x00);
    }
    return true;
  }
  if (matches(characteristic, WIFI_START_CHARACTERISTIC)) {
    if (value.size() != 1) {
      noteAttError(ATT_VALUE_NOT_ALLOWED);
      return false;
    }
    // The purpose byte value set was not captured (spec U-11). The write is
    // recorded but the peer does not assign a meaning to the value.
    return true;
  }
  noteAttError(ATT_INVALID_VALUE);
  return false;
}

bool LumixVirtualCamera::write(NimBLEClient &client,
                               const NimBLEUUID &service,
                               const NimBLEUUID &characteristic,
                               const std::vector<uint8_t> &value,
                               bool response) {
  if (!m_Connected || (m_Client != &client) || !canWrite(service, characteristic)) {
    return false;
  }

  Command command = Command::Unknown;
  if (matches(service, SESSION_SERVICE)) {
    if (matches(characteristic, MEI0_CHARACTERISTIC)) {
      command = Command::Mei0Identity;
    } else if (matches(characteristic, DEVICE_NAME_CHARACTERISTIC)) {
      command = Command::ControllerName;
    } else if (matches(characteristic, APS_IDENTITY_CHARACTERISTIC)) {
      command = Command::ApsIdentity;
    } else if (matches(characteristic, APS_NAME_CHARACTERISTIC)) {
      command = Command::ApsName;
    } else if (matches(characteristic, WIFI_START_CHARACTERISTIC)) {
      command = Command::WifiStart;
    }
  } else if (matches(service, CONTROL_SERVICE)) {
    if (value.size() == 1) {
      switch (value[0]) {
        case CONTROL_CONFIRM:
          command = Command::ControlConfirm;
          break;
        case CONTROL_START:
          command = Command::ControlStart;
          break;
        case CONTROL_FINISH:
          command = Command::ControlFinish;
          break;
        case CONTROL_SHUTTER_ON:
          command = Command::ControlShutterOn;
          break;
        case CONTROL_SHUTTER_OFF:
          command = Command::ControlShutterOff;
          break;
        case CONTROL_REC_ON:
          command = Command::ControlRecOn;
          break;
        case CONTROL_REC_OFF:
          command = Command::ControlRecOff;
          break;
        default:
          break;
      }
    }
  } else if (matches(service, LOCATION_SERVICE)) {
    command =
        (value.size() == 16 && value[14] == GPS_FIX_STOP) ? Command::GpsStop : Command::GpsPush;
  } else if (matches(service, CLOCK_SERVICE)) {
    command = Command::ClockWrite;
  } else if (matches(service, WIFI_SERVICE)) {
    if (matches(characteristic, SSID_CHARACTERISTIC)) {
      command = Command::SsidWrite;
    } else if (matches(characteristic, CONNECT_TYPE_CHARACTERISTIC)) {
      command = Command::ConnectType;
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
    // LUMIX has no LE bond. The closest equivalent is a camera-side
    // registration record that was deleted from the menu.
    removeRegistration();
    noteAttError(ATT_INSUFFICIENT_AUTHENTICATION);
    recordWrite(service, characteristic, command, value, response, false);
    return false;
  }
  if (fault == Fault::Timeout) {
    recordWrite(service, characteristic, command, value, response, true);
    return true;
  }

  bool accepted = false;
  if ((m_Config.require_registration_for_control && !m_Registered)
      && ((command == Command::ControlConfirm) || (command == Command::ControlStart)
          || (command == Command::ControlFinish) || (command == Command::ControlShutterOn)
          || (command == Command::ControlShutterOff) || (command == Command::ControlRecOn)
          || (command == Command::ControlRecOff) || (command == Command::GpsPush)
          || (command == Command::GpsStop) || (command == Command::ClockWrite))) {
    // The app writes the MEI0 or APS identity before the control session. A
    // body that requires that record refuses the later services until it is
    // present. This is the session order in the vendor flow, not a captured
    // error code, so the refusal is synthetic.
    noteAttError(ATT_APPLICATION_ERROR);
    recordWrite(service, characteristic, command, value, response, false);
    return false;
  }
  if (command == Command::Mei0Identity) {
    accepted = handleMei0(value);
  } else if (command == Command::ControllerName) {
    if (value.empty() || (value.size() > m_Config.max_plain_name_bytes) || !printableAscii(value)) {
      noteAttError(ATT_VALUE_NOT_ALLOWED);
    } else {
      m_ControllerName.assign(value.begin(), value.end());
      accepted = true;
    }
  } else if (command == Command::ApsIdentity) {
    if (!m_Config.aps_supported) {
      noteAttError(ATT_INVALID_VALUE);
    } else {
      accepted = handleApsIdentity(value);
    }
  } else if (command == Command::ApsName) {
    if (!m_Config.aps_supported) {
      noteAttError(ATT_INVALID_VALUE);
    } else {
      accepted = handleApsName(value);
    }
  } else if (command == Command::WifiStart) {
    accepted = handleWifi(characteristic, value);
  } else if ((command == Command::ControlConfirm) || (command == Command::ControlStart)
             || (command == Command::ControlFinish) || (command == Command::ControlShutterOn)
             || (command == Command::ControlShutterOff) || (command == Command::ControlRecOn)
             || (command == Command::ControlRecOff)) {
    accepted = handleControl(value);
  } else if ((command == Command::GpsPush) || (command == Command::GpsStop)) {
    accepted = handleGps(value);
  } else if (command == Command::ClockWrite) {
    accepted = handleClock(value);
  } else if ((command == Command::SsidWrite) || (command == Command::ConnectType)) {
    accepted = handleWifi(characteristic, value);
  } else {
    noteAttError(ATT_INVALID_VALUE);
  }

  if (accepted && (command == Command::Mei0Identity) && m_IdentityChanged
      && m_Config.drop_on_identity_change) {
    recordWrite(service, characteristic, command, value, response, accepted);
    client.mockDropLink(0x13, /*fire_callback=*/true);
    return false;
  }

  recordWrite(service, characteristic, command, value, response, accepted);

  if (fault == Fault::Malformed) {
    // A readiness notification with the wrong length, so a parser that trusts
    // the documented one-byte value rejects it.
    emitNotification(CONTROL_SERVICE, CONTROL_READINESS_CHARACTERISTIC,
                     {m_Confirmed ? static_cast<uint8_t>(0x00) : static_cast<uint8_t>(0x01), 0x00});
  }
  if (fault == Fault::Disconnect) {
    client.mockDropLink(0x13, /*fire_callback=*/true);
    return false;
  }
  return accepted;
}

NimBLEAttValue LumixVirtualCamera::read(NimBLEClient &client,
                                        const NimBLEUUID &service,
                                        const NimBLEUUID &characteristic) {
  if (!m_Connected || (m_Client != &client) || !hasCharacteristic(service, characteristic)) {
    return {};
  }

  if (matches(service, SESSION_SERVICE)) {
    if (matches(characteristic, APS_KEY_CHARACTERISTIC)) {
      if (m_Config.aps_key_reads_zero) {
        return NimBLEAttValue({0x00, 0x00, 0x00, 0x00});
      }
      return NimBLEAttValue(
          std::vector<uint8_t>(m_EffectiveApsKey.begin(), m_EffectiveApsKey.end()));
    }
    if (matches(characteristic, SSID_READ_CHARACTERISTIC)) {
      return NimBLEAttValue(m_Config.camera_ssid);
    }
    if (matches(characteristic, CAMERA_NAME_CHARACTERISTIC)) {
      return NimBLEAttValue(m_Config.camera_name);
    }
    if (matches(characteristic, AP_PASSWORD_CHARACTERISTIC)) {
      // The XOR obfuscation transform is not documented (spec U-13). The peer
      // returns the configured test fixture rather than inventing a decoder.
      return NimBLEAttValue(m_Config.ap_password);
    }
    return {};
  }
  if (matches(service, INFO_SERVICE)) {
    if (matches(characteristic, CAPABILITY_CHARACTERISTIC)) {
      return NimBLEAttValue(
          std::vector<uint8_t>(m_Config.capability.begin(), m_Config.capability.end()));
    }
    if (matches(characteristic, VENDOR_CHARACTERISTIC)) {
      return NimBLEAttValue(m_Config.vendor_name);
    }
    if (matches(characteristic, MODEL_CHARACTERISTIC)) {
      return NimBLEAttValue(m_Config.camera_model);
    }
    return {};
  }
  return {};
}

bool LumixVirtualCamera::subscribe(NimBLEClient &client,
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

  if (m_Config.readiness_on_subscribe) {
    if (matches(service, LOCATION_SERVICE)
        && matches(characteristic, LOCATION_READINESS_CHARACTERISTIC)) {
      emitLocationReadiness(0x01);
    } else if (matches(service, CLOCK_SERVICE)
               && matches(characteristic, CLOCK_REQUEST_CHARACTERISTIC)) {
      emitClockRequest(0x01);
    }
  }
  return true;
}

bool LumixVirtualCamera::secureConnection(NimBLEClient &client) {
  if (!m_Connected || (m_Client != &client)) {
    return false;
  }
  m_SecureRequested = true;
  if (m_Config.reject_secure_connection) {
    m_SecureAccepted = false;
    return false;
  }
  // No SMP, no bond, and no encryption change. The documented camera path
  // accepts the link, so the call is a no-op that reports success.
  m_SecureAccepted = true;
  return true;
}

bool LumixVirtualCamera::updateConnectionParams(NimBLEClient &client,
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

int LumixVirtualCamera::getRssi() const {
  return -58;
}

const NimBLEUUID &LumixVirtualCamera::sessionServiceUUID() {
  return SESSION_SERVICE;
}

const NimBLEUUID &LumixVirtualCamera::infoServiceUUID() {
  return INFO_SERVICE;
}

const NimBLEUUID &LumixVirtualCamera::controlServiceUUID() {
  return CONTROL_SERVICE;
}

const NimBLEUUID &LumixVirtualCamera::locationServiceUUID() {
  return LOCATION_SERVICE;
}

const NimBLEUUID &LumixVirtualCamera::clockServiceUUID() {
  return CLOCK_SERVICE;
}

const NimBLEUUID &LumixVirtualCamera::wifiServiceUUID() {
  return WIFI_SERVICE;
}

const NimBLEUUID &LumixVirtualCamera::stateServiceUUID() {
  return STATE_SERVICE;
}

const NimBLEUUID &LumixVirtualCamera::mei0CharacteristicUUID() {
  return MEI0_CHARACTERISTIC;
}

const NimBLEUUID &LumixVirtualCamera::deviceNameCharacteristicUUID() {
  return DEVICE_NAME_CHARACTERISTIC;
}

const NimBLEUUID &LumixVirtualCamera::apsKeyCharacteristicUUID() {
  return APS_KEY_CHARACTERISTIC;
}

const NimBLEUUID &LumixVirtualCamera::apsIdentityCharacteristicUUID() {
  return APS_IDENTITY_CHARACTERISTIC;
}

const NimBLEUUID &LumixVirtualCamera::apsNameCharacteristicUUID() {
  return APS_NAME_CHARACTERISTIC;
}

const NimBLEUUID &LumixVirtualCamera::controlCharacteristicUUID() {
  return CONTROL_CHARACTERISTIC;
}

const NimBLEUUID &LumixVirtualCamera::controlReadinessCharacteristicUUID() {
  return CONTROL_READINESS_CHARACTERISTIC;
}

const NimBLEUUID &LumixVirtualCamera::gpsCharacteristicUUID() {
  return GPS_CHARACTERISTIC;
}

const NimBLEUUID &LumixVirtualCamera::locationReadinessCharacteristicUUID() {
  return LOCATION_READINESS_CHARACTERISTIC;
}

const NimBLEUUID &LumixVirtualCamera::clockCharacteristicUUID() {
  return CLOCK_CHARACTERISTIC;
}

const NimBLEUUID &LumixVirtualCamera::clockRequestCharacteristicUUID() {
  return CLOCK_REQUEST_CHARACTERISTIC;
}

const NimBLEUUID &LumixVirtualCamera::ssidCharacteristicUUID() {
  return SSID_CHARACTERISTIC;
}

const NimBLEUUID &LumixVirtualCamera::connectTypeCharacteristicUUID() {
  return CONNECT_TYPE_CHARACTERISTIC;
}

const NimBLEUUID &LumixVirtualCamera::ssidAcknowledgementCharacteristicUUID() {
  return SSID_ACK_CHARACTERISTIC;
}

const NimBLEUUID &LumixVirtualCamera::wifiStartCharacteristicUUID() {
  return WIFI_START_CHARACTERISTIC;
}

const NimBLEUUID &LumixVirtualCamera::capabilityCharacteristicUUID() {
  return CAPABILITY_CHARACTERISTIC;
}

const NimBLEUUID &LumixVirtualCamera::vendorCharacteristicUUID() {
  return VENDOR_CHARACTERISTIC;
}

const NimBLEUUID &LumixVirtualCamera::modelCharacteristicUUID() {
  return MODEL_CHARACTERISTIC;
}

}  // namespace Host
}  // namespace Furble
