#ifndef FURBLE_HOST_MOCK_NIMBLE_H
#define FURBLE_HOST_MOCK_NIMBLE_H

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "NimBLEScan.h"
// Angle include on purpose. The host suite resolves this to the no-op
// esp_log.h beside this header; the simulator resolves it to its own shim
// earlier on the include path, so only one set of ESP_LOG macros is ever
// defined in a translation unit.
#include <esp_log.h>

enum esp_power_level_t : int8_t {
  ESP_PWR_LVL_N12 = -12,
  ESP_PWR_LVL_N9 = -9,
  ESP_PWR_LVL_N6 = -6,
  ESP_PWR_LVL_N3 = -3,
  ESP_PWR_LVL_P0 = 0,
  ESP_PWR_LVL_P3 = 3,
  ESP_PWR_LVL_P6 = 6,
  ESP_PWR_LVL_P9 = 9,
  ESP_PWR_LVL_P12 = 12,
  ESP_PWR_LVL_P15 = 15,
  ESP_PWR_LVL_P18 = 18,
  ESP_PWR_LVL_P21 = 21,
};

constexpr uint8_t BLE_HS_IO_DISPLAY_YESNO = 1;
constexpr uint8_t BLE_HS_IO_KEYBOARD_DISPLAY = 4;
constexpr uint8_t BLE_SM_PAIR_KEY_DIST_ENC = 0x01;
constexpr uint8_t BLE_SM_PAIR_KEY_DIST_ID = 0x02;
constexpr uint8_t BLE_OWN_ADDR_RPA_PUBLIC_DEFAULT = 0;
constexpr uint16_t BLE_GAP_INITIAL_CONN_ITVL_MIN = 6;
constexpr uint16_t BLE_GAP_INITIAL_CONN_ITVL_MAX = 12;
constexpr uint16_t BLE_GAP_INITIAL_SUPERVISION_TIMEOUT = 100;
constexpr uint16_t BLE_HS_CONN_HANDLE_NONE = 0xffff;

struct ble_gap_upd_params {
  uint16_t itvl_min = 0;
  uint16_t itvl_max = 0;
  uint16_t latency = 0;
  uint16_t supervision_timeout = 0;
};

// Transmit power target selector. The real Control requests the shared
// connection transmit power with the two-argument NimBLEDevice::setPower below.
enum class NimBLETxPowerType {
  Advertising = 0,
  Scan = 1,
  Connection = 2,
};

// Cancel any in-flight GAP connection attempt. Control::disconnect() calls this
// to force an aborting connect to unwind. The mock has no asynchronous connect,
// so this is a no-op that reports success.
extern "C" int ble_gap_conn_cancel(void);

class NimBLEUUID {
 public:
  NimBLEUUID();
  explicit NimBLEUUID(uint16_t shortUuid);
  NimBLEUUID(uint32_t first, uint16_t second, uint16_t third, uint64_t tail);
  explicit NimBLEUUID(const char *uuid);
  explicit NimBLEUUID(const std::string &uuid);

  std::string toString() const;

  bool operator==(const NimBLEUUID &other) const;
  bool operator!=(const NimBLEUUID &other) const;
  bool operator<(const NimBLEUUID &other) const;

 private:
  std::array<uint8_t, 16> m_Bytes;
};

class NimBLEAddress {
 public:
  NimBLEAddress();
  NimBLEAddress(uint64_t address, uint8_t type = 0);

  uint8_t getType() const;
  std::string toString() const;

  explicit operator uint64_t() const;
  bool operator==(const NimBLEAddress &other) const;
  bool operator!=(const NimBLEAddress &other) const;

 private:
  uint64_t m_Address;
  uint8_t m_Type;
};

class NimBLEAttValue {
 public:
  NimBLEAttValue();
  NimBLEAttValue(const uint8_t *data, size_t length);
  NimBLEAttValue(const char *value);
  NimBLEAttValue(const std::string &value);
  NimBLEAttValue(std::initializer_list<uint8_t> value);
  NimBLEAttValue(const std::vector<uint8_t> &value);

