#include <NimBLEAdvertisedDevice.h>

#include "Camera.h"
#include "Device.h"

namespace Furble {

Camera::Camera(Type type, PairType pairType) : m_PairType(pairType), m_Type(type) {}

Camera::~Camera() {
  m_Connected = false;
  m_Client = nullptr;
}

void Camera::onConnect(NimBLEClient *pClient) {
  const int8_t requestedDbm = Device::powerLevelToDbm(m_Power);
  // Set BLE transmit power after connection is established. The controller
  // offers no clean per-connection readback, log the request only.
  const bool set = NimBLEDevice::setPower(requestedDbm);
  ESP_LOGI(LOG_TAG, "Connected, transmit power requested %d dBm (level %d), set %s", requestedDbm,
           static_cast<int>(m_Power), set ? "ok" : "failed");
  m_Connected = true;
}

void Camera::onDisconnect(NimBLEClient *pClient, int reason) {
  ESP_LOGI(LOG_TAG, "Disconnected");
  m_Connected = false;
  m_Progress = 0;
}

bool Camera::connect(esp_power_level_t power, uint32_t timeout) {
  const std::lock_guard<std::mutex> lock(m_Mutex);

  m_Power = power;

  m_Client = NimBLEDevice::createClient();
  if (m_Client == nullptr) {
    ESP_LOGI(LOG_TAG, "Failed to create client");
    return false;
  }

  m_Client->setClientCallbacks(this, false);
  m_Client->setSelfDelete(true, true);  // self-delete on any connection failure

  // adjust connection timeout and parameters
  m_Client->setConnectTimeout(timeout);
  // try extending range by adjusting connection parameters
  m_Client->setConnectionParams(m_MinInterval, m_MaxInterval, m_Latency, m_Timeout);

  // set per-camera BLE security before connecting
  NimBLEDevice::setSecurityAuth(true, true, true);
  NimBLEDevice::setSecurityIOCap(static_cast<uint8_t>(securityMode()));

  bool connected = this->_connect();
  if (connected) {
    m_Paired = true;
  } else {
    this->_disconnect();
  }
  NimBLEDevice::setSecurityIOCap(static_cast<uint8_t>(m_SecurityModeDefault));

  return m_Connected;
}

void Camera::disconnect(void) {
  const std::lock_guard<std::mutex> lock(m_Mutex);
  m_Active = false;
  m_Progress = 0;
  this->_disconnect();
}

bool Camera::isActive(void) const {
  return m_Active;
}

void Camera::setActive(bool active) {
  m_Active = active;
}

const Camera::Type &Camera::getType(void) const {
  return m_Type;
}

const std::string &Camera::getName(void) const {
  return m_Name;
}

const NimBLEAddress &Camera::getAddress(void) const {
  return m_Address;
}

uint8_t Camera::getConnectProgress(void) const {
  return m_Progress.load();
}

int8_t Camera::getRssi(void) const {
  const std::lock_guard<std::mutex> lock(m_Mutex);
  if ((m_Type == Type::FAUXNY) || !m_Connected || (m_Client == nullptr)
      || !m_Client->isConnected()) {
    return 0;
  }

  return static_cast<int8_t>(m_Client->getRssi());
}

bool Camera::isConnected(void) const {
  const std::lock_guard<std::mutex> lock(m_Mutex);
  if (m_Type == Type::FAUXNY) {
    return m_Connected;
  }

  return m_Connected && m_Client && m_Client->isConnected();
}

}  // namespace Furble
