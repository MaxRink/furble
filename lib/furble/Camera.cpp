#include <algorithm>
#include <cstring>

#include <NimBLEAdvertisedDevice.h>
#include <NimBLERemoteService.h>
#include <NimBLEUtils.h>
#include <esp_timer.h>

#include "BtDebugJournal.h"
#include "Camera.h"
#include "Device.h"

namespace Furble {

namespace {
uint32_t connectionTimeMs(void) {
  return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
}

#if defined(FURBLE_CONSOLE)
void copyText(char *destination, size_t bytes, const std::string &source) {
  if (bytes == 0) {
    return;
  }
  const size_t length = std::min(bytes - 1, source.size());
  memcpy(destination, source.data(), length);
  destination[length] = '\0';
}

std::string payloadHex(const uint8_t *data, size_t length) {
  static constexpr char HEX[] = "0123456789abcdef";
  if (data == nullptr || length == 0) {
    return "<empty>";
  }
  std::string result;
  result.reserve(length * 2);
  for (size_t index = 0; index < length; ++index) {
    result.push_back(HEX[data[index] >> 4]);
    result.push_back(HEX[data[index] & 0x0f]);
  }
  return result;
}

void printJournalEvent(const BtDebugEvent &event, void *) {
  printf(
      "bt.event: seq:%llu session:%u attempt:%u time:%llu kind:%u phase:%s op:%s result:%s "
      "duration_ms:%u reason:%ld(%s) gen:%llu ",
      static_cast<unsigned long long>(event.sequence), static_cast<unsigned>(event.session_id),
      static_cast<unsigned>(event.attempt_id), static_cast<unsigned long long>(event.timestamp_ms),
      static_cast<unsigned>(event.kind), event.begin ? "begin" : "end", event.operation,
      event.result, static_cast<unsigned>(event.duration_ms), static_cast<long>(event.reason),
      event.reason_text, static_cast<unsigned long long>(event.generation));
  if (event.address[0] != '\0') {
    printf("addr:%s type:%u ", event.address, static_cast<unsigned>(event.address_type));
  }
  if (event.identity[0] != '\0') {
    printf("identity:%s type:%u ", event.identity, static_cast<unsigned>(event.identity_type));
  }
  if (event.service_uuid[0] != '\0' || event.characteristic_uuid[0] != '\0') {
    printf("svc:%s chr:%s response:%s ", event.service_uuid, event.characteristic_uuid,
           event.response ? "true" : "false");
  }
  if (event.interval_before != 0 || event.interval_after != 0) {
    printf("conn:%u/%u/%u->%u/%u/%u ", static_cast<unsigned>(event.interval_before),
           static_cast<unsigned>(event.latency_before), static_cast<unsigned>(event.timeout_before),
           static_cast<unsigned>(event.interval_after), static_cast<unsigned>(event.latency_after),
           static_cast<unsigned>(event.timeout_after));
  }
  if (event.state[0] != '\0') {
    printf("state:%s owner:%s physical:%s logical:%s ", event.state, event.owner,
           event.physical ? "true" : "false", event.logical ? "true" : "false");
  }
  const size_t payload_bytes = std::min<size_t>(event.payload_length, sizeof(event.payload));
  printf("security:%s/%s/%s key:%u payload_len:%u payload:%s%s rssi:%d name:%s mfr:%s\n",
         event.encrypted ? "encrypted" : "open",
         event.authenticated ? "authenticated" : "unauthenticated",
         event.bonded ? "bonded" : "unbonded", static_cast<unsigned>(event.key_size),
         static_cast<unsigned>(event.payload_length),
         payloadHex(event.payload, payload_bytes).c_str(), event.payload_truncated ? "..." : "",
         static_cast<int>(event.rssi), event.name, event.manufacturer);
}

void journalRecord(const char *operation,
                   const NimBLEUUID &service,
                   const NimBLEUUID &characteristic,
                   const uint8_t *data,
                   size_t length,
                   bool success,
                   bool response,
                   uint64_t started_us,
                   uint32_t attempt_id = 0,
                   bool begin = false) {
  BtDebugEvent event;
  event.timestamp_ms = static_cast<uint64_t>(esp_timer_get_time()) / 1000ULL;
  event.kind = BtDebugEventKind::GATT;
  event.success = success;
  event.response = response;
  event.attempt_id = attempt_id;
  event.begin = begin;
  event.duration_ms = static_cast<uint16_t>(std::min<uint64_t>(
      (static_cast<uint64_t>(esp_timer_get_time()) - started_us) / 1000ULL, UINT16_MAX));
  copyText(event.service_uuid, sizeof(event.service_uuid), service.toString());
  copyText(event.characteristic_uuid, sizeof(event.characteristic_uuid), characteristic.toString());
  copyText(event.operation, sizeof(event.operation), operation);
  copyText(event.result, sizeof(event.result), begin ? "begin" : (success ? "ok" : "failed"));
  event.payload_truncated = length > sizeof(event.payload);
  event.payload_length = static_cast<uint16_t>(std::min(length, static_cast<size_t>(UINT16_MAX)));
  const size_t payload_bytes = std::min(length, sizeof(event.payload));
  if (data != nullptr && payload_bytes != 0) {
    memcpy(event.payload, data, payload_bytes);
  }
  BtDebugJournal::instance().record(event);
}

#endif
}  // namespace

Camera::Camera(Type type, PairType pairType) : m_PairType(pairType), m_Type(type) {}

Camera::~Camera() {
  m_Connected = false;
  m_Client = nullptr;
}

void Camera::onConnect(NimBLEClient *pClient) {
#if defined(FURBLE_CONSOLE)
  BtDebugEvent event;
  event.timestamp_ms = static_cast<uint64_t>(esp_timer_get_time()) / 1000ULL;
  event.kind = BtDebugEventKind::GAP_CONNECT;
  event.success = true;
  event.attempt_id = m_DebugAttemptId;
  event.address_type = m_Address.getType();
  if (pClient != nullptr) {
    const NimBLEConnInfo info = pClient->getConnInfo();
    event.identity_type = info.getIdAddress().getType();
    copyText(event.address, sizeof(event.address), info.getAddress().toString());
    copyText(event.identity, sizeof(event.identity), info.getIdAddress().toString());
  }
  copyText(event.operation, sizeof(event.operation), "connect");
  copyText(event.result, sizeof(event.result), "ok");
  copyText(event.reason_text, sizeof(event.reason_text), "requested/negotiated");
  BtDebugJournal::instance().record(event);
#endif
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
#if defined(FURBLE_CONSOLE)
  // NimBLE can invoke this callback after it has removed the GAP connection
  // record.  Do not call getConnInfo() here: besides being unavailable at
  // that point, it emits a misleading "Connection info not found" diagnostic.
  // The requested address and attempt id were captured by the connect event,
  // so retain that stable identity for lifecycle correlation.
  (void)pClient;
  BtDebugEvent event;
  event.timestamp_ms = static_cast<uint64_t>(esp_timer_get_time()) / 1000ULL;
  event.kind = BtDebugEventKind::GAP_DISCONNECT;
  event.success = true;
  event.attempt_id = m_DebugAttemptId;
  event.reason = reason;
  event.address_type = m_Address.getType();
  copyText(event.address, sizeof(event.address), m_Address.toString());
  copyText(event.operation, sizeof(event.operation), "disconnect");
  copyText(event.result, sizeof(event.result), "ok");
  copyText(event.reason_text, sizeof(event.reason_text), btGapReasonName(reason));
  BtDebugJournal::instance().record(event);
#endif
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

#if defined(FURBLE_CONSOLE)
  m_DebugAttemptId = BtDebugJournal::instance().nextAttempt();
#endif

  {
    // Gate the idle profile out for the whole connection attempt. Discovery
    // and subscription round trips at the idle interval would stretch a two
    // second connect into minutes.
    const std::lock_guard<std::mutex> params(m_ConnParamsMutex);
    m_ConnectInProgress = true;
    m_FujifilmSecureRegistration = false;
  }

  m_Power = power;
  m_ClientDeleteOnDisconnect = false;
  // A verdict from the previous attempt never carries into this one. Control
  // reads the flag only after connect() has returned, so clearing it here is
  // always after the reader that mattered.
  m_NeedsRepair = false;

  m_Client = NimBLEDevice::createClient();
  if (m_Client == nullptr) {
    ESP_LOGI(LOG_TAG, "Failed to create client");
#if defined(FURBLE_CONSOLE)
    BtDebugEvent event;
    event.timestamp_ms = static_cast<uint64_t>(esp_timer_get_time()) / 1000ULL;
    event.kind = BtDebugEventKind::GAP_CONNECT_FAILED;
    event.attempt_id = m_DebugAttemptId;
    event.address_type = m_Address.getType();
    copyText(event.address, sizeof(event.address), m_Address.toString());
    copyText(event.operation, sizeof(event.operation), "connect");
    copyText(event.result, sizeof(event.result), "no-client");
    BtDebugJournal::instance().record(event);
#endif
    const std::lock_guard<std::mutex> params(m_ConnParamsMutex);
    m_ConnectInProgress = false;
    m_FujifilmSecureRegistration = false;
    return false;
  }

  // Pool accounting for the NimBLE client leak hunt. The pool is fixed size
  // (CONFIG_BT_NIMBLE_MAX_CONNECTIONS), so a count that climbs across failed
  // connects is the leak signature. DEBUG level, compiled out of release.
  ESP_LOGD(LOG_TAG, "createClient(%s), pool now %u", m_Name.c_str(),
           static_cast<unsigned>(NimBLEDevice::getCreatedClientCount()));

  m_Client->setClientCallbacks(this, false);
  // Own the client lifetime for the whole connect attempt. Do not let NimBLE
  // self-delete it: a peer that resets mid-connect (the camera power-cycled
  // during the handshake) fires onDisconnect on the NimBLE host task, and a
  // self-deleting client is freed there while _connect() still runs on this
  // task and keeps dereferencing the client through service discovery and the
  // pairing writes. That cross-task free of a client still in use was the
  // mid-connect crash. With self-delete off across _connect() the client stays
  // valid until _connect() returns, and this task reclaims it deterministically
  // below. On success self-delete is restored so the live session tears down
  // through onDisconnect exactly as before.
  m_Client->setSelfDelete(false, false);

  {
    // Publish the client to the cancel path for the duration of the attempt.
    const std::lock_guard<std::mutex> cancel(m_CancelMutex);
    m_CancelClient = m_Client;
  }

  // adjust connection timeout and parameters
  m_Client->setConnectTimeout(timeout);
  // try extending range by adjusting connection parameters
  m_Client->setConnectionParams(m_MinInterval, m_MaxInterval, m_Latency, m_Timeout);

  // set per-camera BLE security before connecting
  NimBLEDevice::setSecurityAuth(true, true, true);
  NimBLEDevice::setSecurityIOCap(static_cast<uint8_t>(securityMode()));

  bool connected = this->_connect();

  {
    // Withdraw the client before any teardown below can free it. A cancel that
    // arrives after this point finds nothing to terminate, which is correct:
    // the attempt has already unwound.
    const std::lock_guard<std::mutex> cancel(m_CancelMutex);
    m_CancelClient = nullptr;
  }

  if (connected) {
    m_Paired = true;
    // The session is live. Restore self-delete so a later peer disconnect frees
    // the client through onDisconnect, the unchanged runtime teardown path.
    m_Client->setSelfDelete(true, true);
  } else {
    // The connect failed. One failure path is the mid-connect crash this fix
    // targets: the peer reset during the handshake, so onDisconnect fired and
    // cleared m_Connected while _connect() was still running. Two others reach
    // here too: the link never came up (a stale-session reconnect whose pairing
    // scan timed out before NimBLEClient::connect() was even called), or a
    // registration step failed on a live link (half-open). Reclaim the client
    // here, on this task, so no async self-delete can race a reader.
    //
    // Tear down a still-live link first, then detach this Camera from the client
    // so a late onDisconnect from that terminate lands on NimBLE's no-op
    // callbacks (the #128 reclaim pattern), then free it. deleteClient() frees
    // now if the link is already down, or on the deferred disconnect otherwise,
    // and is a safe no-op if the client somehow already went away. Every
    // live-link m_Client deref is guarded by m_Connected, which is false below,
    // so clearing m_Client cannot race a reader.
    const bool liveClient = m_Connected && (m_Client != nullptr) && m_Client->isConnected();
    if (liveClient) {
      // Tear the link down and clear vendor state while self-delete is still
      // off. A secure timeout that lands here can leave the controller's
      // disconnect event already queued on the host task. Arming self-delete
      // before _disconnect() hands that event the free, and the host task can
      // free the client while _disconnect() is still dereferencing it, the
      // Ricoh secure-timeout LoadProhibited. With self-delete off the queued
      // event only clears m_Connected, so _disconnect() runs on a valid
      // client.
      this->_disconnect();
      if (m_Connected) {
        // onDisconnect has not run, so the disconnect event is still in
        // flight (a link our terminate is taking down, or a dead link whose
        // queued event the host has not consumed yet). Arm the callback-side
        // deletion so the client frees through onDisconnect exactly as
        // before. This arm is the last touch of the client on this task:
        // NimBLE may free it immediately after the callback. When
        // onDisconnect already ran no further event will fire; the client is
        // reclaimed below through deleteClient() instead.
        m_ClientDeleteOnDisconnect = true;
        m_Client->setSelfDelete(true, false);
      }
    } else if (m_Connected) {
      this->_disconnect();
    }
    m_Connected = false;
    if (m_ClientDeleteOnDisconnect.exchange(false)) {
      // The client owns its callback-side deletion. Do not touch it after the
      // terminate, as NimBLE may free it immediately after onDisconnect.
      m_Client = nullptr;
    } else if (m_Client != nullptr) {
      m_Client->setClientCallbacks(nullptr, false);
      NimBLEDevice::deleteClient(m_Client);
    }
    m_Client = nullptr;
    ESP_LOGD(LOG_TAG, "deleteClient(%s) after failed connect, pool now %u", m_Name.c_str(),
             static_cast<unsigned>(NimBLEDevice::getCreatedClientCount()));
  }
  NimBLEDevice::setSecurityIOCap(static_cast<uint8_t>(m_SecurityModeDefault));

  if (!m_Connected) {
    ESP_LOGW(LOG_TAG, "Camera connect failed for %s", m_Address.toString().c_str());
#if defined(FURBLE_CONSOLE)
    BtDebugEvent event;
    event.timestamp_ms = static_cast<uint64_t>(esp_timer_get_time()) / 1000ULL;
    event.kind = BtDebugEventKind::GAP_CONNECT_FAILED;
    event.attempt_id = m_DebugAttemptId;
    event.address_type = m_Address.getType();
    copyText(event.address, sizeof(event.address), m_Address.toString());
    copyText(event.operation, sizeof(event.operation), "connect");
    copyText(event.result, sizeof(event.result), "handshake");
    BtDebugJournal::instance().record(event);
#endif
  }

  {
    const std::lock_guard<std::mutex> params(m_ConnParamsMutex);
    m_ConnectInProgress = false;
    m_FujifilmSecureRegistration = false;
    // Restart the inactivity timer so the idle countdown begins at connect
    // completion, not at the connection event mid-setup.
    m_LastConnActivityMs = connectionTimeMs();
  }

  return m_Connected;
}

void Camera::setFujifilmSecureRegistration(bool in_progress) {
  const std::lock_guard<std::mutex> lock(m_ConnParamsMutex);
  m_FujifilmSecureRegistration = in_progress;
}

bool Camera::requestFujifilmSecureFastProfile() {
  if (m_Client == nullptr || !m_Connected) {
    return false;
  }

  // The Secure peer may have installed its long registration timeout. NimBLE
  // only queues this update. FujifilmSecure waits separately for the controller
  // to expose the exact live profile.
  m_Client->setConnectionParams(m_FastMinInterval, m_FastMaxInterval, m_FastLatency, m_FastTimeout);
  if (!m_Client->updateConnParams(m_FastMinInterval, m_FastMaxInterval, m_FastLatency,
                                  m_FastTimeout)) {
    ESP_LOGW(LOG_TAG, "Fujifilm Secure fast connection profile was rejected");
    return false;
  }
  return true;
}

bool Camera::confirmFujifilmSecureFastProfile() {
  if (!m_Connected || (m_Client == nullptr) || !m_Client->isConnected()) {
    return false;
  }

  const NimBLEConnInfo info = m_Client->getConnInfo();
  const bool applied =
      (info.getConnInterval() >= m_FastMinInterval) && (info.getConnInterval() <= m_FastMaxInterval)
      && (info.getConnLatency() == m_FastLatency) && (info.getConnTimeout() == m_FastTimeout);
  if (!applied) {
    return false;
  }

  const std::lock_guard<std::mutex> lock(m_ConnParamsMutex);
  // The accepted registration request marks the live profile as peer-owned.
  // Once our exact FAST profile is confirmed, return ownership to the saver so
  // its normal inactivity transition can later request IDLE.
  m_PeerOverride = false;
  m_LastRequestedProfile = ConnProfile::FAST;
  m_LastRequestMs = connectionTimeMs();
  m_LastRequestValid = true;
  m_LastRequestSucceeded = true;
  m_LastConnActivityMs = m_LastRequestMs;
  return true;
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
#if defined(FURBLE_CONSOLE)
  const NimBLEConnInfo before = client->getConnInfo();
#endif
  client->setConnectionParams(minInterval, maxInterval, latency, timeout);
  const bool updated = client->updateConnParams(minInterval, maxInterval, latency, timeout);

#if defined(FURBLE_CONSOLE)
  const NimBLEConnInfo after = client->getConnInfo();
  BtDebugEvent event;
  event.timestamp_ms = static_cast<uint64_t>(esp_timer_get_time()) / 1000ULL;
  event.kind = BtDebugEventKind::CONNECTION_PARAMS;
  event.success = updated;
  event.interval_before = before.getConnInterval();
  event.latency_before = before.getConnLatency();
  event.timeout_before = before.getConnTimeout();
  event.interval_after = after.getConnInterval();
  event.latency_after = after.getConnLatency();
  event.timeout_after = after.getConnTimeout();
  event.address_type = m_Address.getType();
  copyText(event.address, sizeof(event.address), after.getAddress().toString());
  copyText(event.identity, sizeof(event.identity), after.getIdAddress().toString());
  copyText(event.operation, sizeof(event.operation), connProfileName(profile));
  copyText(event.result, sizeof(event.result), updated ? "ok" : "failed");
  BtDebugJournal::instance().record(event);
#endif

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

#if defined(FURBLE_CONSOLE)
  const uint64_t started_us = static_cast<uint64_t>(esp_timer_get_time());
  // Snapshot the UUIDs before the write. A concurrent disconnect plus
  // reconnect rediscovery frees the remote characteristic, so the pointer
  // must not be dereferenced again after the operation returns.
  const NimBLERemoteService *service = characteristic->getRemoteService();
  const NimBLEUUID empty;
  const NimBLEUUID service_uuid = service != nullptr ? service->getUUID() : empty;
  const NimBLEUUID characteristic_uuid = characteristic->getUUID();
#endif
  const bool result = characteristic->writeValue(data, length, response);

#if defined(FURBLE_CONSOLE)
  journalRecord("write", service_uuid, characteristic_uuid, data, length, result, response,
                started_us, m_DebugAttemptId);
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

#if defined(FURBLE_CONSOLE)
  const uint64_t started_us = static_cast<uint64_t>(esp_timer_get_time());
#endif
  const bool result = m_Client->setValue(service, characteristic, value, response);

#if defined(FURBLE_CONSOLE)
  journalRecord("write", service, characteristic, value.data(), value.size(), result, response,
                started_us, m_DebugAttemptId);
#endif

  return result;
}

bool Camera::gattRead(NimBLERemoteCharacteristic *characteristic, NimBLEAttValue &value) {
  if (characteristic == nullptr) {
    value = NimBLEAttValue();
    return false;
  }

#if defined(FURBLE_CONSOLE)
  const uint64_t started_us = static_cast<uint64_t>(esp_timer_get_time());
  // Snapshot the UUIDs before the read. A concurrent disconnect plus
  // reconnect rediscovery frees the remote characteristic, so the pointer
  // must not be dereferenced again after the operation returns. Observed on
  // hardware 2026-08-28: GR IV standby drop during the sleep gate read, then
  // LoadProhibited on 0xfefefefe in the journal call after readValue.
  const NimBLERemoteService *service = characteristic->getRemoteService();
  const NimBLEUUID empty;
  const NimBLEUUID service_uuid = service != nullptr ? service->getUUID() : empty;
  const NimBLEUUID characteristic_uuid = characteristic->getUUID();
#endif
  value = characteristic->readValue();

#if defined(FURBLE_CONSOLE)
  journalRecord("read", service_uuid, characteristic_uuid, value.data(), value.size(), true, false,
                started_us, m_DebugAttemptId);
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

#if defined(FURBLE_CONSOLE)
  const uint64_t started_us = static_cast<uint64_t>(esp_timer_get_time());
#endif
  value = m_Client->getValue(service, characteristic);

#if defined(FURBLE_CONSOLE)
  journalRecord("read", service, characteristic, value.data(), value.size(), true, false,
                started_us, m_DebugAttemptId);
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

#if defined(FURBLE_CONSOLE)
  const uint64_t started_us = static_cast<uint64_t>(esp_timer_get_time());
  const uint32_t attempt_id = m_DebugAttemptId;
  const NimBLEUUID empty;
  const NimBLERemoteService *service = characteristic->getRemoteService();
  const NimBLEUUID service_uuid = service != nullptr ? service->getUUID() : empty;
  const uint8_t cccd[] = {static_cast<uint8_t>(indicate ? 0x02 : 0x01), 0x00};
  journalRecord("subscribe", service_uuid, characteristic->getUUID(), cccd, sizeof(cccd), false,
                response, started_us, attempt_id, true);
#endif
  const bool result = characteristic->subscribe(
      !indicate,
#if defined(FURBLE_CONSOLE)
      [callback, attempt_id](NimBLERemoteCharacteristic *remoteCharacteristic, uint8_t *data,
                             size_t length,
#else
      [callback](NimBLERemoteCharacteristic *remoteCharacteristic, uint8_t *data, size_t length,
#endif
                             bool isNotify) {
#if defined(FURBLE_CONSOLE)
        const NimBLERemoteService *service = remoteCharacteristic->getRemoteService();
        const NimBLEUUID empty;
        journalRecord(isNotify ? "notify" : "indicate",
                      service != nullptr ? service->getUUID() : empty,
                      remoteCharacteristic->getUUID(), data, length, true, false,
                      static_cast<uint64_t>(esp_timer_get_time()), attempt_id);
#endif
        if (callback) {
          callback(remoteCharacteristic, data, length, isNotify);
        }
      },
      response);
#if defined(FURBLE_CONSOLE)
  journalRecord("subscribe", service_uuid, characteristic->getUUID(), cccd, sizeof(cccd), result,
                response, started_us, attempt_id);
#endif
  return result;
}

#if defined(FURBLE_CONSOLE)

bool Camera::gattJournalSetEnabled(bool enabled) {
  return BtDebugJournal::instance().setEnabled(enabled);
}

void Camera::gattJournalDrain(void) {
  BtDebugJournal::instance().drain(8, printJournalEvent, nullptr);
}

void Camera::gattJournalDump(size_t count) {
  BtDebugJournal &journal = BtDebugJournal::instance();
  printf("bt.journal: count:%zu dropped:%zu capacity:%zu storage_bytes:%zu\n", journal.size(),
         journal.droppedCount(), journal.capacity(), journal.storageBytes());
  journal.dump(std::min(count, BtDebugJournal::MAX_EVENTS), printJournalEvent, nullptr);
}

void Camera::gattJournalClear(void) {
  BtDebugJournal::instance().clear();
}

#endif

bool Camera::onConnParamsUpdateRequest(NimBLEClient *pClient, const ble_gap_upd_params *params) {
#if defined(FURBLE_CONSOLE)
  BtDebugEvent event;
  event.timestamp_ms = static_cast<uint64_t>(esp_timer_get_time()) / 1000ULL;
  event.kind = BtDebugEventKind::CONNECTION_PARAMS;
  event.success = params != nullptr;
  event.interval_after = params != nullptr ? params->itvl_min : 0;
  event.latency_after = params != nullptr ? params->latency : 0;
  event.timeout_after = params != nullptr ? params->supervision_timeout : 0;
  event.address_type = m_Address.getType();
  if (pClient != nullptr) {
    const NimBLEConnInfo info = pClient->getConnInfo();
    copyText(event.address, sizeof(event.address), info.getAddress().toString());
    copyText(event.identity, sizeof(event.identity), info.getIdAddress().toString());
  }
  copyText(event.operation, sizeof(event.operation), "peer-request");
  copyText(event.result, sizeof(event.result), params != nullptr ? "observed" : "missing");
  BtDebugJournal::instance().record(event);
#else
  (void)pClient;
#endif
  const std::lock_guard<std::mutex> lock(m_ConnParamsMutex);
  if (params != nullptr) {
    ESP_LOGI(LOG_TAG, "Peer requested connection parameters (%u-%u, latency %u, timeout %u)",
             params->itvl_min, params->itvl_max, params->latency, params->supervision_timeout);

    // The supervision timeout is also the dead-link detector: it bounds how long
    // a powered-off or out-of-range camera keeps reporting connected before
    // onDisconnect fires. furble caps its own timeout at m_IdleTimeout, but a
    // camera that requests a long timeout to save its own power would otherwise
    // win here and blunt detection for that whole window, the false-connected
    // bug where a power-off went unnoticed for tens of seconds and shutter
    // writes buffered until the camera returned. Reject an over-cap request
    // once registration is complete. During the Secure identifier write only,
    // Fujifilm cameras require their initial request to be accepted and drop
    // the link if it is rejected. Camera::connect() requests and verifies the
    // bounded FAST profile immediately after that handshake, so this temporary
    // exception does not weaken steady-state dead-link detection. Rejecting keeps
    // the existing link parameters, which already satisfy the timeout margin, so
    // detection stays prompt. A well-behaved peer accepts the reject; furble
    // does not counter-request, so there is no renegotiation loop.
    const bool secureRegistration =
        (m_Type == Type::FUJIFILM_SECURE) && m_FujifilmSecureRegistration;
    if (params->supervision_timeout > m_IdleTimeout && !secureRegistration) {
      ESP_LOGW(LOG_TAG,
               "Rejecting peer supervision timeout %u (cap %u) to keep dead-link detection",
               params->supervision_timeout, m_IdleTimeout);
      return false;
    }
  }

  // Accept the peer values and do not immediately fight them with our own
  // request. A later shutter or focus press starts a new fast-profile cycle.
  m_PeerOverride = true;
  m_LastRequestValid = false;
  m_LastRequestSucceeded = false;
  return true;
}

void Camera::cancelConnect(void) {
  // Lock-free on purpose: this must be callable while connect() holds m_Mutex
  // across a long vendor wait, and Control::disconnect() calls it with its own
  // mutex held, where no radio call is allowed. The wait polls
  // connectCancelled() and unwinds, which is what bounds the m_Mutex
  // acquisition in disconnect() below.
  //
  // The token alone cannot reach a call that is already blocked inside NimBLE.
  // abortBlockingConnect() is the other half and must be called too.
  m_ConnectCancelled = true;
}

void Camera::abortBlockingConnect(void) {
  // secureConnection() holds the connect task in the NimBLE host for the whole
  // pairing timeout, up to 30 s on the rc=13 stale-bond reconnect measured on
  // the X100VI (2026-09-02 bench). Nothing polls during that call, so the
  // cancel token is unread, the interactive disconnect waits for the attempt
  // to unwind, and the device looks locked up to the user, who cannot even
  // cancel.
  //
  // Terminating the link makes the blocking call fail immediately. The vendor
  // path then sees the token, or the cleared connected flag, on its next check
  // and unwinds exactly as it does for any other mid-connect link loss.
  // ble_gap_conn_cancel() in Control::disconnect() already covers the earlier
  // GAP connect phase; this covers everything after the link comes up.
  const std::lock_guard<std::mutex> lock(m_CancelMutex);
  if ((m_CancelClient != nullptr) && m_CancelClient->isConnected()) {
    m_CancelClient->disconnect();
  }
}

void Camera::clearConnectCancel(void) {
  m_ConnectCancelled = false;
}

bool Camera::connectCancelled(void) const {
  return m_ConnectCancelled.load();
}

std::string Camera::getDisplayName(void) const {
  return m_Name.empty() ? std::string(DISPLAY_NAME_FALLBACK) : m_Name;
}

bool Camera::needsRepair(void) const {
  return m_NeedsRepair.load();
}

uint8_t Camera::noteSecureFailure(void) {
  return static_cast<uint8_t>(m_SecureFailures.fetch_add(1) + 1);
}

void Camera::clearSecureFailures(void) {
  m_SecureFailures = 0;
}

void Camera::setNeedsRepair(void) {
  m_NeedsRepair = true;
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
  // A fresh user connect request re-arms the camera: the user may have put it
  // back into pairing mode since the re-pair prompt. The failure run goes with
  // it, so "consecutive security failures" always means "within one connect
  // cycle" and can never accumulate across days of idle transients.
  m_NeedsRepair = false;
  m_SecureFailures = 0;

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