  const uint8_t *data() const;
  uint8_t *data();
  size_t size() const;
  size_t length() const;
  const char *c_str() const;
  operator std::string() const;
  uint8_t operator[](size_t index) const;

 private:
  std::vector<uint8_t> m_Data;
  std::string m_String;

  void setData(const uint8_t *data, size_t length);
};

class NimBLEClient;
class NimBLERemoteCharacteristic;
class NimBLERemoteService;

using NimBLENotifyCallback =
    std::function<void(NimBLERemoteCharacteristic *, uint8_t *, size_t, bool)>;
using BLERemoteCharacteristic = NimBLERemoteCharacteristic;

class NimBLEMockPeer {
 public:
  virtual ~NimBLEMockPeer() = default;

  virtual bool acceptConnection(NimBLEClient &client, const NimBLEAddress &address) = 0;
  virtual void disconnect(NimBLEClient &client, int reason) = 0;
  virtual bool hasService(const NimBLEUUID &service) const = 0;
  virtual bool hasCharacteristic(const NimBLEUUID &service,
                                 const NimBLEUUID &characteristic) const = 0;
  virtual bool discoverCharacteristic(NimBLEClient &client,
                                      const NimBLEUUID &service,
                                      const NimBLEUUID &characteristic) = 0;
  virtual bool canWrite(const NimBLEUUID &service, const NimBLEUUID &characteristic) const = 0;
  virtual bool write(NimBLEClient &client,
                     const NimBLEUUID &service,
                     const NimBLEUUID &characteristic,
                     const std::vector<uint8_t> &value,
                     bool response) = 0;
  virtual NimBLEAttValue read(NimBLEClient &client,
                              const NimBLEUUID &service,
                              const NimBLEUUID &characteristic) = 0;
  virtual bool subscribe(NimBLEClient &client,
                         const NimBLEUUID &service,
                         const NimBLEUUID &characteristic,
                         bool notification,
                         NimBLERemoteCharacteristic *remote,
                         const NimBLENotifyCallback &callback,
                         bool response) = 0;
  virtual bool secureConnection(NimBLEClient &client) = 0;

  // MITM pairing seam. A peer that models LE Secure Connections numeric
  // comparison or passkey display registers itself through
  // NimBLEDevice::setPasskeyPeer while its secureConnection() waits, and the
  // production NimBLEDevice::injectConfirmPasskey and injectPassKey calls are
  // delivered here. Peers with a just-works handshake never see these.
  virtual void onPasskeyConfirmed(bool accept) { (void)accept; }
  virtual void onPasskeyEntered(uint32_t passkey) { (void)passkey; }
  virtual bool updateConnectionParams(NimBLEClient &client,
                                      uint16_t min_interval,
                                      uint16_t max_interval,
                                      uint16_t latency,
                                      uint16_t timeout) = 0;
  virtual int getRssi() const = 0;
};

class NimBLERemoteCharacteristic {
 public:
  NimBLERemoteCharacteristic(NimBLEClient *client,
                             const NimBLEUUID &service,
                             const NimBLEUUID &characteristic);

  const NimBLEUUID &getUUID() const;
  NimBLERemoteService *getRemoteService() const;
  uint16_t getHandle() const;
  bool canWrite() const;
  bool canRead() const;
  bool canIndicate() const;
  bool canNotify() const;
  bool canWriteNoResponse() const;
  bool writeValue(const uint8_t *data, size_t length, bool response);
  bool writeValue(const char *data, size_t length, bool response);
  bool writeValue(const NimBLEAttValue &value, bool response);
  bool subscribe(bool notification, const NimBLENotifyCallback &callback, bool response);
  NimBLEAttValue readValue() const;

 private:
  NimBLEClient *m_Client;
  NimBLEUUID m_Service;
  NimBLEUUID m_Characteristic;
  uint16_t m_Handle;
};

class NimBLERemoteService {
 public:
  NimBLERemoteService(NimBLEClient *client, const NimBLEUUID &service);

