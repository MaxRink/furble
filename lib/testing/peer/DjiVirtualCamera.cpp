#include <algorithm>

#include "DjiVirtualCamera.h"
#include "WriteJournal.h"

namespace Furble {
namespace Host {

namespace {

const NimBLEUUID TARGET_SERVICE {(uint16_t)0xfff0};
const NimBLEUUID NOTIFY_CHARACTERISTIC {(uint16_t)0xfff4};
const NimBLEUUID WRITE_CHARACTERISTIC {(uint16_t)0xfff5};
const NimBLEUUID EXTERNAL_DATA_CHARACTERISTIC {(uint16_t)0xfff3};
const NimBLEUUID GATT_SERVICE {(uint16_t)0x1801};
const NimBLEUUID SERVICE_CHANGED_CHARACTERISTIC {(uint16_t)0x2a05};

constexpr uint8_t FRAME_SOF = 0xaa;
constexpr uint16_t FRAME_LENGTH_MASK = 0x03ff;
constexpr uint16_t FRAME_VERSION_MASK = 0xfc00;
constexpr uint8_t FRAME_RESPONSE_BIT = 0x20;
constexpr size_t FRAME_HEADER_LENGTH = 12;
constexpr size_t FRAME_COMMAND_LENGTH = 2;
constexpr size_t FRAME_TAIL_LENGTH = 4;
constexpr size_t FRAME_OVERHEAD = FRAME_HEADER_LENGTH + FRAME_COMMAND_LENGTH + FRAME_TAIL_LENGTH;

constexpr uint8_t CMD_SET_COMMON = 0x00;
constexpr uint8_t CMD_SET_CAMERA = 0x1d;
constexpr uint8_t CMD_VERSION = 0x00;
constexpr uint8_t CMD_KEY_REPORT = 0x11;
constexpr uint8_t CMD_RESTART = 0x16;
constexpr uint8_t CMD_GPS_PUSH = 0x17;
constexpr uint8_t CMD_CONNECTION = 0x19;
constexpr uint8_t CMD_POWER_MODE = 0x1a;
constexpr uint8_t CMD_RECORD = 0x03;
constexpr uint8_t CMD_STATUS_PUSH = 0x02;
constexpr uint8_t CMD_MODE_SWITCH = 0x04;
constexpr uint8_t CMD_STATUS_SUBSCRIPTION = 0x05;
constexpr uint8_t CMD_NEW_STATUS_PUSH = 0x06;

constexpr uint8_t ATT_APPLICATION_ERROR = 0x80;
constexpr uint8_t ATT_INSUFFICIENT_AUTHENTICATION = 0x05;
constexpr uint8_t ATT_INVALID_VALUE = 0x13;

constexpr uint8_t RETURN_SUCCESS = 0x00;
constexpr uint8_t RETURN_PARSE_ERROR = 0x01;
constexpr uint8_t RETURN_EXECUTION_FAILED = 0x02;

constexpr uint8_t POWER_NORMAL = 0x00;
constexpr uint8_t POWER_SLEEP = 0x03;

constexpr uint8_t KEY_RECORD = 0x01;
constexpr uint8_t KEY_QUICK_SWITCH = 0x02;
constexpr uint8_t KEY_SNAPSHOT = 0x03;

constexpr uint16_t GPS_PAYLOAD_BYTES = 48;
constexpr uint16_t CONNECTION_PAYLOAD_BYTES = 33;

}  // namespace

DjiVirtualCamera::DjiVirtualCamera() : DjiVirtualCamera(Config {}) {}

DjiVirtualCamera::DjiVirtualCamera(const Config &config) : m_Config(config) {
  m_Asleep = m_Config.asleep;
  m_CameraMode = m_Config.camera_mode;
}

DjiVirtualCamera::~DjiVirtualCamera() = default;

bool DjiVirtualCamera::matches(const NimBLEUUID &left, const NimBLEUUID &right) {
  return left == right;
}

std::string DjiVirtualCamera::key(const NimBLEUUID &service, const NimBLEUUID &characteristic) {
  return service.toString() + "/" + characteristic.toString();
}

uint16_t DjiVirtualCamera::readLE16(const uint8_t *data) {
  return static_cast<uint16_t>(data[0]) | (static_cast<uint16_t>(data[1]) << 8);
}

uint32_t DjiVirtualCamera::readLE32(const uint8_t *data) {
  return static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8)
         | (static_cast<uint32_t>(data[2]) << 16) | (static_cast<uint32_t>(data[3]) << 24);
}

void DjiVirtualCamera::writeLE16(uint8_t *data, uint16_t value) {
  data[0] = static_cast<uint8_t>(value & 0xff);
  data[1] = static_cast<uint8_t>((value >> 8) & 0xff);
}

void DjiVirtualCamera::writeLE32(uint8_t *data, uint32_t value) {
  data[0] = static_cast<uint8_t>(value & 0xff);
  data[1] = static_cast<uint8_t>((value >> 8) & 0xff);
  data[2] = static_cast<uint8_t>((value >> 16) & 0xff);
  data[3] = static_cast<uint8_t>((value >> 24) & 0xff);
}

uint16_t DjiVirtualCamera::crc16(const uint8_t *data, size_t length) {
  uint16_t crc = 0x3aa3;
  for (size_t i = 0; i < length; i++) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; bit++) {
      crc = (crc & 1U) != 0 ? static_cast<uint16_t>((crc >> 1) ^ 0xa001U)
                            : static_cast<uint16_t>(crc >> 1);
    }
  }
  return crc;
}

uint32_t DjiVirtualCamera::crc32(const uint8_t *data, size_t length) {
  uint32_t crc = 0x00003aa3;
  for (size_t i = 0; i < length; i++) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; bit++) {
      crc = (crc & 1U) != 0 ? (crc >> 1) ^ 0xedb88320U : crc >> 1;
    }
  }
  return crc;
}

std::vector<uint8_t> DjiVirtualCamera::buildFrame(uint8_t commandSet,
                                                  uint8_t commandId,
                                                  uint8_t commandType,
                                                  uint16_t sequence,
                                                  const std::vector<uint8_t> &payload) {
  const size_t length = FRAME_OVERHEAD + payload.size();
  if (length > 1023) {
    return {};
  }

  std::vector<uint8_t> frame(length, 0x00);
  frame[0] = FRAME_SOF;
  const uint16_t encodedLength = static_cast<uint16_t>(length & FRAME_LENGTH_MASK);
  frame[1] = static_cast<uint8_t>(encodedLength & 0xff);
  frame[2] = static_cast<uint8_t>((encodedLength >> 8) & 0xff);
  frame[3] = commandType;
  frame[4] = 0x00;  // ENC none
  frame[5] = 0x00;
  frame[6] = 0x00;
  frame[7] = 0x00;
  writeLE16(frame.data() + 8, sequence);
  writeLE16(frame.data() + 10, crc16(frame.data(), 10));
  frame[12] = commandSet;
  frame[13] = commandId;
  std::copy(payload.begin(), payload.end(), frame.begin() + 14);
  writeLE32(frame.data() + length - FRAME_TAIL_LENGTH,
            crc32(frame.data(), length - FRAME_TAIL_LENGTH));
  return frame;
}

