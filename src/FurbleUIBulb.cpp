#include <lvgl.h>

#include "FurbleSettings.h"
#include "FurbleUI.h"

namespace Furble {

UI::Bulb::Bulb(const SpinValue::nvs_t &duration) : m_Duration(this, duration) {}

void UI::Bulb::save(void) {
  Settings::save<Settings::BULB>(m_Duration.m_SpinValue.toNVS());
}

}  // namespace Furble
