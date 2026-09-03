#include <inttypes.h>
#include <array>
#include <cstring>
#include <vector>

#include <NimBLEDevice.h>
#include <NimBLERemoteService.h>
#include <esp_mac.h>
#include <esp_random.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "DJIOsmo.h"
#include "Device.h"
#include "protocol/AdvertisementProtocol.h"

namespace Furble {

const NimBLEUUID DJIOsmo::TARGET_SERVICE_UUID {(uint16_t)0xFFF0};
const NimBLEUUID DJIOsmo::NOTIFY_CHARACTERISTIC_UUID {(uint16_t)0xFFF4};
const NimBLEUUID DJIOsmo::WRITE_CHARACTERISTIC_UUID {(uint16_t)0xFFF5};

namespace {

constexpr uint32_t PROTOCOL_HANDSHAKE_TIMEOUT_MS = 30 * 1000;
constexpr uint32_t PROTOCOL_HANDSHAKE_POLL_MS = 100;

}  // namespace

DJIOsmo::DJIOsmo(const void *data, size_t len) : Camera(Type::DJI_OSMO, PairType::SAVED) {
  if (len != sizeof(dji_osmo_t))
    abort();

  const dji_osmo_t *dji = static_cast<const dji_osmo_t *>(data);
  m_Name = std::string(dji->name);
  m_Address = NimBLEAddress(dji->address, dji->type);
}

DJIOsmo::DJIOsmo(const NimBLEAdvertisedDevice *pDevice) : Camera(Type::DJI_OSMO, PairType::NEW) {
  m_Name = pDevice->getName();
  if (m_Name.empty())
    m_Name = "DJI Osmo Action";
  m_Address = pDevice->getAddress();
  ESP_LOGI(LOG_TAG, "DJI Osmo Name = %s", m_Name.c_str());
  ESP_LOGI(LOG_TAG, "DJI Osmo Address = %s", m_Address.toString().c_str());
}

bool DJIOsmo::matches(const NimBLEAdvertisedDevice *pDevice) {
  if (pDevice == nullptr || !pDevice->haveManufacturerData())
    return false;

  const std::string manufacturer = pDevice->getManufacturerData();
  return AdvertisementProtocol::matchesDJIAdvertisement(
      reinterpret_cast<const uint8_t *>(manufacturer.data()), manufacturer.size());
}

uint16_t DJIOsmo::readLE16(const uint8_t *data) {
  return static_cast<uint16_t>(data[0]) | (static_cast<uint16_t>(data[1]) << 8);
}

uint32_t DJIOsmo::readLE32(const uint8_t *data) {
  return static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8)
         | (static_cast<uint32_t>(data[2]) << 16) | (static_cast<uint32_t>(data[3]) << 24);
}

void DJIOsmo::writeLE16(uint8_t *data, uint16_t value) {
  data[0] = static_cast<uint8_t>(value & 0xFF);
  data[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
}

void DJIOsmo::writeLE32(uint8_t *data, uint32_t value) {
  data[0] = static_cast<uint8_t>(value & 0xFF);
  data[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
  data[2] = static_cast<uint8_t>((value >> 16) & 0xFF);
  data[3] = static_cast<uint8_t>((value >> 24) & 0xFF);
}

uint16_t DJIOsmo::crc16(const uint8_t *data, size_t length) {
  uint16_t crc = 0x3AA3;
  for (size_t i = 0; i < length; i++) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; bit++) {
      crc = (crc & 1U) != 0 ? static_cast<uint16_t>((crc >> 1) ^ 0xA001U)
                            : static_cast<uint16_t>(crc >> 1);
    }
  }
  return crc;
}

uint32_t DJIOsmo::crc32(const uint8_t *data, size_t length) {
  uint32_t crc = 0x00003AA3;
  for (size_t i = 0; i < length; i++) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; bit++) {
      crc = (crc & 1U) != 0 ? (crc >> 1) ^ 0xEDB88320U : crc >> 1;
    }
  }
  return crc;
}