  const NimBLEUUID &getUUID() const;
  NimBLERemoteCharacteristic *getCharacteristic(const NimBLEUUID &characteristic);

 private:
  NimBLEClient *m_Client;
  NimBLEUUID m_Service;
  std::map<std::string, std::unique_ptr<NimBLERemoteCharacteristic>> m_Characteristics;
};

class NimBLEConnInfo {
 public:
  NimBLEConnInfo();
  NimBLEConnInfo(uint16_t interval, uint16_t latency, uint16_t timeout);

  uint16_t getConnInterval() const;
  uint16_t getConnLatency() const;
  uint16_t getConnTimeout() const;
  bool isEncrypted() const;
  bool isAuthenticated() const;
  bool isBonded() const;
  uint8_t getSecKeySize() const;
  const NimBLEAddress &getAddress() const;
  const NimBLEAddress &getIdAddress() const;

  uint16_t getConnHandle() const { return m_Handle; }

  // Stamp the handle of the link this info describes. Real NimBLE fills the
  // handle from the GAP descriptor; the mock stamps it from the client, so an
  // answer recorded against an earlier link is detectable as stale.
  void mockSetConnHandle(uint16_t handle) { m_Handle = handle; }

 private:
  uint16_t m_Interval;
  uint16_t m_Latency;
  uint16_t m_Timeout;
  uint16_t m_Handle = BLE_HS_CONN_HANDLE_NONE;
};

class NimBLEClientCallbacks {
 public:
  virtual ~NimBLEClientCallbacks() = default;
  virtual void onConnect(NimBLEClient *client);
  virtual void onDisconnect(NimBLEClient *client, int reason);
  virtual bool onConnParamsUpdateRequest(NimBLEClient *client, const ble_gap_upd_params *params);
  virtual void onPassKeyEntry(NimBLEConnInfo &connInfo);
  virtual uint32_t onPassKeyDisplay(NimBLEConnInfo &connInfo);
  virtual void onConfirmPasskey(NimBLEConnInfo &connInfo, uint32_t pin);
  virtual void onAuthenticationComplete(NimBLEConnInfo &connInfo);
};

class NimBLEClient {
 public:
  NimBLEClient();
  ~NimBLEClient() = default;

  void setClientCallbacks(NimBLEClientCallbacks *callbacks, bool delete_callbacks);
  void setSelfDelete(bool delete_on_disconnect, bool delete_on_connect_failure);
  void setConnectTimeout(uint32_t timeout);
  void setConnectionParams(uint16_t min_interval,
                           uint16_t max_interval,
                           uint16_t latency,
                           uint16_t timeout);
  bool connect(const NimBLEAddress &address);
  void disconnect();
  bool isConnected() const;
  NimBLERemoteService *getService(const NimBLEUUID &service);
  bool secureConnection();
  NimBLEAttValue getValue(const NimBLEUUID &service, const NimBLEUUID &characteristic);
  bool setValue(const NimBLEUUID &service,
                const NimBLEUUID &characteristic,
                const NimBLEAttValue &value,
                bool response);
  bool updateConnParams(uint16_t min_interval,
                        uint16_t max_interval,
                        uint16_t latency,
                        uint16_t timeout);
  NimBLEConnInfo getConnInfo() const;
  int getRssi() const;
  uint16_t getConnHandle() const;

  NimBLEMockPeer *getPeer() const;

  // Host test hook. Model a spontaneous BLE link loss such as a supervision
  // timeout or the peer powering off. It severs the peer link and clears the
  // client connected flag. When fire_callback is true the disconnect callback
  // also runs, mirroring the stack detecting the drop and notifying the app.
  // When fire_callback is false the link is down but no callback fires, which
  // models the window where the peer is gone yet the app still holds a stale
  // connected flag because onDisconnect has not run.
  //
  // With the deferred-delete model enabled (NimBLEDevice::setDeferredClientDelete)
  // a fire_callback drop on a self-deleting client does not free it inline. The
  // client is queued for asynchronous reap instead, because a link-loss drop is
  // driven from a helper thread that still holds this raw pointer. The fuzz
  // harness reaps the queue at a quiescent point (NimBLEDevice::reapDeferredClients).
  void mockDropLink(int reason, bool fire_callback);

