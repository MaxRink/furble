#ifndef FURBLE_UI_H
#define FURBLE_UI_H

#include <array>
#include <atomic>
#include <initializer_list>
#include <mutex>
#include <string>
#include <unordered_map>

#include <lvgl.h>

#include "FurbleCalibrate.h"
#include "FurbleControl.h"
#include "FurbleFeedback.h"
#include "FurbleGPS.h"
#include "FurblePlatform.h"
#include "FurbleSettings.h"
#include "interval.h"

namespace Furble {
class UI {
 public:
  /**
   * UI input control modes.
   *
   * Modifies button/navigation operation.
   */
  enum class ControlMode { MENU, SHUTTER, SLIDER, REVERT };

#if defined(FURBLE_CONSOLE)
  /** Operations the console asks the UI task to carry out on its behalf. */
  enum class Request {
    CONNECT,         /**< arg: saved camera index, negative for the multi-connect selection */
    DISCONNECT,      /**< arg: unused */
    SCAN,            /**< arg: non-zero to start, zero to stop */
    CAMERAS,         /**< arg: non-zero to reload the saved cameras before printing */
    GPS_RELOAD,      /**< arg: unused */
    GPS_POWER,       /**< arg: non-zero to power the external 5V rail */
    IR_RELOAD,       /**< arg: unused */
    FEEDBACK_RELOAD, /**< arg: unused */
    FEEDBACK_TEST,   /**< arg: Feedback::event_t value, bypasses the event mask */
  };

  /**
   * Queue a request for the UI task.
   *
   * LVGL is not thread safe, so anything touching it has to run on the UI task.
   * Safe to call from any task.
   *
   * @return true if the request was queued.
   */
  static bool sendRequest(Request request, int32_t arg);
#endif

  UI(const interval_t &interval);

  void task(void);

#if defined(FURBLE_SIM)
  /** Reset to the root menu for scripted simulator runs. */
  bool simulatorHome(void);

  /** Navigate to the previous LVGL menu page for scripted simulator runs. */
  bool simulatorBack(void);
#endif

  /** Set inactivity timeout in multiples of 30s. */
  void setInactivityTimeout(uint8_t timeout);

  /** Check and/or handle inactivity. */
  void processInactivity(void);

  /**
   * Display/hide navigation bar.
   */
  void displayNavigationBar(bool show);

  /** Configure input control mode. */
  void configureControl(ControlMode mode, bool set = true);

  /** Display shutter intervalometer menu .*/
  void showShutterIntervalometer(bool show);

  /** Lock shutter. */
  void shutterLock(Control &control);

  /** Unlock shutter. */
  void shutterUnlock(Control &control);

#if defined(FURBLE_SIM)
  /** Apply one deterministic scenario action without changing firmware builds. */
  void simScenarioAction(const char *action);
#endif

  /** Current intervalometer state for the companion status record. */
  static uint8_t getIntervalometerState(void);

  /** Number of intervalometer shots remaining for the companion status record. */
  static uint16_t getIntervalometerRemaining(void);

  /** Battery readings shared with the companion status record. */
  static int32_t getBatteryLevel(void);
  static int16_t getBatteryVoltage(void);
  static int32_t getBatteryCurrent(void);
  static int16_t getBatteryVBUSVoltage(void);
  static bool isBatteryCharging(void);

 private:
  typedef struct {
    int32_t column;
    int32_t row;
  } grid_position_t;

  typedef struct {
    lv_obj_t *main;
    lv_group_t *group;
    lv_obj_t *page;
    lv_obj_t *button;
    grid_position_t grid;
  } menu_t;