bool DJIOsmo::validFrame(const uint8_t *data, size_t length) {
  if (data == nullptr || length < FRAME_HEADER_LENGTH + FRAME_COMMAND_LENGTH + FRAME_TAIL_LENGTH)
    return false;
  if (data[0] != FRAME_SOF)
    return false;

  const uint16_t encodedLength = readLE16(data + 1);
  if ((encodedLength & FRAME_LENGTH_MASK) != length)
    return false;
  if (readLE16(data + 10) != crc16(data, 10))
    return false;
  return readLE32(data + length - FRAME_TAIL_LENGTH) == crc32(data, length - FRAME_TAIL_LENGTH);
}

bool DJIOsmo::supportedCameraId(uint32_t deviceId) {
  return deviceId == ACTION_4_DEVICE_ID || deviceId == ACTION_5_DEVICE_ID;
}

uint16_t DJIOsmo::nextSequence(void) {
  const std::lock_guard<std::mutex> lock(m_ProtocolMutex);
  return ++m_NextSequence;
}

bool DJIOsmo::_connect(void) {
  m_Progress = 0;
  clearProtocolState();

  m_RemoteDeviceId = Device::getUUID128().uint32[0];
  if (m_RemoteDeviceId == 0)
    m_RemoteDeviceId = 0x12345678U;

  if (!NimBLEDevice::setMTU(517))
    ESP_LOGW(LOG_TAG, "DJI Osmo could not request the 517 byte BLE MTU");

  ESP_LOGI(LOG_TAG, "DJI Osmo connecting");
  if (!m_Client->connect(m_Address)) {
    ESP_LOGI(LOG_TAG, "DJI Osmo connection failed");
    return false;
  }
  m_Progress = 20;

  ESP_LOGI(LOG_TAG, "DJI Osmo securing");
  if (!m_Client->secureConnection()) {
    ESP_LOGW(LOG_TAG, "DJI Osmo secure connection failed");
    return false;
  }
  {
    const NimBLEConnInfo connInfo = m_Client->getConnInfo();
    ESP_LOGI(LOG_TAG, "DJI Osmo secure: encrypted=%d authenticated=%d bonded=%d keySize=%u",
             connInfo.isEncrypted(), connInfo.isAuthenticated(), connInfo.isBonded(),
             connInfo.getSecKeySize());
    if (!connInfo.isEncrypted() || !connInfo.isBonded()) {
      ESP_LOGW(LOG_TAG, "DJI Osmo secure link incomplete");
      return false;
    }
  }
  m_Progress = 35;

  NimBLERemoteService *service = m_Client->getService(TARGET_SERVICE_UUID);
  if (service == nullptr) {
    ESP_LOGW(LOG_TAG, "DJI Osmo target service unavailable");
    return false;
  }

  m_Notify = service->getCharacteristic(NOTIFY_CHARACTERISTIC_UUID);
  m_Write = service->getCharacteristic(WRITE_CHARACTERISTIC_UUID);
  if (m_Notify == nullptr || !m_Notify->canNotify()) {
    ESP_LOGW(LOG_TAG, "DJI Osmo notify characteristic unavailable");
    return false;
  }
  if (m_Write == nullptr || (!m_Write->canWrite() && !m_Write->canWriteNoResponse())) {
    ESP_LOGW(LOG_TAG, "DJI Osmo write characteristic unavailable");
    return false;
  }
  m_WriteWithResponse = m_Write->canWrite();
  m_Progress = 55;

  const bool subscribed = m_Notify->subscribe(
      true,
      [this](NimBLERemoteCharacteristic *, uint8_t *data, size_t length, bool) {
        handleNotification(data, length);
      },
      true);
  if (!subscribed) {
    ESP_LOGW(LOG_TAG, "DJI Osmo notification subscription failed");
    return false;
  }
  m_Progress = 65;

  if (!finishProtocolConnection())
    return false;
  m_Progress = 85;

  if (!subscribeCameraStatus())
    ESP_LOGW(LOG_TAG, "DJI Osmo status subscription failed, recording control remains enabled");

  m_Progress = 100;
  ESP_LOGI(LOG_TAG, "DJI Osmo connected");
  return true;
}

