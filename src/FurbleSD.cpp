#include <cctype>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <M5Unified.h>
#include <driver/sdspi_host.h>
#include <esp_log.h>
#include <esp_system.h>
#include <esp_vfs_fat.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <sys/stat.h>
#include <unistd.h>

#include "FurblePlatform.h"
#include "FurbleSD.h"
#include "FurbleSettings.h"
#include "FurbleTypes.h"

namespace Furble {
namespace {

constexpr const char *SD_MOUNT_POINT = "/sd";
constexpr const char *SETTINGS_DIRECTORY = "/sd/furble";
constexpr const char *SETTINGS_FILE = "/sd/furble/settings.txt";
constexpr uint32_t SD_SPI_FREQUENCY_KHZ = 10000;
constexpr UBaseType_t WRITER_QUEUE_LENGTH = 8;
constexpr uint32_t WRITER_STACK_BYTES = 6144;
// same priority as the UI main task, SD calls block anyway, so a long mount
// competes with the UI for CPU instead of preempting it
constexpr UBaseType_t WRITER_PRIORITY = 1;
// control requests share the queue with GPX points, a short send timeout
// rides out a transient point backlog instead of dropping the request
constexpr uint32_t REQUEST_SEND_TIMEOUT_MS = 100;
constexpr uint32_t POWER_OFF_WAIT_MS = 3000;
constexpr uint8_t MAX_FAILURES = 3;

bool isCoreBoard(void) {
  switch (M5.getBoard()) {
    case m5::board_t::board_M5Stack:
    case m5::board_t::board_M5StackCore2:
      return true;
    default:
      return false;
  }
}

bool isHex(char c) {
  return std::isxdigit(static_cast<unsigned char>(c)) != 0;
}

char hexDigit(uint8_t value) {
  constexpr const char *digits = "0123456789abcdef";
  return digits[value & 0x0f];
}

std::string bytesToHex(const void *data, size_t length) {
  const auto *bytes = static_cast<const uint8_t *>(data);
  std::string result;
  result.reserve(length * 2);
  for (size_t i = 0; i < length; i++) {
    result += hexDigit(bytes[i] >> 4);
    result += hexDigit(bytes[i]);
  }
  return result;
}

bool parseUnsigned(const std::string &text, uint64_t maximum, uint64_t &value) {
  if (text.empty()) {
    return false;
  }

  char *end = nullptr;
  errno = 0;
  unsigned long long parsed = strtoull(text.c_str(), &end, 10);
  if ((errno != 0) || (end == text.c_str()) || (*end != '\0')
      || (parsed > static_cast<unsigned long long>(maximum))) {
    return false;
  }

  value = parsed;
  return true;
}

bool decodeHex(const std::string &text, void *data, size_t length) {
  if (text.size() != length * 2) {
    return false;
  }

  auto *bytes = static_cast<uint8_t *>(data);
  for (size_t i = 0; i < length; i++) {
    if (!isHex(text[2 * i]) || !isHex(text[(2 * i) + 1])) {
      return false;
    }
    char hex[3] = {text[2 * i], text[(2 * i) + 1], '\0'};
    bytes[i] = static_cast<uint8_t>(strtoul(hex, nullptr, 16));
  }
  return true;
}

bool decodeSizedHex(const std::string &text, void *data, size_t length) {
  const size_t separator = text.find(':');
  if (separator == std::string::npos) {
    return false;
  }

  uint64_t encodedLength = 0;
  if (!parseUnsigned(text.substr(0, separator), sizeof(uint8_t) * 65535, encodedLength)
      || (encodedLength != length)) {
    return false;
  }
  return decodeHex(text.substr(separator + 1), data, length);
}

std::string sizedHex(const void *data, size_t length) {
  return std::to_string(length) + ":" + bytesToHex(data, length);
}

bool parseBool(const std::string &text, bool &value) {
  if ((text == "true") || (text == "1")) {
    value = true;
    return true;
  }
  if ((text == "false") || (text == "0")) {
    value = false;
    return true;
  }
  return false;
}

bool validTheme(const std::string &value) {
  return (value == "Dark") || (value == "Default") || (value == "Mono Furble");
}

bool validButtonMode(const std::string &value) {
  return (value == Settings::BUTTON_MODE_TWO_BUTTON_VALUE)
         || (value == Settings::BUTTON_MODE_ONE_BUTTON_VALUE);
}

bool validSpinValue(const SpinValue::nvs_t &value, bool allowInfinite) {
  const uint8_t unit = static_cast<uint8_t>(value.unit);
  if ((value.value > 999) || (unit > SpinValue::UNIT_MIN)) {
    return false;
  }
  return allowInfinite || (unit != SpinValue::UNIT_INF);
}

bool validInterval(const interval_t &value) {
  return validSpinValue(value.count, true) && validSpinValue(value.delay, false)
         && validSpinValue(value.shutter, false) && validSpinValue(value.wait, false);
}

bool validCalibration(const Settings::calibration_t &value) {
  if ((M5.Display.width() <= 0) || (M5.Display.height() <= 0)) {
    return false;
  }

  for (const auto &point : value.pairs) {
    if ((point.x > M5.Display.width()) || (point.y > M5.Display.height())) {
      return false;
    }
  }
  return true;
}

bool validMultiselect(const Settings::multiselect_t &value) {
  if (value.count > Settings::MULTISELECT_MAX) {
    return false;
  }

  // Every name slot must be null terminated so later string use is safe.
  for (size_t i = 0; i < Settings::MULTISELECT_MAX; i++) {
    bool terminated = false;
    for (size_t j = 0; j < Settings::MULTISELECT_NAME_MAX; j++) {
      if (value.name[i][j] == '\0') {
        terminated = true;
        break;
      }
    }
    if (!terminated) {
      return false;
    }
  }
  return true;
}

bool serializeSetting(const Settings::setting_t &setting, std::string &value) {
  switch (setting.type) {
    case Settings::BRIGHTNESS:
    case Settings::INACTIVITY:
    case Settings::DISPLAY_OFF:
    case Settings::TX_POWER:
    case Settings::GPS_RATE:
    case Settings::GPS_CONSTEL:
    case Settings::GPS_POWER:
    case Settings::GPS_DUTY:
    case Settings::GPS_ASSIST:
    case Settings::CPU_FREQ:
    case Settings::BATT_STYLE:
    case Settings::TEXT_SIZE:
    case Settings::SCAN_MODE:
    case Settings::IR_PROTO:
    case Settings::FB_OUTPUT:
    case Settings::FB_EVENTS:
    case Settings::FB_VOLUME:
    case Settings::AUTO_OFF:
    case Settings::LOW_BATT:
#if !defined(FURBLE_NO_DISPLAY)
    case Settings::DISPLAY_MODE:
#endif
      value = std::to_string(Settings::load<uint8_t>(setting.type));
      return true;

    case Settings::GPS_BAUD:
    case Settings::SCAN_TIMEOUT:
      value = std::to_string(Settings::load<uint32_t>(setting.type));
      return true;

    case Settings::GPX_PERIOD:
      value = std::to_string(Settings::load<uint16_t>(setting.type));
      return true;

    case Settings::THEME:
    case Settings::BUTTON_MODE:
      value = Settings::load<std::string>(setting.type);
      return true;

    case Settings::COMPANION_PASSWORD:
      return false;

    case Settings::GPS:
    case Settings::IMU:
    case Settings::GPS_NMEA:
    case Settings::MULTICONNECT:
    case Settings::RECONNECT:
    case Settings::RECON_BACKOFF:
    case Settings::COMPANION:
    case Settings::FAUXNY:
    case Settings::AUTOCONNECT:
    case Settings::SHOW_TITLE:
    case Settings::SLEEP_CONN:
    case Settings::PRESET_PICKER:
    case Settings::SD_GPX:
    case Settings::IR:
    case Settings::CONN_SAVER:
    case Settings::TX_ADAPTIVE:
    case Settings::BOOT_SPLASH:
    case Settings::BATTERY_SAVER:
    case Settings::AUTO_OFF_CHARGING:
#if defined(FURBLE_M5STICKS3)
    case Settings::WATCHDOG:
#endif
      value = Settings::load<bool>(setting.type) ? "true" : "false";
      return true;

    case Settings::INTERVAL:
    {
      const interval_t interval = Settings::load<interval_t>(setting.type);
      value = sizedHex(&interval, sizeof(interval));
      return true;
    }

    case Settings::BULB:
    {
      const SpinValue::nvs_t bulb = Settings::load<SpinValue::nvs_t>(setting.type);
      value = sizedHex(&bulb, sizeof(bulb));
      return true;
    }

    case Settings::TOUCH_CALIBRATION:
    {
      const Settings::calibration_t calibration =
          Settings::load<Settings::calibration_t>(setting.type);
      value = sizedHex(&calibration, sizeof(calibration));
      return true;
    }

    case Settings::MULTISELECT:
    {
      const Settings::multiselect_t selection =
          Settings::load<Settings::multiselect_t>(setting.type);
      value = sizedHex(&selection, sizeof(selection));
      return true;
    }
  }
  return false;
}

bool importSetting(const Settings::setting_t &setting, const std::string &text) {
  uint64_t value = 0;

  switch (setting.type) {
    case Settings::BRIGHTNESS:
      if (!parseUnsigned(text, 240, value) || (value < 32)) {
        return false;
      }
      Settings::save<uint8_t>(setting.type, static_cast<uint8_t>(value));
      return true;

    case Settings::INACTIVITY:
      if (!parseUnsigned(text, 20, value)) {
        return false;
      }
      Settings::save<uint8_t>(setting.type, static_cast<uint8_t>(value));
      return true;

    case Settings::DISPLAY_OFF:
    case Settings::GPS_POWER:
      if (!parseUnsigned(text, 2, value)) {
        return false;
      }
      Settings::save<uint8_t>(setting.type, static_cast<uint8_t>(value));
      return true;

    case Settings::GPS_DUTY:
      if (!parseUnsigned(text, 15, value)
          || ((value != 0) && (value != 5) && (value != 10) && (value != 15))) {
        return false;
      }
      Settings::save<uint8_t>(setting.type, static_cast<uint8_t>(value));
      return true;

    case Settings::TX_POWER:
    case Settings::BATT_STYLE:
      if (!parseUnsigned(text, 2, value)) {
        return false;
      }
      Settings::save<uint8_t>(setting.type, static_cast<uint8_t>(value));
      return true;

    case Settings::GPS_RATE:
      if (!parseUnsigned(text, 4, value)) {
        return false;
      }
      Settings::save<uint8_t>(setting.type, static_cast<uint8_t>(value));
      return true;

    case Settings::GPS_CONSTEL:
      if (!parseUnsigned(text, 7, value)) {
        return false;
      }
      Settings::save<uint8_t>(setting.type, static_cast<uint8_t>(value));
      return true;

    case Settings::GPS_ASSIST:
      if (!parseUnsigned(text, 2, value)) {
        return false;
      }
      Settings::save<uint8_t>(setting.type, static_cast<uint8_t>(value));
      return true;

    case Settings::AUTO_OFF:
    case Settings::LOW_BATT:
      if (!parseUnsigned(text, UINT8_MAX, value)) {
        return false;
      }
      Settings::save<uint8_t>(setting.type, static_cast<uint8_t>(value));
      return true;

    case Settings::CPU_FREQ:
      if (!parseUnsigned(text, 240, value) || ((value != 80) && (value != 160) && (value != 240))) {
        return false;
      }
      Settings::save<uint8_t>(setting.type, static_cast<uint8_t>(value));
      return true;

    case Settings::TEXT_SIZE:
      if (!parseUnsigned(text, Settings::TEXT_SIZE_LARGE, value)) {
        return false;
      }
      Settings::save<uint8_t>(setting.type, static_cast<uint8_t>(value));
      return true;

    case Settings::SCAN_MODE:
      if (!parseUnsigned(text, 2, value)) {
        return false;
      }
      Settings::save<uint8_t>(setting.type, static_cast<uint8_t>(value));
      return true;

#if !defined(FURBLE_NO_DISPLAY)
    case Settings::DISPLAY_MODE:
      if (!parseUnsigned(text, 1, value)) {
        return false;
      }
      Settings::save<uint8_t>(setting.type, static_cast<uint8_t>(value));
      return true;
#endif

    case Settings::FB_OUTPUT:
      if (!parseUnsigned(text, 4, value)) {
        return false;
      }
      Settings::save<uint8_t>(setting.type, static_cast<uint8_t>(value));
      return true;

    case Settings::IR_PROTO:
    case Settings::FB_EVENTS:
    case Settings::FB_VOLUME:
      if (!parseUnsigned(text, UINT8_MAX, value)) {
        return false;
      }
      Settings::save<uint8_t>(setting.type, static_cast<uint8_t>(value));
      return true;

    case Settings::GPS_BAUD:
      if (!parseUnsigned(text, 115200, value)
          || ((value != Settings::BAUD_9600) && (value != Settings::BAUD_115200))) {
        return false;
      }
      Settings::save<uint32_t>(setting.type, static_cast<uint32_t>(value));
      return true;

    case Settings::SCAN_TIMEOUT:
      if (!parseUnsigned(text, 120, value)
          || ((value != 0) && (value != 30) && (value != 60) && (value != 120))) {
        return false;
      }
      Settings::save<uint32_t>(setting.type, static_cast<uint32_t>(value));
      return true;

    case Settings::GPX_PERIOD:
      if (!parseUnsigned(text, Settings::GPX_PERIOD_MAX, value)
          || (value < Settings::GPX_PERIOD_MIN)) {
        return false;
      }
      Settings::save<uint16_t>(setting.type, static_cast<uint16_t>(value));
      return true;

    case Settings::THEME:
      if (!validTheme(text)) {
        return false;
      }
      Settings::save<std::string>(setting.type, text);
      return true;

    case Settings::BUTTON_MODE:
      if (!validButtonMode(text)) {
        return false;
      }
      Settings::save<std::string>(setting.type, text);
      return true;

    case Settings::COMPANION_PASSWORD:
      return false;

    case Settings::GPS:
    case Settings::IMU:
    case Settings::GPS_NMEA:
    case Settings::MULTICONNECT:
    case Settings::RECONNECT:
    case Settings::RECON_BACKOFF:
    case Settings::COMPANION:
    case Settings::FAUXNY:
    case Settings::AUTOCONNECT:
    case Settings::SHOW_TITLE:
    case Settings::SLEEP_CONN:
    case Settings::PRESET_PICKER:
    case Settings::SD_GPX:
    case Settings::IR:
    case Settings::CONN_SAVER:
    case Settings::TX_ADAPTIVE:
    case Settings::BOOT_SPLASH:
    case Settings::BATTERY_SAVER:
    case Settings::AUTO_OFF_CHARGING:
#if defined(FURBLE_M5STICKS3)
    case Settings::WATCHDOG:
#endif
    {
      bool enabled = false;
      if (!parseBool(text, enabled)) {
        return false;
      }
      Settings::save<bool>(setting.type, enabled);
      return true;
    }

    case Settings::INTERVAL:
    {
      interval_t interval = {};
      if (!decodeSizedHex(text, &interval, sizeof(interval)) || !validInterval(interval)) {
        return false;
      }
      Settings::save<interval_t>(setting.type, interval);
      return true;
    }

    case Settings::BULB:
    {
      SpinValue::nvs_t bulb = {};
      if (!decodeSizedHex(text, &bulb, sizeof(bulb)) || !validSpinValue(bulb, false)) {
        return false;
      }
      Settings::save<SpinValue::nvs_t>(setting.type, bulb);
      return true;
    }

    case Settings::TOUCH_CALIBRATION:
    {
      uint8_t bytes[sizeof(Settings::calibration_t)] = {};
      if (!decodeSizedHex(text, bytes, sizeof(bytes))
          || (bytes[offsetof(Settings::calibration_t, calibrated)] > 1)) {
        return false;
      }
      Settings::calibration_t calibration = {};
      memcpy(&calibration, bytes, sizeof(calibration));
      if (!validCalibration(calibration)) {
        return false;
      }
      Settings::save<Settings::calibration_t>(setting.type, calibration);
      return true;
    }

    case Settings::MULTISELECT:
    {
      Settings::multiselect_t selection = {};
      if (!decodeSizedHex(text, &selection, sizeof(selection))) {
        // A backup exported before MULTISELECT_NAME_MAX widened carries the old
        // record size. It is still valid data, so widen it rather than failing
        // the whole restore.
        Settings::multiselect_legacy_t legacy = {};
        if (!decodeSizedHex(text, &legacy, sizeof(legacy))) {
          return false;
        }
        selection = Settings::multiselectFromLegacy(legacy);
      }
      if (!validMultiselect(selection)) {
        return false;
      }
      Settings::save<Settings::multiselect_t>(setting.type, selection);
      return true;
    }
  }
  return false;
}

std::string trim(const std::string &text) {
  size_t first = 0;
  while ((first < text.size()) && std::isspace(static_cast<unsigned char>(text[first]))) {
    first++;
  }

  size_t last = text.size();
  while ((last > first) && std::isspace(static_cast<unsigned char>(text[last - 1]))) {
    last--;
  }
  return text.substr(first, last - first);
}

}  // namespace

SD &SD::getInstance(void) {
  static SD instance;
  return instance;
}

bool SD::isSupported(void) const {
  return m_Supported;
}

void SD::init(void) {
  auto &sd = getInstance();
  const int cs = static_cast<int>(M5.getPin(m5::pin_name_t::sd_spi_cs));
  sd.m_Supported = isCoreBoard() && (cs >= 0) && (cs != 255);

  if (!sd.m_Supported) {
    ESP_LOGI(LOG_TAG, "SD card storage is not supported on this board.");
    return;
  }

  ESP_LOGI(LOG_TAG, "SD card storage is supported, CS=%d.", cs);

  sd.m_Queue = xQueueCreate(WRITER_QUEUE_LENGTH, sizeof(message_t));
  sd.m_PowerOffDone = xSemaphoreCreateBinary();
  if ((sd.m_Queue == nullptr) || (sd.m_PowerOffDone == nullptr)
      || (xTaskCreate([](void *param) { static_cast<SD *>(param)->taskLoop(); }, "sd",
                      WRITER_STACK_BYTES, &sd, WRITER_PRIORITY, nullptr)
          != pdTRUE)) {
    ESP_LOGE(LOG_TAG, "Failed to create the SD writer task.");
    abort();
  }

  // apply the stored settings, mounts the card when logging is enabled
  sd.request(request_t::RELOAD);
}

bool SD::request(request_t request) {
  if (m_Queue == nullptr) {
    return false;
  }

  const message_t message = {request, {}};
  return xQueueSend(m_Queue, &message, pdMS_TO_TICKS(REQUEST_SEND_TIMEOUT_MS)) == pdTRUE;
}

bool SD::logPoint(const GPX::point_t &point) {
  if (m_Queue == nullptr) {
    return false;
  }

  const message_t message = {request_t::POINT, point};
  return xQueueSend(m_Queue, &message, 0) == pdTRUE;
}

void SD::powerOff(void) {
  if (!request(request_t::POWER_OFF)) {
    return;
  }
  xSemaphoreTake(m_PowerOffDone, pdMS_TO_TICKS(POWER_OFF_WAIT_MS));
}

SD::card_state_t SD::cardState(void) const {
  return static_cast<card_state_t>(m_CardState.load());
}

uint32_t SD::capacityMB(void) const {
  return m_CapacityMB.load();
}

uint32_t SD::freeMB(void) const {
  return m_FreeMB.load();
}

uint32_t SD::generation(void) const {
  return m_Generation.load();
}

void SD::taskLoop(void) {
  while (true) {
    message_t message;
    if (xQueueReceive(m_Queue, &message, portMAX_DELAY) == pdTRUE) {
      handleRequest(message);
    }
  }
}

void SD::handleRequest(const message_t &message) {
  auto &gpx = GPX::getInstance();

  switch (message.request) {
    case request_t::POINT:
      handlePoint(message.point);
      break;

    case request_t::CLOSE:
      gpx.close();
      maybeUnmount();
      publish();
      break;

    case request_t::MOUNT:
      m_UIHold = true;
      mount();
      publish();
      break;

    case request_t::PAGE_LEAVE:
      m_UIHold = false;
      maybeUnmount();
      publish();
      break;

    case request_t::RELOAD:
      handleReload();
      break;

    case request_t::EXPORT:
      handleTransfer(false);
      break;

    case request_t::IMPORT:
      handleTransfer(true);
      break;

    case request_t::POWER_OFF:
      gpx.close();
      unmount();
      publish();
      xSemaphoreGive(m_PowerOffDone);
      break;
  }
}

void SD::handlePoint(const GPX::point_t &point) {
  if (!m_LoggingEnabled) {
    return;
  }

  const bool wasMounted = m_Mounted;
  bool ok = mount();
  if (ok) {
    ok = GPX::getInstance().addPoint(point, m_PeriodSeconds);
    if (!ok) {
      // the card may be gone, force a fresh enumeration on the next attempt
      unmount();
    }
  }

  if (ok) {
    m_Failures = 0;
    if (!wasMounted) {
      publish();
    }
    return;
  }

  m_Failures++;
  if (m_Failures >= MAX_FAILURES) {
    m_LoggingEnabled = false;
    Settings::save<Settings::SD_GPX>(false);
    ESP_LOGE(LOG_TAG, "Disabling GPX logging after repeated SD failures.");
    maybeUnmount();
  }
  publish();
}

void SD::handleReload(void) {
  m_LoggingEnabled = Settings::load<Settings::SD_GPX>();
  m_PeriodSeconds = Settings::clampGPXPeriod(Settings::load<Settings::GPX_PERIOD>());

  if (m_LoggingEnabled) {
    m_Failures = 0;
    if (!mount()) {
      ESP_LOGW(LOG_TAG, "SD mount failed on logging reload.");
    }
  } else {
    GPX::getInstance().close();
    maybeUnmount();
  }
  publish();
}

void SD::handleTransfer(bool import) {
  // a running track is closed cleanly before the settings file is touched
  GPX::getInstance().close();

  const bool ok = import ? importSettings() : exportSettings();
  if (ok && import) {
    ESP_LOGI(LOG_TAG, "SD settings import complete, restarting.");
    unmount();
    Platform::getInstance().restart();
  }

  if (ok) {
    ESP_LOGI(LOG_TAG, "SD settings export complete.");
  } else {
    ESP_LOGE(LOG_TAG, "SD settings %s failed.", import ? "import" : "export");
  }
  maybeUnmount();
  publish();
}

bool SD::mount(void) {
  if (!m_Supported) {
    return false;
  }
  if (m_Mounted) {
    return true;
  }

  m_MountFailed = true;

  const int cs = static_cast<int>(M5.getPin(m5::pin_name_t::sd_spi_cs));
  auto *panel = M5.Display.getPanel();
  if ((cs < 0) || (cs == 255) || (panel == nullptr)) {
    ESP_LOGE(LOG_TAG, "SD card pins or display panel are unavailable.");
    return false;
  }

  auto *ibus = panel->getBus();
  if ((ibus == nullptr) || (ibus->busType() != lgfx::bus_spi)) {
    ESP_LOGE(LOG_TAG, "Display does not expose an SPI bus for SD sharing.");
    return false;
  }
  auto *bus = static_cast<lgfx::Bus_SPI *>(ibus);

  const auto bus_config = bus->config();
  sdmmc_host_t host = SDSPI_HOST_DEFAULT();
  host.slot = bus_config.spi_host;
  host.max_freq_khz = SD_SPI_FREQUENCY_KHZ;

  sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
  slot_config.gpio_cs = static_cast<gpio_num_t>(cs);
  slot_config.host_id = static_cast<spi_host_device_t>(host.slot);

  esp_vfs_fat_mount_config_t mount_config = {};
  mount_config.format_if_mount_failed = false;
  mount_config.max_files = 4;
  mount_config.allocation_unit_size = 16 * 1024;

  sdmmc_card_t *card = nullptr;
  const esp_err_t err =
      esp_vfs_fat_sdspi_mount(SD_MOUNT_POINT, &host, &slot_config, &mount_config, &card);
  if (err != ESP_OK) {
    ESP_LOGW(LOG_TAG, "SD card mount failed: %s.", esp_err_to_name(err));
    return false;
  }

  m_Card = card;
  m_Mounted = true;
  m_MountFailed = false;
  ESP_LOGI(LOG_TAG, "SD card mounted.");
  return true;
}

void SD::unmount(void) {
  if (!m_Mounted) {
    return;
  }

  const esp_err_t err = esp_vfs_fat_sdcard_unmount(SD_MOUNT_POINT, m_Card);
  if (err != ESP_OK) {
    ESP_LOGW(LOG_TAG, "SD card unmount failed: %s.", esp_err_to_name(err));
  }
  m_Card = nullptr;
  m_Mounted = false;
  m_MountFailed = false;
}

void SD::maybeUnmount(void) {
  if (m_Mounted && !m_UIHold && !m_LoggingEnabled && !GPX::getInstance().isOpen()) {
    unmount();
  }
}

void SD::publish(void) {
  card_state_t state = card_state_t::UNMOUNTED;
  uint32_t capacityMB = 0;
  uint32_t freeMB = 0;

  if (m_Mounted) {
    state = card_state_t::MOUNTED;
    if (m_Card != nullptr) {
      capacityMB = static_cast<uint32_t>(
          (static_cast<uint64_t>(m_Card->csd.capacity) * m_Card->csd.sector_size) >> 20);
    }

    uint64_t totalBytes = 0;
    uint64_t freeBytes = 0;
    if (esp_vfs_fat_info(SD_MOUNT_POINT, &totalBytes, &freeBytes) == ESP_OK) {
      freeMB = static_cast<uint32_t>(freeBytes >> 20);
    }
  } else if (m_MountFailed) {
    state = card_state_t::FAILED;
  }

  m_CardState.store(static_cast<uint8_t>(state));
  m_CapacityMB.store(capacityMB);
  m_FreeMB.store(freeMB);
  m_Generation.fetch_add(1);
}

bool SD::exportSettings(void) {
  if (!mount() || ((mkdir(SETTINGS_DIRECTORY, 0777) != 0) && (errno != EEXIST))) {
    ESP_LOGE(LOG_TAG, "Unable to prepare SD settings directory.");
    return false;
  }

  FILE *file = fopen(SETTINGS_FILE, "w");
  if (file == nullptr) {
    ESP_LOGE(LOG_TAG, "Unable to open settings export: %s.", strerror(errno));
    return false;
  }

  bool ok = (fprintf(file, "# furble settings v1\n") >= 0);
  for (const auto &entry : Settings::getAll()) {
    std::string value;
    if (!serializeSetting(entry.second, value) || (value.find('\n') != std::string::npos)
        || (fprintf(file, "%s=%s\n", entry.second.key, value.c_str()) < 0)) {
      ok = false;
      ESP_LOGE(LOG_TAG, "Unable to serialize setting %s.", entry.second.key);
      break;
    }
  }

  if ((fflush(file) != 0) || (fsync(fileno(file)) != 0)) {
    ok = false;
    ESP_LOGE(LOG_TAG, "Unable to flush settings export: %s.", strerror(errno));
  }
  if (fclose(file) != 0) {
    ok = false;
  }

  if (ok) {
    ESP_LOGI(LOG_TAG, "Settings exported to %s.", SETTINGS_FILE);
  }
  return ok;
}

bool SD::importSettings(void) {
  if (!mount()) {
    return false;
  }

  FILE *file = fopen(SETTINGS_FILE, "r");
  if (file == nullptr) {
    ESP_LOGE(LOG_TAG, "Unable to open settings import: %s.", strerror(errno));
    return false;
  }

  bool ok = true;
  std::string line;
  char buffer[512];
  while (fgets(buffer, sizeof(buffer), file) != nullptr) {
    line = buffer;
    line = trim(line);
    if (line.empty() || (line[0] == '#')) {
      continue;
    }

    const size_t separator = line.find('=');
    if (separator == std::string::npos) {
      ESP_LOGW(LOG_TAG, "Skipping malformed settings line.");
      ok = false;
      continue;
    }

    const std::string key = trim(line.substr(0, separator));
    const std::string value = trim(line.substr(separator + 1));
    const Settings::setting_t *setting = nullptr;
    for (const auto &entry : Settings::getAll()) {
      if (key == entry.second.key) {
        setting = &entry.second;
        break;
      }
    }

    if (setting == nullptr) {
      ESP_LOGW(LOG_TAG, "Skipping unknown setting %s.", key.c_str());
      continue;
    }
    if (!importSetting(*setting, value)) {
      ESP_LOGW(LOG_TAG, "Rejecting invalid value for setting %s.", key.c_str());
      ok = false;
    }
  }

  const bool readError = ferror(file) != 0;
  const bool closeError = fclose(file) != 0;
  if (readError || closeError) {
    ok = false;
  }
  return ok;
}

}  // namespace Furble