  typedef struct {
    GPS *gps;
    lv_obj_t *title;
    lv_obj_t *gpsIcon;
    lv_obj_t *batteryIcon;
    lv_obj_t *batteryLabel;
    lv_obj_t *reconnectIcon;
    lv_obj_t *reconnectBackoff;
    // battery page rows, NULL where the board cannot measure them
    lv_obj_t *batteryLevel;
    lv_obj_t *batteryVoltage;
    lv_obj_t *batteryCurrent;
    lv_obj_t *batteryCharging;
    lv_obj_t *batteryRuntime;
    /** Widgets which are only useful while GPS is enabled. */
    std::vector<lv_obj_t *> gpsWidgets;
    bool screenLocked;
    // last battery sample, its smoothed values and the displayed percent
    Platform::battery_t battery;
    float meanLevel;
    float meanVoltage;
    float meanCurrent;
    uint8_t displayLevel;
  } status_t;

  typedef struct {
    // labels refreshed while a diagnostics page is open
    lv_obj_t *aboutUptime;
    lv_obj_t *aboutHeap;
    lv_obj_t *deviceUptime;
    lv_obj_t *deviceHeap;
    lv_obj_t *powerFrequency;
    lv_obj_t *powerSleep;
    lv_obj_t *ble;
    std::string bleText;
  } diagnostics_t;

  /**
   * Owner of a spinner.
   *
   * A spinner edit saves through its owner, so each owner writes only its own
   * setting.
   */
  class SpinnerOwner {
   public:
    virtual ~SpinnerOwner() = default;

    virtual void save(void) = 0;
  };

  /** Labels on the raw NMEA page. */
  typedef struct {
    lv_obj_t *fix;
    lv_obj_t *counters;
    lv_obj_t *sentences;
  } nmea_t;

  class Intervalometer: public SpinnerOwner {
   public:
    class Spinner {
     public:
      Spinner(SpinnerOwner *owner, SpinValue::nvs_t nvs, bool infinite = false)
          : m_Owner {owner}, m_SpinValue {nvs}, m_Infinite {infinite} {};

      static constexpr const char *m_SpinDigitRoller = "0\n1\n2\n3\n4\n5\n6\n7\n8\n9";
      static constexpr const char *m_SpinUnitsRoller = "msec\nsecs\nmins";

      void update(void);
      void updateLabels(void);

      SpinnerOwner *m_Owner;
      SpinValue m_SpinValue;
      lv_obj_t *m_Button;
      lv_obj_t *m_Label;
      lv_obj_t *m_Value;
      const bool m_Infinite;  // Can support infinite?
      lv_obj_t *m_RowInfinite;
      lv_obj_t *m_SwitchInfinite;

      lv_obj_t *m_RowSpinners;
      // array of rollers, 0 = hundred, 1 = ten, 2 = one
      std::array<lv_obj_t *, 3> m_Roller = {nullptr, nullptr, nullptr};
      lv_obj_t *m_RollerUnit = nullptr;
    };

    typedef enum {
      STATE_IDLE,
      STATE_WAIT,
      STATE_SHUTTER_OPEN,
      STATE_DELAY,
      STATE_FINISHED,
    } state_t;

    Intervalometer(const interval_t &interval);

    void save(void) override;

    state_t m_State;
    Spinner m_Count;
    Spinner m_Delay;
    Spinner m_Shutter;
    Spinner m_Wait;

    lv_obj_t *m_StateLabel;
    lv_obj_t *m_CountLabel;
    lv_obj_t *m_RemainingLabel;
  };

  /**
   * Bulb exposure, holds the shutter open for a set duration.
   */
  class Bulb: public SpinnerOwner {
   public:
    Bulb(const SpinValue::nvs_t &duration);

    void save(void) override;

    Intervalometer::Spinner m_Duration;

    lv_obj_t *m_RemainingLabel = nullptr;
  };

  typedef enum { MODE_SCAN, MODE_DELETE, MODE_CONNECT, MODE_MULTICONNECT } CameraListMode_t;

  typedef struct {
    UI *ui;
    lv_obj_t *messageBox;
    lv_obj_t *label;
    lv_obj_t *bar;
    lv_obj_t *cancel;
    const char *menuName;
    bool feedbackConnected;
  } ConnectContext_t;