void DJIOsmo::_disconnect(void) {
  clearProtocolState();
  if (m_Client != nullptr && m_Client->isConnected())
    m_Client->disconnect();
}

void DJIOsmo::clearProtocolState(void) {
  const std::lock_guard<std::mutex> lock(m_ProtocolMutex);
  m_Notify = nullptr;
  m_Write = nullptr;
  m_WriteWithResponse = false;
  m_RemoteDeviceId = 0;
  m_CameraDeviceId = 0;
  m_NextSequence = 0;
  m_Recording = false;
  m_ProtocolReady = false;
  m_ConnectionRejected = false;
  m_CameraRequestReceived = false;
  m_CameraRequestSequence = 0;
  m_CameraRequestDeviceId = 0;
  m_CameraVerifyMode = 0;
  m_CameraVerifyData = 0;
}

bool DJIOsmo::writeFrame(uint8_t cmdSet,
                         uint8_t cmdId,
                         uint8_t cmdType,
                         const uint8_t *payload,
                         size_t payloadLength,
                         uint16_t sequence) {
  if (m_Client == nullptr || !m_Client->isConnected() || m_Write == nullptr) {
    ESP_LOGW(LOG_TAG, "DJI Osmo frame skipped: write path is unavailable");
    return false;
  }
  if (payloadLength
      > FRAME_LENGTH_MASK - FRAME_HEADER_LENGTH - FRAME_COMMAND_LENGTH - FRAME_TAIL_LENGTH) {
    ESP_LOGW(LOG_TAG, "DJI Osmo frame payload is too large: %u",
             static_cast<unsigned>(payloadLength));
    return false;
  }

  const size_t frameLength =
      FRAME_HEADER_LENGTH + FRAME_COMMAND_LENGTH + payloadLength + FRAME_TAIL_LENGTH;
  std::vector<uint8_t> frame(frameLength, 0);
  frame[0] = FRAME_SOF;
  writeLE16(frame.data() + 1, static_cast<uint16_t>(frameLength));
  frame[3] = cmdType;
  writeLE16(frame.data() + 8, sequence);
  writeLE16(frame.data() + 10, crc16(frame.data(), 10));
  frame[12] = cmdSet;
  frame[13] = cmdId;
  if (payloadLength > 0 && payload != nullptr)
    memcpy(frame.data() + FRAME_HEADER_LENGTH + FRAME_COMMAND_LENGTH, payload, payloadLength);
  writeLE32(frame.data() + frameLength - FRAME_TAIL_LENGTH,
            crc32(frame.data(), frameLength - FRAME_TAIL_LENGTH));

  const bool result = m_Write->writeValue(frame.data(), frame.size(), m_WriteWithResponse);
  ESP_LOGD(LOG_TAG, "DJI Osmo frame set=0x%02X id=0x%02X type=0x%02X len=%u seq=%u => %s", cmdSet,
           cmdId, cmdType, static_cast<unsigned>(frame.size()), sequence, result ? "ok" : "failed");
  return result;
}

bool DJIOsmo::writeCommand(uint8_t cmdSet,
                           uint8_t cmdId,
                           uint8_t cmdType,
                           const uint8_t *payload,
                           size_t payloadLength) {
  return writeFrame(cmdSet, cmdId, cmdType, payload, payloadLength, nextSequence());
}

bool DJIOsmo::sendConnectionRequest(void) {
  std::array<uint8_t, 33> payload = {};
  writeLE32(payload.data(), m_RemoteDeviceId);
  payload[4] = 6;

  uint8_t bluetoothMac[6] = {};
  if (esp_read_mac(bluetoothMac, ESP_MAC_BT) != ESP_OK) {
    ESP_LOGW(LOG_TAG, "DJI Osmo could not read the Bluetooth MAC address");
    return false;
  }
  memcpy(payload.data() + 5, bluetoothMac, sizeof(bluetoothMac));

  payload[26] = m_PairType == PairType::NEW ? 1 : 0;
  const uint16_t verifyData =
      m_PairType == PairType::NEW ? static_cast<uint16_t>(esp_random() % 10000U) : 0;
  writeLE16(payload.data() + 27, verifyData);
  ESP_LOGI(LOG_TAG, "DJI Osmo protocol request verify_mode=%u verify_data=%u", payload[26],
           verifyData);
  return writeCommand(CMD_SET_COMMON, CMD_CONNECTION, CMD_WAIT_RESULT, payload.data(),
                      payload.size());
}

