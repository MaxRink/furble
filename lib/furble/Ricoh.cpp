#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>

#include <NimBLEAddress.h>
#include <NimBLEAdvertisedDevice.h>
#include <NimBLEConnInfo.h>
#include <NimBLEDevice.h>
#include <NimBLERemoteCharacteristic.h>
#include <NimBLERemoteService.h>
#include <NimBLEUtils.h>
#include <esp_timer.h>

#include "Device.h"
#include "Ricoh.h"
#include "protocol/AdvertisementProtocol.h"
#include "protocol/RicohTime.h"

namespace Furble {

const NimBLEUUID Ricoh::INFO_SVC_UUID {0x9A5ED1C5, 0x74CC, 0x4C50, 0xB5B666A48E7CCFF1};
const NimBLEUUID Ricoh::MODEL_CHR_UUID {0x35FE6272, 0x6AA5, 0x44D9, 0x88E1F09427F51A71};

const NimBLEUUID Ricoh::CAMERA_SVC_UUID {0x4B445988, 0xCAA0, 0x4DD3, 0x941D37B4F52ACA86};
const NimBLEUUID Ricoh::POWER_CHR_UUID {0xB58CE84C, 0x0666, 0x4DE9, 0xBEC82D27B27B3211};
const NimBLEUUID Ricoh::OPERATION_MODE_CHR_UUID {0x1452335A, 0xEC7F, 0x4877, 0xB8AB0F72E18BB295};
const NimBLEUUID Ricoh::DATE_TIME_CHR_UUID {0xFA46BBDD, 0x8A8F, 0x4796, 0x8CF3AA58949B130A};
const NimBLEUUID Ricoh::GEOTAG_ENABLE_CHR_UUID {0xA36AFDCF, 0x6B67, 0x4046, 0x9BE728FB67DBC071};

const NimBLEUUID Ricoh::SHOOTING_SVC_UUID {0x9F00F387, 0x8345, 0x4BBC, 0x8B92B87B52E3091A};
const NimBLEUUID Ricoh::SHOOTING_FLAVOR_CHR_UUID {0xB29E6DE3, 0x1AEC, 0x48C1, 0x9D0502CEA57CE664};
const NimBLEUUID Ricoh::OPERATION_REQUEST_CHR_UUID {0x559644B8, 0xE0BC, 0x4011, 0x929B5CF9199851E7};
const NimBLEUUID Ricoh::CAPTURE_STATUS_CHR_UUID {0xB5589C08, 0xB5FD, 0x46F5, 0xBE7DAB1B8C074CAA};
const NimBLEUUID Ricoh::SELF_TIMER_CHR_UUID {0x009A8E70, 0xB306, 0x4451, 0xB9437F54392EB971};

const NimBLEUUID Ricoh::BT_CONTROL_SVC_UUID {0x0F291746, 0x0C80, 0x4726, 0x87A73C501FD3B4B6};
const NimBLEUUID Ricoh::PAIRED_DEVICE_NAME_CHR_UUID {0xFE3A32F8, 0xA189, 0x42DE,
                                                     0xA391BC81AE4DAA76};

const NimBLEUUID Ricoh::GPS_SVC_UUID {0x84A0DD62, 0xE8AA, 0x4D0F, 0x91DB819B6724C69E};
const NimBLEUUID Ricoh::GPS_INFO_CHR_UUID {0x28F59D60, 0x8B8E, 0x4FCD, 0xA81F61BDB46595A9};

namespace {

constexpr uint32_t GPS_MIN_INTERVAL_MS = 10 * 1000;
// Spacing between the wake state machine's fresh OperationMode reads. Nothing
// documents the cadence a GR IV wants, so this only needs to be short against
// FURBLE_RICOH_WAKE_PROBE_TIMEOUT_MS and long enough not to flood the link.
constexpr uint32_t WAKE_POLL_MS = 50;
// Slice the wake pause so a teardown that lands during it is honoured inside
// the wait itself (plan 170), without a second read per slice.
constexpr uint32_t WAKE_PAUSE_SLICE_MS = 10;
constexpr double GPS_MIN_DELTA_DEG = 0.00001;
constexpr double GPS_MIN_DELTA_ALT_M = 1.0;

double bswapd64(double x) {
  uint64_t t;
  std::memcpy(&t, &x, sizeof(x));
  t = __builtin_bswap64(t);

  double val;
  std::memcpy(&val, &t, sizeof(t));
  return val;
}

bool validTimesync(const Camera::timesync_t &timesync) {
  return timesync.year >= 2000 && timesync.year <= 2099 && timesync.month >= 1
         && timesync.month <= 12 && timesync.day >= 1 && timesync.day <= 31 && timesync.hour <= 23
         && timesync.minute <= 59 && timesync.second <= 60 && timesync.centisecond <= 99;
}

const char *powerName(uint8_t value) {
  switch (value) {
    case 0:
      return "OFF";
    case 1:
      return "ON";
    case 2:
      return "SLEEP";
    default:
      return "?";
  }
}

const char *opModeName(uint8_t value) {
  switch (value) {
    case 0:
      return "CAPTURE";
    case 1:
      return "PLAYBACK";
    case 2:
      return "BLE_STARTUP";
    case 3:
      return "OTHER";
    case 4:
      return "POWER_OFF_TRANSFER";
    default:
      return "?";
  }
}

}  // namespace

void Ricoh::logChr(NimBLERemoteCharacteristic *pChr,
                   const char *label,
                   const char *(*decode)(uint8_t),
                   std::atomic<uint8_t> *lastByte) {
  if (pChr == nullptr) {
    ESP_LOGI(LOG_TAG, "Ricoh %s: missing", label);
    return;
  }
  ESP_LOGI(LOG_TAG, "Ricoh %s uuid=%s read=%d write=%d writeNoRsp=%d notify=%d indicate=%d", label,
           pChr->getUUID().toString().c_str(), pChr->canRead(), pChr->canWrite(),
           pChr->canWriteNoResponse(), pChr->canNotify(), pChr->canIndicate());
  if (!pChr->canRead())
    return;
  NimBLEAttValue value;
  if (!gattRead(pChr, value)) {
    ESP_LOGI(LOG_TAG, "Ricoh %s read failed", label);
    return;
  }
  if (value.length() == 0) {
    ESP_LOGI(LOG_TAG, "Ricoh %s value=<empty>", label);
    return;
  }
  if (lastByte != nullptr)
    *lastByte = value.data()[0];
  if (decode != nullptr && value.length() == 1)
    ESP_LOGI(LOG_TAG, "Ricoh %s value=0x%02X decoded=%s", label, value.data()[0],
             decode(value.data()[0]));
  else
    ESP_LOGI(
        LOG_TAG, "Ricoh %s value=%s", label,
        NimBLEUtils::dataToHexString(value.data(), static_cast<uint8_t>(value.length())).c_str());
}

Ricoh::Ricoh(const void *data, size_t len) : Camera(Type::RICOH, PairType::SAVED) {
  if (len != sizeof(ricoh_t))
    abort();

  const ricoh_t *ricoh = static_cast<const ricoh_t *>(data);
  m_Name = std::string(ricoh->name);
  m_Address = NimBLEAddress(ricoh->address, ricoh->type);
}

Ricoh::Ricoh(const NimBLEAdvertisedDevice *pDevice) : Camera(Type::RICOH, PairType::NEW) {
  m_Name = pDevice->getName();
  if (m_Name.empty())
    m_Name = "RICOH";
  m_Address = pDevice->getAddress();
  ESP_LOGI(LOG_TAG, "Ricoh Name = %s", m_Name.c_str());
  ESP_LOGI(LOG_TAG, "Ricoh Address = %s", m_Address.toString().c_str());
}

bool Ricoh::nameMatches(const std::string &name) {
  return AdvertisementProtocol::matchesRicohName(name);
}

bool Ricoh::matches(const NimBLEAdvertisedDevice *pDevice) {
  if (pDevice == nullptr) {
    return false;
  }
  return pDevice->isAdvertisingService(INFO_SVC_UUID)
         || pDevice->isAdvertisingService(CAMERA_SVC_UUID)
         || pDevice->isAdvertisingService(SHOOTING_SVC_UUID)
         || pDevice->isAdvertisingService(BT_CONTROL_SVC_UUID) || nameMatches(pDevice->getName());
}

Ricoh::SecurityMode Ricoh::securityMode() const {
  // GR IV firmware exposes numeric-comparison confirmation. The shared Camera
  // pairing callbacks publish the camera's six-digit code and inject the
  // operator's answer. They keep the NimBLEDevice::getSecurityPasskey()
  // fallback, whose default is 123456, for a body that asks for passkey entry
  // or display instead; numeric comparison is the interactive path, not a
  // removed fixed passkey.
  return SecurityMode::SECURE_KEYBOARD_DISPLAY;
}

bool Ricoh::_connect(void) {
  // Drop characteristic pointers from any previous session. A failed connect
  // can reclaim the old client without running _disconnect() on this class, so
  // stale pointers would dangle into the freed client.
  clearRemoteState();
  m_Progress = 0;

  bool bondedBefore = NimBLEDevice::isBonded(m_Address);
  ESP_LOGI(LOG_TAG, "Ricoh bonded(before)=%s pairType=%s", bondedBefore ? "yes" : "no",
           getPairType() == PairType::NEW ? "new" : "saved");

  ESP_LOGI(LOG_TAG, "Ricoh connecting");
  if (!m_Client->connect(m_Address)) {
    ESP_LOGI(LOG_TAG, "Ricoh connection failed");
    return false;
  }
  m_Progress = 15;

  ESP_LOGI(LOG_TAG, "Ricoh securing");
  if (!m_Client->secureConnection()) {
    ESP_LOGW(LOG_TAG, "Ricoh secure connection failed (bonded before=%s)",
             bondedBefore ? "yes" : "no");
    // A live-scan pairing is an explicit request for a new pairing. If only
    // the camera-side bond was removed, discard the stale local key so the
    // next attempt can perform numeric comparison. Saved reconnects retain
    // their bond across transient security failures.
    if (bondedBefore && (getPairType() == PairType::NEW) && !pairingCancelled()) {
      if (NimBLEDevice::deleteBond(m_Address)) {
        ESP_LOGI(LOG_TAG, "Ricoh stale local bond cleared for fresh pairing retry");
      }
    }
    return false;
  }
  {
    NimBLEConnInfo connInfo = m_Client->getConnInfo();
    ESP_LOGI(LOG_TAG, "Ricoh secure: encrypted=%d authenticated=%d bonded=%d keySize=%u",
             connInfo.isEncrypted(), connInfo.isAuthenticated(), connInfo.isBonded(),
             connInfo.getSecKeySize());
    if (!connInfo.isEncrypted() || !connInfo.isAuthenticated() || !connInfo.isBonded()) {
      ESP_LOGW(LOG_TAG, "Ricoh secure link incomplete");
      return false;
    }
  }
  m_Progress = 30;

  NimBLERemoteService *pSvc = m_Client->getService(INFO_SVC_UUID);
  if (pSvc != nullptr) {
    NimBLERemoteCharacteristic *pModel = pSvc->getCharacteristic(MODEL_CHR_UUID);
    if (pModel != nullptr && pModel->canRead()) {
      NimBLEAttValue modelValue;
      if (gattRead(pModel, modelValue)) {
        std::string model = static_cast<std::string>(modelValue);
        if (!model.empty()) {
          ESP_LOGI(LOG_TAG, "Ricoh Model = %s", model.c_str());
          m_Name = model;
        }
      }
    }
  }
  m_Progress = 45;

  pSvc = m_Client->getService(CAMERA_SVC_UUID);
  if (pSvc != nullptr) {
    m_Power = pSvc->getCharacteristic(POWER_CHR_UUID);
    m_OperationMode = pSvc->getCharacteristic(OPERATION_MODE_CHR_UUID);
    m_DateTime = pSvc->getCharacteristic(DATE_TIME_CHR_UUID);
    m_GeotagEnable = pSvc->getCharacteristic(GEOTAG_ENABLE_CHR_UUID);
  } else {
    ESP_LOGW(LOG_TAG, "Ricoh Camera service unavailable");
  }
  m_Progress = 60;

  pSvc = m_Client->getService(SHOOTING_SVC_UUID);
  if (pSvc == nullptr) {
    ESP_LOGW(LOG_TAG, "Ricoh Shooting service unavailable");
    return false;
  }

  m_OperationRequest = pSvc->getCharacteristic(OPERATION_REQUEST_CHR_UUID);
  m_ShootingFlavor = pSvc->getCharacteristic(SHOOTING_FLAVOR_CHR_UUID);
  m_CaptureStatus = pSvc->getCharacteristic(CAPTURE_STATUS_CHR_UUID);
  m_SelfTimer = pSvc->getCharacteristic(SELF_TIMER_CHR_UUID);
  if (m_OperationRequest == nullptr || !m_OperationRequest->canWrite()) {
    ESP_LOGW(LOG_TAG, "Ricoh OperationRequest unavailable");
    return false;
  }
  if (m_ShootingFlavor == nullptr || !m_ShootingFlavor->canWrite()) {
    ESP_LOGW(LOG_TAG, "Ricoh ShootingFlavor unavailable");
    return false;
  }
  m_Progress = 75;

  pSvc = m_Client->getService(GPS_SVC_UUID);
  if (pSvc != nullptr) {
    m_GpsInfo = pSvc->getCharacteristic(GPS_INFO_CHR_UUID);
  } else {
    ESP_LOGW(LOG_TAG, "Ricoh GPS service unavailable");
  }
  m_Progress = 82;

  pSvc = m_Client->getService(BT_CONTROL_SVC_UUID);
  if (pSvc != nullptr) {
    m_PairedDeviceName = pSvc->getCharacteristic(PAIRED_DEVICE_NAME_CHR_UUID);
    if (m_PairedDeviceName != nullptr && m_PairedDeviceName->canWrite()) {
      const std::string name = Device::getStringID();
      if (!gattWrite(m_PairedDeviceName, reinterpret_cast<const uint8_t *>(name.data()),
                     name.size(), true)) {
        ESP_LOGW(LOG_TAG, "Ricoh PairedDeviceName write failed");
      }
    }
  } else {
    ESP_LOGW(LOG_TAG, "Ricoh Bluetooth Control service unavailable");
  }
  m_Progress = 88;

  ESP_LOGI(LOG_TAG, "Ricoh state probe begin");
  logChr(m_Power, "CameraPower", powerName, &m_LastPower);
  logChr(m_OperationMode, "OperationMode", opModeName, &m_LastOperationMode);
  logChr(m_ShootingFlavor, "ShootingFlavor");
  logChr(m_OperationRequest, "OperationRequest");
  logChr(m_CaptureStatus, "CaptureStatus");
  logChr(m_SelfTimer, "SelfTimer");
  logChr(m_GpsInfo, "GpsInfo");
  logChr(m_GeotagEnable, "GeotagEnable");
  logChr(m_PairedDeviceName, "PairedDeviceName");
  ESP_LOGI(LOG_TAG, "Ricoh state probe end");

  subscribeCharacteristic(m_CaptureStatus, "CaptureStatus");
  subscribeCharacteristic(m_SelfTimer, "SelfTimer");
  subscribeCharacteristic(m_Power, "CameraPower");
  subscribeCharacteristic(m_OperationMode, "OperationMode");

  m_Progress = 100;
  ESP_LOGI(LOG_TAG, "Ricoh connected");
  return true;
}

void Ricoh::_disconnect(void) {
  clearRemoteState();
  if (m_Client != nullptr && m_Client->isConnected())
    m_Client->disconnect();
}

void Ricoh::clearRemoteState(void) {
  m_Power = nullptr;
  m_OperationMode = nullptr;
  m_DateTime = nullptr;
  m_ShootingFlavor = nullptr;
  m_OperationRequest = nullptr;
  m_CaptureStatus = nullptr;
  m_SelfTimer = nullptr;
  m_PairedDeviceName = nullptr;
  m_GpsInfo = nullptr;
  m_GeotagEnable = nullptr;
  m_LastPower = STATE_UNKNOWN;
  m_LastOperationMode = STATE_UNKNOWN;
  m_CaptureStatusValue = STATE_UNKNOWN;
  m_CapturesCompleted = 0;
  m_LastGpsWriteMs = 0;
  m_HasGpsWrite = false;
  m_HasClockWrite = false;
}

bool Ricoh::writeByte(NimBLERemoteCharacteristic *pChr, uint8_t value, const char *label) {
  if (!isConnected()) {
    ESP_LOGW(LOG_TAG, "Ricoh %s skipped: not connected", label);
    return false;
  }
  if (pChr == nullptr || !pChr->canWrite()) {
    ESP_LOGW(LOG_TAG, "Ricoh %s unavailable", label);
    return false;
  }
  bool rc = gattWrite(pChr, &value, sizeof(value), true);
  ESP_LOGD(LOG_TAG, "Ricoh %s = %s", label, rc ? "ok" : "failed");
  return rc;
}

bool Ricoh::writeOperation(OperationCode code, OperationParameter parameter) {
  if (!isConnected()) {
    ESP_LOGW(LOG_TAG, "Ricoh OperationRequest skipped: not connected");
    return false;
  }
  if (m_OperationRequest == nullptr) {
    ESP_LOGW(LOG_TAG, "Ricoh OperationRequest unavailable");
    return false;
  }
  const std::array<uint8_t, 2> cmd = {static_cast<uint8_t>(code), static_cast<uint8_t>(parameter)};
  bool rc = gattWrite(m_OperationRequest, cmd.data(), cmd.size(), true);
  ESP_LOGI(LOG_TAG, "Ricoh OperationRequest code=%u param=%u => %s", static_cast<unsigned>(code),
           static_cast<unsigned>(parameter), rc ? "ok" : "failed");
  return rc;
}

bool Ricoh::subscribeCharacteristic(NimBLERemoteCharacteristic *pChr, const char *label) {
  if (pChr == nullptr || !pChr->canNotify()) {
    ESP_LOGD(LOG_TAG, "Ricoh %s subscribe skipped", label);
    return false;
  }
  bool rc = gattSubscribe(
      pChr,
      [this](NimBLERemoteCharacteristic *chr, uint8_t *data, size_t len, bool isNotify) {
        ESP_LOGI(LOG_TAG, "Ricoh notify %s (%s): %s", chr->getUUID().toString().c_str(),
                 isNotify ? "notify" : "indicate",
                 NimBLEUtils::dataToHexString(data, static_cast<uint8_t>(len)).c_str());
        if (len >= 1) {
          if (chr->getUUID() == POWER_CHR_UUID) {
            m_LastPower = data[0];
          } else if (chr->getUUID() == OPERATION_MODE_CHR_UUID) {
            m_LastOperationMode = data[0];
          } else if (chr->getUUID() == CAPTURE_STATUS_CHR_UUID) {
            // Byte 0 is the capture-in-progress property. GR IIIx 1.60 shows
            // 01 00 00 00 while capturing and 00 00 00 00 afterwards. A
            // property read failure substitutes 0xFF, which is neither.
            const uint8_t previous = m_CaptureStatusValue.exchange(data[0]);
            if (previous == 0x01 && data[0] == 0x00) {
              m_CapturesCompleted++;
              ESP_LOGI(LOG_TAG, "Ricoh capture complete (%u observed)",
                       static_cast<unsigned>(m_CapturesCompleted.load()));
            } else if (previous != data[0] && data[0] == 0x01) {
              ESP_LOGI(LOG_TAG, "Ricoh capture started");
            }
          }
        }
      },
      false);
  ESP_LOGI(LOG_TAG, "Ricoh subscribe %s => %s", label, rc ? "ok" : "failed");
  return rc;
}

bool Ricoh::setShootingFlavor(ShootingFlavor flavor) {
  return writeByte(m_ShootingFlavor, static_cast<uint8_t>(flavor), "ShootingFlavor");
}

bool Ricoh::setGeotagEnable(bool enabled) {
  return writeByte(m_GeotagEnable, enabled ? 0x01 : 0x00, "GeotagEnable");
}

Ricoh::ModeRead Ricoh::readOperationMode(uint8_t &mode) {
  mode = STATE_UNKNOWN;
  if (!isConnected()) {
    return ModeRead::FAILED;
  }
  if (m_OperationMode == nullptr || !m_OperationMode->canRead()) {
    // The Ricoh matcher also accepts bodies that expose the shooting service
    // without the GR camera service. Those cannot report a power state and shot
    // fine before this gate, so the gate only applies when the characteristic
    // exists. The GR IV always exposes it.
    return ModeRead::UNAVAILABLE;
  }
  NimBLEAttValue value;
  if (!gattRead(m_OperationMode, value) || value.length() < 1) {
    return ModeRead::FAILED;
  }
  mode = value.data()[0];
  m_LastOperationMode = mode;
  return ModeRead::VALUE;
}

Ricoh::GateResult Ricoh::captureGate(uint8_t &mode) {
  // A GR IV in BLE standby keeps the link up and reports CameraPower ON while
  // OperationMode is BLE_STARTUP. A capture write in that state cold boots the
  // camera (lens extends) and can wedge its firmware. Power ON is not
  // sufficient; only a fresh OperationMode read of CAPTURE authorizes the
  // shutter writes. The cached state and the notification stream can stay stale
  // for the whole held connection, so neither is consulted here. A non-CAPTURE
  // reading is always a safe refusal.
  if (!isConnected()) {
    ESP_LOGW(LOG_TAG, "Ricoh shutter refused: not connected");
    return GateResult::REFUSED;
  }
  const ModeRead read = readOperationMode(mode);
  if (read == ModeRead::UNAVAILABLE) {
    ESP_LOGW(LOG_TAG, "Ricoh OperationMode unavailable; allowing capture");
    return GateResult::AUTHORIZED;
  }
  if (read == ModeRead::FAILED) {
    ESP_LOGW(LOG_TAG, "Ricoh shutter refused: OperationMode read failed");
    return GateResult::REFUSED;
  }
  if (mode == static_cast<uint8_t>(OperationMode::CAPTURE)) {
    return GateResult::AUTHORIZED;
  }
  if (mode == static_cast<uint8_t>(OperationMode::BLE_STARTUP)) {
    return GateResult::ASLEEP;
  }
  ESP_LOGW(LOG_TAG, "Ricoh shutter refused: mode 0x%02X %s", mode, opModeName(mode));
  return GateResult::REFUSED;
}

const char *Ricoh::wakeStateName(WakeState state) {
  switch (state) {
    case WakeState::IDLE:
      return "IDLE";
    case WakeState::RELINK:
      return "RELINK";
    case WakeState::PROBE:
      return "PROBE";
    case WakeState::AWAKE:
      return "AWAKE";
    case WakeState::FAILED:
      return "FAILED";
  }
  return "?";
}

bool Ricoh::wakePause(uint32_t milliseconds) {
  // One implementation of a cancellable wait, on the Camera side: it owns the
  // scheduler-clock rule and the plan 170 poll. Ten millisecond slices keep the
  // probe responsive to a teardown without spraying reads.
  return pollDelay(milliseconds, WAKE_PAUSE_SLICE_MS);
}

bool Ricoh::wakeFromStandby(uint8_t asleepMode) {
  // Wake state machine, run on the target task with no other Camera lock held.
  //
  // RELINK is the only transition out of IDLE because dropping and
  // re-establishing the link is the only wake sequence the plan records. Writing
  // CameraPower is not an option: no source verifies that it wakes the body, and
  // the peer models the write as accepted-or-refused rather than as a wake.
  //
  // PROBE is the only state that can authorize a capture, and it authorizes on
  // the fresh read it just performed, never on a cached value or a notification.
  // A GR IV notifies CameraPower and OperationMode transitions, but a held or
  // freshly re-established link can repeat a stale value, so a notification is a
  // diagnostic here and never a verdict.
  //
  // FAILED writes no capture command, and leaves the link in whatever state
  // RELINK reached, so the user keeps a usable connection and the next press can
  // try again. The reconnect handshake itself repeats the connect-path identity
  // write and the subscriptions before any CAPTURE read authorizes a capture;
  // the invariant this path protects is the capture writes.
  //
  // A press that arrives while the session is already being torn down must not
  // drop a working link. Control::disconnect() sets the cancel token before it
  // posts the teardown, so relinking here can only lengthen the drain, and the
  // teardown then has to take down the link this path just rebuilt. The plan 148
  // cancel contract is that a cancelled press unwinds without touching the
  // camera.
  if (connectCancelled()) {
    ESP_LOGW(LOG_TAG, "Ricoh wake %s -> %s: cancelled by the user", wakeStateName(WakeState::IDLE),
             wakeStateName(WakeState::FAILED));
    return false;
  }

  WakeState state = WakeState::RELINK;
  ESP_LOGW(LOG_TAG, "Ricoh wake %s -> %s: camera asleep (mode 0x%02X %s)",
           wakeStateName(WakeState::IDLE), wakeStateName(state), asleepMode,
           opModeName(asleepMode));

  int64_t deadlineUs = 0;
  while (state == WakeState::RELINK || state == WakeState::PROBE) {
    if (state == WakeState::RELINK) {
      // The reconnect re-runs the whole connect handshake, so every
      // characteristic pointer is re-derived after the drop instead of surviving
      // it. Reusing a pointer across the rediscovery is the standby use-after-free
      // class this file already carries a regression for.
      if (!reconnectInPlace(FURBLE_RICOH_WAKE_CONNECT_TIMEOUT_MS, FURBLE_RICOH_WAKE_SETTLE_MS)) {
        ESP_LOGW(LOG_TAG, "Ricoh wake %s -> %s: the link did not come back", wakeStateName(state),
                 wakeStateName(WakeState::FAILED));
        state = WakeState::FAILED;
        break;
      }
      deadlineUs =
          esp_timer_get_time() + (static_cast<int64_t>(FURBLE_RICOH_WAKE_PROBE_TIMEOUT_MS) * 1000);
      ESP_LOGI(LOG_TAG, "Ricoh wake %s -> %s: link re-established, waiting up to %u ms",
               wakeStateName(state), wakeStateName(WakeState::PROBE),
               static_cast<unsigned>(FURBLE_RICOH_WAKE_PROBE_TIMEOUT_MS));
      state = WakeState::PROBE;
      continue;
    }

    // PROBE: one fresh OperationMode read per pass, bounded by the deadline.
    if (!isConnected()) {
      ESP_LOGW(LOG_TAG, "Ricoh wake %s -> %s: the link dropped during the wake",
               wakeStateName(state), wakeStateName(WakeState::FAILED));
      state = WakeState::FAILED;
      break;
    }
    if (connectCancelled()) {
      ESP_LOGW(LOG_TAG, "Ricoh wake %s -> %s: cancelled by the user", wakeStateName(state),
               wakeStateName(WakeState::FAILED));
      state = WakeState::FAILED;
      break;
    }

    uint8_t mode = STATE_UNKNOWN;
    const ModeRead read = readOperationMode(mode);
    if (read == ModeRead::VALUE) {
      if (mode == static_cast<uint8_t>(OperationMode::CAPTURE)) {
        ESP_LOGI(LOG_TAG, "Ricoh wake %s -> %s: fresh OperationMode read reports CAPTURE",
                 wakeStateName(state), wakeStateName(WakeState::AWAKE));
        state = WakeState::AWAKE;
        break;
      }
      if (mode != static_cast<uint8_t>(OperationMode::BLE_STARTUP)) {
        // A body that came back in another mode is not going to reach CAPTURE by
        // waiting, so stop instead of burning the whole budget.
        ESP_LOGW(LOG_TAG, "Ricoh wake %s -> %s: the body reports mode 0x%02X %s",
                 wakeStateName(state), wakeStateName(WakeState::FAILED), mode, opModeName(mode));
        state = WakeState::FAILED;
        break;
      }
    }

    if (esp_timer_get_time() >= deadlineUs) {
      ESP_LOGW(LOG_TAG, "Ricoh wake %s -> %s: no CAPTURE after %u ms", wakeStateName(state),
               wakeStateName(WakeState::FAILED),
               static_cast<unsigned>(FURBLE_RICOH_WAKE_PROBE_TIMEOUT_MS));
      state = WakeState::FAILED;
      break;
    }
    if (!wakePause(WAKE_POLL_MS)) {
      ESP_LOGW(LOG_TAG, "Ricoh wake %s -> %s: cancelled during the probe", wakeStateName(state),
               wakeStateName(WakeState::FAILED));
      state = WakeState::FAILED;
      break;
    }
  }

  return state == WakeState::AWAKE;
}

void Ricoh::shutterPress(void) {
  uint8_t mode = STATE_UNKNOWN;
  GateResult gate = captureGate(mode);
  if (gate == GateResult::ASLEEP) {
    // The wake's terminal read is the authorization: it is a fresh OperationMode
    // read of CAPTURE, taken immediately before the capture writes. Re-reading
    // here would add a round trip without adding any safety.
    if (!wakeFromStandby(mode)) {
      ESP_LOGW(LOG_TAG, "Ricoh shutter refused: camera did not wake (mode 0x%02X %s)", mode,
               opModeName(mode));
      return;
    }
    gate = GateResult::AUTHORIZED;
  }
  if (gate != GateResult::AUTHORIZED) {
    return;
  }
  if (!setShootingFlavor(ShootingFlavor::IMMEDIATE))
    return;
  writeOperation(OperationCode::START, OperationParameter::AF);
}

void Ricoh::shutterRelease(void) {
  ESP_LOGI(LOG_TAG, "Ricoh shutterRelease ignored (single-write capture)");
}

void Ricoh::focusPress(void) {
  ESP_LOGI(LOG_TAG, "Ricoh focusPress ignored (focus unsupported; no capture write)");
}

void Ricoh::focusRelease(void) {
  ESP_LOGI(LOG_TAG, "Ricoh focusRelease ignored (focus unsupported; no capture write)");
}

uint8_t Ricoh::captureStatus(void) const {
  return m_CaptureStatusValue.load();
}

bool Ricoh::captureInProgress(void) const {
  return m_CaptureStatusValue.load() == 0x01;
}

uint32_t Ricoh::capturesCompleted(void) const {
  return m_CapturesCompleted.load();
}

void Ricoh::updateGeoData(const gps_t &gps, const timesync_t &timesync) {
  if (!isConnected()) {
    ESP_LOGW(LOG_TAG, "Ricoh GPS skipped: not connected");
    return;
  }
  if (!std::isfinite(gps.latitude) || !std::isfinite(gps.longitude) || !std::isfinite(gps.altitude)
      || !validTimesync(timesync)) {
    ESP_LOGW(LOG_TAG,
             "Ricoh GPS invalid: lat=%.7f lon=%.7f alt=%.1f "
             "utc=%04u-%02u-%02u %02u:%02u:%02u.%02u",
             gps.latitude, gps.longitude, gps.altitude, timesync.year, timesync.month, timesync.day,
             timesync.hour, timesync.minute, timesync.second, timesync.centisecond);
    return;
  }

  // The Date Time characteristic carries local wall-clock time and has no
  // timezone field. Write it when the local minute changes, so a connected
  // camera follows the selected zone without a BLE write every GPS tick.
  const bool clockDue = !m_HasClockWrite || (m_LastClock.local.year != timesync.local.year)
                        || (m_LastClock.local.month != timesync.local.month)
                        || (m_LastClock.local.day != timesync.local.day)
                        || (m_LastClock.local.hour != timesync.local.hour)
                        || (m_LastClock.local.minute != timesync.local.minute);
  if (clockDue && m_DateTime != nullptr && m_DateTime->canWrite()) {
    const auto packet = RicohProtocol::encodeDateTime(timesync.local.year, timesync.local.month,
                                                      timesync.local.day, timesync.local.hour,
                                                      timesync.local.minute, timesync.local.second);
    if (gattWrite(m_DateTime, packet.data(), packet.size(), true)) {
      m_HasClockWrite = true;
      m_LastClock = timesync;
      ESP_LOGI(LOG_TAG, "Ricoh local clock %04u-%02u-%02u %02u:%02u:%02u", timesync.local.year,
               timesync.local.month, timesync.local.day, timesync.local.hour, timesync.local.minute,
               timesync.local.second);
    }
  }

  if (m_GpsInfo == nullptr || !m_GpsInfo->canWrite()) {
    ESP_LOGW(LOG_TAG, "Ricoh GPS characteristic unavailable");
    return;
  }

  const uint32_t nowMs = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
  const bool moved = !m_HasGpsWrite
                     || std::fabs(gps.latitude - m_LastGps.latitude) >= GPS_MIN_DELTA_DEG
                     || std::fabs(gps.longitude - m_LastGps.longitude) >= GPS_MIN_DELTA_DEG
                     || std::fabs(gps.altitude - m_LastGps.altitude) >= GPS_MIN_DELTA_ALT_M;
  const bool intervalElapsed =
      !m_HasGpsWrite || static_cast<uint32_t>(nowMs - m_LastGpsWriteMs) >= GPS_MIN_INTERVAL_MS;

  if (!moved && !intervalElapsed)
    return;

  if (!m_HasGpsWrite)
    setGeotagEnable(true);

  ricoh_geo_t geo = {
      .latitude = bswapd64(gps.latitude),
      .longitude = bswapd64(gps.longitude),
      .altitude = bswapd64(gps.altitude),
      .year_lsb = static_cast<uint8_t>(timesync.year & 0xff),
      .year_msb = static_cast<uint8_t>((timesync.year >> 8) & 0xff),
      .month = static_cast<uint8_t>(std::min(timesync.month, 255u)),
      .day = static_cast<uint8_t>(std::min(timesync.day, 255u)),
      .hour = static_cast<uint8_t>(std::min(timesync.hour, 255u)),
      .minute = static_cast<uint8_t>(std::min(timesync.minute, 255u)),
      .second = static_cast<uint8_t>(std::min(timesync.second, 255u)),
      // The official apps write the WGS84 datum selector here, not a
      // centisecond. See docs/camera-research/ricoh.md.
      .datum = 0,
  };

  bool rc = gattWrite(m_GpsInfo, reinterpret_cast<const uint8_t *>(&geo), sizeof(geo), true);
  ESP_LOGI(
      LOG_TAG, "Ricoh GPS lat=%.7f lon=%.7f alt=%.1f utc=%04u-%02u-%02u %02u:%02u:%02u.%02u => %s",
      gps.latitude, gps.longitude, gps.altitude, timesync.year, timesync.month, timesync.day,
      timesync.hour, timesync.minute, timesync.second, timesync.centisecond, rc ? "ok" : "failed");

  if (rc) {
    m_LastGpsWriteMs = nowMs;
    m_HasGpsWrite = true;
    m_LastGps = gps;
    m_LastTimesync = timesync;
  }
}

size_t Ricoh::getSerialisedBytes(void) const {
  return sizeof(ricoh_t);
}

bool Ricoh::serialise(void *buffer, size_t bytes) const {
  if (bytes != sizeof(ricoh_t))
    return false;

  ricoh_t *x = static_cast<ricoh_t *>(buffer);
  strncpy(x->name, m_Name.c_str(), MAX_NAME);
  x->name[MAX_NAME - 1] = '\0';
  x->address = static_cast<uint64_t>(m_Address);
  x->type = m_Address.getType();

  return true;
}

}  // namespace Furble
