#include <chrono>
#include <thread>

#include "FujifilmVirtualCamera.h"

namespace Furble {
namespace Host {

namespace {

const NimBLEUUID PAIR_SERVICE_UUID {0x91f1de68, 0xdff6, 0x466e, 0x8b65ff13b0f16fb8};
const NimBLEUUID PAIR_CHARACTERISTIC_UUID {0xaba356eb, 0x9633, 0x4e60, 0xb73ff52516dbd671};
const NimBLEUUID SECURE_ADVERTISED_SERVICE_UUID {0xa9d2b304, 0xe8d6, 0x4902, 0x8336352b772d7597};
const NimBLEUUID SECURE_PAIR_SERVICE_UUID {0x123d8f06, 0x62a1, 0x4935, 0x9322833c531ee225};
const NimBLEUUID SECURE_STATUS_CHARACTERISTIC_UUID {0xf557d96b, 0x8284, 0x4667, 0x8793b971c1deca2a};
const NimBLEUUID IDENTIFIER_CHARACTERISTIC_UUID {0x85b9163e, 0x62d1, 0x49ff, 0xa6f5054b4630d4a1};
const NimBLEUUID CONFIGURATION_SERVICE_UUID {0x4c0020fe, 0xf3b6, 0x40de, 0xacc977d129067b14};
const NimBLEUUID CONFIGURATION_NOTIFICATION_UUID {0xf9150137, 0x5d40, 0x4801, 0xa8dcf7fc5b01da50};
const NimBLEUUID GEOTAG_REQUEST_CHARACTERISTIC_UUID {0xad06c7b7, 0xf41a, 0x46f4,
                                                     0xa29a712055319122};
const NimBLEUUID CONFIGURATION_INDICATION1_UUID {0xa68e3f66, 0x0fcc, 0x4395, 0x8d4caa980b5877fa};
const NimBLEUUID CONFIGURATION_INDICATION2_UUID {0xbd17ba04, 0xb76b, 0x4892, 0xa545b73ba1f74dae};
const NimBLEUUID CONFIGURATION_INDICATION3_UUID {0x049ec406, 0xef75, 0x4205, 0xa39008fe209c51f0};
const NimBLEUUID SECURE_NOTIFICATION6_UUID {0xe6692c5c, 0xb7cd, 0x44f4, 0x95fceda07ce32560};
const NimBLEUUID SECURE_NOTIFICATION_SERVICE_UUID {0x4e941240, 0xd01d, 0x46b9, 0xa5ea67636806830b};
const NimBLEUUID SECURE_NOTIFICATION4_UUID {0xbf6dc9cf, 0x3606, 0x4ec9, 0xa4c8d77576e93ea4};
const NimBLEUUID SECURE_NOTIFICATION5_UUID {0x75823784, 0xfbb7, 0x4b71, 0xabaecd9a34072e3c};
const NimBLEUUID SECURE_NOTIFICATION7_UUID {0xaab609c4, 0x94dd, 0x4d89, 0xbc60665d5090b828};
const NimBLEUUID SECURE_NOTIFICATION8_UUID {0x2a125640, 0x706d, 0x4dd1, 0xb420c0f4ab93c361};
const NimBLEUUID SECURE_NOTIFICATION9_UUID {0x82a9f452, 0xc5ce, 0x4ef5, 0x82033fc9a47f8171};
const NimBLEUUID SECURE_NOTIFICATION10_UUID {0xdeef7187, 0x3f43, 0x4364, 0x9e2211a8c8a15951};
const NimBLEUUID SECURE_SYNC_INTERVAL_UUID {0xc95d91ae, 0xb247, 0x4d6d, 0x86617dd5d6a0f85b};
const NimBLEUUID SHUTTER_SERVICE_UUID {0x6514eb81, 0x4e8f, 0x458d, 0xaa2ae691336cdfac};
const NimBLEUUID SHUTTER_CHARACTERISTIC_UUID {0x7fcf49c6, 0x4ff0, 0x4777, 0xa03d1a79166af7a8};
const NimBLEUUID GEOTAG_SERVICE_UUID {0x3b46ec2b, 0x48ba, 0x41fd, 0xb1b8ed860b60d22b};
const NimBLEUUID GEOTAG_CHARACTERISTIC_UUID {0x0f36ec14, 0x29e5, 0x411a, 0xa1b664ee8383f090};
const NimBLEUUID ADVERTISED_SERVICE_UUID {0xaf854c2e, 0xb214, 0x458e, 0x97e2912c4ecf2cb8};

bool matches(const NimBLEUUID &left, const NimBLEUUID &right) {
  return left == right;
}

}  // namespace

FujifilmVirtualCamera::FujifilmVirtualCamera() : FujifilmVirtualCamera(Config {}) {}

FujifilmVirtualCamera::FujifilmVirtualCamera(const Config &config) : m_Config(config) {
  if (m_Config.advertised_services.empty()) {
    m_Config.advertised_services.push_back(m_Config.secure ? SECURE_ADVERTISED_SERVICE_UUID
                                                           : advertisedServiceUUID());
  }
}

FujifilmVirtualCamera::~FujifilmVirtualCamera() {
  cancelFlappyTimer();
}

NimBLEAdvertisedDevice FujifilmVirtualCamera::advertisement() const {
  NimBLEAdvertisedDevice device;
  // Both real advertisement forms carry the 0x02 type byte after the company
  // identifier: d8 04 02 + token (Basic) or d8 04 02 + serial (Secure).
  std::vector<uint8_t> manufacturer = {0xd8, 0x04, 0x02};
  if (m_Config.secure) {
    manufacturer.insert(manufacturer.end(), m_Config.serial.begin(), m_Config.serial.end());
  } else {
    manufacturer.insert(manufacturer.end(), m_Config.token.begin(), m_Config.token.end());
  }

  device.setAddress(m_Config.address);
  device.setName(m_Config.name);
  device.setManufacturerData(manufacturer.data(), manufacturer.size());
  device.setRSSI(-42);
  for (const auto &service : m_Config.advertised_services) {
    device.addServiceUUID(service);
  }
  return device;
}

bool FujifilmVirtualCamera::requestGeotag() {
  return emitNotification(configurationServiceUUID(), geotagRequestCharacteristicUUID(),
                          {0x01, 0x00});
}

bool FujifilmVirtualCamera::emitNotification(const NimBLEUUID &service,
                                             const NimBLEUUID &characteristic,
                                             const std::vector<uint8_t> &payload,
                                             bool indication) {
  const auto found = m_Subscriptions.find(key(service, characteristic));
  if ((found == m_Subscriptions.end()) || (found->second.callback == nullptr)) {
    return false;
  }

  Notification notification;
  notification.service = service.toString();
  notification.characteristic = characteristic.toString();
  notification.payload = payload;
  notification.indication = indication;
  m_Notifications.push_back(notification);

  if (matches(characteristic, configurationNotificationUUID()) && (payload.size() >= 2)
      && ((payload[0] == 0x01) || (payload[0] == 0x02)) && (payload[1] == 0x00)) {
    m_Configured = true;
  }
  if (matches(characteristic, geotagRequestCharacteristicUUID()) && (payload.size() >= 2)
      && (payload[0] == 0x01) && (payload[1] == 0x00)) {
    m_GeotagRequested = true;
  }

  std::vector<uint8_t> mutable_payload = payload;
  found->second.callback(found->second.remote,
                         mutable_payload.empty() ? nullptr : mutable_payload.data(),
                         mutable_payload.size(), !indication);
  return true;
}

const FujifilmVirtualCamera::Config &FujifilmVirtualCamera::config() const {
  return m_Config;
}

const std::vector<FujifilmVirtualCamera::Write> &FujifilmVirtualCamera::writes() const {
  return m_Writes;
}

const std::vector<FujifilmVirtualCamera::Notification> &FujifilmVirtualCamera::notifications()
    const {
  return m_Notifications;
}

const std::vector<uint8_t> &FujifilmVirtualCamera::lastGeotag() const {
  return m_LastGeotag;
}

const std::string &FujifilmVirtualCamera::identifier() const {
  return m_Identifier;
}

bool FujifilmVirtualCamera::connected() const {
  return m_Connected;
}

bool FujifilmVirtualCamera::tokenAccepted() const {
  return m_TokenAccepted;
}

bool FujifilmVirtualCamera::configured() const {
  return m_Configured;
}

bool FujifilmVirtualCamera::geotagRequested() const {
  return m_GeotagRequested;
}

size_t FujifilmVirtualCamera::accessAfterDrop() const {
  return m_AccessAfterDrop;
}

bool FujifilmVirtualCamera::subscriptionRequestedWithResponse(
    const NimBLEUUID &service,
    const NimBLEUUID &characteristic) const {
  const auto found = m_Subscriptions.find(key(service, characteristic));
  return found != m_Subscriptions.end() && found->second.response;
}

void FujifilmVirtualCamera::setStaleSubscribeSession(bool stale) {
  m_StaleSubscribeSession = stale;
}

void FujifilmVirtualCamera::setWithholdRegistration(bool withhold) {
  m_WithholdRegistration = withhold;
}

void FujifilmVirtualCamera::setRegistrationPayload(const std::vector<uint8_t> &payload) {
  m_RegistrationPayload = payload;
}

bool FujifilmVirtualCamera::emitStaleRegistration() {
  const auto current =
      m_Subscriptions.find(key(configurationServiceUUID(), configurationNotificationUUID()));
  if (!m_HaveStaleRegistration || (current == m_Subscriptions.end())
      || (m_StaleRegistration.callback == nullptr)) {
    return false;
  }

  std::vector<uint8_t> payload = {0x01, 0x00};
  m_StaleRegistration.callback(current->second.remote, payload.data(), payload.size(), true);
  return true;
}

void FujifilmVirtualCamera::suppressService(const NimBLEUUID &service) {
  m_SuppressedServices.push_back(service);
}

void FujifilmVirtualCamera::suppressCharacteristic(const NimBLEUUID &service,
                                                   const NimBLEUUID &characteristic) {
  m_SuppressedCharacteristics.emplace_back(service, characteristic);
}

void FujifilmVirtualCamera::failWrite(const NimBLEUUID &service, const NimBLEUUID &characteristic) {
  m_FailedWrites.emplace_back(service, characteristic);
}

void FujifilmVirtualCamera::setFlappy(uint32_t fail_attempts, uint32_t drop_after_ms) {
  cancelFlappyTimer();
  const std::lock_guard<std::recursive_mutex> lock(m_FlappyMutex);
  m_FlappyEnabled = (fail_attempts > 0) || (drop_after_ms > 0);
  m_FlappyFailAttempts = fail_attempts;
  m_FlappyFailRemaining = fail_attempts;
  m_FlappyDropAfterMs = drop_after_ms;
}

bool FujifilmVirtualCamera::isPairHandshakeWrite(const NimBLEUUID &service,
                                                 const NimBLEUUID &characteristic) const {
  return matches(service, m_Config.secure ? SECURE_PAIR_SERVICE_UUID : pairServiceUUID())
         && matches(characteristic,
                    m_Config.secure ? SECURE_STATUS_CHARACTERISTIC_UUID : pairCharacteristicUUID());
}

bool FujifilmVirtualCamera::flappyConsumeHandshakeFailure() {
  const std::lock_guard<std::recursive_mutex> lock(m_FlappyMutex);
  if (!m_FlappyEnabled || (m_FlappyFailRemaining == 0)) {
    return false;
  }
  m_FlappyFailRemaining--;
  return true;
}

void FujifilmVirtualCamera::armFlappyDrop(NimBLEClient &client) {
  if (m_FlappyDropAfterMs == 0) {
    return;
  }
  cancelFlappyTimer();
  const std::lock_guard<std::recursive_mutex> lock(m_FlappyMutex);
  m_FlappyCancel = false;
  m_FlappyThread = std::thread([this, &client]() {
    std::unique_lock<std::recursive_mutex> lock(m_FlappyMutex);
    if (m_FlappyCv.wait_for(lock, std::chrono::milliseconds(m_FlappyDropAfterMs),
                            [this]() { return m_FlappyCancel; })) {
      // Cancelled: the link went down through another path first.
      return;
    }
    if (!m_Connected || (m_Client != &client)) {
      return;
    }
    triggerStandbyDrop();
  });
}

bool FujifilmVirtualCamera::triggerStandbyDrop() {
  const std::lock_guard<std::recursive_mutex> lock(m_FlappyMutex);
  if (!m_Connected || (m_Client == nullptr)) {
    return false;
  }
  // Re-arm the handshake failure budget so the reconnect churns, then sever
  // the link. The Fujifilm protocol has no power notification, so the drop
  // is silent, exactly as the standby drop looks to the central.
  m_FlappyFailRemaining = m_FlappyFailAttempts;
  m_Client->mockDropLink(0x08, /*fire_callback=*/true);
  return true;
}

void FujifilmVirtualCamera::requestFlappyCancel() {
  const std::lock_guard<std::recursive_mutex> lock(m_FlappyMutex);
  m_FlappyCancel = true;
  m_FlappyCv.notify_all();
}

void FujifilmVirtualCamera::cancelFlappyTimer() {
  requestFlappyCancel();
  if (m_FlappyThread.joinable() && (m_FlappyThread.get_id() != std::this_thread::get_id())) {
    m_FlappyThread.join();
  }
}

void FujifilmVirtualCamera::dropLinkOnWrite(const NimBLEUUID &service,
                                            const NimBLEUUID &characteristic) {
  m_DropOnWrite.emplace_back(service, characteristic);
}

void FujifilmVirtualCamera::dropLinkDuringConnect(const NimBLEUUID &service,
                                                  const NimBLEUUID &characteristic) {
  m_DropDuringConnect.emplace_back(service, characteristic);
}

void FujifilmVirtualCamera::dropLinkOnSubscribe(const NimBLEUUID &service,
                                                const NimBLEUUID &characteristic) {
  m_DropOnSubscribe.emplace_back(service, characteristic);
}

bool FujifilmVirtualCamera::isServiceSuppressed(const NimBLEUUID &service) const {
  for (const auto &suppressed : m_SuppressedServices) {
    if (matches(suppressed, service)) {
      return true;
    }
  }
  return false;
}

bool FujifilmVirtualCamera::isCharacteristicSuppressed(const NimBLEUUID &service,
                                                       const NimBLEUUID &characteristic) const {
  for (const auto &suppressed : m_SuppressedCharacteristics) {
    if (matches(suppressed.first, service) && matches(suppressed.second, characteristic)) {
      return true;
    }
  }
  return false;
}

bool FujifilmVirtualCamera::isWriteFailed(const NimBLEUUID &service,
                                          const NimBLEUUID &characteristic) const {
  for (const auto &failed : m_FailedWrites) {
    if (matches(failed.first, service) && matches(failed.second, characteristic)) {
      return true;
    }
  }
  return false;
}

bool FujifilmVirtualCamera::isDropOnWrite(const NimBLEUUID &service,
                                          const NimBLEUUID &characteristic) const {
  for (const auto &drop : m_DropOnWrite) {
    if (matches(drop.first, service) && matches(drop.second, characteristic)) {
      return true;
    }
  }
  return false;
}

bool FujifilmVirtualCamera::isDropDuringConnect(const NimBLEUUID &service,
                                                const NimBLEUUID &characteristic) const {
  for (const auto &drop : m_DropDuringConnect) {
    if (matches(drop.first, service) && matches(drop.second, characteristic)) {
      return true;
    }
  }
  return false;
}

bool FujifilmVirtualCamera::isDropOnSubscribe(const NimBLEUUID &service,
                                              const NimBLEUUID &characteristic) const {
  for (const auto &drop : m_DropOnSubscribe) {
    if (matches(drop.first, service) && matches(drop.second, characteristic)) {
      return true;
    }
  }
  return false;
}

void FujifilmVirtualCamera::clearEvents() {
  m_Writes.clear();
  m_Notifications.clear();
  m_LastGeotag.clear();
}

void FujifilmVirtualCamera::faultNextOperation(std::function<void(NimBLEClient &)> fault) {
  m_OperationFault = std::move(fault);
}

void FujifilmVirtualCamera::runOperationFault(NimBLEClient &client) {
  if (!m_OperationFault) {
    return;
  }
  // One shot. Move the fault out first so a reinstall from the fault itself
  // cannot recurse, then run it. The fault may free the remote service and
  // characteristic objects this operation was reached through, so callers run
  // this only after the operation result is computed and touch nothing after.
  auto fault = std::move(m_OperationFault);
  m_OperationFault = nullptr;
  fault(client);
}

void FujifilmVirtualCamera::clearFaults() {
  cancelFlappyTimer();
  {
    const std::lock_guard<std::recursive_mutex> lock(m_FlappyMutex);
    m_FlappyEnabled = false;
    m_FlappyFailAttempts = 0;
    m_FlappyFailRemaining = 0;
    m_FlappyDropAfterMs = 0;
  }
  m_SuppressedServices.clear();
  m_SuppressedCharacteristics.clear();
  m_FailedWrites.clear();
  m_DropOnWrite.clear();
  m_DropDuringConnect.clear();
  m_DropOnSubscribe.clear();
  m_OperationFault = nullptr;
  m_StaleSubscribeSession = false;
  {
    const std::lock_guard<std::mutex> lock(m_StallMutex);
    m_SecureConnectionStallMs = 0;
    m_StallLinkDown = true;
  }
  m_StallSignal.notify_all();
  m_SecureConnectionDropsLink = false;
  m_SecureTimeoutsRemaining = 0;
  m_RefuseWhileBonded = false;
  {
    const std::lock_guard<std::mutex> lock(m_StallMutex);
    m_SecureConnectionStallMs = 0;
    m_StallLinkDown = true;
  }
  m_StallSignal.notify_all();
  m_RequireLongConnParamsAfterIdentifier = false;
  m_DelayRegistrationConnParamsUntilFastRequest = false;
  m_RequestConnParamsOnSubscribe = false;
  m_RejectFastBeforeShutterDiscovery = false;
}

bool FujifilmVirtualCamera::acceptConnection(NimBLEClient &client, const NimBLEAddress &address) {
  if (m_Connected || (address != m_Config.address)) {
    return false;
  }
  m_Client = &client;
  m_Connected = true;
  m_TokenAccepted = false;
  m_Configured = false;
  m_GeotagRequested = false;
  m_Identifier.clear();
  m_LastGeotag.clear();
  m_ConnParamsNegotiated = false;
  m_DroppedLink = false;
  m_AccessAfterDrop = 0;
  m_ShutterCharacteristicRequested = false;
  m_Subscriptions.clear();
  if (m_RequestConnParamsDuringConnect) {
    m_RegistrationConnParamsAccepted = client.mockPeerRequestConnParams(m_RegistrationConnParams);
  }
  return true;
}

void FujifilmVirtualCamera::disconnect(NimBLEClient &client, int reason) {
  (void)reason;
  {
    // Release a handshake parked in secureConnection(). This runs on whichever
    // thread issued the terminate, so it is the only place the stall can be
    // ended from. Unconditional: a terminate may arrive for a client the peer
    // has already dropped, and a stall left parked would hang the harness.
    const std::lock_guard<std::mutex> lock(m_StallMutex);
    m_StallLinkDown = true;
  }
  m_StallSignal.notify_all();

  if (m_Client == &client) {
    // Cancel the pending flappy drop for this session only: a stale or
    // foreign client's teardown must not disarm the current session's timer.
    // No join here: this may run on the drop timer's own thread
    // (mockDropLink -> peer disconnect) where a join would deadlock. A
    // canceller on another thread blocks on the recursive mutex until an
    // in-flight drop finishes, so the ordering stays safe.
    requestFlappyCancel();
    m_Client = nullptr;
    m_Connected = false;
    const auto registration =
        m_Subscriptions.find(key(configurationServiceUUID(), configurationNotificationUUID()));
    if (registration != m_Subscriptions.end()) {
      m_StaleRegistration = registration->second;
      m_HaveStaleRegistration = true;
    }
    m_Subscriptions.clear();
  }
}

bool FujifilmVirtualCamera::hasService(const NimBLEUUID &service) const {
  if (m_DroppedLink)
    m_AccessAfterDrop++;
  if (isServiceSuppressed(service)) {
    return false;
  }
  return matches(service, m_Config.secure ? SECURE_PAIR_SERVICE_UUID : pairServiceUUID())
         || matches(service, configurationServiceUUID())
         || (m_Config.secure && matches(service, SECURE_NOTIFICATION_SERVICE_UUID))
         || matches(service, shutterServiceUUID()) || matches(service, geotagServiceUUID());
}

bool FujifilmVirtualCamera::hasCharacteristic(const NimBLEUUID &service,
                                              const NimBLEUUID &characteristic) const {
  if (m_DroppedLink)
    m_AccessAfterDrop++;
  if (isServiceSuppressed(service) || isCharacteristicSuppressed(service, characteristic)) {
    return false;
  }
  if (matches(service, m_Config.secure ? SECURE_PAIR_SERVICE_UUID : pairServiceUUID())) {
    return matches(characteristic,
                   m_Config.secure ? SECURE_STATUS_CHARACTERISTIC_UUID : pairCharacteristicUUID())
           || matches(characteristic, identifierCharacteristicUUID());
  }
  if (matches(service, configurationServiceUUID())) {
    return matches(characteristic, configurationIndication1UUID())
           || matches(characteristic, configurationIndication2UUID())
           || matches(characteristic, configurationNotificationUUID())
           || matches(characteristic, geotagRequestCharacteristicUUID())
           || matches(characteristic, configurationIndication3UUID());
  }
  if (m_Config.secure && matches(service, SECURE_NOTIFICATION_SERVICE_UUID)) {
    return matches(characteristic, SECURE_NOTIFICATION6_UUID)
           || matches(characteristic, SECURE_NOTIFICATION4_UUID)
           || matches(characteristic, SECURE_NOTIFICATION5_UUID)
           || matches(characteristic, SECURE_NOTIFICATION7_UUID)
           || matches(characteristic, SECURE_NOTIFICATION8_UUID)
           || matches(characteristic, SECURE_NOTIFICATION9_UUID)
           || matches(characteristic, SECURE_NOTIFICATION10_UUID)
           || matches(characteristic, SECURE_SYNC_INTERVAL_UUID);
  }
  if (matches(service, shutterServiceUUID())) {
    const_cast<FujifilmVirtualCamera *>(this)->m_ShutterCharacteristicRequested = true;
    return matches(characteristic, shutterCharacteristicUUID());
  }
  if (matches(service, geotagServiceUUID())) {
    return matches(characteristic, geotagCharacteristicUUID());
  }
  return false;
}

bool FujifilmVirtualCamera::discoverCharacteristic(NimBLEClient &client,
                                                   const NimBLEUUID &service,
                                                   const NimBLEUUID &characteristic) {
  (void)client;
  (void)service;
  (void)characteristic;
  return true;
}

bool FujifilmVirtualCamera::canWrite(const NimBLEUUID &service,
                                     const NimBLEUUID &characteristic) const {
  return (matches(service, m_Config.secure ? SECURE_PAIR_SERVICE_UUID : pairServiceUUID())
          && (matches(characteristic, m_Config.secure ? SECURE_STATUS_CHARACTERISTIC_UUID
                                                      : pairCharacteristicUUID())
              || matches(characteristic, identifierCharacteristicUUID())))
         || (m_Config.secure && matches(service, SECURE_NOTIFICATION_SERVICE_UUID)
             && matches(characteristic, SECURE_SYNC_INTERVAL_UUID))
         || (matches(service, shutterServiceUUID())
             && matches(characteristic, shutterCharacteristicUUID()))
         || (matches(service, geotagServiceUUID())
             && matches(characteristic, geotagCharacteristicUUID()));
}

bool FujifilmVirtualCamera::write(NimBLEClient &client,
                                  const NimBLEUUID &service,
                                  const NimBLEUUID &characteristic,
                                  const std::vector<uint8_t> &value,
                                  bool response) {
  if (!m_Connected || (m_Client != &client) || !canWrite(service, characteristic)) {
    if (m_DroppedLink)
      m_AccessAfterDrop++;
    return false;
  }

  // Fault injection: the peer accepts the GATT write at the ATT layer but
  // returns an error status, so the central sees the write fail. A handshake
  // write that fails this way must abort the connect and reclaim the client.
  if (isWriteFailed(service, characteristic)) {
    return false;
  }

  // FlappyPeer: a standby camera accepts the link but fails the pairing
  // handshake for the configured number of attempts before completing one.
  if (isPairHandshakeWrite(service, characteristic) && flappyConsumeHandshakeFailure()) {
    return false;
  }

  // Fault injection: a supervision-timeout link loss lands on this write. Sever
  // the link and deliver onDisconnect inline (so the central's connected flag
  // clears mid-handshake), then report the write as failed so _connect unwinds.
  if (isDropOnWrite(service, characteristic)) {
    client.mockDropLink(0x08, /*fire_callback=*/true);
    return false;
  }

  Write write_event;
  write_event.service = service.toString();
  write_event.characteristic = characteristic.toString();
  write_event.payload = value;
  write_event.response = response;
  m_Writes.push_back(write_event);

  bool result = false;
  if (matches(service, m_Config.secure ? SECURE_PAIR_SERVICE_UUID : pairServiceUUID())
      && matches(characteristic,
                 m_Config.secure ? SECURE_STATUS_CHARACTERISTIC_UUID : pairCharacteristicUUID())) {
    if (m_Config.secure) {
      result = value.size() == 4;
    } else {
      const std::vector<uint8_t> expected(m_Config.token.begin(), m_Config.token.end());
      m_TokenAccepted = (value == expected);
      result = m_TokenAccepted;
    }
  } else if (matches(service, m_Config.secure ? SECURE_PAIR_SERVICE_UUID : pairServiceUUID())
             && matches(characteristic, identifierCharacteristicUUID())) {
    m_Identifier.assign(value.begin(), value.end());
    result = m_Config.secure || m_TokenAccepted;
    if (result && m_RequireLongConnParamsAfterIdentifier
        && !m_DelayRegistrationConnParamsUntilFastRequest && !m_ConnParamsNegotiated) {
      ble_gap_upd_params params = {};
      params.itvl_min = 24;
      params.itvl_max = 40;
      params.latency = 0;
      params.supervision_timeout = 2000;
      m_RegistrationConnParams = params;
      m_RegistrationConnParamsAccepted = client.mockPeerRequestConnParams(params);
      if (!m_RegistrationConnParamsAccepted) {
        client.mockDropLink(0x08, true);
        return false;
      }
      m_ConnParamsNegotiated = true;
    }
  } else if (m_Config.secure && matches(service, SECURE_NOTIFICATION_SERVICE_UUID)
             && matches(characteristic, SECURE_SYNC_INTERVAL_UUID)) {
    result = value.size() == 2;
  } else if (matches(service, geotagServiceUUID())
             && matches(characteristic, geotagCharacteristicUUID())) {
    m_LastGeotag = value;
    result = m_GeotagRequested;
  } else if (matches(service, shutterServiceUUID())
             && matches(characteristic, shutterCharacteristicUUID())) {
    result = (value.size() == 2);
  }

  // FlappyPeer: the handshake completed, arm the autonomous standby drop.
  if (m_FlappyEnabled && result && isPairHandshakeWrite(service, characteristic)) {
    armFlappyDrop(client);
  }

  // Fault injection: the peer resets mid-handshake. The write itself completes
  // (result keeps its computed value), then the link is severed with an inline
  // self-deleting drop, exactly as the NimBLE host task frees a setSelfDelete
  // client after its disconnect callback. Do this last and touch nothing after:
  // it may free this client and the characteristic this write was reached
  // through, so _connect() continuing to its next m_Client dereference is what
  // this exercises.
  if (isDropDuringConnect(service, characteristic)) {
    client.mockDropLinkSelfDelete(0x08);
  }
  // In-flight fault window: the write result is final, the operation has not
  // yet returned to the transport, and the service and characteristic
  // references may be freed by the fault. Touch nothing after this.
  runOperationFault(client);
  return result;
}

NimBLEAttValue FujifilmVirtualCamera::read(NimBLEClient &client,
                                           const NimBLEUUID &service,
                                           const NimBLEUUID &characteristic) {
  if (m_DroppedLink)
    m_AccessAfterDrop++;
  NimBLEAttValue result;
  if (m_Connected && (m_Client == &client) && hasCharacteristic(service, characteristic)
      && m_Config.secure && matches(service, SECURE_PAIR_SERVICE_UUID)
      && matches(characteristic, SECURE_STATUS_CHARACTERISTIC_UUID)) {
    result = NimBLEAttValue({0x07, 0x96, 0x00, 0x00});
  }
  // In-flight fault window: the read result is final, the operation has not
  // yet returned to the transport, and the service and characteristic
  // references may be freed by the fault. Touch nothing after this.
  runOperationFault(client);
  return result;
}

bool FujifilmVirtualCamera::subscribe(NimBLEClient &client,
                                      const NimBLEUUID &service,
                                      const NimBLEUUID &characteristic,
                                      bool notification,
                                      NimBLERemoteCharacteristic *remote,
                                      const NimBLENotifyCallback &callback,
                                      bool response) {
  if (m_DroppedLink)
    m_AccessAfterDrop++;
  if (!m_Connected || (m_Client != &client) || !hasCharacteristic(service, characteristic)
      || (callback == nullptr)) {
    return false;
  }

  // Stale-session reconnect: the camera still holds the CCCD subscription from
  // the previous session. An acknowledged CCCD write never gets its ATT write
  // response, which blocks the connect forever on hardware. Model that block as
  // a write failure. An unacknowledged write (response = false) does not wait
  // for a response, so it still succeeds, which is the bounded path the fix
  // takes.
  const bool requiredSecureIndication = m_Config.secure
                                        && (characteristic == configurationIndication1UUID()
                                            || characteristic == configurationIndication2UUID());
  if (m_StaleSubscribeSession && response && !requiredSecureIndication) {
    return false;
  }

  // A real Secure camera can defer its registration connection-parameter
  // request until the first indication CCCD is enabled.  Exercise that timing
  // explicitly: rejecting the request must sever the link, as the camera does
  // when its registration contract is not met.
  if (m_RequestConnParamsOnSubscribe && (service == m_ConnParamsSubscribeService)
      && (characteristic == m_ConnParamsSubscribeCharacteristic) && !m_ConnParamsNegotiated) {
    m_RegistrationConnParamsAccepted = client.mockPeerRequestConnParams(m_RegistrationConnParams);
    if (!m_RegistrationConnParamsAccepted) {
      client.mockDropLink(0x08, true);
      return false;
    }
    m_ConnParamsNegotiated = true;
  }

  if (isDropOnSubscribe(service, characteristic)) {
    m_DroppedLink = true;
    client.mockDropLink(0x08, true);
    return false;
  }

  Subscription subscription;
  subscription.client = &client;
  subscription.remote = remote;
  subscription.callback = callback;
  subscription.notification = notification;
  subscription.response = response;
  m_Subscriptions[key(service, characteristic)] = subscription;

  if (matches(characteristic, configurationNotificationUUID()) && !m_WithholdRegistration) {
    // This is the X100VI capture: CHR_NOT1_UUID carries 01 00. The production
    // protocol helper also accepts the legacy 02 00 Basic form.
    emitNotification(service, characteristic, m_RegistrationPayload, false);
  }
  return true;
}

uint32_t FujifilmVirtualCamera::secureInitiateCount() const {
  return m_SecureInitiates.load();
}

bool FujifilmVirtualCamera::secureConnection(NimBLEClient &client) {
  m_SecureInitiates++;
  // Block first, like the real call. Parking here rather than polling a cancel
  // is the point: this is the one wait in the connect path that no token can
  // shorten. Only a link terminate ends it early, which is what the peer's own
  // disconnect() signals below.
  {
    std::unique_lock<std::mutex> lock(m_StallMutex);
    if (m_SecureConnectionStallMs > 0) {
      const uint32_t stall = m_SecureConnectionStallMs;
      m_StallLinkDown = false;
      m_StallAborted = false;
      m_StallEntries++;
      const bool terminated = waitForStallLocked(lock, stall);
      if (terminated) {
        m_StallAborted = true;
      }
      // Either way the handshake failed: NimBLE reports the failure to the
      // parked caller both when the link dies under it and when the pairing
      // timeout expires.
      return false;
    }
  }
  if (m_SecureTimeoutsRemaining > 0) {
    // The bench signature: the handshake times out and the link goes with it,
    // so the connect task sees the failure with the link already down.
    if (m_SecureTimeoutsRemaining != kSecureTimeoutAlways) {
      m_SecureTimeoutsRemaining--;
    }
    client.mockDropLink(0x08, true);
    return false;
  }
  if (m_RefuseWhileBonded && NimBLEDevice::isBonded(m_Config.address)) {
    // Dead keys are refused, but the camera stays on the link so a fresh
    // pairing can still be made once the stale bond is deleted.
    return false;
  }
  if (m_SecureConnectionDropsLink) {
    // The standby-timeout shape: the link dies under the encryption handshake
    // and the failure reaches the caller only after onDisconnect cleared the
    // link state, so the stale-bond recovery must not treat it as rejection.
    client.mockDropLink(0x08, true);
    return false;
  }
  return m_SecureConnectionResult && m_Connected && (m_Client == &client);
}

void FujifilmVirtualCamera::setSecureConnectionResult(bool result) {
  m_SecureConnectionResult = result;
}

bool FujifilmVirtualCamera::waitForStallLocked(std::unique_lock<std::mutex> &lock,
                                               uint32_t stallMs) {
  if (Host::peerStallFunction().load(std::memory_order_acquire) == nullptr) {
    // Host harness: park on the condition variable, on the host clock, exactly
    // as the terminate expects.
    return m_StallSignal.wait_for(lock, std::chrono::milliseconds(stallMs),
                                  [this]() { return m_StallLinkDown; });
  }

  // Simulator: the deadline belongs to the virtual clock, which no host
  // condition variable can wait on. Spend it in slices on the installed clock
  // and read the terminate flag between them, so a terminate still ends the
  // wait early. The slice is the terminate's resolution, not the model's: it is
  // one control tick, so an abort is observed in the same tick it lands.
  constexpr uint32_t STALL_SLICE_MS = 50;
  uint32_t remaining = stallMs;
  while (remaining != 0) {
    const uint32_t slice = remaining < STALL_SLICE_MS ? remaining : STALL_SLICE_MS;
    lock.unlock();
    Host::peerStall(slice);
    lock.lock();
    if (m_StallLinkDown) {
      return true;
    }
    remaining -= slice;
  }
  return m_StallLinkDown;
}

void FujifilmVirtualCamera::setSecureConnectionStallMs(uint32_t stallMs) {
  const std::lock_guard<std::mutex> lock(m_StallMutex);
  m_SecureConnectionStallMs = stallMs;
}

bool FujifilmVirtualCamera::secureStallWasAborted() const {
  const std::lock_guard<std::mutex> lock(m_StallMutex);
  return m_StallAborted;
}

uint32_t FujifilmVirtualCamera::secureStallEntries() const {
  const std::lock_guard<std::mutex> lock(m_StallMutex);
  return m_StallEntries;
}

void FujifilmVirtualCamera::setSecureConnectionDropsLink(bool drop) {
  m_SecureConnectionDropsLink = drop;
}

void FujifilmVirtualCamera::setSecureTimeouts(uint32_t attempts) {
  m_SecureTimeoutsRemaining = attempts;
}

void FujifilmVirtualCamera::setRefuseWhileBonded(bool refuse) {
  m_RefuseWhileBonded = refuse;
}

void FujifilmVirtualCamera::setRequireLongConnParamsAfterIdentifier(bool require) {
  m_RequireLongConnParamsAfterIdentifier = require;
}

void FujifilmVirtualCamera::setDelayRegistrationConnParamsUntilFastRequest(bool delay) {
  m_DelayRegistrationConnParamsUntilFastRequest = delay;
  if (delay) {
    m_RegistrationConnParams.itvl_min = 24;
    m_RegistrationConnParams.itvl_max = 40;
    m_RegistrationConnParams.latency = 0;
    m_RegistrationConnParams.supervision_timeout = 2000;
  }
}

void FujifilmVirtualCamera::setRejectFastBeforeShutterDiscovery(bool reject) {
  m_RejectFastBeforeShutterDiscovery = reject;
}

void FujifilmVirtualCamera::requestConnParamsDuringConnect(const ble_gap_upd_params &params) {
  m_RegistrationConnParams = params;
  m_RequestConnParamsDuringConnect = true;
}

void FujifilmVirtualCamera::requestConnParamsOnSubscribe(const NimBLEUUID &service,
                                                         const NimBLEUUID &characteristic,
                                                         const ble_gap_upd_params &params) {
  m_ConnParamsSubscribeService = service;
  m_ConnParamsSubscribeCharacteristic = characteristic;
  m_RegistrationConnParams = params;
  m_RequestConnParamsOnSubscribe = true;
}

bool FujifilmVirtualCamera::registrationConnParamsAccepted() const {
  return m_RegistrationConnParamsAccepted;
}

bool FujifilmVirtualCamera::updateConnectionParams(NimBLEClient &client,
                                                   uint16_t min_interval,
                                                   uint16_t max_interval,
                                                   uint16_t latency,
                                                   uint16_t timeout) {
  if (m_RequireLongConnParamsAfterIdentifier && m_DelayRegistrationConnParamsUntilFastRequest
      && !m_ConnParamsNegotiated) {
    m_RegistrationConnParamsAccepted = client.mockPeerRequestConnParams(m_RegistrationConnParams);
    if (!m_RegistrationConnParamsAccepted) {
      client.mockDropLink(0x08, true);
      return false;
    }
    m_ConnParamsNegotiated = true;
  }
  if (m_RejectFastBeforeShutterDiscovery && !m_ShutterCharacteristicRequested
      && timeout == (2 * BLE_GAP_INITIAL_SUPERVISION_TIMEOUT)) {
    client.mockDropLink(0x08, true);
    return false;
  }
  (void)min_interval;
  (void)max_interval;
  (void)latency;
  (void)timeout;
  return m_Connected && (m_Client == &client);
}

int FujifilmVirtualCamera::getRssi() const {
  return -42;
}

const NimBLEUUID &FujifilmVirtualCamera::pairServiceUUID() {
  return PAIR_SERVICE_UUID;
}

const NimBLEUUID &FujifilmVirtualCamera::pairCharacteristicUUID() {
  return PAIR_CHARACTERISTIC_UUID;
}

const NimBLEUUID &FujifilmVirtualCamera::identifierCharacteristicUUID() {
  return IDENTIFIER_CHARACTERISTIC_UUID;
}

const NimBLEUUID &FujifilmVirtualCamera::configurationServiceUUID() {
  return CONFIGURATION_SERVICE_UUID;
}

const NimBLEUUID &FujifilmVirtualCamera::configurationNotificationUUID() {
  return CONFIGURATION_NOTIFICATION_UUID;
}

const NimBLEUUID &FujifilmVirtualCamera::geotagRequestCharacteristicUUID() {
  return GEOTAG_REQUEST_CHARACTERISTIC_UUID;
}

const NimBLEUUID &FujifilmVirtualCamera::configurationIndication1UUID() {
  return CONFIGURATION_INDICATION1_UUID;
}

const NimBLEUUID &FujifilmVirtualCamera::configurationIndication2UUID() {
  return CONFIGURATION_INDICATION2_UUID;
}

const NimBLEUUID &FujifilmVirtualCamera::configurationIndication3UUID() {
  return CONFIGURATION_INDICATION3_UUID;
}

const NimBLEUUID &FujifilmVirtualCamera::shutterServiceUUID() {
  return SHUTTER_SERVICE_UUID;
}

const NimBLEUUID &FujifilmVirtualCamera::shutterCharacteristicUUID() {
  return SHUTTER_CHARACTERISTIC_UUID;
}

const NimBLEUUID &FujifilmVirtualCamera::geotagServiceUUID() {
  return GEOTAG_SERVICE_UUID;
}

const NimBLEUUID &FujifilmVirtualCamera::geotagCharacteristicUUID() {
  return GEOTAG_CHARACTERISTIC_UUID;
}

const NimBLEUUID &FujifilmVirtualCamera::advertisedServiceUUID() {
  return ADVERTISED_SERVICE_UUID;
}

std::string FujifilmVirtualCamera::key(const NimBLEUUID &service,
                                       const NimBLEUUID &characteristic) {
  return service.toString() + "/" + characteristic.toString();
}

}  // namespace Host
}  // namespace Furble
