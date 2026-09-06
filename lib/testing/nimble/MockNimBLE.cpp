#include "MockNimBLE.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <thread>

namespace {

uint8_t hexDigit(char value) {
  if ((value >= '0') && (value <= '9')) {
    return static_cast<uint8_t>(value - '0');
  }
  if ((value >= 'a') && (value <= 'f')) {
    return static_cast<uint8_t>(value - 'a' + 10);
  }
  if ((value >= 'A') && (value <= 'F')) {
    return static_cast<uint8_t>(value - 'A' + 10);
  }
  return 0xff;
}

std::array<uint8_t, 16> parseUuid(const std::string &value) {
  std::string digits;
  digits.reserve(value.size());
  for (const char character : value) {
    if (character == '-') {
      continue;
    }
    digits.push_back(character);
  }

  std::array<uint8_t, 16> bytes {};
  if (digits.size() != 32) {
    return bytes;
  }

  for (size_t i = 0; i < bytes.size(); i++) {
    const uint8_t high = hexDigit(digits[i * 2]);
    const uint8_t low = hexDigit(digits[i * 2 + 1]);
    if ((high == 0xff) || (low == 0xff)) {
      return {};
    }
    bytes[i] = static_cast<uint8_t>((high << 4) | low);
  }
  return bytes;
}

std::string uuidString(const std::array<uint8_t, 16> &bytes) {
  static constexpr char HEX[] = "0123456789abcdef";
  std::string result;
  result.reserve(36);
  for (size_t i = 0; i < bytes.size(); i++) {
    if ((i == 4) || (i == 6) || (i == 8) || (i == 10)) {
      result.push_back('-');
    }
    result.push_back(HEX[bytes[i] >> 4]);
    result.push_back(HEX[bytes[i] & 0x0f]);
  }
  return result;
}

std::array<uint8_t, 16> uuidBytes(uint32_t first, uint16_t second, uint16_t third, uint64_t tail) {
  std::array<uint8_t, 16> bytes {};
  bytes[0] = static_cast<uint8_t>(first >> 24);
  bytes[1] = static_cast<uint8_t>(first >> 16);
  bytes[2] = static_cast<uint8_t>(first >> 8);
  bytes[3] = static_cast<uint8_t>(first);
  bytes[4] = static_cast<uint8_t>(second >> 8);
  bytes[5] = static_cast<uint8_t>(second);
  bytes[6] = static_cast<uint8_t>(third >> 8);
  bytes[7] = static_cast<uint8_t>(third);
  for (size_t i = 0; i < sizeof(tail); i++) {
    bytes[8 + i] = static_cast<uint8_t>(tail >> (56 - (i * 8)));
  }
  return bytes;
}

// The client pool is shared across the real Control task, the per-target tasks
// and the fuzz driver thread, so its container mutations are serialised. The
// pointed-to NimBLEClient objects are heap allocated through unique_ptr, so
// their addresses stay stable across a push_back reallocation; only the vector
// itself needs the lock. It is a recursive mutex because a client freed inline
// on a clean teardown erases itself while the caller may already hold it.
std::recursive_mutex g_ClientsMutex;
NimBLEServer g_Server;
NimBLEScan g_Scan;
NimBLEMockPeer *g_Peer = nullptr;
std::vector<std::pair<NimBLEAddress, NimBLEMockPeer *>> g_Peers;
std::vector<std::unique_ptr<NimBLEClient>> g_Clients;
std::vector<NimBLEClient *> g_PendingReap;  // clients queued for async reap
// Host stack state, guarded by g_ClientsMutex like the rest of the mock's
// globals so a reader on the console task never races Device::init().
bool g_Initialised = false;
int g_Power = 0;
bool g_ConnectShouldFail = false;
size_t g_ConnectFailCount = 0;  // number of connect() calls still forced to fail
size_t g_MaxClients = 0;        // 0 means unlimited
bool g_DeferredDelete = false;  // honour setSelfDelete and defer live deleteClient
bool g_AsyncDisconnect = false;
bool g_AsyncDisconnectEventFound = false;
bool g_ScanStartAllowed = true;
uint32_t g_ConnectDelayMs = 0;  // one-shot block at the start of the next connect()
size_t g_ConnParamApplyDelayReads = 1;
bool g_Bonded = false;
size_t g_DeleteBondCount = 0;
// Absent-peer model: addresses whose advertisements the scan never delivers.
std::vector<NimBLEAddress> g_AbsentAddresses;

// Erase a client from the live pool, freeing it. Caller must not touch the
// pointer afterwards. Safe to call on a pointer no longer in the pool. Also
// drops the pointer from the pending-reap queue so a later reap cannot free a
// different client that reused the same address. Returns true if the client was
// in the live pool.
bool eraseClient(NimBLEClient *client) {
  const std::lock_guard<std::recursive_mutex> lock(g_ClientsMutex);
  g_PendingReap.erase(std::remove(g_PendingReap.begin(), g_PendingReap.end(), client),
                      g_PendingReap.end());
  for (auto it = g_Clients.begin(); it != g_Clients.end(); ++it) {
    if (it->get() == client) {
      g_Clients.erase(it);
      return true;
    }
  }
  return false;
}

}  // namespace

bool nimbleMockGapScanStartAllowed(void) {
  const std::lock_guard<std::recursive_mutex> lock(g_ClientsMutex);
  return g_ScanStartAllowed
         && std::none_of(g_Clients.begin(), g_Clients.end(),
                         [](const auto &client) { return client->mockDisconnectEventPending(); });
}

