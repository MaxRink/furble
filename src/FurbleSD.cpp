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
#include <esp_vfs_fat.h>

#include <sys/stat.h>
#include <unistd.h>

#include "FurbleSD.h"
#include "FurbleSettings.h"
#include "FurbleTypes.h"

namespace Furble {
namespace {

constexpr const char *SD_MOUNT_POINT = "/sd";
constexpr const char *SETTINGS_DIRECTORY = "/sd/furble";
constexpr const char *SETTINGS_FILE = "/sd/furble/settings.txt";
constexpr uint32_t SD_SPI_FREQUENCY_KHZ = 10000;

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
    case Settings::CPU_FREQ:
    case Settings::BATT_STYLE:
    case Settings::SCAN_MODE:
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
      value = Settings::load<std::string>(setting.type);
      return true;

    case Settings::GPS:
    case Settings::GPS_NMEA:
    case Settings::MULTICONNECT:
    case Settings::RECONNECT:
    case Settings::RECON_BACKOFF:
    case Settings::COMPANION:
    case Settings::FAUXNY:
    case Settings::AUTOCONNECT:
    case Settings::SHOW_TITLE:
    case Settings::SLEEP_CONN:
    case Settings::SD_GPX:
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

    case Settings::CPU_FREQ:
      if (!parseUnsigned(text, 240, value) || ((value != 80) && (value != 160) && (value != 240))) {
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
      if (!parseUnsigned(text, 60, value) || (value < 1)) {
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

    case Settings::GPS:
    case Settings::GPS_NMEA:
    case Settings::MULTICONNECT:
    case Settings::RECONNECT:
    case Settings::RECON_BACKOFF:
    case Settings::COMPANION:
    case Settings::FAUXNY:
    case Settings::AUTOCONNECT:
    case Settings::SHOW_TITLE:
    case Settings::SLEEP_CONN:
    case Settings::SD_GPX:
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
  if (Settings::load<Settings::SD_GPX>()) {
    sd.mount();
  }
}

bool SD::mount(void) {
  if (!m_Supported) {
    return false;
  }
  if (m_Mounted) {
    return true;
  }

  const int cs = static_cast<int>(M5.getPin(m5::pin_name_t::sd_spi_cs));
  auto *panel = M5.Display.getPanel();
  if ((cs < 0) || (cs == 255) || (panel == nullptr)) {
    ESP_LOGE(LOG_TAG, "SD card pins or display panel are unavailable.");
    return false;
  }

  auto *bus = static_cast<lgfx::Bus_SPI *>(panel->getBus());
  if (bus == nullptr) {
    ESP_LOGE(LOG_TAG, "Display does not expose an SPI bus for SD sharing.");
    return false;
  }

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
}

bool SD::isMounted(void) const {
  return m_Mounted;
}

uint64_t SD::capacityBytes(void) const {
  if ((m_Card == nullptr) || !m_Mounted) {
    return 0;
  }
  return static_cast<uint64_t>(m_Card->csd.capacity) * m_Card->csd.sector_size;
}

uint64_t SD::freeBytes(void) const {
  if (!m_Mounted) {
    return 0;
  }

  uint64_t totalBytes = 0;
  uint64_t freeBytes = 0;
  if (esp_vfs_fat_info(SD_MOUNT_POINT, &totalBytes, &freeBytes) != ESP_OK) {
    return 0;
  }
  return freeBytes;
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
