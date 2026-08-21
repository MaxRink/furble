#include <algorithm>
#include <cstring>

#include <NimBLEAdvertisedDevice.h>
#include <NimBLERemoteService.h>
#include <NimBLEUtils.h>
#include <esp_timer.h>

#if defined(FURBLE_CONSOLE)
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>
#endif

#include "Camera.h"
#include "Device.h"

namespace Furble {

namespace {
uint32_t connectionTimeMs(void) {
  return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
}

#if defined(FURBLE_CONSOLE)

enum class journal_direction_t : uint8_t {
  WRITE,
  WRITE_RESPONSE,
  READ,
  NOTIFY,
  INDICATE,
};

constexpr uint8_t JOURNAL_FLAG_OK = 1 << 0;
constexpr uint8_t JOURNAL_FLAG_TRUNCATED = 1 << 1;

#if defined(FURBLE_M5STICKS3)
constexpr size_t JOURNAL_BYTES = 64 * 1024;
constexpr size_t JOURNAL_PAYLOAD_BYTES = 64;
#else
constexpr size_t JOURNAL_BYTES = 8 * 1024;
constexpr size_t JOURNAL_PAYLOAD_BYTES = 32;
#endif

struct journal_record_t {
  uint32_t timestamp_ms;
  journal_direction_t direction;
  uint8_t flags;
  uint16_t length;
  uint8_t service_uuid[16];
  uint8_t characteristic_uuid[16];
  uint8_t payload[JOURNAL_PAYLOAD_BYTES];
};

static_assert(sizeof(journal_record_t) == (40 + JOURNAL_PAYLOAD_BYTES),
              "journal record layout changed");

portMUX_TYPE g_JournalMux = portMUX_INITIALIZER_UNLOCKED;
uint8_t *g_JournalBuffer = nullptr;
size_t g_JournalSlots = 0;
size_t g_JournalCount = 0;
uint64_t g_JournalWriteSequence = 0;
uint64_t g_JournalLiveSequence = 0;
std::atomic_bool g_JournalEnabled = false;

void copyUuid(uint8_t destination[16], const NimBLEUUID &source) {
  memset(destination, 0, 16);
  NimBLEUUID uuid = source;
  if (uuid.bitSize() != 128) {
    uuid.to128();
  }

  const uint8_t *value = uuid.getValue();
  if (value != nullptr) {
    memcpy(destination, value, 16);
  }
}

std::string uuidString(const uint8_t value[16]) {
  return NimBLEUUID(value, 16).toString();
}

const char *journalDirection(journal_direction_t direction) {
  switch (direction) {
    case journal_direction_t::WRITE:
      return "tx";
    case journal_direction_t::WRITE_RESPONSE:
      return "txr";
    case journal_direction_t::READ:
      return "rx";
    case journal_direction_t::NOTIFY:
      return "nfy";
    case journal_direction_t::INDICATE:
      return "ind";
  }
  return "?";
}

void printJournalRecord(const journal_record_t &record) {
  const std::string service = uuidString(record.service_uuid);
  const std::string characteristic = uuidString(record.characteristic_uuid);
  const size_t bytes = std::min<size_t>(record.length, JOURNAL_PAYLOAD_BYTES);
  const std::string payload = NimBLEUtils::dataToHexString(record.payload, bytes);
  printf("bt: %lu %s %s %s %u %s%s\n", static_cast<unsigned long>(record.timestamp_ms),
         journalDirection(record.direction), service.c_str(), characteristic.c_str(),
         static_cast<unsigned>(record.length), payload.c_str(),
         (record.flags & JOURNAL_FLAG_TRUNCATED) ? "..." : "");
}

void journalRecord(journal_direction_t direction,
                   const NimBLEUUID &service,
                   const NimBLEUUID &characteristic,
                   const uint8_t *data,
                   size_t length,
                   bool success) {
  if (!g_JournalEnabled.load()) {
    return;
  }

  journal_record_t record = {};
  record.timestamp_ms = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
  record.direction = direction;
  record.flags = success ? JOURNAL_FLAG_OK : 0;
  record.length = static_cast<uint16_t>(std::min<size_t>(length, UINT16_MAX));
  copyUuid(record.service_uuid, service);
  copyUuid(record.characteristic_uuid, characteristic);

  const size_t bytes = std::min(length, JOURNAL_PAYLOAD_BYTES);
  if (data != nullptr && bytes > 0) {
    memcpy(record.payload, data, bytes);
  }
  if (length > JOURNAL_PAYLOAD_BYTES) {
    record.flags |= JOURNAL_FLAG_TRUNCATED;
  }

  portENTER_CRITICAL(&g_JournalMux);
  if (g_JournalEnabled.load() && (g_JournalBuffer != nullptr) && (g_JournalSlots > 0)) {
    const size_t slot = g_JournalWriteSequence % g_JournalSlots;
    memcpy(g_JournalBuffer + (slot * sizeof(record)), &record, sizeof(record));
    g_JournalWriteSequence++;
    if (g_JournalCount < g_JournalSlots) {
      g_JournalCount++;
    }
  }
  portEXIT_CRITICAL(&g_JournalMux);
}

#endif
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