void nimbleMockSetGapScanStartAllowed(bool allowed) {
  const std::lock_guard<std::recursive_mutex> lock(g_ClientsMutex);
  g_ScanStartAllowed = allowed;
}

bool nimbleMockScanDeliveryAllowed(const NimBLEAdvertisedDevice *device) {
  if (device == nullptr) {
    return true;
  }
  const std::lock_guard<std::recursive_mutex> lock(g_ClientsMutex);
  for (const auto &address : g_AbsentAddresses) {
    if (address == device->getAddress()) {
      return false;
    }
  }
  return true;
}

NimBLEServer *NimBLEDevice::createServer() {
  return &g_Server;
}

NimBLEScan *NimBLEDevice::getScan() {
  return &g_Scan;
}

NimBLEUUID::NimBLEUUID() : m_Bytes {} {}

NimBLEUUID::NimBLEUUID(uint16_t shortUuid) : NimBLEUUID() {
  m_Bytes[0] = static_cast<uint8_t>(shortUuid >> 8);
  m_Bytes[1] = static_cast<uint8_t>(shortUuid & 0xff);
}

NimBLEUUID::NimBLEUUID(uint32_t first, uint16_t second, uint16_t third, uint64_t tail)
    : m_Bytes(uuidBytes(first, second, third, tail)) {}

NimBLEUUID::NimBLEUUID(const char *uuid) : NimBLEUUID(uuid == nullptr ? "" : std::string(uuid)) {}

NimBLEUUID::NimBLEUUID(const std::string &uuid) : m_Bytes(parseUuid(uuid)) {}

std::string NimBLEUUID::toString() const {
  return uuidString(m_Bytes);
}

bool NimBLEUUID::operator==(const NimBLEUUID &other) const {
  return m_Bytes == other.m_Bytes;
}

bool NimBLEUUID::operator!=(const NimBLEUUID &other) const {
  return !(*this == other);
}

bool NimBLEUUID::operator<(const NimBLEUUID &other) const {
  return m_Bytes < other.m_Bytes;
}

NimBLEAddress::NimBLEAddress() : m_Address(0), m_Type(0) {}

NimBLEAddress::NimBLEAddress(uint64_t address, uint8_t type) : m_Address(address), m_Type(type) {}

uint8_t NimBLEAddress::getType() const {
  return m_Type;
}

std::string NimBLEAddress::toString() const {
  char value[18];
  std::snprintf(value, sizeof(value), "%012llx", static_cast<unsigned long long>(m_Address));
  return value;
}

NimBLEAddress::operator uint64_t() const {
  return m_Address;
}

bool NimBLEAddress::operator==(const NimBLEAddress &other) const {
  return (m_Address == other.m_Address) && (m_Type == other.m_Type);
}

bool NimBLEAddress::operator!=(const NimBLEAddress &other) const {
  return !(*this == other);
}

NimBLEAttValue::NimBLEAttValue() = default;

NimBLEAttValue::NimBLEAttValue(const uint8_t *data, size_t length) {
  setData(data, length);
}

NimBLEAttValue::NimBLEAttValue(const char *value) {
  if (value != nullptr) {
    setData(reinterpret_cast<const uint8_t *>(value), std::strlen(value));
  }
}

NimBLEAttValue::NimBLEAttValue(const std::string &value) {
  setData(reinterpret_cast<const uint8_t *>(value.data()), value.size());
}

NimBLEAttValue::NimBLEAttValue(std::initializer_list<uint8_t> value) : m_Data(value) {
  if (!m_Data.empty()) {
    m_String.assign(reinterpret_cast<const char *>(m_Data.data()), m_Data.size());
  }
}

NimBLEAttValue::NimBLEAttValue(const std::vector<uint8_t> &value) : m_Data(value) {
  if (!m_Data.empty()) {
    m_String.assign(reinterpret_cast<const char *>(m_Data.data()), m_Data.size());
  }
}

const uint8_t *NimBLEAttValue::data() const {
  return m_Data.data();
}

uint8_t *NimBLEAttValue::data() {
  return m_Data.data();
}

size_t NimBLEAttValue::size() const {
  return m_Data.size();
}

size_t NimBLEAttValue::length() const {
  return size();
}

const char *NimBLEAttValue::c_str() const {
  return m_String.c_str();
}

NimBLEAttValue::operator std::string() const {
  return std::string(reinterpret_cast<const char *>(m_Data.data()), m_Data.size());
}

uint8_t NimBLEAttValue::operator[](size_t index) const {
  return m_Data[index];
}

void NimBLEAttValue::setData(const uint8_t *data, size_t length) {
  if ((data == nullptr) || (length == 0)) {
    m_Data.clear();
    m_String.clear();
    return;
  }
  m_Data.assign(data, data + length);
  m_String.assign(reinterpret_cast<const char *>(data), length);
}

NimBLERemoteCharacteristic::NimBLERemoteCharacteristic(NimBLEClient *client,
                                                       const NimBLEUUID &service,
                                                       const NimBLEUUID &characteristic)
    : m_Client(client),
      m_Service(service),
      m_Characteristic(characteristic),
      m_Handle(static_cast<uint16_t>(std::hash<std::string> {}(characteristic.toString()))) {}

const NimBLEUUID &NimBLERemoteCharacteristic::getUUID() const {
  return m_Characteristic;
}