bool DJIOsmo::finishProtocolConnection(void) {
  if (!sendConnectionRequest())
    return false;

  for (uint32_t elapsed = 0; elapsed < PROTOCOL_HANDSHAKE_TIMEOUT_MS;
       elapsed += PROTOCOL_HANDSHAKE_POLL_MS) {
    bool rejected;
    bool requestReceived;
    {
      const std::lock_guard<std::mutex> lock(m_ProtocolMutex);
      rejected = m_ConnectionRejected;
      requestReceived = m_CameraRequestReceived;
    }
    if (rejected) {
      ESP_LOGW(LOG_TAG, "DJI Osmo camera rejected the protocol connection");
      return false;
    }
    if (requestReceived)
      break;
    vTaskDelay(pdMS_TO_TICKS(PROTOCOL_HANDSHAKE_POLL_MS));
  }

  uint16_t requestSequence;
  uint32_t cameraDeviceId;
  uint8_t verifyMode;
  uint16_t verifyData;
  {
    const std::lock_guard<std::mutex> lock(m_ProtocolMutex);
    if (!m_CameraRequestReceived)
      return false;
    requestSequence = m_CameraRequestSequence;
    cameraDeviceId = m_CameraRequestDeviceId;
    verifyMode = m_CameraVerifyMode;
    verifyData = m_CameraVerifyData;
  }

  if (!supportedCameraId(cameraDeviceId)) {
    ESP_LOGW(LOG_TAG, "DJI Osmo unsupported camera device ID 0x%08" PRIX32, cameraDeviceId);
    return false;
  }
  if (verifyMode != 2 || verifyData != 0) {
    ESP_LOGW(LOG_TAG, "DJI Osmo camera verification failed: mode=%u result=%u", verifyMode,
             verifyData);
    return false;
  }

  std::array<uint8_t, 9> response = {};
  writeLE32(response.data(), m_RemoteDeviceId);
  response[4] = 0;
  if (!writeFrame(CMD_SET_COMMON, CMD_CONNECTION, ACK_NO_RESPONSE, response.data(), response.size(),
                  requestSequence)) {
    return false;
  }

  {
    const std::lock_guard<std::mutex> lock(m_ProtocolMutex);
    m_CameraDeviceId = cameraDeviceId;
    m_ProtocolReady = true;
  }
  ESP_LOGI(LOG_TAG, "DJI Osmo protocol connected to camera device ID 0x%08" PRIX32, cameraDeviceId);
  return true;
}

bool DJIOsmo::subscribeCameraStatus(void) {
  const std::array<uint8_t, 6> payload = {3, 20, 0, 0, 0, 0};
  return writeCommand(CMD_SET_CAMERA, CMD_STATUS_SUBSCRIPTION, CMD_NO_RESPONSE, payload.data(),
                      payload.size());
}

bool DJIOsmo::writeRecordCommand(bool start) {
  uint32_t cameraDeviceId;
  {
    const std::lock_guard<std::mutex> lock(m_ProtocolMutex);
    if (!m_ProtocolReady || m_CameraDeviceId == 0)
      return false;
    cameraDeviceId = m_CameraDeviceId;
  }

  std::array<uint8_t, 9> payload = {};
  writeLE32(payload.data(), cameraDeviceId);
  payload[4] = start ? 0 : 1;
  const bool result =
      writeCommand(CMD_SET_CAMERA, CMD_RECORD, CMD_RESPONSE_OR_NOT, payload.data(), payload.size());
  if (result) {
    const std::lock_guard<std::mutex> lock(m_ProtocolMutex);
    m_Recording = start;
  }
  ESP_LOGI(LOG_TAG, "DJI Osmo %s recording => %s", start ? "start" : "stop",
           result ? "ok" : "failed");
  return result;
}