  const std::lock_guard<std::mutex> lock(m_ConnParamsMutex);
  m_LastConnActivityMs = connectionTimeMs();
  m_ShutterHeld = false;
  m_LastRequestValid = false;
  m_LastRequestSucceeded = false;
  m_PeerOverride = false;
  // Never request the idle profile here. Connection setup still has to run
  // service discovery and subscriptions at the fast interval; the inactivity
  // timer moves the link to idle once the connect has finished.
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
  m_StatsValid = false;
}

bool Camera::connect(esp_power_level_t power, uint32_t timeout) {
  const std::lock_guard<std::mutex> lock(m_Mutex);

  {
    // Gate the idle profile out for the whole connection attempt. Discovery
    // and subscription round trips at the idle interval would stretch a two
    // second connect into minutes.
    const std::lock_guard<std::mutex> params(m_ConnParamsMutex);
    m_ConnectInProgress = true;
  }

  m_Power = power;

  m_Client = NimBLEDevice::createClient();
  if (m_Client == nullptr) {
    ESP_LOGI(LOG_TAG, "Failed to create client");
    const std::lock_guard<std::mutex> params(m_ConnParamsMutex);
    m_ConnectInProgress = false;
    return false;
  }

  // Pool accounting for the NimBLE client leak hunt. The pool is fixed size
  // (CONFIG_BT_NIMBLE_MAX_CONNECTIONS), so a count that climbs across failed
  // connects is the leak signature. DEBUG level, compiled out of release.
  ESP_LOGD(LOG_TAG, "createClient(%s), pool now %u", m_Name.c_str(),
           static_cast<unsigned>(NimBLEDevice::getCreatedClientCount()));

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
  } else if (m_Connected) {
    // The link came up (onConnect set m_Connected) but a later registration step
    // failed, so _connect() returned false. Tear down the live link. The
    // disconnect callback self-deletes the client (deleteOnDisconnect), so do
    // not delete it here.
    this->_disconnect();
    // Do not report this half-open session as connected: connect() returns
    // m_Connected below and the caller treats a true return as success, which
    // would leave a stale connected flag on the persistent CameraList camera and
    // short-circuit the next connect. The async terminate above still runs;
    // onDisconnect will also clear the flag once it fires, but the camera may
    // already be gone, so clear it now. This is safe: every m_Client deref is
    // guarded by m_Connected, so clearing it only removes access, it never races
    // the self-delete.
    m_Connected = false;
  } else {
    // The attempt failed before the link ever came up, so onConnect never ran
    // and m_Connected is false. Reclaim the client here so it does not leak from
    // the fixed-size NimBLE pool (CONFIG_BT_NIMBLE_MAX_CONNECTIONS). Two failure
    // classes reach this branch, and NimBLEDevice::deleteClient() is safe for
    // both because it first checks the client is still in the live client list:
    //
    //   - A vendor _connect() returned before NimBLEClient::connect() was ever
    //     called. On a Fujifilm reconnect the camera still holds its previous
    //     session, so the pairing scan times out and _connect() returns false
    //     with no connection procedure started. No connect or disconnect event
    //     can fire for a client that never linked, so setSelfDelete never runs
    //     and the client is orphaned. deleteClient() reclaims it. This is the
    //     leak that exhausted the pool after nine failed reconnects and broke
    //     every later connect ("Unable to create client; already at max: 9")
    //     until a reboot.
    //
    //   - NimBLEClient::connect() was reached and failed. setSelfDelete already
    //     freed the client (deleteOnConnectFail), so it is no longer in the live
    //     list and deleteClient() is a no-op. No double free.
    //
    // Clearing m_Client afterwards is safe because every deref is guarded by
    // m_Connected, which is false here.
    NimBLEDevice::deleteClient(m_Client);
    m_Client = nullptr;
    ESP_LOGD(LOG_TAG, "deleteClient(%s) after failed connect, pool now %u", m_Name.c_str(),
             static_cast<unsigned>(NimBLEDevice::getCreatedClientCount()));
  }
  NimBLEDevice::setSecurityIOCap(static_cast<uint8_t>(m_SecurityModeDefault));

