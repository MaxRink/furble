#include <lvgl.h>

#include "FurbleUI.h"

namespace Furble {

UI::Intervalometer::Intervalometer(const interval_t &interval)
    : m_State {STATE_IDLE},
      m_Count(this, interval.count, true),
      m_Delay(this, interval.delay),
      m_Shutter(this, interval.shutter),
      m_Wait(this, interval.wait) {}

void UI::Intervalometer::save(void) {
  interval_t interval = {m_Count.m_SpinValue.toNVS(), m_Delay.m_SpinValue.toNVS(),
                         m_Shutter.m_SpinValue.toNVS(), m_Wait.m_SpinValue.toNVS()};
  Settings::save<Settings::INTERVAL>(interval);
}

void UI::Intervalometer::Spinner::update(void) {
  if (lv_obj_has_state(m_SwitchInfinite, LV_STATE_CHECKED)) {
    m_SpinValue.m_Unit = SpinValue::UNIT_INF;
    lv_obj_add_flag(m_RowSpinners, LV_OBJ_FLAG_HIDDEN);
  } else {
    m_SpinValue.m_Unit = SpinValue::UNIT_NIL;
    lv_obj_clear_flag(m_RowSpinners, LV_OBJ_FLAG_HIDDEN);

    uint32_t h = lv_roller_get_selected(m_Roller[0]);
    uint32_t t = lv_roller_get_selected(m_Roller[1]);
    uint32_t u = lv_roller_get_selected(m_Roller[2]);

    m_SpinValue.m_Value = (h * 100) + (t * 10) + u;
  }

  if (m_RollerUnit != nullptr) {
    uint32_t u = lv_roller_get_selected(m_RollerUnit);
    switch (u) {
      case 0:
        m_SpinValue.m_Unit = SpinValue::UNIT_MS;
        break;
      case 1:
        m_SpinValue.m_Unit = SpinValue::UNIT_SEC;
        break;
      case 2:
        m_SpinValue.m_Unit = SpinValue::UNIT_MIN;
        break;
    }
  }
  updateLabels();

  m_Owner->save();
}

size_t UI::Intervalometer::Spinner::nearestPreset(uint32_t milliseconds) {
  size_t nearest = 0;
  uint32_t distance = milliseconds > m_ExposurePresetMilliseconds[0]
                          ? milliseconds - m_ExposurePresetMilliseconds[0]
                          : m_ExposurePresetMilliseconds[0] - milliseconds;

  for (size_t i = 1; i < m_ExposurePresetMilliseconds.size(); i++) {
    uint32_t candidate = m_ExposurePresetMilliseconds[i];
    uint32_t candidateDistance =
        milliseconds > candidate ? milliseconds - candidate : candidate - milliseconds;
    if (candidateDistance < distance) {
      nearest = i;
      distance = candidateDistance;
    }
  }

  return nearest;
}

SpinValue::nvs_t UI::Intervalometer::Spinner::presetNVS(size_t index) {
  uint32_t milliseconds = m_ExposurePresetMilliseconds[index];
  if ((milliseconds % 1000) == 0) {
    return SpinValue::nvs_t {static_cast<uint16_t>(milliseconds / 1000), SpinValue::UNIT_SEC};
  }
  return SpinValue::nvs_t {static_cast<uint16_t>(milliseconds), SpinValue::UNIT_MS};
}

void UI::Intervalometer::Spinner::snapToDigits(void) {
  uint32_t milliseconds = m_SpinValue.toMilliseconds();
  if (milliseconds < 1000) {
    m_SpinValue.m_Value = static_cast<uint16_t>(milliseconds);
    m_SpinValue.m_Unit = SpinValue::UNIT_MS;
    return;
  }

  uint32_t seconds = (milliseconds + 500) / 1000;
  if (seconds <= 999) {
    m_SpinValue.m_Value = static_cast<uint16_t>(seconds);
    m_SpinValue.m_Unit = SpinValue::UNIT_SEC;
    return;
  }

  // Above 999 s pick the nearer of 999 s and the rounded whole minute. The
  // 1000 s series entry lands on 999 s, not 17 min.
  uint32_t minutes = (seconds + 30) / 60;
  if (minutes > 999) {
    minutes = 999;
  }
  uint32_t secDistance = milliseconds - 999000;
  uint32_t minMilliseconds = minutes * 60000;
  uint32_t minDistance = milliseconds > minMilliseconds ? milliseconds - minMilliseconds
                                                        : minMilliseconds - milliseconds;
  if (secDistance <= minDistance) {
    m_SpinValue.m_Value = 999;
    m_SpinValue.m_Unit = SpinValue::UNIT_SEC;
  } else {
    m_SpinValue.m_Value = static_cast<uint16_t>(minutes);
    m_SpinValue.m_Unit = SpinValue::UNIT_MIN;
  }
}

void UI::Intervalometer::Spinner::setPresetPicker(bool enabled) {
  if (!m_PresetSupported || (m_PresetPicker == enabled)) {
    return;
  }

  m_PresetPicker = enabled;
  if (enabled) {
    // Track the nearest series entry for stepping. The stored value is not
    // modified or saved until the user steps in the picker.
    m_PresetIndex = nearestPreset(m_SpinValue.toMilliseconds());
  }

  updatePresetPickerVisibility();
  updateLabels();
}

void UI::Intervalometer::Spinner::stepPreset(int direction) {
  if (!m_PresetPicker || (direction == 0)) {
    return;
  }

  int next = static_cast<int>(m_PresetIndex) + (direction > 0 ? 1 : -1);
  if ((next < 0) || (next >= static_cast<int>(m_ExposurePresetMilliseconds.size()))) {
    return;
  }

  m_PresetIndex = static_cast<size_t>(next);
  SpinValue::nvs_t nvs = presetNVS(m_PresetIndex);
  m_SpinValue.m_Value = nvs.value;
  m_SpinValue.m_Unit = nvs.unit;
  updateLabels();
  m_Owner->save();
}

void UI::Intervalometer::Spinner::updatePresetPickerVisibility(void) {
  if (!m_PresetSupported || (m_RowSpinners == nullptr) || (m_PresetRow == nullptr)) {
    return;
  }

  // The rollers stay in their group. LVGL 9 group navigation skips objects
  // with a hidden ancestor, and removing plus re-adding would scramble the
  // traversal order because lv_group_add_obj appends to the tail.
  if (m_PresetPicker) {
    lv_obj_add_flag(m_RowSpinners, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(m_PresetRow, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_clear_flag(m_RowSpinners, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(m_PresetRow, LV_OBJ_FLAG_HIDDEN);
  }
}

void UI::Intervalometer::Spinner::updateLabels(void) {
  // In digit mode the rollers can only show values 0-999. A preset value like
  // 1.3 s is stored as 1300 ms, so snap it to the nearest representable value
  // before driving the rollers. Not persisted, saving happens when the user
  // next changes the value.
  if (!m_PresetPicker && (m_SpinValue.m_Value > 999)
      && ((m_SpinValue.m_Unit == SpinValue::UNIT_MS) || (m_SpinValue.m_Unit == SpinValue::UNIT_SEC)
          || (m_SpinValue.m_Unit == SpinValue::UNIT_MIN))) {
    snapToDigits();
  }

  switch (m_SpinValue.m_Unit) {
    case SpinValue::UNIT_INF:
      lv_label_set_text_fmt(m_Value, "Infinite");
      break;

    case SpinValue::UNIT_NIL:
      lv_label_set_text_fmt(m_Value, "%u", m_SpinValue.m_Value);
      break;

    default:
      // The narrow panels share one row between the name and the value, so the
      // value uses the short unit and never gives up a digit for it. The
      // 320x240 grid has the width for the spelled out unit.
      // See plans/168-notouch-layout-overflows.md.
#if defined(FURBLE_M5COREX)
      lv_label_set_text_fmt(m_Value, "%u %s", m_SpinValue.m_Value, m_SpinValue.getUnitString());
#else
      lv_label_set_text_fmt(m_Value, "%u %s", m_SpinValue.m_Value,
                            m_SpinValue.getShortUnitString());
#endif
      break;
  }

  if (m_PresetPicker) {
    if (m_PresetValue != nullptr) {
      // Show the actual stored value. It equals a series entry after stepping,
      // but until the first step it may sit between entries.
      uint32_t milliseconds = m_SpinValue.toMilliseconds();
      if ((milliseconds % 1000) == 0) {
        lv_label_set_text_fmt(m_PresetValue, "%lu secs",
                              static_cast<unsigned long>(milliseconds / 1000));
      } else {
        lv_label_set_text_fmt(m_PresetValue, "%lu.%lu secs",
                              static_cast<unsigned long>(milliseconds / 1000),
                              static_cast<unsigned long>((milliseconds % 1000) / 100));
      }
    }
    return;
  }

  if (m_SpinValue.m_Unit != SpinValue::UNIT_INF) {
    // Update rollers
    uint32_t h = m_SpinValue.m_Value / 100;
    uint32_t t = (m_SpinValue.m_Value % 100) / 10;
    uint32_t u = (m_SpinValue.m_Value % 10);

    lv_roller_set_selected(m_Roller[0], h, LV_ANIM_ON);
    lv_roller_set_selected(m_Roller[1], t, LV_ANIM_ON);
    lv_roller_set_selected(m_Roller[2], u, LV_ANIM_ON);

    if (m_SpinValue.m_Unit != SpinValue::UNIT_NIL) {
      uint32_t i = 0;
      switch (m_SpinValue.m_Unit) {
        case SpinValue::UNIT_MS:
          i = 0;
          break;
        case SpinValue::UNIT_SEC:
          i = 1;
          break;
        case SpinValue::UNIT_MIN:
          i = 2;
          break;
        default:
          break;
      }

      lv_roller_set_selected(m_RollerUnit, i, LV_ANIM_ON);
    }
  }
}

}  // namespace Furble
