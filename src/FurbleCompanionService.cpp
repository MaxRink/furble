#include <algorithm>
#include <climits>
#include <cstring>
#include <utility>

#include "CameraList.h"
#include "Scan.h"

#include "../include/FurbleCompanionService.h"
#include "FurbleControl.h"
#include "FurbleFeedback.h"
#include "FurbleGPS.h"
#include "FurbleSettings.h"
#include "FurbleTypes.h"
#include "FurbleUI.h"

namespace Furble {

namespace {

// Stable companion wire form for the interval setting.
//
// The NVS interval_t stores each field as SpinValue::nvs_t. Its unit_t enum is
// an unscoped enum that the compiler sizes as 4 bytes, so nvs_t is 6 bytes and
// interval_t is 24 bytes. That layout is a firmware storage detail and must not
// leak onto the companion characteristic. The companion instead uses this
// packed 12-byte form: four {uint16 value little endian, uint8 unit} fields in
// count, delay, shutter, wait order. The static_assert locks the wire size so a
// future field change cannot silently shift the Android companion decoder.
struct __attribute__((packed)) interval_wire_field_t {
  uint16_t value;
  uint8_t unit;
};

struct __attribute__((packed)) interval_wire_t {
  interval_wire_field_t count;
  interval_wire_field_t delay;
  interval_wire_field_t shutter;
  interval_wire_field_t wait;
};

static_assert(sizeof(interval_wire_t) == 12, "companion interval wire must stay 12 bytes");

interval_wire_field_t packField(const SpinValue::nvs_t &nvs) {
  return {nvs.value, static_cast<uint8_t>(nvs.unit)};
}

SpinValue::nvs_t unpackField(const interval_wire_field_t &field) {
  return {field.value, static_cast<SpinValue::unit_t>(field.unit)};
}

interval_wire_t packInterval(const interval_t &interval) {
  return {packField(interval.count), packField(interval.delay), packField(interval.shutter),
          packField(interval.wait)};
}

bool unpackInterval(const uint8_t *data, size_t length, interval_t &interval) {
  if (length != sizeof(interval_wire_t)) {
    return false;
  }
  interval_wire_t wire;
  std::memcpy(&wire, data, sizeof(wire));
  // Reject unit codes the firmware does not understand.
  for (const auto &field : {wire.count, wire.delay, wire.shutter, wire.wait}) {
    if (field.unit > SpinValue::UNIT_MIN) {
      return false;
    }
  }
  interval.count = unpackField(wire.count);
  interval.delay = unpackField(wire.delay);
  interval.shutter = unpackField(wire.shutter);
  interval.wait = unpackField(wire.wait);
  return true;
}

bool selectionContains(const Settings::multiselect_t &selection, const std::string &name) {
  char selectedName[Settings::MULTISELECT_NAME_MAX] = {};
  std::strncpy(selectedName, name.c_str(), sizeof(selectedName) - 1);
  const size_t count = std::min<size_t>(selection.count, Settings::MULTISELECT_MAX);
  for (size_t i = 0; i < count; i++) {
    if (std::strncmp(selection.name[i], selectedName, sizeof(selectedName)) == 0) {
      return true;
    }
  }
  return false;
}

void copySelectionName(char *destination, const std::string &name) {
  std::memset(destination, 0, Settings::MULTISELECT_NAME_MAX);
  std::strncpy(destination, name.c_str(), Settings::MULTISELECT_NAME_MAX - 1);
}

bool addSelectionName(Settings::multiselect_t &selection, const std::string &name) {
  if (selectionContains(selection, name)) {
    return true;
  }
  if (selection.count >= Settings::MULTISELECT_MAX) {
    return false;
  }
  copySelectionName(selection.name[selection.count], name);
  selection.count++;
  return true;
}

void removeSelectionName(Settings::multiselect_t &selection, const std::string &name) {
  char selectedName[Settings::MULTISELECT_NAME_MAX] = {};
  std::strncpy(selectedName, name.c_str(), sizeof(selectedName) - 1);
  size_t write = 0;
  const size_t count = std::min<size_t>(selection.count, Settings::MULTISELECT_MAX);
  for (size_t read = 0; read < count; read++) {
    if (std::strncmp(selection.name[read], selectedName, sizeof(selectedName)) != 0) {
      if (write != read) {
        std::memcpy(selection.name[write], selection.name[read], sizeof(selection.name[write]));
      }
      write++;
    }
  }
  for (size_t i = write; i < Settings::MULTISELECT_MAX; i++) {
    std::memset(selection.name[i], 0, sizeof(selection.name[i]));
  }
  selection.count = static_cast<uint8_t>(write);
}

}  // namespace

CompanionService::CompanionService(CompanionTransport &transport) : m_Transport {transport} {}

uint64_t CompanionService::nowMs(void) {
  return static_cast<uint64_t>(esp_timer_get_time()) / 1000;
}

void CompanionService::init(void) {
  const std::lock_guard<std::mutex> lock(m_Mutex);
  if (m_TimedShutterTimer != nullptr) {
    return;
  }

  const esp_timer_create_args_t args = {
      .callback = timedShutter,
      .arg = this,
      .dispatch_method = ESP_TIMER_TASK,
      .name = "companion shutter",
      .skip_unhandled_events = false,
  };
  if (esp_timer_create(&args, &m_TimedShutterTimer) != ESP_OK) {
    ESP_LOGE(LOG_TAG, "Failed to create companion shutter timer");
  }
}

void CompanionService::deinit(void) {
  esp_timer_handle_t timer = nullptr;
  {
    const std::lock_guard<std::mutex> lock(m_Mutex);
    timer = m_TimedShutterTimer;
    m_TimedShutterTimer = nullptr;
  }
  if (timer != nullptr) {
    if (esp_timer_is_active(timer)) {
      esp_timer_stop(timer);
    }
    esp_timer_delete(timer);
  }
}

void CompanionService::onConnected(void) {
  const std::lock_guard<std::mutex> lock(m_Mutex);
  m_HaveLastStatus = false;
  m_LastStatusNotificationMs = 0;
  m_HaveLastCameraSnapshots = false;
  m_LastCameraSnapshots.clear();
  m_LastCameraNotificationMs = 0;
}

void CompanionService::onDisconnected(void) {
  {
    const std::lock_guard<std::mutex> lock(m_Mutex);
    m_HaveLastCameraSnapshots = false;
    m_LastCameraSnapshots.clear();
    m_LastCameraNotificationMs = 0;
  }
  releaseHeldCommands();
}

void CompanionService::beginPairing(uint32_t pin) {
  const std::lock_guard<std::mutex> lock(m_Mutex);
  m_PendingPairing = true;
  m_PendingPairingPin = pin;
}

bool CompanionService::hasPendingPairing(void) const {
  const std::lock_guard<std::mutex> lock(m_Mutex);
  return m_PendingPairing;
}

uint32_t CompanionService::getPendingPairingPin(void) const {
  const std::lock_guard<std::mutex> lock(m_Mutex);
  return m_PendingPairingPin;
}

void CompanionService::confirmPairing(bool accept) {
  (void)accept;
  const std::lock_guard<std::mutex> lock(m_Mutex);
  if (!m_PendingPairing) {
    return;
  }
  m_PendingPairing = false;
  m_PendingPairingPin = 0;
}

void CompanionService::setSettingReloadCallback(std::function<void(bool)> callback) {
  m_SettingReloadCallback = std::move(callback);
}

CompanionService::companion_status_t CompanionService::getStatus(void) const {
  companion_status_t status = {};
  status.version = PACKET_VERSION;

  const int32_t batteryLevel = UI::getBatteryLevel();
  status.battery_percent =
      (batteryLevel >= 0 && batteryLevel <= 100) ? static_cast<uint8_t>(batteryLevel) : 255;

  const int32_t batteryVoltage = UI::getBatteryVoltage();
  status.battery_mv = (batteryVoltage >= 0) ? static_cast<uint16_t>(batteryVoltage) : 0xffff;

  const int32_t batteryCurrent = UI::getBatteryCurrent();
  status.battery_ma = static_cast<int16_t>(std::clamp<int32_t>(
      batteryCurrent, static_cast<int32_t>(-32768), static_cast<int32_t>(32767)));
  if (UI::isBatteryCharging()) {
    status.power_flags |= 1 << 0;
  }
  if (UI::getBatteryVBUSVoltage() > 0) {
    status.power_flags |= 1 << 1;
  }

  const auto &control = Control::getInstance();
  status.camera_total = static_cast<uint8_t>(std::min<size_t>(control.getTargetCount(), 255));
  status.camera_connected =
      static_cast<uint8_t>(std::min<size_t>(control.getConnectedTargetCount(), 255));
  switch (control.getState()) {
    case Control::STATE_IDLE:
      status.control_state = 0;
      break;
    case Control::STATE_CONNECT:
      status.control_state = 1;
      break;
    case Control::STATE_CONNECTING:
      status.control_state = 2;
      break;
    case Control::STATE_CONNECT_FAILED:
      status.control_state = 3;
      break;
    case Control::STATE_ACTIVE:
      status.control_state = 4;
      break;
    case Control::STATE_DISCONNECTING:
      status.control_state = 5;
      break;
  }

  const auto &gps = GPS::getInstance();
  status.gps_source = static_cast<uint8_t>(gps.getSource());
  status.gps_satellites = gps.getSatellites();
  status.ivl_state = UI::getIntervalometerState();
  status.ivl_remaining = UI::getIntervalometerRemaining();
  status.uptime_s = static_cast<uint32_t>(nowMs() / 1000);
  return status;
}

void CompanionService::notifyStatus(bool force) {
  if (!m_Transport.isConnected()) {
    return;
  }

  const companion_status_t status = getStatus();
  const uint64_t now = nowMs();
  bool shouldNotify = false;
  {
    const std::lock_guard<std::mutex> lock(m_Mutex);
    const bool changed =
        !m_HaveLastStatus || (std::memcmp(&status, &m_LastStatus, sizeof(status)) != 0);
    const bool keepalive = !m_HaveLastStatus || ((now - m_LastStatusNotificationMs) >= 30 * 1000);
    const bool rateAllowed = !m_HaveLastStatus || ((now - m_LastStatusNotificationMs) >= 1000);

    shouldNotify = force || keepalive || (changed && rateAllowed);
    if (shouldNotify) {
      // Publish the cache reservation before entering the transport. This
      // suppresses a concurrent duplicate without inverting the service and
      // production GATT mutex order around a virtual call.
      m_LastStatusNotificationMs = now;
      m_LastStatus = status;
      m_HaveLastStatus = true;
    }
  }

  if (shouldNotify) {
    m_Transport.notify(COMPANION_CHAR_STATUS, reinterpret_cast<const uint8_t *>(&status),
                       sizeof(status));
  }
}

void CompanionService::handleLocation(const uint8_t *data, size_t len) {
  if (data == nullptr || len < (offsetof(companion_fix_t, age_ms) + sizeof(uint32_t))) {
    ESP_LOGW(LOG_TAG, "Short companion location write");
    return;
  }

  companion_fix_t packet = {};
  std::memcpy(&packet, data, std::min<size_t>(len, sizeof(packet)));
  if ((packet.version == 0) || (packet.flags & ~(LOCATION_VALID | TIME_VALID | ALTITUDE_VALID))) {
    return;
  }

  GPS::external_fix_t fix = {};
  fix.gps = {
      packet.latitude,
      packet.longitude,
      packet.altitude,
      packet.satellites,
  };
  fix.timesync = {
      packet.year,   packet.month,  packet.day,         packet.hour,
      packet.minute, packet.second, packet.centisecond,
  };
  fix.age_ms = packet.age_ms;
  fix.position_valid = (packet.flags & LOCATION_VALID) != 0;
  fix.time_valid = (packet.flags & TIME_VALID) != 0;
  fix.altitude_valid = (packet.flags & ALTITUDE_VALID) != 0;
  GPS::getInstance().setExternalFix(fix);
}

std::vector<CompanionService::camera_snapshot_t> CompanionService::getCameraSnapshots(void) const {
  auto &control = Control::getInstance();
  const auto targets = control.getTargets();
  const auto controlState = control.getState();
  const bool multiconnect = Settings::load<Settings::MULTICONNECT>();
  const Settings::multiselect_t selection = Settings::load<Settings::MULTISELECT>();
  const std::shared_ptr<Camera> connecting = control.getConnectingCamera();

  std::vector<camera_snapshot_t> snapshots;
  for (size_t n = 0; n < CameraList::size(); n++) {
    std::shared_ptr<Camera> camera = CameraList::get(n);
    const uint8_t cameraId = CameraList::getCameraId(camera.get());
    if (cameraId == 0) {
      continue;
    }

    bool activeTarget = false;
    for (const auto *target : targets) {
      if ((target != nullptr) && (target->getCamera() == camera)) {
        activeTarget = true;
        break;
      }
    }

    const bool connected = camera->isConnected();
    camera_snapshot_t snapshot = {};
    snapshot.camera_id = cameraId;
    snapshot.cam_type = static_cast<uint8_t>(static_cast<uint32_t>(camera->getType()));
    snapshot.flags = CAMERA_SAVED;
    if (camera->isActive() || (multiconnect && selectionContains(selection, camera->getName()))) {
      snapshot.flags |= CAMERA_SELECTED;
    }
    if (activeTarget) {
      snapshot.flags |= CAMERA_ACTIVE_TARGET;
    }
    if (connected) {
      snapshot.flags |= CAMERA_CONNECTED_FLAG;
    }
    snapshot.progress = camera->getConnectProgress();
    snapshot.name = camera->getName();

    if (connected) {
      uint16_t interval = 0;
      uint16_t latency = 0;
      uint16_t timeout = 0;
      int rssi = 0;
      if (camera->getConnParams(interval, latency, timeout, rssi) && (rssi != 0)) {
        snapshot.rssi = static_cast<int8_t>(std::clamp(rssi, -127, 127));
      }
    }

    if (controlState == Control::STATE_DISCONNECTING) {
      snapshot.state = CAMERA_DISCONNECTING;
    } else if (connected) {
      snapshot.state = CAMERA_CONNECTED;
    } else if (!activeTarget) {
      snapshot.state = CAMERA_IDLE;
    } else {
      switch (controlState) {
        case Control::STATE_CONNECT:
          snapshot.state = CAMERA_CONNECTING;
          break;
        case Control::STATE_CONNECTING:
          snapshot.state = (connecting == camera) ? CAMERA_CONNECTING : CAMERA_RECONNECTING;
          break;
        case Control::STATE_CONNECT_FAILED:
          snapshot.state = CAMERA_LOST;
          break;
        case Control::STATE_ACTIVE:
          snapshot.state = CAMERA_RECONNECTING;
          break;
        case Control::STATE_IDLE:
          snapshot.state = CAMERA_IDLE;
          break;
        case Control::STATE_DISCONNECTING:
          snapshot.state = CAMERA_DISCONNECTING;
          break;
      }
    }

    snapshots.push_back(std::move(snapshot));
  }

  return snapshots;
}

bool CompanionService::cameraSnapshotEqual(const camera_snapshot_t &left,
                                           const camera_snapshot_t &right) {
  return (left.camera_id == right.camera_id) && (left.status == right.status)
         && (left.cam_type == right.cam_type) && (left.flags == right.flags)
         && (left.progress == right.progress) && (left.rssi == right.rssi)
         && (left.state == right.state) && (left.name == right.name);
}

void CompanionService::appendCameraRecord(std::vector<uint8_t> &response,
                                          const camera_snapshot_t &snapshot) {
  const size_t nameLength = std::min<size_t>(snapshot.name.size(), 64);
  const companion_camera_record_t record = {
      snapshot.status,   snapshot.camera_id, snapshot.cam_type, snapshot.flags,
      snapshot.progress, snapshot.rssi,      snapshot.state,    static_cast<uint8_t>(nameLength),
  };
  const auto *bytes = reinterpret_cast<const uint8_t *>(&record);
  response.insert(response.end(), bytes, bytes + sizeof(record));
  response.insert(response.end(), snapshot.name.begin(), snapshot.name.begin() + nameLength);
}

bool CompanionService::updateCameraSelection(uint8_t cameraId, bool selected) {
  Settings::multiselect_t selection = Settings::load<Settings::MULTISELECT>();
  const Settings::multiselect_t original = selection;

  if (cameraId == 0xff) {
    if (!selected) {
      selection = {};
      for (size_t n = 0; n < CameraList::size(); n++) {
        CameraList::get(n)->setActive(false);
      }
    } else {
      selection = {};
      for (size_t n = 0; n < CameraList::size(); n++) {
        std::shared_ptr<Camera> camera = CameraList::get(n);
        if (CameraList::getCameraId(camera.get()) == 0) {
          continue;
        }
        if (!addSelectionName(selection, camera->getName())) {
          return false;
        }
      }
      for (size_t n = 0; n < CameraList::size(); n++) {
        std::shared_ptr<Camera> camera = CameraList::get(n);
        camera->setActive(CameraList::getCameraId(camera.get()) != 0);
      }
    }
  } else {
    std::shared_ptr<Camera> selectedCamera = nullptr;
    for (size_t n = 0; n < CameraList::size(); n++) {
      std::shared_ptr<Camera> camera = CameraList::get(n);
      if (CameraList::getCameraId(camera.get()) == cameraId) {
        selectedCamera = camera;
        break;
      }
    }
    if (selectedCamera == nullptr) {
      return false;
    }

    if (selected) {
      if (!addSelectionName(selection, selectedCamera->getName())) {
        return false;
      }
      selectedCamera->setActive(true);
    } else {
      removeSelectionName(selection, selectedCamera->getName());
      selectedCamera->setActive(false);
    }
  }

  if (std::memcmp(&original, &selection, sizeof(selection)) != 0) {
    Settings::save<Settings::MULTISELECT>(selection);
  }
  return true;
}

bool CompanionService::applyCameraSelection(void) {
  if (!Settings::load<Settings::MULTICONNECT>()) {
    return false;
  }

  const Settings::multiselect_t selection = Settings::load<Settings::MULTISELECT>();
  bool anySelected = false;
  for (size_t n = 0; n < CameraList::size(); n++) {
    std::shared_ptr<Camera> camera = CameraList::get(n);
    const bool selected = (CameraList::getCameraId(camera.get()) != 0)
                          && selectionContains(selection, camera->getName());
    camera->setActive(selected);
    anySelected = anySelected || selected;
  }
  return anySelected;
}

void CompanionService::notifyCameras(bool force) {
  if (!m_Transport.isConnected()) {
    return;
  }

  const std::vector<camera_snapshot_t> snapshots = getCameraSnapshots();
  const uint64_t now = nowMs();
  const bool rateAllowed =
      (m_LastCameraNotificationMs == 0) || ((now - m_LastCameraNotificationMs) >= 1000);
  if (!force && !rateAllowed) {
    return;
  }

  std::vector<const camera_snapshot_t *> changed;
  for (const auto &snapshot : snapshots) {
    const auto previous = std::find_if(
        m_LastCameraSnapshots.begin(), m_LastCameraSnapshots.end(),
        [&snapshot](const auto &item) { return item.camera_id == snapshot.camera_id; });
    if (force || !m_HaveLastCameraSnapshots || (previous == m_LastCameraSnapshots.end())
        || !cameraSnapshotEqual(snapshot, *previous)) {
      changed.push_back(&snapshot);
    }
  }

  for (const auto *snapshot : changed) {
    std::vector<uint8_t> record;
    appendCameraRecord(record, *snapshot);
    m_Transport.notify(COMPANION_CHAR_CAMERAS, record.data(), record.size());
  }

  if (!changed.empty() || force) {
    m_LastCameraSnapshots = snapshots;
    m_HaveLastCameraSnapshots = true;
    m_LastCameraNotificationMs = now;
  }
}

void CompanionService::handleCameras(const uint8_t *data, size_t len) {
  if (!m_Transport.isEncrypted() || !m_Transport.isAuthenticated() || data == nullptr) {
    return;
  }

  const uint8_t op = len > 0 ? data[0] : 0xff;
  const uint8_t cameraId = len > 1 ? data[1] : 0xff;
  auto indicate = [this](const camera_snapshot_t &snapshot) {
    std::vector<uint8_t> response;
    appendCameraRecord(response, snapshot);
    if (!response.empty()) {
      m_Transport.indicate(COMPANION_CHAR_CAMERAS, response.data(), response.size());
    }
  };
  auto responseFor = [this](uint8_t status, uint8_t id) {
    camera_snapshot_t response = {};
    response.status = status;
    response.camera_id = id;
    if (id != 0xff) {
      const auto snapshots = getCameraSnapshots();
      const auto found =
          std::find_if(snapshots.begin(), snapshots.end(),
                       [id](const auto &snapshot) { return snapshot.camera_id == id; });
      if (found != snapshots.end()) {
        response = *found;
        response.status = status;
      }
    }
    return response;
  };

  if ((len != 2) || (op > 4)) {
    indicate(responseFor(CAMERA_REJECTED, cameraId));
    return;
  }

  if (op == 0) {
    const auto snapshots = getCameraSnapshots();
    for (const auto &snapshot : snapshots) {
      indicate(snapshot);
    }
    indicate(responseFor(CAMERA_OK, 0xff));
    return;
  }

  if ((op == 3) || (op == 4)) {
    if (!Settings::load<Settings::MULTICONNECT>()) {
      indicate(responseFor(CAMERA_REJECTED, cameraId));
      return;
    }
    bool known = (cameraId == 0xff);
    if (!known) {
      const auto snapshots = getCameraSnapshots();
      known = std::any_of(snapshots.begin(), snapshots.end(), [cameraId](const auto &snapshot) {
        return snapshot.camera_id == cameraId;
      });
    }
    if (!known || !updateCameraSelection(cameraId, op == 3)) {
      indicate(responseFor(known ? CAMERA_REJECTED : CAMERA_UNKNOWN_ID, cameraId));
      return;
    }
    indicate(responseFor(CAMERA_OK, cameraId));
    notifyCameras(true);
    return;
  }

  if (op == 2) {
    // Control has no per-target address yet, so v1 disconnects all targets.
    Control::getInstance().disconnect();
    indicate(responseFor(CAMERA_OK, 0xff));
    notifyCameras(true);
    return;
  }

  const auto snapshots = getCameraSnapshots();
  if ((cameraId != 0xff)
      && !std::any_of(snapshots.begin(), snapshots.end(), [cameraId](const auto &snapshot) {
           return snapshot.camera_id == cameraId;
         })) {
    indicate(responseFor(CAMERA_UNKNOWN_ID, cameraId));
    return;
  }

  auto &control = Control::getInstance();
  if (Scan::getInstance().isActive() || (control.getState() != Control::STATE_IDLE)
      || (control.getTargetCount() != 0)) {
    indicate(responseFor(CAMERA_BUSY, cameraId));
    return;
  }

  if (cameraId == 0xff) {
    if (!applyCameraSelection()) {
      indicate(responseFor(CAMERA_REJECTED, cameraId));
      return;
    }
  } else {
    for (size_t n = 0; n < CameraList::size(); n++) {
      std::shared_ptr<Camera> camera = CameraList::get(n);
      camera->setActive(CameraList::getCameraId(camera.get()) == cameraId);
    }
  }

  size_t active = 0;
  for (size_t n = 0; n < CameraList::size(); n++) {
    std::shared_ptr<Camera> camera = CameraList::get(n);
    if (camera->isActive()) {
      control.addActive(camera);
      active++;
    }
  }
  if (active == 0) {
    indicate(responseFor(CAMERA_REJECTED, cameraId));
    return;
  }

  control.connectAll(Settings::load<Settings::RECONNECT>());
  indicate(responseFor(CAMERA_OK, cameraId));
  notifyCameras(true);
}

CompanionService::setting_type_t CompanionService::settingType(Settings::type_t type) {
  switch (type) {
    case Settings::GPS:
    case Settings::IMU:
    case Settings::IR:
    case Settings::GPS_NMEA:
    case Settings::PRESET_PICKER:
    case Settings::CONN_SAVER:
    case Settings::MULTICONNECT:
    case Settings::TX_ADAPTIVE:
    case Settings::RECONNECT:
    case Settings::RECON_BACKOFF:
    case Settings::FAUXNY:
    case Settings::AUTOCONNECT:
    case Settings::COMPANION:
    case Settings::SHOW_TITLE:
    case Settings::SLEEP_CONN:
    case Settings::SD_GPX:
    case Settings::BOOT_SPLASH:
    case Settings::BATTERY_SAVER:
    case Settings::AUTO_OFF_CHARGING:
#if defined(FURBLE_M5STICKS3)
    case Settings::WATCHDOG:
#endif
      return SETTING_BOOL;
    case Settings::BRIGHTNESS:
    case Settings::INACTIVITY:
    case Settings::DISPLAY_OFF:
    case Settings::TX_POWER:
    case Settings::GPS_RATE:
    case Settings::GPS_CONSTEL:
    case Settings::GPS_POWER:
    case Settings::GPS_DUTY:
    case Settings::GPS_ASSIST:
    case Settings::IR_PROTO:
    case Settings::FB_OUTPUT:
    case Settings::FB_EVENTS:
    case Settings::FB_VOLUME:
    case Settings::AUTO_OFF:
    case Settings::LOW_BATT:
#if !defined(FURBLE_NO_DISPLAY)
    case Settings::DISPLAY_MODE:
#endif
    case Settings::CPU_FREQ:
    case Settings::BATT_STYLE:
    case Settings::SCAN_MODE:
    case Settings::TEXT_SIZE:
      return SETTING_U8;
    case Settings::GPS_BAUD:
    case Settings::SCAN_TIMEOUT:
      return SETTING_U32;
    case Settings::THEME:
    case Settings::BUTTON_MODE:
      return SETTING_STRING;
    case Settings::INTERVAL:
    case Settings::MULTISELECT:
      return SETTING_BLOB;
    case Settings::BULB:
    case Settings::TOUCH_CALIBRATION:
    case Settings::GPX_PERIOD:
    case Settings::MULTISELECT:
      return SETTING_BLOB;
  }
  return SETTING_BLOB;
}

bool CompanionService::settingValue(Settings::type_t type, std::vector<uint8_t> &value) {
  switch (type) {
    case Settings::GPS:
    case Settings::IMU:
    case Settings::IR:
    case Settings::GPS_NMEA:
    case Settings::PRESET_PICKER:
    case Settings::CONN_SAVER:
    case Settings::MULTICONNECT:
    case Settings::TX_ADAPTIVE:
    case Settings::RECONNECT:
    case Settings::RECON_BACKOFF:
    case Settings::FAUXNY:
    case Settings::AUTOCONNECT:
    case Settings::COMPANION:
    case Settings::SHOW_TITLE:
    case Settings::SLEEP_CONN:
    case Settings::SD_GPX:
    case Settings::BOOT_SPLASH:
    case Settings::BATTERY_SAVER:
    case Settings::AUTO_OFF_CHARGING:
#if defined(FURBLE_M5STICKS3)
    case Settings::WATCHDOG:
#endif
    {
      const bool v = Settings::load<bool>(type);
      value.assign(reinterpret_cast<const uint8_t *>(&v),
                   reinterpret_cast<const uint8_t *>(&v) + 1);
      return true;
    }
    case Settings::BRIGHTNESS:
    case Settings::INACTIVITY:
    case Settings::DISPLAY_OFF:
    case Settings::TX_POWER:
    case Settings::GPS_RATE:
    case Settings::GPS_CONSTEL:
    case Settings::GPS_POWER:
    case Settings::GPS_DUTY:
    case Settings::GPS_ASSIST:
    case Settings::IR_PROTO:
    case Settings::FB_OUTPUT:
    case Settings::FB_EVENTS:
    case Settings::FB_VOLUME:
    case Settings::AUTO_OFF:
    case Settings::LOW_BATT:
#if !defined(FURBLE_NO_DISPLAY)
    case Settings::DISPLAY_MODE:
#endif
    case Settings::CPU_FREQ:
    case Settings::BATT_STYLE:
    case Settings::SCAN_MODE:
    case Settings::TEXT_SIZE:
    {
      const uint8_t v = Settings::load<uint8_t>(type);
      value.assign(1, v);
      return true;
    }
    case Settings::GPS_BAUD:
    case Settings::SCAN_TIMEOUT:
    {
      const uint32_t v = Settings::load<uint32_t>(type);
      value.resize(sizeof(v));
      std::memcpy(value.data(), &v, sizeof(v));
      return true;
    }
    case Settings::THEME:
    case Settings::BUTTON_MODE:
    {
      const std::string v = Settings::load<std::string>(type);
      value.assign(v.begin(), v.end());
      return value.size() <= 255;
    }
    case Settings::INTERVAL:
    {
      const interval_t v = Settings::load<interval_t>(type);
      const interval_wire_t wire = packInterval(v);
      value.resize(sizeof(wire));
      std::memcpy(value.data(), &wire, sizeof(wire));
      return true;
    }
    case Settings::MULTISELECT:
    {
      const Settings::multiselect_t v = Settings::load<Settings::multiselect_t>(type);
      value.resize(sizeof(v));
      std::memcpy(value.data(), &v, sizeof(v));
      return true;
    }
    case Settings::BULB:
    case Settings::TOUCH_CALIBRATION:
    case Settings::GPX_PERIOD:
    case Settings::MULTISELECT:
      return false;
  }
  return false;
}

bool CompanionService::saveSetting(Settings::type_t type, const uint8_t *value, uint8_t length) {
  const setting_type_t wireType = settingType(type);
  switch (wireType) {
    case SETTING_BOOL:
      if (length != 1 || (value[0] > 1)) {
        return false;
      }
      Settings::save<bool>(type, value[0] != 0);
      return true;
    case SETTING_U8:
      if (length != 1) {
        return false;
      }
      if ((type == Settings::GPS_ASSIST) && (value[0] > 2)) {
        return false;
      }
      Settings::save<uint8_t>(type, value[0]);
      return true;
    case SETTING_U32:
    {
      if (length != sizeof(uint32_t)) {
        return false;
      }
      uint32_t v;
      std::memcpy(&v, value, sizeof(v));
      Settings::save<uint32_t>(type, v);
      return true;
    }
    case SETTING_STRING:
    {
      const std::string v(reinterpret_cast<const char *>(value), length);
      if ((type == Settings::BUTTON_MODE) && (v != Settings::BUTTON_MODE_TWO_BUTTON_VALUE)
          && (v != Settings::BUTTON_MODE_ONE_BUTTON_VALUE)) {
        return false;
      }
      Settings::save<std::string>(type, v);
      return true;
    }
    case SETTING_BLOB:
    {
      if (type == Settings::INTERVAL) {
        interval_t interval;
        if (!unpackInterval(value, length, interval)) {
          return false;
        }
        Settings::save<interval_t>(type, interval);
        return true;
      }
      if (type == Settings::MULTISELECT) {
        if (length != sizeof(Settings::multiselect_t)) {
          return false;
        }
        Settings::multiselect_t selection;
        std::memcpy(&selection, value, sizeof(selection));
        if (selection.count > Settings::MULTISELECT_MAX) {
          return false;
        }
        Settings::save<Settings::multiselect_t>(type, selection);
        return true;
      }
      return false;
    }
  }
  return false;
}

void CompanionService::appendResponse(std::vector<uint8_t> &response,
                                      setting_status_t status,
                                      uint8_t id,
                                      setting_type_t type,
                                      uint8_t flags,
                                      const std::vector<uint8_t> &value,
                                      bool listRecord) {
  if (value.size() > 255) {
    return;
  }
  response.push_back(static_cast<uint8_t>(status));
  response.push_back(id);
  response.push_back(static_cast<uint8_t>(type));
  response.push_back(static_cast<uint8_t>(value.size()));
  response.insert(response.end(), value.begin(), value.end());
  if (listRecord) {
    // Keep the flags trailing. The v1 app parses them after the value.
    response.push_back(flags);
  }
}

void CompanionService::notifySettings(const std::vector<uint8_t> &value) {
  if (!value.empty()) {
    m_Transport.indicate(COMPANION_CHAR_SETTINGS, value.data(), value.size());
  }
}

void CompanionService::handleSettings(const uint8_t *data, size_t len) {
  if (!m_Transport.isEncrypted() || !m_Transport.isAuthenticated() || data == nullptr
      || (len < 3)) {
    return;
  }

  const uint8_t op = data[0];
  const uint8_t id = data[1];
  const uint8_t length = data[2];
  const bool lengthMatches = len == (static_cast<size_t>(3) + length);
  const Settings::setting_t *setting = Settings::getByWireId(id);

  if (!lengthMatches || ((op <= 1) && (length != 0)) || (op > 2)) {
    std::vector<uint8_t> response;
    appendResponse(response, SETTING_BAD_LENGTH, id, SETTING_BLOB, 0, {}, false);
    notifySettings(response);
    return;
  }

  if (op == 0) {
    std::vector<const Settings::setting_t *> settings;
    for (const auto &it : Settings::all()) {
      if (it.second.wire_id != 0) {
        settings.push_back(&it.second);
      }
    }
    std::sort(settings.begin(), settings.end(),
              [](const auto *left, const auto *right) { return left->wire_id < right->wire_id; });
    for (const auto *entry : settings) {
      std::vector<uint8_t> current;
      if (settingValue(entry->type, current)) {
        std::vector<uint8_t> response;
        uint8_t flags = Settings::appliesImmediately(entry->type) ? 0 : SETTING_NEEDS_RESTART;
        if (Settings::isDangerous(entry->type)) {
          flags |= SETTING_DANGEROUS;
        }
        appendResponse(response, SETTING_OK, entry->wire_id, settingType(entry->type), flags,
                       current, true);
        notifySettings(response);
      }
    }
    std::vector<uint8_t> terminator;
    appendResponse(terminator, SETTING_OK, 0xff, SETTING_BLOB, 0, {}, true);
    notifySettings(terminator);
    return;
  }

  if (setting == nullptr) {
    std::vector<uint8_t> response;
    appendResponse(response, SETTING_UNKNOWN_ID, id, SETTING_BLOB, 0, {}, false);
    notifySettings(response);
    return;
  }

  const setting_type_t type = settingType(setting->type);
  if (op == 1) {
    std::vector<uint8_t> current;
    const bool valueValid = settingValue(setting->type, current);
    std::vector<uint8_t> response;
    appendResponse(response, valueValid ? SETTING_OK : SETTING_REJECTED, id, type, 0, current,
                   false);
    notifySettings(response);
    return;
  }

  const uint8_t expected =
      (type == SETTING_BOOL || type == SETTING_U8)
          ? 1
          : (type == SETTING_U32 ? sizeof(uint32_t)
                                 : (type == SETTING_STRING ? length
                                                           : (setting->type == Settings::MULTISELECT
                                                                  ? sizeof(Settings::multiselect_t)
                                                                  : sizeof(interval_wire_t))));
  const bool saved = (type == SETTING_STRING || length == expected)
                     && saveSetting(setting->type, data + 3, length);
  std::vector<uint8_t> response;
  appendResponse(response, saved ? SETTING_OK : SETTING_BAD_LENGTH, id, type, 0, {}, false);
  notifySettings(response);

  if (!saved) {
    return;
  }

  switch (setting->type) {
    case Settings::GPS:
    case Settings::GPS_BAUD:
    case Settings::GPS_RATE:
    case Settings::GPS_NMEA:
    case Settings::GPS_CONSTEL:
    case Settings::GPS_POWER:
    case Settings::GPS_DUTY:
    case Settings::GPS_ASSIST:
      GPS::getInstance().reloadSetting();
      break;
    case Settings::FB_EVENTS:
    case Settings::FB_VOLUME:
      // Direct call like the GPS case above: the UI request queue exists only
      // with FURBLE_CONSOLE and the companion also runs in release builds.
      // reload() is task-safe, with the output frozen at boot it is two byte
      // stores into the cache. FB_OUTPUT stays restart-only.
      Feedback::getInstance().reload();
      break;
    case Settings::TX_POWER:
      Control::getInstance().setPower(Settings::load<esp_power_level_t>(Settings::TX_POWER));
      break;
    case Settings::COMPANION:
      if (m_SettingReloadCallback) {
        m_SettingReloadCallback(false);
      }
      break;
    case Settings::MULTISELECT:
      break;
    default:
      break;
  }
}

bool CompanionService::allowTrigger(void) {
  const uint64_t now = nowMs();
  if ((now - m_CommandWindowMs) >= 1000) {
    m_CommandWindowMs = now;
    m_CommandCount = 0;
  }
  if (m_CommandCount >= 10) {
    return false;
  }
  m_CommandCount++;
  return true;
}

void CompanionService::handleTrigger(const uint8_t *data, size_t len) {
  if (!m_Transport.isEncrypted() || !m_Transport.isAuthenticated() || data == nullptr
      || (len < 2)) {
    return;
  }
  const uint8_t op = data[1];
  if ((data[0] == 0) || (op > 4) || ((op == 4) && (len != 4)) || ((op != 4) && (len != 2))) {
    return;
  }
  const std::lock_guard<std::mutex> lock(m_Mutex);
  if (Control::getInstance().getState() != Control::STATE_ACTIVE || !allowTrigger()) {
    return;
  }

  switch (op) {
    case 0:
      if (Control::getInstance().sendCommand(Control::CMD_SHUTTER_RELEASE) == pdTRUE) {
        m_ShutterHeld = false;
      }
      break;
    case 1:
      if (!m_ShutterHeld
          && (Control::getInstance().sendCommand(Control::CMD_SHUTTER_PRESS) == pdTRUE)) {
        m_ShutterHeld = true;
      }
      break;
    case 2:
      if (!m_FocusHeld
          && (Control::getInstance().sendCommand(Control::CMD_FOCUS_PRESS) == pdTRUE)) {
        m_FocusHeld = true;
      }
      break;
    case 3:
      if (Control::getInstance().sendCommand(Control::CMD_FOCUS_RELEASE) == pdTRUE) {
        m_FocusHeld = false;
      }
      break;
    case 4:
    {
      uint16_t holdMs;
      std::memcpy(&holdMs, data + 2, sizeof(holdMs));
      if (m_ShutterHeld
          || (Control::getInstance().sendCommand(Control::CMD_SHUTTER_PRESS) != pdTRUE)) {
        return;
      }
      m_ShutterHeld = true;
      if ((m_TimedShutterTimer == nullptr) || (holdMs == 0)) {
        // handleTrigger already owns m_Mutex. Release inline so the zero-delay
        // path does not recursively enter timedShutter and deadlock.
        Control::getInstance().sendCommand(Control::CMD_SHUTTER_RELEASE);
        m_ShutterHeld = false;
      } else {
        if (esp_timer_is_active(m_TimedShutterTimer)) {
          esp_timer_stop(m_TimedShutterTimer);
        }
        esp_timer_start_once(m_TimedShutterTimer, static_cast<uint64_t>(holdMs) * 1000);
      }
      break;
    }
    default:
      break;
  }
}

void CompanionService::releaseHeldCommands(void) {
  const std::lock_guard<std::mutex> lock(m_Mutex);
  if (m_ShutterHeld) {
    Control::getInstance().sendCommand(Control::CMD_SHUTTER_RELEASE);
    m_ShutterHeld = false;
  }
  if (m_FocusHeld) {
    Control::getInstance().sendCommand(Control::CMD_FOCUS_RELEASE);
    m_FocusHeld = false;
  }
}

void CompanionService::timedShutter(void *param) {
  auto *service = static_cast<CompanionService *>(param);
  const std::lock_guard<std::mutex> lock(service->m_Mutex);
  if (service->m_ShutterHeld) {
    Control::getInstance().sendCommand(Control::CMD_SHUTTER_RELEASE);
    service->m_ShutterHeld = false;
  }
}

}  // namespace Furble
