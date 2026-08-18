#include "FurbleBtDebug.h"

#if defined(FURBLE_CONSOLE)

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <NimBLEAddress.h>
#include <NimBLEAdvertisedDevice.h>
#include <NimBLEClient.h>
#include <NimBLEConnInfo.h>
#include <NimBLEDevice.h>
#include <NimBLERemoteCharacteristic.h>
#include <NimBLERemoteDescriptor.h>
#include <NimBLERemoteService.h>
#include <NimBLEScan.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "FurbleControl.h"
#include "Scan.h"

namespace Furble {

namespace {

constexpr uint32_t EXPLORE_TASK_STACK = 8192;
constexpr UBaseType_t EXPLORE_TASK_PRIORITY = 2;
constexpr uint32_t EXPLORE_CONNECT_TIMEOUT_MS = 20000;
constexpr uint16_t CCCD_UUID = 0x2902;

std::string hexDump(const uint8_t *data, size_t length) {
  if ((data == nullptr) || (length == 0)) {
    return "<empty>";
  }

  static constexpr char HEX[] = "0123456789abcdef";
  std::string result;
  result.reserve(length * 2);
  for (size_t index = 0; index < length; index++) {
    result.push_back(HEX[data[index] >> 4]);
    result.push_back(HEX[data[index] & 0x0f]);
  }
  return result;
}

const char *addressTypeName(uint8_t type) {
  switch (type) {
    case BLE_ADDR_PUBLIC:
      return "public";
    case BLE_ADDR_RANDOM:
      return "random";
    case 2:
      return "public-id";
    case 3:
      return "random-id";
    default:
      return "unknown";
  }
}

const char *controlStateName(Control::state_t state) {
  switch (state) {
    case Control::STATE_IDLE:
      return "idle";
    case Control::STATE_CONNECT:
      return "connect";
    case Control::STATE_CONNECTING:
      return "connecting";
    case Control::STATE_CONNECT_FAILED:
      return "connect_failed";
    case Control::STATE_ACTIVE:
      return "active";
    case Control::STATE_DISCONNECTING:
      return "disconnecting";
  }
  return "unknown";
}

class VerboseScanCallbacks final: public NimBLEScanCallbacks {
 public:
  void begin(bool duplicates) {
    m_Duplicates = duplicates;
    m_Seen.clear();
  }

  void onResult(const NimBLEAdvertisedDevice *device) override {
    if (device == nullptr) {
      return;
    }

    const std::string address = device->getAddress().toString();
    if (!m_Duplicates && std::find(m_Seen.begin(), m_Seen.end(), address) != m_Seen.end()) {
      return;
    }
    if (!m_Duplicates) {
      m_Seen.push_back(address);
    }

    const std::vector<uint8_t> &payload = device->getPayload();
    const uint8_t *payloadData = payload.empty() ? nullptr : payload.data();
    const size_t advLength = std::min<size_t>(device->getAdvLength(), payload.size());
    const size_t responseLength = payload.size() - advLength;

    printf("adv.addr: %s (%s)\n", address.c_str(), addressTypeName(device->getAddressType()));
    printf("adv.rssi: %d\n", device->getRSSI());
    printf("adv.name: %s\n", device->getName().c_str());
    printf("adv.raw: %s\n", hexDump(payloadData, advLength).c_str());
    printf("adv.rsp: %s\n",
           hexDump(payloadData == nullptr ? nullptr : payloadData + advLength, responseLength)
               .c_str());

    for (uint8_t index = 0; index < device->getManufacturerDataCount(); index++) {
      const std::string manufacturer = device->getManufacturerData(index);
      if (manufacturer.size() < 2) {
        printf("adv.mfr: %u company: <short> payload: %s\n", static_cast<unsigned>(index),
               hexDump(reinterpret_cast<const uint8_t *>(manufacturer.data()), manufacturer.size())
                   .c_str());
        continue;
      }

      const uint8_t *data = reinterpret_cast<const uint8_t *>(manufacturer.data());
      const uint16_t company = static_cast<uint16_t>(data[0] | (data[1] << 8));
      printf("adv.mfr: %u company: 0x%04x payload: %s\n", static_cast<unsigned>(index), company,
             hexDump(data + 2, manufacturer.size() - 2).c_str());
    }

    for (uint8_t index = 0; index < device->getServiceUUIDCount(); index++) {
      printf("adv.svc: %s\n", device->getServiceUUID(index).toString().c_str());
    }
  }

