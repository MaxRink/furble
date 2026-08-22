#include "FujifilmVirtualCamera.h"

namespace Furble {
namespace Host {

namespace {

const NimBLEUUID PAIR_SERVICE_UUID {0x91f1de68, 0xdff6, 0x466e, 0x8b65ff13b0f16fb8};
const NimBLEUUID PAIR_CHARACTERISTIC_UUID {0xaba356eb, 0x9633, 0x4e60, 0xb73ff52516dbd671};
const NimBLEUUID IDENTIFIER_CHARACTERISTIC_UUID {0x85b9163e, 0x62d1, 0x49ff, 0xa6f5054b4630d4a1};
const NimBLEUUID CONFIGURATION_SERVICE_UUID {0x4c0020fe, 0xf3b6, 0x40de, 0xacc977d129067b14};
const NimBLEUUID CONFIGURATION_NOTIFICATION_UUID {0xf9150137, 0x5d40, 0x4801, 0xa8dcf7fc5b01da50};
const NimBLEUUID GEOTAG_REQUEST_CHARACTERISTIC_UUID {0xad06c7b7, 0xf41a, 0x46f4,
                                                     0xa29a712055319122};
const NimBLEUUID CONFIGURATION_INDICATION1_UUID {0xa68e3f66, 0x0fcc, 0x4395, 0x8d4caa980b5877fa};
const NimBLEUUID CONFIGURATION_INDICATION2_UUID {0xbd17ba04, 0xb76b, 0x4892, 0xa545b73ba1f74dae};
const NimBLEUUID CONFIGURATION_INDICATION3_UUID {0x049ec406, 0xef75, 0x4205, 0xa39008fe209c51f0};
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
    m_Config.advertised_services.push_back(advertisedServiceUUID());
  }
}

NimBLEAdvertisedDevice FujifilmVirtualCamera::advertisement() const {
  NimBLEAdvertisedDevice device;
  const std::array<uint8_t, 7> manufacturer = {
      0xd8, 0x04, 0x02, m_Config.token[0], m_Config.token[1], m_Config.token[2], m_Config.token[3],
  };

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
      && (payload[0] == 0x02) && (payload[1] == 0x00)) {
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

void FujifilmVirtualCamera::setStaleSubscribeSession(bool stale) {
  m_StaleSubscribeSession = stale;
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

void FujifilmVirtualCamera::dropLinkOnWrite(const NimBLEUUID &service,
                                            const NimBLEUUID &characteristic) {
  m_DropOnWrite.emplace_back(service, characteristic);
}

void FujifilmVirtualCamera::dropLinkDuringConnect(const NimBLEUUID &service,
                                                  const NimBLEUUID &characteristic) {
  m_DropDuringConnect.emplace_back(service, characteristic);
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

void FujifilmVirtualCamera::clearEvents() {
  m_Writes.clear();
  m_Notifications.clear();
  m_LastGeotag.clear();
}

void FujifilmVirtualCamera::clearFaults() {
  m_SuppressedServices.clear();
  m_SuppressedCharacteristics.clear();
  m_FailedWrites.clear();
  m_DropOnWrite.clear();
  m_DropDuringConnect.clear();
  m_StaleSubscribeSession = false;
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
  m_Subscriptions.clear();
  return true;
}

void FujifilmVirtualCamera::disconnect(NimBLEClient &client, int reason) {
  (void)reason;
  if (m_Client == &client) {
    m_Client = nullptr;
    m_Connected = false;
    m_Subscriptions.clear();
  }
}

bool FujifilmVirtualCamera::hasService(const NimBLEUUID &service) const {
  if (isServiceSuppressed(service)) {
    return false;
  }
  return matches(service, pairServiceUUID()) || matches(service, configurationServiceUUID())
         || matches(service, shutterServiceUUID()) || matches(service, geotagServiceUUID());
}

bool FujifilmVirtualCamera::hasCharacteristic(const NimBLEUUID &service,
                                              const NimBLEUUID &characteristic) const {
  if (isServiceSuppressed(service) || isCharacteristicSuppressed(service, characteristic)) {
    return false;
  }
  if (matches(service, pairServiceUUID())) {
    return matches(characteristic, pairCharacteristicUUID())
           || matches(characteristic, identifierCharacteristicUUID());
  }
  if (matches(service, configurationServiceUUID())) {
    return matches(characteristic, configurationIndication1UUID())
           || matches(characteristic, configurationIndication2UUID())
           || matches(characteristic, configurationNotificationUUID())
           || matches(characteristic, geotagRequestCharacteristicUUID())
           || matches(characteristic, configurationIndication3UUID());
  }
  if (matches(service, shutterServiceUUID())) {
    return matches(characteristic, shutterCharacteristicUUID());
  }
  if (matches(service, geotagServiceUUID())) {
    return matches(characteristic, geotagCharacteristicUUID());
  }
  return false;
}

bool FujifilmVirtualCamera::canWrite(const NimBLEUUID &service,
                                     const NimBLEUUID &characteristic) const {
  return (matches(service, pairServiceUUID())
          && (matches(characteristic, pairCharacteristicUUID())
              || matches(characteristic, identifierCharacteristicUUID())))
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
    return false;
  }

  // Fault injection: the peer accepts the GATT write at the ATT layer but
  // returns an error status, so the central sees the write fail. A handshake
  // write that fails this way must abort the connect and reclaim the client.
  if (isWriteFailed(service, characteristic)) {
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
  if (matches(service, pairServiceUUID()) && matches(characteristic, pairCharacteristicUUID())) {
    const std::vector<uint8_t> expected(m_Config.token.begin(), m_Config.token.end());
    m_TokenAccepted = (value == expected);
    result = m_TokenAccepted;
  } else if (matches(service, pairServiceUUID())
             && matches(characteristic, identifierCharacteristicUUID())) {
    m_Identifier.assign(value.begin(), value.end());
    result = m_TokenAccepted;
  } else if (matches(service, geotagServiceUUID())
             && matches(characteristic, geotagCharacteristicUUID())) {
    m_LastGeotag = value;
    result = m_GeotagRequested;
  } else if (matches(service, shutterServiceUUID())
             && matches(characteristic, shutterCharacteristicUUID())) {
    result = (value.size() == 2);
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
  return result;
}

NimBLEAttValue FujifilmVirtualCamera::read(NimBLEClient &client,
                                           const NimBLEUUID &service,
                                           const NimBLEUUID &characteristic) {
  (void)client;
  (void)service;
  (void)characteristic;
  return {};
}

bool FujifilmVirtualCamera::subscribe(NimBLEClient &client,
                                      const NimBLEUUID &service,
                                      const NimBLEUUID &characteristic,
                                      bool notification,
                                      NimBLERemoteCharacteristic *remote,
                                      const NimBLENotifyCallback &callback,
                                      bool response) {
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
  if (m_StaleSubscribeSession && response) {
    return false;
  }

  Subscription subscription;
  subscription.client = &client;
  subscription.remote = remote;
  subscription.callback = callback;
  subscription.notification = notification;
  m_Subscriptions[key(service, characteristic)] = subscription;

  if (matches(characteristic, configurationNotificationUUID())) {
    emitNotification(service, characteristic, {0x02, 0x00}, false);
  }
  return true;
}

bool FujifilmVirtualCamera::secureConnection(NimBLEClient &client) {
  return m_Connected && (m_Client == &client);
}

bool FujifilmVirtualCamera::updateConnectionParams(NimBLEClient &client,
                                                   uint16_t min_interval,
                                                   uint16_t max_interval,
                                                   uint16_t latency,
                                                   uint16_t timeout) {
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
