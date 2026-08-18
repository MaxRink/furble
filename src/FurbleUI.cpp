#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numeric>
#include <tuple>

#include <M5Unified.h>
#include <esp_chip_info.h>
#include <esp_flash.h>
#include <esp_idf_version.h>
#include <esp_sleep.h>
#include <esp_system.h>
#include <lvgl.h>
#include <src/themes/lv_theme_private.h>

#include <Device.h>
#include <Scan.h>

#include "icons.h"

#include "FurbleCalibrate.h"
#include "FurbleCompanion.h"
#include "FurbleControl.h"
#include "FurbleFeedback.h"
#include "FurbleGPS.h"
#include "FurbleIR.h"
#include "FurblePlatform.h"
#include "FurblePower.h"
#include "FurbleSettings.h"
#include "FurbleUI.h"
#include "interval.h"

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
}  // namespace

std::mutex UI::m_Mutex;

UI::ConnectContext_t UI::m_ConnectContext;

lv_obj_t *UI::m_ScanFinished;

lv_timer_t *UI::m_ConnectTimer;

lv_timer_t *UI::m_GPSDataTimer;

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
    {m_GPSNMEAStr,           {nullptr, nullptr, nullptr, nullptr, {0, 0}}},
    {m_IntervalometerStr,    {nullptr, nullptr, nullptr, nullptr, {3, 0}}},
    {m_IntervalCountStr,     {nullptr, nullptr, nullptr, nullptr, {0, 0}}},
    {m_IntervalDelayStr,     {nullptr, nullptr, nullptr, nullptr, {0, 0}}},
    {m_IntervalShutterStr,   {nullptr, nullptr, nullptr, nullptr, {0, 0}}},
    {m_IntervalWaitStr,      {nullptr, nullptr, nullptr, nullptr, {0, 0}}},
    {m_DisplayStr,           {nullptr, nullptr, nullptr, nullptr, {0, 0}}},
    {m_IRSettingsStr,        {nullptr, nullptr, nullptr, nullptr, {1, 2}}},
    {m_ThemeStr,             {nullptr, nullptr, nullptr, nullptr, {0, 1}}},
    {m_BluetoothStr,         {nullptr, nullptr, nullptr, nullptr, {1, 1}}},
    {m_AboutStr,             {nullptr, nullptr, nullptr, nullptr, {2, 1}}},
    {m_PowerStr,             {nullptr, nullptr, nullptr, nullptr, {3, 1}}},
    {m_FeedbackStr,          {nullptr, nullptr, nullptr, nullptr, {1, 2}}},
    {m_DiagnosticsStr,       {nullptr, nullptr, nullptr, nullptr, {0, 2}}},
    {m_BatteryStr,           {nullptr, nullptr, nullptr, nullptr, {0, 0}}},
    {m_FeedbackEventsStr,    {nullptr, nullptr, nullptr, nullptr, {0, 0}}},
    {m_FeedbackVolumeStr,    {nullptr, nullptr, nullptr, nullptr, {0, 0}}},
    {m_DeviceInfoStr,        {nullptr, nullptr, nullptr, nullptr, {0, 0}}},
    {m_PowerStateStr,        {nullptr, nullptr, nullptr, nullptr, {0, 0}}},
    {m_BLEStr,               {nullptr, nullptr, nullptr, nullptr, {0, 0}}},
    {m_TransmitPowerStr,     {nullptr, nullptr, nullptr, nullptr, {0, 0}}},
    {m_RemoteShutter,        {nullptr, nullptr, nullptr, nullptr, {0, 0}}},
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
  m_DisplayOffMode = Settings::load<Settings::DISPLAY_OFF>();
  if (m_DisplayOffMode > 2) {
    m_DisplayOffMode = 0;
  }
  setInactivityTimeout(Settings::load<Settings::INACTIVITY>());

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

  // start inactivity timer
  m_InactivityTimer = lv_timer_create(
      [](lv_timer_t *t) {
        auto *ui = static_cast<Furble::UI *>(lv_timer_get_user_data(t));
        ui->processInactivity();
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

  initInputDevices();

  setTheme(Settings::load<Settings::THEME>());

  m_Screen = lv_screen_active();

  m_Root = lv_win_create(m_Screen);
  lv_obj_update_layout(m_Root);

  m_Status.title = lv_win_add_title(m_Root, m_Title);
  m_Header = lv_win_get_header(m_Root);

  m_GPS.init();
  m_Status.gps = &m_GPS;
  m_Status.reconnectIcon = addIcon(&icon_all_inclusive);
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
  m_Status.battery = Platform::getInstance().readBattery();
  m_Status.meanLevel = m_Status.battery.level;
  m_Status.meanVoltage = m_Status.battery.voltage;
  m_Status.meanCurrent = m_Status.battery.current;
  m_Status.displayLevel = m_Status.battery.level;
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

  m_GPS.startService();
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
  lv_obj_add_event_cb(
      accept,
      [](lv_event_t *event) {
        auto *ui = static_cast<UI *>(lv_event_get_user_data(event));
        Companion::getInstance().confirmPairing(true);
        ui->closeCompanionPairingDialog();
      },
      LV_EVENT_CLICKED, ui);

  lv_obj_t *reject = lv_msgbox_add_footer_button(ui->m_CompanionPairingDialog, "Reject");
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
  lv_timer_resume(m_IconTimer);
  lv_display_trigger_activity(m_Display);
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

void UI::setTheme(std::string name) {
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
      lv_theme_default_init(display, primary, secondary, dark, LV_FONT_DEFAULT);
  theme = *theme_default;
  lv_theme_set_parent(&theme, theme_default);
  lv_theme_set_apply_cb(&theme, [](lv_theme_t *th, lv_obj_t *obj) {
    if (lv_obj_check_type(obj, &lv_button_class) || lv_obj_check_type(obj, &lv_roller_class)
        || lv_obj_check_type(obj, &lv_slider_class) || lv_obj_check_type(obj, &lv_switch_class)) {
      lv_obj_add_style(obj, &style_button, LV_STATE_FOCUS_KEY);
    } else if (!lv_obj_check_type(obj, &lv_button_class)
               && !lv_obj_check_type(obj, &lv_msgbox_footer_button_class)) {
      lv_obj_add_style(obj, &style_bg, LV_STATE_DEFAULT);
    }

    lv_obj_add_style(obj, &style_no_shadow, LV_STATE_DEFAULT);
    lv_obj_add_style(obj, &style_img, LV_STATE_DEFAULT);
    lv_obj_add_style(obj, &style_button, LV_STATE_FOCUSED);
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

void UI::handleFocus(lv_event_t *e) {
  auto *ui = static_cast<UI *>(lv_event_get_user_data(e));
  auto &control = Control::getInstance();
  lv_event_code_t code = lv_event_get_code(e);
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
        lv_obj_t *back = lv_menu_get_main_header_back_button(m_MainMenu.main);
        lv_obj_send_event(back, LV_EVENT_CLICKED, m_MainMenu.main);
        ui->configureControl(ControlMode::MENU);
      },
      LV_EVENT_CLICKED, this);

  lv_obj_add_event_cb(m_OK, handleShutter, LV_EVENT_ALL, this);

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
  lv_obj_set_style_pad_top(cont, 6, LV_STATE_DEFAULT);
  lv_obj_set_style_pad_bottom(cont, 6, LV_STATE_DEFAULT);
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
      lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
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

lv_obj_t *UI::addCameraItem(Camera *camera, const menu_t &menu, const CameraListMode_t mode) {
  bool checkbox = (mode == MODE_MULTICONNECT);

  lv_obj_t *item = addMenuItem(menu, NULL, camera->getName().c_str(), checkbox);

  switch (mode) {
    case MODE_DELETE:
      lv_obj_add_event_cb(
          item,
          [](lv_event_t *e) {
            auto *camera = static_cast<Camera *>(lv_event_get_user_data(e));

            CameraList::remove(camera);
            refreshDelete();
          },
          LV_EVENT_CLICKED, camera);
      break;
    case MODE_SCAN:
    case MODE_CONNECT:
      lv_obj_add_event_cb(
          item,
          [](lv_event_t *e) {
            auto *camera = static_cast<Camera *>(lv_event_get_user_data(e));
            camera->setActive(true);

            doConnect(e);
          },
          LV_EVENT_CLICKED, camera);
      break;
    case MODE_MULTICONNECT:
      lv_obj_add_event_cb(
          item,
          [](lv_event_t *e) {
            auto *camera = static_cast<Camera *>(lv_event_get_user_data(e));
            auto *check = static_cast<lv_obj_t *>(lv_event_get_target(e));

            camera->setActive(lv_obj_has_state(check, LV_STATE_CHECKED));
          },
          LV_EVENT_VALUE_CHANGED, camera);
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
#if defined(FURBLE_M5STACK_CORE)
        esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
#endif
        Platform::getInstance().powerOff();
      },
      LV_EVENT_CLICKED, NULL);

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
            auto *camera = CameraList::get(0);
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
          // disable 'Back' when intervalometer is running
          lv_obj_remove_state(back, LV_STATE_DISABLED);
        } else if (page == m_Menu.at(m_GPSDataStr).page) {
          // 'GPS Data' is reachable from the connected page, always display 'Back'
          lv_obj_remove_state(back, LV_STATE_DISABLED);
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
    case ControlMode::REVERT:
      configureControl(m_ControlMode);
      break;
  }
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

void UI::showShutterIntervalometer(bool show) {
  if (show) {
    lv_obj_clear_flag(m_IntervalStart, LV_OBJ_FLAG_HIDDEN);
    lv_group_focus_obj(m_IntervalStart);
  } else {
    lv_obj_add_flag(m_IntervalStart, LV_OBJ_FLAG_HIDDEN);
  }
}

void UI::connectTimerHandler(lv_timer_t *timer) {
  auto *ctx = static_cast<ConnectContext_t *>(lv_timer_get_user_data(timer));
  auto &control = Control::getInstance();
  Camera *camera = nullptr;
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
      camera = control.getConnectingCamera();

      if (lv_obj_has_flag(ctx->messageBox, LV_OBJ_FLAG_HIDDEN)) {
        // hide menu, unhide message box
        lv_obj_add_flag(m_MainMenu.main, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ctx->messageBox, LV_OBJ_FLAG_HIDDEN);
        ctx->ui->displayNavigationBar(false);
        ctx->ui->configureControl(ControlMode::MENU, false);

        lv_group_focus_obj(ctx->cancel);
      }

      if (camera != nullptr) {
        lv_label_set_text(ctx->label, camera->getName().c_str());
        lv_bar_set_value(ctx->bar, camera->getConnectProgress(), LV_ANIM_ON);
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
            CameraList::save(target->getCamera());
          }
          ctx->menuName = NULL;
        }

        // everything connected, display menu, hide connection message box
        lv_obj_add_flag(ctx->messageBox, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(m_MainMenu.main, LV_OBJ_FLAG_HIDDEN);
        ctx->ui->displayNavigationBar(true);
        ctx->ui->configureControl(ControlMode::REVERT);
        lv_group_focus_next(lv_group_get_default());
      }
      lv_timer_pause(m_ConnectTimer);
      break;

    case Control::STATE_IDLE:
    case Control::STATE_DISCONNECTING:
      // The disconnect feedback fired above on leaving the active state.
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
          const auto *camera = CameraList::get(n);
          printf("camera%u.name: %s\n", static_cast<unsigned>(n), camera->getName().c_str());
          printf("camera%u.type: %lu\n", static_cast<unsigned>(n),
                 static_cast<unsigned long>(camera->getType()));
        }
        break;

      case Request::GPS_RELOAD:
        GPS::getInstance().reloadSetting();
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
    }
  }
}
#endif

