#include <NimBLEAdvertisedDevice.h>

#include "Camera.h"

namespace Furble {

namespace {

int8_t powerLevelToDbm(esp_power_level_t power) {
  switch (power) {
    case ESP_PWR_LVL_P3:
      return 3;
    case ESP_PWR_LVL_P6:
      return 6;
    case ESP_PWR_LVL_P9:
      return 9;
    default:
      return 3;
  }
}

}  // namespace

Camera::Camera(Type type, PairType pairType) : m_PairType(pairType), m_Type(type) {}

Camera::~Camera() {
  m_Connected = false;
  m_Client = nullptr;
}

void Camera::onConnect(NimBLEClient *pClient) {
  const int8_t requestedDbm = powerLevelToDbm(m_CurrentPower);
  // Set BLE transmit power after connection is established.
  const bool set = NimBLEDevice::setPower(requestedDbm);
  const int8_t appliedDbm = NimBLEDevice::getPower();
  ESP_LOGI(LOG_TAG, "Connected, transmit power request %d dBm (level %d), applied %d dBm, set %s",
           requestedDbm, static_cast<int>(m_CurrentPower), appliedDbm, set ? "ok" : "failed");
  m_Connected = true;
}

void Camera::onDisconnect(NimBLEClient *pClient, int reason) {
  ESP_LOGI(LOG_TAG, "Disconnected");
  m_Connected = false;
  m_CurrentPower = m_Power;
  m_Progress = 0;
}

bool Camera::connect(esp_power_level_t power, uint32_t timeout) {
  const std::lock_guard<std::mutex> lock(m_Mutex);

  m_Power = power;
  m_CurrentPower = power;

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
  m_CurrentPower = m_Power;
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

esp_power_level_t Camera::getCurrentPower(void) const {
  const std::lock_guard<std::mutex> lock(m_Mutex);
  return m_CurrentPower;
}

void Camera::setCurrentPower(esp_power_level_t power) {
  const std::lock_guard<std::mutex> lock(m_Mutex);
  m_CurrentPower = power;

  if (!m_Connected || (m_Type == Type::FAUXNY)) {
    return;
  }

  const int8_t requestedDbm = powerLevelToDbm(power);
  const bool set = NimBLEDevice::setPower(requestedDbm, NimBLETxPowerType::Connection);
  const int8_t appliedDbm = NimBLEDevice::getPower(NimBLETxPowerType::Connection);
  ESP_LOGI(LOG_TAG, "Adaptive transmit power request %d dBm (level %d), applied %d dBm, set %s",
           requestedDbm, static_cast<int>(power), appliedDbm, set ? "ok" : "failed");
}

bool Camera::isConnected(void) const {
  const std::lock_guard<std::mutex> lock(m_Mutex);
  if (m_Type == Type::FAUXNY) {
    return m_Connected;
  }

  return m_Connected && m_Client && m_Client->isConnected();
}

}  // namespace Furble
