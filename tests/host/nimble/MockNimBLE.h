#ifndef FURBLE_HOST_MOCK_NIMBLE_H
#define FURBLE_HOST_MOCK_NIMBLE_H

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "NimBLEScan.h"
#include "esp_log.h"

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

 private:
  uint16_t m_Interval;
  uint16_t m_Latency;
  uint16_t m_Timeout;
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

 private:
  friend class NimBLEDevice;

  NimBLEClientCallbacks *m_Callbacks = nullptr;
  NimBLEMockPeer *m_Peer = nullptr;
  NimBLEAddress m_Address;
  NimBLEConnInfo m_ConnInfo;
  uint32_t m_ConnectTimeout = 0;
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
  static void setSecurityIOCap(uint8_t capability);
  static void setSecurityInitKey(uint8_t key_distribution);
  static void setSecurityRespKey(uint8_t key_distribution);
  static void setOwnAddrType(uint8_t address_type);

  static NimBLEClient *createClient();
  // Return a client to the pool. Mirrors NimBLEDevice::deleteClient(): it only
  // deletes a client that is still in the live client list, so calling it on a
  // client that already self-deleted (or was already reclaimed) is a safe no-op
  // that returns false. This is what makes the leak fix double-free safe.
  static bool deleteClient(NimBLEClient *client);
  static bool deleteBond(const NimBLEAddress &address);
  static bool isBonded(const NimBLEAddress &address);
  static bool setMTU(uint16_t mtu);
  static void injectPassKey(NimBLEConnInfo &connInfo, uint32_t passKey);
  static void injectConfirmPasskey(NimBLEConnInfo &connInfo, bool accept);
  static void setMockPeer(NimBLEMockPeer *peer);
  // Route a client to a peer by the advertised BLE address. The single-peer
  // setter remains the fallback used by existing tests; address routing lets
  // the companion host scenario model more than one camera in one session.
  static void setMockPeerForAddress(const NimBLEAddress &address, NimBLEMockPeer *peer);
  static NimBLEMockPeer *getMockPeer();
  static void resetMock();

  // Host test hooks.
  // The most recently created client, so a test can drive link loss on the
  // client a Camera created internally.
  static NimBLEClient *lastClient();
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

  // Free every client queued for asynchronous reap by a link-loss drop under the
  // deferred-delete model. The fuzz harness calls this at a quiescent point where
  // no other thread holds the queued pointers. Returns the number reaped.
  static size_t reapDeferredClients();

  // Number of clients queued for asynchronous reap but not yet freed.
  static size_t pendingReapCount();
};

#endif