NimBLERemoteService *NimBLERemoteCharacteristic::getRemoteService() const {
  // The host double keeps the service UUID as a value on the characteristic.
  // Camera's typed journal tests use UUID-addressed operations; pointer-based
  // explorer tests only need a null-safe service lookup.
  return nullptr;
}

uint16_t NimBLERemoteCharacteristic::getHandle() const {
  return m_Handle;
}

bool NimBLERemoteCharacteristic::canWrite() const {
  return (m_Client != nullptr) && (m_Client->getPeer() != nullptr)
         && m_Client->getPeer()->canWrite(m_Service, m_Characteristic);
}

bool NimBLERemoteCharacteristic::canIndicate() const {
  return true;
}

bool NimBLERemoteCharacteristic::canRead() const {
  return true;
}

bool NimBLERemoteCharacteristic::canNotify() const {
  return true;
}
bool NimBLERemoteCharacteristic::canWriteNoResponse() const {
  return true;
}

bool NimBLERemoteCharacteristic::writeValue(const uint8_t *data, size_t length, bool response) {
  if ((m_Client == nullptr) || (m_Client->getPeer() == nullptr)) {
    return false;
  }
  std::vector<uint8_t> value;
  if ((data != nullptr) && (length > 0)) {
    value.assign(data, data + length);
  }
  return m_Client->getPeer()->write(*m_Client, m_Service, m_Characteristic, value, response);
}

bool NimBLERemoteCharacteristic::writeValue(const char *data, size_t length, bool response) {
  return writeValue(reinterpret_cast<const uint8_t *>(data), length, response);
}

bool NimBLERemoteCharacteristic::writeValue(const NimBLEAttValue &value, bool response) {
  return writeValue(value.data(), value.size(), response);
}

bool NimBLERemoteCharacteristic::subscribe(bool notification,
                                           const NimBLENotifyCallback &callback,
                                           bool response) {
  if ((m_Client == nullptr) || (m_Client->getPeer() == nullptr)) {
    return false;
  }
  return m_Client->getPeer()->subscribe(*m_Client, m_Service, m_Characteristic, notification, this,
                                        callback, response);
}

NimBLEAttValue NimBLERemoteCharacteristic::readValue() const {
  if ((m_Client == nullptr) || (m_Client->getPeer() == nullptr)) {
    return {};
  }
  return m_Client->getPeer()->read(*m_Client, m_Service, m_Characteristic);
}

NimBLERemoteService::NimBLERemoteService(NimBLEClient *client, const NimBLEUUID &service)
    : m_Client(client), m_Service(service) {}

const NimBLEUUID &NimBLERemoteService::getUUID() const {
  return m_Service;
}

NimBLERemoteCharacteristic *NimBLERemoteService::getCharacteristic(
    const NimBLEUUID &characteristic) {
  if ((m_Client == nullptr) || (m_Client->getPeer() == nullptr)
      || !m_Client->getPeer()->hasCharacteristic(m_Service, characteristic)
      || !m_Client->getPeer()->discoverCharacteristic(*m_Client, m_Service, characteristic)) {
    return nullptr;
  }

  const std::string key = characteristic.toString();
  auto found = m_Characteristics.find(key);
  if (found == m_Characteristics.end()) {
    auto remote = std::make_unique<NimBLERemoteCharacteristic>(m_Client, m_Service, characteristic);
    found = m_Characteristics.emplace(key, std::move(remote)).first;
  }
  return found->second.get();
}

NimBLEConnInfo::NimBLEConnInfo() : m_Interval(0), m_Latency(0), m_Timeout(0) {}

NimBLEConnInfo::NimBLEConnInfo(uint16_t interval, uint16_t latency, uint16_t timeout)
    : m_Interval(interval), m_Latency(latency), m_Timeout(timeout) {}

uint16_t NimBLEConnInfo::getConnInterval() const {
  return m_Interval;
}

uint16_t NimBLEConnInfo::getConnLatency() const {
  return m_Latency;
}

uint16_t NimBLEConnInfo::getConnTimeout() const {
  return m_Timeout;
}

bool NimBLEConnInfo::isEncrypted() const {
  return true;
}
bool NimBLEConnInfo::isAuthenticated() const {
  return true;
}
bool NimBLEConnInfo::isBonded() const {
  return true;
}
uint8_t NimBLEConnInfo::getSecKeySize() const {
  return 16;
}
const NimBLEAddress &NimBLEConnInfo::getAddress() const {
  static const NimBLEAddress address;
  return address;
}

const NimBLEAddress &NimBLEConnInfo::getIdAddress() const {
  return getAddress();
}

void NimBLEClientCallbacks::onConnect(NimBLEClient *client) {
  (void)client;
}

void NimBLEClientCallbacks::onDisconnect(NimBLEClient *client, int reason) {
  (void)client;
  (void)reason;
}

bool NimBLEClientCallbacks::onConnParamsUpdateRequest(NimBLEClient *client,
                                                      const ble_gap_upd_params *params) {
  (void)client;
  (void)params;
  return true;
}

void NimBLEClientCallbacks::onPassKeyEntry(NimBLEConnInfo &) {}
uint32_t NimBLEClientCallbacks::onPassKeyDisplay(NimBLEConnInfo &) {
  return 0;
}
void NimBLEClientCallbacks::onConfirmPasskey(NimBLEConnInfo &, uint32_t) {}
void NimBLEClientCallbacks::onAuthenticationComplete(NimBLEConnInfo &) {}