  void onScanEnd(const NimBLEScanResults &, int reason) override {
    printf("bt.scan: done reason: %d\n", reason);
    m_Running.store(false);
  }

  bool isRunning() const { return m_Running.load(); }

  void setRunning(bool running) { m_Running.store(running); }

 private:
  bool m_Duplicates = false;
  std::vector<std::string> m_Seen;
  std::atomic_bool m_Running = false;
};

class Explorer final: public NimBLEClientCallbacks {
 public:
  bool start(const char *address, BtDebug::PairMode pairMode, bool keepBond) {
    if ((address == nullptr) || m_Running.exchange(true)) {
      return false;
    }

    m_Address = address;
    m_PairMode = pairMode;
    m_KeepBond.store(keepBond);
    m_Stop.store(false);
    m_ReadRequested.store(false);
    m_Connected.store(false);
    {
      const std::lock_guard<std::mutex> lock(m_Mutex);
      m_PairInfo.reset();
      m_PairRequest = PairRequest::NONE;
    }

    const BaseType_t result = xTaskCreate(taskEntry, "bt-explore", EXPLORE_TASK_STACK, this,
                                          EXPLORE_TASK_PRIORITY, &m_Task);
    if (result != pdPASS) {
      m_Running.store(false);
      m_Task = nullptr;
      return false;
    }

    return true;
  }

  bool stop(bool keepBond) {
    if (!m_Running.load()) {
      return false;
    }

    if (keepBond) {
      m_KeepBond.store(true);
    }
    m_Stop.store(true);

    const std::lock_guard<std::mutex> lock(m_Mutex);
    if (m_Client != nullptr) {
      NimBLEClient *client = m_Client;
      client->cancelConnect();
      if (client->isConnected()) {
        client->disconnect();
      }
    }
    printf("bt.explore: stopping\n");
    return true;
  }

  bool read() {
    if (!m_Running.load() || !m_Connected.load()) {
      return false;
    }
    m_ReadRequested.store(true);
    return true;
  }

  bool confirm(bool accept) {
    std::unique_ptr<NimBLEConnInfo> info;
    {
      const std::lock_guard<std::mutex> lock(m_Mutex);
      if (m_PairRequest != PairRequest::CONFIRM) {
        return false;
      }
      info = std::make_unique<NimBLEConnInfo>(*m_PairInfo);
      m_PairInfo.reset();
      m_PairRequest = PairRequest::NONE;
    }
    NimBLEDevice::injectConfirmPasskey(*info, accept);
    printf("pair.confirmed: %s\n", accept ? "yes" : "no");
    return true;
  }

  bool key(uint32_t value) {
    std::unique_ptr<NimBLEConnInfo> info;
    {
      const std::lock_guard<std::mutex> lock(m_Mutex);
      if (m_PairRequest != PairRequest::ENTRY) {
        return false;
      }
      info = std::make_unique<NimBLEConnInfo>(*m_PairInfo);
      m_PairInfo.reset();
      m_PairRequest = PairRequest::NONE;
    }
    NimBLEDevice::injectPassKey(*info, value);
    printf("pair.key: sent\n");
    return true;
  }

  bool isRunning() const { return m_Running.load(); }

  void onConnect(NimBLEClient *client) override {
    const NimBLEConnInfo info = client->getConnInfo();
    m_Connected.store(true);
    printf("explore.connected: true\n");
    printf("explore.address: %s\n", info.getAddress().toString().c_str());
    printf("explore.mtu: %u\n", static_cast<unsigned>(info.getMTU()));
    printf("explore.interval: %u\n", static_cast<unsigned>(info.getConnInterval()));
    printf("explore.latency: %u\n", static_cast<unsigned>(info.getConnLatency()));
    printf("explore.timeout: %u\n", static_cast<unsigned>(info.getConnTimeout()));
  }