  {
    const std::lock_guard<std::mutex> params(m_ConnParamsMutex);
    m_ConnectInProgress = false;
    // Restart the inactivity timer so the idle countdown begins at connect
    // completion, not at the connection event mid-setup.
    m_LastConnActivityMs = connectionTimeMs();
  }

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
    if (!m_ConnSaverEnabled || m_ShutterHeld || m_PeerOverride || m_ConnectInProgress
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
      // Only idle requests honour the retry guard. A failed fast request must
      // stay retryable on the next press: the host returns BLE_HS_EALREADY
      // while an idle update is still in flight, and latching that failure
      // for the guard period would leave the link slow when the user is
      // actively shooting.
      if ((profile == ConnProfile::IDLE) && (now - m_LastRequestMs < m_ConnParamsUpdateGuardMs)) {
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
  //
  // Mirror the requested profile into the client wrapper first. NimBLE fills
  // the counter-proposal in a peer CONN_UPDATE_REQ from those stored values,
  // so without this a renegotiating camera would be answered with the stale
  // pre-connect fast parameters and the idle profile would never stick.
  client->setConnectionParams(minInterval, maxInterval, latency, timeout);
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

void Camera::updateConnStats(void) {
  {
    const std::lock_guard<std::mutex> params(m_ConnParamsMutex);
    if (connectionTimeMs() - m_LastStatsMs < m_ConnStatsIntervalMs) {
      return;
    }
  }

  // m_Mutex guards m_Client against the connect and disconnect paths. Use
  // try_lock so this sampler never stalls behind a connection attempt; the
  // previous snapshot simply stays in place.
  const std::unique_lock<std::mutex> lock(m_Mutex, std::try_to_lock);
  if (!lock.owns_lock()) {
    return;
  }

  const bool valid = m_Connected && (m_Client != nullptr) && m_Client->isConnected();
  uint16_t interval = 0;
  uint16_t latency = 0;
  uint16_t timeout = 0;
  int rssi = 0;
  if (valid) {
    const NimBLEConnInfo info = m_Client->getConnInfo();
    interval = info.getConnInterval();
    latency = info.getConnLatency();
    timeout = info.getConnTimeout();
    rssi = m_Client->getRssi();
  }

  const std::lock_guard<std::mutex> params(m_ConnParamsMutex);
  m_LastStatsMs = connectionTimeMs();
  m_StatsValid = valid;
  m_StatsInterval = interval;
  m_StatsLatency = latency;
  m_StatsTimeout = timeout;
  m_StatsRssi = rssi;
}

bool Camera::getConnParams(uint16_t &interval,
                           uint16_t &latency,
                           uint16_t &timeout,
                           int &rssi) const {
  const std::lock_guard<std::mutex> lock(m_ConnParamsMutex);
  if (!m_StatsValid) {
    return false;
  }

  interval = m_StatsInterval;
  latency = m_StatsLatency;
  timeout = m_StatsTimeout;
  rssi = m_StatsRssi;
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

bool Camera::gattWrite(NimBLERemoteCharacteristic *characteristic,
                       const uint8_t *data,
                       size_t length,
                       bool response) {
  if (characteristic == nullptr) {
    return false;
  }

  const bool result = characteristic->writeValue(data, length, response);

#if defined(FURBLE_CONSOLE)
  const NimBLERemoteService *service = characteristic->getRemoteService();
  const NimBLEUUID empty;
  journalRecord(response ? journal_direction_t::WRITE_RESPONSE : journal_direction_t::WRITE,
                service != nullptr ? service->getUUID() : empty, characteristic->getUUID(), data,
                length, result);
#endif

  return result;
}

bool Camera::gattWrite(const NimBLEUUID &service,
                       const NimBLEUUID &characteristic,
                       const uint8_t *data,
                       size_t length,
                       bool response) {
  const NimBLEAttValue value(data, static_cast<uint16_t>(length));
  return gattWrite(service, characteristic, value, response);
}

bool Camera::gattWrite(const NimBLEUUID &service,
                       const NimBLEUUID &characteristic,
                       const NimBLEAttValue &value,
                       bool response) {
  if (m_Client == nullptr) {
    return false;
  }

  const bool result = m_Client->setValue(service, characteristic, value, response);

#if defined(FURBLE_CONSOLE)
  journalRecord(response ? journal_direction_t::WRITE_RESPONSE : journal_direction_t::WRITE,
                service, characteristic, value.data(), value.size(), result);
#endif

  return result;
}

bool Camera::gattRead(NimBLERemoteCharacteristic *characteristic, NimBLEAttValue &value) {
  if (characteristic == nullptr) {
    value = NimBLEAttValue();
    return false;
  }

  value = characteristic->readValue();

#if defined(FURBLE_CONSOLE)
  const NimBLERemoteService *service = characteristic->getRemoteService();
  const NimBLEUUID empty;
  journalRecord(journal_direction_t::READ, service != nullptr ? service->getUUID() : empty,
                characteristic->getUUID(), value.data(), value.size(), true);
#endif

  return true;
}

bool Camera::gattRead(const NimBLEUUID &service,
                      const NimBLEUUID &characteristic,
                      NimBLEAttValue &value) {
  if (m_Client == nullptr) {
    value = NimBLEAttValue();
    return false;
  }

  value = m_Client->getValue(service, characteristic);

#if defined(FURBLE_CONSOLE)
  journalRecord(journal_direction_t::READ, service, characteristic, value.data(), value.size(),
                true);
#endif

  return true;
}

bool Camera::gattSubscribe(NimBLERemoteCharacteristic *characteristic,
                           gatt_notify_cb callback,
                           bool indicate,
                           bool response) {
  if (characteristic == nullptr) {
    return false;
  }

  return characteristic->subscribe(
      !indicate,
      [callback](NimBLERemoteCharacteristic *remoteCharacteristic, uint8_t *data, size_t length,
                 bool isNotify) {
#if defined(FURBLE_CONSOLE)
        const NimBLERemoteService *service = remoteCharacteristic->getRemoteService();
        const NimBLEUUID empty;
        journalRecord(isNotify ? journal_direction_t::NOTIFY : journal_direction_t::INDICATE,
                      service != nullptr ? service->getUUID() : empty,
                      remoteCharacteristic->getUUID(), data, length, true);
#endif
        if (callback) {
          callback(remoteCharacteristic, data, length, isNotify);
        }
      },
      response);
}

#if defined(FURBLE_CONSOLE)

bool Camera::gattJournalSetEnabled(bool enabled) {
  if (!enabled) {
    g_JournalEnabled.store(false);
    portENTER_CRITICAL(&g_JournalMux);
    g_JournalLiveSequence = g_JournalWriteSequence;
    portEXIT_CRITICAL(&g_JournalMux);
    return true;
  }

  if (g_JournalBuffer == nullptr) {
    const size_t bytes = JOURNAL_BYTES - (JOURNAL_BYTES % sizeof(journal_record_t));
#if defined(FURBLE_M5STICKS3)
    uint8_t *buffer = static_cast<uint8_t *>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM));
#else
    uint8_t *buffer =
        static_cast<uint8_t *>(heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
#endif
    if (buffer == nullptr) {
      return false;
    }

    portENTER_CRITICAL(&g_JournalMux);
    if (g_JournalBuffer == nullptr) {
      g_JournalBuffer = buffer;
      g_JournalSlots = bytes / sizeof(journal_record_t);
    } else {
      heap_caps_free(buffer);
    }
    portEXIT_CRITICAL(&g_JournalMux);
  }

  g_JournalEnabled.store(true);
  return true;
}

void Camera::gattJournalDrain(void) {
  while (true) {
    journal_record_t record = {};
    bool lost = false;
    bool haveRecord = false;

    portENTER_CRITICAL(&g_JournalMux);
    const uint64_t oldest = g_JournalWriteSequence - g_JournalCount;
    if (g_JournalLiveSequence < oldest) {
      g_JournalLiveSequence = oldest;
      lost = true;
    }
    if (g_JournalLiveSequence < g_JournalWriteSequence) {
      const size_t slot = g_JournalLiveSequence % g_JournalSlots;
      memcpy(&record, g_JournalBuffer + (slot * sizeof(record)), sizeof(record));
      g_JournalLiveSequence++;
      haveRecord = true;
    }
    portEXIT_CRITICAL(&g_JournalMux);

    if (lost) {
      printf("bt: journal_lost true\n");
    }
    if (!haveRecord) {
      return;
    }
    printJournalRecord(record);
  }
}

void Camera::gattJournalDump(size_t count) {
  uint64_t start;
  uint64_t end;

  portENTER_CRITICAL(&g_JournalMux);
  end = g_JournalWriteSequence;
  const size_t available = g_JournalCount;
  if (count == 0 || count > available) {
    count = available;
  }
  start = end - count;
  const bool ready = (g_JournalBuffer != nullptr) && (g_JournalSlots > 0);
  portEXIT_CRITICAL(&g_JournalMux);

  if (!ready) {
    return;
  }

  for (uint64_t sequence = start; sequence < end; sequence++) {
    journal_record_t record = {};
    bool haveRecord = false;
    portENTER_CRITICAL(&g_JournalMux);
    if (sequence >= (g_JournalWriteSequence - g_JournalCount)
        && sequence < g_JournalWriteSequence) {
      const size_t slot = sequence % g_JournalSlots;
      memcpy(&record, g_JournalBuffer + (slot * sizeof(record)), sizeof(record));
      haveRecord = true;
    }
    portEXIT_CRITICAL(&g_JournalMux);
    if (haveRecord) {
      printJournalRecord(record);
    }
  }
}

void Camera::gattJournalClear(void) {
  portENTER_CRITICAL(&g_JournalMux);
  g_JournalCount = 0;
  g_JournalWriteSequence = 0;
  g_JournalLiveSequence = 0;
  portEXIT_CRITICAL(&g_JournalMux);
}

#endif

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
  // Only tear down a live link. When m_Connected is false the NimBLE client has
  // already self-deleted (setSelfDelete on disconnect or on a failed connect)
  // and m_Client is a dangling pointer. A target task force-completed during a
  // connect abort reaches here after the aborted connect freed the client, so
  // calling _disconnect() unconditionally would dereference freed memory. Every
  // live-link deref of m_Client (isConnected, getRssi, setConnProfile,
  // updateConnStats) is likewise gated on m_Connected, which the disconnect
  // callback clears before NimBLE frees the client.
  if (m_Connected) {
    this->_disconnect();
  }
}

void Camera::resetConnectionState(void) {
  // Drop a stale connected flag left by a prior session. onDisconnect is the
  // normal clearer, but it can be missed: a powered-off camera delays it to the
  // supervision timeout, and a session torn down through Control::disconnect()
  // does not clear the flag on the persistent CameraList object. Clearing it
  // here, on a fresh connect request, guarantees connectAll() does not skip a
  // camera it wrongly believes is still connected.
  //
  // Deliberately lock-free: this runs on the UI task from Control::addActive(),
  // and taking m_Mutex would block behind an in-flight cold connect that holds
  // it across the ~60 s scan, which is the same watchdog starvation isConnected()
  // avoids. m_Connected and m_Progress are atomic and m_Client is never touched,
  // so no lock is needed for them.
  m_Connected = false;
  m_Progress = 0;

  const std::lock_guard<std::mutex> params(m_ConnParamsMutex);
  m_StatsValid = false;
}

void Camera::reclaimClient(void) {
  const std::lock_guard<std::mutex> lock(m_Mutex);

  // A gone peer whose ble_gap_terminate stalled: the link is still locally
  // connected, so onDisconnect has not fired and will not until the supervision
  // timeout finally resolves it, seconds later. Reclaim the client here so the
  // teardown completes and it does not leak from the fixed NimBLE pool
  // (CONFIG_BT_NIMBLE_MAX_CONNECTIONS).
  //
  // Detach this Camera from the client first. NimBLEDevice::deleteClient() on a
  // still-connected client does not free synchronously: it sets deleteOnDisconnect
  // and re-issues the terminate, so the client outlives this Camera while still
  // holding the raw callback pointer set at connect (setClientCallbacks(this)).
  // This Camera is freed as soon as the drained target is reaped and the next
  // connect rebuilds the CameraList, so the late onDisconnect fired when the
  // stalled terminate finally resolves would call through a dangling pointer.
  // setClientCallbacks(nullptr) points the client at NimBLE's default no-op
  // callbacks, so that late event is harmless. deleteClient() then frees the
  // client now if it is already down or on the deferred disconnect otherwise, and
  // is a no-op (no double free) if it already self-deleted. m_Connected guards
  // every live-link m_Client dereference and we clear it here, so freeing cannot
  // race a reader.
  if (m_Client != nullptr) {
    m_Client->setClientCallbacks(nullptr, false);
    NimBLEDevice::deleteClient(m_Client);
    m_Client = nullptr;
  }
  m_Connected = false;
  m_Progress = 0;
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
  // Lock-free status read: return the cached flag with no m_Mutex and no
  // m_Client dereference. The UI task calls this on every status render. Taking
  // m_Mutex here was the first-connect watchdog reset: a cold connect holds
  // m_Mutex across the secure scan and connect timeout (up to ~60 s), the UI
  // task blocked on the same mutex, and the M5PM1 watchdog fed from the UI task
  // starved until the device reset.
  //
  // Dereferencing m_Client here would be a use-after-free: NimBLE frees the
  // self-deleting client on the host task inside the disconnect callback and on
  // a failed connect, so a lock-free reader can race that free. A live-client
  // cross-check would also give no real benefit: on a silent drop (camera off)
  // the local client still reports connected until the supervision timeout
  // fires, so it cannot detect a dead link any earlier than the flag does once
  // the supervision timeout is capped (see m_IdleTimeout). Correctness therefore
  // depends on m_Connected being cleared on every teardown path, which
  // onDisconnect, the connect failure branch, and resetConnectionState() do.
  return m_Connected.load();
}

}  // namespace Furble