NimBLEClient::NimBLEClient() = default;

void NimBLEClient::setClientCallbacks(NimBLEClientCallbacks *callbacks, bool delete_callbacks) {
  (void)delete_callbacks;
  m_Callbacks = callbacks;
}

void NimBLEClient::setSelfDelete(bool delete_on_disconnect, bool delete_on_connect_failure) {
  m_DeleteOnDisconnect = delete_on_disconnect;
  m_DeleteOnConnectFailure = delete_on_connect_failure;

  // Repro: the supervision-timeout disconnect event is still queued on
  // the host task. Arming delete-on-disconnect hands the free to that event.
  // Deliver it here, right after the arm, which is one legal interleaving of
  // the real host task against the failed-connect reclaim: onDisconnect fires,
  // then the armed client is freed inline (NimBLEClient.cpp:1084-1098,
  // NimBLEDevice.cpp:373). Nothing may touch this object after eraseClient.
  if (delete_on_disconnect && m_LinkDeadEventPending) {
    m_LinkDeadEventPending = false;
    const int reason = m_LinkDeadReason;
    if (m_Peer != nullptr) {
      m_Peer->disconnect(*this, reason);
      m_Peer = nullptr;
    }
    m_Connected = false;
    if (m_Callbacks != nullptr) {
      m_Callbacks->onDisconnect(this, reason);
    }
    eraseClient(this);
    return;
  }
}

void NimBLEClient::mockMarkLinkDeadEventPending(int reason) {
  // The physical link is gone (supervision timeout) but the host task has not
  // consumed BLE_GAP_EVENT_DISCONNECT yet: the client still reports connected.
  m_LinkDeadEventPending = true;
  m_LinkDeadReason = reason;
}

void NimBLEClient::setConnectTimeout(uint32_t timeout) {
  m_ConnectTimeout = timeout;
}

void NimBLEClient::setConnectionParams(uint16_t min_interval,
                                       uint16_t max_interval,
                                       uint16_t latency,
                                       uint16_t timeout) {
  (void)max_interval;
  // NimBLE stores these as the preferred parameters for the next connection
  // and for counter-proposals. It does not change an existing controller link.
  // Only update the live mock snapshot before connect; connected links move via
  // the asynchronous updateConnParams() path below.
  if (!m_Connected) {
    m_ConnInfo = NimBLEConnInfo(min_interval, latency, timeout);
  }
}

bool NimBLEClient::connect(const NimBLEAddress &address) {
  if (g_ConnectDelayMs > 0) {
    // Model the seconds-long block a reconnect spends inside NimBLEClient::connect
    // on device. One-shot: consume it so only the targeted reconnect blocks. This
    // holds the control task inside connectAll() long enough for a test to prove
    // that commands issued during the outage are not buffered and replayed.
    const uint32_t delay = g_ConnectDelayMs;
    g_ConnectDelayMs = 0;
    std::this_thread::sleep_for(std::chrono::milliseconds(delay));
  }
  if (g_ConnectShouldFail) {
    // Model a connect that never establishes. The link stays down and no
    // callback fires, matching NimBLE returning false from connect().
    return false;
  }
  if (g_ConnectFailCount > 0) {
    // A transient failure that drains: fail now, let a later attempt succeed.
    g_ConnectFailCount--;
    return false;
  }
  NimBLEMockPeer *peer = g_Peer;
  for (const auto &entry : g_Peers) {
    if (entry.first == address) {
      peer = entry.second;
      break;
    }
  }
  if ((peer == nullptr) || !peer->acceptConnection(*this, address)) {
    return false;
  }

  m_Peer = peer;
  m_Address = address;
  m_Connected = true;
  if (m_Callbacks != nullptr) {
    m_Callbacks->onConnect(this);
  }
  return true;
}

void NimBLEClient::disconnect() {
  if (!m_Connected) {
    return;
  }

  // Repro: a terminate issued against a link the supervision timeout
  // already killed. ble_gap_terminate returns ENOTCONN-ish and the queued
  // disconnect event is consumed around the call: onDisconnect fires here. A
  // client not armed for delete-on-disconnect survives (the fixed ordering);
  // an armed one is freed inline exactly as on the host task.
  if (m_LinkDeadEventPending) {
    m_LinkDeadEventPending = false;
    const int reason = m_LinkDeadReason;
    if (m_Peer != nullptr) {
      m_Peer->disconnect(*this, reason);
      m_Peer = nullptr;
    }
    m_Connected = false;
    const bool selfDelete = m_DeleteOnDisconnect;
    if (m_Callbacks != nullptr) {
      m_Callbacks->onDisconnect(this, reason);
    }
    if (selfDelete) {
      eraseClient(this);
    }
    return;
  }

  if (m_StuckTerminate) {
    // Gone peer: ble_gap_terminate is issued but never completes, so the link
    // stays locally connected and no onDisconnect fires until the supervision
    // timeout resolves it later via mockCompleteStalledTerminate().
    if (m_Peer != nullptr) {
      m_Peer->disconnect(*this, 0);
    }
    return;
  }

  if (m_Peer != nullptr) {
    m_Peer->disconnect(*this, 0);
  }

  // In the real stack ble_gap_terminate can complete the link teardown before
  // the NimBLE GAP task dispatches BLE_GAP_EVENT_DISCONNECT.  Keep the client
  // in the pool and queue that event, but expose the link as down to match the
  // state seen by the application.  A premature deleteClient() then erases the
  // object while the controller event still refers to its connection handle.
  if (g_AsyncDisconnect) {
    m_Connected = false;
    m_DisconnectEventPending = true;
    return;
  }

  m_Connected = false;
  if (m_Callbacks != nullptr) {
    m_Callbacks->onDisconnect(this, 0);
  }

  // Clean Camera-driven teardown. Under the deferred-delete model a self-deleting
  // client is freed here, right after onDisconnect, exactly as NimBLE frees it on
  // the host task. This teardown runs single-threaded from the owning target task
  // (m_Connected guards every other reader), so the inline free is safe and arms
  // ASan to catch any later dereference of the freed client. Nothing below may
  // touch a member: the object is gone.
  if (g_DeferredDelete && m_DeleteOnDisconnect) {
    eraseClient(this);
  }
}