  static std::mutex m_Mutex;

#if defined(FURBLE_CONSOLE)
  typedef struct {
    Request request;
    int32_t arg;
  } request_t;

  static constexpr UBaseType_t m_RequestQueueLength = 8;

  static QueueHandle_t m_RequestQueue;

  /** Drain the console request queue, called on the UI task with m_Mutex held. */
  void serviceRequests(void);
#endif

  static ConnectContext_t m_ConnectContext;

  const uint32_t m_KeyLeft = LV_KEY_LEFT;
  const uint32_t m_KeyEnter = LV_KEY_ENTER;
  const uint32_t m_KeyRight = LV_KEY_RIGHT;

#if defined(FURBLE_M5STICKS3)
  const uint32_t m_RightYOffset = 65;
#else
  const uint32_t m_RightYOffset = 0;
#endif

#if defined(FURBLE_RIG)
  static constexpr const char *m_Title = "RIG BUILD, NO BLE, NO ENCRYPTION";
#elif (FURBLE_TEST_VERSION + 0)
  static constexpr const char *m_Title = FURBLE_VERSION;
#else
  static constexpr const char *m_Title = FURBLE_STR;
#endif
  static const uint8_t m_BrightnessSteps = 16;

  // main menu
  static constexpr const char *m_ConnectStr = "Connect";
  static constexpr const char *m_ScanStr = "Scan";
  static constexpr const char *m_DeleteStr = "Delete";
  static constexpr const char *m_IRStr = "IR";
  static constexpr const char *m_SettingsStr = "Settings";
  static constexpr const char *m_PowerOffStr = "Off";

  // connected
  static constexpr const char *m_ConnectedStr = "Connected";
  static constexpr const char *m_RemoteShutter = "Remote";
  static constexpr const char *m_RemoteBulb = "Bulb";
  static constexpr const char *m_RemoteInterval = "Interval";
  static constexpr const char *m_RemoteDisconnect = "Disconnect";
  // dodgy hack, add a space so map key is unique
  static constexpr const char *m_RemoteGPSData = "GPS Data ";
  static constexpr const char *m_IntervalometerRunStr = "Intervalometer ";
  static constexpr const char *m_BulbRunStr = "Bulb ";

  // connected->bulb
  static constexpr const char *m_BulbDurationStr = "Duration";

  // settings
  static constexpr const char *m_DisplayStr = "Display";
  static constexpr const char *m_DisplayOffOptions = "Dim\nOff\nOff, remote on";
  static constexpr const char *m_DisplayOffTouchOptions = "Dim\nOff";
  static constexpr const char *m_FeaturesStr = "Features";
  static constexpr const char *m_GPSStr = "GPS";
  static constexpr const char *m_IntervalometerStr = "Timer";
  static constexpr const char *m_ThemeStr = "Theme";
  static constexpr const char *m_IRSettingsStr = "Infrared";
  static constexpr const char *m_BluetoothStr = "Bluetooth";
  static constexpr const char *m_AboutStr = "About";
  static constexpr const char *m_PowerStr = "Power";
  static constexpr const char *m_FeedbackStr = "Feedback";
  static constexpr const char *m_DiagnosticsStr = "Diagnostics";

  // settings->power
  static constexpr const char *m_BatteryStr = "Battery";

  // settings->feedback
  static constexpr const char *m_FeedbackEventsStr = "Feedback Events";
  static constexpr const char *m_FeedbackVolumeStr = "Volume";

  // settings->diagnostics
  static constexpr const char *m_DeviceInfoStr = "Device info";
  static constexpr const char *m_PowerStateStr = "Power state";
  static constexpr const char *m_BLEStr = "BLE";

  // settings->bluetooth
  static constexpr const char *m_TransmitPowerStr = "TX Power";

  // settings->infrared
  static constexpr const char *m_IRProtoStr = "IR Protocol";
  static constexpr const char *m_IRProtoOptions = "Nikon\nSony\nCanon\nCanon 2s";