bool DjiVirtualCamera::validFrame(const uint8_t *data, size_t length) {
  if ((data == nullptr) || (length < FRAME_OVERHEAD) || (data[0] != FRAME_SOF)) {
    return false;
  }
  const uint16_t encodedLength = readLE16(data + 1);
  if ((encodedLength & FRAME_VERSION_MASK) != 0) {
    // The version field has no documented nonzero value. Reject it rather
    // than guess how a camera would interpret it (spec U-40).
    return false;
  }
  if ((encodedLength & FRAME_LENGTH_MASK) != length) {
    return false;
  }
  if (readLE16(data + 10) != crc16(data, 10)) {
    return false;
  }
  return readLE32(data + length - FRAME_TAIL_LENGTH) == crc32(data, length - FRAME_TAIL_LENGTH);
}

bool DjiVirtualCamera::parseFrame(const uint8_t *data, size_t length, Frame &frame) {
  if (!validFrame(data, length)) {
    return false;
  }
  frame.command_type = data[3];
  frame.sequence = readLE16(data + 8);
  frame.command_set = data[12];
  frame.command_id = data[13];
  frame.response = (frame.command_type & FRAME_RESPONSE_BIT) != 0;
  frame.payload.assign(data + 14, data + length - FRAME_TAIL_LENGTH);
  return true;
}

bool DjiVirtualCamera::supportedDeviceId(uint32_t deviceId) {
  // Action 4 and Action 5 Pro in the three documented byte orders. The wire
  // truth is unresolved (spec U-01 and U-02), so the peer models all three
  // rather than treating one encoding as canonical.
  return (deviceId == 0x33ff0000U) || (deviceId == 0xff330000U) || (deviceId == 0x0000ff33U)
         || (deviceId == 0x44ff0000U) || (deviceId == 0xff440000U) || (deviceId == 0x0000ff44U);
}

bool DjiVirtualCamera::knownCameraMode(uint8_t mode) {
  switch (mode) {
    case 0x00:
    case 0x01:
    case 0x02:
    case 0x05:
    case 0x0a:
    case 0x1a:
    case 0x23:
    case 0x28:
    case 0x34:
    case 0x38:
    case 0x3a:
    case 0x3c:
    case 0x3f:
    case 0x41:
    case 0x43:
    case 0x44:
    case 0x4a:
      return true;
    default:
      return false;
  }
}

bool DjiVirtualCamera::validKeyReport(const uint8_t *payload, size_t length) {
  if ((payload == nullptr) || (length != 4)) {
    return false;
  }
  if ((payload[0] != KEY_RECORD) && (payload[0] != KEY_QUICK_SWITCH)
      && (payload[0] != KEY_SNAPSHOT)) {
    return false;
  }
  const uint8_t mode = payload[1];
  const uint16_t value = readLE16(payload + 2);
  if (mode == 0x00) {
    return value <= 0x0001;
  }
  if (mode == 0x01) {
    return value <= 0x0004;
  }
  return false;
}

const char *DjiVirtualCamera::commandName(Command command) {
  switch (command) {
    case Command::VersionQuery:
      return "version query";
    case Command::KeyReport:
      return "key report";
    case Command::DeviceRestart:
      return "device restart";
    case Command::GpsPush:
      return "GPS push";
    case Command::ConnectionRequest:
      return "connection request";
    case Command::ConnectionResponse:
      return "connection response";
    case Command::PowerMode:
      return "power mode";
    case Command::RecordControl:
      return "record control";
    case Command::ModeSwitch:
      return "mode switch";
    case Command::StatusSubscription:
      return "status subscription";
    case Command::Unknown:
    default:
      return "unknown";
  }
}

NimBLEAdvertisedDevice DjiVirtualCamera::advertisement() const {
  std::vector<uint8_t> manufacturer = {0xaa, 0x08, m_Config.device_type, m_Config.model_byte3,
                                       m_Config.state_byte};
  if (m_Config.advertise_mimo_tail) {
    manufacturer.insert(manufacturer.end(), m_Config.mac.begin(), m_Config.mac.end());
    manufacturer.push_back(0x04);  // Mimo flags byte, value not interpreted.
    manufacturer.push_back(static_cast<uint8_t>(m_Config.advertisement_product_id & 0xff));
    manufacturer.push_back(static_cast<uint8_t>((m_Config.advertisement_product_id >> 8) & 0xff));
  }

  NimBLEAdvertisedDevice device;
  device.setAddress(m_Config.address);
  device.setName(m_Config.name);
  device.setManufacturerData(manufacturer.data(), manufacturer.size());
  device.setRSSI(-52);
  return device;
}

const DjiVirtualCamera::Config &DjiVirtualCamera::config() const {
  return m_Config;
}

bool DjiVirtualCamera::matchesDemoAdvertisement() const {
  const NimBLEAdvertisedDevice device = advertisement();
  const auto manufacturer = device.getManufacturerData();
  return (manufacturer.size() >= 5) && (manufacturer[0] == 0xaa) && (manufacturer[1] == 0x08)
         && (manufacturer[4] == 0xfa);
}

uint8_t DjiVirtualCamera::advertisedDeviceType() const {
  return m_Config.device_type;
}

bool DjiVirtualCamera::cameraBonded() const {
  return m_Config.camera_bonded;
}

bool DjiVirtualCamera::staleBond() const {
  return m_StaleBond;
}

bool DjiVirtualCamera::smpRisk() const {
  return m_SmpRisk;
}

bool DjiVirtualCamera::secureAccepted() const {
  return m_SecureAccepted;
}

bool DjiVirtualCamera::secured() const {
  return m_SecureAccepted;
}

bool DjiVirtualCamera::configured() const {
  return m_ProtocolReady;
}

void DjiVirtualCamera::removeCameraBond() {
  m_Config.camera_bonded = false;
  m_Config.camera_pairing_record = false;
  m_ConnectionApproved = false;
  m_StaleBond = false;
}