bool NimBLEClient::mockCompleteAsyncDisconnect(void) {
  if (!m_DisconnectEventPending) {
    return false;
  }

  m_DisconnectEventPending = false;
  if (m_Callbacks != nullptr) {
    m_Callbacks->onDisconnect(this, 0);
  }
  if (g_DeferredDelete && m_DeleteOnDisconnect) {
    eraseClient(this);
  }
  return true;
}

bool NimBLEClient::mockDisconnectEventPending(void) const {
  return m_DisconnectEventPending;
}

bool NimBLEClient::isConnected() const {
  return m_Connected;
}

void NimBLEClient::mockDropLink(int reason, bool fire_callback) {
  if (m_Peer != nullptr) {
    m_Peer->disconnect(*this, reason);
  }
  m_Connected = false;
  if (fire_callback && (m_Callbacks != nullptr)) {
    m_Callbacks->onDisconnect(this, reason);
  }

  // A link-loss drop that delivered onDisconnect frees a self-deleting client on
  // hardware. Here the drop is driven from a helper thread that still holds this
  // raw pointer, so freeing inline would race that thread. Queue the client for
  // an asynchronous reap the fuzz harness performs at a quiescent point, and
  // guard against a second drop queueing the same client twice.
  if (fire_callback && g_DeferredDelete && m_DeleteOnDisconnect && !m_PendingReap) {
    m_PendingReap = true;
    const std::lock_guard<std::recursive_mutex> lock(g_ClientsMutex);
    g_PendingReap.push_back(this);
  }
}

void NimBLEClient::mockDropLinkSelfDelete(int reason) {
  // Sever the peer link and clear the connected flag, as a supervision-timeout or
  // peer power-cycle does mid-handshake.
  if (m_Peer != nullptr) {
    m_Peer->disconnect(*this, reason);
  }
  m_Connected = false;

  // Deliver onDisconnect through the callbacks the client currently holds, then
  // free a self-deleting client inline, exactly as the NimBLE host task frees a
  // setSelfDelete client right after its disconnect callback. When the owner has
  // turned self-delete off for the connect window the client is kept alive, so a
  // still-running _connect() can unwind against a valid (disconnected) client.
  const bool selfDelete = m_DeleteOnDisconnect;
  if (m_Callbacks != nullptr) {
    m_Callbacks->onDisconnect(this, reason);
  }
  if (selfDelete) {
    // delete-this: frees this client and the remote services and characteristics
    // it owns. Nothing may touch this object, or a characteristic reached through
    // it, after this returns.
    eraseClient(this);
  }
}

void NimBLEClient::mockStallTerminate() {
  // Model a peer that has gone away mid-session with a stalled ble_gap_terminate:
  // the client stays locally connected and its next disconnect() will not
  // complete, so onDisconnect is deferred to the supervision timeout.
  m_StuckTerminate = true;
}

bool NimBLEClient::mockRequestDelete() {
  // Real NimBLEDevice::deleteClient() does not free a still-connected client: it
  // sets deleteOnDisconnect and defers the free to the eventual onDisconnect.
  // Model that deferral only for a stalled-terminate client (the gone-peer case
  // Control reclaims), so the existing synchronous-free path other host tests
  // rely on is unchanged. This keeps the deferred-delete model purely additive.
  if (m_Connected && m_StuckTerminate) {
    m_DeferredDelete = true;
    return false;
  }
  return true;
}

void NimBLEClient::mockCompleteStalledTerminate(int reason) {
  // The supervision timeout finally resolves the stalled terminate. NimBLE fires
  // onDisconnect through whatever callbacks the client currently holds (the
  // default no-op set if the owner detached in reclaimClient), then self-deletes
  // a client that was marked for deferred deletion.
  m_StuckTerminate = false;
  m_Connected = false;
  if (m_Peer != nullptr) {
    m_Peer->disconnect(*this, reason);
    m_Peer = nullptr;
  }
  if (m_Callbacks != nullptr) {
    m_Callbacks->onDisconnect(this, reason);
  }
  if (m_DeferredDelete) {
    // Not connected now, so this frees the client synchronously. Nothing touches
    // this object afterwards.
    NimBLEDevice::deleteClient(this);
  }
}

bool NimBLEClient::mockPeerRequestConnParams(const ble_gap_upd_params &params) {
  // Model a peer-initiated L2CAP connection parameter update request. The real
  // stack runs the client callback and, on accept, installs the peer's values as
  // the live parameters; on reject it keeps the current parameters. That reject
  // path is what bounds dead-link detection when a camera asks for a long
  // supervision timeout.
  if (m_Callbacks == nullptr) {
    return true;
  }
  const bool accepted = m_Callbacks->onConnParamsUpdateRequest(this, &params);
  if (accepted) {
    m_ConnParamUpdatePending = false;
    m_ConnInfo = NimBLEConnInfo(params.itvl_min, params.latency, params.supervision_timeout);
  }
  return accepted;
}

