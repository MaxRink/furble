#include "CanonRemoteVirtualCamera.h"
#include "WriteJournal.h"

namespace Furble {
namespace Host {
namespace {

// Canon Remote role, spec/canon.md section 3.2. Only 00050002 and 00050003 are
// documented; no artifact shows 00050001 or any characteristic above 00050003.
const NimBLEUUID REMOTE_SERVICE {0x00050000, 0x0000, 0x1000, 0x0000d8492fffa821};
const NimBLEUUID NAME_CHARACTERISTIC {0x00050002, 0x0000, 0x1000, 0x0000d8492fffa821};
const NimBLEUUID CONTROL_CHARACTERISTIC {0x00050003, 0x0000, 0x1000, 0x0000d8492fffa821};

constexpr uint8_t SHUTTER = 0x80;
constexpr uint8_t FOCUS = 0x40;
constexpr uint8_t TELE = 0x20;
constexpr uint8_t WIDE = 0x10;
constexpr uint8_t MODE_MASK = 0x0c;
constexpr uint8_t MODE_IMMEDIATE = 0x0c;
constexpr uint8_t MODE_DELAY = 0x04;
constexpr uint8_t MODE_MOVIE = 0x08;
constexpr uint8_t RESERVED_MASK = 0x03;

constexpr size_t REMOTE_NAME_MAX = 31;

bool isPrintableAscii(const std::vector<uint8_t> &value, size_t offset) {
  if (value.size() <= offset) {
    return false;
  }
  for (size_t i = offset; i < value.size(); i++) {
    if ((value[i] < 0x20) || (value[i] > 0x7e)) {
      return false;
    }
  }
  return true;
}

}  // namespace

CanonRemoteVirtualCamera::CanonRemoteVirtualCamera() : CanonRemoteVirtualCamera(Config {}) {}

CanonRemoteVirtualCamera::CanonRemoteVirtualCamera(const Config &config) : m_Config(config) {}

CanonRemoteVirtualCamera::~CanonRemoteVirtualCamera() = default;

bool CanonRemoteVirtualCamera::matches(const NimBLEUUID &left, const NimBLEUUID &right) {
  return left == right;
}

NimBLEAdvertisedDevice CanonRemoteVirtualCamera::advertisement() const {
  NimBLEAdvertisedDevice device;
  device.setAddress(m_Config.address);
  device.setName(m_Config.name);
  device.setRSSI(-48);
  if (m_Config.remote_supported) {
    device.addServiceUUID(REMOTE_SERVICE);
  }
  if (m_Config.include_manufacturer_data) {
    // U-40: the Remote advertisement form is not documented. When a test asks
    // for it, reuse the Canon company ID and type byte with the product ID.
    const std::vector<uint8_t> manufacturer = {
        0xa9,
        0x01,
        0x01,
        static_cast<uint8_t>(m_Config.product_id & 0xff),
        static_cast<uint8_t>((m_Config.product_id >> 8) & 0xff),
    };
    device.setManufacturerData(manufacturer.data(), manufacturer.size());
  }
  return device;
}

const CanonRemoteVirtualCamera::Config &CanonRemoteVirtualCamera::config() const {
  return m_Config;
}

const std::vector<CanonRemoteVirtualCamera::Write> &CanonRemoteVirtualCamera::writes() const {
  return m_Writes;
}

const std::vector<CanonRemoteVirtualCamera::Notification> &CanonRemoteVirtualCamera::notifications()
    const {
  return m_Notifications;
}

void CanonRemoteVirtualCamera::clearEvents() {
  m_Writes.clear();
  m_Notifications.clear();
}

bool CanonRemoteVirtualCamera::connected() const {
  return m_Connected;
}

bool CanonRemoteVirtualCamera::secured() const {
  return m_Secured;
}

bool CanonRemoteVirtualCamera::cameraBonded() const {
  return m_Config.camera_bonded;
}

void CanonRemoteVirtualCamera::setCameraBonded(bool bonded) {
  m_Config.camera_bonded = bonded;
}

void CanonRemoteVirtualCamera::removeCameraBond() {
  m_Config.camera_bonded = false;
  m_RegisteredName.clear();
}

const std::string &CanonRemoteVirtualCamera::registeredName() const {
  return m_RegisteredName;
}

uint8_t CanonRemoteVirtualCamera::lastControl() const {
  return m_LastControl;
}

bool CanonRemoteVirtualCamera::shutterHeld() const {
  return m_ShutterHeld;
}

bool CanonRemoteVirtualCamera::focusHeld() const {
  return m_FocusHeld;
}

int CanonRemoteVirtualCamera::zoomDirection() const {
  return m_ZoomDirection;
}

uint8_t CanonRemoteVirtualCamera::controlMode() const {
  return m_ControlMode;
}

size_t CanonRemoteVirtualCamera::shutterPresses() const {
  return m_ShutterPresses;
}

size_t CanonRemoteVirtualCamera::focusPresses() const {
  return m_FocusPresses;
}

size_t CanonRemoteVirtualCamera::releases() const {
  return m_Releases;
}

size_t CanonRemoteVirtualCamera::rejectedWrites() const {
  return m_RejectedWrites;
}

void CanonRemoteVirtualCamera::setFault(Fault fault) {
  m_Fault = fault;
}

CanonRemoteVirtualCamera::Fault CanonRemoteVirtualCamera::fault() const {
  return m_Fault;
}

void CanonRemoteVirtualCamera::clearFault() {
  m_Fault = Fault::None;
}

void CanonRemoteVirtualCamera::setScripted(bool scripted) {
  m_Scripted = scripted;
}

bool CanonRemoteVirtualCamera::scripted() const {
  return m_Scripted;
}

void CanonRemoteVirtualCamera::scriptNextCommand(Fault fault) {
  m_Script.push_back(fault);
}

size_t CanonRemoteVirtualCamera::scriptedRemaining() const {
  return m_Script.size();
}

void CanonRemoteVirtualCamera::clearScript() {
  m_Script.clear();
}

CanonRemoteVirtualCamera::Fault CanonRemoteVirtualCamera::consumeFault() {
  if (m_Scripted) {
    if (m_Script.empty()) {
      return Fault::None;
    }
    const Fault fault = m_Script.front();
    m_Script.pop_front();
    return fault;
  }
  return m_Fault;
}

bool CanonRemoteVirtualCamera::acceptConnection(NimBLEClient &client,
                                                const NimBLEAddress &address) {
  if (m_Connected || (address != m_Config.address)) {
    return false;
  }
  m_Client = &client;
  m_Connected = true;
  m_Secured = false;
  return true;
}

void CanonRemoteVirtualCamera::disconnect(NimBLEClient &client, int reason) {
  (void)reason;
  if (m_Client == &client) {
    m_Client = nullptr;
    m_Connected = false;
    m_Secured = false;
  }
}

bool CanonRemoteVirtualCamera::hasService(const NimBLEUUID &service) const {
  return m_Config.remote_supported && matches(service, REMOTE_SERVICE);
}

bool CanonRemoteVirtualCamera::hasCharacteristic(const NimBLEUUID &service,
                                                 const NimBLEUUID &characteristic) const {
  if (!matches(service, REMOTE_SERVICE)) {
    return false;
  }
  return matches(characteristic, NAME_CHARACTERISTIC)
         || matches(characteristic, CONTROL_CHARACTERISTIC);
}

bool CanonRemoteVirtualCamera::discoverCharacteristic(NimBLEClient &client,
                                                      const NimBLEUUID &service,
                                                      const NimBLEUUID &characteristic) {
  (void)client;
  return hasCharacteristic(service, characteristic);
}

bool CanonRemoteVirtualCamera::canWrite(const NimBLEUUID &service,
                                        const NimBLEUUID &characteristic) const {
  return matches(service, REMOTE_SERVICE)
         && (matches(characteristic, NAME_CHARACTERISTIC)
             || matches(characteristic, CONTROL_CHARACTERISTIC));
}

bool CanonRemoteVirtualCamera::write(NimBLEClient &client,
                                     const NimBLEUUID &service,
                                     const NimBLEUUID &characteristic,
                                     const std::vector<uint8_t> &value,
                                     bool response) {
  const std::lock_guard<std::mutex> journal(Host::peerWriteJournalMutex());
  if (!m_Connected || (m_Client != &client) || !canWrite(service, characteristic)) {
    return false;
  }

  const Fault fault = consumeFault();
  switch (fault) {
    case Fault::None:
    case Fault::Timeout:
    case Fault::StaleBond:
      break;
    case Fault::Reject:
    case Fault::Busy:
    case Fault::WrongMode:
    case Fault::Malformed:
      m_RejectedWrites++;
      return false;
    case Fault::Disconnect:
      m_RejectedWrites++;
      client.mockDropLink(0x08, /*fire_callback=*/true);
      return false;
  }

  // CR-01, the pairing name. 03 plus a US-ASCII name, no NUL. Furble and three
  // public peers write that form; BR-M5 writes the bare name.
  if (matches(service, REMOTE_SERVICE) && matches(characteristic, NAME_CHARACTERISTIC)) {
    if (m_Config.require_secure && !m_Secured) {
      return false;
    }
    std::string parsed;
    if ((value.size() >= 2) && (value[0] == 0x03) && isPrintableAscii(value, 1)
        && (value.size() - 1 <= REMOTE_NAME_MAX)) {
      parsed.assign(value.begin() + 1, value.end());
    } else if (m_Config.accept_unprefixed_name && isPrintableAscii(value, 0)
               && (value.size() <= REMOTE_NAME_MAX)) {
      parsed.assign(value.begin(), value.end());
    } else {
      return false;
    }
    if (fault == Fault::Timeout) {
      // A timed-out write is ignored by the camera: the previously registered
      // name must survive rather than being replaced by an empty string.
    } else {
      m_RegisteredName = std::move(parsed);
    }
    Write event;
    event.service = service.toString();
    event.characteristic = characteristic.toString();
    event.payload = value;
    event.response = response;
    m_Writes.push_back(event);
    return true;
  }

  // CR-03 to CR-10, the one-byte control bitfield.
  if (matches(service, REMOTE_SERVICE) && matches(characteristic, CONTROL_CHARACTERISTIC)) {
    if (value.size() != 1) {
      return false;
    }
    if (fault == Fault::Timeout) {
      // The camera acknowledges the write and drops the action. Nothing is
      // recorded in the control state, so the next press has no prior state.
      Write event;
      event.service = service.toString();
      event.characteristic = characteristic.toString();
      event.payload = value;
      event.response = response;
      m_Writes.push_back(event);
      return true;
    }
    if (!handleControl(value[0])) {
      return false;
    }
    Write event;
    event.service = service.toString();
    event.characteristic = characteristic.toString();
    event.payload = value;
    event.response = response;
    m_Writes.push_back(event);
    return true;
  }

  return false;
}

bool CanonRemoteVirtualCamera::handleControl(uint8_t value) {
  // The control write is exactly one byte (section 4.2).
  if (m_Config.require_secure && !m_Secured) {
    return false;
  }
  if ((value & RESERVED_MASK) != 0) {
    return m_Config.allow_unproven_control_bits;
  }
  const uint8_t mode = value & MODE_MASK;
  uint8_t control_mode = 0;
  if (mode == MODE_IMMEDIATE) {
    control_mode = 1;
  } else if (mode == MODE_DELAY) {
    control_mode = 2;
  } else if (mode == MODE_MOVIE) {
    control_mode = 3;
  } else {
    // Mode value 0 has no documented meaning, so it is refused.
    return m_Config.allow_unproven_control_bits;
  }
  if (((value & TELE) != 0) && ((value & WIDE) != 0)) {
    // The two zoom bits are never documented together.
    return false;
  }

  m_ControlMode = control_mode;
  m_LastControl = value;
  const bool shutter = (value & SHUTTER) != 0;
  const bool focus = (value & FOCUS) != 0;
  const bool tele = (value & TELE) != 0;
  const bool wide = (value & WIDE) != 0;

  if (shutter && !m_ShutterHeld) {
    m_ShutterPresses++;
  }
  if (focus && !m_FocusHeld) {
    m_FocusPresses++;
  }
  if (!shutter && !focus && !tele && !wide) {
    m_Releases++;
  }

  m_ShutterHeld = shutter;
  m_FocusHeld = focus;
  m_ZoomDirection = tele ? 1 : (wide ? -1 : 0);
  return true;
}

NimBLEAttValue CanonRemoteVirtualCamera::read(NimBLEClient &client,
                                              const NimBLEUUID &service,
                                              const NimBLEUUID &characteristic) {
  (void)client;
  (void)service;
  (void)characteristic;
  // The Remote role documents no readable characteristic. 00050002 and 00050003
  // are write-only, so a read returns no value.
  return {};
}

bool CanonRemoteVirtualCamera::subscribe(NimBLEClient &client,
                                         const NimBLEUUID &service,
                                         const NimBLEUUID &characteristic,
                                         bool notification,
                                         NimBLERemoteCharacteristic *remote,
                                         const NimBLENotifyCallback &callback,
                                         bool response) {
  (void)client;
  (void)service;
  (void)characteristic;
  (void)notification;
  (void)remote;
  (void)callback;
  (void)response;
  // No CCCD is documented on either Remote characteristic, so there is nothing
  // to subscribe to.
  return false;
}

bool CanonRemoteVirtualCamera::secureConnection(NimBLEClient &client) {
  if (!m_Connected || (m_Client != &client)) {
    return false;
  }
  if (m_Fault == Fault::StaleBond) {
    client.mockMarkLinkDeadEventPending(520);
    return false;
  }
  if (!m_Config.accept_numeric_comparison) {
    return false;
  }
  const bool local_bonded = NimBLEDevice::isBonded(m_Config.address);
  if (local_bonded != m_Config.camera_bonded) {
    return false;
  }
  if (!local_bonded) {
    NimBLEDevice::setBondedAddress(m_Config.address);
    m_Config.camera_bonded = true;
  }
  m_Secured = true;
  return true;
}

bool CanonRemoteVirtualCamera::updateConnectionParams(NimBLEClient &client,
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

int CanonRemoteVirtualCamera::getRssi() const {
  return -48;
}

const NimBLEUUID &CanonRemoteVirtualCamera::remoteServiceUUID() {
  return REMOTE_SERVICE;
}

const NimBLEUUID &CanonRemoteVirtualCamera::nameCharacteristicUUID() {
  return NAME_CHARACTERISTIC;
}

const NimBLEUUID &CanonRemoteVirtualCamera::controlCharacteristicUUID() {
  return CONTROL_CHARACTERISTIC;
}

}  // namespace Host
}  // namespace Furble