  /** Scan timeout roller values, in seconds, zero is no timeout. */
  static constexpr std::array<uint32_t, 4> m_ScanTimeout = {0, 30, 60, 120};

  /** Inactivity values stored in NVS, indexed by the display roller. */
  static constexpr std::array<uint8_t, 6> m_InactivityValues = {0, 1, 2, 4, 10, 20};

  /** Inactivity timeout values in milliseconds, indexed by the display roller. */
  static constexpr std::array<uint32_t, 6> m_InactivityTimeouts = {0,      30000,  60000,
                                                                   120000, 300000, 600000};

  // settings->gps
  static constexpr const char *m_GPSDataStr = "GPS Data";
  static constexpr const char *m_GPSRateStr = "Update rate";
  static constexpr const char *m_GPSSentencesStr = "Sentences";
  static constexpr const char *m_GPSConstellationStr = "Constellation";
  static constexpr const char *m_GPSPowerStr = "Power saving";
  static constexpr const char *m_GPSNMEAStr = "Raw NMEA";

  // settings->gps rollers
  static constexpr const char *m_GPSRateOptions = "Default\n1000 ms\n500 ms\n200 ms\n100 ms";
  static constexpr const char *m_GPSSentencesOptions = "Default\nRMC+GGA";
  static constexpr const char *m_GPSConstellationOptions =
      "Default\nGPS\nBDS\nGPS+BDS\nGLONASS\nGPS+GLO\nBDS+GLO\nAll";
  static constexpr const char *m_GPSPowerOptions = "Always on\nStandby (PCAS12)\nRail cycling";
  static constexpr const char *m_GPSDutyOptions = "No standby\n5 s\n10 s\n15 s";

  // settings->intervalometer
  static constexpr const char *m_IntervalCountStr = "Count";
  static constexpr const char *m_IntervalDelayStr = "Delay";
  static constexpr const char *m_IntervalShutterStr = "Shutter";
  static constexpr const char *m_IntervalWaitStr = "Wait";

  static constexpr uint8_t BYTES_PER_PIXEL = (LV_COLOR_FORMAT_GET_SIZE(LV_COLOR_FORMAT_RGB565));
  static constexpr int32_t MAX_WIDTH = 320;
  static constexpr int32_t MAX_HEIGHT = 240;
  static constexpr size_t BUFFER_SIZE = (UI::MAX_WIDTH * (MAX_HEIGHT / 15) * UI::BYTES_PER_PIXEL);

#if defined(FURBLE_M5COREX)
  static constexpr int32_t ICON_MENU_SIZE = 48;
#else
  static constexpr int32_t ICON_MENU_SIZE = 24;
#endif

  static constexpr int32_t ICON_HEADER_SIZE = 24;

  LV_ATTRIBUTE_MEM_ALIGN void *m_Buffer1;
  LV_ATTRIBUTE_MEM_ALIGN void *m_Buffer2;

  /** 'Scan finished' notice on the Scan page, hidden while scanning. */
  static lv_obj_t *m_ScanFinished;

  static lv_timer_t *m_ConnectTimer;
  static lv_timer_t *m_GPSDataTimer;
  static lv_timer_t *m_IntervalPageRefresh;
  static uint32_t m_IntervalNext;
  static std::atomic<uint8_t> m_IntervalometerState;
  static std::atomic<uint16_t> m_IntervalometerRemaining;
  static bool m_IntervalCountdownActive;
  static uint8_t m_IntervalLastAnnouncedSecond;

  static lv_timer_t *m_BulbTimer;
  static lv_timer_t *m_BulbPageRefresh;
  static uint32_t m_BulbEnd;

  lv_timer_t *m_IntervalTimer;
  lv_timer_t *m_InactivityTimer;
  lv_timer_t *m_IconTimer;
  lv_timer_t *m_BatteryTimer;
  lv_timer_t *m_DiagnosticsTimer;
  lv_timer_t *m_CompanionPairingTimer = nullptr;
  lv_obj_t *m_CompanionPairingDialog = nullptr;
  lv_obj_t *m_CompanionPairingPrevFocus = nullptr;