  // Host test hook. Model a spontaneous BLE drop delivered by the NimBLE host
  // task in the middle of the connect handshake, where the disconnect self-frees
  // a setSelfDelete client. It severs the peer link, clears the connected flag,
  // fires onDisconnect through the current callbacks, then, if the client is
  // armed for delete-on-disconnect, frees it INLINE exactly as NimBLE frees a
  // self-deleting client on the host task right after its disconnect callback.
  //
  // Unlike mockDropLink, which queues a self-deleting client for a later
  // asynchronous reap (so a fuzz driver thread that still holds the raw pointer
  // does not race the free), this frees synchronously. It is meant to be driven
  // from the peer write seam while the control task is still inside _connect(),
  // so the very next m_Client dereference that _connect() performs lands on the
  // freed client, which is the mid-connect use-after-free. Freeing the client
  // frees the NimBLERemoteCharacteristic this call may have been reached through,
  // so the caller must not touch that characteristic afterwards (the mock write
  // path returns immediately, delete-this style).
  void mockDropLinkSelfDelete(int reason);

  // Repro hook. Model a link killed by the supervision timeout whose
  // BLE_GAP_EVENT_DISCONNECT is still queued on the NimBLE host task: the
  // client keeps reporting connected (the conn handle is still set) while the
  // physical link is gone. The queued event is then consumed at the two points
  // the real host task can consume it relative to the failed-connect reclaim:
  //
  // - setSelfDelete(true, x): the broken reclaim ordering arms
  //   delete-on-disconnect first. The hook delivers the queued event right
  //   after the arm, exactly as the concurrent host task can: onDisconnect
  //   fires and,
  //   because deleteOnDisconnect is now set, the client is freed inline
  //   (NimBLEClient.cpp:1090 -> NimBLEDevice.cpp:373). Any later touch of the
  //   client by the reclaim is the use-after-free.
  // - disconnect(): a terminate against the dead link. The queued event is
  //   consumed during the call (onDisconnect fires); with delete-on-disconnect
  //   still off nothing is freed, matching the fixed ordering.
  void mockMarkLinkDeadEventPending(int reason);

  // Complete a controller disconnect event held back after a central
  // terminate.  Returns false when no event is queued.
  bool mockCompleteAsyncDisconnect(void);
  bool mockDisconnectEventPending(void) const;

  // Host test hooks that model a gone peer whose ble_gap_terminate stalls.
  //
  // mockStallTerminate() marks the client so its next disconnect() issues the
  // terminate but does not complete: the link stays locally connected and no
  // onDisconnect fires, exactly as a powered-off peer looks until the
  // supervision timeout. This is the state in which Control reclaims the client.
  //
  // mockRequestDelete() models NimBLEDevice::deleteClient() on such a client:
  // the real stack does not free a still-connected client, it sets
  // deleteOnDisconnect and defers the free to the eventual onDisconnect. Returns
  // false (keep alive) when still connected, true (free now) otherwise.
  //
  // mockCompleteStalledTerminate() resolves the stalled terminate at last (the
  // supervision timeout): it fires onDisconnect through whatever callbacks the
  // client currently holds (the default no-op set if the owner detached) and
  // then frees a client that was marked for deferred deletion. Used to drive the
  // late-callback window a reclaim must not leave pointing at a freed owner.
  void mockStallTerminate();
  bool mockRequestDelete();
  void mockCompleteStalledTerminate(int reason);

  // Host test hook. Model the peer requesting its own connection parameters, as a
  // camera does to save its own power. Runs the client's
  // onConnParamsUpdateRequest callback with these params. When the callback
  // accepts (returns true) the mock applies them to the live NimBLEConnInfo,
  // mirroring the controller installing the peer's values; when it rejects
  // (returns false) the current parameters stay in force. Returns the callback
  // result so a test can assert the accept or reject decision.
  bool mockPeerRequestConnParams(const ble_gap_upd_params &params);