bool DjiVirtualCamera::protocolReady() const {
  return m_ProtocolReady;
}

bool DjiVirtualCamera::connectionApproved() const {
  return m_ConnectionApproved;
}

bool DjiVirtualCamera::connectionRejected() const {
  return m_ConnectionRejected;
}

bool DjiVirtualCamera::pendingApproval() const {
  return m_PendingApproval;
}

uint16_t DjiVirtualCamera::pendingSequence() const {
  return m_PendingSequence;
}

uint32_t DjiVirtualCamera::lastCameraDeviceId() const {
  return m_LastCameraDeviceId;
}

uint32_t DjiVirtualCamera::cameraDeviceId() const {
  return m_Config.camera_device_id;
}

uint32_t DjiVirtualCamera::controllerDeviceId() const {
  return m_Config.controller_device_id;
}

DjiVirtualCamera::DeviceIdEncoding DjiVirtualCamera::deviceIdEncoding() const {
  return m_Config.device_id_encoding;
}

void DjiVirtualCamera::setDeviceIdEncoding(DeviceIdEncoding encoding) {
  m_Config.device_id_encoding = encoding;
}

uint32_t DjiVirtualCamera::wireDeviceId() const {
  switch (m_Config.device_id_encoding) {
    case DeviceIdEncoding::Rotated:
      return (m_Config.camera_device_id >> 16) | (m_Config.camera_device_id << 16);
    case DeviceIdEncoding::LowHalf:
      return m_Config.camera_device_id >> 16;
    case DeviceIdEncoding::Table:
    default:
      return m_Config.camera_device_id;
  }
}

bool DjiVirtualCamera::asleep() const {
  return m_Asleep;
}

bool DjiVirtualCamera::recording() const {
  return m_Recording;
}

uint8_t DjiVirtualCamera::cameraMode() const {
  return m_CameraMode;
}

uint32_t DjiVirtualCamera::cameraIndex() const {
  return m_CameraIndex;
}

bool DjiVirtualCamera::statusSubscribed() const {
  return m_StatusSubscribed;
}

uint8_t DjiVirtualCamera::pushMode() const {
  return m_PushMode;
}

uint8_t DjiVirtualCamera::pushFrequency() const {
  return m_PushFrequency;
}

uint8_t DjiVirtualCamera::lastReturnCode() const {
  return m_LastReturnCode;
}

uint8_t DjiVirtualCamera::lastAttError() const {
  return m_LastAttError;
}

DjiVirtualCamera::Fault DjiVirtualCamera::lastFault() const {
  return m_LastFault;
}

uint8_t DjiVirtualCamera::lastKeyCode() const {
  return m_LastKeyCode;
}

uint16_t DjiVirtualCamera::lastKeyValue() const {
  return m_LastKeyValue;
}

uint8_t DjiVirtualCamera::lastMode() const {
  return m_LastMode;
}

const std::vector<uint8_t> &DjiVirtualCamera::lastGps() const {
  return m_LastGps;
}

const DjiVirtualCamera::Frame &DjiVirtualCamera::lastFrame() const {
  return m_LastFrame;
}

size_t DjiVirtualCamera::malformedFrameCount() const {
  return m_MalformedFrames;
}

size_t DjiVirtualCamera::invalidCommandCount() const {
  return m_InvalidCommands;
}

size_t DjiVirtualCamera::wakeAdvertisementCount() const {
  return m_WakeAdvertisements;
}

const std::vector<DjiVirtualCamera::Write> &DjiVirtualCamera::writes() const {
  return m_Writes;
}

const std::vector<DjiVirtualCamera::Notification> &DjiVirtualCamera::notifications() const {
  return m_Notifications;
}

void DjiVirtualCamera::clearEvents() {
  m_Writes.clear();
  m_Notifications.clear();
}

bool DjiVirtualCamera::connected() const {
  return m_Connected;
}

void DjiVirtualCamera::setScript(const std::vector<ScriptStep> &steps) {
  m_Script.assign(steps.begin(), steps.end());
  m_Scripted = true;
}

void DjiVirtualCamera::appendScriptStep(const ScriptStep &step) {
  m_Script.push_back(step);
  m_Scripted = true;
}

void DjiVirtualCamera::clearScript() {
  m_Script.clear();
  m_Scripted = false;
}

size_t DjiVirtualCamera::scriptRemaining() const {
  return m_Script.size();
}

void DjiVirtualCamera::setScripted(bool enabled) {
  m_Scripted = enabled;
}

bool DjiVirtualCamera::scripted() const {
  return m_Scripted;
}

void DjiVirtualCamera::scriptNextCommand(Fault fault) {
  m_Script.push_back({Command::Unknown, fault});
  m_Scripted = true;
}

size_t DjiVirtualCamera::scriptedRemaining() const {
  return m_Script.size();
}

void DjiVirtualCamera::setNextFault(Fault fault) {
  m_NextFault = fault;
}

DjiVirtualCamera::Fault DjiVirtualCamera::pendingFault() const {
  return m_NextFault;
}

void DjiVirtualCamera::clearFault() {
  m_NextFault = Fault::None;
}

void DjiVirtualCamera::clearFaults() {
  m_NextFault = Fault::None;
  m_Script.clear();
  m_Scripted = false;
}