  void onDisconnect(NimBLEClient *, int reason) override {
    m_Connected.store(false);
    printf("explore.disconnected: %d\n", reason);
  }

  void onConnectFail(NimBLEClient *, int reason) override {
    printf("explore.connect_failed: %d\n", reason);
  }

  void onPassKeyEntry(NimBLEConnInfo &connInfo) override {
    {
      const std::lock_guard<std::mutex> lock(m_Mutex);
      m_PairInfo = std::make_unique<NimBLEConnInfo>(connInfo);
      m_PairRequest = PairRequest::ENTRY;
    }
    printf("pair.entry: required\n");
  }

  uint32_t onPassKeyDisplay(NimBLEConnInfo &connInfo) override {
    (void)connInfo;
    const uint32_t passkey = NimBLEDevice::getSecurityPasskey();
    printf("pair.display: %06lu\n", static_cast<unsigned long>(passkey));
    return passkey;
  }

  void onConfirmPasskey(NimBLEConnInfo &connInfo, uint32_t pin) override {
    {
      const std::lock_guard<std::mutex> lock(m_Mutex);
      m_PairInfo = std::make_unique<NimBLEConnInfo>(connInfo);
      m_PairRequest = PairRequest::CONFIRM;
    }
    printf("pair.confirm: %06lu\n", static_cast<unsigned long>(pin));
  }

  void onAuthenticationComplete(NimBLEConnInfo &connInfo) override {
    printf("pair.auth: encrypted: %s authenticated: %s bonded: %s key_size: %u\n",
           connInfo.isEncrypted() ? "true" : "false", connInfo.isAuthenticated() ? "true" : "false",
           connInfo.isBonded() ? "true" : "false", static_cast<unsigned>(connInfo.getSecKeySize()));
  }

 private:
  enum class PairRequest : uint8_t {
    NONE,
    CONFIRM,
    ENTRY,
  };

  static void taskEntry(void *context) { static_cast<Explorer *>(context)->run(); }

  void configureSecurity() {
    switch (m_PairMode) {
      case BtDebug::PairMode::NONE:
        return;
      case BtDebug::PairMode::JUST_WORKS:
        NimBLEDevice::setSecurityAuth(true, false, true);
        NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
        break;
      case BtDebug::PairMode::NUMERIC_DISPLAY:
        NimBLEDevice::setSecurityAuth(true, true, true);
        NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_YESNO);
        break;
    }
  }

  void resetSecurity() {
    if (m_PairMode == BtDebug::PairMode::NONE) {
      return;
    }
    NimBLEDevice::setSecurityAuth(true, true, true);
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_YESNO);
  }

  bool connect(const NimBLEAddress &address, bool &bondedBefore) {
    NimBLEClient *client = NimBLEDevice::createClient();
    if (client == nullptr) {
      printf("explore.connect_failed: no client\n");
      return false;
    }

    client->setClientCallbacks(this, false);
    client->setSelfDelete(false, false);
    client->setConnectTimeout(EXPLORE_CONNECT_TIMEOUT_MS);
    client->setConnectRetries(0);

    {
      const std::lock_guard<std::mutex> lock(m_Mutex);
      m_Client = client;
    }

    bondedBefore = NimBLEDevice::isBonded(address);
    printf("explore.connect: %s (%s)\n", address.toString().c_str(),
           addressTypeName(address.getType()));
    if (client->connect(address, true, false, true)) {
      return true;
    }

    {
      const std::lock_guard<std::mutex> lock(m_Mutex);
      m_Client = nullptr;
    }
    NimBLEDevice::deleteClient(client);
    return false;
  }

