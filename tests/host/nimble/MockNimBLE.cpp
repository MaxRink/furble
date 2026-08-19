#include "MockNimBLE.h"

#include <chrono>
#include <cstdio>

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

NimBLEMockPeer *g_Peer = nullptr;
std::vector<std::unique_ptr<NimBLEClient>> g_Clients;
bool g_ConnectShouldFail = false;

}  // namespace

NimBLEUUID::NimBLEUUID() : m_Bytes {} {}

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

uint16_t NimBLERemoteCharacteristic::getHandle() const {
  return m_Handle;
}

bool NimBLERemoteCharacteristic::canWrite() const {
  return (m_Client != nullptr) && (m_Client->getPeer() != nullptr)
         && m_Client->getPeer()->canWrite(m_Service, m_Characteristic);
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
      || !m_Client->getPeer()->hasCharacteristic(m_Service, characteristic)) {
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

NimBLEClient::NimBLEClient() = default;

void NimBLEClient::setClientCallbacks(NimBLEClientCallbacks *callbacks, bool delete_callbacks) {
  (void)delete_callbacks;
  m_Callbacks = callbacks;
}

void NimBLEClient::setSelfDelete(bool delete_on_disconnect, bool delete_on_connect_failure) {
  (void)delete_on_disconnect;
  (void)delete_on_connect_failure;
}

void NimBLEClient::setConnectTimeout(uint32_t timeout) {
  m_ConnectTimeout = timeout;
}

void NimBLEClient::setConnectionParams(uint16_t min_interval,
                                       uint16_t max_interval,
                                       uint16_t latency,
                                       uint16_t timeout) {
  (void)max_interval;
  m_ConnInfo = NimBLEConnInfo(min_interval, latency, timeout);
}

bool NimBLEClient::connect(const NimBLEAddress &address) {
  if (g_ConnectShouldFail) {
    // Model a connect that never establishes. The link stays down and no
    // callback fires, matching NimBLE returning false from connect().
    return false;
  }
  if ((g_Peer == nullptr) || !g_Peer->acceptConnection(*this, address)) {
    return false;
  }

  m_Peer = g_Peer;
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

  if (m_Peer != nullptr) {
    m_Peer->disconnect(*this, 0);
  }
  m_Connected = false;
  if (m_Callbacks != nullptr) {
    m_Callbacks->onDisconnect(this, 0);
  }
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
  m_ConnInfo = NimBLEConnInfo(min_interval, latency, timeout);
  return true;
}

NimBLEConnInfo NimBLEClient::getConnInfo() const {
  return m_ConnInfo;
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
}

bool NimBLEDevice::setPower(int8_t power) {
  (void)power;
  return true;
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
  g_Clients.push_back(std::make_unique<NimBLEClient>());
  return g_Clients.back().get();
}

void NimBLEDevice::setMockPeer(NimBLEMockPeer *peer) {
  g_Peer = peer;
}

NimBLEMockPeer *NimBLEDevice::getMockPeer() {
  return g_Peer;
}

void NimBLEDevice::resetMock() {
  g_Clients.clear();
  g_Peer = nullptr;
  g_ConnectShouldFail = false;
}

NimBLEClient *NimBLEDevice::lastClient() {
  return g_Clients.empty() ? nullptr : g_Clients.back().get();
}

void NimBLEDevice::setConnectShouldFail(bool fail) {
  g_ConnectShouldFail = fail;
}

extern "C" int64_t esp_timer_get_time(void) {
  static const auto start = std::chrono::steady_clock::now();
  const auto elapsed = std::chrono::steady_clock::now() - start;
  return std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();
}
