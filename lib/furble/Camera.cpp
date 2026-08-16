#include <NimBLEAdvertisedDevice.h>
#include <esp_timer.h>

#include "Camera.h"
#include "Device.h"

namespace Furble {

namespace {
uint32_t connectionTimeMs(void) {
  return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
}
}  // namespace

Camera::Camera(Type type, PairType pairType) : m_PairType(pairType), m_Type(type) {}

Camera::~Camera() {
  m_Connected = false;
  m_Client = nullptr;
}

void Camera::onConnect(NimBLEClient *pClient) {
  (void)pClient;
  const int8_t requestedDbm = Device::powerLevelToDbm(m_Power);
  // Set BLE transmit power after connection is established. The controller
  // offers no clean per-connection readback, log the request only.
  const bool set = NimBLEDevice::setPower(requestedDbm);
  ESP_LOGI(LOG_TAG, "Connected, transmit power requested %d dBm (level %d), set %s", requestedDbm,
           static_cast<int>(m_Power), set ? "ok" : "failed");
  m_Connected = true;

  bool connSaverEnabled;
  {
    const std::lock_guard<std::mutex> lock(m_ConnParamsMutex);
    m_LastConnActivityMs = connectionTimeMs();
    m_ShutterHeld = false;
    m_LastRequestValid = false;
    m_LastRequestSucceeded = false;
    m_PeerOverride = false;
    connSaverEnabled = m_ConnSaverEnabled;
  }

  if (connSaverEnabled) {
    setConnProfile(ConnProfile::IDLE);
  }
}

void Camera::onDisconnect(NimBLEClient *pClient, int reason) {
  (void)pClient;
  (void)reason;
  ESP_LOGI(LOG_TAG, "Disconnected");
  m_Connected = false;
  m_Progress = 0;

  const std::lock_guard<std::mutex> lock(m_ConnParamsMutex);
  m_LastRequestValid = false;
  m_LastRequestSucceeded = false;
  m_PeerOverride = false;
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

void Camera::setConnSaverEnabled(bool enabled) {
  bool connected;
  bool disableWhileConnected;
  {
    const std::lock_guard<std::mutex> lock(m_ConnParamsMutex);
    if (m_ConnSaverEnabled == enabled) {
      return;
    }

    connected = m_Connected && (m_Client != nullptr);
    disableWhileConnected = !enabled && connected;
    // Keep the old enabled state while restoring the fast profile. This lets
    // setConnProfile() issue the final request when the setting is disabled.
    if (!disableWhileConnected) {
      m_ConnSaverEnabled = enabled;
    }
    m_LastConnActivityMs = connectionTimeMs();
    m_ShutterHeld = false;
    m_PeerOverride = false;
  }

  if (connected) {
    // Start a newly enabled saver in the fast profile. The idle timer will
    // move it to the idle profile after a quiet period. When disabling the
    // saver, this is the final fast-profile request before the flag changes.
    setConnProfile(ConnProfile::FAST);
  }

  if (disableWhileConnected) {
    const std::lock_guard<std::mutex> lock(m_ConnParamsMutex);
    m_ConnSaverEnabled = false;
  }
}

void Camera::noteConnActivity(bool held) {
  const std::lock_guard<std::mutex> lock(m_ConnParamsMutex);
  m_LastConnActivityMs = connectionTimeMs();
  m_ShutterHeld = held;
  if (held) {
    // A new user activity cycle is allowed to leave a peer override and ask
    // for the fast profile before the command is written.
    m_PeerOverride = false;
  }
}

void Camera::maybeSetIdle(void) {
  {
    const std::lock_guard<std::mutex> lock(m_ConnParamsMutex);
    if (!m_ConnSaverEnabled || m_ShutterHeld || m_PeerOverride
        || (connectionTimeMs() - m_LastConnActivityMs < m_ConnSaverIdleMs)) {
      return;
    }
  }

  setConnProfile(ConnProfile::IDLE);
}

bool Camera::setConnProfile(ConnProfile profile) {
  if (profile == ConnProfile::PEER) {
    return false;
  }

  NimBLEClient *client;
  uint16_t minInterval;
  uint16_t maxInterval;
  uint16_t latency;
  uint16_t timeout;
  const uint32_t now = connectionTimeMs();

  {
    const std::lock_guard<std::mutex> lock(m_ConnParamsMutex);
    // With the experimental setting off, preserve the original behavior and
    // leave the pre-connect fast parameters untouched.
    if (!m_ConnSaverEnabled && profile == ConnProfile::FAST) {
      return true;
    }
    if ((profile == ConnProfile::IDLE && !m_ConnSaverEnabled) || !m_Connected
        || (m_Client == nullptr)) {
      return false;
    }

    if (profile == ConnProfile::FAST) {
      m_PeerOverride = false;
    } else if (m_PeerOverride) {
      return false;
    }

    if (m_LastRequestValid && (m_LastRequestedProfile == profile)) {
      if (m_LastRequestSucceeded) {
        return true;
      }
      if (now - m_LastRequestMs < m_ConnParamsUpdateGuardMs) {
        return false;
      }
    }

    client = m_Client;
    if (profile == ConnProfile::FAST) {
      minInterval = m_FastMinInterval;
      maxInterval = m_FastMaxInterval;
      latency = m_FastLatency;
      timeout = m_FastTimeout;
    } else {
      minInterval = m_IdleMinInterval;
      maxInterval = m_IdleMaxInterval;
      latency = m_IdleLatency;
      timeout = m_IdleTimeout;
    }

    m_LastRequestedProfile = profile;
    m_LastRequestMs = now;
    m_LastRequestValid = true;
    m_LastRequestSucceeded = false;
  }

  // Do not hold m_ConnParamsMutex while entering NimBLE. The host may invoke
  // the peer request callback as part of this operation.
  const bool updated = client->updateConnParams(minInterval, maxInterval, latency, timeout);

  {
    const std::lock_guard<std::mutex> lock(m_ConnParamsMutex);
    if (!m_PeerOverride) {
      m_LastRequestSucceeded = updated;
    }
  }

  if (updated) {
    ESP_LOGI(LOG_TAG, "Requested %s connection profile (%u-%u, latency %u, timeout %u)",
             connProfileName(profile), minInterval, maxInterval, latency, timeout);
  } else {
    ESP_LOGW(LOG_TAG, "Rejected %s connection profile (%u-%u, latency %u, timeout %u)",
             connProfileName(profile), minInterval, maxInterval, latency, timeout);
  }

  return updated;
}

bool Camera::getConnParams(uint16_t &interval,
                           uint16_t &latency,
                           uint16_t &timeout,
                           int &rssi) const {
  if (m_Client == nullptr || !m_Connected || !m_Client->isConnected()) {
    return false;
  }

  const NimBLEConnInfo info = m_Client->getConnInfo();
  interval = info.getConnInterval();
  latency = info.getConnLatency();
  timeout = info.getConnTimeout();
  rssi = m_Client->getRssi();
  return true;
}

Camera::ConnProfile Camera::getConnProfile(void) const {
  uint16_t interval;
  uint16_t latency;
  uint16_t timeout;
  int rssi;
  if (!getConnParams(interval, latency, timeout, rssi)) {
    return ConnProfile::PEER;
  }

  if ((interval >= m_FastMinInterval) && (interval <= m_FastMaxInterval)
      && (latency == m_FastLatency) && (timeout == m_FastTimeout)) {
    return ConnProfile::FAST;
  }

  if ((interval >= m_IdleMinInterval) && (interval <= m_IdleMaxInterval)
      && (latency == m_IdleLatency) && (timeout == m_IdleTimeout)) {
    return ConnProfile::IDLE;
  }

  return ConnProfile::PEER;
}

const char *Camera::connProfileName(ConnProfile profile) {
  switch (profile) {
    case ConnProfile::FAST:
      return "fast";
    case ConnProfile::IDLE:
      return "idle";
    case ConnProfile::PEER:
      return "peer";
  }
  return "peer";
}

bool Camera::onConnParamsUpdateRequest(NimBLEClient *pClient, const ble_gap_upd_params *params) {
  (void)pClient;
  if (params != nullptr) {
    ESP_LOGI(LOG_TAG, "Peer requested connection parameters (%u-%u, latency %u, timeout %u)",
             params->itvl_min, params->itvl_max, params->latency, params->supervision_timeout);
  }

  const std::lock_guard<std::mutex> lock(m_ConnParamsMutex);
  // Accept the peer values and do not immediately fight them with our own
  // request. A later shutter or focus press starts a new fast-profile cycle.
  m_PeerOverride = true;
  m_LastRequestValid = false;
  m_LastRequestSucceeded = false;
  return true;
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