void DJIOsmo::handleNotification(const uint8_t *data, size_t length) {
  if (!validFrame(data, length)) {
    ESP_LOGW(LOG_TAG, "DJI Osmo rejected an invalid protocol frame");
    return;
  }

  const size_t dataLength = length - FRAME_HEADER_LENGTH - FRAME_TAIL_LENGTH;
  if (dataLength < FRAME_COMMAND_LENGTH)
    return;

  const uint8_t cmdSet = data[FRAME_HEADER_LENGTH];
  const uint8_t cmdId = data[FRAME_HEADER_LENGTH + 1];
  const uint8_t *payload = data + FRAME_HEADER_LENGTH + FRAME_COMMAND_LENGTH;
  const size_t payloadLength = dataLength - FRAME_COMMAND_LENGTH;
  const bool response = (data[3] & FRAME_RESPONSE_BIT) != 0;

  if (cmdSet == CMD_SET_COMMON && cmdId == CMD_CONNECTION) {
    if (response) {
      if (payloadLength >= 5 && payload[4] != 0) {
        const std::lock_guard<std::mutex> lock(m_ProtocolMutex);
        m_ConnectionRejected = true;
      }
      return;
    }

    if (payloadLength < 33)
      return;
    const std::lock_guard<std::mutex> lock(m_ProtocolMutex);
    m_CameraRequestReceived = true;
    m_CameraRequestSequence = readLE16(data + 8);
    m_CameraRequestDeviceId = readLE32(payload);
    m_CameraVerifyMode = payload[26];
    m_CameraVerifyData = readLE16(payload + 27);
    return;
  }

  if (!response && cmdSet == CMD_SET_CAMERA && cmdId == CMD_STATUS_PUSH && payloadLength >= 7) {
    const uint8_t cameraMode = payload[0];
    const uint8_t cameraStatus = payload[1];
    const uint16_t recordTime = readLE16(payload + 5);
    const bool recording = cameraStatus == 0x05 || (cameraStatus == 0x03 && cameraMode != 0x05);
    const std::lock_guard<std::mutex> lock(m_ProtocolMutex);
    m_Recording = recording;
    ESP_LOGD(LOG_TAG, "DJI Osmo mode=0x%02X status=0x%02X record_time=%u recording=%d", cameraMode,
             cameraStatus, recordTime, recording);
  }
}

void DJIOsmo::shutterPress(void) {
  bool start;
  {
    const std::lock_guard<std::mutex> lock(m_ProtocolMutex);
    if (!m_ProtocolReady) {
      ESP_LOGW(LOG_TAG, "DJI Osmo shutter skipped: protocol is not connected");
      return;
    }
    start = !m_Recording;
  }
  writeRecordCommand(start);
}

void DJIOsmo::shutterRelease(void) {
  ESP_LOGD(LOG_TAG, "DJI Osmo shutterRelease ignored");
}

void DJIOsmo::focusPress(void) {
  ESP_LOGD(LOG_TAG, "DJI Osmo focusPress ignored");
}

void DJIOsmo::focusRelease(void) {
  ESP_LOGD(LOG_TAG, "DJI Osmo focusRelease ignored");
}

void DJIOsmo::updateGeoData(const gps_t &gps, const timesync_t &timesync) {
  (void)gps;
  (void)timesync;
}

size_t DJIOsmo::getSerialisedBytes(void) const {
  return sizeof(dji_osmo_t);
}

bool DJIOsmo::serialise(void *buffer, size_t bytes) const {
  if (bytes != sizeof(dji_osmo_t))
    return false;

  dji_osmo_t *x = static_cast<dji_osmo_t *>(buffer);
  strncpy(x->name, m_Name.c_str(), MAX_NAME);
  x->name[MAX_NAME - 1] = '\0';
  x->address = static_cast<uint64_t>(m_Address);
  x->type = m_Address.getType();
  return true;
}

}  // namespace Furble