  // Central-initiated connection updates are asynchronous in NimBLE. Expose
  // enough state for the regression test to prove production waited for the
  // controller result instead of rereading the old parameters immediately.
  size_t mockConnInfoReadCount() const;
  bool mockConnParamUpdatePending() const;

  // Host test hook. Free every cached NimBLERemoteService and, through the
  // service owners, every cached NimBLERemoteCharacteristic, exactly as the
  // reconnect path's service rediscovery frees the remote attribute objects
  // after a link drop. Driven from a peer fault while a read or write is in
  // flight, it models the free landing between the GATT operation and any
  // later dereference of the remote pointers the caller still holds.
  void dropServiceCache();

  // The callbacks the production Camera registered. A peer modelling a MITM
  // handshake calls onConfirmPasskey or onPassKeyDisplay through this, which
  // is what the real NimBLE host task does.
  NimBLEClientCallbacks *mockCallbacks() const { return m_Callbacks; }

 private:
  friend class NimBLEDevice;

  NimBLEClientCallbacks *m_Callbacks = nullptr;
  NimBLEMockPeer *m_Peer = nullptr;
  NimBLEAddress m_Address;
  mutable NimBLEConnInfo m_ConnInfo;
  mutable NimBLEConnInfo m_PendingConnInfo;
  mutable size_t m_ConnInfoReadCount = 0;
  mutable size_t m_PendingConnInfoReads = 0;
  mutable bool m_ConnParamUpdatePending = false;
  uint32_t m_ConnectTimeout = 0;
  // The GAP connection handle of the live link, or BLE_HS_CONN_HANDLE_NONE
  // when there is none. Every successful connect takes a fresh value, so the
  // production stale-handle guard has a real handle to compare against.
  uint16_t m_Handle = BLE_HS_CONN_HANDLE_NONE;
  bool m_Connected = false;
  bool m_StuckTerminate = false;
  bool m_DeferredDelete = false;
  // Self-delete flags recorded from setSelfDelete(). The fuzzer deferred-delete
  // model uses m_DeleteOnDisconnect to free a client after onDisconnect,
  // mirroring the real NimBLEClient. m_PendingReap guards against a client being
  // queued for asynchronous reap twice by repeated drops.
  bool m_DeleteOnDisconnect = false;
  bool m_DeleteOnConnectFailure = false;
  bool m_PendingReap = false;
  bool m_DisconnectEventPending = false;
  // Repro state for mockMarkLinkDeadEventPending().
  bool m_LinkDeadEventPending = false;
  int m_LinkDeadReason = 0;
  std::map<std::string, std::unique_ptr<NimBLERemoteService>> m_Services;
};

class NimBLEAdvertisedDevice {
 public:
  NimBLEAdvertisedDevice();

  void setAddress(const NimBLEAddress &address);
  void setName(const std::string &name);
  void setManufacturerData(const uint8_t *data, size_t length);
  void addServiceUUID(const NimBLEUUID &service);
  void setRSSI(int rssi);

  const NimBLEAddress &getAddress() const;
  const std::string &getName() const;
  const NimBLEAttValue &getManufacturerData() const;
  template <typename T>
  T getManufacturerData() const {
    T value {};
    const size_t length = std::min(sizeof(T), m_Manufacturer.size());
    if (length > 0) {
      std::memcpy(&value, m_Manufacturer.data(), length);
    }
    return value;
  }
  bool haveManufacturerData() const;
  bool haveServiceUUID() const;
  NimBLEUUID getServiceUUID() const;
  bool isAdvertisingService(const NimBLEUUID &service) const;
  int getRSSI() const;

 private:
  NimBLEAddress m_Address;
  std::string m_Name;
  NimBLEAttValue m_Manufacturer;
  std::vector<NimBLEUUID> m_Services;
  int m_Rssi = 0;
};

class NimBLEUtils {
 public:
  static std::string dataToHexString(const uint8_t *data, size_t length);
};