NimBLERemoteService *NimBLEClient::getService(const NimBLEUUID &service) {
  if (!m_Connected || (m_Peer == nullptr) || !m_Peer->hasService(service)) {
    return nullptr;
  }

  const std::string key = service.toString();
  auto found = m_Services.find(key);
  if (found == m_Services.end()) {
    auto remote = std::make_unique<NimBLERemoteService>(this, service);
    found = m_Services.emplace(key, std::move(remote)).first;
  }
  return found->second.get();
}

void NimBLEClient::dropServiceCache() {
  m_Services.clear();
}

bool NimBLEClient::secureConnection() {
  return (m_Peer != nullptr) && m_Peer->secureConnection(*this);
}

NimBLEAttValue NimBLEClient::getValue(const NimBLEUUID &service, const NimBLEUUID &characteristic) {
  if ((m_Peer == nullptr) || !m_Connected) {
    return {};
  }
  return m_Peer->read(*this, service, characteristic);
}

bool NimBLEClient::setValue(const NimBLEUUID &service,
                            const NimBLEUUID &characteristic,
                            const NimBLEAttValue &value,
                            bool response) {
  if ((m_Peer == nullptr) || !m_Connected) {
    return false;
  }
  return m_Peer->write(*this, service, characteristic,
                       std::vector<uint8_t>(value.data(), value.data() + value.size()), response);
}

bool NimBLEClient::updateConnParams(uint16_t min_interval,
                                    uint16_t max_interval,
                                    uint16_t latency,
                                    uint16_t timeout) {
  if ((m_Peer == nullptr) || !m_Connected) {
    return false;
  }
  if (!m_Peer->updateConnectionParams(*this, min_interval, max_interval, latency, timeout)) {
    return false;
  }
  // The real API reports whether the controller request was queued. The GAP
  // update event changes the live connection info later. Keep one stale read
  // between those events so an immediate request-and-reread test cannot pass.
  m_PendingConnInfo = NimBLEConnInfo(min_interval, latency, timeout);
  m_PendingConnInfoReads = g_ConnParamApplyDelayReads;
  m_ConnParamUpdatePending = true;
  return true;
}

NimBLEConnInfo NimBLEClient::getConnInfo() const {
  m_ConnInfoReadCount++;
  if (m_ConnParamUpdatePending) {
    if (m_PendingConnInfoReads > 0) {
      m_PendingConnInfoReads--;
    } else {
      m_ConnInfo = m_PendingConnInfo;
      m_ConnParamUpdatePending = false;
    }
  }
  return m_ConnInfo;
}

size_t NimBLEClient::mockConnInfoReadCount() const {
  return m_ConnInfoReadCount;
}

bool NimBLEClient::mockConnParamUpdatePending() const {
  return m_ConnParamUpdatePending;
}

int NimBLEClient::getRssi() const {
  return (m_Peer == nullptr) ? 0 : m_Peer->getRssi();
}

NimBLEMockPeer *NimBLEClient::getPeer() const {
  return m_Peer;
}

NimBLEAdvertisedDevice::NimBLEAdvertisedDevice() = default;

void NimBLEAdvertisedDevice::setAddress(const NimBLEAddress &address) {
  m_Address = address;
}

void NimBLEAdvertisedDevice::setName(const std::string &name) {
  m_Name = name;
}

void NimBLEAdvertisedDevice::setManufacturerData(const uint8_t *data, size_t length) {
  m_Manufacturer = NimBLEAttValue(data, length);
}

void NimBLEAdvertisedDevice::addServiceUUID(const NimBLEUUID &service) {
  m_Services.push_back(service);
}

void NimBLEAdvertisedDevice::setRSSI(int rssi) {
  m_Rssi = rssi;
}

const NimBLEAddress &NimBLEAdvertisedDevice::getAddress() const {
  return m_Address;
}

const std::string &NimBLEAdvertisedDevice::getName() const {
  return m_Name;
}

const NimBLEAttValue &NimBLEAdvertisedDevice::getManufacturerData() const {
  return m_Manufacturer;
}

bool NimBLEAdvertisedDevice::haveManufacturerData() const {
  return m_Manufacturer.size() > 0;
}

bool NimBLEAdvertisedDevice::haveServiceUUID() const {
  return !m_Services.empty();
}

NimBLEUUID NimBLEAdvertisedDevice::getServiceUUID() const {
  return m_Services.empty() ? NimBLEUUID() : m_Services.front();
}

bool NimBLEAdvertisedDevice::isAdvertisingService(const NimBLEUUID &service) const {
  for (const auto &advertised : m_Services) {
    if (advertised == service) {
      return true;
    }
  }
  return false;
}

int NimBLEAdvertisedDevice::getRSSI() const {
  return m_Rssi;
}

std::string NimBLEUtils::dataToHexString(const uint8_t *data, size_t length) {
  static constexpr char HEX[] = "0123456789abcdef";
  std::string value;
  value.reserve(length * 2);
  for (size_t i = 0; i < length; i++) {
    value.push_back(HEX[data[i] >> 4]);
    value.push_back(HEX[data[i] & 0x0f]);
  }
  return value;
}

