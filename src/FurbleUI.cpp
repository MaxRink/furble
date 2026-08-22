#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <numeric>
#include <string>
#include <tuple>

#include <M5Unified.h>
#include <esp_chip_info.h>
#include <esp_flash.h>
#include <esp_idf_version.h>
#include <esp_sleep.h>
#include <esp_system.h>
#include <lvgl.h>
#if defined(FURBLE_CONSOLE)
#include <lvgl_private.h>
#endif
#include <src/themes/lv_theme_private.h>

#include <Device.h>
#include <Scan.h>

#include "icons.h"

#include "FurbleBootScreen.h"
#include "FurbleCalibrate.h"
#include "FurbleCompanion.h"
#include "FurbleControl.h"
#include "FurbleFeedback.h"
#include "FurbleGPS.h"
#include "FurbleIR.h"
#include "FurblePlatform.h"
#include "FurblePower.h"
#include "FurbleSD.h"
#include "FurbleSettings.h"
#include "FurbleUI.h"
#include "interval.h"

#if defined(FURBLE_SIM)
#include "power_profiler.h"
#define FURBLE_SIM_TIMER_FIRE(name) Furble::Sim::profilerTimerFire(name)

namespace {
// Registry of settings switches keyed by Settings::type_t. Scripted scenarios
// toggle a setting through its real widget and persistence callback.
std::unordered_map<int, lv_obj_t *> g_simSettingSwitches;

// Counts doDisconnect() calls so a scenario can prove exactly one disconnect
// fires per Cancel click regardless of how many connect attempts preceded it.
uint32_t g_simDisconnectCalls = 0;
}  // namespace
#else
#define FURBLE_SIM_TIMER_FIRE(name) ((void)0)
#endif
#if defined(FURBLE_CONSOLE)
#include "FurbleUIAudit.h"
#endif

#if defined(FURBLE_M5STICKC) || defined(FURBLE_M5STICKC_PLUS) || defined(FURBLE_M5STICKS3)
// Use 24x24 icons for StickC screens
#define icon_add_a_photo icon_add_a_photo_24
#define icon_bluetooth icon_bluetooth_24
#define icon_cell_tower icon_cell_tower_24
#define icon_delete icon_delete_24
#define icon_info icon_info_24
#define icon_linked_camera icon_linked_camera_24
#define icon_notifications_active icon_notifications_active_24
#define icon_location_searching icon_location_searching_24
#define icon_no_photography icon_no_photography_24
#define icon_palette icon_palette_24
#define icon_power_settings_new icon_power_settings_new_24
#define icon_settings icon_settings_24
#define icon_settings_brightness icon_settings_brightness_24
#define icon_settings_remote icon_settings_remote_24
#define icon_timer icon_timer_24
#define icon_troubleshoot icon_troubleshoot_24
#define icon_wand_stars icon_wand_stars_24
#endif