class NimBLEDevice {
 public:
  static NimBLEServer *createServer();
  static NimBLEScan *getScan();
  static void init(const std::string &name);
  static bool setPower(int8_t power);
  static bool setPower(int8_t power, NimBLETxPowerType type);
  static void setSecurityAuth(bool bonding, bool mitm, bool secure_connections);
  // The passkey NimBLE displays for a BLE_SM_IOACT_DISP action. Defaults to
  // NimBLE's own 123456 so an unconfigured build behaves identically.
  static void setSecurityPasskey(uint32_t passkey);
  static uint32_t getSecurityPasskey();
  static void setSecurityIOCap(uint8_t capability);
  static void setSecurityInitKey(uint8_t key_distribution);
  static void setSecurityRespKey(uint8_t key_distribution);
  static void setOwnAddrType(uint8_t address_type);

  // Mirrors of the real NimBLEDevice accessors the developer console's
  // 'debug ble' dump reports from: whether the host stack is up, the
  // controller address, the configured transmit power, and the live client
  // for a peer address (a target with no client is the leak signature).
  static bool isInitialized();
  static NimBLEAddress getAddress();
  static int getPower();
  static NimBLEClient *getClientByPeerAddress(const NimBLEAddress &address);

  static NimBLEClient *createClient();
  // Return a client to the pool. Mirrors NimBLEDevice::deleteClient(): it only
  // deletes a client that is still in the live client list, so calling it on a
  // client that already self-deleted (or was already reclaimed) is a safe no-op
  // that returns false. This is what makes the leak fix double-free safe.
  static bool deleteClient(NimBLEClient *client);
  static bool deleteBond(const NimBLEAddress &address);
  static bool isBonded(const NimBLEAddress &address);
  static void setBonded(bool bonded);
  static size_t deleteBondCount();
  static bool setMTU(uint16_t mtu);
  static void injectPassKey(NimBLEConnInfo &connInfo, uint32_t passKey);
  static void injectConfirmPasskey(NimBLEConnInfo &connInfo, bool accept);
  // Register the peer that a pending injectConfirmPasskey or injectPassKey
  // answer belongs to. A null peer means no handshake is waiting, so an
  // injected answer is dropped exactly as the real host drops one for a
  // connection that is no longer pairing.
  static void setPasskeyPeer(NimBLEMockPeer *peer);
  static size_t mockPasskeyConfirmCount();
  static bool mockLastPasskeyAccept();
  static size_t mockPasskeyEntryCount();
  static uint32_t mockLastPasskeyEntered();

  static void setMockPeer(NimBLEMockPeer *peer);
  // Route a client to a peer by the advertised BLE address. The single-peer
  // setter remains the fallback used by existing tests; address routing lets
  // the companion host scenario model more than one camera in one session.
  static void setMockPeerForAddress(const NimBLEAddress &address, NimBLEMockPeer *peer);
  static NimBLEMockPeer *getMockPeer();
  static void resetMock();

  // Absent-peer model: a saved camera that is powered off or out of range. Its
  // address stays registered (a connect attempt would still be routed), but the
  // scan never delivers its advertisement, so the saved-reconnect SCAN path
  // times out instead of the connect call failing. This is distinct from
  // setConnectShouldFail: that fails NimBLEClient::connect(); this starves the
  // scan wait that runs before connect() is ever reached. resetMock clears it.
  static void setScanAbsentAddress(const NimBLEAddress &address, bool absent);

