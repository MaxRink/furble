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

class NimBLEUUID {
 public:
  NimBLEUUID();
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
  void mockDropLink(int reason, bool fire_callback);

 private:
  NimBLEClientCallbacks *m_Callbacks = nullptr;
  NimBLEMockPeer *m_Peer = nullptr;
  NimBLEAddress m_Address;
  NimBLEConnInfo m_ConnInfo;
  uint32_t m_ConnectTimeout = 0;
  bool m_Connected = false;
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
  static void init(const std::string &name);
  static bool setPower(int8_t power);
  static void setSecurityAuth(bool bonding, bool mitm, bool secure_connections);
  static void setSecurityIOCap(uint8_t capability);
  static void setSecurityInitKey(uint8_t key_distribution);
  static void setSecurityRespKey(uint8_t key_distribution);
  static void setOwnAddrType(uint8_t address_type);

  static NimBLEClient *createClient();
  static void setMockPeer(NimBLEMockPeer *peer);
  static NimBLEMockPeer *getMockPeer();
  static void resetMock();

  // Host test hooks.
  // The most recently created client, so a test can drive link loss on the
  // client a Camera created internally.
  static NimBLEClient *lastClient();
  // Force the next NimBLEClient::connect() to fail, modelling a connect that
  // never establishes (advertisement gone, peer busy, security rejected).
  static void setConnectShouldFail(bool fail);
};

#endif
