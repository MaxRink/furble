#include "FurbleProvision.h"

#include <cstring>

#include "FurbleSettings.h"
#include "interval.h"

namespace Furble {
namespace Provision {

namespace {

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

static_assert(sizeof(interval_wire_t) == 12, "provision interval wire must stay 12 bytes");

bool unpackInterval(const std::vector<uint8_t> &value, interval_t &interval) {
  if (value.size() != sizeof(interval_wire_t)) {
    return false;
  }

  interval_wire_t wire = {};
  std::memcpy(&wire, value.data(), sizeof(wire));
  for (const auto &field : {wire.count, wire.delay, wire.shutter, wire.wait}) {
    if (field.unit > SpinValue::UNIT_MIN) {
      return false;
    }
  }

  interval.count = {wire.count.value, static_cast<SpinValue::unit_t>(wire.count.unit)};
  interval.delay = {wire.delay.value, static_cast<SpinValue::unit_t>(wire.delay.unit)};
  interval.shutter = {wire.shutter.value, static_cast<SpinValue::unit_t>(wire.shutter.unit)};
  interval.wait = {wire.wait.value, static_cast<SpinValue::unit_t>(wire.wait.unit)};
  return true;
}

ProvisionTLV::ValueType runtimeType(Settings::type_t type) {
  switch (type) {
    case Settings::GPS:
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
    case Settings::IMU:
#if defined(FURBLE_M5STICKS3)
    case Settings::WATCHDOG:
#endif
      return ProvisionTLV::ValueType::BOOL;

    case Settings::BRIGHTNESS:
    case Settings::INACTIVITY:
    case Settings::DISPLAY_OFF:
    case Settings::TX_POWER:
    case Settings::GPS_RATE:
    case Settings::GPS_CONSTEL:
    case Settings::GPS_POWER:
    case Settings::GPS_DUTY:
    case Settings::GPS_ASSIST:
    case Settings::GPS_PLATFORM:
    case Settings::IR_PROTO:
    case Settings::FB_OUTPUT:
    case Settings::FB_EVENTS:
    case Settings::FB_VOLUME:
    case Settings::CPU_FREQ:
    case Settings::BATT_STYLE:
    case Settings::SCAN_MODE:
    case Settings::TEXT_SIZE:
    case Settings::AUTO_OFF:
    case Settings::LOW_BATT:
#if !defined(FURBLE_NO_DISPLAY)
    case Settings::DISPLAY_MODE:
#endif
      return ProvisionTLV::ValueType::U8;

    case Settings::GPS_BAUD:
    case Settings::SCAN_TIMEOUT:
      return ProvisionTLV::ValueType::U32;

    case Settings::THEME:
    case Settings::BUTTON_MODE:
      return ProvisionTLV::ValueType::STRING;

    case Settings::INTERVAL:
      return ProvisionTLV::ValueType::BLOB;

    case Settings::MULTISELECT:
    case Settings::TOUCH_CALIBRATION:
    case Settings::BULB:
    case Settings::GPX_PERIOD:
      return ProvisionTLV::ValueType::BLOB;
  }
  return ProvisionTLV::ValueType::BLOB;
}

uint32_t littleEndianU32(const std::vector<uint8_t> &value) {
  return static_cast<uint32_t>(value[0]) | (static_cast<uint32_t>(value[1]) << 8)
         | (static_cast<uint32_t>(value[2]) << 16) | (static_cast<uint32_t>(value[3]) << 24);
}

bool validateSetting(const ProvisionTLV::SettingValue &field,
                     const Settings::setting_t &setting,
                     ApplyReport &report) {
  const auto *schema = ProvisionTLV::schemaForSetting(field.wireId);
  if ((schema == nullptr) || (field.type != schema->type)
      || (runtimeType(setting.type) != field.type)) {
    report.error = ApplyError::UNSUPPORTED_SETTING;
    report.failedSettingId = field.wireId;
    report.message = "setting type is not supported";
    return false;
  }
  if ((field.value.size() < schema->minLength) || (field.value.size() > schema->maxLength)) {
    report.error = ApplyError::BAD_SETTING;
    report.failedSettingId = field.wireId;
    report.message = "setting length is invalid";
    return false;
  }

  switch (field.type) {
    case ProvisionTLV::ValueType::BOOL:
      if (field.value[0] > 1) {
        report.error = ApplyError::BAD_SETTING;
        report.failedSettingId = field.wireId;
        report.message = "boolean setting must be 0 or 1";
        return false;
      }
      break;
    case ProvisionTLV::ValueType::U8:
      if ((setting.type == Settings::GPS_ASSIST) && (field.value[0] > 2)) {
        report.error = ApplyError::BAD_SETTING;
        report.failedSettingId = field.wireId;
        report.message = "GPS assistance setting must be 0, 1 or 2";
        return false;
      }
      if ((setting.type == Settings::GPS_PLATFORM) && (field.value[0] > 4)) {
        report.error = ApplyError::BAD_SETTING;
        report.failedSettingId = field.wireId;
        report.message = "GPS platform setting must be 0 through 4";
        return false;
      }
      if ((setting.type == Settings::TEXT_SIZE) && (field.value[0] > Settings::TEXT_SIZE_LARGE)) {
        report.error = ApplyError::BAD_SETTING;
        report.failedSettingId = field.wireId;
        report.message = "text size setting is out of range";
        return false;
      }
      if ((setting.type == Settings::FB_OUTPUT) && (field.value[0] > 4)) {
        report.error = ApplyError::BAD_SETTING;
        report.failedSettingId = field.wireId;
        report.message = "feedback output is out of range";
        return false;
      }
      if (setting.type == Settings::GPS_DUTY) {
        const uint8_t duty = field.value[0];
        if ((duty != 0) && (duty != 5) && (duty != 10) && (duty != 15)) {
          report.error = ApplyError::BAD_SETTING;
          report.failedSettingId = field.wireId;
          report.message = "GPS duty must be 0, 5, 10 or 15";
          return false;
        }
      }
#if !defined(FURBLE_NO_DISPLAY)
      if ((setting.type == Settings::DISPLAY_MODE) && (field.value[0] > Settings::CONSOLE)) {
        report.error = ApplyError::BAD_SETTING;
        report.failedSettingId = field.wireId;
        report.message = "display mode is out of range";
        return false;
      }
#endif
      break;
    case ProvisionTLV::ValueType::U32:
      if ((setting.type == Settings::GPS_BAUD)
          && (littleEndianU32(field.value) != Settings::BAUD_AUTO)
          && (littleEndianU32(field.value) != Settings::BAUD_9600)
          && (littleEndianU32(field.value) != Settings::BAUD_115200)) {
        report.error = ApplyError::BAD_SETTING;
        report.failedSettingId = field.wireId;
        report.message = "GPS baud must be auto, 9600 or 115200";
        return false;
      }
      break;
    case ProvisionTLV::ValueType::STRING:
    {
      const std::string value(field.value.begin(), field.value.end());
      if ((setting.type == Settings::BUTTON_MODE)
          && (value != Settings::BUTTON_MODE_TWO_BUTTON_VALUE)
          && (value != Settings::BUTTON_MODE_ONE_BUTTON_VALUE)) {
        report.error = ApplyError::BAD_SETTING;
        report.failedSettingId = field.wireId;
        report.message = "button mode is not recognised";
        return false;
      }
    } break;
    case ProvisionTLV::ValueType::BLOB:
      if ((setting.type != Settings::INTERVAL)) {
        report.error = ApplyError::UNSUPPORTED_SETTING;
        report.failedSettingId = field.wireId;
        report.message = "blob setting has no write path";
        return false;
      }
      {
        interval_t interval = {};
        if (!unpackInterval(field.value, interval)) {
          report.error = ApplyError::BAD_SETTING;
          report.failedSettingId = field.wireId;
          report.message = "interval value is malformed";
          return false;
        }
      }
      break;
  }
  return true;
}

void saveSetting(const ProvisionTLV::SettingValue &field, Settings::type_t type) {
  switch (field.type) {
    case ProvisionTLV::ValueType::BOOL:
      Settings::save<bool>(type, field.value[0] != 0);
      break;
    case ProvisionTLV::ValueType::U8:
      Settings::save<uint8_t>(type, field.value[0]);
      break;
    case ProvisionTLV::ValueType::U32:
      Settings::save<uint32_t>(type, littleEndianU32(field.value));
      break;
    case ProvisionTLV::ValueType::STRING:
      Settings::save<std::string>(type, std::string(field.value.begin(), field.value.end()));
      break;
    case ProvisionTLV::ValueType::BLOB:
    {
      interval_t interval = {};
      if (unpackInterval(field.value, interval)) {
        Settings::save<interval_t>(type, interval);
      }
    } break;
  }
}

size_t deferredFieldCount(const ProvisionTLV::ProvisionBundle &bundle) {
  // TODO(#53): persist/apply these network fields when the WiFi/MQTT backend
  // lands. Keeping them in the validated bundle lets the flasher protocol
  // land independently of association code.
  // TODO(#116): connect companionPassword to the companion secret store when
  // that backend lands; it is deliberately never echoed by the console.
  size_t count = 0;
  count += bundle.wifiSsid.has_value() ? 1 : 0;
  count += bundle.wifiPsk.has_value() ? 1 : 0;
  count += bundle.companionPassword.has_value() ? 1 : 0;
  count += bundle.mqttUri.has_value() ? 1 : 0;
  count += bundle.mqttUsername.has_value() ? 1 : 0;
  count += bundle.mqttPassword.has_value() ? 1 : 0;
  count += bundle.mqttBaseTopic.has_value() ? 1 : 0;
  return count;
}

}  // namespace

const char *applyErrorString(ApplyError error) {
  switch (error) {
    case ApplyError::NONE:
      return "ok";
    case ApplyError::UNKNOWN_SETTING_ID:
      return "unknown setting id";
    case ApplyError::BAD_SETTING:
      return "bad setting";
    case ApplyError::UNSUPPORTED_SETTING:
      return "unsupported setting";
  }
  return "unknown apply error";
}

bool apply(const ProvisionTLV::ProvisionBundle &bundle,
           ApplyReport &report,
           const ApplyOptions &options) {
  report = {};
  report.deferredFields = deferredFieldCount(bundle);

  // Validate the complete batch before the first NVS write. Settings::save()
  // itself is void, so this preflight is what prevents a later bad record from
  // leaving an earlier record half-applied.
  for (const auto &field : bundle.settings) {
    const Settings::setting_t *setting = Settings::getByWireId(field.wireId);
    if (setting == nullptr) {
      report.error = ApplyError::UNKNOWN_SETTING_ID;
      report.failedSettingId = field.wireId;
      report.message = "setting id is not present in this build";
      return false;
    }
    if (!validateSetting(field, *setting, report)) {
      return false;
    }
  }

  for (const auto &field : bundle.settings) {
    const Settings::setting_t *setting = Settings::getByWireId(field.wireId);
    // The same lookup was checked above; keeping this guard makes the write
    // loop robust if the settings table ever becomes mutable.
    if (setting == nullptr) {
      report.error = ApplyError::UNKNOWN_SETTING_ID;
      report.failedSettingId = field.wireId;
      report.message = "setting id disappeared during apply";
      report.settingsApplied = 0;
      return false;
    }
    saveSetting(field, setting->type);
    report.settingsApplied++;
    if (options.onSettingApplied != nullptr) {
      options.onSettingApplied(field.wireId);
    }
  }

  report.ok = true;
  return true;
}

}  // namespace Provision
}  // namespace Furble