void NimBLEDevice::init(const std::string &name) {
  (void)name;
  const std::lock_guard<std::recursive_mutex> lock(g_ClientsMutex);
  g_Initialised = true;
}

bool NimBLEDevice::isInitialized() {
  const std::lock_guard<std::recursive_mutex> lock(g_ClientsMutex);
  return g_Initialised;
}

NimBLEAddress NimBLEDevice::getAddress() {
  // A fixed controller address. The mock has no radio, so this only has to be
  // stable and printable.
  return NimBLEAddress(0x0011223344ULL, 0);
}

int NimBLEDevice::getPower() {
  const std::lock_guard<std::recursive_mutex> lock(g_ClientsMutex);
  return g_Power;
}

NimBLEClient *NimBLEDevice::getClientByPeerAddress(const NimBLEAddress &address) {
  const std::lock_guard<std::recursive_mutex> lock(g_ClientsMutex);
  for (const auto &client : g_Clients) {
    if (client->m_Address == address) {
      return client.get();
    }
  }
  return nullptr;
}

bool NimBLEDevice::setPower(int8_t power) {
  const std::lock_guard<std::recursive_mutex> lock(g_ClientsMutex);
  g_Power = power;
  return true;
}

bool NimBLEDevice::setPower(int8_t power, NimBLETxPowerType type) {
  (void)type;
  const std::lock_guard<std::recursive_mutex> lock(g_ClientsMutex);
  g_Power = power;
  return true;
}

extern "C" int ble_gap_conn_cancel(void) {
  return 0;
}

void NimBLEDevice::setSecurityAuth(bool bonding, bool mitm, bool secure_connections) {
  (void)bonding;
  (void)mitm;
  (void)secure_connections;
}

void NimBLEDevice::setSecurityIOCap(uint8_t capability) {
  (void)capability;
}

void NimBLEDevice::setSecurityInitKey(uint8_t key_distribution) {
  (void)key_distribution;
}

void NimBLEDevice::setSecurityRespKey(uint8_t key_distribution) {
  (void)key_distribution;
}

void NimBLEDevice::setOwnAddrType(uint8_t address_type) {
  (void)address_type;
}

NimBLEClient *NimBLEDevice::createClient() {
  const std::lock_guard<std::recursive_mutex> lock(g_ClientsMutex);
  if ((g_MaxClients != 0) && (g_Clients.size() >= g_MaxClients)) {
    // Pool exhausted, exactly as NimBLE reports "Unable to create client;
    // already at max". Camera::connect() handles the null return.
    return nullptr;
  }
  g_Clients.push_back(std::make_unique<NimBLEClient>());
  return g_Clients.back().get();
}

bool NimBLEDevice::deleteClient(NimBLEClient *client) {
  if (client == nullptr) {
    return false;
  }
  const std::lock_guard<std::recursive_mutex> lock(g_ClientsMutex);

  // Callers may retain the address of a client that self-deleted or was
  // already reclaimed. Establish ownership before reading any member through
  // that possibly stale pointer; the previous ordering called isConnected()
  // and mockRequestDelete() first, which made the documented no-op path a
  // host-side use-after-free under ASan.
  const auto live = std::find_if(g_Clients.begin(), g_Clients.end(),
                                 [client](const auto &owned) { return owned.get() == client; });
  if (live == g_Clients.end()) {
    return false;
  }

  // Fuzzer deferred-delete model (NimBLEDevice::setDeferredClientDelete):
  // deleteClient() on a still-connected client does not free synchronously. It
  // marks the client for self-delete on its next disconnect and returns, leaving
  // the live client (and its callback pointer) alive. A caller that reclaims a
  // still-connected client and then dereferences it after the deferred free is
  // the reclaim use-after-free class; modelling the deferral here is what lets
  // ASan see it.
  if (g_DeferredDelete && client->isConnected()) {
    client->m_DeleteOnDisconnect = true;
    return true;
  }

  // Stuck-terminate deferral for the non-fuzzer suites (g_DeferredDelete off):
  // the real stack does not free a still-connected client, it sets
  // deleteOnDisconnect and defers the free to the eventual onDisconnect. Keep it
  // alive and let mockCompleteStalledTerminate() free it when the link drops.
  if (!client->mockRequestDelete()) {
    return true;
  }

  // Not connected: free now (and purge any pending-reap entry so a reused address
  // is not double freed). Returns false if it already self-deleted or was already
  // reclaimed, so a double delete is safe.
  return eraseClient(client);
}

bool NimBLEDevice::deleteBond(const NimBLEAddress &) {
  g_Bonded = false;
  g_DeleteBondCount++;
  return true;
}

bool NimBLEDevice::isBonded(const NimBLEAddress &) {
  return g_Bonded;
}

void NimBLEDevice::setBonded(bool bonded) {
  g_Bonded = bonded;
}

size_t NimBLEDevice::deleteBondCount() {
  return g_DeleteBondCount;
}

bool NimBLEDevice::setMTU(uint16_t) {
  return true;
}

void NimBLEDevice::injectPassKey(NimBLEConnInfo &, uint32_t) {}
void NimBLEDevice::injectConfirmPasskey(NimBLEConnInfo &, bool) {}

size_t NimBLEDevice::liveClientCount() {
  const std::lock_guard<std::recursive_mutex> lock(g_ClientsMutex);
  return g_Clients.size();
}

size_t NimBLEDevice::getCreatedClientCount() {
  return g_Clients.size();
}

void NimBLEDevice::setMaxClients(size_t max) {
  g_MaxClients = max;
}