  void printGattTree(NimBLEClient *client) {
    m_Characteristics.clear();
    const auto &services = client->getServices(true);
    printf("gatt.services: %u\n", static_cast<unsigned>(services.size()));

    for (NimBLERemoteService *service : services) {
      if (service == nullptr) {
        continue;
      }
      printf("svc: %s handle: %u\n", service->getUUID().toString().c_str(),
             static_cast<unsigned>(service->getHandle()));

      const auto &characteristics = service->getCharacteristics(true);
      for (NimBLERemoteCharacteristic *characteristic : characteristics) {
        if (characteristic == nullptr) {
          continue;
        }
        m_Characteristics.push_back(characteristic);

        std::string properties;
        if (characteristic->canRead()) {
          properties += "R ";
        }
        if (characteristic->canWrite()) {
          properties += "W ";
        }
        if (characteristic->canWriteNoResponse()) {
          properties += "w ";
        }
        if (characteristic->canNotify()) {
          properties += "N ";
        }
        if (characteristic->canIndicate()) {
          properties += "I ";
        }
        if (!properties.empty()) {
          properties.pop_back();
        }
        if (properties.empty()) {
          properties = "-";
        }

        printf("chr: %s handle: %u properties: %s\n", characteristic->getUUID().toString().c_str(),
               static_cast<unsigned>(characteristic->getHandle()), properties.c_str());

        const auto &descriptors = characteristic->getDescriptors(true);
        for (NimBLERemoteDescriptor *descriptor : descriptors) {
          if (descriptor == nullptr) {
            continue;
          }
          const bool cccd = descriptor->getUUID() == NimBLEUUID(CCCD_UUID);
          printf("desc: %s handle: %u cccd: %s\n", descriptor->getUUID().toString().c_str(),
                 static_cast<unsigned>(descriptor->getHandle()), cccd ? "true" : "false");
        }
      }
    }
  }

  void subscribeAll() {
    for (NimBLERemoteCharacteristic *characteristic : m_Characteristics) {
      if (characteristic == nullptr) {
        continue;
      }
      if (characteristic->canNotify()) {
        const bool result = characteristic->subscribe(
            true,
            [](NimBLERemoteCharacteristic *remote, uint8_t *data, size_t length, bool isNotify) {
              printf("%s: %llu %s %s\n", isNotify ? "notify" : "indicate",
                     esp_timer_get_time() / 1000ULL, remote->getUUID().toString().c_str(),
                     hexDump(data, length).c_str());
            },
            true);
        printf("subscribe: %s notify: %s\n", characteristic->getUUID().toString().c_str(),
               result ? "true" : "false");
      }
      if (characteristic->canIndicate()) {
        const bool result = characteristic->subscribe(
            false,
            [](NimBLERemoteCharacteristic *remote, uint8_t *data, size_t length, bool isNotify) {
              printf("%s: %llu %s %s\n", isNotify ? "notify" : "indicate",
                     esp_timer_get_time() / 1000ULL, remote->getUUID().toString().c_str(),
                     hexDump(data, length).c_str());
            },
            true);
        printf("subscribe: %s indicate: %s\n", characteristic->getUUID().toString().c_str(),
               result ? "true" : "false");
      }
    }
  }

  void readAll() {
    printf("explore.read: begin\n");
    for (NimBLERemoteCharacteristic *characteristic : m_Characteristics) {
      if ((characteristic == nullptr) || !characteristic->canRead() || m_Stop.load()) {
        continue;
      }
      const NimBLEAttValue value = characteristic->readValue();
      printf("read: %s %u %s\n", characteristic->getUUID().toString().c_str(),
             static_cast<unsigned>(value.size()), hexDump(value.data(), value.size()).c_str());
    }
    printf("explore.read: end\n");
  }