void UI::doConnect(lv_event_t *e) {
  auto &control = Control::getInstance();

  m_ConnectContext.feedbackConnected = false;

  // activate selected cameras
  for (auto n = 0; n < CameraList::size(); n++) {
    auto *camera = CameraList::get(n);
    if (camera->isActive()) {
      control.addActive(camera);
    }
  }

  lv_obj_add_event_cb(
      m_ConnectContext.cancel, [](lv_event_t *e) { doDisconnect(); }, LV_EVENT_CLICKED, NULL);

  control.connectAll(Settings::load<Settings::RECONNECT>());
  lv_timer_ready(m_ConnectTimer);
  lv_timer_resume(m_ConnectTimer);

  menu_t &menu = m_Menu.at(m_ConnectedStr);
  lv_menu_set_page(m_MainMenu.main, menu.page);
}

void UI::doDisconnect(void) {
  lv_timer_pause(m_ConnectTimer);

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

UI::menu_t &UI::addConnectedMenu(void) {
  menu_t &menuConnected = addMenu(m_ConnectedStr, NULL, false);

#if defined(FURBLE_M5COREX)
  // three by two suits the landscape screens, the gap falls in the bottom middle
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
      lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
      lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    }

    m_OK = std::get<1>(buttons[0]);
    m_Right = std::get<1>(buttons[1]);
    m_ShutterLockIcon = std::get<1>(buttons[2]);

    lv_obj_add_event_cb(m_OK, handleShutter, LV_EVENT_ALL, this);
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
        for (size_t n = 0; n < CameraList::size(); n++) {
          auto *camera = CameraList::get(n);
          addCameraItem(camera, menu, multiconnect ? MODE_MULTICONNECT : MODE_CONNECT);
        }

        if (multiconnect) {
          lv_obj_t *multibutton = lv_button_create(menu.page);
          lv_obj_t *label = lv_label_create(multibutton);
          lv_label_set_text(label, "Multi-Connect");
          lv_obj_center(label);
          lv_obj_add_flag(multibutton, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
          addToInputGroup(menu.group, multibutton);
          lv_obj_add_event_cb(multibutton, doConnect, LV_EVENT_CLICKED, e);
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

  scan.setMode(static_cast<Scan::Mode>(Settings::load<Settings::SCAN_MODE>()));
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
    auto *camera = CameraList::get(n);
    addCameraItem(camera, menu, MODE_DELETE);
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
        auto *gpsData = static_cast<menu_t *>(lv_timer_get_user_data(t));
        auto &gps = GPS::getInstance().get();

        static lv_obj_t *age = lv_label_create(gpsData->page);
        lv_label_set_text_fmt(age, "%lus ago", gps.location.age() / 1000);

        static lv_obj_t *satellites = lv_label_create(gpsData->page);
        lv_label_set_text_fmt(satellites, "%lu satellites", gps.satellites.value());

        static lv_obj_t *lat = lv_label_create(gpsData->page);
        lv_label_set_text_fmt(lat, "%.2f°", gps.location.lat());

        static lv_obj_t *lon = lv_label_create(gpsData->page);
        lv_label_set_text_fmt(lon, "%.2f°", gps.location.lng());

        static lv_obj_t *alt = lv_label_create(gpsData->page);
        lv_label_set_text_fmt(alt, "%.2f m", gps.altitude.meters());

#if defined(FURBLE_M5COREX)
        static lv_obj_t *datetime = lv_label_create(gpsData->page);
        lv_label_set_text_fmt(datetime, "%4u-%02u-%02u %02u:%02u:%02u", gps.date.year(),
                              gps.date.month(), gps.date.day(), gps.time.hour(), gps.time.minute(),
                              gps.time.second());
#else
        static lv_obj_t *date = lv_label_create(gpsData->page);
        lv_label_set_text_fmt(date, "%4u-%02u-%02u", gps.date.year(), gps.date.month(),
                              gps.date.day());
        static lv_obj_t *time = lv_label_create(gpsData->page);
        lv_label_set_text_fmt(time, "%02u:%02u:%02u", gps.time.hour(), gps.time.minute(),
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
 * Sentence capture only runs while the page is open, it is the only way to
 * observe whether a $PCAS command was accepted.
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
        auto *ui = static_cast<UI *>(lv_timer_get_user_data(t));
        auto &gps = GPS::getInstance();
        auto &tinygps = gps.get();

        lv_label_set_text_fmt(ui->m_NMEA.fix, "%lu sats, hdop %.1f\n%lus ago",
                              (unsigned long)tinygps.satellites.value(), tinygps.hdop.hdop(),
                              (unsigned long)(tinygps.location.age() / 1000));
        lv_label_set_text_fmt(
            ui->m_NMEA.counters, "rx %lu\nok %lu, bad %lu", (unsigned long)tinygps.charsProcessed(),
            (unsigned long)tinygps.passedChecksum(), (unsigned long)tinygps.failedChecksum());

        std::string text;
        for (const auto &sentence : gps.getSentences()) {
          text += sentence + "\n";
        }
        lv_label_set_text(ui->m_NMEA.sentences, text.c_str());
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

  addSettingItem(menu.page, NULL, Settings::AUTOCONNECT);
  addSettingItem(menu.page, NULL, Settings::FAUXNY);
  addSettingItem(menu.page, NULL, Settings::RECONNECT);
  addSettingItem(menu.page, NULL, Settings::RECON_BACKOFF);
  addSettingItem(menu.page, NULL, Settings::MULTICONNECT);
  addSettingItem(menu.page, NULL, Settings::COMPANION);
#if defined(FURBLE_M5STICKS3)
  addSettingItem(menu.page, NULL, Settings::WATCHDOG);
#endif

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

  if ((spinner.m_SpinValue.m_Unit != SpinValue::UNIT_NIL)
      && (spinner.m_SpinValue.m_Unit != SpinValue::UNIT_INF)) {
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

      if ((spinner.m_SpinValue.m_Unit != SpinValue::UNIT_NIL)
          && (spinner.m_SpinValue.m_Unit != SpinValue::UNIT_INF)) {
        lv_obj_set_style_pad_left(spinner.m_RollerUnit, 2, LV_STATE_DEFAULT);
        lv_obj_set_style_pad_right(spinner.m_RollerUnit, 2, LV_STATE_DEFAULT);
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

  lv_obj_t *stop = lv_button_create(cont);
  lv_obj_t *stopLabel = lv_label_create(stop);
  lv_label_set_text(stopLabel, "Stop");
  lv_obj_add_flag(stop, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
  addToInputGroup(m_Group, stop);
  lv_obj_add_event_cb(
      stop,
      [](lv_event_t *e) {
        auto *timer = static_cast<lv_timer_t *>(lv_event_get_user_data(e));
        auto &control = Control::getInstance();

        // pause all interval timers
        lv_timer_pause(timer);
        lv_timer_pause(m_IntervalPageRefresh);
        m_IntervalCountdownActive = false;
        m_IntervalLastAnnouncedSecond = 0;

        // release shutter and exit
        control.sendCommand(Control::CMD_SHUTTER_RELEASE);
        lv_obj_t *back = lv_menu_get_main_header_back_button(m_MainMenu.main);
        lv_obj_send_event(back, LV_EVENT_CLICKED, m_MainMenu.main);
      },
      LV_EVENT_CLICKED, m_IntervalTimer);

  m_IntervalPageRefresh = lv_timer_create(
      [](lv_timer_t *timer) {
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

void UI::bulbStop(void) {
  lv_timer_pause(m_BulbTimer);
  lv_timer_pause(m_BulbPageRefresh);

  // no-op if the shutter is not held
  shutterUnlock(Control::getInstance());

  m_BulbEnd = tick();
  bulbRefresh();
}

void UI::addBulbMenu(const menu_t &parent) {
  menu_t &menu = addMenu(m_RemoteBulb, &icon_camera, true, parent);
  menu_t &menuBulbRun = addMenu(m_BulbRunStr, NULL, false, menu);

  addSpinnerPage(menu, m_BulbDurationStr, m_Bulb.m_Duration);

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
        uint32_t duration = ui->m_Bulb.m_Duration.m_SpinValue.toMilliseconds();

        // hold the shutter with the same lock the remote page uses
        ui->shutterLock(Control::getInstance());

        m_BulbEnd = tick() + duration;
        lv_timer_set_period(m_BulbTimer, duration);
        lv_timer_reset(m_BulbTimer);
        lv_timer_resume(m_BulbTimer);

        ui->bulbRefresh();
        lv_timer_resume(m_BulbPageRefresh);
      },
      LV_EVENT_CLICKED, this);

  lv_obj_t *cont = lv_menu_cont_create(menuBulbRun.page);
  lv_obj_set_height(cont, LV_PCT(100));
  lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);

  m_Bulb.m_RemainingLabel = lv_label_create(cont);

  lv_obj_t *stop = lv_button_create(cont);
  lv_obj_t *stopLabel = lv_label_create(stop);
  lv_label_set_text(stopLabel, "Stop");
  lv_obj_add_flag(stop, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
  addToInputGroup(m_Group, stop);
  lv_obj_add_event_cb(
      stop,
      [](lv_event_t *e) {
        auto *ui = static_cast<UI *>(lv_event_get_user_data(e));

        // release the shutter and exit
        ui->bulbStop();
        lv_obj_t *back = lv_menu_get_main_header_back_button(m_MainMenu.main);
        lv_obj_send_event(back, LV_EVENT_CLICKED, m_MainMenu.main);
      },
      LV_EVENT_CLICKED, this);

  // one shot exposure timer, the period is the exposure duration
  m_BulbTimer = lv_timer_create(
      [](lv_timer_t *timer) {
        auto *ui = static_cast<UI *>(lv_timer_get_user_data(timer));
        ui->bulbStop();
      },
      1000, this);
  lv_timer_pause(m_BulbTimer);

  m_BulbPageRefresh = lv_timer_create(
      [](lv_timer_t *timer) {
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
  auto *status = static_cast<status_t *>(lv_timer_get_user_data(timer));
  auto &platform = Platform::getInstance();
  const auto &caps = platform.getBatteryCaps();

  status->battery = platform.readBattery();

  // the raw readings jitter by a few percent, smooth everything that is
  // displayed with an exponentially weighted moving average
  status->meanLevel += (status->battery.level - status->meanLevel) / 4.0f;
  status->displayLevel = lroundf(status->meanLevel);

  if (caps.level) {
    // feed the smoothed level, the raw reading jitters across the threshold
    Feedback::getInstance().updateBattery(status->displayLevel,
                                          caps.charging && status->battery.charging);
  }

  if (caps.voltage) {
    status->meanVoltage += (status->battery.voltage - status->meanVoltage) / 4.0f;
  }

  if (caps.current) {
    // a slower average, a runtime estimate from a twitchy current is useless,
    // at the 5s sample period this is a time constant of about a minute
    status->meanCurrent += (status->battery.current - status->meanCurrent) / 12.0f;
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

  // Only the StickS3 has a Bluetooth controller configured for modem sleep, so
  // the switch does nothing on the other boards. Leave it out there.
  bool sleepConn = (M5.getBoard() == m5::board_t::board_M5StickS3);
  if (sleepConn) {
    addSettingItem(menu.page, NULL, Settings::SLEEP_CONN);
  }

  lv_obj_t *cont = lv_menu_cont_create(menu.page);
  // share the page with the switch, otherwise keep the roller centred
  lv_obj_set_height(cont, sleepConn ? LV_SIZE_CONTENT : LV_PCT(100));
  lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);

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
#if defined(FURBLE_M5STICKS3)
        Platform::getInstance().watchdogEnable(false);
#endif
        esp_restart();
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
  auto *diagnostics = static_cast<diagnostics_t *>(lv_timer_get_user_data(timer));
  auto &platform = Platform::getInstance();

  SpinValue::hms_t hms = SpinValue::toHMS(platform.tick());
  uint32_t heap = esp_get_free_heap_size();
  uint32_t minimum = esp_get_minimum_free_heap_size();

  if (diagnostics->aboutUptime != nullptr) {
    lv_label_set_text_fmt(diagnostics->aboutUptime, "Uptime:\n%02lu:%02lu:%02lu", hms.hours,
                          hms.minutes, hms.seconds);
  }

  if (diagnostics->aboutHeap != nullptr) {
    lv_label_set_text_fmt(diagnostics->aboutHeap, "Heap:\n%lu B, min %lu B", heap, minimum);
  }

  if (diagnostics->deviceUptime != nullptr) {
    lv_label_set_text_fmt(diagnostics->deviceUptime, "Uptime:\n%02lu:%02lu:%02lu", hms.hours,
                          hms.minutes, hms.seconds);
  }

  if (diagnostics->deviceHeap != nullptr) {
    lv_label_set_text_fmt(diagnostics->deviceHeap, "Heap:\n%lu B, min %lu B", heap, minimum);
  }

  if ((diagnostics->powerFrequency != nullptr) || (diagnostics->powerSleep != nullptr)) {
    auto pm = platform.getPMConfig();

    if (diagnostics->powerFrequency != nullptr) {
      lv_label_set_text_fmt(diagnostics->powerFrequency, "CPU:\n%u to %u MHz", pm.min_freq_mhz,
                            pm.max_freq_mhz);
    }

    if (diagnostics->powerSleep != nullptr) {
      lv_label_set_text_fmt(diagnostics->powerSleep, "Light sleep:\n%s",
                            pm.light_sleep_enable ? "on" : "off");
    }
  }

  if (diagnostics->ble != nullptr) {
    std::string text;
    for (const auto &target : Control::getInstance().getTargets()) {
      auto *camera = target->getCamera();
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
  lv_label_set_text(console, "Locks go to the serial console, without hold times.");

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
  addBluetoothMenu(menu);
  addAboutMenu(menu);
  addPowerMenu(menu);
  addFeedbackMenu(menu);
  // after 'Power', the battery page it builds is linked from diagnostics
  addDiagnosticsMenu(menu);

  lv_menu_set_load_page_event(menu.main, menu.button, menu.page);
}

void UI::updateItems(const menu_t &menu) {
  auto *camera = CameraList::last();

  addCameraItem(camera, menu, MODE_SCAN);
}

void UI::setInactivityTimeout(uint8_t timeout) {
  m_InactivityTimeout = m_InactivityTimeouts[inactivityIndex(timeout)];
}

void UI::processInactivity(void) {
  bool timedOut =
      (m_InactivityTimeout > 0) && (lv_disp_get_inactive_time(m_Display) > m_InactivityTimeout);

  if (!timedOut) {
    if (m_DisplayState == DisplayState::DIM) {
      auto brightness = Settings::load<Settings::BRIGHTNESS>();
      M5.Display.setBrightness(brightness);
      m_DisplayState = DisplayState::ACTIVE;
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
      }
      break;
    case 1:
    case 2:
      if (m_DisplayState == DisplayState::DIM) {
        auto brightness = Settings::load<Settings::BRIGHTNESS>();
        M5.Display.setBrightness(brightness);
        m_DisplayState = DisplayState::ACTIVE;
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

void UI::handleLockScreen(void) {
  // toggle screen lock on power button double click for touch screens
  if (M5.Touch.isEnabled()) {
    if (M5.BtnPWR.wasDoubleClicked()) {
      m_Status.screenLocked = !m_Status.screenLocked;
    }
  }
}

void UI::task(void) {
  while (true) {
    Platform::getInstance().update();

    handleLockScreen();

    m_Mutex.lock();
#if defined(FURBLE_CONSOLE)
    serviceRequests();
#endif
    lv_task_handler();
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
#endif
}  // namespace Furble