  // Host test hooks.
  // The most recently created client, so a test can drive link loss on the
  // client a Camera created internally.
  static NimBLEClient *lastClient();
  // The live client currently connected to this advertised address, or nullptr.
  // A harness that owns several cameras at once (the simulator's multi-connect
  // sessions) needs to address one link without knowing which client the
  // Camera allocated for it.
  static NimBLEClient *connectedClientForAddress(const NimBLEAddress &address);
  // Force the next NimBLEClient::connect() to fail, modelling a connect that
  // never establishes. This stands in for the failure class that actually leaks
  // on hardware: a reconnect whose pairing scan times out because the camera
  // still holds its previous session, so NimBLEClient::connect() is never even
  // reached and setSelfDelete never frees the client. The mock therefore does
  // not self-delete on this path; only Camera::connect() reclaiming the client
  // keeps the pool from leaking.
  static void setConnectShouldFail(bool fail);
  // Fail the next `count` NimBLEClient::connect() calls, then let connects
  // succeed. Models a transient link that establishes only after a few failed
  // attempts, such as a reconnect that misses while the camera still holds its
  // previous session. Unlike setConnectShouldFail this drains, so a test can
  // drive reconnect churn and then confirm recovery. resetMock clears it.
  static void setConnectFailCount(size_t count);
  // Block the next NimBLEClient::connect() for `ms` milliseconds before it
  // proceeds, modelling the seconds-long block a reconnect spends inside connect()
  // on device. One-shot: consumed by the next connect() call. It lets a test hold
  // the control task inside connectAll() long enough to prove commands issued
  // during the outage are dropped, not buffered on the control queue and replayed.
  // resetMock clears it.
  static void setConnectDelayMs(uint32_t ms);
  // Delay a central connection update for this many getConnInfo() reads. The
  // default is one stale read. Large values model a peer/controller that never
  // applies the accepted request within the production registration bound.
  static void setConnParamApplyDelayReads(size_t reads);
  // Number of NimBLE clients currently live (created minus deleted). A leak
  // shows up as this count growing across failed connect attempts.
  static size_t liveClientCount();
  // Mirror of the real NimBLEDevice::getCreatedClientCount(). Camera::connect()
  // logs this for the pool accounting, so the mock must offer the same name.
  static size_t getCreatedClientCount();
  // Cap the pool the way NimBLE caps it at CONFIG_BT_NIMBLE_MAX_CONNECTIONS.
  // Once the live count reaches the cap, createClient() returns nullptr, exactly
  // as the controller does with "Unable to create client; already at max". Zero
  // means unlimited. resetMock() restores unlimited.
  static void setMaxClients(size_t max);

  // Enable the deferred client-delete model, off by default so the existing
  // suites keep their simpler synchronous semantics. When enabled the mock
  // faithfully honours setSelfDelete: a self-deleting client is freed after its
  // onDisconnect fires (on a clean Camera-driven teardown, inline; on a link-loss
  // mockDropLink, queued for reapDeferredClients), and deleteClient() on a client
  // that is still connected defers instead of freeing synchronously. This is what
  // makes the live-client count a sound leak probe across reconnect cycles and
  // arms ASan to catch a dereference of a client after its self-delete, the
  // reclaim use-after-free class. resetMock() disables it again.
  static void setDeferredClientDelete(bool enabled);

  // Make a central disconnect look like a controller event which has already
  // removed the link, while NimBLE still has the GAP disconnect callback
  // queued.  This is the narrow race exposed by failed registration cleanup:
  // deleting the client before that callback runs leaves the host with an
  // orphaned connection event.  Tests complete the queued event explicitly at
  // a quiescent point.
  static void setAsyncDisconnect(bool enabled);
  static bool completeAsyncDisconnect(void);
  static bool asyncDisconnectEventFound(void);

  // Deterministic lifetime-race hooks used by the pairing regression. The
  // connection-handle hook pauses answerPairing after it has selected the
  // client. The disconnect hook signals that a concurrent self-delete has
  // entered onDisconnect before the answer resumes.
  using host_hook_t = void (*)();
  static void setGetConnHandleHook(host_hook_t hook);
  static void setDisconnectCallbackHook(host_hook_t hook);
  static bool clientUseAfterFreeDetected();

  // Free every client queued for asynchronous reap by a link-loss drop under the
  // deferred-delete model. The fuzz harness calls this at a quiescent point where
  // no other thread holds the queued pointers. Returns the number reaped.
  static size_t reapDeferredClients();

  // Number of clients queued for asynchronous reap but not yet freed.
  static size_t pendingReapCount();
};

#endif