  void run() {
    configureSecurity();

    NimBLEAddress address;
    bool bondedBefore = false;
    bool connected = false;
    for (uint8_t type = BLE_ADDR_PUBLIC; type <= 3 && !m_Stop.load(); type++) {
      address = NimBLEAddress(m_Address, type);
      if (connect(address, bondedBefore)) {
        connected = true;
        break;
      }
    }

    NimBLEClient *client = nullptr;
    {
      const std::lock_guard<std::mutex> lock(m_Mutex);
      client = m_Client;
    }

    if (connected && (client != nullptr) && !m_Stop.load()) {
      printGattTree(client);

      if ((m_PairMode != BtDebug::PairMode::NONE) && !m_Stop.load()) {
        printf("pair.mode: %s\n",
               m_PairMode == BtDebug::PairMode::JUST_WORKS ? "just-works" : "numeric-display");
        printf("pair.secure: %s\n", client->secureConnection(false) ? "true" : "false");
      }

      if (!m_Stop.load() && client->isConnected()) {
        subscribeAll();
        printf("explore.ready: true\n");
      }

      while (!m_Stop.load() && client->isConnected()) {
        if (m_ReadRequested.exchange(false)) {
          readAll();
        }
        vTaskDelay(pdMS_TO_TICKS(100));
      }
    }

    if (client != nullptr) {
      if (client->isConnected()) {
        client->disconnect();
      }
      NimBLEDevice::deleteClient(client);
    }

    if (connected && (m_PairMode != BtDebug::PairMode::NONE) && !bondedBefore
        && !m_KeepBond.load()) {
      NimBLEDevice::deleteBond(address);
      printf("pair.bond: deleted\n");
    }

    {
      const std::lock_guard<std::mutex> lock(m_Mutex);
      m_Client = nullptr;
      m_PairInfo.reset();
      m_PairRequest = PairRequest::NONE;
    }
    m_Connected.store(false);
    m_Running.store(false);
    m_Task = nullptr;
    resetSecurity();
    printf("bt.explore: done\n");
    vTaskDelete(nullptr);
  }

  std::mutex m_Mutex;
  std::string m_Address;
  NimBLEClient *m_Client = nullptr;
  TaskHandle_t m_Task = nullptr;
  std::vector<NimBLERemoteCharacteristic *> m_Characteristics;
  std::unique_ptr<NimBLEConnInfo> m_PairInfo;
  PairRequest m_PairRequest = PairRequest::NONE;
  BtDebug::PairMode m_PairMode = BtDebug::PairMode::NONE;
  std::atomic_bool m_Running = false;
  std::atomic_bool m_Stop = false;
  std::atomic_bool m_Connected = false;
  std::atomic_bool m_ReadRequested = false;
  std::atomic_bool m_KeepBond = false;
};

VerboseScanCallbacks g_ScanCallbacks;
Explorer g_Explorer;

}  // namespace

bool BtDebug::startScan(uint32_t seconds, bool duplicates) {
  auto &control = Control::getInstance();
  if (control.getState() != Control::STATE_IDLE) {
    printf("bt.scan: refused state: %s\n", controlStateName(control.getState()));
    return false;
  }
  if (Scan::getInstance().isActive() || g_ScanCallbacks.isRunning() || g_Explorer.isRunning()) {
    printf("bt.scan: refused busy\n");
    return false;
  }

  g_ScanCallbacks.begin(duplicates);
  g_ScanCallbacks.setRunning(true);
  Scan::getInstance().start(&g_ScanCallbacks, seconds * 1000, duplicates);
  printf("bt.scan: started seconds: %lu duplicates: %s\n", static_cast<unsigned long>(seconds),
         duplicates ? "true" : "false");
  return true;
}

bool BtDebug::stopScan() {
  if (!Scan::getInstance().isActive() && !g_ScanCallbacks.isRunning()) {
    return false;
  }
  Scan::getInstance().stop();
  g_ScanCallbacks.setRunning(false);
  printf("bt.scan: stopped\n");
  return true;
}

bool BtDebug::startExplore(const char *address, PairMode mode, bool keepBond) {
  auto &control = Control::getInstance();
  if (control.getState() != Control::STATE_IDLE) {
    printf("bt.explore: refused state: %s\n", controlStateName(control.getState()));
    return false;
  }
  if (Scan::getInstance().isActive() || g_ScanCallbacks.isRunning()) {
    printf("bt.explore: refused scan active\n");
    return false;
  }
  if (!g_Explorer.start(address, mode, keepBond)) {
    printf("bt.explore: refused busy or task unavailable\n");
    return false;
  }
  printf("bt.explore: started\n");
  return true;
}

bool BtDebug::stopExplore(bool keepBond) {
  return g_Explorer.stop(keepBond);
}

bool BtDebug::readExplore() {
  return g_Explorer.read();
}

bool BtDebug::pairConfirm(bool accept) {
  return g_Explorer.confirm(accept);
}

bool BtDebug::pairKey(uint32_t key) {
  return g_Explorer.key(key);
}

bool BtDebug::isExploreRunning() {
  return g_Explorer.isRunning();
}

}  // namespace Furble

#endif  // FURBLE_CONSOLE
