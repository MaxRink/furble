#include <chrono>
#include <cstring>

#include "RicohVirtualCamera.h"

namespace Furble {
namespace Host {
namespace {

const NimBLEUUID INFO_SERVICE {0x9A5ED1C5, 0x74CC, 0x4C50, 0xB5B666A48E7CCFF1};
const NimBLEUUID MODEL_CHARACTERISTIC {0x35FE6272, 0x6AA5, 0x44D9, 0x88E1F09427F51A71};
const NimBLEUUID CAMERA_SERVICE {0x4B445988, 0xCAA0, 0x4DD3, 0x941D37B4F52ACA86};
const NimBLEUUID POWER_CHARACTERISTIC {0xB58CE84C, 0x0666, 0x4DE9, 0xBEC82D27B27B3211};
const NimBLEUUID OPERATION_MODE_CHARACTERISTIC {0x1452335A, 0xEC7F, 0x4877, 0xB8AB0F72E18BB295};
const NimBLEUUID GEOTAG_ENABLE_CHARACTERISTIC {0xA36AFDCF, 0x6B67, 0x4046, 0x9BE728FB67DBC071};
const NimBLEUUID SHOOTING_SERVICE {0x9F00F387, 0x8345, 0x4BBC, 0x8B92B87B52E3091A};
const NimBLEUUID SHOOTING_FLAVOR_CHARACTERISTIC {0xB29E6DE3, 0x1AEC, 0x48C1, 0x9D0502CEA57CE664};
const NimBLEUUID OPERATION_REQUEST_CHARACTERISTIC {0x559644B8, 0xE0BC, 0x4011, 0x929B5CF9199851E7};
const NimBLEUUID CAPTURE_STATUS_CHARACTERISTIC {0xB5589C08, 0xB5FD, 0x46F5, 0xBE7DAB1B8C074CAA};
const NimBLEUUID SELF_TIMER_CHARACTERISTIC {0x009A8E70, 0xB306, 0x4451, 0xB9437F54392EB971};
const NimBLEUUID BT_CONTROL_SERVICE {0x0F291746, 0x0C80, 0x4726, 0x87A73C501FD3B4B6};
const NimBLEUUID PAIRED_NAME_CHARACTERISTIC {0xFE3A32F8, 0xA189, 0x42DE, 0xA391BC81AE4DAA76};
const NimBLEUUID GPS_SERVICE {0x84A0DD62, 0xE8AA, 0x4D0F, 0x91DB819B6724C69E};
const NimBLEUUID GPS_CHARACTERISTIC {0x28F59D60, 0x8B8E, 0x4FCD, 0xA81F61BDB46595A9};

// Three byte-swapped float64 values followed by a native little-endian year.
// Evidence: rea-camera-firmware/analysis/ricoh-handlers.md section 6.
double readSwappedDouble(const std::vector<uint8_t> &value, size_t offset) {
  uint64_t bits = 0;
  for (size_t index = 0; index < 8; index++) {
    bits = (bits << 8) | value[offset + index];
  }
  double result = 0.0;
  std::memcpy(&result, &bits, sizeof(result));
  return result;
}

}  // namespace

RicohVirtualCamera::RicohVirtualCamera() : RicohVirtualCamera(Config {}) {}

RicohVirtualCamera::RicohVirtualCamera(const Config &config) : m_Config(config) {}

RicohVirtualCamera::~RicohVirtualCamera() {
  cancelFlappyTimer();
}

bool RicohVirtualCamera::matches(const NimBLEUUID &left, const NimBLEUUID &right) {
  return left == right;
}

std::string RicohVirtualCamera::key(const NimBLEUUID &service, const NimBLEUUID &characteristic) {
  return service.toString() + "/" + characteristic.toString();
}

NimBLEAdvertisedDevice RicohVirtualCamera::advertisement() const {
  NimBLEAdvertisedDevice device;
  device.setAddress(m_Config.address);
  device.setName(m_Config.name);
  device.setRSSI(-55);
  // GR III/IIIx manufacturer record: company identifier 0x065F, format 0xDA,
  // then typed fields 0x01 model code, 0x02 serial (four bytes little-endian),
  // 0x03 camera power. Evidence: ricoh-handlers.md section 10.2 and
  // rea-camera-firmware/spec/ricoh.md section 2.
  const std::vector<uint8_t> manufacturer = {
      0x5f,
      0x06,
      0xda,
      0x01,
      m_Config.model_code,
      0x02,
      m_Config.serial[0],
      m_Config.serial[1],
      m_Config.serial[2],
      m_Config.serial[3],
      0x03,
      static_cast<uint8_t>(m_Config.camera_power != 0 ? 0x01 : 0x00)};
  device.setManufacturerData(manufacturer.data(), manufacturer.size());
  // Only one service is ever advertised: Camera Information normally and
  // Bluetooth Control in sleep. Camera, Shooting and WLAN are never used as
  // advertising filters. Evidence: ricoh-handlers.md section 10.1.
  device.addServiceUUID(m_Config.sleep_advertising ? BT_CONTROL_SERVICE : INFO_SERVICE);
  return device;
}

bool RicohVirtualCamera::cameraBonded() const {
  return m_Config.camera_bonded;
}

void RicohVirtualCamera::removeCameraBond() {
  m_Config.camera_bonded = false;
}

void RicohVirtualCamera::setCameraPower(uint8_t power) {
  m_Config.camera_power = power;
}

void RicohVirtualCamera::setOperationMode(uint8_t mode) {
  m_Config.operation_mode = mode;
}

void RicohVirtualCamera::setOperationModeReadFails(bool fails) {
  m_Config.operation_mode_read_fails = fails;
}

uint8_t RicohVirtualCamera::cameraPower() const {
  return m_Config.camera_power;
}

bool RicohVirtualCamera::powerWriteAccepted() const {
  return m_PowerWriteAccepted;
}

bool RicohVirtualCamera::geotagEnabled() const {
  return m_GeotagEnabled;
}

const RicohVirtualCamera::GpsFix &RicohVirtualCamera::lastGps() const {
  return m_LastGps;
}

uint8_t RicohVirtualCamera::driveModeIndex() const {
  return m_DriveModeIndex;
}

const RicohVirtualCamera::DriveMode &RicohVirtualCamera::driveMode() const {
  return driveModeTable()[m_DriveModeIndex];
}

size_t RicohVirtualCamera::captureCount() const {
  return m_CaptureCount;
}

bool RicohVirtualCamera::lastOperationRequest(uint8_t &code, uint8_t &arg) const {
  code = m_LastOperationCode;
  arg = m_LastOperationArg;
  return m_HaveOperationRequest;
}

bool RicohVirtualCamera::selfTimerTriggered() const {
  return m_SelfTimerTriggered;
}

void RicohVirtualCamera::failWrite(const NimBLEUUID &service, const NimBLEUUID &characteristic) {
  m_FailedWrites.emplace_back(service, characteristic);
}

void RicohVirtualCamera::setBusy(bool busy) {
  m_Busy = busy;
}

void RicohVirtualCamera::setRepairRequest(bool repair) {
  m_RepairRequest = repair;
}

void RicohVirtualCamera::setPasskeyAccepted(bool accepted) {
  m_PasskeyAccepted = accepted;
}

bool RicohVirtualCamera::passkeyAccepted() const {
  return m_PasskeyAccepted;
}

void RicohVirtualCamera::clearFaults() {
  m_FailedWrites.clear();
  m_Busy = false;
  m_RepairRequest = false;
  m_PasskeyAccepted = true;
  m_Config.in_pairing_mode = true;
}

bool RicohVirtualCamera::isWriteFailed(const NimBLEUUID &service,
                                       const NimBLEUUID &characteristic) const {
  for (const auto &failed : m_FailedWrites) {
    if (matches(failed.first, service) && matches(failed.second, characteristic)) {
      return true;
    }
  }
  return false;
}

const std::array<RicohVirtualCamera::DriveMode, 16> &RicohVirtualCamera::driveModeTable() {
  // DriveMode::Write copies 16 records of (drive, self-timer) from the image
  // and sets properties 0x30 and 0x31. Index 0 is single frame with no timer.
  // Evidence: ricoh-handlers.md section 5.9.1.
  static const std::array<DriveMode, 16> table = {
      {
       {0, 0, 0},
       {1, 0, 10},
       {2, 0, 2},
       {3, 1, 0},
       {4, 10, 0},
       {5, 10, 10},
       {6, 10, 2},
       {7, 8, 0},
       {8, 8, 10},
       {9, 8, 2},
       {10, 5, 0},
       {11, 5, 10},
       {12, 5, 2},
       {13, 6, 0},
       {14, 6, 10},
       {15, 6, 2},
       }
  };
  return table;
}

bool RicohVirtualCamera::handleOperationRequest(const std::vector<uint8_t> &value) {
  // Accepted forms are exactly {0,1} stop, {1,0} start without the AF
  // argument, {1,1} start with the AF argument, and {2,*} compose. {1,2} and
  // every other pair is refused. Evidence: ricoh-handlers.md section 8.1.
  if (value.size() != 2) {
    return false;
  }
  const uint8_t code = value[0];
  const uint8_t arg = value[1];
  bool accepted = false;
  if (code == 0x01) {
    accepted = (arg == 0x00) || (arg == 0x01);
  } else if (code == 0x00) {
    accepted = (arg == 0x01);
  } else if (code == 0x02) {
    accepted = true;
  }
  if (!accepted) {
    return false;
  }
  // A capture command that the camera cannot service answers with an ATT
  // error while the link stays up. Evidence: ricoh-handlers.md section 2.1.
  if (m_Busy && (code == 0x01)) {
    return false;
  }
  m_HaveOperationRequest = true;
  m_LastOperationCode = code;
  m_LastOperationArg = arg;
  if (code == 0x01) {
    m_CaptureCount++;
  }
  return true;
}

bool RicohVirtualCamera::parseGpsFix(const std::vector<uint8_t> &value) {
  // Exactly 32 bytes. The camera validates nothing: it only fails when the
  // property write fails. Byte 31 is never read, so it is recorded and
  // otherwise ignored. Evidence: ricoh-handlers.md section 6.
  if (value.size() != 32) {
    return false;
  }
  m_LastGps.latitude = readSwappedDouble(value, 0);
  m_LastGps.longitude = readSwappedDouble(value, 8);
  m_LastGps.altitude = readSwappedDouble(value, 16);
  m_LastGps.year = static_cast<uint16_t>(value[24] | (static_cast<uint16_t>(value[25]) << 8));
  m_LastGps.month = value[26];
  m_LastGps.day = value[27];
  m_LastGps.hour = value[28];
  m_LastGps.minute = value[29];
  m_LastGps.second = value[30];
  m_LastGps.trailing = value[31];
  m_LastGps.valid = true;
  return true;
}

const std::vector<RicohVirtualCamera::Write> &RicohVirtualCamera::writes() const {
  return m_Writes;
}

const std::vector<RicohVirtualCamera::Notification> &RicohVirtualCamera::notifications() const {
  return m_Notifications;
}

void RicohVirtualCamera::clearEvents() {
  m_Writes.clear();
  m_Notifications.clear();
}

bool RicohVirtualCamera::emitNotification(const NimBLEUUID &service,
                                          const NimBLEUUID &characteristic,
                                          const std::vector<uint8_t> &payload) {
  // m_FlappyMutex also guards m_Subscriptions: the drop timer emits on its own
  // thread while the central subscribes and tears down on another. Recursive,
  // so the timer (which already holds it) re-enters without deadlock.
  const std::lock_guard<std::recursive_mutex> lock(m_FlappyMutex);
  const auto found = m_Subscriptions.find(key(service, characteristic));
  if ((found == m_Subscriptions.end()) || (found->second.callback == nullptr)) {
    return false;
  }

  Notification notification;
  notification.service = service.toString();
  notification.characteristic = characteristic.toString();
  notification.payload = payload;
  m_Notifications.push_back(notification);

  std::vector<uint8_t> mutable_payload = payload;
  found->second.callback(found->second.remote,
                         mutable_payload.empty() ? nullptr : mutable_payload.data(),
                         mutable_payload.size(), found->second.notification);
  return true;
}

void RicohVirtualCamera::setFlappy(uint32_t fail_attempts, uint32_t drop_after_ms) {
  cancelFlappyTimer();
  const std::lock_guard<std::recursive_mutex> lock(m_FlappyMutex);
  m_FlappyEnabled = (fail_attempts > 0) || (drop_after_ms > 0);
  m_FlappyFailAttempts = fail_attempts;
  m_FlappyFailRemaining = fail_attempts;
  m_FlappyDropAfterMs = drop_after_ms;
}

void RicohVirtualCamera::armFlappyDrop(NimBLEClient &client) {
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

bool RicohVirtualCamera::triggerStandbyDrop() {
  const std::lock_guard<std::recursive_mutex> lock(m_FlappyMutex);
  if (!m_Connected || (m_Client == nullptr)) {
    return false;
  }
  // Re-arm the secure-failure budget so the reconnect churns, announce the
  // power state the way the GR IV does before its standby drop, then sever
  // the link.
  m_FlappyFailRemaining = m_FlappyFailAttempts;
  m_Config.camera_power = 0x00;
  emitNotification(CAMERA_SERVICE, POWER_CHARACTERISTIC, {0x00});
  m_Client->mockDropLink(0x08, /*fire_callback=*/true);
  return true;
}

void RicohVirtualCamera::requestFlappyCancel() {
  const std::lock_guard<std::recursive_mutex> lock(m_FlappyMutex);
  m_FlappyCancel = true;
  m_FlappyCv.notify_all();
}

void RicohVirtualCamera::cancelFlappyTimer() {
  requestFlappyCancel();
  if (m_FlappyThread.joinable() && (m_FlappyThread.get_id() != std::this_thread::get_id())) {
    m_FlappyThread.join();
  }
}

bool RicohVirtualCamera::acceptConnection(NimBLEClient &client, const NimBLEAddress &address) {
  if (m_Connected || address != m_Config.address) {
    return false;
  }
  m_Client = &client;
  m_Connected = true;
  const std::lock_guard<std::recursive_mutex> lock(m_FlappyMutex);
  m_Subscriptions.clear();
  return true;
}

void RicohVirtualCamera::disconnect(NimBLEClient &client, int reason) {
  (void)reason;
  if (m_Client == &client) {
    // Cancel the pending flappy drop for this session only: a stale or
    // foreign client's teardown must not disarm the current session's timer.
    // No join here: this may run on the drop timer's own thread
    // (mockDropLink -> peer disconnect) where a join would deadlock.
    requestFlappyCancel();
    m_Client = nullptr;
    m_Connected = false;
    const std::lock_guard<std::recursive_mutex> lock(m_FlappyMutex);
    m_Subscriptions.clear();
  }
}

bool RicohVirtualCamera::hasService(const NimBLEUUID &service) const {
  return matches(service, INFO_SERVICE) || matches(service, CAMERA_SERVICE)
         || matches(service, SHOOTING_SERVICE) || matches(service, BT_CONTROL_SERVICE)
         || matches(service, GPS_SERVICE);
}

bool RicohVirtualCamera::hasCharacteristic(const NimBLEUUID &service,
                                           const NimBLEUUID &characteristic) const {
  if (matches(service, INFO_SERVICE))
    return matches(characteristic, MODEL_CHARACTERISTIC);
  if (matches(service, CAMERA_SERVICE)) {
    return matches(characteristic, POWER_CHARACTERISTIC)
           || matches(characteristic, OPERATION_MODE_CHARACTERISTIC)
           || matches(characteristic, GEOTAG_ENABLE_CHARACTERISTIC);
  }
  if (matches(service, SHOOTING_SERVICE)) {
    return matches(characteristic, SHOOTING_FLAVOR_CHARACTERISTIC)
           || matches(characteristic, OPERATION_REQUEST_CHARACTERISTIC)
           || matches(characteristic, CAPTURE_STATUS_CHARACTERISTIC)
           || matches(characteristic, SELF_TIMER_CHARACTERISTIC);
  }
  if (matches(service, BT_CONTROL_SERVICE))
    return matches(characteristic, PAIRED_NAME_CHARACTERISTIC);
  return matches(service, GPS_SERVICE) && matches(characteristic, GPS_CHARACTERISTIC);
}

bool RicohVirtualCamera::discoverCharacteristic(NimBLEClient &client,
                                                const NimBLEUUID &service,
                                                const NimBLEUUID &characteristic) {
  (void)client;
  return hasCharacteristic(service, characteristic);
}

bool RicohVirtualCamera::canWrite(const NimBLEUUID &service,
                                  const NimBLEUUID &characteristic) const {
  return (matches(service, SHOOTING_SERVICE)
          && (matches(characteristic, SHOOTING_FLAVOR_CHARACTERISTIC)
              || matches(characteristic, OPERATION_REQUEST_CHARACTERISTIC)
              || matches(characteristic, SELF_TIMER_CHARACTERISTIC)))
         || (matches(service, CAMERA_SERVICE)
             && (matches(characteristic, GEOTAG_ENABLE_CHARACTERISTIC)
                 || matches(characteristic, POWER_CHARACTERISTIC)))
         || (matches(service, GPS_SERVICE) && matches(characteristic, GPS_CHARACTERISTIC));
}

bool RicohVirtualCamera::write(NimBLEClient &client,
                               const NimBLEUUID &service,
                               const NimBLEUUID &characteristic,
                               const std::vector<uint8_t> &value,
                               bool response) {
  (void)response;
  if (!m_Connected || m_Client != &client || !canWrite(service, characteristic))
    return false;
  // An application-level refusal is the only error signal `bled` produces: the
  // handler answers false and the stack returns application error 0x80.
  // Evidence: ricoh-handlers.md section 2.1.
  if (isWriteFailed(service, characteristic))
    return false;

  bool result = false;
  if (matches(service, CAMERA_SERVICE) && matches(characteristic, POWER_CHARACTERISTIC)) {
    // CameraPower::Write accepts exactly 0 (power off) and 1 (accepted, no
    // command). Every value >= 2, including SLEEP 2, is refused.
    // Evidence: ricoh-handlers.md section 5.1.
    if (value.size() == 1) {
      if (value[0] == 0x00) {
        m_Config.camera_power = 0x00;
        m_PowerWriteAccepted = true;
        // The camera announces the power transition and then drops the link.
        // Both are scheduled events in the certified model; triggerStandbyDrop
        // owns them so the write path never notifies synchronously.
        // Evidence: rea-camera-firmware/spec/ricoh.md F-11.
        result = true;
      } else if (value[0] == 0x01) {
        m_PowerWriteAccepted = true;
        result = true;
      }
    }
  } else if (matches(service, CAMERA_SERVICE)
             && matches(characteristic, GEOTAG_ENABLE_CHARACTERISTIC)) {
    // GEOTag::Write accepts 0 or 1 and refuses >= 2. It never touches WLAN.
    // Evidence: ricoh-handlers.md sections 5.5 and 7.
    if ((value.size() == 1) && (value[0] < 2)) {
      m_GeotagEnabled = (value[0] == 1);
      result = true;
    }
  } else if (matches(service, SHOOTING_SERVICE)
             && matches(characteristic, SHOOTING_FLAVOR_CHARACTERISTIC)) {
    // Drive Mode is a one-byte index into the 16-entry (drive, self-timer)
    // table. Values 16 and above are refused.
    // Evidence: ricoh-handlers.md section 5.9.1.
    if ((value.size() == 1) && (value[0] < driveModeTable().size())) {
      m_DriveModeIndex = value[0];
      result = true;
    }
  } else if (matches(service, SHOOTING_SERVICE)
             && matches(characteristic, OPERATION_REQUEST_CHARACTERISTIC)) {
    result = handleOperationRequest(value);
  } else if (matches(service, SHOOTING_SERVICE)
             && matches(characteristic, SELF_TIMER_CHARACTERISTIC)) {
    // Only the exact value 1 starts the capture self-timer. Every other
    // value is a successful no-op. Evidence: ricoh-handlers.md section 5.12.
    if (value.size() == 1) {
      if (value[0] == 0x01) {
        m_SelfTimerTriggered = true;
      }
      result = true;
    }
  } else if (matches(service, GPS_SERVICE) && matches(characteristic, GPS_CHARACTERISTIC)) {
    result = parseGpsFix(value);
  }

  if (result) {
    m_Writes.push_back({service.toString(), characteristic.toString(), value});
  }
  return result;
}

NimBLEAttValue RicohVirtualCamera::read(NimBLEClient &client,
                                        const NimBLEUUID &service,
                                        const NimBLEUUID &characteristic) {
  if (!m_Connected || m_Client != &client)
    return {};
  if (matches(service, INFO_SERVICE) && matches(characteristic, MODEL_CHARACTERISTIC)) {
    return NimBLEAttValue(m_Config.name);
  }
  if (matches(service, BT_CONTROL_SERVICE) && matches(characteristic, PAIRED_NAME_CHARACTERISTIC)) {
    return NimBLEAttValue("furble");
  }
  if (matches(service, CAMERA_SERVICE) && matches(characteristic, POWER_CHARACTERISTIC)) {
    return NimBLEAttValue({m_Config.camera_power});
  }
  if (matches(service, CAMERA_SERVICE) && matches(characteristic, OPERATION_MODE_CHARACTERISTIC)) {
    if (m_Config.operation_mode_read_fails)
      return {};
    return NimBLEAttValue({m_Config.operation_mode});
  }
  return NimBLEAttValue({0x00});
}

bool RicohVirtualCamera::subscribe(NimBLEClient &client,
                                   const NimBLEUUID &service,
                                   const NimBLEUUID &characteristic,
                                   bool notification,
                                   NimBLERemoteCharacteristic *remote,
                                   const NimBLENotifyCallback &callback,
                                   bool response) {
  (void)response;
  if (!m_Connected || m_Client != &client || !hasCharacteristic(service, characteristic)
      || (callback == nullptr)) {
    return false;
  }

  // Retain the subscription so the peer can emit notifications, mirroring
  // FujifilmVirtualCamera. Before this the callback was discarded and the
  // Ricoh peer could never notify (CameraPower, CaptureStatus).
  Subscription subscription;
  subscription.client = &client;
  subscription.remote = remote;
  subscription.callback = callback;
  subscription.notification = notification;
  const std::lock_guard<std::recursive_mutex> lock(m_FlappyMutex);
  m_Subscriptions[key(service, characteristic)] = subscription;
  return true;
}

bool RicohVirtualCamera::secureConnection(NimBLEClient &client) {
  if (!m_Connected || m_Client != &client)
    return false;
  if (m_FlappyEnabled) {
    const std::lock_guard<std::recursive_mutex> lock(m_FlappyMutex);
    if (m_FlappyFailRemaining > 0) {
      // Standby flap: the encryption handshake dies under a supervision
      // timeout. The failure wakes the connect task while the disconnect
      // event is still queued on the host task, so the client keeps
      // reporting connected: the rc=520 class.
      m_FlappyFailRemaining--;
      client.mockMarkLinkDeadEventPending(520);
      return false;
    }
  }

  // Camera-side pairing policy, in the order `BleServer::OnSecurityRequest`
  // applies it. Evidence: ricoh-handlers.md section 9.4.
  // 1. Pairing menu closed and no stored bond: deny and disconnect.
  if (!m_Config.in_pairing_mode && !m_Config.camera_bonded) {
    client.mockDropLink(0x08, true);
    return false;
  }
  // 2. A fresh pairing request from a device the camera still has bonded: the
  //    camera denies, deletes its own entry, and disconnects. The central keeps
  //    its key, which is the stale-bond signature.
  if (m_RepairRequest) {
    m_RepairRequest = false;
    m_Config.camera_bonded = false;
    client.mockDropLink(0x08, true);
    return false;
  }

  // 3. Association method. GR III and GR IIIx display a passkey that the
  //    central must enter; GR IV and HDF require numeric comparison with a
  //    camera-side confirmation. Evidence: ricoh-handlers.md section 9.1 and
  //    9.2, rea-camera-firmware/spec/ricoh.md section 7.1.
  if (m_Config.pairing == PairingMethod::kPasskeyDisplay) {
    if (!m_PasskeyAccepted)
      return false;
  } else if (!m_Config.accept_numeric_comparison) {
    return false;
  }

  const bool localBonded = NimBLEDevice::isBonded(m_Config.address);
  if (localBonded != m_Config.camera_bonded)
    return false;
  if (!localBonded) {
    NimBLEDevice::setBonded(true);
    m_Config.camera_bonded = true;
  }
  if (m_FlappyEnabled) {
    armFlappyDrop(client);
  }
  return true;
}

bool RicohVirtualCamera::updateConnectionParams(NimBLEClient &client,
                                                uint16_t min_interval,
                                                uint16_t max_interval,
                                                uint16_t latency,
                                                uint16_t timeout) {
  (void)min_interval;
  (void)max_interval;
  (void)latency;
  (void)timeout;
  return m_Connected && m_Client == &client;
}

int RicohVirtualCamera::getRssi() const {
  return -55;
}

const NimBLEUUID &RicohVirtualCamera::shootingFlavorCharacteristicUUID() {
  return SHOOTING_FLAVOR_CHARACTERISTIC;
}

const NimBLEUUID &RicohVirtualCamera::operationRequestCharacteristicUUID() {
  return OPERATION_REQUEST_CHARACTERISTIC;
}

const NimBLEUUID &RicohVirtualCamera::cameraServiceUUID() {
  return CAMERA_SERVICE;
}

const NimBLEUUID &RicohVirtualCamera::powerCharacteristicUUID() {
  return POWER_CHARACTERISTIC;
}

const NimBLEUUID &RicohVirtualCamera::geotagEnableCharacteristicUUID() {
  return GEOTAG_ENABLE_CHARACTERISTIC;
}

const NimBLEUUID &RicohVirtualCamera::gpsInformationCharacteristicUUID() {
  return GPS_CHARACTERISTIC;
}

const NimBLEUUID &RicohVirtualCamera::selfTimerCharacteristicUUID() {
  return SELF_TIMER_CHARACTERISTIC;
}

const NimBLEUUID &RicohVirtualCamera::captureStatusCharacteristicUUID() {
  return CAPTURE_STATUS_CHARACTERISTIC;
}

const NimBLEUUID &RicohVirtualCamera::pairedDeviceNameCharacteristicUUID() {
  return PAIRED_NAME_CHARACTERISTIC;
}

}  // namespace Host
}  // namespace Furble