void NimBLEDevice::setDeferredClientDelete(bool enabled) {
  g_DeferredDelete = enabled;
}

void NimBLEDevice::setAsyncDisconnect(bool enabled) {
  g_AsyncDisconnect = enabled;
  g_AsyncDisconnectEventFound = false;
}

bool NimBLEDevice::completeAsyncDisconnect(void) {
  NimBLEClient *pending = nullptr;
  {
    const std::lock_guard<std::recursive_mutex> lock(g_ClientsMutex);
    for (const auto &client : g_Clients) {
      if (client->m_DisconnectEventPending) {
        pending = client.get();
        break;
      }
    }
  }
  if (pending == nullptr) {
    return false;
  }

  g_AsyncDisconnectEventFound = pending->mockCompleteAsyncDisconnect();
  return g_AsyncDisconnectEventFound;
}

bool NimBLEDevice::asyncDisconnectEventFound(void) {
  return g_AsyncDisconnectEventFound;
}

size_t NimBLEDevice::reapDeferredClients() {
  const std::lock_guard<std::recursive_mutex> lock(g_ClientsMutex);
  size_t reaped = 0;
  for (NimBLEClient *client : g_PendingReap) {
    for (auto it = g_Clients.begin(); it != g_Clients.end(); ++it) {
      if (it->get() == client) {
        g_Clients.erase(it);
        reaped++;
        break;
      }
    }
  }
  g_PendingReap.clear();
  return reaped;
}

size_t NimBLEDevice::pendingReapCount() {
  const std::lock_guard<std::recursive_mutex> lock(g_ClientsMutex);
  return g_PendingReap.size();
}

void NimBLEDevice::setMockPeer(NimBLEMockPeer *peer) {
  g_Peer = peer;
}

void NimBLEDevice::setMockPeerForAddress(const NimBLEAddress &address, NimBLEMockPeer *peer) {
  for (auto &entry : g_Peers) {
    if (entry.first == address) {
      entry.second = peer;
      return;
    }
  }
  g_Peers.emplace_back(address, peer);
}

NimBLEMockPeer *NimBLEDevice::getMockPeer() {
  return g_Peer;
}

void NimBLEDevice::resetMock() {
  const std::lock_guard<std::recursive_mutex> lock(g_ClientsMutex);
  g_Clients.clear();
  g_PendingReap.clear();
  g_Peer = nullptr;
  g_Peers.clear();
  g_ConnectShouldFail = false;
  g_ConnectFailCount = 0;
  g_MaxClients = 0;
  g_DeferredDelete = false;
  g_AsyncDisconnect = false;
  g_AsyncDisconnectEventFound = false;
  g_ScanStartAllowed = true;
  g_ConnectDelayMs = 0;
  g_ConnParamApplyDelayReads = 1;
  g_Bonded = false;
  g_DeleteBondCount = 0;
  g_AbsentAddresses.clear();
  g_Initialised = false;
  g_Power = 0;
}

void NimBLEDevice::setScanAbsentAddress(const NimBLEAddress &address, bool absent) {
  const std::lock_guard<std::recursive_mutex> lock(g_ClientsMutex);
  g_AbsentAddresses.erase(std::remove(g_AbsentAddresses.begin(), g_AbsentAddresses.end(), address),
                          g_AbsentAddresses.end());
  if (absent) {
    g_AbsentAddresses.push_back(address);
  }
}

NimBLEClient *NimBLEDevice::lastClient() {
  const std::lock_guard<std::recursive_mutex> lock(g_ClientsMutex);
  return g_Clients.empty() ? nullptr : g_Clients.back().get();
}

NimBLEClient *NimBLEDevice::connectedClientForAddress(const NimBLEAddress &address) {
  const std::lock_guard<std::recursive_mutex> lock(g_ClientsMutex);
  for (const auto &client : g_Clients) {
    if (client->isConnected() && client->m_Address == address) {
      return client.get();
    }
  }
  return nullptr;
}

void NimBLEDevice::setConnectShouldFail(bool fail) {
  g_ConnectShouldFail = fail;
}

void NimBLEDevice::setConnectFailCount(size_t count) {
  g_ConnectFailCount = count;
}

void NimBLEDevice::setConnectDelayMs(uint32_t ms) {
  g_ConnectDelayMs = ms;
}

void NimBLEDevice::setConnParamApplyDelayReads(size_t reads) {
  g_ConnParamApplyDelayReads = reads;
}

#if !defined(FURBLE_SIM)
// Host suites have no clock of their own, so the mock supplies a monotonic one.
// The simulator already owns a virtual clock and defines this in its esp_timer
// shim, so leave it alone there: two definitions would fight, and the wall
// clock would make every scenario's timing nondeterministic.
namespace {
// Test-driven offset on the mock clock. Lets a suite jump a rate limit forward
// deterministically instead of sleeping for it.
std::atomic<int64_t> g_TimeOffsetUs {0};
}  // namespace

extern "C" int64_t esp_timer_get_time(void) {
  static const auto start = std::chrono::steady_clock::now();
  const auto elapsed = std::chrono::steady_clock::now() - start;
  return std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count()
         + g_TimeOffsetUs.load();
}

// Jump the mock clock forward. Host suites use this to cross a rate limit
// deterministically instead of sleeping through it.
extern "C" void furble_host_advance_time(int64_t us) {
  g_TimeOffsetUs.fetch_add(us);
}
#endif