  const std::vector<int32_t> m_GridLayoutColDsc = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1),
                                                   LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
  const std::vector<int32_t> m_GridLayoutRowDsc = {LV_GRID_FR(1), LV_GRID_FR(1),
                                                   LV_GRID_TEMPLATE_LAST};

  // the settings page holds more entries than the main menu, give it its own
  // rows so the main menu keeps its layout
  const std::vector<int32_t> m_SettingsGridLayoutRowDsc = {LV_GRID_FR(1), LV_GRID_FR(1),
                                                           LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};

  GPS &m_GPS;

  int32_t m_Width;
  int32_t m_Height;

  uint8_t m_MinimumBrightness;

  lv_indev_t *m_ButtonL;
  lv_indev_t *m_ButtonO;
  lv_indev_t *m_ButtonR;
  lv_indev_t *m_Touch = nullptr;
  lv_group_t *m_Group;

  lv_display_t *m_Display = nullptr;
  lv_obj_t *m_Screen = nullptr;
  lv_obj_t *m_Root = nullptr;
  lv_obj_t *m_Header = nullptr;
  lv_obj_t *m_Content = nullptr;
  lv_obj_t *m_NavBar = nullptr;

  lv_obj_t *m_Left;
  lv_obj_t *m_OK;
  lv_obj_t *m_Right;
  lv_obj_t *m_ShutterLockIcon;
  lv_obj_t *m_IRConnectedButton = nullptr;
  ControlMode m_ControlMode = ControlMode::MENU;

  enum class DisplayState { ACTIVE, DIM, OFF };

  lv_obj_t *m_IntervalStart = nullptr;
  Intervalometer m_Intervalometer;

  lv_obj_t *m_BulbStart = nullptr;
  Bulb m_Bulb;

  status_t m_Status;
  diagnostics_t m_Diagnostics = {};
  nmea_t m_NMEA;
  lv_timer_t *m_NMEATimer = nullptr;
  bool m_FocusPressed = false;
  bool m_ShutterLock = false;
  uint32_t m_InactivityTimeout;
  uint8_t m_DisplayOffMode = 0;
  DisplayState m_DisplayState = DisplayState::ACTIVE;
  bool m_DisplayOff = false;
  bool m_SwallowInput = false;
  uint8_t m_SwallowPending = 0;
  uint32_t m_SleepTick = 0;
  uint32_t m_WakeTick = 0;
  bool m_LeftPressed = false;
  bool m_LeftLongPressHandled = false;
  uint32_t m_LeftPressTick = 0;

  static constexpr uint32_t LEFT_LONG_PRESS_MS = 800;

  /** ST7789 and ILI934x need 120 ms between Sleep In and Sleep Out. */
  static constexpr uint32_t DISPLAY_SLEEP_DWELL_MS = 120;
  uint32_t m_MainCount = 0;

  static menu_t m_MainMenu;

  static std::unordered_map<const char *, menu_t> m_Menu;

  lv_obj_t *m_PowerOff = nullptr;

  CalibrationUI m_CalibrationUI;

  static void buttonPWRRead(lv_indev_t *drv, lv_indev_data_t *data);
  static void buttonPEKRead(lv_indev_t *drv, lv_indev_data_t *data);
  static void buttonARead(lv_indev_t *drv, lv_indev_data_t *data);
  static void buttonBRead(lv_indev_t *drv, lv_indev_data_t *data);
  static void buttonCRead(lv_indev_t *drv, lv_indev_data_t *data);
  static void touchRead(lv_indev_t *drv, lv_indev_data_t *data);

  /** Get the navigation key assigned to an input device. */
  uint32_t inputKey(lv_indev_t *drv) const;

  /** Get the swallow tracking bit assigned to an input device. */
  uint8_t inputBit(lv_indev_t *drv) const;

  /** Wake the display and swallow the input which caused the wake. */
  bool handleDisplayInput(lv_indev_t *drv,
                          lv_indev_data_t *data,
                          bool pressed,
                          bool releaseExpected);