DjiVirtualCamera::Fault DjiVirtualCamera::takeFault(Command command) {
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

void DjiVirtualCamera::noteReturnCode(uint8_t code) {
  m_LastReturnCode = code;
}

void DjiVirtualCamera::recordWrite(const NimBLEUUID &characteristic,
                                   Command command,
                                   const std::vector<uint8_t> &frame,
                                   bool accepted) {
  Write write;
  write.service = TARGET_SERVICE.toString();
  write.characteristic = characteristic.toString();
  write.command = commandName(command);
  write.frame = frame;
  write.accepted = accepted;
  m_Writes.push_back(write);
}

bool DjiVirtualCamera::emitNotification(const std::vector<uint8_t> &payload) {
  return emitRawNotification(payload);
}

bool DjiVirtualCamera::emitRawNotification(const std::vector<uint8_t> &payload) {
  const auto found = m_Subscriptions.find(key(TARGET_SERVICE, NOTIFY_CHARACTERISTIC));
  if (!found || (found->callback == nullptr)) {
    return false;
  }

  Notification notification;
  notification.characteristic = NOTIFY_CHARACTERISTIC.toString();
  notification.payload = payload;
  m_Notifications.push_back(notification);

  std::vector<uint8_t> mutable_payload = payload;
  found->callback(found->remote, mutable_payload.empty() ? nullptr : mutable_payload.data(),
                  mutable_payload.size(), found->notification);
  return true;
}

bool DjiVirtualCamera::sendResponse(uint8_t commandSet,
                                    uint8_t commandId,
                                    uint8_t commandType,
                                    uint16_t sequence,
                                    const std::vector<uint8_t> &payload) {
  if (m_SuppressResponses) {
    return false;
  }
  const std::vector<uint8_t> frame =
      buildFrame(commandSet, commandId, commandType, sequence, payload);
  if (frame.empty()) {
    return false;
  }
  return emitRawNotification(frame);
}

bool DjiVirtualCamera::sendStatusResponse(uint8_t commandSet,
                                          uint8_t commandId,
                                          uint16_t sequence,
                                          uint8_t returnCode) {
  std::vector<uint8_t> payload;
  if (commandSet == CMD_SET_CAMERA) {
    payload = {returnCode, 0x00, 0x00, 0x00, 0x00};
  } else {
    payload = {returnCode};
  }
  return sendResponse(commandSet, commandId, 0x21, sequence, payload);
}

std::vector<uint8_t> DjiVirtualCamera::cameraConnectionRequest(bool allow) const {
  std::vector<uint8_t> payload(CONNECTION_PAYLOAD_BYTES, 0x00);
  writeLE32(payload.data(), wireDeviceId());
  payload[4] = 0x06;  // mac_addr_len
  for (size_t i = 0; i < m_Config.mac.size(); i++) {
    payload[5 + i] = m_Config.mac[i];
  }
  // fw_version stays 0 so the camera does not push firmware updates to the
  // controller, as the SDK documents.
  payload[25] = 0x00;  // conidx
  payload[26] = 0x02;  // verify_mode result
  writeLE16(payload.data() + 27, allow ? 0x0000 : 0x0001);
  return payload;
}

bool DjiVirtualCamera::sendCameraConnectionRequest(bool allow) {
  const std::vector<uint8_t> payload = cameraConnectionRequest(allow);
  const bool sent = sendResponse(CMD_SET_COMMON, CMD_CONNECTION, 0x02, m_PendingSequence, payload);
  m_LastCameraDeviceId = wireDeviceId();
  if (sent) {
    m_ConnectionApproved = allow;
    m_ConnectionRejected = !allow;
  }
  m_PendingApproval = false;
  if (allow) {
    // The camera-side record is an application decision. It can be saved even
    // when the controller did not receive the approval frame, which is the
    // timeout case a later saved reconnect must tolerate.
    m_Config.camera_pairing_record = true;
  }
  return sent;
}

bool DjiVirtualCamera::approveConnection(bool allow) {
  if (!m_PendingApproval) {
    return false;
  }
  return sendCameraConnectionRequest(allow);
}

bool DjiVirtualCamera::handleConnectionRequest(const Frame &frame) {
  if (frame.payload.size() != CONNECTION_PAYLOAD_BYTES) {
    m_InvalidCommands++;
    if (m_Config.send_connection_response) {
      std::vector<uint8_t> payload(9, 0x00);
      writeLE32(payload.data(), m_Config.controller_device_id);
      payload[4] = RETURN_PARSE_ERROR;
      sendResponse(CMD_SET_COMMON, CMD_CONNECTION, 0x20, frame.sequence, payload);
    }
    noteReturnCode(RETURN_PARSE_ERROR);
    m_ConnectionRejected = true;
    return false;
  }

  m_PendingControllerId = readLE32(frame.payload.data());
  m_PendingSequence = frame.sequence;
  const uint8_t macLength = frame.payload[4];
  const uint8_t verifyMode = frame.payload[26];
  const uint16_t verifyData = readLE16(frame.payload.data() + 27);
  (void)verifyData;

  if (macLength != 0x06) {
    m_InvalidCommands++;
    if (m_Config.send_connection_response) {
      std::vector<uint8_t> payload(9, 0x00);
      writeLE32(payload.data(), m_PendingControllerId);
      payload[4] = RETURN_PARSE_ERROR;
      sendResponse(CMD_SET_COMMON, CMD_CONNECTION, 0x20, frame.sequence, payload);
    }
    noteReturnCode(RETURN_PARSE_ERROR);
    m_ConnectionRejected = true;
    return false;
  }
  if (verifyMode > 0x01) {
    // Only 0 and 1 are controller modes. 2 is the camera result mode.
    m_InvalidCommands++;
    if (m_Config.send_connection_response) {
      std::vector<uint8_t> payload(9, 0x00);
      writeLE32(payload.data(), m_PendingControllerId);
      payload[4] = RETURN_PARSE_ERROR;
      sendResponse(CMD_SET_COMMON, CMD_CONNECTION, 0x20, frame.sequence, payload);
    }
    noteReturnCode(RETURN_PARSE_ERROR);
    m_ConnectionRejected = true;
    return false;
  }

  if (m_Config.reject_connection) {
    if (m_Config.send_connection_response) {
      std::vector<uint8_t> payload(9, 0x00);
      writeLE32(payload.data(), m_PendingControllerId);
      payload[4] = RETURN_EXECUTION_FAILED;
      sendResponse(CMD_SET_COMMON, CMD_CONNECTION, 0x20, frame.sequence, payload);
    }
    noteReturnCode(RETURN_EXECUTION_FAILED);
    m_ConnectionRejected = true;
    return false;
  }

  if (m_Config.send_connection_response) {
    std::vector<uint8_t> payload(9, 0x00);
    writeLE32(payload.data(), m_PendingControllerId);
    payload[4] = RETURN_SUCCESS;
    sendResponse(CMD_SET_COMMON, CMD_CONNECTION, 0x20, frame.sequence, payload);
  }
  noteReturnCode(RETURN_SUCCESS);

  // The camera shows a code popup for a first pairing, and for a saved
  // reconnect whose camera-side record is gone. A saved record with mode 0
  // proceeds without the popup.
  const bool needPopup = (verifyMode == 0x01)
                         || ((verifyMode == 0x00) && !m_Config.camera_pairing_record
                             && m_Config.show_popup_when_no_record);
  if (needPopup && m_Config.require_approval && !m_Config.auto_approve) {
    m_PendingApproval = true;
    return true;
  }
  return sendCameraConnectionRequest(true);
}

bool DjiVirtualCamera::handleConnectionResponse(const Frame &frame) {
  if (frame.payload.size() != 9) {
    m_InvalidCommands++;
    noteReturnCode(RETURN_PARSE_ERROR);
    return false;
  }
  if (!m_ConnectionApproved || (frame.sequence != m_PendingSequence)) {
    m_InvalidCommands++;
    noteReturnCode(RETURN_PARSE_ERROR);
    return false;
  }
  const uint32_t controllerId = readLE32(frame.payload.data());
  if (controllerId != m_PendingControllerId) {
    m_InvalidCommands++;
    noteReturnCode(RETURN_PARSE_ERROR);
    return false;
  }
  const uint8_t returnCode = frame.payload[4];
  if (returnCode != RETURN_SUCCESS) {
    noteReturnCode(returnCode);
    m_ConnectionRejected = true;
    m_ProtocolReady = false;
    return false;
  }
  m_CameraIndex = readLE32(frame.payload.data() + 5);
  m_ProtocolReady = true;
  m_ConnectionRejected = false;
  noteReturnCode(RETURN_SUCCESS);
  return true;
}

bool DjiVirtualCamera::handleCommand(const Frame &frame) {
  switch (classify(frame)) {
    case Command::VersionQuery:
    {
      if (!m_ProtocolReady) {
        noteReturnCode(RETURN_EXECUTION_FAILED);
        sendResponse(CMD_SET_COMMON, CMD_VERSION, 0x22, frame.sequence, {0x02, 0x00});
        return false;
      }
      std::vector<uint8_t> payload = {0x00, 0x00};
      std::array<uint8_t, 16> product = {};
      const size_t productLength =
          std::min<size_t>(m_Config.product_id_string.size(), product.size());
      std::copy_n(m_Config.product_id_string.begin(), productLength, product.begin());
      payload.insert(payload.end(), product.begin(), product.end());
      payload.insert(payload.end(), m_Config.sdk_version.begin(), m_Config.sdk_version.end());
      noteReturnCode(RETURN_SUCCESS);
      sendResponse(CMD_SET_COMMON, CMD_VERSION, 0x22, frame.sequence, payload);
      return true;
    }
    case Command::KeyReport:
    {
      if (!m_ProtocolReady) {
        noteReturnCode(RETURN_EXECUTION_FAILED);
        sendStatusResponse(CMD_SET_COMMON, CMD_KEY_REPORT, frame.sequence, RETURN_EXECUTION_FAILED);
        return false;
      }
      if (!validKeyReport(frame.payload.data(), frame.payload.size())) {
        m_InvalidCommands++;
        noteReturnCode(RETURN_PARSE_ERROR);
        sendStatusResponse(CMD_SET_COMMON, CMD_KEY_REPORT, frame.sequence, RETURN_PARSE_ERROR);
        return false;
      }
      m_LastKeyCode = frame.payload[0];
      m_LastKeyValue = readLE16(frame.payload.data() + 2);
      // The SDK documents the key report, but whether a short press takes a
      // physical photo in photo mode is not proven (spec U-29). The peer
      // records the key and reports success without claiming a capture.
      noteReturnCode(RETURN_SUCCESS);
      sendStatusResponse(CMD_SET_COMMON, CMD_KEY_REPORT, frame.sequence, RETURN_SUCCESS);
      return true;
    }
    case Command::DeviceRestart:
    {
      if (!m_ProtocolReady) {
        noteReturnCode(RETURN_EXECUTION_FAILED);
        sendStatusResponse(CMD_SET_COMMON, CMD_RESTART, frame.sequence, RETURN_EXECUTION_FAILED);
        return false;
      }
      if (frame.payload.empty()) {
        m_InvalidCommands++;
        noteReturnCode(RETURN_PARSE_ERROR);
        sendStatusResponse(CMD_SET_COMMON, CMD_RESTART, frame.sequence, RETURN_PARSE_ERROR);
        return false;
      }
      std::vector<uint8_t> payload(9, 0x00);
      writeLE32(payload.data(), readLE32(frame.payload.data()));
      payload[4] = RETURN_SUCCESS;
      noteReturnCode(RETURN_SUCCESS);
      sendResponse(CMD_SET_COMMON, CMD_RESTART,
                   static_cast<uint8_t>(0x20 | (frame.command_type & 0x1f)), frame.sequence,
                   payload);
      if (m_Config.restart_drops_link && (m_Client != nullptr)) {
        m_Client->mockDropLink(0x13, /*fire_callback=*/true);
      }
      return true;
    }
    case Command::GpsPush:
    {
      if (!m_ProtocolReady) {
        m_InvalidCommands++;
        noteReturnCode(RETURN_EXECUTION_FAILED);
        return false;
      }
      if (frame.payload.size() != GPS_PAYLOAD_BYTES) {
        m_InvalidCommands++;
        noteReturnCode(RETURN_PARSE_ERROR);
        return false;
      }
      m_LastGps = frame.payload;
      noteReturnCode(RETURN_SUCCESS);
      if (m_Config.ack_gps_push) {
        sendStatusResponse(CMD_SET_COMMON, CMD_GPS_PUSH, frame.sequence, RETURN_SUCCESS);
      }
      return true;
    }
    case Command::PowerMode:
    {
      if (!m_ProtocolReady) {
        noteReturnCode(RETURN_EXECUTION_FAILED);
        sendStatusResponse(CMD_SET_COMMON, CMD_POWER_MODE, frame.sequence, RETURN_EXECUTION_FAILED);
        return false;
      }
      if (frame.payload.empty()) {
        m_InvalidCommands++;
        noteReturnCode(RETURN_PARSE_ERROR);
        sendStatusResponse(CMD_SET_COMMON, CMD_POWER_MODE, frame.sequence, RETURN_PARSE_ERROR);
        return false;
      }
      const uint8_t powerMode = frame.payload[0];
      if ((powerMode != POWER_NORMAL) && (powerMode != POWER_SLEEP)) {
        noteReturnCode(RETURN_PARSE_ERROR);
        sendStatusResponse(CMD_SET_COMMON, CMD_POWER_MODE, frame.sequence, RETURN_PARSE_ERROR);
        return false;
      }
      m_Asleep = (powerMode == POWER_SLEEP);
      m_Config.asleep = m_Asleep;
      m_Config.power_mode = powerMode;
      noteReturnCode(RETURN_SUCCESS);
      sendStatusResponse(CMD_SET_COMMON, CMD_POWER_MODE, frame.sequence, RETURN_SUCCESS);
      return true;
    }
    case Command::RecordControl:
    {
      if (frame.payload.size() != 9) {
        m_InvalidCommands++;
        noteReturnCode(RETURN_PARSE_ERROR);
        sendStatusResponse(CMD_SET_CAMERA, CMD_RECORD, frame.sequence, RETURN_PARSE_ERROR);
        return false;
      }
      if (m_Config.strict_command_device_id
          && (readLE32(frame.payload.data()) != m_Config.controller_device_id)) {
        m_InvalidCommands++;
        noteReturnCode(RETURN_PARSE_ERROR);
        sendStatusResponse(CMD_SET_CAMERA, CMD_RECORD, frame.sequence, RETURN_PARSE_ERROR);
        return false;
      }
      const uint8_t recordControl = frame.payload[4];
      if (recordControl > 0x01) {
        m_InvalidCommands++;
        noteReturnCode(RETURN_PARSE_ERROR);
        sendStatusResponse(CMD_SET_CAMERA, CMD_RECORD, frame.sequence, RETURN_PARSE_ERROR);
        return false;
      }
      if (!m_ProtocolReady || m_Asleep) {
        // The camera mode is not checked, as the SDK documents, but a sleeping
        // camera is refused because the wake path was not exercised.
        noteReturnCode(RETURN_EXECUTION_FAILED);
        sendStatusResponse(CMD_SET_CAMERA, CMD_RECORD, frame.sequence, RETURN_EXECUTION_FAILED);
        return false;
      }
      m_Recording = (recordControl == 0x00);
      m_Config.camera_status = m_Recording ? 0x03 : 0x01;
      m_Config.record_time = 0;
      noteReturnCode(RETURN_SUCCESS);
      sendStatusResponse(CMD_SET_CAMERA, CMD_RECORD, frame.sequence, RETURN_SUCCESS);
      if (m_Config.status_push_on_record) {
        emitStatus();
      }
      return true;
    }
    case Command::ModeSwitch:
    {
      if (frame.payload.size() != 9) {
        m_InvalidCommands++;
        noteReturnCode(RETURN_PARSE_ERROR);
        sendStatusResponse(CMD_SET_CAMERA, CMD_MODE_SWITCH, frame.sequence, RETURN_PARSE_ERROR);
        return false;
      }
      const uint8_t mode = frame.payload[4];
      if (!knownCameraMode(mode)) {
        m_InvalidCommands++;
        noteReturnCode(RETURN_PARSE_ERROR);
        sendStatusResponse(CMD_SET_CAMERA, CMD_MODE_SWITCH, frame.sequence, RETURN_PARSE_ERROR);
        return false;
      }
      if (!m_ProtocolReady || m_Asleep) {
        noteReturnCode(RETURN_EXECUTION_FAILED);
        sendStatusResponse(CMD_SET_CAMERA, CMD_MODE_SWITCH, frame.sequence,
                           RETURN_EXECUTION_FAILED);
        return false;
      }
      m_CameraMode = mode;
      m_LastMode = mode;
      m_Config.camera_mode = mode;
      noteReturnCode(RETURN_SUCCESS);
      sendStatusResponse(CMD_SET_CAMERA, CMD_MODE_SWITCH, frame.sequence, RETURN_SUCCESS);
      if (m_Config.status_push_on_mode) {
        emitStatus();
      }
      if (m_Config.push_1d06 && (mode > 0x34)) {
        emitNewStatus();
      }
      return true;
    }
    case Command::StatusSubscription:
    {
      if (frame.payload.size() != 6) {
        m_InvalidCommands++;
        noteReturnCode(RETURN_PARSE_ERROR);
        return false;
      }
      const uint8_t pushMode = frame.payload[0];
      const uint8_t pushFrequency = frame.payload[1];
      if ((pushMode > 0x03) || ((pushMode != 0x00) && (pushFrequency != 20))) {
        // push_freq is in 0.1 Hz and only 20 (2 Hz) is allowed.
        m_InvalidCommands++;
        noteReturnCode(RETURN_PARSE_ERROR);
        return false;
      }
      if (!m_ProtocolReady) {
        noteReturnCode(RETURN_EXECUTION_FAILED);
        return false;
      }
      m_StatusSubscribed = (pushMode != 0x00);
      m_PushMode = pushMode;
      m_PushFrequency = pushFrequency;
      noteReturnCode(RETURN_SUCCESS);
      if (m_StatusSubscribed) {
        emitStatus();
      }
      return true;
    }
    case Command::ConnectionRequest:
      return handleConnectionRequest(frame);
    case Command::ConnectionResponse:
      return handleConnectionResponse(frame);
    case Command::Unknown:
    default:
    {
      m_InvalidCommands++;
      noteReturnCode(RETURN_PARSE_ERROR);
      if ((frame.command_type & 0x1f) != 0x00) {
        sendStatusResponse(frame.command_set, frame.command_id, frame.sequence, RETURN_PARSE_ERROR);
      }
      return false;
    }
  }
}

bool DjiVirtualCamera::write(NimBLEClient &client,
                             const NimBLEUUID &service,
                             const NimBLEUUID &characteristic,
                             const std::vector<uint8_t> &value,
                             bool response) {
  const std::lock_guard<std::mutex> journal(Host::peerWriteJournalMutex());
  (void)response;
  if (!m_Connected || (m_Client != &client) || !canWrite(service, characteristic)) {
    return false;
  }

  if (matches(service, TARGET_SERVICE) && matches(characteristic, EXTERNAL_DATA_CHARACTERISTIC)) {
    if (!m_Config.fff3_writes_accepted) {
      // FFF3 is present in Mimo, but its payload is not documented and the
      // R SDK demo never uses it. Do not report an unproven write as success.
      m_LastAttError = ATT_INVALID_VALUE;
      recordWrite(characteristic, Command::Unknown, value, false);
      return false;
    }
    recordWrite(characteristic, Command::Unknown, value, true);
    return true;
  }

  Frame frame;
  if (!parseFrame(value.data(), value.size(), frame)) {
    m_MalformedFrames++;
    m_LastAttError = ATT_INVALID_VALUE;
    recordWrite(characteristic, Command::Unknown, value, false);
    // A malformed frame is still a completed ATT write on hardware. The
    // camera logs the rejection and ignores the frame, so no response is sent.
    return m_Config.att_accepts_malformed_frames;
  }
  m_LastFrame = frame;

  const Command command = classify(frame);
  const Fault fault = takeFault(command);
  m_LastFault = fault;
  m_LastAttError = 0;

  if ((fault == Fault::Reject) || (fault == Fault::Busy) || (fault == Fault::WrongMode)) {
    const uint8_t code = fault == Fault::Reject ? RETURN_PARSE_ERROR : RETURN_EXECUTION_FAILED;
    noteReturnCode(code);
    if (command == Command::ConnectionRequest) {
      std::vector<uint8_t> payload(9, 0x00);
      writeLE32(payload.data(), m_Config.controller_device_id);
      payload[4] = code;
      sendResponse(CMD_SET_COMMON, CMD_CONNECTION, 0x20, frame.sequence, payload);
      m_ConnectionRejected = true;
    } else if ((frame.command_type & 0x1f) != 0x00) {
      sendStatusResponse(frame.command_set, frame.command_id, frame.sequence, code);
    }
    m_LastAttError = ATT_APPLICATION_ERROR;
    recordWrite(characteristic, command, value, false);
    return false;
  }
  if (fault == Fault::StaleBond) {
    m_Config.camera_bonded = false;
    m_Config.camera_pairing_record = false;
    m_StaleBond = true;
    m_LastAttError = ATT_INSUFFICIENT_AUTHENTICATION;
    noteReturnCode(RETURN_PARSE_ERROR);
    if (command == Command::ConnectionRequest) {
      std::vector<uint8_t> payload(9, 0x00);
      writeLE32(payload.data(), m_Config.controller_device_id);
      payload[4] = RETURN_PARSE_ERROR;
      sendResponse(CMD_SET_COMMON, CMD_CONNECTION, 0x20, frame.sequence, payload);
      m_ConnectionRejected = true;
    } else if ((frame.command_type & 0x1f) != 0x00) {
      sendStatusResponse(frame.command_set, frame.command_id, frame.sequence, RETURN_PARSE_ERROR);
    }
    recordWrite(characteristic, command, value, false);
    return false;
  }

  bool accepted = false;
  if (fault == Fault::Timeout) {
    // The command still reaches the camera state, but no response or status
    // notification is emitted, so a caller waiting for one times out.
    m_SuppressResponses = true;
    accepted = handleCommand(frame);
    m_SuppressResponses = false;
  } else {
    accepted = handleCommand(frame);
  }

  recordWrite(characteristic, command, value, accepted);

  if (fault == Fault::Malformed) {
    std::vector<uint8_t> malformed =
        buildFrame(CMD_SET_CAMERA, CMD_STATUS_PUSH, 0x00, 0, statusPayload());
    if (!malformed.empty()) {
      malformed.back() ^= 0xff;
      emitRawNotification(malformed);
    }
  }
  if (fault == Fault::Disconnect) {
    client.mockDropLink(0x13, /*fire_callback=*/true);
    return false;
  }
  return accepted;
}

NimBLEAttValue DjiVirtualCamera::read(NimBLEClient &client,
                                      const NimBLEUUID &service,
                                      const NimBLEUUID &characteristic) {
  (void)client;
  (void)service;
  (void)characteristic;
  // FFF4 read is unproven and every other standard read format is unknown, so
  // the peer does not fabricate a value.
  return {};
}

bool DjiVirtualCamera::subscribe(NimBLEClient &client,
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
  m_Subscriptions.set(key(service, characteristic), subscription);
  return true;
}

bool DjiVirtualCamera::secureConnection(NimBLEClient &client) {
  if (!m_Connected || (m_Client != &client)) {
    return false;
  }
  m_SecureAccepted = false;
  if (m_Config.smp_rejects) {
    m_LastAttError = ATT_INSUFFICIENT_AUTHENTICATION;
    return false;
  }
  if (m_Config.smp_open_link_only) {
    // The official demo uses no SMP and unauthenticated ATT for every
    // operation. Furble requires an encrypted bonded link, so a camera that
    // accepted only the demo flow leaves Furble unable to connect.
    m_SmpRisk = true;
    m_LastAttError = ATT_INSUFFICIENT_AUTHENTICATION;
    return false;
  }
  if (!m_Config.accept_numeric_comparison) {
    m_LastAttError = ATT_INSUFFICIENT_AUTHENTICATION;
    return false;
  }

  const bool localBonded = NimBLEDevice::isBonded(m_Config.address);
  if (localBonded != m_Config.camera_bonded) {
    m_StaleBond = true;
    m_LastAttError = ATT_INSUFFICIENT_AUTHENTICATION;
    return false;
  }
  if (!localBonded) {
    NimBLEDevice::setBondedAddress(m_Config.address);
    m_Config.camera_bonded = true;
  }
  m_StaleBond = false;
  m_LastAttError = 0;
  m_SecureAccepted = true;
  return true;
}

bool DjiVirtualCamera::updateConnectionParams(NimBLEClient &client,
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

int DjiVirtualCamera::getRssi() const {
  return -52;
}

bool DjiVirtualCamera::acceptConnection(NimBLEClient &client, const NimBLEAddress &address) {
  if (m_Connected || (address != m_Config.address)) {
    return false;
  }
  m_Client = &client;
  m_Connected = true;
  m_LastAttError = 0;
  resetLink();
  return true;
}

void DjiVirtualCamera::disconnect(NimBLEClient &client, int reason) {
  (void)reason;
  if (m_Client != &client) {
    return;
  }
  m_Client = nullptr;
  m_Connected = false;
  m_SecureAccepted = false;
  m_ProtocolReady = false;
  m_PendingApproval = false;
  m_StatusSubscribed = false;
  m_Subscriptions.clear();
}

void DjiVirtualCamera::resetLink() {
  m_SecureAccepted = false;
  m_ProtocolReady = false;
  m_ConnectionApproved = false;
  m_ConnectionRejected = false;
  m_PendingApproval = false;
  m_PendingSequence = 0;
  m_PendingControllerId = 0;
  m_CameraIndex = 0;
  m_StatusSubscribed = false;
  m_PushMode = 0;
  m_PushFrequency = 0;
  m_SuppressResponses = false;
  m_Subscriptions.clear();
}

bool DjiVirtualCamera::hasService(const NimBLEUUID &service) const {
  return matches(service, TARGET_SERVICE)
         || (m_Config.service_changed_present && matches(service, GATT_SERVICE));
}

bool DjiVirtualCamera::hasCharacteristic(const NimBLEUUID &service,
                                         const NimBLEUUID &characteristic) const {
  if (matches(service, GATT_SERVICE) && m_Config.service_changed_present) {
    return matches(characteristic, SERVICE_CHANGED_CHARACTERISTIC);
  }
  if (!matches(service, TARGET_SERVICE)) {
    return false;
  }
  return matches(characteristic, NOTIFY_CHARACTERISTIC)
         || matches(characteristic, WRITE_CHARACTERISTIC)
         || (m_Config.fff3_present && matches(characteristic, EXTERNAL_DATA_CHARACTERISTIC));
}

bool DjiVirtualCamera::discoverCharacteristic(NimBLEClient &client,
                                              const NimBLEUUID &service,
                                              const NimBLEUUID &characteristic) {
  (void)client;
  return hasCharacteristic(service, characteristic);
}

bool DjiVirtualCamera::canWrite(const NimBLEUUID &service, const NimBLEUUID &characteristic) const {
  return matches(service, TARGET_SERVICE)
         && (matches(characteristic, WRITE_CHARACTERISTIC)
             || (m_Config.fff3_present && matches(characteristic, EXTERNAL_DATA_CHARACTERISTIC)));
}

DjiVirtualCamera::Command DjiVirtualCamera::classify(const Frame &frame) const {
  if (frame.response) {
    if ((frame.command_set == CMD_SET_COMMON) && (frame.command_id == CMD_CONNECTION)) {
      return Command::ConnectionResponse;
    }
    return Command::Unknown;
  }
  if (frame.command_set == CMD_SET_COMMON) {
    switch (frame.command_id) {
      case CMD_VERSION:
        return Command::VersionQuery;
      case CMD_KEY_REPORT:
        return Command::KeyReport;
      case CMD_RESTART:
        return Command::DeviceRestart;
      case CMD_GPS_PUSH:
        return Command::GpsPush;
      case CMD_CONNECTION:
        return Command::ConnectionRequest;
      case CMD_POWER_MODE:
        return Command::PowerMode;
      default:
        return Command::Unknown;
    }
  }
  if (frame.command_set == CMD_SET_CAMERA) {
    switch (frame.command_id) {
      case CMD_RECORD:
        return Command::RecordControl;
      case CMD_MODE_SWITCH:
        return Command::ModeSwitch;
      case CMD_STATUS_SUBSCRIPTION:
        return Command::StatusSubscription;
      default:
        return Command::Unknown;
    }
  }
  return Command::Unknown;
}

std::vector<uint8_t> DjiVirtualCamera::statusPayload() const {
  std::vector<uint8_t> payload(38, 0x00);
  payload[0] = m_CameraMode;
  payload[1] = m_Config.camera_status;
  payload[2] = m_Config.video_resolution;
  payload[3] = m_Config.fps_idx;
  payload[4] = m_Config.eis_mode;
  writeLE16(payload.data() + 5, m_Config.record_time);
  payload[7] = 0x00;  // fov_type reserved
  payload[8] = m_Config.photo_ratio;
  writeLE16(payload.data() + 9, 0x0000);   // real_time_countdown
  writeLE16(payload.data() + 11, 0x0000);  // timelapse_interval
  writeLE16(payload.data() + 13, 0x0000);  // timelapse_duration
  writeLE32(payload.data() + 15, m_Config.remain_capacity_mb);
  writeLE32(payload.data() + 19, m_Config.remain_photo_num);
  writeLE32(payload.data() + 23, m_Config.remain_time_s);
  payload[27] = m_Config.user_mode;
  payload[28] = m_Asleep ? POWER_SLEEP : POWER_NORMAL;
  payload[29] = m_CameraMode;
  payload[30] = m_Config.temp_over;
  writeLE32(payload.data() + 31, m_Config.photo_countdown_ms);
  writeLE16(payload.data() + 35, m_Config.loop_record_sends);
  payload[37] = m_Config.battery;
  return payload;
}

std::vector<uint8_t> DjiVirtualCamera::newStatusPayload() const {
  std::vector<uint8_t> payload(46, 0x00);
  payload[0] = 0x01;
  const size_t nameLength = std::min<size_t>(m_Config.mode_name.size(), 20);
  payload[1] = static_cast<uint8_t>(nameLength);
  std::copy_n(m_Config.mode_name.begin(), nameLength, payload.begin() + 2);
  payload[23] = 0x02;
  const size_t paramLength = std::min<size_t>(m_Config.mode_param.size(), 20);
  payload[24] = static_cast<uint8_t>(paramLength);
  std::copy_n(m_Config.mode_param.begin(), paramLength, payload.begin() + 25);
  return payload;
}

bool DjiVirtualCamera::emitStatus() {
  if (m_SuppressResponses) {
    return false;
  }
  const std::vector<uint8_t> frame =
      buildFrame(CMD_SET_CAMERA, CMD_STATUS_PUSH, 0x00, 0, statusPayload());
  if (frame.empty()) {
    return false;
  }
  return emitRawNotification(frame);
}

bool DjiVirtualCamera::emitNewStatus() {
  if (m_SuppressResponses) {
    return false;
  }
  const std::vector<uint8_t> frame =
      buildFrame(CMD_SET_CAMERA, CMD_NEW_STATUS_PUSH, 0x00, 0, newStatusPayload());
  if (frame.empty()) {
    return false;
  }
  return emitRawNotification(frame);
}

std::vector<uint8_t> DjiVirtualCamera::wakeAdvertisement() const {
  std::vector<uint8_t> payload = {0x0a, 0xff, 0x57, 0x4b, 0x50};
  for (size_t i = 0; i < m_Config.mac.size(); i++) {
    payload.push_back(m_Config.mac[m_Config.mac.size() - 1 - i]);
  }
  return payload;
}

bool DjiVirtualCamera::receiveWakeAdvertisement(const std::vector<uint8_t> &payload) {
  if (payload.size() != 11) {
    return false;
  }
  static const std::array<uint8_t, 5> PREFIX = {0x0a, 0xff, 0x57, 0x4b, 0x50};
  if (!std::equal(PREFIX.begin(), PREFIX.end(), payload.begin())) {
    return false;
  }
  for (size_t i = 0; i < m_Config.mac.size(); i++) {
    if (payload[5 + i] != m_Config.mac[m_Config.mac.size() - 1 - i]) {
      return false;
    }
  }
  m_WakeAdvertisements++;
  if (!m_Config.wake_allowed || !m_Asleep) {
    return false;
  }
  m_Asleep = false;
  m_Config.asleep = false;
  m_Config.power_mode = POWER_NORMAL;
  return true;
}

const NimBLEUUID &DjiVirtualCamera::serviceUUID() {
  return TARGET_SERVICE;
}

const NimBLEUUID &DjiVirtualCamera::notifyCharacteristicUUID() {
  return NOTIFY_CHARACTERISTIC;
}

const NimBLEUUID &DjiVirtualCamera::writeCharacteristicUUID() {
  return WRITE_CHARACTERISTIC;
}

const NimBLEUUID &DjiVirtualCamera::externalDataCharacteristicUUID() {
  return EXTERNAL_DATA_CHARACTERISTIC;
}

}  // namespace Host
}  // namespace Furble