namespace Furble {

namespace {
void setLabelTextIfChanged(lv_obj_t *label, const char *text) {
  const char *current = lv_label_get_text(label);
  if ((current == nullptr) || (std::strcmp(current, text) != 0)) {
    lv_label_set_text(label, text);
  }
}

template <typename... Args>
void setLabelTextFmtIfChanged(lv_obj_t *label, const char *format, Args... args) {
  char text[96];
  std::snprintf(text, sizeof(text), format, args...);
  setLabelTextIfChanged(label, text);
}

uint32_t gpsDutyIndex(uint8_t seconds) {
  for (size_t i = 0; i < GPS::DUTY_SECONDS.size(); i++) {
    if (GPS::DUTY_SECONDS[i] == seconds) {
      return i;
    }
  }
  return 0;
}

void addToInputGroup(lv_group_t *group, lv_obj_t *obj) {
  if ((group != nullptr) && (obj != nullptr) && (lv_obj_get_group(obj) != group)) {
    lv_group_add_obj(group, obj);
  }
}

void setLabelIfChanged(lv_obj_t *label, const char *text) {
  if ((label != nullptr) && std::strcmp(lv_label_get_text(label), text)) {
    lv_label_set_text(label, text);
  }
}

// Show or hide a status-row icon, but only when its visibility actually
// changes. A 64x64 icon costs a decompress on every draw, so toggling the
// hidden flag every tick would needlessly invalidate and redraw it. The guard
// keeps periodic callers within the LVGL redraw discipline.
void showStatusIcon(lv_obj_t *icon, bool show) {
  if (icon == nullptr) {
    return;
  }
  const bool hidden = lv_obj_has_flag(icon, LV_OBJ_FLAG_HIDDEN);
  if (show && hidden) {
    lv_obj_clear_flag(icon, LV_OBJ_FLAG_HIDDEN);
  } else if (!show && !hidden) {
    lv_obj_add_flag(icon, LV_OBJ_FLAG_HIDDEN);
  }
}

template <typename... Args>
void setLabelIfChangedFmt(lv_obj_t *label, const char *format, Args... args) {
  char text[128];
  std::snprintf(text, sizeof(text), format, args...);
  setLabelIfChanged(label, text);
}
const lv_font_t *fontForTextSize(uint8_t textSize) {
  // Clamp to the board maximum so a value stored past this board's limit (for
  // example Large carried over from a larger board's NVS, or forced through the
  // console) can never select a font that overflows the panel.
  textSize = TextSizePolicy::clamp(textSize);
  switch (textSize) {
    case Settings::TEXT_SIZE_SMALL:
#if defined(FURBLE_M5STICKC)
      return &lv_font_montserrat_10;
#elif defined(FURBLE_M5STICKC_PLUS) || defined(FURBLE_M5STICKS3)
      return &lv_font_montserrat_12;
#else
      return &lv_font_montserrat_16;
#endif

    case Settings::TEXT_SIZE_LARGE:
#if defined(FURBLE_M5STICKC)
      return &lv_font_montserrat_14;
#elif defined(FURBLE_M5STICKC_PLUS) || defined(FURBLE_M5STICKS3)
      return &lv_font_montserrat_22;
#else
      return &lv_font_montserrat_28;
#endif

    case Settings::TEXT_SIZE_NORMAL:
    default:
      return LV_FONT_DEFAULT;
  }
}

const lv_font_t *fontForIconMenu(uint8_t textSize) {
  // Large may only grow the icon menu font, never shrink it below the default.
  const lv_font_t *base = &lv_font_montserrat_16;
  if (textSize == Settings::TEXT_SIZE_LARGE) {
    const lv_font_t *large = fontForTextSize(textSize);
    if (lv_font_get_line_height(large) > lv_font_get_line_height(base)) {
      return large;
    }
  }
  return base;
}

// LVGL 9.4 has no public long press time getter. This is its default.
constexpr uint32_t BUTTON_MODE_CLICK_WINDOW_MS = 400;

}  // namespace

static lv_obj_t *addRollerItem(lv_obj_t *page, const char *text, const char *options);

std::mutex UI::m_Mutex;

UI::ConnectContext_t UI::m_ConnectContext;

lv_obj_t *UI::m_ScanFinished;

lv_timer_t *UI::m_ConnectTimer;

lv_timer_t *UI::m_GPSDataTimer;

lv_timer_t *UI::m_CamerasTimer;

lv_timer_t *UI::m_IntervalPageRefresh;
uint32_t UI::m_IntervalNext;
std::atomic<uint8_t> UI::m_IntervalometerState {0};
std::atomic<uint16_t> UI::m_IntervalometerRemaining {0};
bool UI::m_IntervalCountdownActive;
uint8_t UI::m_IntervalLastAnnouncedSecond;

lv_timer_t *UI::m_BulbTimer;
lv_timer_t *UI::m_BulbPageRefresh;
uint32_t UI::m_BulbEnd;

UI::menu_t UI::m_MainMenu;

std::unordered_map<const char *, UI::menu_t> UI::m_Menu = {
    {m_ConnectStr,           {nullptr, nullptr, nullptr, nullptr, {0, 0}}},
    {m_ScanStr,              {nullptr, nullptr, nullptr, nullptr, {1, 0}}},
    {m_DeleteStr,            {nullptr, nullptr, nullptr, nullptr, {2, 0}}},
    {m_IRStr,                {nullptr, nullptr, nullptr, nullptr, {0, 1}}},
    {m_SettingsStr,          {nullptr, nullptr, nullptr, nullptr, {3, 0}}},
    {m_PowerOffStr,          {nullptr, nullptr, nullptr, nullptr, {3, 1}}},
    {m_ConnectedStr,         {nullptr, nullptr, nullptr, nullptr, {0, 0}}},
    {m_FeaturesStr,          {nullptr, nullptr, nullptr, nullptr, {1, 0}}},
    {m_GPSStr,               {nullptr, nullptr, nullptr, nullptr, {2, 0}}},
    {m_GPSDataStr,           {nullptr, nullptr, nullptr, nullptr, {0, 0}}},
    {m_GPSRateStr,           {nullptr, nullptr, nullptr, nullptr, {0, 0}}},
    {m_GPSSentencesStr,      {nullptr, nullptr, nullptr, nullptr, {0, 0}}},
    {m_GPSConstellationStr,  {nullptr, nullptr, nullptr, nullptr, {0, 0}}},
    {m_GPSPowerStr,          {nullptr, nullptr, nullptr, nullptr, {0, 0}}},
    {m_GPSAssistStr,         {nullptr, nullptr, nullptr, nullptr, {0, 0}}},
    {m_GPSNMEAStr,           {nullptr, nullptr, nullptr, nullptr, {0, 0}}},
    {m_IntervalometerStr,    {nullptr, nullptr, nullptr, nullptr, {3, 0}}},
    {m_IntervalCountStr,     {nullptr, nullptr, nullptr, nullptr, {0, 0}}},
    {m_IntervalDelayStr,     {nullptr, nullptr, nullptr, nullptr, {0, 0}}},
    {m_IntervalShutterStr,   {nullptr, nullptr, nullptr, nullptr, {0, 0}}},
    {m_IntervalWaitStr,      {nullptr, nullptr, nullptr, nullptr, {0, 0}}},
    {m_DisplayStr,           {nullptr, nullptr, nullptr, nullptr, {0, 0}}},
    {m_IRSettingsStr,        {nullptr, nullptr, nullptr, nullptr, {1, 2}}},
    {m_TextSizeStr,          {nullptr, nullptr, nullptr, nullptr, {2, 2}}},
    {m_ThemeStr,             {nullptr, nullptr, nullptr, nullptr, {0, 1}}},
    {m_BluetoothStr,         {nullptr, nullptr, nullptr, nullptr, {1, 1}}},
    {m_AboutStr,             {nullptr, nullptr, nullptr, nullptr, {2, 1}}},
    {m_PowerStr,             {nullptr, nullptr, nullptr, nullptr, {3, 1}}},
    {m_FeedbackStr,          {nullptr, nullptr, nullptr, nullptr, {1, 2}}},
    {m_DiagnosticsStr,       {nullptr, nullptr, nullptr, nullptr, {0, 2}}},
    {m_StorageStr,           {nullptr, nullptr, nullptr, nullptr, {3, 2}}},
    {m_BatteryStr,           {nullptr, nullptr, nullptr, nullptr, {0, 0}}},
    {m_FeedbackEventsStr,    {nullptr, nullptr, nullptr, nullptr, {0, 0}}},
    {m_FeedbackVolumeStr,    {nullptr, nullptr, nullptr, nullptr, {0, 0}}},
    {m_DeviceInfoStr,        {nullptr, nullptr, nullptr, nullptr, {0, 0}}},
    {m_PowerStateStr,        {nullptr, nullptr, nullptr, nullptr, {0, 0}}},
    {m_BLEStr,               {nullptr, nullptr, nullptr, nullptr, {0, 0}}},
    {m_TransmitPowerStr,     {nullptr, nullptr, nullptr, nullptr, {0, 0}}},
    {m_RemoteShutter,        {nullptr, nullptr, nullptr, nullptr, {0, 0}}},
    {m_CamerasStr,           {nullptr, nullptr, nullptr, nullptr, {1, 1}}},
    {m_RemoteBulb,           {nullptr, nullptr, nullptr, nullptr, {1, 0}}},
    {m_RemoteInterval,       {nullptr, nullptr, nullptr, nullptr, {2, 0}}},
    {m_RemoteGPSData,        {nullptr, nullptr, nullptr, nullptr, {0, 1}}},
    {m_RemoteDisconnect,     {nullptr, nullptr, nullptr, nullptr, {2, 1}}},
    {m_IntervalometerRunStr, {nullptr, nullptr, nullptr, nullptr, {0, 0}}},
    {m_BulbRunStr,           {nullptr, nullptr, nullptr, nullptr, {0, 0}}},
    {m_BulbDurationStr,      {nullptr, nullptr, nullptr, nullptr, {0, 0}}},
};

UI::UI(const interval_t &interval)
    : m_GPS {GPS::getInstance()},
      m_Intervalometer(interval),
      m_Bulb(Settings::load<Settings::BULB>()),
      m_CalibrationUI(M5.Display.width(), M5.Display.height()) {
#if defined(FURBLE_CONSOLE)
  m_RequestQueue = xQueueCreate(m_RequestQueueLength, sizeof(request_t));
  if (m_RequestQueue == NULL) {
    ESP_LOGE(LOG_TAG, "Failed to create console request queue.");
    abort();
  }
#endif

  // The backlight PWM is clocked from the APB bus. DFS scaling the APB
  // frequency modulates the PWM and the whole screen flickers, so pin the
  // APB clock while the display is on. Display off can release this later.
  Power::getInstance().acquire(Power::LockType::APB_FREQ_MAX, "display");

  lv_init();
  lv_tick_set_cb(tick);

  // set display resolution
  m_Width = M5.Display.width();
  m_Height = M5.Display.height();

  // set display brightness
  auto brightness = Settings::load<Settings::BRIGHTNESS>();
  M5.Display.setBrightness(brightness);
  m_DisplayOffMode = Settings::displayOffEffective();
  if (m_DisplayOffMode > 2) {
    m_DisplayOffMode = 0;
  }
  setInactivityTimeout(Settings::inactivityEffective());
#if defined(FURBLE_SIM)
  Sim::profilerSetDisplayState("on");
#endif
  reloadPowerPolicies();

  // set minimum, ensure this is a multiple of m_BrightnessSteps so the slider steps work
  switch (M5.getBoard()) {
    case m5::board_t::board_M5Stack:
    case m5::board_t::board_M5StackCore2:
    case m5::board_t::board_M5StickCPlus2:
    case m5::board_t::board_M5StickS3:
    case m5::board_t::board_M5Tough:
      m_MinimumBrightness = 32;
      break;
    case m5::board_t::board_M5StickCPlus:
    case m5::board_t::board_M5StickC:
      m_MinimumBrightness = 48;
      break;
    default:
      m_MinimumBrightness = 32;
  }

  // start the one-second housekeeping timer, the power policies run
  // independently of the inactivity decision
  m_InactivityTimer = lv_timer_create(
      [](lv_timer_t *t) {
        FURBLE_SIM_TIMER_FIRE("inactivity_timer");
        auto *ui = static_cast<Furble::UI *>(lv_timer_get_user_data(t));
        ui->processInactivity();
        ui->processAutoOff();
        ui->processLowBattery();
      },
      1000, this);

  // configure display
  m_Display = lv_display_create(m_Width, m_Height);
  lv_display_set_default(m_Display);
  lv_display_set_flush_cb(m_Display, displayFlush);

  // configure display buffers
  m_Buffer1 = heap_caps_aligned_alloc(64, BUFFER_SIZE, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  m_Buffer2 = heap_caps_aligned_alloc(64, BUFFER_SIZE, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  lv_display_set_buffers(m_Display, m_Buffer1, m_Buffer2, BUFFER_SIZE,
                         LV_DISPLAY_RENDER_MODE_PARTIAL);

#if defined(FURBLE_CONSOLE) && defined(CONFIG_LV_USE_PERF_MONITOR)
  // Create the sysmon label and timer, then keep the overlay hidden by default.
  lv_sysmon_show_performance(m_Display);
  lv_sysmon_hide_performance(m_Display);
#endif

#if defined(FURBLE_CONSOLE)
  // diagnostic: log what invalidates, rate limited to avoid flooding
  lv_display_add_event_cb(
      m_Display,
      [](lv_event_t *e) {
        auto *area = static_cast<lv_area_t *>(lv_event_get_param(e));
        static uint32_t count = 0;
        static uint32_t window = 0;
        uint32_t now = Platform::getInstance().tick();
        count++;
        if ((now - window) >= 1000) {
          ESP_LOGI(LOG_TAG, "invalidate: %lu/s last=(%ld,%ld)-(%ld,%ld)", count, area->x1, area->y1,
                   area->x2, area->y2);
          count = 0;
          window = now;
        }
      },
      LV_EVENT_INVALIDATE_AREA, NULL);
#endif

#if defined(FURBLE_SIM)
  lv_display_add_event_cb(
      m_Display,
      [](lv_event_t *e) {
        const auto *area = static_cast<const lv_area_t *>(lv_event_get_param(e));
        if (area != nullptr) {
          const uint64_t width = static_cast<uint64_t>(area->x2 - area->x1 + 1);
          const uint64_t height = static_cast<uint64_t>(area->y2 - area->y1 + 1);
          Sim::profilerInvalidatedArea(width * height);
        }
      },
      LV_EVENT_INVALIDATE_AREA, NULL);
#endif

  initInputDevices();

  setTheme(Settings::load<Settings::THEME>(), Settings::load<Settings::TEXT_SIZE>());

  m_Screen = lv_screen_active();

  m_Root = lv_win_create(m_Screen);
  lv_obj_update_layout(m_Root);

  m_Status.title = lv_win_add_title(m_Root, m_Title);
  m_Header = lv_win_get_header(m_Root);

  m_GPS.init();
  m_Status.gps = &m_GPS;
  m_Status.reconnectIcon = addIcon(&icon_all_inclusive);
  // Non-blocking indicator for an in-progress mid-session reconnect. Hidden
  // until a live link drops, so it never competes with the connected view.
  m_Status.reconnectingIcon = addIcon(&icon_bluetooth);
  lv_obj_add_flag(m_Status.reconnectingIcon, LV_OBJ_FLAG_HIDDEN);
  // Captured once the main menu is built (addMainMenu). Left null until then.
  m_Status.menuTitle = nullptr;
  m_Status.reconnectBackoff = nullptr;
  m_Status.gpsIcon = addIcon(&icon_location_disabled);
  m_Status.batteryIcon = addIcon(&icon_battery_android_frame_4);
  m_Status.batteryLabel = lv_label_create(m_Header);
  m_Status.batteryLevel = nullptr;
  m_Status.batteryVoltage = nullptr;
  m_Status.batteryCurrent = nullptr;
  m_Status.batteryCharging = nullptr;
  m_Status.batteryRuntime = nullptr;
  m_Status.screenLocked = false;

  // prime the battery cache before anything renders it
  const auto sample = Platform::getInstance().sampleBattery();
  m_Status.battery = sample.battery;
  m_Status.meanLevel = sample.meanLevel;
  m_Status.meanVoltage = sample.meanVoltage;
  m_Status.meanCurrent = sample.meanCurrent;
  m_Status.displayLevel = sample.displayLevel;
  m_Status.sampleCount = 0;
  lv_label_set_text_fmt(m_Status.batteryLabel, "%u%%", m_Status.displayLevel);
  setBatteryStyle(Settings::load<Settings::BATT_STYLE>());
  setShowTitle(Settings::load<Settings::SHOW_TITLE>());

  m_GPS.setIcon(m_Status.gpsIcon);

  // sample the battery every 5s, the PMIC is on I2C and the values move slowly
  m_BatteryTimer = lv_timer_create(batteryUpdate, 5000, &m_Status);

  // refresh the diagnostics pages every second, but only while one is open
  m_DiagnosticsTimer = lv_timer_create(diagnosticsUpdate, 1000, &m_Diagnostics);
  lv_timer_pause(m_DiagnosticsTimer);

  // refresh icons every 250ms
  m_IconTimer = lv_timer_create(
      [](lv_timer_t *timer) {
        FURBLE_SIM_TIMER_FIRE("icon_timer");
        status_t *status = static_cast<status_t *>(lv_timer_get_user_data(timer));

        const lv_image_dsc_t *symbol = NULL;
        uint8_t level = status->displayLevel;
        if (level >= 95) {
          symbol = &icon_battery_android_frame_full;
        } else if (level >= 66) {
          symbol = &icon_battery_android_frame_6;
        } else if (level >= 33) {
          symbol = &icon_battery_android_frame_4;
        } else if (level >= 5) {
          symbol = &icon_battery_android_frame_2;
        } else {
          symbol = &icon_battery_android_0;
        }
        // setting the source invalidates the image and forces a decode, only
        // do it when the icon actually changes
        static const lv_image_dsc_t *renderedSymbol = NULL;
        if (renderedSymbol != symbol) {
          renderedSymbol = symbol;
          lv_image_set_src(status->batteryIcon, symbol);
        }

        // the label only changes when the sampled level changes
        static uint8_t rendered = UINT8_MAX;
        if (rendered != level) {
          rendered = level;
          lv_label_set_text_fmt(status->batteryLabel, "%u%%", level);
        }

        if (status->gps->isEnabled() || (status->gps->getSource() == GPS::SOURCE_COMPANION)) {
          lv_obj_clear_flag(status->gpsIcon, LV_OBJ_FLAG_HIDDEN);
        } else {
          lv_obj_add_flag(status->gpsIcon, LV_OBJ_FLAG_HIDDEN);
        }

        static lv_obj_t *lockMsgBox = NULL;
        if (status->screenLocked && (lockMsgBox == NULL)) {
          lockMsgBox = lv_msgbox_create(NULL);
          lv_msgbox_add_title(lockMsgBox, "Screen Locked");
          lv_msgbox_add_text(lockMsgBox, "Double-click PWR button to unlock.");
        } else if (!status->screenLocked && (lockMsgBox != NULL)) {
          lv_msgbox_close_async(lockMsgBox);
          lockMsgBox = NULL;
        }
      },
      250, &m_Status);

  lv_obj_update_layout(m_Header);
  lv_obj_set_height(m_Header, 1.2f * lv_font_get_line_height(LV_FONT_DEFAULT));

  lv_obj_t *x = lv_win_get_content(m_Root);
  lv_obj_set_width(x, LV_PCT(100));
  lv_obj_set_layout(x, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(x, LV_FLEX_FLOW_COLUMN);
  m_Content = lv_obj_create(x);
  lv_obj_set_width(m_Content, LV_PCT(100));
  lv_obj_set_flex_grow(m_Content, 1);

  // zero the padding
  lv_obj_set_style_pad_top(m_Content, 0, LV_STATE_DEFAULT);
  lv_obj_set_style_pad_row(m_Content, 0, LV_STATE_DEFAULT);
  lv_obj_set_style_pad_bottom(m_Content, 0, LV_STATE_DEFAULT);
  lv_obj_set_style_pad_left(m_Content, 0, LV_STATE_DEFAULT);
  lv_obj_set_style_pad_right(m_Content, 0, LV_STATE_DEFAULT);

  lv_obj_set_style_pad_row(x, 0, LV_STATE_DEFAULT);
  lv_obj_set_style_pad_top(x, 0, LV_STATE_DEFAULT);
  lv_obj_set_style_pad_bottom(x, 0, LV_STATE_DEFAULT);
  lv_obj_set_style_pad_left(x, 0, LV_STATE_DEFAULT);
  lv_obj_set_style_pad_right(x, 0, LV_STATE_DEFAULT);

  if (M5.Touch.isEnabled()) {
    // load calibration
    Settings::calibration_t calibration = Settings::load<Settings::TOUCH_CALIBRATION>();
    if (calibration.calibrated) {
      M5.Display.setTouchCalibrate(calibration.points);
    }
  } else {
    // add navigation buttons
    m_NavBar = lv_obj_create(x);

    lv_obj_set_width(m_NavBar, LV_PCT(100));
    lv_obj_set_height(m_NavBar, ICON_HEADER_SIZE + 2);
    lv_obj_set_layout(m_NavBar, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(m_NavBar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(m_NavBar, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(m_NavBar, LV_OBJ_FLAG_SCROLLABLE);

    switch (M5.getBoard()) {
      case m5::board_t::board_M5StickC:
      case m5::board_t::board_M5StickCPlus2:
      case m5::board_t::board_M5StickCPlus:
      case m5::board_t::board_M5StickS3:
        lv_obj_set_style_pad_left(m_Content, 0, LV_STATE_DEFAULT);
        lv_obj_set_style_pad_right(m_Content, 0, LV_STATE_DEFAULT);

        m_Left = lv_button_create(m_Screen);
        m_OK = lv_button_create(m_Screen);
        m_Right = lv_button_create(m_Screen);
        lv_obj_set_style_radius(m_Left, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(m_OK, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(m_Right, 0, LV_PART_MAIN);

        lv_obj_add_flag(m_Left, LV_OBJ_FLAG_FLOATING);
        lv_obj_align(m_Left, LV_ALIGN_BOTTOM_LEFT, 0, 0);

        lv_obj_add_flag(m_OK, LV_OBJ_FLAG_FLOATING);
        lv_obj_align(m_OK, LV_ALIGN_BOTTOM_MID, 0, 0);

        lv_obj_add_flag(m_Right, LV_OBJ_FLAG_FLOATING);
        lv_obj_align(m_Right, LV_ALIGN_RIGHT_MID, 0, m_RightYOffset);
        break;

      default:
        m_Left = lv_button_create(m_NavBar);
        m_OK = lv_button_create(m_NavBar);
        m_Right = lv_button_create(m_NavBar);
        break;
    }

    // lighten the buttons 50%, to distinguish from other widget selection
    lv_color_t lighter = lv_color_lighten(lv_obj_get_style_bg_color(m_Left, LV_PART_MAIN), 255 / 2);

    lv_obj_set_style_bg_color(m_Left, lighter, LV_PART_MAIN);
    lv_obj_set_style_bg_color(m_OK, lighter, LV_PART_MAIN);
    lv_obj_set_style_bg_color(m_Right, lighter, LV_PART_MAIN);

    // prepare shutter handling
    prepareShutterControl();

    // ensure buttons do not receive focus
    lv_group_remove_obj(m_Left);
    lv_group_remove_obj(m_OK);
    lv_group_remove_obj(m_Right);

    // In SHUTTER mode the physical buttons drive these indicators through a
    // LV_INDEV_TYPE_BUTTON device. LVGL click focus sends LV_EVENT_FOCUSED to a
    // click focusable object on press and only clears it when another object is
    // pressed, so the green focus outline lingers after release. The indicators
    // are pure hints and never take focus, so drop the flag to stop it.
    lv_obj_remove_flag(m_Left, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_remove_flag(m_OK, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_remove_flag(m_Right, LV_OBJ_FLAG_CLICK_FOCUSABLE);

    lv_obj_set_size(m_Left, ICON_HEADER_SIZE, ICON_HEADER_SIZE);
    lv_obj_set_size(m_OK, ICON_HEADER_SIZE, ICON_HEADER_SIZE);
    lv_obj_set_size(m_Right, ICON_HEADER_SIZE, ICON_HEADER_SIZE);
  }

  configureControl(ControlMode::MENU);

  // create connection timer
  m_ConnectContext = {this, NULL, NULL, NULL, NULL, NULL, false};
  m_ConnectTimer = lv_timer_create(connectTimerHandler, 50, &m_ConnectContext);
  lv_timer_pause(m_ConnectTimer);

  // create intervalometer timer
  m_IntervalTimer = lv_timer_create(intervalometer, 100, &m_Intervalometer);
  lv_timer_pause(m_IntervalTimer);

  if (Settings::load<Settings::COMPANION>()) {
    startCompanionPairingTimer();
  }

  addMainMenu();

  setPresetPicker(Settings::load<Settings::PRESET_PICKER>());

  m_GPS.startService();
  setDisplayMode(Settings::load<uint8_t>(Settings::DISPLAY_MODE));
}

void UI::startCompanionPairingTimer(void) {
  if (m_CompanionPairingTimer == nullptr) {
    m_CompanionPairingTimer = lv_timer_create(companionPairingTimer, 250, this);
  }
}

void UI::closeCompanionPairingDialog(void) {
  if (m_CompanionPairingDialog != nullptr) {
    if (lv_obj_is_valid(m_CompanionPairingDialog)) {
      lv_msgbox_close_async(m_CompanionPairingDialog);
    }
    m_CompanionPairingDialog = nullptr;
  }

  if (m_CompanionPairingPrevFocus != nullptr) {
    if (lv_obj_is_valid(m_CompanionPairingPrevFocus)) {
      lv_group_focus_obj(m_CompanionPairingPrevFocus);
    }
    m_CompanionPairingPrevFocus = nullptr;
  }
}

void UI::stopCompanionPairingTimer(void) {
  closeCompanionPairingDialog();
  if (m_CompanionPairingTimer != nullptr) {
    lv_timer_del(m_CompanionPairingTimer);
    m_CompanionPairingTimer = nullptr;
  }
}

void UI::companionPairingTimer(lv_timer_t *timer) {
  FURBLE_SIM_TIMER_FIRE("companion_pairing_timer");
  auto *ui = static_cast<UI *>(lv_timer_get_user_data(timer));
  auto &companion = Companion::getInstance();
  if (!companion.isEnabled() || !companion.hasPendingPairing()) {
    ui->closeCompanionPairingDialog();
    return;
  }

  if (ui->m_CompanionPairingDialog != nullptr) {
    return;
  }

  char text[96];
  std::snprintf(text, sizeof(text), "Confirm number:\n%06lu", companion.getPendingPairingPin());
  ui->m_CompanionPairingPrevFocus = lv_group_get_focused(ui->m_Group);
  ui->m_CompanionPairingDialog = lv_msgbox_create(nullptr);
  lv_msgbox_add_title(ui->m_CompanionPairingDialog, "Pair companion");
  lv_msgbox_add_text(ui->m_CompanionPairingDialog, text);

  lv_obj_t *accept = lv_msgbox_add_footer_button(ui->m_CompanionPairingDialog, "Accept");
  // Add the button to the encoder group so it is focusable and operable on
  // non-touch devices. Without this, lv_group_focus_obj below is a no-op.
  addToInputGroup(ui->m_Group, accept);
  lv_obj_add_event_cb(
      accept,
      [](lv_event_t *event) {
        auto *ui = static_cast<UI *>(lv_event_get_user_data(event));
        Companion::getInstance().confirmPairing(true);
        ui->closeCompanionPairingDialog();
      },
      LV_EVENT_CLICKED, ui);

  lv_obj_t *reject = lv_msgbox_add_footer_button(ui->m_CompanionPairingDialog, "Reject");
  addToInputGroup(ui->m_Group, reject);
  lv_obj_add_event_cb(
      reject,
      [](lv_event_t *event) {
        auto *ui = static_cast<UI *>(lv_event_get_user_data(event));
        Companion::getInstance().confirmPairing(false);
        ui->closeCompanionPairingDialog();
      },
      LV_EVENT_CLICKED, ui);

  lv_group_focus_obj(accept);
}

void UI::buttonPWRRead(lv_indev_t *drv, lv_indev_data_t *data) {
  auto *ui = static_cast<UI *>(lv_indev_get_user_data(drv));
  data->key = ui->inputKey(drv);
  bool pressed = M5.BtnPWR.isPressed();
  if (ui->handleLeftLongPress(drv, pressed)) {
    data->state = LV_INDEV_STATE_RELEASED;
    return;
  }
  if (ui->handleDisplayInput(drv, data, pressed, true)) {
    return;
  }

  if (M5.BtnPWR.isReleased()) {
    data->state = LV_INDEV_STATE_RELEASED;
  } else if (pressed) {
    data->state = LV_INDEV_STATE_PRESSED;
  }
}

// read power button for M5StickC and M5StickCPlus
void UI::buttonPEKRead(lv_indev_t *drv, lv_indev_data_t *data) {
  auto *ui = static_cast<UI *>(lv_indev_get_user_data(drv));
  data->key = ui->inputKey(drv);
  bool held = M5.BtnPWR.isPressed();
  if (ui->handleLeftLongPress(drv, held)) {
    data->state = LV_INDEV_STATE_RELEASED;
    return;
  }
  bool pressed = Platform::getInstance().getPWRClickCount() > 0;
  if (ui->handleDisplayInput(drv, data, pressed, false)) {
    return;
  }

  if (pressed) {
    data->state = LV_INDEV_STATE_PRESSED;
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

void UI::buttonARead(lv_indev_t *drv, lv_indev_data_t *data) {
  auto *ui = static_cast<UI *>(lv_indev_get_user_data(drv));
  data->key = ui->inputKey(drv);
  bool pressed = M5.BtnA.isPressed();
  if (ui->handleLeftLongPress(drv, pressed)) {
    data->state = LV_INDEV_STATE_RELEASED;
    return;
  }
  if (ui->handleDisplayInput(drv, data, pressed, true)) {
    return;
  }

  if (M5.BtnA.isReleased()) {
    data->state = LV_INDEV_STATE_RELEASED;
  } else if (pressed) {
    data->state = LV_INDEV_STATE_PRESSED;
  }
}

void UI::buttonBRead(lv_indev_t *drv, lv_indev_data_t *data) {
  auto *ui = static_cast<UI *>(lv_indev_get_user_data(drv));
  data->key = ui->inputKey(drv);
  bool pressed = M5.BtnB.isPressed();
  if (ui->handleLeftLongPress(drv, pressed)) {
    data->state = LV_INDEV_STATE_RELEASED;
    return;
  }
  if (ui->handleDisplayInput(drv, data, pressed, true)) {
    return;
  }

  if (M5.BtnB.isReleased()) {
    data->state = LV_INDEV_STATE_RELEASED;
  } else if (pressed) {
    data->state = LV_INDEV_STATE_PRESSED;
  }
}

void UI::buttonCRead(lv_indev_t *drv, lv_indev_data_t *data) {
  auto *ui = static_cast<UI *>(lv_indev_get_user_data(drv));
  data->key = ui->inputKey(drv);
  bool pressed = M5.BtnC.isPressed();
  if (ui->handleLeftLongPress(drv, pressed)) {
    data->state = LV_INDEV_STATE_RELEASED;
    return;
  }
  if (ui->handleDisplayInput(drv, data, pressed, true)) {
    return;
  }

  if (M5.BtnC.isReleased()) {
    data->state = LV_INDEV_STATE_RELEASED;
  } else if (pressed) {
    data->state = LV_INDEV_STATE_PRESSED;
  }
}

void UI::touchRead(lv_indev_t *drv, lv_indev_data_t *data) {
  auto count = M5.Touch.getCount();
  auto *ui = static_cast<UI *>(lv_indev_get_user_data(drv));
  if (ui->handleDisplayInput(drv, data, count > 0, true)) {
    return;
  }

  if (count == 0) {
    data->state = LV_INDEV_STATE_RELEASED;
  } else {
    auto touch = M5.Touch.getDetail(0);
    data->state = LV_INDEV_STATE_PRESSED;
    data->point.x = touch.x;
    data->point.y = touch.y;
  }
}

uint32_t UI::inputKey(lv_indev_t *drv) const {
  if (drv == m_ButtonL) {
    return m_KeyLeft;
  }
  if (drv == m_ButtonO) {
    return m_KeyEnter;
  }
  if (drv == m_ButtonR) {
    return m_KeyRight;
  }
  return 0;
}

bool UI::handleDisplayInput(lv_indev_t *drv,
                            lv_indev_data_t *data,
                            bool pressed,
                            bool releaseExpected) {
  if (m_SwallowInput) {
    // Swallow every source until all of them report released. On touch
    // boards one tap drives the touch indev and a synthesized button indev,
    // so swallowing only the waking source would let the other one through.
    data->state = LV_INDEV_STATE_RELEASED;
    if (pressed && releaseExpected) {
      m_SwallowPending |= inputBit(drv);
    } else {
      m_SwallowPending &= ~inputBit(drv);
    }
    if (m_SwallowPending == 0) {
      m_SwallowInput = false;
    }
    return true;
  }

  if (!m_DisplayOff || !pressed || isBlindRemoteInput(drv)) {
    return false;
  }

  wakeDisplay();
  // the swallowed press never reaches LVGL, so record the activity here
  lv_display_trigger_activity(m_Display);
  data->state = LV_INDEV_STATE_RELEASED;
  if (releaseExpected) {
    m_SwallowInput = true;
    m_SwallowPending = inputBit(drv);
  }
  return true;
}

bool UI::handleLeftLongPress(lv_indev_t *drv, bool pressed) {
  if (drv != m_ButtonL) {
    return false;
  }

  if (!pressed) {
    const bool handled = m_LeftLongPressHandled;
    m_LeftPressed = false;
    m_LeftLongPressHandled = false;
    m_LeftPressTick = 0;
    return handled;
  }

  if (!m_LeftPressed) {
    m_LeftPressed = true;
    m_LeftPressTick = tick();
  }

  if (m_LeftLongPressHandled) {
    return true;
  }

  if ((tick() - m_LeftPressTick) < LEFT_LONG_PRESS_MS) {
    return false;
  }

  m_LeftLongPressHandled = true;
  m_SwallowPending &= static_cast<uint8_t>(~inputBit(drv));
  if (m_SwallowPending == 0) {
    m_SwallowInput = false;
  }
  if (m_DisplayOff) {
    wakeDisplay();
  }
  navigateBack();
  return true;
}

void UI::navigateBack(void) {
  if ((m_MainMenu.main == nullptr) || !lv_obj_is_valid(m_MainMenu.main)) {
    return;
  }

  lv_obj_t *back = lv_menu_get_main_header_back_button(m_MainMenu.main);
  if ((back == nullptr) || !lv_obj_is_valid(back)) {
    return;
  }

  // Some connected and remote pages hide or disable the visual back arrow.
  // The raw left-button escape is deliberately stronger than that policy.
  lv_obj_remove_state(back, LV_STATE_DISABLED);
  lv_obj_clear_flag(back, LV_OBJ_FLAG_HIDDEN);
  if (m_Group != nullptr) {
    lv_group_set_editing(m_Group, false);
  }
  lv_obj_send_event(back, LV_EVENT_CLICKED, m_MainMenu.main);
  configureControl(ControlMode::MENU);
}

uint8_t UI::inputBit(lv_indev_t *drv) const {
  if (drv == m_ButtonL) {
    return 0x01;
  }
  if (drv == m_ButtonO) {
    return 0x02;
  }
  if (drv == m_ButtonR) {
    return 0x04;
  }
  if (drv == m_Touch) {
    return 0x08;
  }
  return 0;
}

bool UI::isBlindRemoteInput(lv_indev_t *drv) const {
  return isBlindRemoteActive() && ((drv == m_ButtonO) || (drv == m_ButtonR));
}

bool UI::isBlindRemoteActive(void) const {
  return (m_DisplayOffMode == 2) && !M5.Touch.isEnabled()
         && (m_ControlMode == ControlMode::SHUTTER);
}

void UI::sleepDisplay(void) {
  if (m_DisplayOff) {
    return;
  }

  // The panel datasheets require 120 ms between Sleep Out and Sleep In.
  // Refuse to sleep too soon after a wake, the inactivity timer retries.
  if ((tick() - m_WakeTick) < DISPLAY_SLEEP_DWELL_MS) {
    return;
  }

  M5.Display.sleep();
  m_SleepTick = tick();
  m_DisplayOff = true;
  m_DisplayState = DisplayState::OFF;
  Feedback::getInstance().setDisplayOff(true);
  Platform::getInstance().setDisplayOff(true);
#if defined(FURBLE_SIM)
  Sim::profilerSetDisplayState("off");
#endif
  lv_timer_pause(m_IconTimer);

  // The backlight PWM no longer needs a fixed APB clock while the panel sleeps.
  Power::getInstance().release(Power::LockType::APB_FREQ_MAX, "display");
}

void UI::wakeDisplay(void) {
  if (!m_DisplayOff) {
    return;
  }

  // The panel datasheets require 120 ms between Sleep In and Sleep Out.
  // M5GFX setSleep issues the bare commands, so enforce the dwell here.
  // This runs on the UI task without the Control mutex, a short delay is safe.
  uint32_t elapsed = tick() - m_SleepTick;
  if (elapsed < DISPLAY_SLEEP_DWELL_MS) {
    vTaskDelay(pdMS_TO_TICKS(DISPLAY_SLEEP_DWELL_MS - elapsed));
  }

  // Acquire before wakeup because M5GFX restores the backlight during wakeup.
  Power::getInstance().acquire(Power::LockType::APB_FREQ_MAX, "display");
  M5.Display.wakeup();
  m_WakeTick = tick();
  m_DisplayOff = false;
  m_DisplayState = DisplayState::ACTIVE;
  Platform::getInstance().setDisplayOff(false);
  Feedback::getInstance().setDisplayOff(false);
#if defined(FURBLE_SIM)
  Sim::profilerSetDisplayState("on");
#endif
  lv_timer_resume(m_IconTimer);
  // Deliberately no lv_display_trigger_activity() here. A wake is not user
  // activity, the input path that saw a real press triggers it itself. The
  // low battery warning also wakes the panel and must not postpone auto off.
}

size_t UI::inactivityIndex(uint8_t value) {
  size_t closest = 0;
  uint8_t distance = UINT8_MAX;

  for (size_t index = 0; index < m_InactivityValues.size(); index++) {
    uint8_t candidate = m_InactivityValues[index];
    uint8_t current = (value > candidate) ? (value - candidate) : (candidate - value);
    if (current < distance) {
      distance = current;
      closest = index;
    }
  }

  return closest;
}

void UI::displayFlush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
  uint32_t w = (area->x2 - area->x1 + 1);
  uint32_t h = (area->y2 - area->y1 + 1);

#if defined(FURBLE_SIM)
  Sim::profilerFlushedPixels(static_cast<uint64_t>(w) * h);
#endif
  lv_draw_sw_rgb565_swap(px_map, w * h);
  M5.Display.pushImageDMA<uint16_t>(area->x1, area->y1, w, h, (uint16_t *)px_map);
  lv_disp_flush_ready(disp);
}

uint32_t UI::tick(void) {
  return Platform::getInstance().tick();
}

void UI::initInputDevices(void) {
  m_Group = lv_group_create();
  lv_group_set_default(m_Group);

  m_ButtonL = lv_indev_create();
  lv_indev_set_type(m_ButtonL, LV_INDEV_TYPE_ENCODER);
  lv_indev_set_user_data(m_ButtonL, this);
  lv_indev_set_group(m_ButtonL, m_Group);

  m_ButtonO = lv_indev_create();
  lv_indev_set_type(m_ButtonO, LV_INDEV_TYPE_ENCODER);
  lv_indev_set_user_data(m_ButtonO, this);
  lv_indev_set_group(m_ButtonO, m_Group);

  m_ButtonR = lv_indev_create();
  lv_indev_set_type(m_ButtonR, LV_INDEV_TYPE_ENCODER);
  lv_indev_set_user_data(m_ButtonR, this);
  lv_indev_set_group(m_ButtonR, m_Group);

  switch (M5.getBoard()) {
    case m5::board_t::board_M5StickC:
    case m5::board_t::board_M5StickCPlus:
      lv_indev_set_read_cb(m_ButtonL, buttonPEKRead);
      lv_indev_set_read_cb(m_ButtonO, buttonARead);
      lv_indev_set_read_cb(m_ButtonR, buttonBRead);
      break;

    case m5::board_t::board_M5StickCPlus2:
    case m5::board_t::board_M5StickS3:
      lv_indev_set_read_cb(m_ButtonL, buttonPWRRead);
      lv_indev_set_read_cb(m_ButtonO, buttonARead);
      lv_indev_set_read_cb(m_ButtonR, buttonBRead);
      break;

    case m5::board_t::board_M5Tough:
    case m5::board_t::board_M5StackCore2:
      m_Touch = lv_indev_create();
      lv_indev_set_type(m_Touch, LV_INDEV_TYPE_POINTER);
      lv_indev_set_user_data(m_Touch, this);
      lv_indev_set_read_cb(m_Touch, touchRead);
      __attribute__((fallthrough));

    case m5::board_t::board_M5Stack:
      lv_indev_set_read_cb(m_ButtonL, buttonARead);
      lv_indev_set_read_cb(m_ButtonO, buttonBRead);
      lv_indev_set_read_cb(m_ButtonR, buttonCRead);
      break;

    default:
      ESP_LOGE("ui", "Unknown hardware, not configuring input devices");
  }
}

void UI::setTheme(std::string name, uint8_t textSize) {
  lv_display_t *display = lv_display_get_default();
  lv_color_t primary = lv_palette_main(LV_PALETTE_BLUE);
  lv_color_t secondary = lv_color_black();
  bool dark = false;
  static lv_theme_t theme;
  static lv_style_t style_img;
  static lv_style_t style_bg;
  static lv_style_t style_button;
  static lv_style_t style_disable;
  static lv_style_t style_no_shadow;

  lv_style_init(&style_img);
  lv_style_init(&style_bg);
  lv_style_init(&style_button);
  lv_style_init(&style_disable);
  lv_style_init(&style_no_shadow);

  lv_style_set_shadow_width(&style_no_shadow, 0);

  // fully recolor black pixels in images
  lv_style_set_image_recolor_opa(&style_img, LV_OPA_COVER);

  // add 40% opacity for disabled widgets
  lv_style_set_text_opa(&style_disable, LV_OPA_40);
  lv_style_set_image_opa(&style_disable, LV_OPA_40);

  // Make the focus ring explicit and bold so the selected item is obvious in
  // every theme. Without this the ring inherits the LVGL default width and
  // opacity, which reads weakly on switches and rollers in the light Default
  // theme where the outline sits on a white background. The per-theme
  // outline_color set below still controls the hue.
  lv_style_set_outline_width(&style_button, 3);
  lv_style_set_outline_opa(&style_button, LV_OPA_COVER);
  lv_style_set_outline_pad(&style_button, 2);

  if (name == "Dark") {
    dark = true;
    lv_style_set_image_recolor(&style_img, lv_color_white());
    lv_style_set_bg_color(&style_bg, lv_color_black());
    lv_style_set_outline_color(&style_button, LV_COLOR_MAKE(127, 255, 0));
  } else if (name == "Mono Furble") {
    dark = true;
    primary = lv_palette_main(LV_PALETTE_ORANGE);
    lv_style_set_image_recolor(&style_img, primary);
    lv_style_set_image_recolor(&style_button, lv_color_white());
    lv_style_set_bg_color(&style_bg, lv_color_black());
    lv_style_set_outline_color(&style_button, lv_color_white());
  } else {
    // Default
    dark = false;

    // lighten focused images
    lv_style_set_image_recolor_opa(&style_button, LV_OPA_50);

    lv_style_set_outline_color(&style_button, lv_palette_main(LV_PALETTE_ORANGE));
  }

  lv_theme_t *theme_default =
      lv_theme_default_init(display, primary, secondary, dark, fontForTextSize(textSize));
  theme = *theme_default;
  lv_theme_set_parent(&theme, theme_default);
  lv_theme_set_apply_cb(&theme, [](lv_theme_t *th, lv_obj_t *obj) {
    // The focus outline belongs on the focusable item container only. Menu rows
    // set LV_OBJ_FLAG_STATE_TRICKLE, so focusing a row propagates
    // LV_STATE_FOCUSED to its child icon and label. Attaching style_button (which
    // carries the outline) to those children draws a second and third ring inside
    // the row. Keep the outline off image and label widgets so the selected item
    // shows a single ring around the whole item.
    const bool outlineTarget =
        !lv_obj_check_type(obj, &lv_image_class) && !lv_obj_check_type(obj, &lv_label_class);
    if (lv_obj_check_type(obj, &lv_button_class) || lv_obj_check_type(obj, &lv_roller_class)
        || lv_obj_check_type(obj, &lv_slider_class) || lv_obj_check_type(obj, &lv_switch_class)) {
      lv_obj_add_style(obj, &style_button, LV_STATE_FOCUS_KEY);
    } else if (!lv_obj_check_type(obj, &lv_button_class)
               && !lv_obj_check_type(obj, &lv_msgbox_footer_button_class)) {
      lv_obj_add_style(obj, &style_bg, LV_STATE_DEFAULT);
    }

    lv_obj_add_style(obj, &style_no_shadow, LV_STATE_DEFAULT);
    lv_obj_add_style(obj, &style_img, LV_STATE_DEFAULT);
    if (outlineTarget) {
      lv_obj_add_style(obj, &style_button, LV_STATE_FOCUSED);
    }
    lv_obj_add_style(obj, &style_disable, LV_STATE_DISABLED);
  });
  lv_display_set_theme(display, &theme);
}

void UI::shutterLock(Control &control) {
  if (!m_ShutterLock) {
    control.sendCommand(Control::CMD_SHUTTER_PRESS);
    Feedback::getInstance().signal(Feedback::SHUTTER_FIRED);
    m_ShutterLock = true;
    ESP_LOGI("ui", "SHUTTER LOCKED");

    if (M5.Touch.isEnabled()) {
      lv_obj_add_state(m_OK, LV_STATE_DISABLED);
      lv_obj_add_state(m_Right, LV_STATE_DISABLED);
      lv_obj_set_style_bg_image_src(m_ShutterLockIcon, &icon_lock, 0);
    } else {
      lv_obj_set_style_bg_image_src(m_ShutterLockIcon, &icon_lock_24, 0);
      lv_obj_set_style_radius(m_ShutterLockIcon, (ICON_HEADER_SIZE / 2), LV_PART_MAIN);
    }
  }
}

void UI::shutterUnlock(Control &control) {
  if (m_ShutterLock) {
    control.sendCommand(Control::CMD_SHUTTER_RELEASE);
    m_ShutterLock = false;
    ESP_LOGI("ui", "SHUTTER UNLOCKED");

    if (M5.Touch.isEnabled()) {
      lv_obj_remove_state(m_OK, LV_STATE_DISABLED);
      lv_obj_remove_state(m_Right, LV_STATE_DISABLED);
      lv_obj_set_style_bg_image_src(m_ShutterLockIcon, &icon_lock_open_right, 0);
    } else {
      lv_obj_set_style_bg_image_src(m_ShutterLockIcon, &icon_lock_open_right_24, 0);
      lv_obj_set_style_radius(m_ShutterLockIcon, 0, LV_PART_MAIN);
    }
  }
}

void UI::handleShutter(lv_event_t *e) {
  auto *ui = static_cast<UI *>(lv_event_get_user_data(e));
  auto &control = Control::getInstance();
  lv_event_code_t code = lv_event_get_code(e);

  if (ui->m_ControlMode == ControlMode::PRESET) {
    if (code == LV_EVENT_RELEASED) {
      ui->presetConfirm();
    }
    return;
  }

  switch (code) {
    case LV_EVENT_PRESSED:
      if (ui->m_FocusPressed) {
        ui->shutterLock(control);
      } else if (!ui->m_ShutterLock) {
        control.sendCommand(Control::CMD_SHUTTER_PRESS);
        Feedback::getInstance().signal(Feedback::SHUTTER_FIRED);
      }
      break;
    case LV_EVENT_RELEASED:
      if (ui->m_ShutterLock) {
        if (!ui->m_FocusPressed) {
          ui->shutterUnlock(control);
        }
      } else {
        control.sendCommand(Control::CMD_SHUTTER_RELEASE);
      }
      break;
    default:
      break;
  }
}

void UI::handleButtonMode(lv_event_t *e) {
  auto *ui = static_cast<UI *>(lv_event_get_user_data(e));
  if (Settings::load<Settings::BUTTON_MODE>() != Settings::BUTTON_MODE_ONE_BUTTON_VALUE) {
    handleShutter(e);
    return;
  }

  auto &control = Control::getInstance();
  const lv_event_code_t code = lv_event_get_code(e);
  auto *indev = lv_indev_active();
  uint8_t streak = indev == nullptr ? 0 : lv_indev_get_short_click_streak(indev);
#if defined(FURBLE_SIM)
  // Headless scenarios cannot drive LVGL's real click-streak timing, so they
  // inject the streak the dispatch should classify. The 400 ms window itself
  // stays a hardware concern; this only feeds the classification input.
  if (ui->m_SimClickStreakActive) {
    streak = ui->m_SimClickStreak;
  }
#endif

  switch (code) {
    case LV_EVENT_PRESSED:
    {
      if (ui->m_ShutterLock || ui->m_ButtonModeFocusPressed || ui->m_ButtonModeShutterPressed) {
        break;
      }
      ui->m_ButtonModeLongPressed = false;
      // Defer the single-click focus action. Firing focus eagerly on the first
      // press leaked a stray focus tap before every double click and
      // click-then-hold. Instead, focus only engages when this first press is
      // held (LONG_PRESSED). A press that follows a recent short click is the
      // second half of a multi-click gesture (double click or click-then-hold),
      // so it drives the shutter directly with no leading focus. A quick release
      // makes the shutter tap; a held release makes a sustained shutter press.
      const bool followsClick = (ui->m_ButtonModeClickStreak >= 1)
                                && (lv_tick_diff(lv_tick_get(), ui->m_ButtonModeLastClick)
                                    <= BUTTON_MODE_CLICK_WINDOW_MS);
      if (followsClick) {
        control.sendCommand(Control::CMD_SHUTTER_PRESS);
        ui->m_ButtonModeShutterPressed = true;
        // Consume the click so the release does not re-arm another shutter.
        ui->m_ButtonModeClickStreak = 0;
      }
      break;
    }
    case LV_EVENT_LONG_PRESSED:
      // LVGL can send the initial long press twice. Require release first.
      if (ui->m_ButtonModeLongPressed) {
        break;
      }
      ui->m_ButtonModeLongPressed = true;
      // A held first press with no shutter already in flight is the single
      // press-and-hold focus gesture.
      if (!ui->m_ButtonModeShutterPressed && !ui->m_ButtonModeFocusPressed) {
        control.sendCommand(Control::CMD_FOCUS_PRESS);
        ui->m_ButtonModeFocusPressed = true;
      }
      break;
    case LV_EVENT_RELEASED:
      if (ui->m_ButtonModeShutterPressed) {
        control.sendCommand(Control::CMD_SHUTTER_RELEASE);
        ui->m_ButtonModeShutterPressed = false;
      }
      if (ui->m_ButtonModeFocusPressed) {
        control.sendCommand(Control::CMD_FOCUS_RELEASE);
        ui->m_ButtonModeFocusPressed = false;
      }
      ui->m_ButtonModeLongPressed = false;
      break;
    case LV_EVENT_SHORT_CLICKED:
      // Record the short click so a press within the streak window is treated
      // as the second half of a multi-click gesture. A lone short click does
      // nothing on its own.
      ui->m_ButtonModeClickStreak = streak;
      ui->m_ButtonModeLastClick = lv_tick_get();
      break;
    default:
      break;
  }
}

void UI::handleFocus(lv_event_t *e) {
  auto *ui = static_cast<UI *>(lv_event_get_user_data(e));
  auto &control = Control::getInstance();
  lv_event_code_t code = lv_event_get_code(e);

  if (ui->m_ControlMode == ControlMode::PRESET) {
    if ((code == LV_EVENT_PRESSED) || (code == LV_EVENT_LONG_PRESSED_REPEAT)) {
      ui->presetStep(1);
    }
    return;
  }

  switch (code) {
    case LV_EVENT_PRESSED:
      ui->m_FocusPressed = true;
      if (ui->m_ShutterLock) {
        ui->shutterUnlock(control);
      } else {
        control.sendCommand(Control::CMD_FOCUS_PRESS);
      }
      break;
    case LV_EVENT_RELEASED:
      ui->m_FocusPressed = false;
      if (!ui->m_ShutterLock) {
        control.sendCommand(Control::CMD_FOCUS_RELEASE);
      }
      break;
    default:
      break;
  }
}

void UI::handleShutterLock(lv_event_t *e) {
  auto *ui = static_cast<UI *>(lv_event_get_user_data(e));
  auto &control = Control::getInstance();
  lv_event_code_t code = lv_event_get_code(e);

  // Bug in LVGL?
  // Initial long pressed is triggered twice, fix with requiring release event
  static bool released = true;

  switch (code) {
    case LV_EVENT_LONG_PRESSED:
      if (released) {
        if (ui->m_ShutterLock) {
          ui->shutterUnlock(control);
        } else {
          ui->shutterLock(control);
        }
        released = false;
      }
      break;
    case LV_EVENT_RELEASED:
      released = true;
      break;
    default:
      break;
  }
}

void UI::prepareShutterControl(void) {
  lv_obj_add_event_cb(
      m_Left,
      [](lv_event_t *e) {
        auto *ui = static_cast<UI *>(lv_event_get_user_data(e));
        lv_event_code_t code = lv_event_get_code(e);
        if (ui->m_ControlMode == ControlMode::PRESET) {
          if ((code == LV_EVENT_PRESSED) || (code == LV_EVENT_LONG_PRESSED_REPEAT)) {
            ui->presetStep(-1);
          }
          return;
        }

        if (code == LV_EVENT_CLICKED) {
          lv_obj_t *back = lv_menu_get_main_header_back_button(m_MainMenu.main);
          lv_obj_send_event(back, LV_EVENT_CLICKED, m_MainMenu.main);
          ui->configureControl(ControlMode::MENU);
        }
      },
      LV_EVENT_ALL, this);

  lv_obj_add_event_cb(m_OK, handleButtonMode, LV_EVENT_ALL, this);

  lv_obj_add_event_cb(m_Right, handleFocus, LV_EVENT_ALL, this);
}

lv_obj_t *UI::addIcon(const lv_image_dsc_t *symbol) {
  lv_obj_t *icon = lv_image_create(m_Header);

  setIcon(icon, symbol);

  return icon;
}

void UI::setIcon(lv_obj_t *icon, const lv_image_dsc_t *symbol) {
  lv_obj_set_size(icon, ICON_HEADER_SIZE, ICON_HEADER_SIZE);
  lv_image_set_src(icon, symbol);
}

lv_obj_t *UI::addMenuItem(const menu_t &menu,
                          const lv_image_dsc_t *icon,
                          const char *text,
                          bool checkbox,
                          const int32_t col_pos,
                          const int32_t row_pos) {
  lv_obj_t *cont = lv_menu_cont_create(menu.page);
#if defined(FURBLE_M5COREX)
  lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
#else
  lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_ROW);
#if defined(FURBLE_M5STICKC_PLUS) || defined(FURBLE_M5STICKS3)
  const bool connectedPage = menu.page == m_Menu.at(m_ConnectedStr).page;
  lv_obj_set_style_pad_top(cont, connectedPage ? 0 : 6, LV_STATE_DEFAULT);
  lv_obj_set_style_pad_bottom(cont, connectedPage ? 0 : 6, LV_STATE_DEFAULT);
#elif defined(FURBLE_M5STICKC)
  // 80x160 is the shortest panel. Trim the per-row padding so the home menu
  // (Connect, Scan, Delete, Settings, Power off) fits without scrolling. The
  // Connected page carries the most rows now that it also holds the Cameras
  // entry, so it drops to zero padding to keep the extra row on-panel, matching
  // the large narrow panels above.
  const bool connectedPage = menu.page == m_Menu.at(m_ConnectedStr).page;
  lv_obj_set_style_pad_top(cont, connectedPage ? 0 : 1, LV_STATE_DEFAULT);
  lv_obj_set_style_pad_bottom(cont, connectedPage ? 0 : 1, LV_STATE_DEFAULT);
#endif
#endif
  lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);

#if defined(FURBLE_M5STICKC)
  // screen is too small for icons
#else
  if (icon) {
    lv_obj_t *img = lv_image_create(cont);
    lv_obj_set_size(img, ICON_MENU_SIZE, ICON_MENU_SIZE);
    lv_image_set_inner_align(img, LV_IMAGE_ALIGN_STRETCH);
    lv_image_set_src(img, icon);
    lv_obj_set_grid_cell(cont, LV_GRID_ALIGN_STRETCH, col_pos, 1, LV_GRID_ALIGN_STRETCH, row_pos,
                         1);
  }
#endif

  if (checkbox) {
    lv_obj_t *check = lv_checkbox_create(cont);
    lv_checkbox_set_text(check, text);
    lv_obj_set_width(check, LV_PCT(100));
    lv_obj_add_flag(check, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    addToInputGroup(menu.group, check);
    return check;
  } else {
    lv_obj_t *label = lv_label_create(cont);
    lv_label_set_text(label, text);
    if (icon) {
#if defined(FURBLE_M5COREX)
      lv_obj_set_style_text_font(label, fontForIconMenu(Settings::load<Settings::TEXT_SIZE>()), 0);
#endif
    } else {
      lv_obj_set_width(label, LV_PCT(100));
    }
    lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_add_flag(cont, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(cont, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_add_flag(cont, LV_OBJ_FLAG_STATE_TRICKLE);
    addToInputGroup(menu.group, cont);
  }

  return cont;
}

void UI::addSettingItem(lv_obj_t *page, const char *symbol, Settings::type_t setting) {
  lv_obj_t *obj = lv_menu_cont_create(page);
  lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_ROW_WRAP);

  if (symbol) {
    lv_obj_t *icon = lv_image_create(obj);
    lv_image_set_src(icon, symbol);
  }

  auto &s = Settings::get(setting);

  lv_obj_t *label = lv_label_create(obj);
  lv_label_set_text(label, s.name);
  lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_set_flex_grow(label, 1);

  lv_obj_t *sw = lv_switch_create(obj);
  lv_obj_add_flag(sw, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
  addToInputGroup(m_Group, sw);
#if defined(FURBLE_SIM)
  g_simSettingSwitches[static_cast<int>(setting)] = sw;
#endif
  bool enable = Settings::load<bool>(setting);
  lv_obj_add_state(sw, enable ? LV_STATE_CHECKED : LV_STATE_DEFAULT);
  lv_obj_add_event_cb(
      sw,
      [](lv_event_t *e) {
        const auto *setting = static_cast<Settings::setting_t *>(lv_event_get_user_data(e));
        lv_obj_t *sw = static_cast<lv_obj_t *>(lv_event_get_target(e));
        Settings::save<bool>(setting->type, lv_obj_has_state(sw, LV_STATE_CHECKED));
        if (setting->type == Settings::CONN_SAVER) {
          Control::getInstance().setConnSaver(lv_obj_has_state(sw, LV_STATE_CHECKED));
        }
      },
      LV_EVENT_VALUE_CHANGED, const_cast<Settings::setting_t *>(&s));

  if (setting == Settings::IR) {
    lv_obj_add_event_cb(
        sw,
        [](lv_event_t *e) {
          auto *ui = static_cast<UI *>(lv_event_get_user_data(e));
          ui->updateIRMenuVisibility();
        },
        LV_EVENT_VALUE_CHANGED, this);
  }

  if (setting == Settings::PRESET_PICKER) {
    lv_obj_add_event_cb(
        sw,
        [](lv_event_t *e) {
          auto *ui = static_cast<UI *>(lv_event_get_user_data(e));
          auto *sw = static_cast<lv_obj_t *>(lv_event_get_target(e));
          ui->setPresetPicker(lv_obj_has_state(sw, LV_STATE_CHECKED));
        },
        LV_EVENT_VALUE_CHANGED, this);
  }

  if (setting == Settings::GPS) {
    lv_obj_add_event_cb(
        sw,
        [](lv_event_t *e) {
          auto *status = static_cast<status_t *>(lv_event_get_user_data(e));
          status->gps->reloadSetting();
          showGPSWidgets(status, status->gps->isEnabled());
        },
        LV_EVENT_VALUE_CHANGED, &m_Status);
  }

  if (setting == Settings::SD_GPX) {
    m_StorageGPXSwitch = sw;
    lv_obj_add_event_cb(
        sw,
        [](lv_event_t *) {
          // the SD writer task applies the change, never mount from the UI task
          GPS::getInstance().reloadLogSettings();
          SD::getInstance().request(SD::request_t::RELOAD);
        },
        LV_EVENT_VALUE_CHANGED, NULL);
  }

  if (setting == Settings::SHOW_TITLE) {
    lv_obj_add_event_cb(
        sw,
        [](lv_event_t *e) {
          auto *ui = static_cast<UI *>(lv_event_get_user_data(e));
          auto *sw = static_cast<lv_obj_t *>(lv_event_get_target(e));
          ui->setShowTitle(lv_obj_has_state(sw, LV_STATE_CHECKED));
        },
        LV_EVENT_VALUE_CHANGED, this);
  }

  if (setting == Settings::RECONNECT) {
    if (!enable) {
      lv_obj_add_flag(m_Status.reconnectIcon, LV_OBJ_FLAG_HIDDEN);
    }

    lv_obj_add_event_cb(
        sw,
        [](lv_event_t *e) {
          auto *sw = static_cast<lv_obj_t *>(lv_event_get_target(e));
          auto *status = static_cast<status_t *>(lv_event_get_user_data(e));
          if (lv_obj_has_state(sw, LV_STATE_CHECKED)) {
            lv_obj_clear_flag(status->reconnectIcon, LV_OBJ_FLAG_HIDDEN);
            if (status->reconnectBackoff != nullptr) {
              lv_obj_remove_state(status->reconnectBackoff, LV_STATE_DISABLED);
            }
          } else {
            lv_obj_add_flag(status->reconnectIcon, LV_OBJ_FLAG_HIDDEN);
            if (status->reconnectBackoff != nullptr) {
              lv_obj_add_state(status->reconnectBackoff, LV_STATE_DISABLED);
            }
          }
        },
        LV_EVENT_VALUE_CHANGED, &m_Status);
  }

  if (setting == Settings::RECON_BACKOFF) {
    m_Status.reconnectBackoff = sw;
    if (!Settings::load<Settings::RECONNECT>()) {
      lv_obj_add_state(sw, LV_STATE_DISABLED);
    }
  }

  if (setting == Settings::COMPANION) {
    lv_obj_add_event_cb(
        sw,
        [](lv_event_t *e) {
          auto *ui = static_cast<UI *>(lv_event_get_user_data(e));
          auto *sw = static_cast<lv_obj_t *>(lv_event_get_target(e));
          if (lv_obj_has_state(sw, LV_STATE_CHECKED)) {
            Companion::getInstance().reloadSetting(true);
            ui->startCompanionPairingTimer();
          } else {
            Companion::getInstance().reloadSetting(false);
            ui->stopCompanionPairingTimer();
          }
        },
        LV_EVENT_VALUE_CHANGED, this);
  }
}

void UI::updateMultiConnectButton(lv_obj_t *button) {
  if (button == nullptr) {
    return;
  }

  size_t selected = 0;
  for (size_t n = 0; n < CameraList::size(); n++) {
    if (CameraList::get(n)->isActive()) {
      selected++;
    }
  }

  lv_obj_t *label = lv_obj_get_child(button, 0);
  if (label != nullptr) {
    char text[24];
    snprintf(text, sizeof(text), "Connect %u", static_cast<unsigned>(selected));
    if (strcmp(lv_label_get_text(label), text) != 0) {
      lv_label_set_text(label, text);
    }
  }

  if (selected == 0) {
    lv_obj_add_state(button, LV_STATE_DISABLED);
  } else {
    lv_obj_remove_state(button, LV_STATE_DISABLED);
  }
}

void UI::saveMultiConnectSelection(void) {
  Settings::multiselect_t selection = {};

  for (size_t n = 0; n < CameraList::size(); n++) {
    auto camera = CameraList::get(n);
    if (!camera->isActive() || (selection.count >= Settings::MULTISELECT_MAX)) {
      continue;
    }

    snprintf(selection.name[selection.count], Settings::MULTISELECT_NAME_MAX, "%s",
             camera->getName().c_str());
    selection.count++;
  }

  // skip the NVS write when the remembered set is unchanged
  const Settings::multiselect_t stored = Settings::load<Settings::MULTISELECT>();
  if (memcmp(&stored, &selection, sizeof(selection)) != 0) {
    Settings::save<Settings::MULTISELECT>(selection);
  }
}

lv_obj_t *UI::addCameraItem(size_t index, const menu_t &menu, const CameraListMode_t mode) {
  bool checkbox = (mode == MODE_MULTICONNECT);

  if (index >= CameraList::size()) {
    return nullptr;
  }

  auto camera = CameraList::get(index);
  lv_obj_t *item = addMenuItem(menu, NULL, camera->getName().c_str(), checkbox);

  // Stash the CameraList index, not a raw Camera pointer. A raw pointer would
  // dangle once CameraList::load() frees and rebuilds the list, so a stale menu
  // item click would be a use-after-free. The event callbacks resolve the index
  // back to a Camera fresh from CameraList, so a stale entry resolves to nothing
  // (out of range) instead of a freed pointer.
  void *ctx = reinterpret_cast<void *>(static_cast<uintptr_t>(index));

  switch (mode) {
    case MODE_DELETE:
      lv_obj_add_event_cb(
          item,
          [](lv_event_t *e) {
            size_t index =
                static_cast<size_t>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
            if (index >= CameraList::size()) {
              return;
            }
            CameraList::remove(CameraList::get(index).get());
            refreshDelete();
          },
          LV_EVENT_CLICKED, ctx);
      break;
    case MODE_SCAN:
    case MODE_CONNECT:
      lv_obj_add_event_cb(
          item,
          [](lv_event_t *e) {
            size_t index =
                static_cast<size_t>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
            if (index >= CameraList::size()) {
              return;
            }
            CameraList::get(index)->setActive(true);

            doConnect(e);
          },
          LV_EVENT_CLICKED, ctx);
      break;
    case MODE_MULTICONNECT:
      lv_obj_add_event_cb(
          item,
          [](lv_event_t *e) {
            size_t index =
                static_cast<size_t>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
            if (index >= CameraList::size()) {
              return;
            }
            auto *check = static_cast<lv_obj_t *>(lv_event_get_target(e));

            CameraList::get(index)->setActive(lv_obj_has_state(check, LV_STATE_CHECKED));

            lv_obj_t *container = lv_obj_get_parent(check);
            lv_obj_t *page = container == nullptr ? nullptr : lv_obj_get_parent(container);
            lv_obj_t *button = page == nullptr ? nullptr : lv_obj_get_child(page, 0);
            updateMultiConnectButton(button);
          },
          LV_EVENT_VALUE_CHANGED, ctx);
      break;
  }

  return item;
}

UI::menu_t &UI::addMenu(const char *name,
                        const lv_image_dsc_t *icon,
                        bool button,
                        const menu_t &parent) {
  menu_t &menu = m_Menu.at(name);
  menu.main = m_MainMenu.main;
  menu.group = m_Group;
  menu.page = lv_menu_page_create(m_MainMenu.main, name);

  if (button) {
    menu.button = addMenuItem(parent, icon, name, false, menu.grid.column, menu.grid.row);
  }

  // Every menu and sub page is built here during startup, all on the main task
  // before the UI loop begins. Yield once per page so the synchronous build
  // never holds CPU0 long enough to starve IDLE0 and trip the task watchdog.
  bootYield();

  return menu;
}

void UI::addMainMenu(void) {
  lv_obj_update_layout(m_Content);
  lv_obj_set_scrollbar_mode(m_Content, LV_SCROLLBAR_MODE_OFF);

  m_MainMenu.main = lv_menu_create(m_Content);
  if (M5.Touch.isEnabled()) {
    lv_menu_set_mode_header(m_MainMenu.main, LV_MENU_HEADER_BOTTOM_FIXED);
  } else {
    lv_menu_set_mode_header(m_MainMenu.main, LV_MENU_HEADER_TOP_FIXED);
  }

  lv_menu_set_mode_root_back_button(m_MainMenu.main, LV_MENU_ROOT_BACK_BUTTON_DISABLED);
  lv_obj_t *back = lv_menu_get_main_header_back_button(m_MainMenu.main);
  addToInputGroup(m_Group, back);

  // Cache the menu header title label so a mid-session reconnect can rewrite the
  // connected page title in place (Connected -> Reconnecting). lv_menu keeps a
  // single header title label, populated from the current page on load, so we
  // find it once here by its type rather than a brittle child index.
  lv_obj_t *header = lv_menu_get_main_header(m_MainMenu.main);
  if (header != nullptr) {
    for (uint32_t i = 0; i < lv_obj_get_child_count(header); i++) {
      lv_obj_t *child = lv_obj_get_child(header, i);
      if (lv_obj_check_type(child, &lv_label_class)) {
        m_Status.menuTitle = child;
        break;
      }
    }
  }
#if defined(FURBLE_M5COREX) || defined(FURBLE_M5STICKC_PLUS) || defined(FURBLE_M5STICKS3)
  // StickC display too narrow for icons
  lv_obj_t *back_img = lv_obj_get_child(back, 0);
  lv_image_set_src(back_img, &icon_undo);
#if defined(FURBLE_M5COREX)
  lv_obj_set_width(back_img, 48);
#else
  lv_obj_set_width(back_img, 24);
#endif
#endif

  lv_obj_set_size(m_MainMenu.main, LV_PCT(100), LV_PCT(100));
  lv_obj_center(m_MainMenu.main);

  m_MainMenu.page = lv_menu_page_create(m_MainMenu.main, NULL);
  m_MainMenu.group = m_Group;

#if defined(FURBLE_M5COREX)
  lv_obj_set_grid_dsc_array(m_MainMenu.page, m_GridLayoutColDsc.data(), m_GridLayoutRowDsc.data());
  lv_obj_set_layout(m_MainMenu.page, LV_LAYOUT_GRID);
#else
#endif
  lv_obj_set_size(m_MainMenu.page, LV_PCT(100), LV_PCT(100));
  lv_obj_center(m_MainMenu.page);

  addConnectMenu();
  addScanMenu();
  addDeleteMenu();
  addIRMenu();
  addSettingsMenu();
  addConnectedMenu();

  menu_t &off = addMenu(m_PowerOffStr, &icon_power_settings_new);

  lv_obj_add_event_cb(
      off.button,
      [](lv_event_t *e) {
        auto *ui = static_cast<UI *>(lv_event_get_user_data(e));
        ui->doPowerOff();
      },
      LV_EVENT_CLICKED, this);

  lv_obj_add_event_cb(
      m_MainMenu.main,
      [](lv_event_t *e) {
        auto *ui = static_cast<UI *>(lv_event_get_user_data(e));
        auto *target = static_cast<lv_obj_t *>(lv_event_get_target(e));
        auto *page = lv_menu_get_cur_main_page(target);
        auto *back = lv_menu_get_main_header_back_button(m_MainMenu.main);
        auto &scan = Scan::getInstance();

        // A roller or slider can leave the shared encoder group in edit mode.
        // Menu pages always start in navigation mode so left/right can reach
        // the shared header back button.
        lv_group_set_editing(ui->m_Group, false);

        // LVGL hides the header back button on the root page. Only re-enable and
        // un-hide it on sub-pages so the encoder can reach it. On the root page
        // keep it hidden so no stray back arrow appears on the home screen.
        if (page != m_MainMenu.page) {
          lv_obj_remove_state(back, LV_STATE_DISABLED);
          lv_obj_clear_flag(back, LV_OBJ_FLAG_HIDDEN);
        } else {
          lv_obj_add_flag(back, LV_OBJ_FLAG_HIDDEN);
        }

        // the diagnostics values only refresh while one of their pages is open
        if ((page == m_Menu.at(m_AboutStr).page) || (page == m_Menu.at(m_DeviceInfoStr).page)
            || (page == m_Menu.at(m_PowerStateStr).page) || (page == m_Menu.at(m_BLEStr).page)) {
          lv_timer_resume(ui->m_DiagnosticsTimer);
          lv_timer_ready(ui->m_DiagnosticsTimer);
        } else {
          lv_timer_pause(ui->m_DiagnosticsTimer);
        }

        bool presetPage =
            (page == m_Menu.at(m_BulbDurationStr).page) && ui->m_Bulb.m_Duration.usesPresetPicker();
        if (presetPage) {
          ui->configureControl(ControlMode::PRESET);
        } else if (ui->m_ControlMode == ControlMode::PRESET) {
          ui->configureControl(ControlMode::MENU);
        }

        // the Cameras rows only refresh while their page is open
        if (page == m_Menu.at(m_CamerasStr).page) {
          rebuildCamerasPage(m_Menu.at(m_CamerasStr));
          lv_timer_resume(m_CamerasTimer);
        } else {
          lv_timer_pause(m_CamerasTimer);
        }

        // a bulb exposure only runs on its own page, never strand a held shutter
        if (page != m_Menu.at(m_BulbRunStr).page) {
          ui->bulbStop();
        }

        if (page == m_MainMenu.page) {
          size_t saveCount = CameraList::getSaveCount();
          ui->m_MainCount++;

          // Hide connect & delete if there are zero saved
          if (saveCount == 0) {
            lv_obj_add_state(m_Menu.at(m_ConnectStr).button, LV_STATE_DISABLED);
            lv_obj_add_state(m_Menu.at(m_DeleteStr).button, LV_STATE_DISABLED);
            lv_group_focus_obj(m_Menu.at(m_ScanStr).button);
          } else {
            lv_obj_remove_state(m_Menu.at(m_ConnectStr).button, LV_STATE_DISABLED);
            lv_obj_remove_state(m_Menu.at(m_DeleteStr).button, LV_STATE_DISABLED);
          }

          // Ensure no active scans
          scan.stop();

          // If enabled and connections exist, auto connect to first camera on first display of main
          // menu
          if ((saveCount > 0) && (ui->m_MainCount == 1)
              && Settings::load<Settings::AUTOCONNECT>()) {
            CameraList::load();
            auto camera = CameraList::get(0);
            camera->setActive(true);
            doConnect(e);
          }
        } else if (page == m_Menu.at(m_DeleteStr).page) {
        } else if (page == m_Menu.at(m_ScanStr).page) {
          startScan();
        } else if (page == m_Menu.at(m_SettingsStr).page) {
        } else if (page == m_Menu.at(m_BatteryStr).page) {
          // refresh the battery page on entry rather than waiting for the timer
          lv_timer_ready(ui->m_BatteryTimer);
        } else if (page == m_Menu.at(m_ConnectStr).page) {
          // ensure menu control
          // especially if arrived here from a disconnect/cancel
          ui->configureControl(ControlMode::MENU);
        } else if (page == m_Menu.at(m_ConnectedStr).page) {
          // Ensure no active scans
          scan.stop();

          // only offer GPS data when GPS is enabled
          if (ui->m_GPS.isEnabled()) {
            lv_obj_clear_flag(m_Menu.at(m_RemoteGPSData).button, LV_OBJ_FLAG_HIDDEN);
          } else {
            lv_obj_add_flag(m_Menu.at(m_RemoteGPSData).button, LV_OBJ_FLAG_HIDDEN);
          }

          // hide and disable back button
          lv_obj_add_state(back, LV_STATE_DISABLED);
          lv_obj_add_flag(back, LV_OBJ_FLAG_HIDDEN);
        } else if (page == m_Menu.at(m_RemoteShutter).page) {
          if (M5.Touch.isEnabled()) {
            // if touch screen, enable back
            lv_obj_remove_state(back, LV_STATE_DISABLED);
          } else {
            // hide the back button
            lv_obj_add_flag(back, LV_OBJ_FLAG_HIDDEN);
          }
        } else if ((page == m_Menu.at(m_RemoteBulb).page)
                   || (page == m_Menu.at(m_BulbRunStr).page)) {
          // bulb is reachable from the connected page, always display 'Back'
          lv_obj_remove_state(back, LV_STATE_DISABLED);
          lv_obj_clear_flag(back, LV_OBJ_FLAG_HIDDEN);
        } else if (page == m_Menu.at(m_IntervalometerStr).page) {
          // always display 'Back' in intervalometer
          lv_obj_remove_state(back, LV_STATE_DISABLED);
        } else if (page == m_Menu.at(m_IntervalometerRunStr).page) {
          // disable 'Back' while the intervalometer is running so the run cannot
          // be orphaned by navigating away; 'Stop' is the only way out
          lv_obj_add_state(back, LV_STATE_DISABLED);
        } else if ((page == m_Menu.at(m_GPSDataStr).page)
                   || (page == m_Menu.at(m_CamerasStr).page)) {
          // These pages are reachable from the connected page, always display 'Back'
          lv_obj_remove_state(back, LV_STATE_DISABLED);
          lv_obj_clear_flag(back, LV_OBJ_FLAG_HIDDEN);
        }
      },
      LV_EVENT_VALUE_CHANGED, this);

  lv_obj_add_event_cb(
      m_MainMenu.main,
      [](lv_event_t *e) {
        auto *ui = static_cast<UI *>(lv_event_get_user_data(e));
        auto *target = static_cast<lv_obj_t *>(lv_event_get_current_target(e));
        auto *page = lv_menu_get_cur_main_page(target);

        if (page == m_MainMenu.page) {
        } else if (page == m_Menu.at(m_DeleteStr).page) {
        } else if (page == m_Menu.at(m_SettingsStr).page) {
        } else if (page == m_Menu.at(m_ConnectedStr).page) {
          lv_obj_t *back = lv_menu_get_main_header_back_button(m_MainMenu.main);
          if (lv_obj_has_state(back, LV_STATE_FOCUS_KEY)) {
            lv_group_focus_next(lv_group_get_default());
          }
          ui->shutterUnlock(Control::getInstance());
        }
      },
      LV_EVENT_CLICKED, this);

  lv_menu_set_page(m_MainMenu.main, m_MainMenu.page);
}

void UI::bootYield(void) {
#if !defined(FURBLE_SIM)
  // The task loop has not started yet, so nothing else touches LVGL here and a
  // one tick sleep is safe. It hands CPU0 to IDLE0 long enough to reset the
  // ESP-IDF task watchdog while the menu tree is built.
  vTaskDelay(1);
#endif
}

void UI::displayNavigationBar(bool show) {
  if (!M5.Touch.isEnabled()) {
    if (show) {
      lv_obj_clear_flag(m_NavBar, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(m_Left, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(m_OK, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(m_Right, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(m_NavBar, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(m_Left, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(m_OK, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(m_Right, LV_OBJ_FLAG_HIDDEN);
    }
  }
}

void UI::configureControl(ControlMode mode, bool set) {
  switch (mode) {
    case ControlMode::MENU:
      if (set) {
        m_ControlMode = ControlMode::MENU;
      }
      configMenuControl();
      break;
    case ControlMode::SHUTTER:
      if (set) {
        m_ControlMode = ControlMode::SHUTTER;
      }
      configShutterControl();
      break;
    case ControlMode::SLIDER:
      if (set) {
        m_ControlMode = ControlMode::SLIDER;
      }
      configSliderControl();
      break;
    case ControlMode::PRESET:
      if (set) {
        m_ControlMode = ControlMode::PRESET;
      }
      configPresetControl();
      break;
    case ControlMode::REVERT:
      configureControl(m_ControlMode);
      break;
  }
}

#if defined(FURBLE_SIM)
void UI::simScenarioAction(const char *action) {
  const std::string command = action == nullptr ? "" : action;
  if (command == "blind") {
    lv_menu_set_page(m_MainMenu.main, m_Menu.at(m_RemoteShutter).page);
    configureControl(ControlMode::SHUTTER);
    return;
  }

  if (command == "blind-shutter") {
    auto &control = Control::getInstance();
    control.sendCommand(Control::CMD_SHUTTER_PRESS);
    control.sendCommand(Control::CMD_SHUTTER_RELEASE);
    return;
  }

  // Replay LVGL's click-focus decision for the shutter button indicators. On a
  // real device the physical buttons drive these indicators through a
  // LV_INDEV_TYPE_BUTTON device; on press LVGL runs indev_click_focus, which
  // sends LV_EVENT_FOCUSED only to a click focusable object and clears it only
  // on the next press of another object. Mirroring that exact flag gate here
  // lets a headless run prove the indicators no longer latch the green focus
  // outline after a release.
  if (command == "indicator-click-focus") {
    for (lv_obj_t *indicator : {m_Left, m_OK, m_Right}) {
      if (indicator != nullptr && lv_obj_has_flag(indicator, LV_OBJ_FLAG_CLICK_FOCUSABLE)) {
        lv_obj_send_event(indicator, LV_EVENT_FOCUSED, this);
      }
    }
    return;
  }

  // Focus the Remote page shutter lock so a scenario can confirm its focus
  // outline. The sim always renders the touch remote layout, where key
  // navigation does not land on this floating button, so focus it directly.
  if (command == "focus-lock") {
    if (m_ShutterLockIcon != nullptr) {
      lv_obj_add_state(m_ShutterLockIcon,
                       static_cast<lv_state_t>(LV_STATE_FOCUSED | LV_STATE_FOCUS_KEY));
    }
    return;
  }

  // Drive the real connect flow the Scan and Connect buttons trigger. The
  // connect timer then advances the state machine and reveals the connected
  // page, exactly as it does for an on-device button press.
  if (command == "connect") {
    if (CameraList::size() == 0) {
      CameraList::addFauxNY();
    }
    auto camera = CameraList::last();
    if (camera != nullptr) {
      camera->setActive(true);
    }
    doConnect(nullptr);
    return;
  }

  // Connect two cameras so a multi-connect drop can be exercised: one camera
  // dropping must not blank the trigger for the other. Seeds a second FauxNY
  // camera, selects both, and drives the same connect flow as the on-device
  // multi-select screen.
  if (command == "connect-two") {
    while (CameraList::size() < 2) {
      CameraList::addFauxNY();
    }
    for (size_t n = 0; n < CameraList::size(); n++) {
      CameraList::get(n)->setActive(true);
    }
    doConnect(nullptr);
    return;
  }

  // Mirror the on-device disconnect button.
  if (command == "disconnect") {
    doDisconnect();
    return;
  }

  // Simulate a mid-session BLE link drop on an active link. Control leaves
  // STATE_ACTIVE, which the connect timer must observe so the UI reflects the
  // reconnect (task #54 / F3). "drop" drops every active link; "drop <n>" drops
  // only target n, so a multi-connect session can lose one camera and keep the
  // rest live.
  if (command == "drop" || command.rfind("drop ", 0) == 0) {
    int index = -1;
    if (command.size() > 5) {
      index = std::atoi(command.c_str() + 5);
    }
    Control::getInstance().simDropActiveLink(index);
    return;
  }

  // Click the real Cancel button on the connect message box, running the same
  // handler an on-device press would. Used to prove one Cancel click fires
  // exactly one disconnect regardless of prior connect attempts (F4).
  if (command == "cancel") {
    if (m_ConnectContext.cancel != nullptr) {
      lv_obj_send_event(m_ConnectContext.cancel, LV_EVENT_CLICKED, this);
    }
    return;
  }

  // Fire the shutter through the real shutter button handler.
  if (command == "shutter") {
    lv_obj_send_event(m_OK, LV_EVENT_PRESSED, this);
    lv_obj_send_event(m_OK, LV_EVENT_RELEASED, this);
    return;
  }

  // Select the main-button behavior mode. BUTTON_MODE is a string roller, not a
  // boolean switch, so it has no toggle entry. handleButtonMode reads the stored
  // value live on each event, so a later gesture picks up the change.
  if (command == "button-mode one-button") {
    Settings::save<std::string>(Settings::BUTTON_MODE, Settings::BUTTON_MODE_ONE_BUTTON_VALUE);
    return;
  }
  if (command == "button-mode two-button") {
    Settings::save<std::string>(Settings::BUTTON_MODE, Settings::BUTTON_MODE_TWO_BUTTON_VALUE);
    return;
  }

  // Single press and hold of the main button (gesture 1). The press is held
  // past the long-press threshold, so one-button mode dispatches focus, held
  // until release. A quick tap alone is a no-op by design.
  if (command == "main-press-hold") {
    lv_obj_send_event(m_OK, LV_EVENT_PRESSED, this);
    lv_obj_send_event(m_OK, LV_EVENT_LONG_PRESSED, this);
    lv_obj_send_event(m_OK, LV_EVENT_RELEASED, this);
    return;
  }

  // Double click of the main button (gesture 2). This replays the full real
  // event stream so the dispatch's focus-leak path is exercised: the first
  // click is a genuine press, release and short click (streak one), then the
  // second press and release drive the shutter tap. The fixed handler must
  // dispatch exactly one shutter press and release and leak no focus. The sim
  // cannot reproduce LVGL's real streak timing, so it injects the streak the
  // short-click classification records.
  if (command == "main-double-click") {
    m_SimClickStreakActive = true;
    m_SimClickStreak = 1;
    lv_obj_send_event(m_OK, LV_EVENT_PRESSED, this);
    lv_obj_send_event(m_OK, LV_EVENT_RELEASED, this);
    lv_obj_send_event(m_OK, LV_EVENT_SHORT_CLICKED, this);
    lv_obj_send_event(m_OK, LV_EVENT_PRESSED, this);
    lv_obj_send_event(m_OK, LV_EVENT_RELEASED, this);
    m_SimClickStreakActive = false;
    return;
  }

  // Click then hold of the main button (gesture 3). The first click primes the
  // streak, then the second press is held past the long-press threshold. The
  // fixed handler must dispatch a single sustained shutter press and release,
  // and leak no focus.
  if (command == "main-click-hold") {
    m_SimClickStreakActive = true;
    m_SimClickStreak = 1;
    lv_obj_send_event(m_OK, LV_EVENT_PRESSED, this);
    lv_obj_send_event(m_OK, LV_EVENT_RELEASED, this);
    lv_obj_send_event(m_OK, LV_EVENT_SHORT_CLICKED, this);
    lv_obj_send_event(m_OK, LV_EVENT_PRESSED, this);
    lv_obj_send_event(m_OK, LV_EVENT_LONG_PRESSED, this);
    lv_obj_send_event(m_OK, LV_EVENT_RELEASED, this);
    m_SimClickStreakActive = false;
    return;
  }

  // Toggle a boolean setting through its real switch widget so the switch's
  // value-changed callback persists the new value, exactly as a button press
  // on that switch would.
  constexpr const char *TOGGLE_PREFIX = "toggle ";
  if (command.compare(0, std::char_traits<char>::length(TOGGLE_PREFIX), TOGGLE_PREFIX) == 0) {
    const std::string name = command.substr(std::char_traits<char>::length(TOGGLE_PREFIX));
    static const std::unordered_map<std::string, Settings::type_t> settings = {
        {"gps",           Settings::GPS          },
        {"gps_nmea",      Settings::GPS_NMEA     },
        {"autoconnect",   Settings::AUTOCONNECT  },
        {"reconnect",     Settings::RECONNECT    },
        {"multiconnect",  Settings::MULTICONNECT },
        {"companion",     Settings::COMPANION    },
#if defined(FURBLE_M5STICKS3)
        {"watchdog",      Settings::WATCHDOG     },
#endif
        {"ir",            Settings::IR           },
        {"show_title",    Settings::SHOW_TITLE   },
        {"tx_adaptive",   Settings::TX_ADAPTIVE  },
        {"conn_saver",    Settings::CONN_SAVER   },
        {"preset_picker", Settings::PRESET_PICKER},
        {"recon_backoff", Settings::RECON_BACKOFF},
    };
    const auto found = settings.find(name);
    if (found == settings.end()) {
      return;
    }
    const auto entry = g_simSettingSwitches.find(static_cast<int>(found->second));
    if (entry == g_simSettingSwitches.end() || entry->second == nullptr) {
      return;
    }
    lv_obj_t *widget = entry->second;
    if (lv_obj_has_state(widget, LV_STATE_CHECKED)) {
      lv_obj_remove_state(widget, LV_STATE_CHECKED);
    } else {
      lv_obj_add_state(widget, LV_STATE_CHECKED);
    }
    lv_obj_send_event(widget, LV_EVENT_VALUE_CHANGED, nullptr);
    return;
  }

  // Drive the real Start button so the run page loads and the interval timer
  // starts exactly as an on-device press does, including the run page Back
  // handling.
  if (command == "intervalometer") {
    lv_obj_send_event(m_IntervalStart, LV_EVENT_CLICKED, nullptr);
    return;
  }

  // Drive the real Stop button so the run state reset and back navigation run
  // exactly as an on-device press does.
  if (command == "stop") {
    lv_obj_send_event(m_IntervalStop, LV_EVENT_CLICKED, nullptr);
    return;
  }

  // Open the Bulb Duration page and step the exposure preset picker one 1/3-stop
  // entry, running the same handler the physical plus and minus keys drive. The
  // step snaps the stored bulb duration onto the series and persists it, so a
  // scenario can assert the picked preset survived through Settings::BULB.
  if (command == "preset-step-up" || command == "preset-step-down") {
    lv_menu_set_page(m_MainMenu.main, m_Menu.at(m_BulbDurationStr).page);
    presetStep(command == "preset-step-up" ? 1 : -1);
    return;
  }

  // Inject a pending companion pairing without a rig TCP peer, then make sure
  // the pairing timer is running so it raises the real modal. This lets the
  // input-after-approve regression (task #32) be reproduced headlessly. The pin
  // is fixed so captures stay deterministic.
  if (command == "companion-pair-request") {
    Sim::rigInjectPendingPairing(123456);
    startCompanionPairingTimer();
    return;
  }

  // Click a real companion pairing modal footer button, running the same
  // handler an on-device button press would. Footer child 0 is Accept, 1 is
  // Reject. Used to reproduce the input-after-approve regression (task #32).
  if (command == "companion-accept" || command == "companion-reject") {
    if (m_CompanionPairingDialog == nullptr || !lv_obj_is_valid(m_CompanionPairingDialog)) {
      return;
    }
    lv_obj_t *footer = lv_msgbox_get_footer(m_CompanionPairingDialog);
    if (footer == nullptr) {
      return;
    }
    const uint32_t index = command == "companion-accept" ? 0 : 1;
    lv_obj_t *button = lv_obj_get_child(footer, index);
    if (button != nullptr) {
      lv_obj_send_event(button, LV_EVENT_CLICKED, this);
    }
    return;
  }

  // Navigate to a page by clicking its real menu button, so LVGL records the
  // menu history and the header back button pops correctly. This reaches the
  // deep diagnostics pages (BLE, Power state, Device info) the position-based
  // key walks cannot easily target.
  constexpr const char *NAV_PREFIX = "nav ";
  if (command.compare(0, std::char_traits<char>::length(NAV_PREFIX), NAV_PREFIX) == 0) {
    const std::string name = command.substr(std::char_traits<char>::length(NAV_PREFIX));
    static const std::unordered_map<std::string, const char *> buttons = {
        {"connect",     m_ConnectStr       },
        {"scan",        m_ScanStr          },
        {"delete",      m_DeleteStr        },
        {"settings",    m_SettingsStr      },
        {"display",     m_DisplayStr       },
        {"features",    m_FeaturesStr      },
        {"infrared",    m_IRSettingsStr    },
        {"gps",         m_GPSStr           },
        {"gps_data",    m_GPSDataStr       },
        {"nmea",        m_GPSNMEAStr       },
        {"timer",       m_IntervalometerStr},
        {"theme",       m_ThemeStr         },
        {"text_size",   m_TextSizeStr      },
        {"bluetooth",   m_BluetoothStr     },
        {"about",       m_AboutStr         },
        {"power",       m_PowerStr         },
        {"feedback",    m_FeedbackStr      },
        {"diagnostics", m_DiagnosticsStr   },
        {"device_info", m_DeviceInfoStr    },
        {"power_state", m_PowerStateStr    },
        {"ble",         m_BLEStr           },
        {"battery",     m_BatteryStr       },
        {"storage",     m_StorageStr       },
    };
    const auto found = buttons.find(name);
    if (found == buttons.end()) {
      return;
    }
    // Some pages are not registered on every board panel (for example the
    // StickC 80x160 build omits several diagnostics pages). Look the entry up
    // rather than indexing, so a nav to an absent page is a graceful no-op
    // instead of aborting the sim.
    const auto entry = m_Menu.find(found->second);
    if (entry == m_Menu.end()) {
      return;
    }
    lv_obj_t *button = entry->second.button;
    if (button != nullptr) {
      lv_obj_send_event(button, LV_EVENT_CLICKED, this);
    }
    return;
  }

  // Scroll the current menu page so off-screen rows come into view. A settings
  // page taller than the panel only shows its top rows in one screenshot, so the
  // docs capture drives this between frames to picture every option. The scroll
  // runs on the live LVGL page the same way a touch drag or an encoder walk past
  // the last visible row does, so no shipping layout changes.
  //   scroll next    one viewport down, minus a small overlap so no row is skipped
  //   scroll top     back to the first row
  //   scroll bottom  all the way to the last row
  //   scroll <n>     n pixels down (negative scrolls up)
  constexpr const char *SCROLL_PREFIX = "scroll ";
  if (command.compare(0, std::char_traits<char>::length(SCROLL_PREFIX), SCROLL_PREFIX) == 0) {
    const std::string arg = command.substr(std::char_traits<char>::length(SCROLL_PREFIX));
    lv_obj_t *page = lv_menu_get_cur_main_page(m_MainMenu.main);
    if (page == nullptr) {
      return;
    }
    lv_obj_update_layout(page);
    if (arg == "top") {
      lv_obj_scroll_to_y(page, 0, LV_ANIM_OFF);
    } else if (arg == "bottom") {
      const int32_t below = lv_obj_get_scroll_bottom(page);
      if (below > 0) {
        lv_obj_scroll_by(page, 0, -below, LV_ANIM_OFF);
      }
    } else if (arg == "next") {
      // One viewport minus an overlap band keeps a couple of rows shared between
      // consecutive frames, so a row straddling the fold is never lost.
      const int32_t viewport = lv_obj_get_height(page);
      const int32_t overlap = viewport / 6;
      int32_t delta = viewport - overlap;
      if (delta < 1) {
        delta = viewport;
      }
      const int32_t below = lv_obj_get_scroll_bottom(page);
      if (delta > below) {
        delta = below;
      }
      if (delta > 0) {
        lv_obj_scroll_by(page, 0, -delta, LV_ANIM_OFF);
      }
    } else {
      lv_obj_scroll_by(page, 0, -std::atoi(arg.c_str()), LV_ANIM_OFF);
    }
    return;
  }

  constexpr const char *PAGE_PREFIX = "page ";
  if (command.compare(0, std::char_traits<char>::length(PAGE_PREFIX), PAGE_PREFIX) != 0) {
    return;
  }

  const std::string page_name = command.substr(std::char_traits<char>::length(PAGE_PREFIX));
  lv_obj_t *page = m_MainMenu.page;
  // Connected-session sub-pages, so the per-page connection-state sweep can land
  // on each place a user sits during a live session and confirm a drop is
  // surfaced there. Set through the real lv_menu page load so its per-page
  // handler runs, exactly as clicking the connected-page tile does.
  if (page_name == "shutter") {
    page = m_Menu.at(m_RemoteShutter).page;
  } else if (page_name == "bulb") {
    page = m_Menu.at(m_RemoteBulb).page;
  } else if (page_name == "cameras") {
    page = m_Menu.at(m_CamerasStr).page;
  } else if (page_name == "remote_timer") {
    page = m_Menu.at(m_RemoteInterval).page;
  } else if (page_name == "remote_gps") {
    page = m_Menu.at(m_RemoteGPSData).page;
  } else if (page_name == "connected") {
    page = m_Menu.at(m_ConnectedStr).page;
  } else if (page_name == "settings") {
    page = m_Menu.at(m_SettingsStr).page;
  } else if (page_name == "display") {
    page = m_Menu.at(m_DisplayStr).page;
  } else if (page_name == "features") {
    page = m_Menu.at(m_FeaturesStr).page;
  } else if (page_name == "gps") {
    page = m_Menu.at(m_GPSStr).page;
  } else if (page_name == "timer") {
    page = m_Menu.at(m_IntervalometerStr).page;
  } else if (page_name == "theme") {
    page = m_Menu.at(m_ThemeStr).page;
  } else if (page_name == "text_size") {
    page = m_Menu.at(m_TextSizeStr).page;
  } else if (page_name == "bluetooth") {
    page = m_Menu.at(m_BluetoothStr).page;
  } else if (page_name == "about") {
    page = m_Menu.at(m_AboutStr).page;
  } else if (page_name == "power") {
    page = m_Menu.at(m_PowerStr).page;
  } else if (page_name == "diagnostics") {
    page = m_Menu.at(m_DiagnosticsStr).page;
  }
  lv_menu_set_page(m_MainMenu.main, page);
}

std::string UI::simQueryState(const char *key) {
  const std::string query = key == nullptr ? "" : key;

  if (query == "connect_box") {
    const bool hidden = m_ConnectContext.messageBox == nullptr
                        || lv_obj_has_flag(m_ConnectContext.messageBox, LV_OBJ_FLAG_HIDDEN);
    return hidden ? "hidden" : "visible";
  }

  // Whether any shutter button indicator currently latches LV_STATE_FOCUSED, the
  // green focus outline that must not survive a button press or release. The
  // indicators are pure hints, so this must always read "no".
  if (query == "indicators_focused") {
    for (lv_obj_t *indicator : {m_Left, m_OK, m_Right}) {
      if (indicator != nullptr && lv_obj_has_state(indicator, LV_STATE_FOCUSED)) {
        return "yes";
      }
    }
    return "no";
  }

  // Persisted bulb exposure duration in milliseconds. Lets a scenario confirm
  // that stepping the exposure preset picker snapped and saved Settings::BULB.
  if (query == "bulb_ms") {
    SpinValue::nvs_t nvs = Settings::load<Settings::BULB>();
    SpinValue value(nvs);
    return std::to_string(value.toMilliseconds());
  }

  if (query == "disconnect_calls") {
    return std::to_string(g_simDisconnectCalls);
  }

  // Whether the non-blocking mid-session reconnect indicator is showing. It must
  // appear while a live link is being reconnected and clear once it is back, all
  // without the connect progress box taking over the screen.
  if (query == "reconnecting") {
    const bool hidden = m_Status.reconnectingIcon == nullptr
                        || lv_obj_has_flag(m_Status.reconnectingIcon, LV_OBJ_FLAG_HIDDEN);
    return hidden ? "no" : "yes";
  }

  // The current menu header title text. On the connected page it reads
  // "Connected" while active and "Reconnecting" (or "Reconnecting (i/n)") during
  // a mid-session reconnect, so a scenario can assert the text tracks the drop
  // and clears on recovery, independently of the reconnecting icon.
  if (query == "status_text") {
    if (m_Status.menuTitle == nullptr) {
      return "none";
    }
    const char *text = lv_label_get_text(m_Status.menuTitle);
    return (text != nullptr) ? std::string(text) : std::string("none");
  }

  // The reconnect count carried in the "Reconnecting (i/n)" title as a single
  // token "i/n": i cameras of the n in the session are currently down. Uses the
  // exact counts the title formats from, so a scenario can assert the per-device
  // count without matching a string that contains a space.
  if (query == "reconnect_count") {
    auto &control = Control::getInstance();
    const size_t total = control.getTargetCount();
    const size_t connected = control.getConnectedTargetCount();
    const size_t down = (total > connected) ? (total - connected) : 0;
    return std::to_string(down) + "/" + std::to_string(total);
  }

  // The Remote shutter page reconnect banner. Reports "hidden" when the banner
  // is not showing, otherwise its label text ("Reconnecting" or "Reconnecting
  // (i/n)"), so a scenario can assert the shutter page surfaces a mid-session
  // drop with the per-device count and clears it on recovery, independently of
  // the connected-page title. The label text is the full string regardless of
  // any wrap on the narrow panel, so the assertion holds on every board.
  if (query == "remote_status") {
    const bool hidden =
        m_RemoteReconnect == nullptr || lv_obj_has_flag(m_RemoteReconnect, LV_OBJ_FLAG_HIDDEN);
    if (hidden) {
      return "hidden";
    }
    if (m_RemoteReconnectLabel == nullptr) {
      return "shown";
    }
    const char *text = lv_label_get_text(m_RemoteReconnectLabel);
    return (text != nullptr) ? std::string(text) : std::string("shown");
  }

  // Token-safe visibility of the shutter page reconnect banner ("yes"/"no"), so
  // a multi-connect scenario can assert it is showing without matching the label
  // text that carries a space in "Reconnecting (i/n)". Pair with reconnect_count
  // for the per-device count.
  if (query == "remote_reconnecting") {
    const bool hidden =
        m_RemoteReconnect == nullptr || lv_obj_has_flag(m_RemoteReconnect, LV_OBJ_FLAG_HIDDEN);
    return hidden ? "no" : "yes";
  }

  // Whether the connect liveness timer is parked. It must pause once the link is
  // fully down so it does not spin forever on a torn-down connection, and keep
  // running while active or reconnecting so a drop is observed.
  if (query == "connect_timer") {
    if (m_ConnectTimer == nullptr) {
      return "none";
    }
    return lv_timer_get_paused(m_ConnectTimer) ? "paused" : "running";
  }

  if (query == "connected") {
    // The UI only presents a connected camera once the progress box is gone,
    // the connected page is showing, and control reports an active link. A
    // stale-connected regression would fail at least one of these checks.
    lv_obj_t *page = lv_menu_get_cur_main_page(m_MainMenu.main);
    const bool onConnectedPage = (page == m_Menu.at(m_ConnectedStr).page);
    const bool boxHidden = m_ConnectContext.messageBox == nullptr
                           || lv_obj_has_flag(m_ConnectContext.messageBox, LV_OBJ_FLAG_HIDDEN);
    const bool active = Control::getInstance().getState() == Control::STATE_ACTIVE;
    return (onConnectedPage && boxHidden && active) ? "yes" : "no";
  }

  if (query == "page") {
    lv_obj_t *page = lv_menu_get_cur_main_page(m_MainMenu.main);
    if (page == m_MainMenu.page) {
      return "main";
    }
    const std::array<std::pair<const char *, const char *>, 25> pages = {
        {
         {m_ConnectStr, "connect"},
         {m_ConnectedStr, "connected"},
         {m_ScanStr, "scan"},
         {m_SettingsStr, "settings"},
         {m_RemoteShutter, "shutter"},
         {m_RemoteBulb, "bulb"},
         {m_BulbRunStr, "bulb_run"},
         {m_CamerasStr, "cameras"},
         {m_RemoteInterval, "remote_timer"},
         {m_RemoteGPSData, "remote_gps"},
         {m_RemoteDisconnect, "remote_disconnect"},
         {m_IntervalometerStr, "timer"},
         {m_IntervalometerRunStr, "timer_run"},
         {m_FeaturesStr, "features"},
         {m_DisplayStr, "display"},
         {m_TextSizeStr, "text_size"},
         {m_GPSStr, "gps"},
         {m_GPSDataStr, "gps_data"},
         {m_GPSNMEAStr, "nmea"},
         {m_BluetoothStr, "bluetooth"},
         {m_AboutStr, "about"},
         {m_DiagnosticsStr, "diagnostics"},
         {m_DeviceInfoStr, "device_info"},
         {m_PowerStateStr, "power_state"},
         {m_BLEStr, "ble"},
         }
    };
    for (const auto &entry : pages) {
      if (page == m_Menu.at(entry.first).page) {
        return entry.second;
      }
    }
    return "other";
  }

  // State of the shared header back button. A page that leaves it "hidden" with
  // no other way out is a navigation dead end (task #34 class).
  if (query == "back") {
    lv_obj_t *back = lv_menu_get_main_header_back_button(m_MainMenu.main);
    if (back == nullptr) {
      return "none";
    }
    if (lv_obj_has_flag(back, LV_OBJ_FLAG_HIDDEN)) {
      return "hidden";
    }
    if (lv_obj_has_state(back, LV_STATE_DISABLED)) {
      return "disabled";
    }
    return "visible";
  }

  // Companion pairing modal presence, for the input-after-approve regression
  // (task #32).
  if (query == "modal") {
    const bool open =
        m_CompanionPairingDialog != nullptr && lv_obj_is_valid(m_CompanionPairingDialog);
    return open ? "open" : "closed";
  }

  // Boot splash outcome. "shown" once the splash drew and advanced every stage
  // during boot; "off" when the toggle disabled it. Lets a scenario assert the
  // splash ran and then the main menu was reached.
  if (query == "boot_splash") {
    if (!BootScreen::wasShown()) {
      return "off";
    }
    return (BootScreen::stepsShown() >= 6) ? "shown" : "partial";
  }

  // Whether the companion pairing modal's Accept button is in the encoder group
  // and currently focused. This locks in the focus contract the modal relies on
  // (task #32 class). Note: in this LVGL build the msgbox footer buttons are
  // auto-added to the default group, so this alone does not go red against the
  // pre-fix code.
  if (query == "modal_focus") {
    if (m_CompanionPairingDialog == nullptr || !lv_obj_is_valid(m_CompanionPairingDialog)) {
      return "closed";
    }
    lv_obj_t *footer = lv_msgbox_get_footer(m_CompanionPairingDialog);
    lv_obj_t *accept = footer == nullptr ? nullptr : lv_obj_get_child(footer, 0);
    if (accept == nullptr) {
      return "no";
    }
    const bool inGroup = lv_obj_get_group(accept) == m_Group;
    const bool focused = lv_group_get_focused(m_Group) == accept;
    return (inGroup && focused) ? "yes" : "no";
  }

  // Number of live pairing message boxes on the top layer. A modal message box
  // built with lv_msgbox_create(nullptr) sits inside a backdrop that is a direct
  // child of the top layer. More than one means a re-entrancy stacked or
  // orphaned a dialog (task #32 class); the timer guard should keep this at one.
  if (query == "modal_count") {
    int count = 0;
    lv_obj_t *top = lv_layer_top();
    for (uint32_t i = 0; i < lv_obj_get_child_count(top); i++) {
      lv_obj_t *backdrop = lv_obj_get_child(top, i);
      for (uint32_t j = 0; j < lv_obj_get_child_count(backdrop); j++) {
        if (lv_obj_check_type(lv_obj_get_child(backdrop, j), &lv_msgbox_class)) {
          count++;
        }
      }
    }
    return std::to_string(count);
  }

  // Whether the encoder group currently has a valid focused object. After a
  // modal closes, a null or stale focus means the input group is trapped and no
  // button can be reached (task #32 class).
  if (query == "focus") {
    lv_obj_t *focused = lv_group_get_focused(m_Group);
    if (focused == nullptr) {
      return "none";
    }
    if (!lv_obj_is_valid(focused)) {
      return "stale";
    }
    return "ok";
  }

  // Whether the focused object sits on the page that is currently displayed. A
  // "no" means the focus escaped the visible page and the encoder drives an
  // off-screen widget.
  if (query == "focus_on_page") {
    lv_obj_t *focused = lv_group_get_focused(m_Group);
    if (focused == nullptr || !lv_obj_is_valid(focused)) {
      return "no";
    }
    lv_obj_t *page = lv_menu_get_cur_main_page(m_MainMenu.main);
    for (lv_obj_t *parent = focused; parent != nullptr; parent = lv_obj_get_parent(parent)) {
      if (parent == page) {
        return "yes";
      }
    }
    return "no";
  }

  // Report whether the current page's content is taller than its viewport, i.e.
  // it needs scrolling. Combined with a screenshot this flags layout overflow
  // on the narrow panels.
  if (query == "overflow") {
    lv_obj_t *page = lv_menu_get_cur_main_page(m_MainMenu.main);
    if (page == nullptr) {
      return "unknown";
    }
    lv_obj_update_layout(page);
    // Scrollable content that extends above or below the viewport means the
    // page does not fit and must be scrolled. On the narrow panels this flags
    // pages that overflow the display.
    const int32_t below = lv_obj_get_scroll_bottom(page);
    const int32_t above = lv_obj_get_scroll_top(page);
    return (below > 0 || above > 0) ? "yes" : "no";
  }

  // Pixels of content still below the current viewport, so a scroll scenario can
  // assert it reached the last row (0) after driving "scroll bottom" or a run of
  // "scroll next" steps. Clamped at 0 so a fully scrolled page never reads
  // negative.
  if (query == "scroll_bottom") {
    lv_obj_t *page = lv_menu_get_cur_main_page(m_MainMenu.main);
    if (page == nullptr) {
      return "unknown";
    }
    lv_obj_update_layout(page);
    const int32_t below = lv_obj_get_scroll_bottom(page);
    return std::to_string(below > 0 ? below : 0);
  }

  // Pixels of content scrolled above the current viewport, so a scenario can
  // assert "scroll top" returned the page to its first row (0).
  if (query == "scroll_top") {
    lv_obj_t *page = lv_menu_get_cur_main_page(m_MainMenu.main);
    if (page == nullptr) {
      return "unknown";
    }
    lv_obj_update_layout(page);
    const int32_t above = lv_obj_get_scroll_top(page);
    return std::to_string(above > 0 ? above : 0);
  }

  // Report the Text size roller's current selection so scenarios can assert the
  // saved TEXT_SIZE setting was loaded and reflected in the widget on boot.
  // "text_size" reports the roller selection; "text_size_options" reports how
  // many sizes the roller offers, so a scenario can assert the small board drops
  // Large from the list.
  if (query == "text_size" || query == "text_size_options") {
    const auto entry = m_Menu.find(m_TextSizeStr);
    if (entry == m_Menu.end() || entry->second.page == nullptr) {
      return "unknown";
    }
    std::function<lv_obj_t *(lv_obj_t *)> findRoller = [&](lv_obj_t *obj) -> lv_obj_t * {
      if (lv_obj_check_type(obj, &lv_roller_class)) {
        return obj;
      }
      for (uint32_t i = 0; i < lv_obj_get_child_count(obj); i++) {
        if (lv_obj_t *found = findRoller(lv_obj_get_child(obj, i))) {
          return found;
        }
      }
      return nullptr;
    };
    lv_obj_t *roller = findRoller(entry->second.page);
    if (roller == nullptr) {
      return "unknown";
    }
    if (query == "text_size_options") {
      // The true option count, not the inflated string an infinite-mode roller
      // repeats internally to fake the wrap-around.
      return std::to_string(lv_roller_get_option_count(roller));
    }
    return std::to_string(lv_roller_get_selected(roller));
  }

  // Report the intervalometer run state so scenarios can assert a clean reset
  // after Stop.
  if (query == "interval_state") {
    switch (m_Intervalometer.m_State) {
      case Intervalometer::STATE_IDLE:
        return "idle";
      case Intervalometer::STATE_WAIT:
        return "wait";
      case Intervalometer::STATE_SHUTTER_OPEN:
        return "shutter";
      case Intervalometer::STATE_DELAY:
        return "delay";
      case Intervalometer::STATE_FINISHED:
        return "finished";
    }
    return "unknown";
  }

  // Read the rendered GPS Data page labels so scenarios can assert the speed
  // line and the five decimal place coordinates. The values come from the
  // actual label text, so a precision or missing line regression fails here.
  // Speed is returned without its "km/h" unit and coordinates without the
  // degree sign, keeping each result a single whitespace free token.
  if (query == "gps_speed" || query == "gps_lat" || query == "gps_lon") {
    lv_obj_t *page = m_Menu.at(m_GPSDataStr).page;
    std::string speed, lat, lon;
    int coordinates = 0;
    for (uint32_t i = 0; page != nullptr && i < lv_obj_get_child_count(page); i++) {
      lv_obj_t *child = lv_obj_get_child(page, i);
      if (!lv_obj_check_type(child, &lv_label_class)) {
        continue;
      }
      const std::string text = lv_label_get_text(child);
      const size_t degree = text.find("°");
      if (text.find("km/h") != std::string::npos) {
        speed = text.substr(0, text.find(' '));
      } else if (degree != std::string::npos) {
        (coordinates++ == 0 ? lat : lon) = text.substr(0, degree);
      }
    }
    if (query == "gps_speed") {
      return speed;
    }
    return query == "gps_lat" ? lat : lon;
  }

  // Count how many widgets in the focused item's subtree currently render a
  // focus outline. Menu rows set LV_OBJ_FLAG_STATE_TRICKLE, so a focused row
  // propagates LV_STATE_FOCUSED to its child icon and label. If the outline
  // style is attached to those children they each draw their own ring, so an
  // icon menu row would report three. The selected item must show a single ring
  // around the whole item, so this is exactly one.
  if (query == "focus_outline_count") {
    lv_obj_t *focused = lv_group_get_focused(m_Group);
    if (focused == nullptr || !lv_obj_is_valid(focused)) {
      return "none";
    }
    std::function<int(lv_obj_t *)> countOutlined = [&](lv_obj_t *obj) -> int {
      int count = lv_obj_get_style_outline_width(obj, LV_PART_MAIN) > 0 ? 1 : 0;
      for (uint32_t i = 0; i < lv_obj_get_child_count(obj); i++) {
        count += countOutlined(lv_obj_get_child(obj, i));
      }
      return count;
    };
    return std::to_string(countOutlined(focused));
  }

  // Effective focus outline width of the Remote page shutter lock, in its
  // current state. Pair with the focus-lock action to confirm the lock shows no
  // focus ring: the button is the only focusable control on the page, so the
  // ring was pure noise.
  if (query == "lock_outline") {
    if (m_ShutterLockIcon == nullptr || !lv_obj_is_valid(m_ShutterLockIcon)) {
      return "none";
    }
    return std::to_string(lv_obj_get_style_outline_width(m_ShutterLockIcon, LV_PART_MAIN));
  }

  // Is a mid-session link loss surfaced ANYWHERE on the page the user is looking
  // at right now? "yes" when the shared status-row reconnecting icon is showing,
  // or, on the full-screen Remote shutter page, when its dedicated banner is up.
  // This is the per-page coverage guard for the connection-state sweep: every
  // page reachable during a connected session must answer "yes" while a target
  // is down and "no" once it is back, so a drop is never silently swallowed on
  // the page in front of the user (the class of bug that left the shutter page
  // blank before PR #154).
  if (query == "link_alert") {
    const bool headerIcon = m_Status.reconnectingIcon != nullptr
                            && !lv_obj_has_flag(m_Status.reconnectingIcon, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *page = lv_menu_get_cur_main_page(m_MainMenu.main);
    const bool onShutter = (page == m_Menu.at(m_RemoteShutter).page);
    const bool banner = onShutter && m_RemoteReconnect != nullptr
                        && !lv_obj_has_flag(m_RemoteReconnect, LV_OBJ_FLAG_HIDDEN);
    return (headerIcon || banner) ? "yes" : "no";
  }

  // Whether the CURRENT page carries a dedicated in-page reconnect banner that is
  // showing. A full-screen page that hides the status row (the Remote shutter and
  // bulb pages) cannot rely on the shared header icon, so it needs its own banner
  // overlaid on the page. Reports:
  //   "none"  the page is not a full-screen remote page, so the header row serves
  //   "yes"   a dedicated banner is showing on this full-screen page
  //   "no"    this full-screen page has no dedicated banner showing
  // The shutter page got its banner in PR #154; the full-screen Bulb and Bulb-run
  // pages still have none, so the sweep marks their "yes" expectation WILL_FAIL
  // until a bulb-page reconnect banner lands.
  if (query == "page_banner") {
    lv_obj_t *page = lv_menu_get_cur_main_page(m_MainMenu.main);
    const bool onShutter = (page == m_Menu.at(m_RemoteShutter).page);
    const bool onBulb =
        (page == m_Menu.at(m_RemoteBulb).page) || (page == m_Menu.at(m_BulbRunStr).page);
    if (!onShutter && !onBulb) {
      return "none";
    }
    if (onShutter) {
      const bool shown =
          m_RemoteReconnect != nullptr && !lv_obj_has_flag(m_RemoteReconnect, LV_OBJ_FLAG_HIDDEN);
      return shown ? "yes" : "no";
    }
    // Bulb and Bulb-run have no dedicated banner object yet.
    return "no";
  }

  // Recolor state of the shared status-row Bluetooth icon that marks a mid-session
  // reconnect. Reports "hidden" when it is not showing, "red" when it is showing
  // recolored red (the on-screen "the link is down" cue), and "plain" when it is
  // showing in the default icon tint. The reconnect icon appears on a drop but is
  // not yet recolored red in the status row (only the shutter-page banner is), so
  // the status-bar matrix marks the "red" expectation WILL_FAIL until the red-BT
  // fix lands.
  if (query == "bt_icon") {
    lv_obj_t *icon = m_Status.reconnectingIcon;
    if (icon == nullptr || lv_obj_has_flag(icon, LV_OBJ_FLAG_HIDDEN)) {
      return "hidden";
    }
    const lv_color_t recolor = lv_obj_get_style_image_recolor(icon, LV_PART_MAIN);
    const lv_opa_t opa = lv_obj_get_style_image_recolor_opa(icon, LV_PART_MAIN);
    const lv_color_t red = lv_palette_main(LV_PALETTE_RED);
    return (opa >= LV_OPA_50 && lv_color_eq(recolor, red)) ? "red" : "plain";
  }

  // Header battery icon x position, for the status-bar layout-stability matrix.
  //   "battery_x"      the icon's current x within the header, absolute
  //   "battery_drift"  x minus the anchor captured on the FIRST battery_drift
  //                    read, so a scenario asserts 0 at its baseline state and
  //                    then 0 again across every connection and GPS state
  // The battery currently rides a left-packed flex row, so showing or hiding the
  // reconnect, reconnecting or GPS icon to its left shoves it sideways; the
  // matrix marks the post-baseline "0" expectations WILL_FAIL until the battery
  // is anchored so its position no longer depends on the icons beside it. The
  // anchor is a function-local static: one sim process runs one scenario, so it
  // is established once per run and is FURBLE_SIM only.
  if (query == "battery_x" || query == "battery_drift") {
    if (m_Status.batteryIcon == nullptr) {
      return "none";
    }
    lv_obj_update_layout(m_Header);
    const int32_t x = lv_obj_get_x(m_Status.batteryIcon);
    if (query == "battery_x") {
      return std::to_string(x);
    }
    static int32_t anchor = INT32_MIN;
    if (anchor == INT32_MIN) {
      anchor = x;
    }
    return std::to_string(x - anchor);
  }

  return "";
}
#endif

void UI::setPresetPicker(bool enabled) {
  m_Bulb.m_Duration.setPresetPicker(enabled);
}

void UI::configShutterControl(void) {
  if (!M5.Touch.isEnabled()) {
    lv_obj_set_style_bg_image_src(m_Left, &icon_arrow_back_24, 0);
    lv_obj_set_style_bg_image_src(m_Right, &icon_center_focus_strong_24, 0);
    lv_obj_set_style_bg_image_src(m_OK, &icon_camera_24, 0);

    lv_indev_set_type(m_ButtonL, LV_INDEV_TYPE_BUTTON);
    lv_indev_set_type(m_ButtonO, LV_INDEV_TYPE_BUTTON);
    lv_indev_set_type(m_ButtonR, LV_INDEV_TYPE_BUTTON);

    // map indev to button coords
    lv_area_t left;
    lv_area_t ok;
    lv_area_t right;

    lv_obj_get_coords(m_Left, &left);
    lv_obj_get_coords(m_OK, &ok);
    lv_obj_get_coords(m_Right, &right);

    static const lv_point_t leftPoint[] = {
        {(left.x1 + left.x2) / 2, (left.y1 + left.y2) / 2}
    };
    static const lv_point_t okPoint[] = {
        {(ok.x1 + ok.x2) / 2, (ok.y1 + ok.y2) / 2}
    };
    static const lv_point_t rightPoint[] = {
        {(right.x1 + right.x2) / 2, (right.y1 + right.y2) / 2}
    };

    lv_indev_set_button_points(m_ButtonL, leftPoint);
    lv_indev_set_button_points(m_ButtonO, okPoint);
    lv_indev_set_button_points(m_ButtonR, rightPoint);
  }
}

void UI::configMenuControl(void) {
  lv_group_set_editing(m_Group, false);
  if (!M5.Touch.isEnabled()) {
    lv_obj_set_style_bg_image_src(m_Left, &icon_arrow_upward_24, 0);
    lv_obj_set_style_bg_image_src(m_OK, &icon_check_24, 0);
    lv_obj_set_style_bg_image_src(m_Right, &icon_arrow_downward_24, 0);

    lv_indev_set_type(m_ButtonL, LV_INDEV_TYPE_ENCODER);
    lv_indev_set_type(m_ButtonO, LV_INDEV_TYPE_ENCODER);
    lv_indev_set_type(m_ButtonR, LV_INDEV_TYPE_ENCODER);
  }
}

void UI::configSliderControl(void) {
  if (!M5.Touch.isEnabled()) {
    lv_obj_set_style_bg_image_src(m_Left, &icon_arrow_back_24, 0);
    lv_obj_set_style_bg_image_src(m_OK, &icon_check_24, 0);
    lv_obj_set_style_bg_image_src(m_Right, &icon_arrow_forward_24, 0);
  }
}

void UI::configPresetControl(void) {
  if (!M5.Touch.isEnabled()) {
    configShutterControl();
    lv_obj_set_style_bg_image_src(m_Left, LV_SYMBOL_MINUS, 0);
    lv_obj_set_style_bg_image_src(m_OK, &icon_check_24, 0);
    lv_obj_set_style_bg_image_src(m_Right, LV_SYMBOL_PLUS, 0);
  }
}

void UI::presetStep(int direction) {
  auto *page = lv_menu_get_cur_main_page(m_MainMenu.main);
  if (page == m_Menu.at(m_BulbDurationStr).page) {
    m_Bulb.m_Duration.stepPreset(direction);
  }
}

void UI::presetConfirm(void) {
  auto *page = lv_menu_get_cur_main_page(m_MainMenu.main);
  if (page == m_Menu.at(m_BulbDurationStr).page) {
    lv_obj_t *back = lv_menu_get_main_header_back_button(m_MainMenu.main);
    lv_obj_send_event(back, LV_EVENT_CLICKED, m_MainMenu.main);
    configureControl(ControlMode::MENU);
  }
}

void UI::showShutterIntervalometer(bool show) {
  if (show) {
    lv_obj_clear_flag(m_IntervalStart, LV_OBJ_FLAG_HIDDEN);
    lv_group_focus_obj(m_IntervalStart);
  } else {
    lv_obj_add_flag(m_IntervalStart, LV_OBJ_FLAG_HIDDEN);
  }
}

void UI::updateReconnectTitle(bool reconnecting) {
  lv_obj_t *label = m_Status.menuTitle;
  if (label == nullptr) {
    return;
  }

  // Only rewrite the title while the connected page owns the header. On any sub
  // page (Shutter, Intervalometer, GPS Data, ...) the header shows that page's
  // own name and must be left untouched; the reconnecting icon still signals the
  // retry from the status row regardless of page.
  lv_obj_t *page = lv_menu_get_cur_main_page(m_MainMenu.main);
  if (page != m_Menu.at(m_ConnectedStr).page) {
    return;
  }

  if (!reconnecting) {
    setLabelTextIfChanged(label, m_ConnectedStr);
    return;
  }

  auto &control = Control::getInstance();
  const size_t total = control.getTargetCount();
  const size_t connected = control.getConnectedTargetCount();
  const size_t down = (total > connected) ? (total - connected) : 0;
  if (total > 1) {
    // "Reconnecting (i/n)": i cameras of the n in the session are currently down
    // and reconnecting. The survivors keep their links and stay usable.
    setLabelTextFmtIfChanged(label, "Reconnecting (%u/%u)", static_cast<unsigned>(down),
                             static_cast<unsigned>(total));
  } else {
    setLabelTextIfChanged(label, "Reconnecting");
  }
}

void UI::updateRemoteReconnect(bool reconnecting) {
  if (m_RemoteReconnect == nullptr) {
    return;
  }

  if (!reconnecting) {
    // Guarded inside showStatusIcon: the hidden flag is only toggled when it
    // actually changes, so a per-tick liveness poll never re-invalidates.
    showStatusIcon(m_RemoteReconnect, false);
    return;
  }

  // Reuse the same per-target connection state that drives updateReconnectTitle
  // so the shutter-page banner and the connected-page title never disagree.
  auto &control = Control::getInstance();
  const size_t total = control.getTargetCount();
  const size_t connected = control.getConnectedTargetCount();
  const size_t down = (total > connected) ? (total - connected) : 0;
  if (total > 1) {
    setLabelTextFmtIfChanged(m_RemoteReconnectLabel, "Reconnecting (%u/%u)",
                             static_cast<unsigned>(down), static_cast<unsigned>(total));
  } else {
    setLabelTextIfChanged(m_RemoteReconnectLabel, "Reconnecting");
  }
  showStatusIcon(m_RemoteReconnect, true);
}

void UI::connectTimerHandler(lv_timer_t *timer) {
  FURBLE_SIM_TIMER_FIRE("connect_timer");
  // Fast cadence while a connect is in progress so the progress bar animates
  // smoothly. Slow liveness cadence once active: the timer keeps running to
  // observe a mid-session drop, but polls gently instead of busy spinning.
  static constexpr uint32_t CONNECT_POLL_PERIOD_MS = 50;
  static constexpr uint32_t LIVENESS_POLL_PERIOD_MS = 500;
  auto *ctx = static_cast<ConnectContext_t *>(lv_timer_get_user_data(timer));
  auto &control = Control::getInstance();
  std::shared_ptr<Camera> camera;
  auto state = control.getState();

  // A drop out of the active state is a disconnect no matter which state
  // follows: with infinite reconnect the control re-enters connecting without
  // ever passing through idle. Clearing the flag here also re-arms the
  // connected signal for a successful reconnect, and guards against double
  // signaling with doDisconnect().
  if ((state != Control::STATE_ACTIVE) && ctx->feedbackConnected) {
    Feedback::getInstance().signal(Feedback::DISCONNECTED);
    ctx->feedbackConnected = false;
  }

  switch (state) {
    case Control::STATE_CONNECT:
    case Control::STATE_CONNECTING:
      lv_timer_set_period(m_ConnectTimer, CONNECT_POLL_PERIOD_MS);
      camera = control.getConnectingCamera();

      if (ctx->sessionEstablished) {
        // Mid-session reconnect. A live session already owns the screen, so do
        // not take it over with the progress box. Surface a non-blocking
        // indicator in the status row instead, keeping the connected view and
        // its trigger usable for any cameras still connected (task #54, and the
        // multi-connect hardware feedback).
        showStatusIcon(ctx->ui->m_Status.reconnectingIcon, true);
        // Match the on-screen text to the icon: the connected page title reads
        // "Reconnecting" (or "Reconnecting (i/n)" for a multi-connect session)
        // instead of still claiming the link is up.
        ctx->ui->updateReconnectTitle(true);
        // Same state on the Remote shutter page, the other place shots are
        // taken: a red Bluetooth icon plus the matching "Reconnecting" text.
        ctx->ui->updateRemoteReconnect(true);
      } else {
        // Initial connect: nothing is on screen yet, so present the progress
        // box that owns the connect flow until the first link goes active.
        if (lv_obj_has_flag(ctx->messageBox, LV_OBJ_FLAG_HIDDEN)) {
          // hide menu, unhide message box
          lv_obj_add_flag(m_MainMenu.main, LV_OBJ_FLAG_HIDDEN);
          lv_obj_clear_flag(ctx->messageBox, LV_OBJ_FLAG_HIDDEN);
          ctx->ui->displayNavigationBar(false);
          ctx->ui->configureControl(ControlMode::MENU, false);

          // Camera::connect() can hold Camera::m_Mutex for the whole attempt,
          // so stop the Cameras page polling isConnected() until this box hides
          lv_timer_pause(m_CamerasTimer);

          lv_group_focus_obj(ctx->cancel);
        }

        if (camera != nullptr) {
          // This runs every 50 ms while connecting. Only touch the widgets when
          // their value actually changes: an unconditional set relabels and
          // redraws the progress box on every tick even when nothing moved.
          const std::string &name = camera->getName();
          if (ctx->connectingName != name) {
            ctx->connectingName = name;
            lv_label_set_text(ctx->label, name.c_str());
          }
          const int32_t progress = camera->getConnectProgress();
          if (ctx->connectProgress != progress) {
            ctx->connectProgress = progress;
            lv_bar_set_value(ctx->bar, progress, LV_ANIM_ON);
          }
        }
      }
      break;

    case Control::STATE_CONNECT_FAILED:
      ESP_LOGE("ui", "Connection failed.");
      doDisconnect();
      break;

    case Control::STATE_ACTIVE:
      if (!ctx->feedbackConnected) {
        Feedback::getInstance().signal(Feedback::CONNECTED);
        ctx->feedbackConnected = true;
      }

      if (!lv_obj_has_flag(ctx->messageBox, LV_OBJ_FLAG_HIDDEN)) {
        // if from scan, save the connection
        if (ctx->menuName == m_ScanStr) {
          for (const auto &target : control.getTargets()) {
            CameraList::save(target->getCamera().get());
          }
          ctx->menuName = NULL;
        }

        // everything connected, display menu, hide connection message box
        lv_obj_add_flag(ctx->messageBox, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(m_MainMenu.main, LV_OBJ_FLAG_HIDDEN);
        ctx->ui->displayNavigationBar(true);
        ctx->ui->configureControl(ControlMode::REVERT);
        lv_group_focus_next(lv_group_get_default());

        // resume the Cameras page refresh if it is the visible page
        if (lv_menu_get_cur_main_page(m_MainMenu.main) == m_Menu.at(m_CamerasStr).page) {
          rebuildCamerasPage(m_Menu.at(m_CamerasStr));
          lv_timer_resume(m_CamerasTimer);
        }
      }
      // The link is live: remember the session so a later drop shows the
      // non-blocking indicator instead of taking over the screen, and clear
      // that indicator now the link is back.
      ctx->sessionEstablished = true;
      showStatusIcon(ctx->ui->m_Status.reconnectingIcon, false);
      // Link is back: restore the connected page title from "Reconnecting".
      ctx->ui->updateReconnectTitle(false);
      // Link is back: clear the Remote shutter page banner too.
      ctx->ui->updateRemoteReconnect(false);
      // Do not pause. A paused timer never observes a mid-session link drop, so
      // the screen would keep showing connected while control drops and retries
      // (task #54). Keep polling liveness at a gentle cadence: a drop re-enters
      // the connecting branch above and shows the reconnecting indicator, and a
      // successful reconnect restores this connected view.
      lv_timer_set_period(m_ConnectTimer, LIVENESS_POLL_PERIOD_MS);
      break;

    case Control::STATE_IDLE:
    case Control::STATE_DISCONNECTING:
      // The disconnect feedback fired above on leaving the active state. The
      // session is fully down: forget it and drop the reconnecting indicator.
      ctx->sessionEstablished = false;
      showStatusIcon(ctx->ui->m_Status.reconnectingIcon, false);
      // Session fully down: clear any lingering "Reconnecting" title back to the
      // default so a later connect starts from the normal "Connected" label.
      ctx->ui->updateReconnectTitle(false);
      // Session fully down: clear the Remote shutter page banner too.
      ctx->ui->updateRemoteReconnect(false);
      // Defensive: doDisconnect() already pauses on the interactive path, and a
      // legitimate reconnect passes through STATE_CONNECT/CONNECTING, not idle.
      // If Control ever drops straight from active to idle, pause here too so
      // the liveness poll self-heals instead of spinning on a torn-down link.
      lv_timer_pause(m_ConnectTimer);
      break;
  }
}

uint8_t UI::getIntervalometerState(void) {
  // Keep the protocol values explicit even if the private enum changes later.
  switch (m_IntervalometerState.load()) {
    case 0:  // STATE_IDLE
      return 0;
    case 1:  // STATE_WAIT
      return 1;
    case 2:  // STATE_SHUTTER_OPEN
      return 2;
    case 3:  // STATE_DELAY
      return 3;
    case 4:  // STATE_FINISHED
      return 4;
    default:
      return 0;
  }
}

uint16_t UI::getIntervalometerRemaining(void) {
  return m_IntervalometerRemaining.load();
}

int32_t UI::getBatteryLevel(void) {
  return M5.Power.getBatteryLevel();
}

int16_t UI::getBatteryVoltage(void) {
  return M5.Power.getBatteryVoltage();
}

int32_t UI::getBatteryCurrent(void) {
  return M5.Power.getBatteryCurrent();
}

int16_t UI::getBatteryVBUSVoltage(void) {
  return M5.Power.getVBUSVoltage();
}

bool UI::isBatteryCharging(void) {
  return static_cast<int>(M5.Power.isCharging()) == 1;
}

void UI::intervalometer(lv_timer_t *timer) {
  FURBLE_SIM_TIMER_FIRE("intervalometer_timer");
  auto &control = Control::getInstance();
  auto *interval = static_cast<Intervalometer *>(lv_timer_get_user_data(timer));
  uint32_t next = 0;

  static uint32_t count = 0;

  m_IntervalometerState.store(static_cast<uint8_t>(interval->m_State));

  if (interval->m_Count.m_SpinValue.m_Unit == SpinValue::UNIT_INF) {
    lv_label_set_text_fmt(interval->m_CountLabel, "%09lu", count);
  } else {
    lv_label_set_text_fmt(interval->m_CountLabel, "%03lu/%03u", count,
                          interval->m_Count.m_SpinValue.m_Value);
  }

  switch (interval->m_State) {
    case Intervalometer::STATE_IDLE:
      count = 0;
      m_IntervalCountdownActive = false;
      lv_label_set_text(interval->m_StateLabel, "IDLE");
      lv_timer_ready(timer);
      interval->m_State = Intervalometer::STATE_WAIT;
      break;

    case Intervalometer::STATE_WAIT:
      lv_label_set_text(interval->m_StateLabel, "WAIT");
      next = interval->m_Wait.m_SpinValue.toMilliseconds();
      m_IntervalCountdownActive = next > 0;
      m_IntervalLastAnnouncedSecond = 0;
      interval->m_State = Intervalometer::STATE_SHUTTER_OPEN;
      break;

    case Intervalometer::STATE_SHUTTER_OPEN:
      m_IntervalCountdownActive = false;
      m_IntervalLastAnnouncedSecond = 0;
      count++;
      lv_label_set_text(interval->m_StateLabel, "SHUTTER");
      control.sendCommand(Control::CMD_SHUTTER_PRESS);
      Feedback::getInstance().signal(Feedback::SHUTTER_FIRED);
      next = interval->m_Shutter.m_SpinValue.toMilliseconds();
      interval->m_State = Intervalometer::STATE_DELAY;
      break;

    case Intervalometer::STATE_DELAY:
      lv_label_set_text(interval->m_StateLabel, "DELAY");
      control.sendCommand(Control::CMD_SHUTTER_RELEASE);
      next = interval->m_Delay.m_SpinValue.toMilliseconds();
      m_IntervalLastAnnouncedSecond = 0;
      if (count >= interval->m_Count.m_SpinValue.m_Value) {
        m_IntervalCountdownActive = false;
        interval->m_State = Intervalometer::STATE_FINISHED;
      } else {
        // The delay precedes the next frame, so the countdown announces
        // before every frame, not only the first.
        m_IntervalCountdownActive = next > 0;
        interval->m_State = Intervalometer::STATE_SHUTTER_OPEN;
      }
      break;

    case Intervalometer::STATE_FINISHED:
      m_IntervalCountdownActive = false;
      lv_label_set_text(interval->m_StateLabel, "FINISHED");
      next = 0;
      lv_timer_pause(timer);
      break;
  }

  if (next > 0) {
    lv_timer_set_period(timer, next);
    m_IntervalNext = tick() + next;
  }

  m_IntervalometerState.store(static_cast<uint8_t>(interval->m_State));
  if (interval->m_Count.m_SpinValue.m_Unit == SpinValue::UNIT_INF) {
    m_IntervalometerRemaining.store(0xffff);
  } else {
    const uint32_t total = interval->m_Count.m_SpinValue.m_Value;
    m_IntervalometerRemaining.store(static_cast<uint16_t>(count >= total ? 0 : total - count));
  }
}

#if defined(FURBLE_CONSOLE)
QueueHandle_t UI::m_RequestQueue = NULL;

bool UI::sendRequest(Request request, int32_t arg) {
  if (m_RequestQueue == NULL) {
    return false;
  }

  const request_t item = {request, arg};

  return xQueueSend(m_RequestQueue, &item, 0) == pdTRUE;
}

void UI::serviceRequests(void) {
  request_t item;

  while (xQueueReceive(m_RequestQueue, &item, 0) == pdTRUE) {
    switch (item.request) {
      case Request::CONNECT:
        CameraList::load();
        if (item.arg >= 0) {
          // An index replaces whatever the multi-connect selection holds.
          for (size_t n = 0; n < CameraList::size(); n++) {
            CameraList::get(n)->setActive(false);
          }
          if (static_cast<size_t>(item.arg) >= CameraList::size()) {
            ESP_LOGE(LOG_TAG, "console: no camera at index %ld", item.arg);
            break;
          }
          CameraList::get(item.arg)->setActive(true);
        }
        doConnect(NULL);
        break;

      case Request::DISCONNECT:
        doDisconnect();
        break;

      case Request::SCAN:
        if (item.arg) {
          menu_t &menu = m_Menu.at(m_ScanStr);
          auto &scan = Scan::getInstance();

          lv_obj_clean(menu.page);
          CameraList::clear();

          if (Settings::load<Settings::FAUXNY>()) {
            CameraList::addFauxNY();
            updateItems(menu);
          }

          scan.clear();
          scan.start(
              [](void *param) {
                auto *menu = static_cast<menu_t *>(param);
                // Called asynchronously from the NimBLE scan thread.
                m_Mutex.lock();
                updateItems(*menu);
                m_Mutex.unlock();
              },
              &menu);
        } else {
          Scan::getInstance().stop();
        }
        break;

      case Request::CAMERAS:
        // CameraList is only ever touched from this task, so the console reads
        // it from here rather than racing the menus that rebuild it.
        if (item.arg) {
          CameraList::load();
        }
        printf("saved: %u\n", static_cast<unsigned>(CameraList::getSaveCount()));
        printf("count: %u\n", static_cast<unsigned>(CameraList::size()));
        for (size_t n = 0; n < CameraList::size(); n++) {
          const auto camera = CameraList::get(n);
          printf("camera%u.name: %s\n", static_cast<unsigned>(n), camera->getName().c_str());
          printf("camera%u.type: %lu\n", static_cast<unsigned>(n),
                 static_cast<unsigned long>(camera->getType()));
        }
        break;

      case Request::GPS_RELOAD:
        GPS::getInstance().reloadSetting();
        break;

      case Request::SD_RELOAD:
        GPS::getInstance().reloadLogSettings();
        SD::getInstance().request(SD::request_t::RELOAD);
        break;

      case Request::GPS_POWER:
        M5.Power.setExtOutput(item.arg != 0, m5::ext_PA);
        break;

      case Request::IR_RELOAD:
        updateIRMenuVisibility();
        break;

      case Request::FEEDBACK_RELOAD:
        Feedback::getInstance().reload();
        break;

      case Request::FEEDBACK_TEST:
        // Runs here because signal() touches the LVGL feedback timer.
        Feedback::getInstance().signal(static_cast<Feedback::event_t>(item.arg), true);
        break;

      case Request::PERF:
#if defined(CONFIG_LV_USE_PERF_MONITOR)
      {
        lv_display_t *display = lv_display_get_default();
        if (display == nullptr) {
          printf("error: no LVGL display\n");
          break;
        }

        if (item.arg < 0) {
          lv_sysmon_performance_dump(display);
          const auto &perf = display->perf_sysmon_info.calculated;
          printf("lvgl.fps: %lu\n", static_cast<unsigned long>(perf.fps));
          printf("lvgl.cpu_pct: %lu\n", static_cast<unsigned long>(perf.cpu));
          printf("lvgl.render_avg_ms: %lu\n", static_cast<unsigned long>(perf.render_avg_time));
          printf("lvgl.flush_avg_ms: %lu\n", static_cast<unsigned long>(perf.flush_avg_time));
        } else if (item.arg != 0) {
          lv_sysmon_show_performance(display);
          printf("lvgl.overlay: on\n");
        } else {
          lv_sysmon_hide_performance(display);
          printf("lvgl.overlay: off\n");
        }
        break;
      }
#else
        printf("error: LVGL performance monitor is not enabled\n");
        break;
#endif
      case Request::AUDIT:
        UIAudit::dump(lv_screen_active());
        break;

      case Request::POWER_RELOAD:
        m_ConnectContext.ui->reloadPowerPolicies();
        break;
#if !defined(FURBLE_NO_DISPLAY)
      case Request::DISPLAY_MODE:
        setDisplayMode(static_cast<uint8_t>(item.arg));
        break;
#endif
    }
  }
}
#endif

void UI::doConnect(lv_event_t *e) {
  auto &control = Control::getInstance();

  m_ConnectContext.feedbackConnected = false;
  // A fresh connect starts without a session, so the initial progress box owns
  // the screen until the first link goes active.
  m_ConnectContext.sessionEstablished = false;

  // Force the progress box to relabel on the first tick of this connect: the
  // widgets are reused across connects, so clear the guard cache.
  m_ConnectContext.connectingName.clear();
  m_ConnectContext.connectProgress = -1;

  // activate selected cameras
  for (auto n = 0; n < CameraList::size(); n++) {
    auto camera = CameraList::get(n);
    if (camera->isActive()) {
      control.addActive(camera);
    }
  }

  control.connectAll(Settings::load<Settings::RECONNECT>());
  lv_timer_ready(m_ConnectTimer);
  lv_timer_resume(m_ConnectTimer);

  menu_t &menu = m_Menu.at(m_ConnectedStr);
  lv_menu_set_page(m_MainMenu.main, menu.page);
}

void UI::doDisconnect(void) {
#if defined(FURBLE_SIM)
  g_simDisconnectCalls++;
#endif
  lv_timer_pause(m_ConnectTimer);
  if (m_CamerasTimer != nullptr) {
    lv_timer_pause(m_CamerasTimer);
  }

  // The session is being torn down: forget it and drop the reconnecting
  // indicator so it never lingers on the next connect.
  m_ConnectContext.sessionEstablished = false;
  showStatusIcon(m_ConnectContext.ui->m_Status.reconnectingIcon, false);
  m_ConnectContext.ui->updateRemoteReconnect(false);

  if (m_ConnectContext.feedbackConnected) {
    Feedback::getInstance().signal(Feedback::DISCONNECTED);
    m_ConnectContext.feedbackConnected = false;
  }

  // release a held bulb exposure before the connection goes away
  m_ConnectContext.ui->bulbStop();

  Scan::getInstance().stop();
  Control::getInstance().disconnect();

  // The shutter page is gone, drop blind remote so buttons wake the screen.
  m_ConnectContext.ui->m_ControlMode = ControlMode::MENU;

  lv_obj_add_flag(m_ConnectContext.messageBox, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(m_MainMenu.main, LV_OBJ_FLAG_HIDDEN);

  lv_menu_clear_history(m_MainMenu.main);
  lv_menu_set_page(m_MainMenu.main, m_MainMenu.page);
  menu_t &connect = m_Menu.at(m_ConnectStr);
  lv_group_focus_obj(connect.button);
}

void UI::updateCameraRow(lv_obj_t *label, Camera *camera, Control::state_t state) {
  const bool connected = camera->isConnected();
  char status[32];

  switch (state) {
    case Control::STATE_CONNECTING:
      if (connected) {
        snprintf(status, sizeof(status), "connected");
      } else {
        snprintf(status, sizeof(status), "reconnecting %u%%",
                 static_cast<unsigned>(camera->getConnectProgress()));
      }
      break;
    case Control::STATE_CONNECT:
      snprintf(status, sizeof(status), connected ? "connected" : "reconnecting");
      break;
    case Control::STATE_CONNECT_FAILED:
      snprintf(status, sizeof(status), connected ? "connected" : "lost");
      break;
    case Control::STATE_ACTIVE:
      snprintf(status, sizeof(status), connected ? "connected" : "reconnecting");
      break;
    case Control::STATE_DISCONNECTING:
      snprintf(status, sizeof(status), "disconnecting");
      break;
    case Control::STATE_IDLE:
      snprintf(status, sizeof(status), "idle");
      break;
  }

  // No live RSSI here. NimBLEClient::getRssi() is a blocking HCI round trip
  // and must not run on the render task. RSSI returns via the cached
  // connection statistics snapshot from PR #24 once that merges.
  char text[128];
  snprintf(text, sizeof(text), "%s   %s", camera->getName().c_str(), status);

  if (strcmp(lv_label_get_text(label), text) != 0) {
    lv_label_set_text(label, text);
  }
}

void UI::rebuildCamerasPage(menu_t &menu) {
  // Deliberately unlocked. Every caller (the page-change dispatch and the
  // camerasUpdate timer) already runs inside lv_task_handler() with m_Mutex
  // held by UI::task, and std::mutex is not recursive. updateItems differs
  // because the NimBLE scan thread calls it and must lock first.
  lv_obj_clean(menu.page);

  auto &control = Control::getInstance();
  const auto &targets = control.getTargets();
  const auto state = control.getState();
  for (const auto &target : targets) {
    lv_obj_t *label = lv_label_create(menu.page);
    lv_obj_set_width(label, LV_PCT(100));
    // The text is near static, so clip with dots instead of a circular
    // scroll that would invalidate the row on every tick.
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    updateCameraRow(label, target->getCamera().get(), state);
  }
}

void UI::camerasUpdate(lv_timer_t *timer) {
  auto *menu = static_cast<menu_t *>(lv_timer_get_user_data(timer));
  auto &control = Control::getInstance();

  // Self-pause outside STATE_ACTIVE. Camera::connect() holds the Camera
  // mutex for the whole attempt, so isConnected() below would block the
  // render task if a tick lands between a drop and connectTimerHandler
  // noticing. getState() only takes the state mutex and never blocks.
  if (control.getState() != Control::STATE_ACTIVE) {
    lv_timer_pause(timer);
    return;
  }

  const auto &targets = control.getTargets();

  if (lv_obj_get_child_count(menu->page) != targets.size()) {
    rebuildCamerasPage(*menu);
    return;
  }

  const auto state = control.getState();
  size_t n = 0;
  for (const auto &target : targets) {
    lv_obj_t *label = lv_obj_get_child(menu->page, n);
    updateCameraRow(label, target->getCamera().get(), state);
    n++;
  }
}

void UI::addCamerasMenu(const menu_t &parent) {
  menu_t &menu = addMenu(m_CamerasStr, &icon_linked_camera, true, parent);

  // The timer only runs while the page is visible. The page-change dispatch
  // in addMainMenu resumes and pauses it, following m_DiagnosticsTimer.
  m_CamerasTimer = lv_timer_create(camerasUpdate, 1000, &menu);
  lv_timer_pause(m_CamerasTimer);

  lv_menu_set_load_page_event(menu.main, menu.button, menu.page);
}

UI::menu_t &UI::addConnectedMenu(void) {
  menu_t &menuConnected = addMenu(m_ConnectedStr, NULL, false);

#if defined(FURBLE_M5COREX)
  // three by two suits the landscape screens, Cameras fills the last open cell
  static int32_t column_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1),
                                 LV_GRID_TEMPLATE_LAST};
  static int32_t row_dsc[] = {LV_GRID_CONTENT, LV_GRID_CONTENT, LV_GRID_TEMPLATE_LAST};
  lv_obj_set_grid_dsc_array(menuConnected.page, column_dsc, row_dsc);
  lv_obj_center(menuConnected.page);
  lv_obj_set_layout(menuConnected.page, LV_LAYOUT_GRID);
#endif

  menu_t &menuShutter = addMenu(m_RemoteShutter, &icon_remote_gen, true, menuConnected);
  menu_t &menuIR = m_Menu.at(m_IRStr);
  m_IRConnectedButton = addMenuItem(menuConnected, &icon_remote_gen, m_IRStr, false, 1, 1);
  lv_menu_set_load_page_event(menuIR.main, m_IRConnectedButton, menuIR.page);
  addCamerasMenu(menuConnected);
  addBulbMenu(menuConnected);
  menu_t &menuInterval = addMenu(m_RemoteInterval, &icon_timer, true, menuConnected);
  menu_t &menuGPSData = addMenu(m_RemoteGPSData, &icon_location_searching, true, menuConnected);
  menu_t &disconnect = addMenu(m_RemoteDisconnect, &icon_no_photography, true, menuConnected);

  if (M5.Touch.isEnabled()) {
    // add remote shutter control for touch screens
    lv_obj_t *cont = lv_menu_cont_create(menuShutter.page);

    static int32_t remote_col_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1),
                                       LV_GRID_TEMPLATE_LAST};
    static int32_t remote_row_dsc[] = {LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};

    lv_obj_set_grid_dsc_array(cont, remote_col_dsc, remote_row_dsc);
    lv_obj_set_size(cont, LV_PCT(100), LV_PCT(100));
    lv_obj_center(cont);

    static std::array<std::tuple<lv_obj_t *, lv_obj_t *, const char *, const lv_image_dsc_t *,
                                 const int32_t, const int32_t>,
                      3>
        buttons = {
            {
             {nullptr, nullptr, "Shutter\n", &icon_camera, 0, 0},
             {nullptr, nullptr, "Focus\n", &icon_center_focus_strong, 1, 0},
             {nullptr, nullptr, "Shutter\nLock", &icon_lock_open_right, 2, 0},
             }
    };

    for (auto &i : buttons) {
      auto &buttonCont = std::get<0>(i);

      buttonCont = lv_obj_create(cont);
      lv_obj_set_layout(buttonCont, LV_LAYOUT_FLEX);
      lv_obj_set_flex_flow(buttonCont, LV_FLEX_FLOW_COLUMN);
      lv_obj_set_flex_align(buttonCont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                            LV_FLEX_ALIGN_CENTER);
      lv_obj_clear_flag(buttonCont, LV_OBJ_FLAG_SCROLLABLE);
      lv_obj_set_size(buttonCont, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
      lv_obj_set_grid_cell(buttonCont, LV_GRID_ALIGN_STRETCH, std::get<4>(i), 1,
                           LV_GRID_ALIGN_STRETCH, std::get<5>(i), 1);

      auto &button = std::get<1>(i);
      button = lv_button_create(buttonCont);
      lv_obj_set_style_bg_image_src(button, std::get<3>(i), 0);
      lv_obj_set_size(button, 64, 64);

      lv_obj_t *label = lv_label_create(buttonCont);
      lv_label_set_text(label, std::get<2>(i));
      lv_obj_set_style_text_font(label, fontForIconMenu(Settings::load<Settings::TEXT_SIZE>()), 0);
      lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    }

    m_OK = std::get<1>(buttons[0]);
    m_Right = std::get<1>(buttons[1]);
    m_ShutterLockIcon = std::get<1>(buttons[2]);

    lv_obj_add_event_cb(m_OK, handleButtonMode, LV_EVENT_ALL, this);
    lv_obj_add_event_cb(m_Right, handleFocus, LV_EVENT_ALL, this);
    lv_obj_add_event_cb(m_ShutterLockIcon, handleShutterLock, LV_EVENT_ALL, this);
  } else {
    // add remote shutter text for buttons
    lv_area_t a;
    lv_obj_update_layout(menuShutter.page);
    lv_obj_get_coords(menuShutter.page, &a);
    lv_obj_clear_flag(menuShutter.page, LV_OBJ_FLAG_SCROLLABLE);

    m_ShutterLockIcon = lv_button_create(menuShutter.page);
    lv_obj_set_style_bg_image_src(m_ShutterLockIcon, &icon_lock_open_right_24, 0);
    lv_obj_set_style_radius(m_ShutterLockIcon, 0, LV_PART_MAIN);
    lv_obj_add_flag(m_ShutterLockIcon, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_size(m_ShutterLockIcon, ICON_HEADER_SIZE, ICON_HEADER_SIZE);

    // @todo Clean up the plethora of hardcoded values here
#if defined(FURBLE_M5STICKC)
    const size_t n = 3;
    int32_t x1 = lv_obj_get_x(m_OK) - 2;
    int32_t y1 = lv_obj_get_y(m_Right) - a.y1 - 10;
    static lv_point_precise_t points[] = {
        {x1 + 40, y1 + 12},
        {x1 + 6,  y1 + 12},
        {x1 + 6,  y1 + 64}
    };
#elif defined(FURBLE_M5STACK_CORE)
    const size_t n = 4;
    const int32_t x1 = 188;
    const int32_t y1 = 80;
    static lv_point_precise_t points[] = {
        {164, 153},
        {164, 92 },
        {82,  92 },
        {82,  153}
    };
#else
    const size_t n = 3;
    int32_t x1 = lv_obj_get_x(m_OK) - 2;
    int32_t y1 = lv_obj_get_y(m_Right) - a.y1 - 7;
    static lv_point_precise_t points[] = {
        {x1 + 50, y1 + 12 },
        {x1 - 1,  y1 + 12 },
        {x1 - 1,  y1 + 103}
    };
#endif

    lv_obj_set_pos(m_ShutterLockIcon, x1, y1);

    static lv_style_t style;
    lv_style_init(&style);
    lv_style_set_line_width(&style, 2);
    lv_style_set_line_color(&style, lv_palette_main(LV_PALETTE_GREY));
    lv_style_set_line_opa(&style, LV_OPA_50);

    lv_obj_t *line = lv_line_create(menuShutter.page);
    lv_line_set_points(line, points, n);
    lv_obj_add_style(line, &style, 0);

    lv_obj_move_foreground(m_ShutterLockIcon);
  }

  // Non-blocking reconnect banner overlaid on the Remote shutter page. The
  // header status row also carries the plain reconnecting icon, but the
  // full-screen shutter view is where shots are taken, so a mid-session drop is
  // surfaced here too with a clearer red Bluetooth icon plus "Reconnecting"
  // (or "Reconnecting (i/n)") text. Floating so it never joins the page layout
  // or its scroll extent, and hidden until updateRemoteReconnect shows it.
  m_RemoteReconnect = lv_obj_create(menuShutter.page);
  lv_obj_remove_style_all(m_RemoteReconnect);
  lv_obj_set_size(m_RemoteReconnect, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_layout(m_RemoteReconnect, LV_LAYOUT_FLEX);
  // Stack the icon over the text: the badge width is then the wider of the two
  // (the text), which keeps "Reconnecting (i/n)" inside the narrow 135 px panel
  // instead of clipping off the right edge as a single icon+text row would.
  lv_obj_set_flex_flow(m_RemoteReconnect, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(m_RemoteReconnect, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_all(m_RemoteReconnect, 2, 0);
  lv_obj_set_style_pad_row(m_RemoteReconnect, 0, 0);
  lv_obj_set_style_radius(m_RemoteReconnect, 4, 0);
  lv_obj_set_style_bg_opa(m_RemoteReconnect, LV_OPA_70, 0);
  lv_obj_set_style_bg_color(m_RemoteReconnect, lv_color_black(), 0);
  lv_obj_clear_flag(m_RemoteReconnect, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(m_RemoteReconnect, LV_OBJ_FLAG_FLOATING);
  lv_obj_align(m_RemoteReconnect, LV_ALIGN_TOP_LEFT, 1, 1);
  lv_obj_add_flag(m_RemoteReconnect, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t *reconnectIcon = lv_image_create(m_RemoteReconnect);
  lv_image_set_src(reconnectIcon, &icon_bluetooth);
  // Recolor the shared Bluetooth glyph red rather than shipping a new
  // compressed asset, so there is no extra decompress cost.
  lv_obj_set_style_image_recolor(reconnectIcon, lv_palette_main(LV_PALETTE_RED), 0);
  lv_obj_set_style_image_recolor_opa(reconnectIcon, LV_OPA_COVER, 0);

  m_RemoteReconnectLabel = lv_label_create(m_RemoteReconnect);
  lv_obj_set_style_text_color(m_RemoteReconnectLabel, lv_palette_main(LV_PALETTE_RED), 0);
  // The board's Small font keeps "Reconnecting (i/n)" within even the 135 px
  // panel width.
  lv_obj_set_style_text_font(m_RemoteReconnectLabel, fontForTextSize(Settings::TEXT_SIZE_SMALL), 0);
  lv_label_set_text(m_RemoteReconnectLabel, "Reconnecting");

  // The 80 px StickC panel is too narrow to fit the text even at the Small font,
  // so drop the label there and keep just the red Bluetooth icon (the connected
  // page title still carries the wording).
  if (M5.Display.width() < 110) {
    lv_obj_add_flag(m_RemoteReconnectLabel, LV_OBJ_FLAG_HIDDEN);
  }

  // The shutter lock is the only focusable control on the Remote page, so its
  // focus is never ambiguous. Suppress the shared focus outline on it: the ring
  // drew a box around the lock icon that read as noise, the same over-outlining
  // the menu rows carried. A local style overrides the theme's added style for
  // these states.
  lv_obj_set_style_outline_width(m_ShutterLockIcon, 0, LV_STATE_FOCUSED);
  lv_obj_set_style_outline_width(m_ShutterLockIcon, 0, LV_STATE_FOCUS_KEY);

  lv_obj_add_event_cb(
      menuShutter.button,
      [](lv_event_t *e) {
        auto *ui = static_cast<UI *>(lv_event_get_user_data(e));
        ui->configureControl(ControlMode::SHUTTER);
      },
      LV_EVENT_CLICKED, this);

  // add intervalometer control
  menu_t &menuIntervalometer = m_Menu.at(m_IntervalometerStr);
  lv_menu_set_load_page_event(menuIntervalometer.main, menuInterval.button,
                              menuIntervalometer.page);

  lv_obj_add_event_cb(
      menuInterval.button,
      [](lv_event_t *e) {
        auto *ui = static_cast<UI *>(lv_event_get_user_data(e));
        ui->showShutterIntervalometer(true);
      },
      LV_EVENT_CLICKED, this);

  // add GPS data control, the page is shared with the settings menu
  menu_t &menuGPSDataPage = m_Menu.at(m_GPSDataStr);
  lv_menu_set_load_page_event(menuGPSDataPage.main, menuGPSData.button, menuGPSDataPage.page);
  lv_obj_add_event_cb(menuGPSData.button, gpsDataStart, LV_EVENT_CLICKED, m_GPSDataTimer);

  // add disconnect control
  lv_obj_add_event_cb(
      disconnect.button,
      [](lv_event_t *e) {
        auto *ui = static_cast<UI *>(lv_event_get_user_data(e));
        doDisconnect();
        ui->showShutterIntervalometer(false);
      },
      LV_EVENT_CLICKED, this);

  lv_menu_set_load_page_event(menuShutter.main, menuShutter.button, menuShutter.page);

  updateIRMenuVisibility();

  return menuConnected;
}

void UI::addConnectMenu(void) {
  menu_t &menu = addMenu(m_ConnectStr, &icon_linked_camera);

  // refresh connection list every time
  lv_obj_add_event_cb(
      menu.button,
      [](lv_event_t *e) {
        auto &menu = m_Menu.at(m_ConnectStr);
        bool multiconnect = Settings::load<Settings::MULTICONNECT>();

        lv_obj_clean(menu.page);

        CameraList::load();
        lv_obj_t *multibutton = nullptr;
        Settings::multiselect_t selection = {};

        if (multiconnect) {
          multibutton = lv_button_create(menu.page);
          lv_obj_t *label = lv_label_create(multibutton);
          lv_label_set_text(label, "Connect 0");
          lv_obj_center(label);
          lv_obj_set_width(multibutton, LV_PCT(100));
          lv_obj_add_flag(multibutton, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
          lv_group_add_obj(menu.group, multibutton);
          selection = Settings::load<Settings::MULTISELECT>();
        }

        for (size_t n = 0; n < CameraList::size(); n++) {
          auto camera = CameraList::get(n);
          if (multiconnect) {
            bool selected = false;
            const size_t count = std::min<size_t>(selection.count, Settings::MULTISELECT_MAX);
            for (size_t i = 0; i < count; i++) {
              if (strncmp(selection.name[i], camera->getName().c_str(),
                          Settings::MULTISELECT_NAME_MAX - 1)
                  == 0) {
                selected = true;
                break;
              }
            }

            camera->setActive(selected);
            lv_obj_t *item = addCameraItem(n, menu, MODE_MULTICONNECT);
            if (selected && item != nullptr) {
              lv_obj_add_state(item, LV_STATE_CHECKED);
            }

          } else {
            addCameraItem(n, menu, MODE_CONNECT);
          }
        }

        if (multiconnect) {
          updateMultiConnectButton(multibutton);
          // only the multi-select flow persists the remembered set: single
          // connect, boot autoconnect and console connect must not clobber it
          lv_obj_add_event_cb(
              multibutton,
              [](lv_event_t *e) {
                saveMultiConnectSelection();
                doConnect(e);
              },
              LV_EVENT_CLICKED, nullptr);
        }

        m_ConnectContext.menuName = NULL;
      },
      LV_EVENT_CLICKED, NULL);

  // create, but immediately hide the connect message box
  m_ConnectContext.messageBox = lv_msgbox_create(m_Screen);
  lv_msgbox_add_title(m_ConnectContext.messageBox, "Connecting");
  lv_obj_set_width(m_ConnectContext.messageBox, LV_PCT(100));
  lv_obj_update_layout(m_ConnectContext.messageBox);
  lv_obj_t *c = lv_msgbox_get_content(m_ConnectContext.messageBox);
  lv_obj_set_flex_align(c, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
  m_ConnectContext.label = lv_label_create(c);
  lv_label_set_long_mode(m_ConnectContext.label, LV_LABEL_LONG_DOT);
  lv_obj_set_width(m_ConnectContext.label, LV_PCT(80));

  m_ConnectContext.bar = lv_bar_create(c);
  lv_obj_set_width(m_ConnectContext.bar, LV_PCT(80));
  lv_bar_set_value(m_ConnectContext.bar, 0, LV_ANIM_ON);

  m_ConnectContext.cancel = lv_msgbox_add_footer_button(m_ConnectContext.messageBox, "Cancel");
  // Register the Cancel handler once here, not per connect attempt. Adding it in
  // doConnect stacked one callback per attempt, so after N attempts a single
  // Cancel click fired N disconnects.
  lv_obj_add_event_cb(
      m_ConnectContext.cancel, [](lv_event_t *e) { doDisconnect(); }, LV_EVENT_CLICKED, NULL);
  lv_obj_t *footer = lv_msgbox_get_footer(m_ConnectContext.messageBox);
  lv_obj_update_layout(footer);
  // @todo cancel button bottom is clipped, weird
  // lv_obj_set_height(m_ConnectContext.cancel, LV_SIZE_CONTENT);
  // lv_obj_set_height(footer, lv_obj_get_height(m_ConnectContext.cancel) * 1.2f);

  lv_obj_add_flag(m_ConnectContext.messageBox, LV_OBJ_FLAG_HIDDEN);

  lv_menu_set_load_page_event(menu.main, menu.button, menu.page);
}

void UI::addScanMenu(void) {
  menu_t &menu = addMenu(m_ScanStr, &icon_add_a_photo);

  lv_menu_set_load_page_event(menu.main, menu.button, menu.page);
}

void UI::startScan(void) {
  menu_t &menu = m_Menu.at(m_ScanStr);
  auto &scan = Scan::getInstance();

  lv_obj_clean(menu.page);
  CameraList::clear();

  // hidden until the scan ends by itself, a finite scan that ends silently
  // looks like a hang
  m_ScanFinished = lv_menu_cont_create(menu.page);
  lv_obj_set_flex_flow(m_ScanFinished, LV_FLEX_FLOW_COLUMN);
  lv_obj_add_flag(m_ScanFinished, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t *label = lv_label_create(m_ScanFinished);
  lv_label_set_text(label, "Scan finished");
  lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_set_width(label, LV_PCT(100));

  lv_obj_t *rescan = lv_button_create(m_ScanFinished);
  lv_obj_t *rescanLabel = lv_label_create(rescan);
  lv_label_set_text(rescanLabel, "Rescan");
  addToInputGroup(menu.group, rescan);

  lv_obj_add_event_cb(
      rescan,
      [](lv_event_t *) {
        // deferred, the restart deletes the button we are called from
        lv_async_call([](void *) { startScan(); }, NULL);
      },
      LV_EVENT_CLICKED, NULL);

  if (Settings::load<Settings::FAUXNY>()) {
    CameraList::addFauxNY();
    updateItems(menu);
  }

  scan.setMode(static_cast<Scan::Mode>(Settings::scanModeEffective()));
  scan.setTimeout(Settings::load<Settings::SCAN_TIMEOUT>());

  scan.clear();
  scan.start(
      [](void *param) {
        auto *menu = static_cast<menu_t *>(param);
        // Can be called asychronously from NimBLE scan thread,
        m_Mutex.lock();
        updateItems(*menu);
        m_Mutex.unlock();
      },
      &menu,
      [](void *) {
        // Can be called asychronously from NimBLE scan thread,
        m_Mutex.lock();
        lv_obj_remove_flag(m_ScanFinished, LV_OBJ_FLAG_HIDDEN);
        m_Mutex.unlock();
      });

  m_ConnectContext.menuName = m_ScanStr;
}

void UI::refreshDelete(void) {
  auto &menu = m_Menu.at(m_DeleteStr);
  lv_obj_clean(menu.page);

  CameraList::load();
  for (size_t n = 0; n < CameraList::size(); n++) {
    addCameraItem(n, menu, MODE_DELETE);
  }
}

void UI::addDeleteMenu(void) {
  menu_t &menu = addMenu(m_DeleteStr, &icon_delete);

  // refresh connection list every time
  lv_obj_add_event_cb(menu.button, [](lv_event_t *e) { refreshDelete(); }, LV_EVENT_CLICKED, NULL);

  lv_menu_set_load_page_event(menu.main, menu.button, menu.page);
}

void UI::showGPSWidgets(status_t *status, bool show) {
  for (auto *widget : status->gpsWidgets) {
    if (show) {
      lv_obj_clear_flag(widget, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(widget, LV_OBJ_FLAG_HIDDEN);
    }
  }
}

void UI::addGPSMenu(const menu_t &parent) {
  menu_t &menu = addMenu(m_GPSStr, &icon_location_searching, true, parent);

  addSettingItem(menu.page, NULL, Settings::GPS);
  lv_menu_set_load_page_event(menu.main, menu.button, menu.page);

  // add GPS baud control
  lv_obj_t *gpsBaud = lv_menu_cont_create(menu.page);
  lv_obj_set_flex_flow(gpsBaud, LV_FLEX_FLOW_ROW_WRAP);
  m_Status.gpsWidgets.push_back(gpsBaud);
  lv_obj_t *label = lv_label_create(gpsBaud);
  lv_label_set_text(label, "GPS baud 115200");
  lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_set_flex_grow(label, 1);

  lv_obj_t *baud_sw = lv_switch_create(gpsBaud);
  lv_obj_add_flag(baud_sw, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
  addToInputGroup(m_Group, baud_sw);
  uint32_t baud = Settings::load<Settings::GPS_BAUD>();
  lv_obj_add_state(baud_sw, baud == Settings::BAUD_115200 ? LV_STATE_CHECKED : LV_STATE_DEFAULT);
  lv_obj_add_event_cb(
      baud_sw,
      [](lv_event_t *e) {
        auto *status = static_cast<status_t *>(lv_event_get_user_data(e));
        lv_obj_t *baud_sw = static_cast<lv_obj_t *>(lv_event_get_target(e));
        uint32_t baud;

        if (lv_obj_has_state(baud_sw, LV_STATE_CHECKED)) {
          baud = Settings::BAUD_115200;
        } else {
          baud = Settings::BAUD_9600;
        }
        Settings::save<Settings::GPS_BAUD>(baud);
        status->gps->reloadSetting();
      },
      LV_EVENT_VALUE_CHANGED, &m_Status);

  // add the receiver configuration pages
  addGPSOptionMenu(
      menu, m_GPSRateStr, m_GPSRateOptions, Settings::load<Settings::GPS_RATE>(),
      [](lv_event_t *e) {
        auto *status = static_cast<status_t *>(lv_event_get_user_data(e));
        auto *roller = static_cast<lv_obj_t *>(lv_event_get_target(e));

        Settings::save<Settings::GPS_RATE>(static_cast<uint8_t>(lv_roller_get_selected(roller)));
        status->gps->reloadSetting();
      });

  addGPSOptionMenu(menu, m_GPSSentencesStr, m_GPSSentencesOptions,
                   Settings::load<Settings::GPS_NMEA>() ? 1 : 0, [](lv_event_t *e) {
                     auto *status = static_cast<status_t *>(lv_event_get_user_data(e));
                     auto *roller = static_cast<lv_obj_t *>(lv_event_get_target(e));

                     Settings::save<Settings::GPS_NMEA>(lv_roller_get_selected(roller) > 0);
                     status->gps->reloadSetting();
                   });

  addGPSOptionMenu(
      menu, m_GPSConstellationStr, m_GPSConstellationOptions,
      Settings::load<Settings::GPS_CONSTEL>(), [](lv_event_t *e) {
        auto *status = static_cast<status_t *>(lv_event_get_user_data(e));
        auto *roller = static_cast<lv_obj_t *>(lv_event_get_target(e));

        Settings::save<Settings::GPS_CONSTEL>(static_cast<uint8_t>(lv_roller_get_selected(roller)));
        status->gps->reloadSetting();
      });

  addGPSPowerMenu(menu);
  addGPSOptionMenu(
      menu, m_GPSAssistStr, m_GPSAssistOptions, Settings::load<Settings::GPS_ASSIST>(),
      [](lv_event_t *e) {
        auto *status = static_cast<status_t *>(lv_event_get_user_data(e));
        auto *roller = static_cast<lv_obj_t *>(lv_event_get_target(e));

        Settings::save<Settings::GPS_ASSIST>(static_cast<uint8_t>(lv_roller_get_selected(roller)));
        status->gps->reloadSetting();
      });

  addGPSDataMenu(menu);
  addGPSNMEAMenu(menu);

  showGPSWidgets(&m_Status, m_Status.gps->isEnabled());
}

void UI::addGPSPowerMenu(const menu_t &parent) {
  menu_t &menu = addMenu(m_GPSPowerStr, NULL, true, parent);
  m_Status.gpsWidgets.push_back(menu.button);

  auto addRoller = [&](const char *name, const char *options, uint32_t selected,
                       lv_event_cb_t handler) {
    lv_obj_t *row = lv_menu_cont_create(menu.page);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_width(row, LV_PCT(100));

    lv_obj_t *label = lv_label_create(row);
    lv_label_set_text(label, name);
    lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL_CIRCULAR);

    lv_obj_t *roller = lv_roller_create(row);
#if !defined(FURBLE_M5COREX)
    lv_obj_set_width(roller, LV_PCT(90));
#endif
    lv_obj_add_flag(roller, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    addToInputGroup(m_Group, roller);
    lv_roller_set_options(roller, options, LV_ROLLER_MODE_NORMAL);
    lv_roller_set_visible_row_count(roller, 2);
    lv_roller_set_selected(roller, selected, LV_ANIM_OFF);
    lv_obj_add_event_cb(roller, handler, LV_EVENT_VALUE_CHANGED, &m_Status);
  };

  const uint8_t policy = Settings::load<Settings::GPS_POWER>();
  const uint8_t selectedPolicy = policy <= GPS::POWER_RAIL_CYCLE ? policy : GPS::POWER_ALWAYS_ON;
  addRoller("Receiver", m_GPSPowerOptions, selectedPolicy, [](lv_event_t *e) {
    auto *status = static_cast<status_t *>(lv_event_get_user_data(e));
    auto *roller = static_cast<lv_obj_t *>(lv_event_get_target(e));

    Settings::save<Settings::GPS_POWER>(static_cast<uint8_t>(lv_roller_get_selected(roller)));
    status->gps->reloadSetting();
  });

  const uint8_t duty = Settings::load<Settings::GPS_DUTY>();
  addRoller("Sleep between fixes", m_GPSDutyOptions, gpsDutyIndex(duty), [](lv_event_t *e) {
    auto *status = static_cast<status_t *>(lv_event_get_user_data(e));
    auto *roller = static_cast<lv_obj_t *>(lv_event_get_target(e));
    const uint32_t selected = lv_roller_get_selected(roller);

    Settings::save<Settings::GPS_DUTY>(
        GPS::DUTY_SECONDS[std::min<size_t>(selected, GPS::DUTY_SECONDS.size() - 1)]);
    status->gps->reloadSetting();
  });

  lv_obj_t *help = lv_label_create(menu.page);
  lv_obj_set_width(help, LV_PCT(100));
  lv_label_set_long_mode(help, LV_LABEL_LONG_WRAP);
  lv_label_set_text(help,
                    "Use 5 to 15 s to keep fixes fresh. Rail cycling is experimental. GPS unit "
                    "v1.1 has no backup supply, a rail cut costs a ~108 s cold refix, so the "
                    "cycle never completes and the receiver stays always on.");

  lv_menu_set_load_page_event(menu.main, menu.button, menu.page);
}

void UI::addGPSOptionMenu(const menu_t &parent,
                          const char *name,
                          const char *options,
                          uint32_t selected,
                          lv_event_cb_t handler) {
  menu_t &menu = addMenu(name, NULL, true, parent);
  m_Status.gpsWidgets.push_back(menu.button);

  lv_obj_t *cont = lv_menu_cont_create(menu.page);
  lv_obj_set_size(cont, LV_PCT(100), LV_PCT(100));
  lv_obj_set_layout(cont, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t *roller = lv_roller_create(cont);
#if !defined(FURBLE_M5COREX)
  lv_obj_set_width(roller, LV_PCT(90));
#endif
  lv_obj_add_flag(roller, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
  addToInputGroup(m_Group, roller);
  lv_roller_set_options(roller, options, LV_ROLLER_MODE_NORMAL);
  lv_roller_set_visible_row_count(roller, 2);
  lv_roller_set_selected(roller, selected, LV_ANIM_OFF);

  lv_obj_add_event_cb(roller, handler, LV_EVENT_VALUE_CHANGED, &m_Status);

  lv_menu_set_load_page_event(menu.main, menu.button, menu.page);
}

void UI::addGPSDataMenu(const menu_t &parent) {
  menu_t &gpsData = addMenu(m_GPSDataStr, NULL, true, parent);
  m_Status.gpsWidgets.push_back(gpsData.button);

  m_GPSDataTimer = lv_timer_create(
      [](lv_timer_t *t) {
        FURBLE_SIM_TIMER_FIRE("gps_data_timer");
        auto *gpsData = static_cast<menu_t *>(lv_timer_get_user_data(t));
        auto &gps = GPS::getInstance().get();

        static lv_obj_t *age = lv_label_create(gpsData->page);
        setLabelTextFmtIfChanged(age, "%lus ago", gps.location.age() / 1000);

        static lv_obj_t *satellites = lv_label_create(gpsData->page);
        setLabelTextFmtIfChanged(satellites, "%lu satellites", gps.satellites.value());

        static lv_obj_t *speed = lv_label_create(gpsData->page);
        setLabelTextFmtIfChanged(speed, "%.1f km/h", gps.speed.kmph());

        static lv_obj_t *lat = nullptr;
        if (lat == nullptr) {
          lat = lv_label_create(gpsData->page);
          lv_obj_set_style_text_font(lat, &lv_font_montserrat_12, 0);
        }
        setLabelTextFmtIfChanged(lat, "%.5f°", gps.location.lat());

        static lv_obj_t *lon = nullptr;
        if (lon == nullptr) {
          lon = lv_label_create(gpsData->page);
          lv_obj_set_style_text_font(lon, &lv_font_montserrat_12, 0);
        }
        setLabelTextFmtIfChanged(lon, "%.5f°", gps.location.lng());

        static lv_obj_t *alt = lv_label_create(gpsData->page);
        setLabelTextFmtIfChanged(alt, "%.2f m", gps.altitude.meters());

#if defined(FURBLE_M5COREX)
        static lv_obj_t *datetime = lv_label_create(gpsData->page);
        setLabelTextFmtIfChanged(datetime, "%4u-%02u-%02u %02u:%02u:%02u", gps.date.year(),
                                 gps.date.month(), gps.date.day(), gps.time.hour(),
                                 gps.time.minute(), gps.time.second());
#else
        static lv_obj_t *date = lv_label_create(gpsData->page);
        setLabelTextFmtIfChanged(date, "%4u-%02u-%02u", gps.date.year(), gps.date.month(),
                                 gps.date.day());
        static lv_obj_t *time = lv_label_create(gpsData->page);
        setLabelTextFmtIfChanged(time, "%02u:%02u:%02u", gps.time.hour(), gps.time.minute(),
                                 gps.time.second());
#endif
      },
      1000, &gpsData);
  lv_timer_pause(m_GPSDataTimer);

  // start the update timer on 'GPS Data' button press
  lv_obj_add_event_cb(gpsData.button, gpsDataStart, LV_EVENT_CLICKED, m_GPSDataTimer);

  lv_menu_set_load_page_event(gpsData.main, gpsData.button, gpsData.page);
}

void UI::gpsDataStart(lv_event_t *e) {
  auto *timer = static_cast<lv_timer_t *>(lv_event_get_user_data(e));
  lv_timer_resume(timer);
  menu_t *menu = static_cast<menu_t *>(lv_timer_get_user_data(timer));
  lv_obj_add_event_cb(menu->main, gpsDataStop, LV_EVENT_CLICKED, timer);
}

void UI::gpsDataStop(lv_event_t *e) {
  auto *timer = static_cast<lv_timer_t *>(lv_event_get_user_data(e));
  auto *target = static_cast<lv_obj_t *>(lv_event_get_target(e));
  lv_timer_pause(timer);
  lv_obj_remove_event_cb(target, gpsDataStop);
}

/**
 * Raw NMEA and satellite debug page.
 *
 * Sentence capture only runs while the page is open. Binary configuration
 * status is shown alongside the captured sentences.
 */
void UI::addGPSNMEAMenu(const menu_t &parent) {
  menu_t &menu = addMenu(m_GPSNMEAStr, NULL, true, parent);
  m_Status.gpsWidgets.push_back(menu.button);

  lv_obj_t *cont = lv_menu_cont_create(menu.page);
  lv_obj_set_size(cont, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_layout(cont, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);

  m_NMEA.fix = lv_label_create(cont);
  lv_obj_set_width(m_NMEA.fix, LV_PCT(100));
  lv_label_set_long_mode(m_NMEA.fix, LV_LABEL_LONG_WRAP);

  m_NMEA.counters = lv_label_create(cont);
  lv_obj_set_width(m_NMEA.counters, LV_PCT(100));
  lv_label_set_long_mode(m_NMEA.counters, LV_LABEL_LONG_WRAP);

  m_NMEA.config = lv_label_create(cont);
  lv_obj_set_width(m_NMEA.config, LV_PCT(100));
  lv_label_set_long_mode(m_NMEA.config, LV_LABEL_LONG_WRAP);

  m_NMEA.sentences = lv_label_create(cont);
  lv_obj_set_width(m_NMEA.sentences, LV_PCT(100));
  lv_label_set_long_mode(m_NMEA.sentences, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_font(m_NMEA.sentences, &lv_font_montserrat_12, 0);

  lv_obj_t *restart = lv_button_create(cont);
  lv_obj_t *label = lv_label_create(restart);
  lv_label_set_text(label, "Hot restart");
  lv_obj_add_flag(restart, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
  addToInputGroup(m_Group, restart);
  lv_obj_add_event_cb(
      restart, [](lv_event_t *e) { GPS::getInstance().restart(0); }, LV_EVENT_CLICKED, NULL);

  m_NMEATimer = lv_timer_create(
      [](lv_timer_t *t) {
        FURBLE_SIM_TIMER_FIRE("nmea_timer");
        auto *ui = static_cast<UI *>(lv_timer_get_user_data(t));
        auto &gps = GPS::getInstance();
        auto &tinygps = gps.get();

        setLabelTextFmtIfChanged(ui->m_NMEA.fix, "%lu sats, hdop %.1f\n%lus ago, %.1f km/h",
                                 (unsigned long)tinygps.satellites.value(), tinygps.hdop.hdop(),
                                 (unsigned long)(tinygps.location.age() / 1000),
                                 tinygps.speed.kmph());
        setLabelTextFmtIfChanged(
            ui->m_NMEA.counters, "rx %lu\nok %lu, bad %lu", (unsigned long)tinygps.charsProcessed(),
            (unsigned long)tinygps.passedChecksum(), (unsigned long)tinygps.failedChecksum());

        std::string config;
        for (const auto &entry : gps.getConfigStatus()) {
          char line[48];
          snprintf(line, sizeof(line), "cfg %02X/%02X %s x%u", entry.class_id, entry.message_id,
                   GPS::configStateName(entry.state), static_cast<unsigned>(entry.attempts));
          if (!config.empty()) {
            config += "\n";
          }
          config += line;
        }
        if (config.empty()) {
          config = "cfg empty";
        }
        if (config != ui->m_NMEA.configText) {
          ui->m_NMEA.configText = config;
          lv_label_set_text(ui->m_NMEA.config, config.c_str());
        }

        std::string text;
        for (const auto &sentence : gps.getSentences()) {
          text += sentence + "\n";
        }
        setLabelTextIfChanged(ui->m_NMEA.sentences, text.c_str());
      },
      1000, this);
  lv_timer_pause(m_NMEATimer);

  // only capture sentences while the page is open
  lv_obj_add_event_cb(
      menu.button,
      [](lv_event_t *e) {
        auto *ui = static_cast<UI *>(lv_event_get_user_data(e));
        GPS::getInstance().setCapture(true);
        lv_timer_resume(ui->m_NMEATimer);
        lv_obj_add_event_cb(m_MainMenu.main, gpsNMEAStop, LV_EVENT_CLICKED, ui);
      },
      LV_EVENT_CLICKED, this);

  lv_menu_set_load_page_event(menu.main, menu.button, menu.page);
}

void UI::gpsNMEAStop(lv_event_t *e) {
  auto *ui = static_cast<UI *>(lv_event_get_user_data(e));
  auto *target = static_cast<lv_obj_t *>(lv_event_get_target(e));
  lv_timer_pause(ui->m_NMEATimer);
  GPS::getInstance().setCapture(false);
  lv_obj_remove_event_cb(target, gpsNMEAStop);
}

void UI::addFeaturesMenu(const menu_t &parent) {
  menu_t &menu = addMenu(m_FeaturesStr, &icon_wand_stars, true, parent);

  lv_obj_t *buttonMode =
      addRollerItem(menu.page, Settings::get(Settings::BUTTON_MODE).name, "Two-button\nOne-button");
  const std::string mode = Settings::load<Settings::BUTTON_MODE>();
  const uint32_t selected = mode == Settings::BUTTON_MODE_ONE_BUTTON_VALUE
                                ? Settings::BUTTON_MODE_ONE_BUTTON
                                : Settings::BUTTON_MODE_TWO_BUTTON;
  lv_roller_set_selected(buttonMode, selected, LV_ANIM_OFF);
  lv_obj_add_event_cb(
      buttonMode,
      [](lv_event_t *e) {
        auto *roller = static_cast<lv_obj_t *>(lv_event_get_target(e));
        const uint32_t selected = lv_roller_get_selected(roller);
        if (selected > Settings::BUTTON_MODE_ONE_BUTTON) {
          return;
        }
        const char *mode = selected == Settings::BUTTON_MODE_ONE_BUTTON
                               ? Settings::BUTTON_MODE_ONE_BUTTON_VALUE
                               : Settings::BUTTON_MODE_TWO_BUTTON_VALUE;
        Settings::save<std::string>(Settings::BUTTON_MODE, mode);
      },
      LV_EVENT_VALUE_CHANGED, NULL);

  // Static, non-focusable hint under the roller. The one-button gestures are
  // not obvious, so describe them where the user picks the mode. It is a plain
  // label, never added to an input group and never clickable, so it does not
  // take encoder focus or draw a focus ring. Always shown so the gestures are
  // discoverable before one-button mode is enabled.
  lv_obj_t *buttonModeHint = lv_label_create(menu.page);
  lv_obj_set_width(buttonModeHint, LV_PCT(100));
  lv_label_set_long_mode(buttonModeHint, LV_LABEL_LONG_WRAP);
  lv_label_set_text(buttonModeHint,
                    "One-button: hold=focus, double-click=shoot, click+hold=hold shutter");

  addSettingItem(menu.page, NULL, Settings::AUTOCONNECT);
  addSettingItem(menu.page, NULL, Settings::FAUXNY);
  addSettingItem(menu.page, NULL, Settings::RECONNECT);
  addSettingItem(menu.page, NULL, Settings::RECON_BACKOFF);
  addSettingItem(menu.page, NULL, Settings::MULTICONNECT);
  addSettingItem(menu.page, NULL, Settings::COMPANION);
#if defined(FURBLE_M5STICKS3)
  addSettingItem(menu.page, NULL, Settings::WATCHDOG);
#endif
  addSettingItem(menu.page, NULL, Settings::PRESET_PICKER);
  addSettingItem(menu.page, NULL, Settings::BOOT_SPLASH);

  lv_menu_set_load_page_event(menu.main, menu.button, menu.page);
}

void UI::addIRMenu(void) {
  menu_t &menu = addMenu(m_IRStr, &icon_remote_gen);
  lv_obj_set_flex_flow(menu.page, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(menu.page, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);

  lv_obj_t *fire = lv_button_create(menu.page);
  lv_obj_set_width(fire, LV_PCT(80));
  lv_obj_t *label = lv_label_create(fire);
  lv_label_set_text(label, "Fire");
  lv_obj_center(label);
  addToInputGroup(menu.group, fire);

  lv_obj_add_event_cb(
      fire,
      [](lv_event_t *e) {
        (void)e;
        IR::getInstance().fire();
      },
      LV_EVENT_CLICKED, NULL);

  lv_menu_set_load_page_event(menu.main, menu.button, menu.page);
  updateIRMenuVisibility();
}

lv_obj_t *UI::addSpinItem(lv_obj_t *page, const char *item, Intervalometer::Spinner &spinner) {
  spinner.m_Button = lv_menu_cont_create(page);
  lv_obj_set_flex_flow(spinner.m_Button, LV_FLEX_FLOW_ROW_WRAP);
#if defined(FURBLE_M5STICKC)
  // 80x160 is the shortest panel. Trim the per-row padding so the Count, Delay,
  // Shutter and Wait rows fit without scrolling the timer page.
  lv_obj_set_style_pad_top(spinner.m_Button, 1, LV_STATE_DEFAULT);
  lv_obj_set_style_pad_bottom(spinner.m_Button, 1, LV_STATE_DEFAULT);
#endif

  spinner.m_Label = lv_label_create(spinner.m_Button);
  lv_label_set_text(spinner.m_Label, item);
#if defined(FURBLE_M5COREX)
  lv_obj_set_flex_grow(spinner.m_Label, 1);
#endif

  spinner.m_Value = lv_label_create(spinner.m_Button);
  lv_label_set_long_mode(spinner.m_Value, LV_LABEL_LONG_SCROLL_CIRCULAR);
#if !defined(FURBLE_M5COREX)
  lv_obj_set_flex_grow(spinner.m_Value, 1);
#endif

  lv_obj_add_event_cb(
      spinner.m_Value,
      [](lv_event_t *e) {
        auto *spinner = static_cast<Intervalometer::Spinner *>(lv_event_get_user_data(e));
        spinner->update();
      },
      LV_EVENT_REFRESH, &spinner);

  return spinner.m_Button;
}

void UI::addSpinnerPage(const menu_t &parent, const char *item, Intervalometer::Spinner &spinner) {
  menu_t &menu = addMenu(item, NULL, false, parent);
  menu.button = addSpinItem(parent.page, item, spinner);

  addToInputGroup(m_Group, menu.button);

  lv_obj_set_flex_flow(menu.page, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scrollbar_mode(menu.page, LV_SCROLLBAR_MODE_OFF);
  spinner.m_RowInfinite = lv_obj_create(menu.page);
  lv_obj_set_size(spinner.m_RowInfinite, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_layout(spinner.m_RowInfinite, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(spinner.m_RowInfinite, LV_FLEX_FLOW_ROW);
  lv_obj_t *label = lv_label_create(spinner.m_RowInfinite);
  lv_label_set_text(label, "Infinite");
  lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_set_flex_grow(label, 1);
  spinner.m_SwitchInfinite = lv_switch_create(spinner.m_RowInfinite);
  lv_obj_add_flag(spinner.m_SwitchInfinite, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
  addToInputGroup(m_Group, spinner.m_SwitchInfinite);
  if (spinner.m_SpinValue.m_Unit == SpinValue::UNIT_INF) {
    lv_obj_add_state(spinner.m_SwitchInfinite, LV_STATE_CHECKED);
  } else {
    lv_obj_remove_state(spinner.m_SwitchInfinite, LV_STATE_CHECKED);
  }

  if (!spinner.m_Infinite) {
    lv_obj_add_flag(spinner.m_RowInfinite, LV_OBJ_FLAG_HIDDEN);
  }

  lv_obj_add_event_cb(
      spinner.m_SwitchInfinite,
      [](lv_event_t *e) {
        auto *spinner = static_cast<Intervalometer::Spinner *>(lv_event_get_user_data(e));
        spinner->update();
      },
      LV_EVENT_VALUE_CHANGED, &spinner);

  spinner.m_RowSpinners = lv_obj_create(menu.page);
  lv_obj_set_size(spinner.m_RowSpinners, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_layout(spinner.m_RowSpinners, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(spinner.m_RowSpinners, LV_FLEX_FLOW_ROW);

  switch (M5.getBoard()) {
    case m5::board_t::board_M5StickC:
    case m5::board_t::board_M5StickCPlus2:
    case m5::board_t::board_M5StickCPlus:
    case m5::board_t::board_M5StickS3:
      lv_obj_set_flex_align(spinner.m_RowSpinners, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                            LV_FLEX_ALIGN_CENTER);
      break;

    default:
      lv_obj_set_flex_align(spinner.m_RowSpinners, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                            LV_FLEX_ALIGN_CENTER);
      break;
  }

  for (auto &r : spinner.m_Roller) {
    r = lv_roller_create(spinner.m_RowSpinners);
    lv_obj_add_flag(r, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_roller_set_options(r, Intervalometer::Spinner::m_SpinDigitRoller, LV_ROLLER_MODE_INFINITE);

    lv_roller_set_visible_row_count(r, 2);
    addToInputGroup(m_Group, r);
    lv_obj_add_event_cb(
        r,
        [](lv_event_t *e) {
          auto *spinner = static_cast<Intervalometer::Spinner *>(lv_event_get_user_data(e));
          lv_obj_send_event(spinner->m_Value, LV_EVENT_REFRESH, spinner);
        },
        LV_EVENT_VALUE_CHANGED, &spinner);
  }

  if (spinner.supportsPresetPicker()
      || ((spinner.m_SpinValue.m_Unit != SpinValue::UNIT_NIL)
          && (spinner.m_SpinValue.m_Unit != SpinValue::UNIT_INF))) {
    spinner.m_RollerUnit = lv_roller_create(spinner.m_RowSpinners);
    lv_obj_add_flag(spinner.m_RollerUnit, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_roller_set_options(spinner.m_RollerUnit, Intervalometer::Spinner::m_SpinUnitsRoller,
                          LV_ROLLER_MODE_INFINITE);

    lv_obj_add_event_cb(
        spinner.m_RollerUnit,
        [](lv_event_t *e) {
          auto *spinner = static_cast<Intervalometer::Spinner *>(lv_event_get_user_data(e));
          lv_obj_send_event(spinner->m_Value, LV_EVENT_REFRESH, spinner);
        },
        LV_EVENT_VALUE_CHANGED, &spinner);

    addToInputGroup(m_Group, spinner.m_RollerUnit);
  }

  if (spinner.supportsPresetPicker()) {
    spinner.m_PresetRow = lv_obj_create(menu.page);
    lv_obj_set_size(spinner.m_PresetRow, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_layout(spinner.m_PresetRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(spinner.m_PresetRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(spinner.m_PresetRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    spinner.m_PresetMinus = lv_button_create(spinner.m_PresetRow);
    lv_obj_set_style_bg_image_src(spinner.m_PresetMinus, LV_SYMBOL_MINUS, 0);
    lv_obj_set_size(spinner.m_PresetMinus, 40, 40);

    spinner.m_PresetValue = lv_label_create(spinner.m_PresetRow);
    lv_obj_set_flex_grow(spinner.m_PresetValue, 1);
    lv_obj_set_style_text_align(spinner.m_PresetValue, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(spinner.m_PresetValue, &lv_font_montserrat_16, 0);

    spinner.m_PresetPlus = lv_button_create(spinner.m_PresetRow);
    lv_obj_set_style_bg_image_src(spinner.m_PresetPlus, LV_SYMBOL_PLUS, 0);
    lv_obj_set_size(spinner.m_PresetPlus, 40, 40);

    lv_obj_add_event_cb(
        spinner.m_PresetMinus,
        [](lv_event_t *e) {
          auto *spinner = static_cast<Intervalometer::Spinner *>(lv_event_get_user_data(e));
          lv_event_code_t code = lv_event_get_code(e);
          if ((code == LV_EVENT_PRESSED) || (code == LV_EVENT_LONG_PRESSED_REPEAT)) {
            spinner->stepPreset(-1);
          }
        },
        LV_EVENT_ALL, &spinner);
    lv_obj_add_event_cb(
        spinner.m_PresetPlus,
        [](lv_event_t *e) {
          auto *spinner = static_cast<Intervalometer::Spinner *>(lv_event_get_user_data(e));
          lv_event_code_t code = lv_event_get_code(e);
          if ((code == LV_EVENT_PRESSED) || (code == LV_EVENT_LONG_PRESSED_REPEAT)) {
            spinner->stepPreset(1);
          }
        },
        LV_EVENT_ALL, &spinner);

    if (M5.Touch.isEnabled()) {
      // The synthesized A/B/C buttons navigate the group, so the plus and
      // minus buttons have to be group members to be reachable.
      lv_group_add_obj(m_Group, spinner.m_PresetMinus);
      lv_group_add_obj(m_Group, spinner.m_PresetPlus);
    } else {
      // The physical keys step directly, the on-screen buttons only waste
      // width on the small displays.
      lv_obj_add_flag(spinner.m_PresetMinus, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(spinner.m_PresetPlus, LV_OBJ_FLAG_HIDDEN);
    }

    lv_obj_add_flag(spinner.m_PresetRow, LV_OBJ_FLAG_HIDDEN);
  }

  // squeeze width for smaller displays
  switch (M5.getBoard()) {
    case m5::board_t::board_M5StickC:
      lv_obj_set_flex_align(spinner.m_RowSpinners, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                            LV_FLEX_ALIGN_CENTER);
      lv_obj_add_flag(spinner.m_RowSpinners, LV_OBJ_FLAG_SCROLLABLE);
      lv_obj_set_scrollbar_mode(spinner.m_RowSpinners, LV_SCROLLBAR_MODE_OFF);
      lv_obj_set_scroll_dir(spinner.m_RowSpinners, LV_DIR_HOR);
      __attribute__((fallthrough));
    case m5::board_t::board_M5StickCPlus2:
    case m5::board_t::board_M5StickCPlus:
    case m5::board_t::board_M5StickS3:
      for (auto &r : spinner.m_Roller) {
        lv_obj_set_style_pad_left(r, 2, LV_STATE_DEFAULT);
        lv_obj_set_style_pad_right(r, 2, LV_STATE_DEFAULT);
      }

      if (spinner.m_RollerUnit != nullptr) {
        lv_obj_set_style_pad_left(spinner.m_RollerUnit, 2, LV_STATE_DEFAULT);
        lv_obj_set_style_pad_right(spinner.m_RollerUnit, 2, LV_STATE_DEFAULT);
      }

      if (spinner.m_PresetRow != nullptr) {
        // squeeze the preset row too so the value label gets the width
        lv_obj_set_style_pad_left(spinner.m_PresetRow, 2, LV_STATE_DEFAULT);
        lv_obj_set_style_pad_right(spinner.m_PresetRow, 2, LV_STATE_DEFAULT);
      }
      break;
    default:
      break;
  }

  spinner.updateLabels();

  lv_menu_set_load_page_event(menu.main, menu.button, menu.page);
}

void UI::addIntervalometerMenu(const menu_t &parent) {
  menu_t &menu = addMenu(m_IntervalometerStr, &icon_timer, true, parent);
  menu_t &menuIntervalRun = addMenu(m_IntervalometerRunStr, NULL, false, menu);

  m_IntervalStart = lv_button_create(menu.page);
  lv_obj_t *label = lv_label_create(m_IntervalStart);
  lv_label_set_text(label, "Start");
  lv_obj_center(label);
  lv_obj_add_flag(m_IntervalStart, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
  addToInputGroup(m_Group, m_IntervalStart);

  addSpinnerPage(menu, m_IntervalCountStr, m_Intervalometer.m_Count);
  addSpinnerPage(menu, m_IntervalDelayStr, m_Intervalometer.m_Delay);
  addSpinnerPage(menu, m_IntervalShutterStr, m_Intervalometer.m_Shutter);
  addSpinnerPage(menu, m_IntervalWaitStr, m_Intervalometer.m_Wait);

  // Reflect count infinite or not
  m_Intervalometer.m_Count.update();

  lv_obj_add_flag(m_IntervalStart, LV_OBJ_FLAG_HIDDEN);

  lv_obj_add_event_cb(
      m_IntervalStart,
      [](lv_event_t *e) {
        auto *timer = static_cast<lv_timer_t *>(lv_event_get_user_data(e));
        auto *interval = static_cast<Intervalometer *>(lv_timer_get_user_data(timer));

        interval->m_State = Intervalometer::STATE_IDLE;
        m_IntervalCountdownActive = false;
        m_IntervalLastAnnouncedSecond = 0;
        lv_timer_resume(timer);

        lv_timer_resume(m_IntervalPageRefresh);
        lv_timer_ready(m_IntervalPageRefresh);
      },
      LV_EVENT_CLICKED, m_IntervalTimer);

  lv_obj_t *cont = lv_menu_cont_create(menuIntervalRun.page);
  lv_obj_set_height(cont, LV_PCT(100));
  lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);

  m_Intervalometer.m_StateLabel = lv_label_create(cont);
  m_Intervalometer.m_CountLabel = lv_label_create(cont);
  m_Intervalometer.m_RemainingLabel = lv_label_create(cont);

  m_IntervalStop = lv_button_create(cont);
  lv_obj_t *stopLabel = lv_label_create(m_IntervalStop);
  lv_label_set_text(stopLabel, "Stop");
  lv_obj_add_flag(m_IntervalStop, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
  addToInputGroup(m_Group, m_IntervalStop);
  lv_obj_add_event_cb(
      m_IntervalStop,
      [](lv_event_t *e) {
        auto *timer = static_cast<lv_timer_t *>(lv_event_get_user_data(e));
        auto *interval = static_cast<Intervalometer *>(lv_timer_get_user_data(timer));
        auto &control = Control::getInstance();

        // pause all interval timers
        lv_timer_pause(timer);
        lv_timer_pause(m_IntervalPageRefresh);
        m_IntervalCountdownActive = false;
        m_IntervalLastAnnouncedSecond = 0;

        // reset the run state so a subsequent start begins a fresh run, and
        // mirror it to the atomic the console status query reads
        interval->m_State = Intervalometer::STATE_IDLE;
        m_IntervalometerState.store(static_cast<uint8_t>(Intervalometer::STATE_IDLE));
        lv_timer_set_period(timer, 100);

        // release shutter and exit
        control.sendCommand(Control::CMD_SHUTTER_RELEASE);
        lv_obj_t *back = lv_menu_get_main_header_back_button(m_MainMenu.main);
        lv_obj_send_event(back, LV_EVENT_CLICKED, m_MainMenu.main);
      },
      LV_EVENT_CLICKED, m_IntervalTimer);

  m_IntervalPageRefresh = lv_timer_create(
      [](lv_timer_t *timer) {
        FURBLE_SIM_TIMER_FIRE("interval_page_refresh");
        auto *label = static_cast<lv_obj_t *>(lv_timer_get_user_data(timer));
        uint32_t now = tick();
        uint32_t remaining = m_IntervalNext > now ? m_IntervalNext - now : 0;
        SpinValue::hms_t hms = SpinValue::toHMS(remaining);
        lv_label_set_text_fmt(label, "%02lu:%02lu:%02lu", hms.hours, hms.minutes, hms.seconds);

        if (!m_IntervalCountdownActive || (remaining == 0)) {
          m_IntervalLastAnnouncedSecond = 0;
        } else if (remaining > 3000) {
          m_IntervalLastAnnouncedSecond = 0;
        } else {
          uint8_t second = static_cast<uint8_t>((remaining + 999) / 1000);
          if ((second >= 1) && (second <= 3) && (second != m_IntervalLastAnnouncedSecond)) {
            Feedback::getInstance().signal(Feedback::COUNTDOWN);
            m_IntervalLastAnnouncedSecond = second;
          }
        }
      },
      333, m_Intervalometer.m_RemainingLabel);
  lv_timer_pause(m_IntervalPageRefresh);

  lv_menu_set_load_page_event(menuIntervalRun.main, m_IntervalStart, menuIntervalRun.page);

  lv_menu_set_load_page_event(menu.main, menu.button, menu.page);
}

void UI::bulbRefresh(void) {
  uint32_t now = tick();
  uint32_t remaining = m_BulbEnd > now ? m_BulbEnd - now : 0;
  SpinValue::hms_t hms = SpinValue::toHMS(remaining);
  lv_label_set_text_fmt(m_Bulb.m_RemainingLabel, "%02lu:%02lu:%02lu", hms.hours, hms.minutes,
                        hms.seconds);
}

void UI::bulbStart(void) {
  if (m_Bulb.m_State == Bulb::STATE_RUNNING) {
    return;
  }

  uint32_t duration = m_Bulb.m_Duration.m_SpinValue.toMilliseconds();
  m_Bulb.m_State = Bulb::STATE_RUNNING;
  m_Bulb.m_StartedAt = tick();

  lv_label_set_text(m_Bulb.m_StateLabel, "Exposing");
  lv_label_set_text(m_Bulb.m_ActionLabel, "Stop");

  // Hold the shutter with the same lock the remote page uses.
  shutterLock(Control::getInstance());

  m_BulbEnd = m_Bulb.m_StartedAt + duration;
  lv_timer_set_period(m_BulbTimer, duration);
  lv_timer_reset(m_BulbTimer);
  lv_timer_resume(m_BulbTimer);

  bulbRefresh();
  lv_timer_resume(m_BulbPageRefresh);
}

void UI::bulbComplete(void) {
  if (m_Bulb.m_State != Bulb::STATE_RUNNING) {
    return;
  }

  uint32_t now = tick();
  uint32_t elapsed = now - m_Bulb.m_StartedAt;
  SpinValue::hms_t hms = SpinValue::toHMS(elapsed);

  m_Bulb.m_State = Bulb::STATE_DONE;
  lv_timer_pause(m_BulbTimer);
  lv_timer_pause(m_BulbPageRefresh);

  // The state guard makes a late timer callback unable to release twice.
  shutterUnlock(Control::getInstance());

  // Show the next exposure length while the completed exposure remains visible.
  m_BulbEnd = now + m_Bulb.m_Duration.m_SpinValue.toMilliseconds();
  bulbRefresh();
  lv_label_set_text_fmt(m_Bulb.m_StateLabel, "Done (%02lu:%02lu:%02lu)", hms.hours, hms.minutes,
                        hms.seconds);
  lv_label_set_text(m_Bulb.m_ActionLabel, "Restart");
}

void UI::bulbStop(void) {
  lv_timer_pause(m_BulbTimer);
  lv_timer_pause(m_BulbPageRefresh);

  if (m_Bulb.m_State == Bulb::STATE_RUNNING) {
    m_Bulb.m_State = Bulb::STATE_IDLE;

    // no-op if the shutter is not held
    shutterUnlock(Control::getInstance());
  } else if (m_Bulb.m_State == Bulb::STATE_DONE) {
    m_Bulb.m_State = Bulb::STATE_IDLE;
  }

  m_BulbEnd = tick();
  bulbRefresh();
}

void UI::updateBulbModeHint(void) {
  // Keep this setter as the hook for future BLE detected camera mode status.
  lv_label_set_text(m_Bulb.m_ModeHintLabel, m_BulbModeHintStr);
}

void UI::addBulbMenu(const menu_t &parent) {
  menu_t &menu = addMenu(m_RemoteBulb, &icon_camera, true, parent);
  menu_t &menuBulbRun = addMenu(m_BulbRunStr, NULL, false, menu);

  addSpinnerPage(menu, m_BulbDurationStr, m_Bulb.m_Duration);

  m_Bulb.m_ModeHintLabel = lv_label_create(menu.page);
  lv_obj_set_width(m_Bulb.m_ModeHintLabel, LV_PCT(100));
  lv_label_set_long_mode(m_Bulb.m_ModeHintLabel, LV_LABEL_LONG_CLIP);
  lv_obj_set_style_text_font(m_Bulb.m_ModeHintLabel, &lv_font_montserrat_12, LV_PART_MAIN);
  lv_obj_set_style_text_align(m_Bulb.m_ModeHintLabel, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_set_style_text_letter_space(m_Bulb.m_ModeHintLabel, -2, LV_PART_MAIN);
  updateBulbModeHint();

  m_BulbStart = lv_button_create(menu.page);
  lv_obj_t *startLabel = lv_label_create(m_BulbStart);
  lv_label_set_text(startLabel, "Start");
  lv_obj_center(startLabel);
  lv_obj_add_flag(m_BulbStart, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
  addToInputGroup(m_Group, m_BulbStart);

  lv_obj_add_event_cb(
      m_BulbStart,
      [](lv_event_t *e) {
        auto *ui = static_cast<UI *>(lv_event_get_user_data(e));
        ui->bulbStart();
      },
      LV_EVENT_CLICKED, this);

  lv_obj_t *cont = lv_menu_cont_create(menuBulbRun.page);
  lv_obj_set_height(cont, LV_PCT(100));
  lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);

  m_Bulb.m_StateLabel = lv_label_create(cont);
  m_Bulb.m_RemainingLabel = lv_label_create(cont);

  lv_obj_t *stop = lv_button_create(cont);
  m_Bulb.m_ActionLabel = lv_label_create(stop);
  lv_label_set_text(m_Bulb.m_ActionLabel, "Stop");
  lv_obj_add_flag(stop, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
  addToInputGroup(m_Group, stop);
  lv_obj_add_event_cb(
      stop,
      [](lv_event_t *e) {
        auto *ui = static_cast<UI *>(lv_event_get_user_data(e));

        if (ui->m_Bulb.m_State == Bulb::STATE_DONE) {
          ui->bulbStart();
          return;
        }

        // Cancel the exposure, release the shutter, and exit.
        ui->bulbStop();
        lv_obj_t *back = lv_menu_get_main_header_back_button(m_MainMenu.main);
        lv_obj_send_event(back, LV_EVENT_CLICKED, m_MainMenu.main);
      },
      LV_EVENT_CLICKED, this);

  // one shot exposure timer, the period is the exposure duration
  m_BulbTimer = lv_timer_create(
      [](lv_timer_t *timer) {
        FURBLE_SIM_TIMER_FIRE("bulb_timer");
        auto *ui = static_cast<UI *>(lv_timer_get_user_data(timer));
        ui->bulbComplete();
      },
      1000, this);
  lv_timer_pause(m_BulbTimer);

  m_BulbPageRefresh = lv_timer_create(
      [](lv_timer_t *timer) {
        FURBLE_SIM_TIMER_FIRE("bulb_page_refresh");
        auto *ui = static_cast<UI *>(lv_timer_get_user_data(timer));
        ui->bulbRefresh();
      },
      333, this);
  lv_timer_pause(m_BulbPageRefresh);

  bulbRefresh();

  lv_menu_set_load_page_event(menuBulbRun.main, m_BulbStart, menuBulbRun.page);

  lv_menu_set_load_page_event(menu.main, menu.button, menu.page);
}

void UI::addDisplayMenu(const menu_t &parent) {
  menu_t &menu = addMenu(m_DisplayStr, &icon_settings_brightness, true, parent);
  lv_obj_t *cont = lv_menu_cont_create(menu.page);
  lv_obj_set_height(cont, LV_PCT(100));
  lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);

  // Add brightness control
  lv_obj_t *label = lv_label_create(cont);
  lv_label_set_text(label, "Brightness");
  lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_set_width(label, LV_PCT(100));

  lv_obj_t *slider = lv_slider_create(cont);
  lv_obj_set_width(slider, LV_PCT(90));
  lv_obj_add_flag(slider, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
  addToInputGroup(m_Group, slider);

  // m_BrightnessSteps * 16 == 256 == black screen :(
  // limit maximum to m_BrightnessSteps * (16 - 1)
  lv_slider_set_range(slider, m_MinimumBrightness / m_BrightnessSteps, m_BrightnessSteps - 1);

  uint8_t brightness = Settings::load<Settings::BRIGHTNESS>();
  lv_slider_set_value(slider, brightness / m_BrightnessSteps, LV_ANIM_ON);

  lv_obj_add_event_cb(
      slider,
      [](lv_event_t *e) {
        auto *ui = static_cast<UI *>(lv_event_get_user_data(e));
        auto *slider = static_cast<lv_obj_t *>(lv_event_get_target(e));
        lv_event_code_t code = lv_event_get_code(e);

        switch (code) {
          case LV_EVENT_VALUE_CHANGED:
          {
            auto brightness = lv_slider_get_value(slider) * m_BrightnessSteps;
            M5.Display.setBrightness(brightness);
            break;
          }
          case LV_EVENT_FOCUSED:
            if (lv_obj_has_state(slider, LV_STATE_EDITED)) {
              ui->configureControl(ControlMode::SLIDER);
            } else {
              ui->configureControl(ControlMode::MENU);
            }
            break;
          default:
            break;
        }
      },
      LV_EVENT_ALL, this);

  lv_obj_add_event_cb(
      slider,
      [](lv_event_t *e) {
        auto *slider = static_cast<lv_obj_t *>(lv_event_get_target(e));
        auto brightness = lv_slider_get_value(slider) * m_BrightnessSteps;
        Settings::save<Settings::BRIGHTNESS>(brightness);
      },
      LV_EVENT_RELEASED, NULL);

  // Add inactivity timeout control
  label = lv_label_create(cont);
  lv_label_set_text(label, "Inactivity timeout");
  lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_set_width(label, LV_PCT(100));

  lv_obj_t *roller = lv_roller_create(cont);
  lv_obj_set_width(roller, LV_PCT(90));
  lv_obj_add_flag(roller, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
  addToInputGroup(m_Group, roller);
  lv_roller_set_options(roller, "Never\n30 secs\n60 secs\n2 mins\n5 mins\n10 mins",
                        LV_ROLLER_MODE_INFINITE);
  lv_roller_set_visible_row_count(roller, 2);
  uint8_t inactivity = Settings::load<Settings::INACTIVITY>();
  lv_roller_set_selected(roller, inactivityIndex(inactivity), LV_ANIM_ON);

  lv_obj_add_event_cb(
      roller,
      [](lv_event_t *e) {
        auto *ui = static_cast<UI *>(lv_event_get_user_data(e));
        auto *roller = static_cast<lv_obj_t *>(lv_event_get_target(e));
        size_t index = lv_roller_get_selected(roller);
        if (index >= m_InactivityValues.size()) {
          index = m_InactivityValues.size() - 1;
        }
        uint8_t inactivity = m_InactivityValues[index];
        Settings::save<Settings::INACTIVITY>(inactivity);
        ui->setInactivityTimeout(inactivity);
      },
      LV_EVENT_VALUE_CHANGED, this);

  // Add screen off control
  label = lv_label_create(cont);
  lv_label_set_text(label, "Screen off");
  lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_set_width(label, LV_PCT(100));

  roller = lv_roller_create(cont);
  lv_obj_set_width(roller, LV_PCT(90));
  lv_obj_add_flag(roller, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
  addToInputGroup(m_Group, roller);
  lv_roller_set_options(roller,
                        M5.Touch.isEnabled() ? m_DisplayOffTouchOptions : m_DisplayOffOptions,
                        LV_ROLLER_MODE_INFINITE);
  lv_roller_set_visible_row_count(roller, 2);
  uint8_t displayOff = m_DisplayOffMode;
  if (displayOff > 2) {
    displayOff = 0;
  }
  if (M5.Touch.isEnabled() && displayOff == 2) {
    displayOff = 1;
  }
  lv_roller_set_selected(roller, displayOff, LV_ANIM_ON);

  lv_obj_add_event_cb(
      roller,
      [](lv_event_t *e) {
        auto *ui = static_cast<UI *>(lv_event_get_user_data(e));
        auto *roller = static_cast<lv_obj_t *>(lv_event_get_target(e));
        uint8_t displayOff = lv_roller_get_selected(roller);
        if (M5.Touch.isEnabled() && displayOff > 1) {
          displayOff = 1;
        }
        Settings::save<Settings::DISPLAY_OFF>(displayOff);
        ui->m_DisplayOffMode = displayOff;
      },
      LV_EVENT_VALUE_CHANGED, this);

  if (M5.Touch.isEnabled()) {
    lv_obj_t *calibrate_button = lv_button_create(cont);
    lv_obj_t *calibrate_label = lv_label_create(calibrate_button);
    lv_label_set_text(calibrate_label, "Calibrate");
    lv_obj_add_flag(calibrate_button, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    addToInputGroup(m_Group, calibrate_button);

    lv_obj_add_event_cb(
        calibrate_button,
        [](lv_event_t *e) {
          auto *calibrationUI = static_cast<CalibrationUI *>(lv_event_get_user_data(e));
          calibrationUI->calibrate();
        },
        LV_EVENT_CLICKED, &m_CalibrationUI);
  }

  // Add title visibility control
  addSettingItem(cont, NULL, Settings::SHOW_TITLE);

  lv_menu_set_load_page_event(menu.main, menu.button, menu.page);
}

void UI::addTextSizeMenu(const menu_t &parent) {
  menu_t &menu = addMenu(m_TextSizeStr, &icon_clear_all_24, true, parent);
  lv_obj_t *cont = lv_menu_cont_create(menu.page);
  lv_obj_set_size(cont, LV_PCT(100), LV_PCT(100));
  lv_obj_set_layout(cont, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);

  lv_obj_t *roller = lv_roller_create(cont);
#if !defined(FURBLE_M5COREX)
  lv_obj_set_width(roller, LV_PCT(90));
#endif
  // The 80x160 M5StickC cannot fit the Large font, so it drops Large and offers
  // only Small and Normal. Every other board offers all three.
  const char *options =
      (TextSizePolicy::MAX >= Settings::TEXT_SIZE_LARGE) ? "Small\nNormal\nLarge" : "Small\nNormal";
  lv_roller_set_options(roller, options, LV_ROLLER_MODE_INFINITE);
  lv_roller_set_visible_row_count(roller, 2);
  // Clamp the stored size to this board's maximum so a value carried in from a
  // larger board lands on a valid roller row instead of running off the end.
  uint8_t textSize = TextSizePolicy::clamp(Settings::load<Settings::TEXT_SIZE>());
  lv_roller_set_selected(roller, textSize, LV_ANIM_OFF);

  // Explain the missing option on the boards that drop Large, so the shorter
  // list does not look like a bug.
  if (TextSizePolicy::MAX < Settings::TEXT_SIZE_LARGE) {
    lv_obj_t *note = lv_label_create(cont);
    lv_obj_set_width(note, LV_PCT(90));
    lv_label_set_long_mode(note, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(note, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(note, "Large needs a bigger screen");
  }

  lv_obj_t *restart = lv_button_create(cont);
  lv_obj_t *restartLabel = lv_label_create(restart);
  lv_label_set_text(restartLabel, "Restart");
  lv_obj_add_event_cb(
      restart,
      [](lv_event_t *e) {
        auto *roller = static_cast<lv_obj_t *>(lv_event_get_user_data(e));
        Settings::save<Settings::TEXT_SIZE>(lv_roller_get_selected(roller));
#if defined(FURBLE_M5STICKS3)
        Platform::getInstance().watchdogEnable(false);
#endif
        esp_restart();
      },
      LV_EVENT_CLICKED, roller);

  lv_menu_set_load_page_event(menu.main, menu.button, menu.page);
}

void UI::setBatteryStyle(uint8_t style) {
  if (style == Settings::BATT_STYLE_PERCENT) {
    lv_obj_add_flag(m_Status.batteryIcon, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_clear_flag(m_Status.batteryIcon, LV_OBJ_FLAG_HIDDEN);
  }

  if (style == Settings::BATT_STYLE_ICON) {
    lv_obj_add_flag(m_Status.batteryLabel, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_clear_flag(m_Status.batteryLabel, LV_OBJ_FLAG_HIDDEN);
  }
}

void UI::setShowTitle(bool show) {
#if defined(FURBLE_RIG)
  (void)show;
  lv_obj_clear_flag(m_Status.title, LV_OBJ_FLAG_HIDDEN);
#else
  if (show) {
    lv_obj_clear_flag(m_Status.title, LV_OBJ_FLAG_HIDDEN);
  } else {
    // the sticks have little header width, the icons take the space instead
    lv_obj_add_flag(m_Status.title, LV_OBJ_FLAG_HIDDEN);
  }
#endif
}

void UI::batteryUpdate(lv_timer_t *timer) {
  FURBLE_SIM_TIMER_FIRE("battery_timer");
  auto *status = static_cast<status_t *>(lv_timer_get_user_data(timer));
  auto &platform = Platform::getInstance();

  const auto &caps = platform.getBatteryCaps();

  // sampleBattery() smooths level/voltage/current with an EWMA internally and
  // honours the battery caps, so the mean values are pulled straight from it.
  const auto sample = platform.sampleBattery();
  status->battery = sample.battery;
  status->meanLevel = sample.meanLevel;
  status->meanVoltage = sample.meanVoltage;
  status->meanCurrent = sample.meanCurrent;
  status->displayLevel = sample.displayLevel;
  status->sampleCount++;

  if (caps.level) {
    // feed the smoothed level, the raw reading jitters across the threshold
    Feedback::getInstance().updateBattery(status->displayLevel,
                                          caps.charging && status->battery.charging);
  }

  if (status->batteryLevel != nullptr) {
    lv_label_set_text_fmt(status->batteryLevel, "Level: %u%%", status->displayLevel);
  }

  if (status->batteryVoltage != nullptr) {
    uint32_t mv = lroundf(status->meanVoltage);
    lv_label_set_text_fmt(status->batteryVoltage, "Volts: %lu.%03lu", mv / 1000, mv % 1000);
  }

  if (status->batteryCurrent != nullptr) {
    lv_label_set_text_fmt(status->batteryCurrent, "Current: %ld mA", status->battery.current);
  }

  if (status->batteryCharging != nullptr) {
    lv_label_set_text(status->batteryCharging,
                      status->battery.charging ? "Charging: yes" : "Charging: no");
  }

  if (status->batteryRuntime != nullptr) {
    if (status->battery.charging) {
      lv_label_set_text(status->batteryRuntime, "Runtime: charging");
    } else if (status->meanCurrent < -1.0f) {
      // remaining capacity in mAh divided by the average discharge current
      float remaining = platform.getBatteryCapacity() * (status->meanLevel / 100.0f);
      uint32_t minutes = (remaining / -status->meanCurrent) * 60.0f;
      lv_label_set_text_fmt(status->batteryRuntime, "Runtime: ~%luh%02lum (est)", minutes / 60,
                            minutes % 60);
    } else {
      lv_label_set_text(status->batteryRuntime, "Runtime: unknown");
    }
  }

#if FURBLE_BATTERY_DEBUG == 1
  ESP_LOGI("battery", "uptime=%lu level=%u voltage=%u current=%ld mean=%.1f charging=%u fail=%lu",
           platform.tick() / 1000, status->battery.level, status->battery.voltage,
           status->battery.current, status->meanCurrent, status->battery.charging,
           platform.getBatteryFailCount());
#endif
}

void UI::addBatteryMenu(const menu_t &parent) {
  menu_t &menu = addMenu(m_BatteryStr, &icon_battery_android_frame_full, true, parent);
  auto &platform = Platform::getInstance();
  const auto &caps = platform.getBatteryCaps();

  lv_obj_t *cont = lv_menu_cont_create(menu.page);
  lv_obj_set_width(cont, LV_PCT(100));
  lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);

  auto addRow = [cont]() {
    lv_obj_t *label = lv_label_create(cont);
    lv_label_set_text(label, "");
    lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(label, LV_PCT(100));

    return label;
  };

  // only add the rows this board can actually measure
  if (caps.level) {
    m_Status.batteryLevel = addRow();
  }

  if (caps.voltage) {
    m_Status.batteryVoltage = addRow();
  }

  if (caps.current) {
    m_Status.batteryCurrent = addRow();
  }

  if (caps.charging) {
    m_Status.batteryCharging = addRow();
  }

  if (caps.current && (platform.getBatteryCapacity() > 0)) {
    m_Status.batteryRuntime = addRow();
  }

  // fill the page with the current values
  lv_timer_ready(m_BatteryTimer);

  lv_menu_set_load_page_event(menu.main, menu.button, menu.page);
}

void UI::addPowerMenu(const menu_t &parent) {
  menu_t &menu = addMenu(m_PowerStr, &icon_power_settings_new, true, parent);

  // Battery Saver is the one-switch low-power profile. It overrides a bundle of
  // power settings when on and leaves the stored individual settings untouched,
  // so turning it off restores the user's own choices. Opt-in, default off.
  addSettingItem(menu.page, NULL, Settings::BATTERY_SAVER);

  // Static, non-focusable hint under the switch, matching the one-button hint
  // precedent. It is never added to an input group so it takes no encoder focus.
  lv_obj_t *batterySaverHint = lv_label_create(menu.page);
  lv_obj_set_width(batterySaverHint, LV_PCT(100));
  lv_label_set_long_mode(batterySaverHint, LV_LABEL_LONG_WRAP);
  lv_label_set_text(batterySaverHint,
                    "Battery Saver: one switch for connection saver, 60s screen off, reconnect "
                    "backoff, balanced scan, and light sleep while connected on StickS3. Applies "
                    "after a reboot and keeps your own settings.");

  // Only the StickS3 has a Bluetooth controller configured for modem sleep, so
  // the switch does nothing on the other boards. Leave it out there.
  bool sleepConn = (M5.getBoard() == m5::board_t::board_M5StickS3);
  if (sleepConn) {
    addSettingItem(menu.page, NULL, Settings::SLEEP_CONN);
  }

  // IP5306 boards do not have a reliable software power-off path.
  const bool policies = (M5.getBoard() != m5::board_t::board_M5Stack);

  lv_obj_t *cont = lv_menu_cont_create(menu.page);
  lv_obj_set_width(cont, LV_PCT(100));
  // The Battery Saver switch and hint are always present above this container,
  // so the rollers always share the page rather than centring on their own.
  lv_obj_set_height(cont, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);

  auto addPowerRoller = [cont](const char *text, const char *options) {
    lv_obj_t *label = lv_label_create(cont);
    lv_label_set_text(label, text);
    lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(label, LV_PCT(100));

    lv_obj_t *roller = lv_roller_create(cont);
    lv_obj_set_width(roller, LV_PCT(90));
    lv_roller_set_options(roller, options, LV_ROLLER_MODE_INFINITE);
    lv_roller_set_visible_row_count(roller, 2);
    lv_obj_add_flag(roller, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    return roller;
  };

  // Add CPU maximum frequency control
  lv_obj_t *label = lv_label_create(cont);
  lv_label_set_text(label, "CPU speed");
  lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_set_width(label, LV_PCT(100));

  const auto &frequencies = Platform::CPU_MAX_FREQ_MHZ;
  std::string options;
  for (const auto mhz : frequencies) {
    if (!options.empty()) {
      options += "\n";
    }
    options += std::to_string(mhz) + " MHz";
  }

  lv_obj_t *roller = lv_roller_create(cont);
  lv_obj_set_width(roller, LV_PCT(90));
  lv_obj_add_flag(roller, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
  addToInputGroup(m_Group, roller);
  lv_roller_set_options(roller, options.c_str(), LV_ROLLER_MODE_INFINITE);
  lv_roller_set_visible_row_count(roller, 2);
  lv_obj_add_flag(roller, LV_OBJ_FLAG_SCROLL_ON_FOCUS);

  // The roller index is not the frequency, map it explicitly
  // getCPUMaxFreq() only ever returns a listed frequency, so the find succeeds
  auto &platform = Platform::getInstance();
  uint32_t index =
      std::distance(frequencies.begin(),
                    std::find(frequencies.begin(), frequencies.end(), platform.getCPUMaxFreq()));
  lv_roller_set_selected(roller, index, LV_ANIM_OFF);

  lv_obj_add_event_cb(
      roller,
      [](lv_event_t *e) {
        auto *roller = static_cast<lv_obj_t *>(lv_event_get_target(e));
        auto index = lv_roller_get_selected(roller);
        if (index >= Platform::CPU_MAX_FREQ_MHZ.size()) {
          return;
        }

        // Takes effect immediately, save what was actually applied
        auto &platform = Platform::getInstance();
        platform.setCPUMaxFreq(Platform::CPU_MAX_FREQ_MHZ[index]);
        Settings::save<Settings::CPU_FREQ>(platform.getCPUMaxFreq());
      },
      LV_EVENT_VALUE_CHANGED, NULL);

  // Add battery style control
  label = lv_label_create(cont);
  lv_label_set_text(label, Settings::get(Settings::BATT_STYLE).name);
  lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_set_width(label, LV_PCT(100));

  roller = lv_roller_create(cont);
  lv_obj_set_width(roller, LV_PCT(90));
  lv_obj_add_flag(roller, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
  addToInputGroup(m_Group, roller);
  lv_roller_set_options(roller, "Icon\nPercent\nBoth", LV_ROLLER_MODE_INFINITE);
  lv_roller_set_visible_row_count(roller, 2);
  lv_obj_add_flag(roller, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
  lv_roller_set_selected(roller, Settings::load<Settings::BATT_STYLE>(), LV_ANIM_OFF);

  lv_obj_add_event_cb(
      roller,
      [](lv_event_t *e) {
        auto *ui = static_cast<UI *>(lv_event_get_user_data(e));
        auto *roller = static_cast<lv_obj_t *>(lv_event_get_target(e));
        uint8_t style = lv_roller_get_selected(roller);

        Settings::save<Settings::BATT_STYLE>(style);
        ui->setBatteryStyle(style);
      },
      LV_EVENT_VALUE_CHANGED, this);

  if (policies) {
    lv_obj_t *autoOff = addPowerRoller("Auto off", "Never\n5 mins\n10 mins\n30 mins\n60 mins");
    uint8_t minutes = Settings::load<Settings::AUTO_OFF>();
    auto autoOffIt = std::find(m_AutoOffMinutes.begin(), m_AutoOffMinutes.end(), minutes);
    uint32_t autoOffIndex =
        (autoOffIt == m_AutoOffMinutes.end())
            ? 0
            : static_cast<uint32_t>(std::distance(m_AutoOffMinutes.begin(), autoOffIt));
    lv_roller_set_selected(autoOff, autoOffIndex, LV_ANIM_OFF);

    lv_obj_add_event_cb(
        autoOff,
        [](lv_event_t *e) {
          auto *ui = static_cast<UI *>(lv_event_get_user_data(e));
          auto *roller = static_cast<lv_obj_t *>(lv_event_get_target(e));
          uint32_t index = lv_roller_get_selected(roller);
          if (index < UI::m_AutoOffMinutes.size()) {
            Settings::save<Settings::AUTO_OFF>(UI::m_AutoOffMinutes[index]);
            ui->reloadPowerPolicies();
          }
        },
        LV_EVENT_VALUE_CHANGED, this);

    lv_obj_t *lowBattery = addPowerRoller("Low battery", "None\nWarn\nWarn then off");
    uint8_t policy = Settings::load<Settings::LOW_BATT>();
    lv_roller_set_selected(lowBattery, (policy <= 2) ? policy : 0, LV_ANIM_OFF);

    lv_obj_add_event_cb(
        lowBattery,
        [](lv_event_t *e) {
          auto *ui = static_cast<UI *>(lv_event_get_user_data(e));
          auto *roller = static_cast<lv_obj_t *>(lv_event_get_target(e));
          uint32_t index = lv_roller_get_selected(roller);
          if (index <= 2) {
            Settings::save<Settings::LOW_BATT>(static_cast<uint8_t>(index));
            ui->reloadPowerPolicies();
          }
        },
        LV_EVENT_VALUE_CHANGED, this);

    lv_obj_t *help = lv_label_create(cont);
    lv_label_set_text(
        help,
        "Auto off needs no camera connection. Infinite Re-Connect keeps reconnecting.\n"
        "Power off is final. Press the power button to wake.");
    lv_label_set_long_mode(help, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(help, LV_PCT(100));
  }

  // Add the battery page entry below the controls
  addBatteryMenu(menu);

  lv_menu_set_load_page_event(menu.main, menu.button, menu.page);
}

void UI::updateFeedbackVolumeVisibility(Feedback::output_t output) {
  auto &feedback = Feedback::getInstance();
  auto &volume = m_Menu.at(m_FeedbackVolumeStr);
  bool show = feedback.supports(output) && Feedback::outputIncludesSound(output);

  if (show) {
    lv_obj_clear_flag(volume.button, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(volume.button, LV_OBJ_FLAG_HIDDEN);
  }
}

void UI::addFeedbackMenu(const menu_t &parent) {
  auto &feedback = Feedback::getInstance();

  std::array<Feedback::output_t, Feedback::OUTPUT_OPTION_COUNT> outputOptions = {};
  size_t outputCount = feedback.getOutputOptions(outputOptions);
  if (outputCount <= 1) {
    // Only Off is available on this board, a menu with one no-op choice is
    // clutter.
    return;
  }

  menu_t &menu = addMenu(m_FeedbackStr, &icon_notifications_active, true, parent);

  lv_obj_set_flex_flow(menu.page, LV_FLEX_FLOW_COLUMN);

  // Output is filtered from the board capability table. The stored enum is
  // kept intact when it is unavailable, so moving the setting to another
  // supported board restores it.
  lv_obj_t *outputCont = lv_menu_cont_create(menu.page);
  lv_obj_set_flex_flow(outputCont, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_t *outputLabel = lv_label_create(outputCont);
  lv_label_set_text(outputLabel, "Output");
  lv_label_set_long_mode(outputLabel, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_set_flex_grow(outputLabel, 1);

  static constexpr const char *outputNames[] = {
      "Off", "Sound", "Light", "Vibrate", "Sound and Light",
  };
  std::string outputText;
  uint8_t selectedOutput = 0;
  uint8_t storedOutput = Settings::load<uint8_t>(Settings::FB_OUTPUT);
  for (size_t n = 0; n < outputCount; n++) {
    if (!outputText.empty()) {
      outputText += "\n";
    }
    outputText += outputNames[outputOptions[n]];
    if (outputOptions[n] == static_cast<Feedback::output_t>(storedOutput)) {
      selectedOutput = n;
    }
  }

  lv_obj_t *outputRoller = lv_roller_create(outputCont);
  lv_obj_set_width(outputRoller, LV_PCT(60));
  lv_obj_add_flag(outputRoller, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
  addToInputGroup(m_Group, outputRoller);
  lv_roller_set_options(outputRoller, outputText.c_str(), LV_ROLLER_MODE_INFINITE);
  lv_roller_set_visible_row_count(outputRoller, 2);
  lv_roller_set_selected(outputRoller, selectedOutput, LV_ANIM_OFF);
  lv_obj_add_event_cb(
      outputRoller,
      [](lv_event_t *e) {
        auto *ui = static_cast<UI *>(lv_event_get_user_data(e));
        auto *roller = static_cast<lv_obj_t *>(lv_event_get_target(e));
        auto output = Feedback::getInstance().outputForOption(lv_roller_get_selected(roller));
        Settings::save<Settings::FB_OUTPUT>(static_cast<uint8_t>(output));
        ui->updateFeedbackVolumeVisibility(output);
      },
      LV_EVENT_VALUE_CHANGED, this);

  lv_obj_t *restart = lv_button_create(menu.page);
  lv_obj_t *restartLabel = lv_label_create(restart);
  lv_label_set_text(restartLabel, "Restart to apply");
  lv_obj_add_flag(restart, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
  addToInputGroup(m_Group, restart);
  lv_obj_add_event_cb(
      restart,
      [](lv_event_t *) {
#if defined(FURBLE_M5STICKS3)
        Platform::getInstance().watchdogEnable(false);
#endif
        esp_restart();
      },
      LV_EVENT_CLICKED, NULL);

  menu_t &events = addMenu(m_FeedbackEventsStr, NULL, true, menu);
  lv_obj_set_flex_flow(events.page, LV_FLEX_FLOW_COLUMN);
  static const uint8_t shutterMask = Feedback::EVENT_SHUTTER_MASK;
  static const uint8_t countdownMask = Feedback::EVENT_COUNTDOWN_MASK;
  static const uint8_t connectionMask = Feedback::EVENT_CONNECTION_MASK;
  static const uint8_t lowBatteryMask = Feedback::EVENT_LOW_BATTERY_MASK;

  auto addEventSwitch = [this, &events](const char *name, const uint8_t *mask) {
    lv_obj_t *row = lv_menu_cont_create(events.page);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_t *label = lv_label_create(row);
    lv_label_set_text(label, name);
    lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_flex_grow(label, 1);
    lv_obj_t *sw = lv_switch_create(row);
    lv_obj_add_flag(sw, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    addToInputGroup(m_Group, sw);
    if ((Settings::load<uint8_t>(Settings::FB_EVENTS) & *mask) != 0) {
      lv_obj_add_state(sw, LV_STATE_CHECKED);
    }
    lv_obj_add_event_cb(
        sw,
        [](lv_event_t *e) {
          auto *mask = static_cast<const uint8_t *>(lv_event_get_user_data(e));
          auto *sw = static_cast<lv_obj_t *>(lv_event_get_target(e));
          uint8_t events = Settings::load<uint8_t>(Settings::FB_EVENTS);
          if (lv_obj_has_state(sw, LV_STATE_CHECKED)) {
            events |= *mask;
          } else {
            events &= static_cast<uint8_t>(~*mask);
          }
          Settings::save<Settings::FB_EVENTS>(events);
          Feedback::getInstance().reload();
        },
        LV_EVENT_VALUE_CHANGED, const_cast<uint8_t *>(mask));
  };

  addEventSwitch("Shutter fired", &shutterMask);
  addEventSwitch("Countdown", &countdownMask);
  addEventSwitch("Connect and disconnect", &connectionMask);
  addEventSwitch("Low battery", &lowBatteryMask);
  lv_menu_set_load_page_event(events.main, events.button, events.page);

  menu_t &volume = addMenu(m_FeedbackVolumeStr, NULL, true, menu);
  lv_obj_set_flex_flow(volume.page, LV_FLEX_FLOW_COLUMN);
  lv_obj_t *volumeLabel = lv_label_create(volume.page);
  lv_label_set_text(volumeLabel, "Volume");
  lv_obj_set_width(volumeLabel, LV_PCT(100));
  lv_obj_t *volumeSlider = lv_slider_create(volume.page);
  lv_obj_set_width(volumeSlider, LV_PCT(90));
  lv_obj_add_flag(volumeSlider, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
  addToInputGroup(m_Group, volumeSlider);
  lv_slider_set_range(volumeSlider, 0, UINT8_MAX);
  lv_slider_set_value(volumeSlider, Settings::load<uint8_t>(Settings::FB_VOLUME), LV_ANIM_OFF);
  lv_obj_add_event_cb(
      volumeSlider,
      [](lv_event_t *e) {
        auto *ui = static_cast<UI *>(lv_event_get_user_data(e));
        auto *slider = static_cast<lv_obj_t *>(lv_event_get_target(e));
        switch (lv_event_get_code(e)) {
          case LV_EVENT_VALUE_CHANGED:
            // Live preview only, the brightness slider precedent: persist on
            // release, not on every step, to spare NVS.
            Feedback::getInstance().setVolume(static_cast<uint8_t>(lv_slider_get_value(slider)));
            break;
          case LV_EVENT_FOCUSED:
            ui->configureControl(lv_obj_has_state(slider, LV_STATE_EDITED) ? ControlMode::SLIDER
                                                                           : ControlMode::MENU);
            break;
          default:
            break;
        }
      },
      LV_EVENT_ALL, this);

  lv_obj_add_event_cb(
      volumeSlider,
      [](lv_event_t *e) {
        auto *slider = static_cast<lv_obj_t *>(lv_event_get_target(e));
        Settings::save<Settings::FB_VOLUME>(static_cast<uint8_t>(lv_slider_get_value(slider)));
      },
      LV_EVENT_RELEASED, NULL);
  lv_menu_set_load_page_event(volume.main, volume.button, volume.page);
  updateFeedbackVolumeVisibility(static_cast<Feedback::output_t>(storedOutput));

  lv_menu_set_load_page_event(menu.main, menu.button, menu.page);
}

void UI::addThemeMenu(const menu_t &parent) {
  menu_t &menu = addMenu(m_ThemeStr, &icon_palette, true, parent);

  static std::array<std::string, 3> themes = {"Dark", "Default", "Mono Furble"};

  lv_obj_t *cont = lv_menu_cont_create(menu.page);
  lv_obj_set_size(cont, LV_PCT(100), LV_PCT(100));
  lv_obj_set_layout(cont, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_t *roller = lv_roller_create(cont);
#if !defined(FURBLE_M5COREX)
  lv_obj_set_width(roller, LV_PCT(90));
#endif

  std::string options =
      std::accumulate(std::next(themes.begin()), themes.end(), themes[0],
                      [](const std::string &a, const std::string &b) { return a + "\n" + b; });

  lv_roller_set_options(roller, options.c_str(), LV_ROLLER_MODE_INFINITE);
  lv_roller_set_visible_row_count(roller, 2);

  std::string current = Settings::load<Settings::THEME>();
  uint32_t index =
      std::distance(themes.data(), std::find(std::begin(themes), std::end(themes), current));
  lv_roller_set_selected(roller, index, LV_ANIM_OFF);

  lv_obj_t *restart = lv_button_create(cont);
  lv_obj_t *label = lv_label_create(restart);
  lv_label_set_text(label, "Restart");
  lv_obj_add_flag(restart, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
  addToInputGroup(m_Group, restart);

  lv_obj_add_event_cb(
      restart,
      [](lv_event_t *e) {
        auto *roller = static_cast<lv_obj_t *>(lv_event_get_user_data(e));
        auto index = lv_roller_get_selected(roller);
        Settings::save<Settings::THEME>(themes[index]);
        Platform::getInstance().restart();
      },
      LV_EVENT_CLICKED, roller);

  lv_menu_set_load_page_event(menu.main, menu.button, menu.page);
}

void UI::addTransmitPowerMenu(const menu_t &parent) {
  menu_t &menu = addMenu(m_TransmitPowerStr, &icon_cell_tower, true, parent);
  addSettingItem(menu.page, NULL, Settings::TX_ADAPTIVE);

  lv_obj_t *cont = lv_menu_cont_create(menu.page);
  lv_obj_set_size(cont, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_t *slider = lv_slider_create(cont);
  lv_obj_set_width(slider, LV_PCT(80));
  lv_obj_add_flag(slider, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
  addToInputGroup(m_Group, slider);
  lv_slider_set_range(slider, 0, 2);

  uint8_t power = Settings::load<Settings::TX_POWER>();
  lv_slider_set_value(slider, power, LV_ANIM_ON);

  lv_obj_add_event_cb(
      slider,
      [](lv_event_t *e) {
        auto *ui = static_cast<UI *>(lv_event_get_user_data(e));
        auto *slider = static_cast<lv_obj_t *>(lv_event_get_target(e));
        lv_event_code_t code = lv_event_get_code(e);

        switch (code) {
          case LV_EVENT_RELEASED:
          {
            auto power = lv_slider_get_value(slider);
            auto &control = Control::getInstance();
            Settings::save<Settings::TX_POWER>(power);
            control.setPower(Settings::load<esp_power_level_t>(Settings::TX_POWER));
            break;
          }
          case LV_EVENT_FOCUSED:
            if (lv_obj_has_state(slider, LV_STATE_EDITED)) {
              ui->configureControl(ControlMode::SLIDER);
            } else {
              ui->configureControl(ControlMode::MENU);
            }
            break;
          default:
            break;
        }
      },
      LV_EVENT_ALL, this);

  lv_menu_set_load_page_event(menu.main, menu.button, menu.page);
}

lv_obj_t *UI::addInfoRow(lv_obj_t *cont) {
  lv_obj_t *label = lv_label_create(cont);
  lv_obj_set_width(label, LV_PCT(100));
  lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
  lv_label_set_text(label, "");

  // rows are read only, but must be focusable so the button boards can scroll
  lv_obj_add_flag(label, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(label, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
  addToInputGroup(lv_group_get_default(), label);

  return label;
}

const char *UI::getResetReason(void) {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:
      return "Power on";
    case ESP_RST_EXT:
      return "External";
    case ESP_RST_SW:
      return "Software";
    case ESP_RST_PANIC:
      return "Panic";
    case ESP_RST_INT_WDT:
      return "Interrupt watchdog";
    case ESP_RST_TASK_WDT:
      return "Task watchdog";
    case ESP_RST_WDT:
      return "Watchdog";
    case ESP_RST_DEEPSLEEP:
      return "Deep sleep";
    case ESP_RST_BROWNOUT:
      return "Brownout";
    default:
      return "Unknown";
  }
}

void UI::diagnosticsUpdate(lv_timer_t *timer) {
  FURBLE_SIM_TIMER_FIRE("diagnostics_timer");
  auto *diagnostics = static_cast<diagnostics_t *>(lv_timer_get_user_data(timer));
  auto &platform = Platform::getInstance();

  SpinValue::hms_t hms = SpinValue::toHMS(platform.tick());
  uint32_t heap = esp_get_free_heap_size();
  uint32_t minimum = esp_get_minimum_free_heap_size();

  setLabelIfChangedFmt(diagnostics->aboutUptime, "Uptime:\n%02lu:%02lu:%02lu", hms.hours,
                       hms.minutes, hms.seconds);
  setLabelIfChangedFmt(diagnostics->aboutHeap, "Heap:\n%lu B, min %lu B", heap, minimum);
  setLabelIfChangedFmt(diagnostics->deviceUptime, "Uptime:\n%02lu:%02lu:%02lu", hms.hours,
                       hms.minutes, hms.seconds);
  setLabelIfChangedFmt(diagnostics->deviceHeap, "Heap:\n%lu B, min %lu B", heap, minimum);

  if ((diagnostics->powerFrequency != nullptr) || (diagnostics->powerSleep != nullptr)) {
    auto pm = platform.getPMConfig();

    setLabelIfChangedFmt(diagnostics->powerFrequency, "CPU:\n%u to %u MHz", pm.min_freq_mhz,
                         pm.max_freq_mhz);
    setLabelIfChangedFmt(diagnostics->powerSleep, "Light sleep:\n%s",
                         pm.light_sleep_enable ? "on" : "off");
  }

  auto &power = Power::getInstance();
  for (size_t n = 0; n < Power::LOCK_COUNT; n++) {
    if (diagnostics->powerLocks[n] == nullptr) {
      continue;
    }

    const auto type = static_cast<Power::LockType>(n);
    const auto stats = power.getStats(type);
    setLabelIfChangedFmt(diagnostics->powerLocks[n], "%s:\ncount %lu, acquires %lu\nheld %llu ms",
                         power.getName(type), static_cast<unsigned long>(stats.count),
                         static_cast<unsigned long>(stats.totalAcquires),
                         static_cast<unsigned long long>(stats.totalHeldUs / 1000));
  }

  if (diagnostics->ble != nullptr) {
    std::string text;
    for (const auto &target : Control::getInstance().getTargets()) {
      auto camera = target->getCamera();
      if (!camera->isConnected()) {
        continue;
      }

      uint16_t interval;
      uint16_t latency;
      uint16_t timeout;
      int rssi;
      if (!camera->getConnParams(interval, latency, timeout, rssi)) {
        continue;
      }

      if (!text.empty()) {
        text += "\n\n";
      }
      text += camera->getName();
      text += "\nProfile: ";
      text += Camera::connProfileName(camera->getConnProfile());
      text += "\nInterval: ";
      text += std::to_string(interval);
      text += " x 1.25 ms\nLatency: ";
      text += std::to_string(latency);
      text += " events\nSupervision: ";
      text += std::to_string(timeout);
      text += " x 10 ms\nRSSI: ";
      text += std::to_string(rssi);
      text += " dBm";
    }

    if (text.empty()) {
      text = "No connected cameras";
    }
    if (diagnostics->bleText != text) {
      lv_label_set_text(diagnostics->ble, text.c_str());
      diagnostics->bleText = text;
    }
  }
}

/**
 * Add a labelled roller row to a menu page.
 *
 * The row is sized to its content so the page keeps flowing, a full height row
 * pushes later rows off screen and stops the page scrolling. The roller is
 * flagged to scroll itself into view, which is the only way the page scrolls
 * under encoder navigation.
 */
static lv_obj_t *addRollerItem(lv_obj_t *page, const char *text, const char *options) {
  lv_obj_t *cont = lv_menu_cont_create(page);
  lv_obj_set_size(cont, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);

  lv_obj_t *label = lv_label_create(cont);
  lv_label_set_text(label, text);
  lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_set_width(label, LV_PCT(100));

  lv_obj_t *roller = lv_roller_create(cont);
  lv_obj_set_width(roller, LV_PCT(90));
  lv_roller_set_options(roller, options, LV_ROLLER_MODE_INFINITE);
  lv_roller_set_visible_row_count(roller, 2);
  lv_obj_add_flag(roller, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
  addToInputGroup(lv_group_get_default(), roller);

  return roller;
}

void UI::addIRSettingsMenu(const menu_t &parent) {
  menu_t &menu = addMenu(m_IRSettingsStr, &icon_settings_remote, true, parent);

  addSettingItem(menu.page, NULL, Settings::IR);

  lv_obj_t *protocol = addRollerItem(menu.page, m_IRProtoStr, m_IRProtoOptions);
  const uint8_t selected =
      static_cast<uint8_t>(IR::clampProtocol(Settings::load<Settings::IR_PROTO>()));
  lv_roller_set_selected(protocol, selected, LV_ANIM_OFF);

  lv_obj_add_event_cb(
      protocol,
      [](lv_event_t *e) {
        auto *roller = static_cast<lv_obj_t *>(lv_event_get_target(e));
        Settings::save<Settings::IR_PROTO>(lv_roller_get_selected(roller));
      },
      LV_EVENT_VALUE_CHANGED, NULL);

  lv_menu_set_load_page_event(menu.main, menu.button, menu.page);
  updateIRMenuVisibility();
}

void UI::updateIRMenuVisibility(void) {
  const bool supported = IR::getInstance().isSupported();
  const bool enabled = supported && Settings::load<Settings::IR>();

  auto &ir = m_Menu.at(m_IRStr);
  if (ir.button != nullptr) {
    if (enabled) {
      lv_obj_clear_flag(ir.button, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(ir.button, LV_OBJ_FLAG_HIDDEN);
    }
  }

  if (m_IRConnectedButton != nullptr) {
    if (enabled) {
      lv_obj_clear_flag(m_IRConnectedButton, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(m_IRConnectedButton, LV_OBJ_FLAG_HIDDEN);
    }
  }

  auto &settings = m_Menu.at(m_IRSettingsStr);
  if (settings.button != nullptr) {
    if (supported) {
      lv_obj_clear_flag(settings.button, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(settings.button, LV_OBJ_FLAG_HIDDEN);
    }
  }
}

void UI::addBluetoothMenu(const menu_t &parent) {
  menu_t &menu = addMenu(m_BluetoothStr, &icon_bluetooth, true, parent);

  lv_obj_set_flex_flow(menu.page, LV_FLEX_FLOW_COLUMN);

  addTransmitPowerMenu(menu);
  addSettingItem(menu.page, NULL, Settings::CONN_SAVER);

  // scan duty cycle preset
  lv_obj_t *modeRoller = addRollerItem(menu.page, "Scan mode", "Full\nBalanced\nLow");
  lv_roller_set_selected(modeRoller, Settings::load<Settings::SCAN_MODE>(), LV_ANIM_OFF);

  lv_obj_add_event_cb(
      modeRoller,
      [](lv_event_t *e) {
        auto *roller = static_cast<lv_obj_t *>(lv_event_get_target(e));
        uint8_t mode = lv_roller_get_selected(roller);
        Settings::save<Settings::SCAN_MODE>(mode);
      },
      LV_EVENT_VALUE_CHANGED, NULL);

  // scan timeout
  lv_obj_t *timeoutRoller =
      addRollerItem(menu.page, "Scan timeout", "Never\n30 secs\n60 secs\n120 secs");

  uint32_t timeout = Settings::load<Settings::SCAN_TIMEOUT>();
  auto it = std::find(m_ScanTimeout.begin(), m_ScanTimeout.end(), timeout);
  if (it != m_ScanTimeout.end()) {
    lv_roller_set_selected(timeoutRoller, std::distance(m_ScanTimeout.begin(), it), LV_ANIM_OFF);
  }

  lv_obj_add_event_cb(
      timeoutRoller,
      [](lv_event_t *e) {
        auto *roller = static_cast<lv_obj_t *>(lv_event_get_target(e));
        Settings::save<Settings::SCAN_TIMEOUT>(m_ScanTimeout[lv_roller_get_selected(roller)]);
      },
      LV_EVENT_VALUE_CHANGED, NULL);

  lv_menu_set_load_page_event(menu.main, menu.button, menu.page);
}

void UI::addAboutMenu(const menu_t &parent) {
  menu_t &menu = addMenu(m_AboutStr, &icon_info, true, parent);
  lv_obj_t *cont = lv_menu_cont_create(menu.page);
  lv_obj_set_width(cont, LV_PCT(100));
  lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t *version = addInfoRow(cont);
  lv_label_set_text_fmt(version, "Version:\n%s", FURBLE_VERSION);

  lv_obj_t *id = addInfoRow(cont);
  lv_label_set_text_fmt(id, "ID:\n%s", Device::getStringID().c_str());

  lv_obj_t *build = addInfoRow(cont);
  lv_label_set_text_fmt(build, "Build:\n%s %s", __DATE__, __TIME__);

  lv_obj_t *idf = addInfoRow(cont);
  lv_label_set_text_fmt(idf, "IDF:\n%s", IDF_VER);

  // filled in by the diagnostics timer whenever the page is open
  m_Diagnostics.aboutUptime = addInfoRow(cont);
  m_Diagnostics.aboutHeap = addInfoRow(cont);

  lv_obj_t *reset = addInfoRow(cont);
  lv_label_set_text_fmt(reset, "Reset:\n%s", getResetReason());

  lv_menu_set_load_page_event(menu.main, menu.button, menu.page);
}

void UI::addDeviceInfoMenu(const menu_t &parent) {
  menu_t &menu = addMenu(m_DeviceInfoStr, NULL, true, parent);
  lv_obj_t *cont = lv_menu_cont_create(menu.page);
  lv_obj_set_width(cont, LV_PCT(100));
  lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  esp_chip_info_t info;
  esp_chip_info(&info);

  uint32_t flash = 0;
  esp_err_t err = esp_flash_get_size(NULL, &flash);
  if (err != ESP_OK) {
    ESP_LOGW("ui", "Unable to read flash size (%s).", esp_err_to_name(err));
    flash = 0;
  }

  lv_obj_t *chip = addInfoRow(cont);
  lv_label_set_text_fmt(chip, "Chip:\n%s rev %u.%u", CONFIG_IDF_TARGET, info.revision / 100,
                        info.revision % 100);

  lv_obj_t *cores = addInfoRow(cont);
  lv_label_set_text_fmt(cores, "Cores: %u", info.cores);

  lv_obj_t *size = addInfoRow(cont);
  lv_label_set_text_fmt(size, "Flash: %lu MB", flash / (1024 * 1024));

  // filled in by the diagnostics timer whenever the page is open
  m_Diagnostics.deviceHeap = addInfoRow(cont);
  m_Diagnostics.deviceUptime = addInfoRow(cont);

  lv_obj_t *reset = addInfoRow(cont);
  lv_label_set_text_fmt(reset, "Reset:\n%s", getResetReason());

  lv_menu_set_load_page_event(menu.main, menu.button, menu.page);
}

void UI::addPowerStateMenu(const menu_t &parent) {
  menu_t &menu = addMenu(m_PowerStateStr, NULL, true, parent);
  lv_obj_t *cont = lv_menu_cont_create(menu.page);
  lv_obj_set_width(cont, LV_PCT(100));
  lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  // filled in by the diagnostics timer whenever the page is open
  m_Diagnostics.powerFrequency = addInfoRow(cont);
  m_Diagnostics.powerSleep = addInfoRow(cont);
  for (size_t n = 0; n < Power::LOCK_COUNT; n++) {
    m_Diagnostics.powerLocks[n] = addInfoRow(cont);
  }

  lv_obj_t *tickless = addInfoRow(cont);
  lv_label_set_text_fmt(tickless, "Tickless idle:\n%s", Platform::hasTicklessIdle() ? "yes" : "no");

  lv_obj_t *configured = addInfoRow(cont);
  lv_label_set_text(configured, "Frequencies are configured, not measured.");

  lv_obj_t *dump = lv_button_create(cont);
  lv_obj_t *label = lv_label_create(dump);
  lv_label_set_text(label, "Dump locks");
  lv_obj_add_flag(dump, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
  addToInputGroup(m_Group, dump);
  lv_obj_add_event_cb(
      dump, [](lv_event_t *e) { Platform::getInstance().dumpPMLocks(); }, LV_EVENT_CLICKED, NULL);

  lv_obj_t *console = addInfoRow(cont);
  lv_label_set_text(console, "Detailed lock stats go to the serial console.");

  lv_menu_set_load_page_event(menu.main, menu.button, menu.page);
}

void UI::addBLEMenu(const menu_t &parent) {
  menu_t &menu = addMenu(m_BLEStr, NULL, true, parent);
  lv_obj_t *cont = lv_menu_cont_create(menu.page);
  lv_obj_set_width(cont, LV_PCT(100));
  lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  // Filled in by the diagnostics timer whenever the page is open.
  m_Diagnostics.ble = addInfoRow(cont);

  lv_menu_set_load_page_event(menu.main, menu.button, menu.page);
}

void UI::addDiagnosticsMenu(const menu_t &parent) {
  menu_t &menu = addMenu(m_DiagnosticsStr, &icon_troubleshoot, true, parent);

  addDeviceInfoMenu(menu);

  // link the battery page rather than building a second one
  auto &battery = m_Menu.at(m_BatteryStr);
  lv_obj_t *button = addMenuItem(menu, &icon_battery_android_frame_full, m_BatteryStr);
  lv_menu_set_load_page_event(menu.main, button, battery.page);

  addPowerStateMenu(menu);
  addBLEMenu(menu);

  lv_menu_set_load_page_event(menu.main, menu.button, menu.page);
}

void UI::updateStorageInfo(lv_obj_t *label) {
  auto &sd = SD::getInstance();

  switch (sd.cardState()) {
    case SD::card_state_t::MOUNTED:
      lv_label_set_text_fmt(label, "%s\nMounted\nCapacity: %lu MB\nFree: %lu MB", m_CardInfoStr,
                            static_cast<unsigned long>(sd.capacityMB()),
                            static_cast<unsigned long>(sd.freeMB()));
      break;
    case SD::card_state_t::FAILED:
      lv_label_set_text_fmt(label, "%s\nMount failed", m_CardInfoStr);
      break;
    case SD::card_state_t::UNMOUNTED:
      lv_label_set_text_fmt(label, "%s\nNot mounted", m_CardInfoStr);
      break;
  }
}

void UI::serviceStorage(void) {
  auto &sd = SD::getInstance();
  if (!sd.isSupported() || (m_StoragePage == nullptr)) {
    return;
  }

  // request a mount on page entry, release the mount hold on page leave
  const bool visible = lv_menu_get_cur_main_page(m_StorageMenuMain) == m_StoragePage;
  if (visible != m_StorageVisible) {
    m_StorageVisible = visible;
    if (visible) {
      sd.request(SD::request_t::MOUNT);
      if (sd.cardState() != SD::card_state_t::MOUNTED) {
        lv_label_set_text_fmt(m_StorageInfoLabel, "%s\nMounting...", m_CardInfoStr);
      } else {
        updateStorageInfo(m_StorageInfoLabel);
      }
    } else {
      sd.request(SD::request_t::PAGE_LEAVE);
    }
  }

  // refresh the storage widgets when the writer task publishes a new state
  const uint32_t generation = sd.generation();
  if (generation != m_StorageGeneration) {
    m_StorageGeneration = generation;
    GPS::getInstance().reloadLogSettings();

    if (m_StorageVisible) {
      updateStorageInfo(m_StorageInfoLabel);
    }

    // reflect an auto-disable after repeated SD failures in the switch
    const bool enabled = Settings::load<Settings::SD_GPX>();
    if ((m_StorageGPXSwitch != nullptr)
        && (lv_obj_has_state(m_StorageGPXSwitch, LV_STATE_CHECKED) != enabled)) {
      if (enabled) {
        lv_obj_add_state(m_StorageGPXSwitch, LV_STATE_CHECKED);
      } else {
        lv_obj_remove_state(m_StorageGPXSwitch, LV_STATE_CHECKED);
      }
    }
  }
}

void UI::addStorageMenu(const menu_t &parent) {
  menu_t &menu = addMenu(m_StorageStr, &icon_save_24, true, parent);
  lv_obj_set_flex_flow(menu.page, LV_FLEX_FLOW_COLUMN);

  addSettingItem(menu.page, NULL, Settings::SD_GPX);

  static constexpr std::array<uint16_t, 6> periods = {1, 2, 5, 10, 30, 60};
  lv_obj_t *periodRoller =
      addRollerItem(menu.page, Settings::get(Settings::GPX_PERIOD).name, "1\n2\n5\n10\n30\n60");
  uint32_t selected = 2;
  const uint16_t period = Settings::load<Settings::GPX_PERIOD>();
  for (size_t i = 0; i < periods.size(); i++) {
    if (period == periods[i]) {
      selected = i;
      break;
    }
  }
  lv_roller_set_selected(periodRoller, selected, LV_ANIM_OFF);
  lv_obj_add_event_cb(
      periodRoller,
      [](lv_event_t *e) {
        const auto *roller = static_cast<lv_obj_t *>(lv_event_get_target(e));
        static constexpr uint16_t values[] = {1, 2, 5, 10, 30, 60};
        const uint32_t selected = lv_roller_get_selected(roller);
        if (selected < (sizeof(values) / sizeof(values[0]))) {
          Settings::save<Settings::GPX_PERIOD>(values[selected]);
          GPS::getInstance().reloadLogSettings();
          SD::getInstance().request(SD::request_t::RELOAD);
        }
      },
      LV_EVENT_VALUE_CHANGED, NULL);

  lv_obj_t *exportButton = lv_button_create(menu.page);
  lv_obj_t *exportLabel = lv_label_create(exportButton);
  lv_label_set_text(exportLabel, m_ExportSettingsStr);
  lv_obj_center(exportLabel);
  lv_obj_add_flag(exportButton, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
  lv_group_add_obj(m_Group, exportButton);
  lv_obj_add_event_cb(
      exportButton,
      [](lv_event_t *e) {
        auto *ui = static_cast<UI *>(lv_event_get_user_data(e));
        ui->showStorageConfirm(false);
      },
      LV_EVENT_CLICKED, this);

  lv_obj_t *importButton = lv_button_create(menu.page);
  lv_obj_t *importLabel = lv_label_create(importButton);
  lv_label_set_text(importLabel, m_ImportSettingsStr);
  lv_obj_center(importLabel);
  lv_obj_add_flag(importButton, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
  lv_group_add_obj(m_Group, importButton);
  lv_obj_add_event_cb(
      importButton,
      [](lv_event_t *e) {
        auto *ui = static_cast<UI *>(lv_event_get_user_data(e));
        ui->showStorageConfirm(true);
      },
      LV_EVENT_CLICKED, this);

  lv_obj_t *cardInfo = lv_menu_cont_create(menu.page);
  lv_obj_set_width(cardInfo, LV_PCT(100));
  lv_obj_t *cardInfoLabel = lv_label_create(cardInfo);
  lv_label_set_text_fmt(cardInfoLabel, "%s\nNot mounted", m_CardInfoStr);
  lv_label_set_long_mode(cardInfoLabel, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(cardInfoLabel, LV_PCT(100));

  // serviceStorage() requests the mount on page entry and refreshes the label
  m_StorageMenuMain = menu.main;
  m_StoragePage = menu.page;
  m_StorageInfoLabel = cardInfoLabel;

  lv_menu_set_load_page_event(menu.main, menu.button, menu.page);
}

void UI::showStorageConfirm(bool import) {
  if (m_StorageMessageBox != nullptr) {
    return;
  }

  m_StorageImport = import;
  m_StorageMessageBox = lv_msgbox_create(m_Screen);
  lv_msgbox_add_title(m_StorageMessageBox, import ? "Import Settings" : "Export Settings");
  lv_msgbox_add_text(m_StorageMessageBox,
                     import ? "Overwrite all settings and restart?" : "Write all settings to SD?");
  lv_obj_set_width(m_StorageMessageBox, LV_PCT(100));
  lv_obj_center(m_StorageMessageBox);

  lv_obj_t *cancel = lv_msgbox_add_footer_button(m_StorageMessageBox, "Cancel");
  lv_obj_t *confirm = lv_msgbox_add_footer_button(m_StorageMessageBox, "Confirm");
  lv_group_add_obj(m_Group, cancel);
  lv_group_add_obj(m_Group, confirm);
  lv_obj_add_event_cb(
      cancel,
      [](lv_event_t *e) { static_cast<UI *>(lv_event_get_user_data(e))->cancelStorageAction(); },
      LV_EVENT_CLICKED, this);
  lv_obj_add_event_cb(
      confirm,
      [](lv_event_t *e) { static_cast<UI *>(lv_event_get_user_data(e))->confirmStorageAction(); },
      LV_EVENT_CLICKED, this);
  lv_group_focus_obj(confirm);
}

void UI::cancelStorageAction(void) {
  if (m_StorageMessageBox == nullptr) {
    return;
  }

  lv_msgbox_close_async(m_StorageMessageBox);
  m_StorageMessageBox = nullptr;
}

void UI::confirmStorageAction(void) {
  if (m_StorageMessageBox == nullptr) {
    return;
  }

  const bool import = m_StorageImport;
  lv_obj_t *messageBox = m_StorageMessageBox;
  m_StorageMessageBox = nullptr;
  lv_msgbox_close_async(messageBox);

  // the SD writer task closes a running track, runs the transfer, and
  // restarts the device after a successful import
  SD::getInstance().request(import ? SD::request_t::IMPORT : SD::request_t::EXPORT);
}

void UI::addSettingsMenu(void) {
  menu_t &menu = addMenu(m_SettingsStr, &icon_settings);

#if defined(FURBLE_M5COREX)
  lv_obj_set_grid_dsc_array(menu.page, m_GridLayoutColDsc.data(),
                            m_SettingsGridLayoutRowDsc.data());
  lv_obj_set_layout(menu.page, LV_LAYOUT_GRID);
#else
#endif
  lv_obj_set_size(menu.page, LV_PCT(100), LV_PCT(100));
  lv_obj_center(menu.page);

  addDisplayMenu(menu);
  addFeaturesMenu(menu);
  addIRSettingsMenu(menu);
  addGPSMenu(menu);
  addIntervalometerMenu(menu);
  addThemeMenu(menu);
  addTextSizeMenu(menu);
  addBluetoothMenu(menu);
  addAboutMenu(menu);
  addPowerMenu(menu);
  addFeedbackMenu(menu);
  // after 'Power', the battery page it builds is linked from diagnostics
  addDiagnosticsMenu(menu);
  if (SD::getInstance().isSupported()) {
    addStorageMenu(menu);
  }

  lv_menu_set_load_page_event(menu.main, menu.button, menu.page);
}

void UI::updateItems(const menu_t &menu) {
  if (CameraList::size() == 0) {
    return;
  }

  addCameraItem(CameraList::size() - 1, menu, MODE_SCAN);
}

void UI::setInactivityTimeout(uint8_t timeout) {
  m_InactivityTimeout = m_InactivityTimeouts[inactivityIndex(timeout)];
}

void UI::processInactivity(void) {
  // Only a pending power-off countdown holds the panel awake through this
  // state machine. The plain warning rides the normal dim and sleep path, a
  // battery at 10 percent must not pin the backlight until it is flat. The
  // idle clock keeps running either way, so auto off is not postponed.
  const bool holdAwake = m_LowBatteryPowerOffPending;
  bool timedOut = !holdAwake && (m_InactivityTimeout > 0)
                  && (lv_disp_get_inactive_time(m_Display) > m_InactivityTimeout);

  if (!timedOut) {
    if (m_DisplayState == DisplayState::DIM) {
      auto brightness = Settings::load<Settings::BRIGHTNESS>();
      M5.Display.setBrightness(brightness);
      m_DisplayState = DisplayState::ACTIVE;
#if defined(FURBLE_SIM)
      Sim::profilerSetDisplayState("on");
#endif
    } else if (m_DisplayState == DisplayState::OFF && !isBlindRemoteActive()) {
      wakeDisplay();
    }
    return;
  }

  switch (m_DisplayOffMode) {
    case 0:
      if (m_DisplayState == DisplayState::OFF) {
        wakeDisplay();
      }
      if (m_DisplayState == DisplayState::ACTIVE) {
        M5.Display.setBrightness(m_MinimumBrightness);
        m_DisplayState = DisplayState::DIM;
#if defined(FURBLE_SIM)
        Sim::profilerSetDisplayState("dim");
#endif
      }
      break;
    case 1:
    case 2:
      if (m_DisplayState == DisplayState::DIM) {
        auto brightness = Settings::load<Settings::BRIGHTNESS>();
        M5.Display.setBrightness(brightness);
        m_DisplayState = DisplayState::ACTIVE;
#if defined(FURBLE_SIM)
        Sim::profilerSetDisplayState("on");
#endif
      }
      if (m_DisplayState == DisplayState::ACTIVE) {
        sleepDisplay();
      }
      break;
    default:
      m_DisplayOffMode = 0;
      break;
  }
}

void UI::reloadPowerPolicies(void) {
  m_AutoOffSetting = Settings::load<Settings::AUTO_OFF>();
  m_LowBattSetting = Settings::load<Settings::LOW_BATT>();

  // a policy change restarts the whole evaluation, including the warn latch
  m_LowBatteryWarned = false;
  m_LowBatteryWarnCount = 0;
  m_LowBatteryOffCount = 0;
  m_LowBatteryPowerOffPending = false;
  closeLowBatteryWarning();
}

void UI::processAutoOff(void) {
  if (m_PoweringOff || (M5.getBoard() == m5::board_t::board_M5Stack)) {
    return;
  }

  // STATE_IDLE also covers an active discovery scan, do not cut it short
  if ((m_AutoOffSetting == 0) || (Control::getInstance().getState() != Control::STATE_IDLE)
      || Scan::getInstance().isActive()) {
    return;
  }

  uint32_t timeout = static_cast<uint32_t>(m_AutoOffSetting) * 60000;
  if (lv_disp_get_inactive_time(m_Display) >= timeout) {
    ESP_LOGI("ui", "Auto power off after %u minutes idle.", m_AutoOffSetting);
    doPowerOff();
  }
}

void UI::showLowBatteryWarning(bool powerOff) {
  // Wake through the display state machine so the SLPIN/SLPOUT dwell, the APB
  // lock and the icon timer stay consistent. processInactivity holds the panel
  // awake only while a power-off countdown is pending, the plain warning lets
  // the display dim and sleep again. The idle clock is never touched.
  wakeDisplay();
  if (m_DisplayState == DisplayState::DIM) {
    M5.Display.setBrightness(Settings::load<Settings::BRIGHTNESS>());
    m_DisplayState = DisplayState::ACTIVE;
  }

  if (m_LowBatteryMessageBox == nullptr) {
    m_LowBatteryMessageBox = lv_msgbox_create(m_Screen);
    lv_msgbox_add_title(m_LowBatteryMessageBox, "Low battery");
    lv_obj_set_width(m_LowBatteryMessageBox, LV_PCT(100));

    lv_obj_t *content = lv_msgbox_get_content(m_LowBatteryMessageBox);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    m_LowBatteryMessage = lv_label_create(content);
    lv_label_set_long_mode(m_LowBatteryMessage, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(m_LowBatteryMessage, LV_PCT(80));

    // Dismissing closes the box. It also cancels a pending power off, the
    // press proves a user is present and the policy re-arms after another
    // qualifying 30 seconds.
    lv_obj_t *dismiss = lv_msgbox_add_footer_button(m_LowBatteryMessageBox, "OK");
    lv_obj_add_event_cb(
        dismiss,
        [](lv_event_t *e) {
          auto *ui = static_cast<UI *>(lv_event_get_user_data(e));
          ui->m_LowBatteryOffCount = 0;
          ui->m_LowBatteryPowerOffPending = false;
          ui->closeLowBatteryWarning();
        },
        LV_EVENT_CLICKED, this);

    // remember where the user was so dismissing puts them back there
    m_LowBatteryPrevFocus = lv_group_get_focused(m_Group);
    lv_group_focus_obj(dismiss);
  }

  lv_label_set_text(m_LowBatteryMessage, powerOff ? m_LowBattCriticalText : m_LowBattWarnText);
}

void UI::closeLowBatteryWarning(void) {
  if (m_LowBatteryMessageBox != nullptr) {
    lv_msgbox_close_async(m_LowBatteryMessageBox);
    m_LowBatteryMessageBox = nullptr;
    m_LowBatteryMessage = nullptr;

    // Put the focus back where it was before the box stole it. The object
    // may have been deleted while the box was open, lv_obj_is_valid walks
    // the tree comparing pointers and never dereferences a stale one.
    if ((m_LowBatteryPrevFocus != nullptr) && lv_obj_is_valid(m_LowBatteryPrevFocus)) {
      lv_group_focus_obj(m_LowBatteryPrevFocus);
    }
    m_LowBatteryPrevFocus = nullptr;
  }
}

void UI::processLowBattery(void) {
  if (m_PoweringOff || (M5.getBoard() == m5::board_t::board_M5Stack)) {
    return;
  }

  const uint8_t policy = m_LowBattSetting;
  if ((policy == 0) || (policy > 2)) {
    m_LowBatteryWarnCount = 0;
    m_LowBatteryOffCount = 0;
    m_LowBatteryPowerOffPending = false;
    closeLowBatteryWarning();
    return;
  }

  const auto &caps = Platform::getInstance().getBatteryCaps();
  if (!caps.level) {
    return;
  }

  // The battery sample already includes the PMIC charging read. Do not wake
  // the PMIC again from this one-second policy check.
  if (caps.charging && m_Status.battery.charging) {
    m_LowBatteryWarnCount = 0;
    m_LowBatteryOffCount = 0;
    m_LowBatteryWarned = false;
    m_LowBatteryPowerOffPending = false;
    closeLowBatteryWarning();
    return;
  }

  // Boards without a charging measurement leave the guard above inert, the
  // device could be sitting on USB power. Warn only, never power off there.
  const bool canPowerOff = (policy == 2) && caps.charging;
  if (!canPowerOff) {
    m_LowBatteryOffCount = 0;
    m_LowBatteryPowerOffPending = false;
  }

  // Hysteresis counts consecutive qualifying battery samples, not wall clock.
  // The battery refreshes every 5 s, six samples in a row is 30 s.
  if (m_Status.sampleCount != m_LowBatterySampleSeen) {
    m_LowBatterySampleSeen = m_Status.sampleCount;

    // A failed M5PM1 read clamps the level to zero, and a total failure also
    // reports zero millivolts, which is no reading rather than an empty pack.
    // Only trust a zero level when a plausible low pack voltage confirms it.
    const bool badRead = (m_Status.battery.level == 0)
                         && (!caps.voltage || (m_Status.battery.voltage == 0)
                             || (m_Status.battery.voltage >= LOW_BATT_VALID_READ_MV));
    if (badRead) {
      m_LowBatteryWarnCount = 0;
      m_LowBatteryOffCount = 0;
    } else {
      // compare the smoothed level, the raw samples jitter under BLE TX bursts
      const uint8_t level = m_Status.displayLevel;

      if (level < LOW_BATT_WARN_LEVEL) {
        if (m_LowBatteryWarnCount < LOW_BATT_QUALIFY_SAMPLES) {
          m_LowBatteryWarnCount++;
        }
        if (!m_LowBatteryWarned && (m_LowBatteryWarnCount >= LOW_BATT_QUALIFY_SAMPLES)) {
          m_LowBatteryWarned = true;
          showLowBatteryWarning(false);
        }
      } else {
        m_LowBatteryWarnCount = 0;
        // recovered above the warn level, retire a lingering warning
        if (!m_LowBatteryPowerOffPending) {
          closeLowBatteryWarning();
        }
      }

      if (canPowerOff) {
        if (level < LOW_BATT_OFF_LEVEL) {
          if (m_LowBatteryOffCount < LOW_BATT_QUALIFY_SAMPLES) {
            m_LowBatteryOffCount++;
          }
          if (!m_LowBatteryPowerOffPending && (m_LowBatteryOffCount >= LOW_BATT_QUALIFY_SAMPLES)) {
            m_LowBatteryPowerOffPending = true;
            m_LowBatteryPowerOffSince = Platform::getInstance().tick();
            m_LowBatteryWarned = true;
            showLowBatteryWarning(true);
          }
        } else {
          m_LowBatteryOffCount = 0;
          if (m_LowBatteryPowerOffPending) {
            // recovered mid countdown, downgrade the box or retire it
            m_LowBatteryPowerOffPending = false;
            if ((level < LOW_BATT_WARN_LEVEL) && (m_LowBatteryMessage != nullptr)) {
              lv_label_set_text(m_LowBatteryMessage, m_LowBattWarnText);
            } else {
              closeLowBatteryWarning();
            }
          }
        }
      }
    }
  }

  // the countdown runs on the one-second path so the deadline holds
  if (m_LowBatteryPowerOffPending
      && ((Platform::getInstance().tick() - m_LowBatteryPowerOffSince)
          >= LOW_BATT_POWER_OFF_DELAY_MS)) {
    doPowerOff();
  }
}

void UI::doPowerOff(void) {
  if (m_PoweringOff) {
    return;
  }

  m_PoweringOff = true;
  closeLowBatteryWarning();

  // TODO(plans/68): prepareRestart on feat/68 needs this same quiesce
  // ordering, a future shared helper should absorb both paths.
#if defined(FURBLE_M5STICKS3)
  // Stop the watchdog first, the BLE teardown below can outlast a feed period.
  Platform::getInstance().watchdogEnable(false);
#endif

  // Quiesce the intervalometer before the link drops. The shutter is held
  // open between the SHUTTER_OPEN and DELAY handlers, release it properly.
  lv_timer_pause(m_IntervalTimer);
  if (m_Intervalometer.m_State == Intervalometer::STATE_DELAY) {
    Control::getInstance().sendCommand(Control::CMD_SHUTTER_RELEASE);
  }
  m_Intervalometer.m_State = Intervalometer::STATE_IDLE;
  m_IntervalometerState.store(static_cast<uint8_t>(Intervalometer::STATE_IDLE));

  if (m_ShutterLock) {
    shutterUnlock(Control::getInstance());
  }

  doDisconnect();

#if defined(FURBLE_M5STACK_CORE)
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
#endif
  if (!Platform::getInstance().powerOff()) {
    // The PMIC refused. Stay alive rather than latching a half-off state
    // with a dead UI, and put the watchdog back the way the user set it.
    ESP_LOGW("ui", "Power off failed, resuming.");
#if defined(FURBLE_M5STICKS3)
    Platform::getInstance().watchdogEnable(Settings::load<Settings::WATCHDOG>());
#endif
    m_PoweringOff = false;
  }
}

void UI::setDisplayMode(uint8_t mode) {
  const bool console = (mode == static_cast<uint8_t>(Settings::CONSOLE));
  if (console == m_DisplayConsole) {
    return;
  }

  if (console) {
    M5.Display.sleep();
    Power::getInstance().release(Power::LockType::APB_FREQ_MAX, "display");
  } else {
    Power::getInstance().acquire(Power::LockType::APB_FREQ_MAX, "display");
    M5.Display.wakeup();
    M5.Display.setBrightness(Settings::load<uint8_t>(Settings::BRIGHTNESS));
    lv_display_trigger_activity(m_Display);
  }

  m_DisplayConsole = console;
}

void UI::handleLockScreen(void) {
  // toggle screen lock on power button double click for touch screens
  if (M5.Touch.isEnabled()) {
    if (M5.BtnPWR.wasDoubleClicked()) {
      m_Status.screenLocked = !m_Status.screenLocked;
    }
  }
}

void UI::task(void) {
  // Keep this loop in step with the headless vUITask() in main.cpp.
  while (true) {
    Platform::getInstance().update();

    m_Mutex.lock();
#if defined(FURBLE_CONSOLE)
    serviceRequests();
#endif
    if (!m_DisplayConsole) {
      handleLockScreen();
#if defined(FURBLE_SIM)
      Sim::profilerBeginUiCycle();
#endif
      lv_task_handler();
#if defined(FURBLE_SIM)
      Sim::profilerEndUiCycle();
#endif
    }
    serviceStorage();
    m_Mutex.unlock();

    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

#if defined(FURBLE_SIM)
bool UI::simulatorHome(void) {
  lv_menu_clear_history(m_MainMenu.main);
  lv_menu_set_page(m_MainMenu.main, m_MainMenu.page);
  lv_group_focus_obj(m_Menu.at(m_ScanStr).button);
  return true;
}

bool UI::simulatorBack(void) {
  lv_obj_t *back = lv_menu_get_main_header_back_button(m_MainMenu.main);
  if (back == nullptr || lv_menu_get_cur_main_page(m_MainMenu.main) == m_MainMenu.page) {
    return false;
  }

  lv_obj_send_event(back, LV_EVENT_CLICKED, m_MainMenu.main);
  return true;
}

bool UI::simPressButton(const char *name, bool hold) {
  const std::string button = name == nullptr ? "" : name;

  // Resolve the silk-screen button to its LVGL input-device role using the same
  // per-board wiring as initInputDevices(). Each board only exposes the buttons
  // the physical device has: the Sticks carry BtnA, BtnB and the side BtnPWR,
  // the Cores carry BtnA/BtnB/BtnC. Pressing a button the board lacks returns
  // false so the scenario fails loudly rather than silently doing nothing.
  lv_indev_t *indev = nullptr;
  switch (M5.getBoard()) {
    case m5::board_t::board_M5StickC:
    case m5::board_t::board_M5StickCPlus:
    case m5::board_t::board_M5StickCPlus2:
    case m5::board_t::board_M5StickS3:
      if (button == "pwr") {
        indev = m_ButtonL;
      } else if (button == "a") {
        indev = m_ButtonO;
      } else if (button == "b") {
        indev = m_ButtonR;
      }
      break;

    case m5::board_t::board_M5Tough:
    case m5::board_t::board_M5StackCore2:
    case m5::board_t::board_M5Stack:
      if (button == "a") {
        indev = m_ButtonL;
      } else if (button == "b") {
        indev = m_ButtonO;
      } else if (button == "c") {
        indev = m_ButtonR;
      }
      break;

    default:
      break;
  }

  if (indev == nullptr) {
    return false;
  }

  // A long press of the left button is furble's universal back escape. This is
  // the exact path buttonPWRRead/buttonARead take through handleLeftLongPress,
  // and it works even on the Remote and blind pages that hide the header back
  // arrow, the F6 case the touch-only sim could not see.
  if (indev == m_ButtonL && hold) {
    navigateBack();
    return true;
  }

  // A short tap feeds the encoder key the read callback reports: the left and
  // right buttons scroll the focus group, the OK button activates the focus.
  lv_group_send_data(m_Group, inputKey(indev));
  return true;
}
#endif
}  // namespace Furble