  /** Handle the raw left-button escape gesture before LVGL sees the input. */
  bool handleLeftLongPress(lv_indev_t *drv, bool pressed);

  /** Navigate back one menu page, including pages which hide the back arrow. */
  void navigateBack(void);

  /** Check whether blind remote mode is active. */
  bool isBlindRemoteActive(void) const;

  /** Check whether an input is a blind remote shutter or focus button. */
  bool isBlindRemoteInput(lv_indev_t *drv) const;

  /** Put the panel to sleep and release the display APB lock. */
  void sleepDisplay(void);

  /** Reacquire the display APB lock and wake the panel. */
  void wakeDisplay(void);

  /** Map a stored inactivity value to its roller index. */
  static size_t inactivityIndex(uint8_t value);

  /** Flush display. */
  static void displayFlush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map);

  /** LVGL tick function. */
  static uint32_t tick(void);

  void initInputDevices(void);

  static void setTheme(std::string name);

  void prepareShutterControl(void);

  /** Add icon to the root window header. */
  lv_obj_t *addIcon(const lv_image_dsc_t *symbol);

  /** Set the icon symbol in the root window header. */
  void setIcon(lv_obj_t *icon, const lv_image_dsc_t *symbol);

  /** Add a menu item. */
  static lv_obj_t *addMenuItem(const menu_t &menu,
                               const lv_image_dsc_t *icon,
                               const char *text,
                               bool checkbox = false,
                               const int32_t col_pos = 0,
                               const int32_t row_pos = 0);

  /** Add a menu switch item. */
  void addSettingItem(lv_obj_t *page, const char *symbol, Settings::type_t setting);

  /** Add camera menu item. */
  static lv_obj_t *addCameraItem(Camera *camera, const menu_t &menu, const CameraListMode_t mode);

  /** Create a menu entry. */
  menu_t &addMenu(const char *entry,
                  const lv_image_dsc_t *symbol,
                  bool button = true,
                  const menu_t &parent = m_MainMenu);

  /** Add the main menu to the root window content. */
  void addMainMenu(void);

  /** Add the 'Connect' menu entry. */
  void addConnectMenu(void);

  /** Add the 'Scan' menu entry. */
  void addScanMenu(void);

  /** Clear the 'Scan' page and start a discovery scan. */
  static void startScan(void);

  /** Add the 'Delete' menu entry. */
  void addDeleteMenu(void);

  /** Add the standalone infrared trigger page. */
  void addIRMenu(void);

  /** Add the 'GPS' menu entry. */
  void addGPSMenu(const menu_t &parent);

  /** Add the 'Power saving' GPS page. */
  void addGPSPowerMenu(const menu_t &parent);

  /** Add 'GPS Data' page. */
  void addGPSDataMenu(const menu_t &parent);

  /** Add a GPS option page holding a single roller. */
  void addGPSOptionMenu(const menu_t &parent,
                        const char *name,
                        const char *options,
                        uint32_t selected,
                        lv_event_cb_t handler);

  /** Add the raw NMEA and satellite debug page. */
  void addGPSNMEAMenu(const menu_t &parent);

  /** Show or hide the widgets which need GPS enabled. */
  static void showGPSWidgets(status_t *status, bool show);

  /** Add the 'Features' menu entry. */
  void addFeaturesMenu(const menu_t &parent);

  /** Add the 'Intervalometer' menu entry. */
  void addIntervalometerMenu(const menu_t &parent);

  /** Add the 'Bulb' menu entry. */
  void addBulbMenu(const menu_t &parent);

  /** Refresh the bulb exposure countdown. */
  void bulbRefresh(void);

  /** Stop any bulb exposure and release the shutter. */
  void bulbStop(void);

  /** Add spinner menu item entry. */
  lv_obj_t *addSpinItem(lv_obj_t *page, const char *item, Intervalometer::Spinner &spinner);

  /** Add the spinner page menu entry. */
  void addSpinnerPage(const menu_t &parent, const char *item, Intervalometer::Spinner &spinner);

  void addDisplayMenu(const menu_t &parent);

  /** Add the 'Power' menu entry. */
  void addPowerMenu(const menu_t &parent);

  /** Add the 'Feedback' menu entry. */
  void addFeedbackMenu(const menu_t &parent);

  /** Update visibility of the sound volume page. */
  void updateFeedbackVolumeVisibility(Feedback::output_t output);

  /** Add the 'Battery' page. */
  void addBatteryMenu(const menu_t &parent);

  /** Show the header battery icon and/or percent according to the setting. */
  void setBatteryStyle(uint8_t style);

  /** Show or hide the window title. */
  void setShowTitle(bool show);

  /** Battery sample timer handler. */
  static void batteryUpdate(lv_timer_t *timer);

  void addThemeMenu(const menu_t &parent);

  void addTransmitPowerMenu(const menu_t &parent);

  /** Add the 'Bluetooth' menu entry. */
  void addBluetoothMenu(const menu_t &parent);

  /** Add the infrared settings page. */
  void addIRSettingsMenu(const menu_t &parent);

  /** Update the visibility of infrared menu entries after a setting change. */
  void updateIRMenuVisibility(void);

  void addAboutMenu(const menu_t &parent);

  /** Add the 'Diagnostics' menu entry. */
  void addDiagnosticsMenu(const menu_t &parent);

  /** Add the 'Device info' page. */
  void addDeviceInfoMenu(const menu_t &parent);

  /** Add the 'Power state' page. */
  void addPowerStateMenu(const menu_t &parent);

  /** Add the live BLE diagnostics page. */
  void addBLEMenu(const menu_t &parent);

  /** Add a read only text row to a page container. */
  static lv_obj_t *addInfoRow(lv_obj_t *cont);

  /** Diagnostics refresh timer handler. */
  static void diagnosticsUpdate(lv_timer_t *timer);

  /** Describe the last reset reason. */
  static const char *getResetReason(void);

  /** Add the 'Settings' menu entry. */
  void addSettingsMenu(void);

  /** Add 'Connected' menu. */
  menu_t &addConnectedMenu(void);

  /** Update entries in connect page. */
  static void updateItems(const menu_t &menu);

  /** Start GPS Data timer. */
  static void gpsDataStart(lv_event_t *e);

  /** Stop GPS Data timer. */
  static void gpsDataStop(lv_event_t *e);

  /** Stop the raw NMEA timer and capture. */
  static void gpsNMEAStop(lv_event_t *e);

  /** Handle connection request. */
  static void doConnect(lv_event_t *e);

  /** Handle disconnection. */
  static void doDisconnect(void);

  /** Refresh deletion items. */
  static void refreshDelete(void);

  /** Connection timer handler. */
  static void connectTimerHandler(lv_timer_t *timer);

  /** Intervalometer timer handler. */
  static void intervalometer(lv_timer_t *timer);

  /** Poll for a pending companion numeric-comparison request. */
  static void companionPairingTimer(lv_timer_t *timer);

  /** Start the companion pairing prompt timer. */
  void startCompanionPairingTimer(void);

  /** Stop the companion pairing prompt timer. */
  void stopCompanionPairingTimer(void);

  /** Close the pairing prompt and restore the focus captured before it opened. */
  void closeCompanionPairingDialog(void);

  /** Handle shutter event. */
  static void handleShutter(lv_event_t *e);

  /** Handle focus event. */
  static void handleFocus(lv_event_t *e);

  /** Handle shutter lock event. */
  static void handleShutterLock(lv_event_t *e);

  /** Configure shutter control. */
  void configShutterControl(void);

  /** Configure menu control. */
  void configMenuControl(void);

  /** Configure slider control. */
  void configSliderControl(void);

  /** Check lock screen activity. */
  void handleLockScreen(void);
};
}  // namespace Furble

#endif
