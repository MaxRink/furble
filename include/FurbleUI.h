#ifndef FURBLE_UI_H
#define FURBLE_UI_H

#include <mutex>

#if defined(FURBLE_SIM)
#include "scenario_action.h"
#endif

namespace Furble {
/** Serializes M5.Imu transactions between UI timers and debug console probes. */
extern std::mutex g_IMUMutex;
}  // namespace Furble

#if defined(FURBLE_NO_DISPLAY)

#include <cstdint>

namespace Furble {
class UI {
 public:
  // Device status for the companion service. The display build serves these
  // from the UI task, the headless build reads M5.Power directly and has no
  // intervalometer. Kept out of the FURBLE_CONSOLE gate below because the
  // companion service needs them whether or not the console is built in.
  static int32_t getBatteryLevel(void);
  static int16_t getBatteryVoltage(void);
  static int32_t getBatteryCurrent(void);
  static int16_t getBatteryVBUSVoltage(void);
  static bool isBatteryCharging(void);
  static uint8_t getIntervalometerState(void);
  static uint16_t getIntervalometerRemaining(void);
  // Settings writes may notify the UI from shared console code. Headless
  // builds have no UI task, so the notification is intentionally a no-op.
  static void notifyGestureSettingsChanged(void) {}

#if defined(FURBLE_CONSOLE)
  /** Operations the console asks the headless loop to carry out. */
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

  /** Create the request queue used by the headless main loop. */
  static void init(void);

  /** Queue an operation for the headless main loop. */
  static bool sendRequest(Request request, int32_t arg);

  /** Drain queued console operations in the headless main loop. */
  static void serviceRequests(void);
#endif
};
}  // namespace Furble

#else

#include <array>
#include <atomic>
#if defined(FURBLE_SIM)
#include <condition_variable>
#include <deque>
#include <functional>
#endif
#include <initializer_list>
#if defined(FURBLE_SIM)
#include <memory>
#endif
#include <mutex>
#include <optional>
#include <string>
#if defined(FURBLE_SIM)
#include <thread>
#include <utility>
#endif
#include <unordered_map>
#include <vector>

#include <lvgl.h>

#include "FurbleCalibrate.h"
#include "FurbleControl.h"
#include "FurbleFeedback.h"
#include "FurbleGPS.h"
#include "FurblePlatform.h"
#include "FurblePower.h"
#include "FurbleSettings.h"
#include "FurbleUIGesture.h"
#include "interval.h"

namespace Furble {
class UI {
 public:
  /**
   * UI input control modes.
   *
   * Modifies button/navigation operation.
   */
  enum class ControlMode { MENU, SHUTTER, SLIDER, PRESET, REVERT };

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
    PERF,            /**< arg: -1 prints LVGL stats, otherwise toggles the overlay */
    AUDIT,           /**< arg: unused */
    POWER_RELOAD,    /**< arg: unused */
    SD_RELOAD,       /**< arg: unused */
#if !defined(FURBLE_NO_DISPLAY)
    DISPLAY_MODE, /**< arg: Settings::display_mode_t */
#endif
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

  /** Notify the UI task that a gesture-related setting changed elsewhere. */
  static void notifyGestureSettingsChanged(void);

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

  /** Check the disconnected idle auto-off policy, runs every second. */
  void processAutoOff(void);

  /** Check the low battery policy, runs every second. */
  void processLowBattery(void);

  /** Re-read the cached auto-off and low battery settings. */
  void reloadPowerPolicies(void);

  /** Hold off automatic CPU/light sleep while the battery is charging. */
  void updateChargingPowerPolicy(void);

  /**
   * Display/hide navigation bar.
   */
  void displayNavigationBar(bool show);

  /** Configure input control mode. */
  void configureControl(ControlMode mode, bool set = true);

  /** Apply the exposure preset setting to the bulb duration spinner. */
  void setPresetPicker(bool enabled);

  /** Display shutter intervalometer menu .*/
  void showShutterIntervalometer(bool show);

  /** Lock shutter. */
  void shutterLock(Control &control);

  /** Unlock shutter. */
  void shutterUnlock(Control &control);

#if defined(FURBLE_SIM)
  enum class sim_action_result_t {
    APPLIED,
    VALID_NO_EFFECT,
    UNAVAILABLE,
    INVALID,
  };

  /** Apply one deterministic scenario action without changing firmware builds. */
  sim_action_result_t simScenarioAction(const char *action);
  sim_action_result_t simScenarioAction(const Sim::scenario_action_t &action);

  /** Report an assertable UI state value for scripted end-to-end scenarios. */
  std::string simQueryState(const char *key);

  /**
   * Drive a physical button through the board's real input-device wiring.
   *
   * name is the silk-screen button (a/b/c/pwr); hold selects the left-button
   * long-press escape. Returns false when the board does not expose that
   * button, so a scenario that presses an absent button fails loudly. This
   * lets headless scenarios exercise the same per-board button->navigation
   * path furble runs on hardware, which the touch-only sim was blind to.
   */
  bool simPressButton(const char *name, bool hold);
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
    // Non-blocking indicator shown in the status row while a mid-session drop is
    // being reconnected. Distinct from reconnectIcon, which only reflects that
    // the reconnect setting is enabled.
    lv_obj_t *reconnectingIcon;
    // The lv_menu main-header title label. On the connected page it normally
    // reads "Connected"; a mid-session reconnect rewrites it to "Reconnecting"
    // (or "Reconnecting (i/n)" for a multi-connect session) so the on-screen text
    // matches the reconnecting icon instead of still claiming the link is up.
    lv_obj_t *menuTitle;
    lv_obj_t *reconnectBackoff;
    // battery page rows, NULL where the board cannot measure them
    lv_obj_t *batteryLevel;
    lv_obj_t *batteryVoltage;
    lv_obj_t *batteryCurrent;
    lv_obj_t *batteryCharging;
    lv_obj_t *batteryRuntime;
    lv_obj_t *gpsExtrapolate;
    /** Widgets which are only useful while GPS is enabled. */
    std::vector<lv_obj_t *> gpsWidgets;
    bool screenLocked;
    // last battery sample, its smoothed values and the displayed percent
    Platform::battery_t battery;
    float meanLevel;
    float meanVoltage;
    float meanCurrent;
    uint8_t displayLevel;
    // bumped on every battery refresh so consumers can spot a new sample
    uint32_t sampleCount;
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
    std::array<lv_obj_t *, 3> powerLocks;
    lv_obj_t *imuAccel;
    lv_obj_t *imuGyro;
    float imuAccelValues[3];
    float imuGyroValues[3];
    bool imuAccelValid;
    bool imuGyroValid;
    uint32_t imuAccelUpdates;
    uint32_t imuGyroUpdates;
    /** True while the 'IMU live' page is open, gates I2C polling. */
    bool imuPageActive;
  } diagnostics_t;

  typedef struct {
    // The top-level window is pixel-sized by lv_win_create. It does not follow
    // later display resolution changes automatically, so level rotation must
    // resize it with the panel or the landscape page remains portrait-width.
    lv_obj_t *root;
    lv_obj_t *surface;
    lv_obj_t *bubble;
    // Fixed reference ring at the exact centre of the circle. The moving bubble
    // nests inside it when the device is level, so the target is distinct from
    // the bubble and never moves.
    lv_obj_t *target;
    lv_obj_t *roll;
    lv_obj_t *pitch;
    lv_obj_t *sideTube;
    lv_obj_t *sideBubble;
    lv_obj_t *hint;
    float accel[3];
    float displayRoll;
    float displayPitch;
    int32_t bubbleX;
    int32_t bubbleY;
    int32_t sideBubbleX;
    // Panel size at build time, portrait orientation. The reflow swaps these
    // when the page rotates to landscape.
    int32_t baseWidth;
    int32_t baseHeight;
    // Active LVGL rotation for the page, one of 0, 90 or 270 degrees. The rest
    // of the UI always runs at 0, this is scoped to the level page.
    int32_t rotation;
    // On the compact StickC and StickS3 panels the physical-button indicators
    // float and are aligned to the screen edges rather than living in a flex
    // navbar. When the level page rotates the panel those indicators have to be
    // re-anchored to the rotated edges, so keep their handles here. They stay
    // null on boards whose indicators reflow on their own and need no fix-up.
    lv_obj_t *navLeft;
    lv_obj_t *navOK;
    lv_obj_t *navRight;
    int32_t navRightYOffset;
    bool filterReady;
    bool displayReady;
  } level_t;

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

  /**
   * Receiver detail labels on the GPS Data page.
   *
   * The page timer is a plain callback with the menu as its user data, so these
   * live beside the static m_GPSDataTimer rather than on the instance. The
   * timer writes them through the changed-check setters, the simulator queries
   * only read the rendered text back.
   */
  typedef struct {
    lv_obj_t *fix;
    lv_obj_t *source;
    lv_obj_t *cycle;
  } gps_data_t;

  /** Labels on the raw NMEA page. */
  typedef struct {
    lv_obj_t *fix;
    lv_obj_t *counters;
    lv_obj_t *config;
    lv_obj_t *sentences;
    std::string configText;
  } nmea_t;

  class Intervalometer: public SpinnerOwner {
   public:
    class Spinner {
     public:
      Spinner(SpinnerOwner *owner,
              SpinValue::nvs_t nvs,
              bool infinite = false,
              bool presetSupported = false)
          : m_Owner {owner},
            m_SpinValue {nvs},
            m_Infinite {infinite},
            m_PresetSupported {presetSupported} {};

      static constexpr const char *m_SpinDigitRoller = "0\n1\n2\n3\n4\n5\n6\n7\n8\n9";
      static constexpr const char *m_SpinUnitsRoller = "msec\nsecs\nmins";
      static constexpr std::array<uint32_t, 31> m_ExposurePresetMilliseconds = {
          1000,   1300,   1600,   2000,   2500,   3200,   4000,   5000,   6000,    8000,   10000,
          13000,  15000,  20000,  25000,  30000,  40000,  50000,  60000,  80000,   100000, 125000,
          160000, 200000, 250000, 320000, 400000, 500000, 640000, 800000, 1000000,
      };

      void update(void);
      void updateLabels(void);
      void setPresetPicker(bool enabled);
      void stepPreset(int direction);
      bool supportsPresetPicker(void) const { return m_PresetSupported; }
      bool usesPresetPicker(void) const { return m_PresetPicker; }

      SpinnerOwner *m_Owner;
      SpinValue m_SpinValue;
      lv_obj_t *m_Button;
      lv_obj_t *m_Label;
      lv_obj_t *m_Value;
      const bool m_Infinite;  // Can support infinite?
      lv_obj_t *m_RowInfinite;
      lv_obj_t *m_SwitchInfinite;

      lv_obj_t *m_RowSpinners = nullptr;
      // array of rollers, 0 = hundred, 1 = ten, 2 = one
      std::array<lv_obj_t *, 3> m_Roller = {nullptr, nullptr, nullptr};
      lv_obj_t *m_RollerUnit = nullptr;

      lv_obj_t *m_PresetRow = nullptr;
      lv_obj_t *m_PresetMinus = nullptr;
      lv_obj_t *m_PresetValue = nullptr;
      lv_obj_t *m_PresetPlus = nullptr;

     private:
      static size_t nearestPreset(uint32_t milliseconds);
      static SpinValue::nvs_t presetNVS(size_t index);
      void updatePresetPickerVisibility(void);
      void snapToDigits(void);

      const bool m_PresetSupported;
      bool m_PresetPicker = false;
      size_t m_PresetIndex = 0;
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
    typedef enum {
      STATE_IDLE,
      STATE_RUNNING,
      STATE_DONE,
    } state_t;

    Bulb(const SpinValue::nvs_t &duration);

    void save(void) override;

    state_t m_State = STATE_IDLE;
    Intervalometer::Spinner m_Duration;

    uint32_t m_StartedAt = 0;
    lv_obj_t *m_StateLabel = nullptr;
    lv_obj_t *m_RemainingLabel = nullptr;
    lv_obj_t *m_ActionLabel = nullptr;
    lv_obj_t *m_ModeHintLabel = nullptr;
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
    // Last values pushed to the progress box. The connect timer fires every
    // 50 ms while connecting, so guarding these setters stops an unconditional
    // relabel and redraw of the box on every tick (LVGL invalidation trap).
    std::string connectingName;
    int32_t connectProgress;
    // Set once a link has gone fully active. A later drop is then a mid-session
    // reconnect: the connected view stays up and only the status indicator
    // reflects it, instead of taking over the screen with the progress box.
    bool sessionEstablished;
    // A connect has been requested and the control task has not acted on it
    // yet. doConnect() only queues CMD_CONNECT, so the control state is still
    // idle for up to one control tick afterwards. Without this the connect
    // timer's first tick can take the idle branch and pause itself, and nothing
    // resumes it again: the progress box is then never dismissed on a failed
    // connect, and no later link drop is ever surfaced. Cleared as soon as the
    // timer observes any non-idle state, and abandoned after
    // CONNECT_REQUEST_GRACE_MS so a request the control task never picks up
    // cannot spin the timer forever.
    bool connectRequested;
    uint32_t connectRequestedAt;
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
  static constexpr const char *m_CamerasStr = "Cameras";
  static constexpr const char *m_RemoteBulb = "Bulb";
  static constexpr const char *m_RemoteInterval = "Interval";
  static constexpr const char *m_RemoteDisconnect = "Disconnect";
  static constexpr const char *m_LevelStr = "Level";
  // dodgy hack, add a space so map key is unique
  static constexpr const char *m_RemoteGPSData = "GPS Data ";
  static constexpr const char *m_IntervalometerRunStr = "Intervalometer ";
  static constexpr const char *m_BulbRunStr = "Bulb ";

  // connected->bulb
  static constexpr const char *m_BulbDurationStr = "Duration";
  static constexpr const char *m_BulbModeHintStr = "Camera must be in B (bulb) mode";

  // settings
  static constexpr const char *m_DisplayStr = "Display";
  static constexpr const char *m_DisplayOffOptions = "Dim\nOff\nOff, remote on";
  static constexpr const char *m_DisplayOffTouchOptions = "Dim\nOff";
  static constexpr const char *m_TextSizeStr = "Text size";
  static constexpr const char *m_FeaturesStr = "Features";
  static constexpr const char *m_SensorsStr = "Sensors";
  static constexpr const char *m_GesturesStr = "Gestures";
  static constexpr const char *m_WakeGestureStr = "Wake Gesture";
  static constexpr const char *m_WakeGestureOptions = "Off\nTap\nShake\nBoth";
  static constexpr const char *m_GPSStr = "GPS";
  static constexpr const char *m_IntervalometerStr = "Timer";
  static constexpr const char *m_ThemeStr = "Theme";
  static constexpr const char *m_IRSettingsStr = "Infrared";
  static constexpr const char *m_BluetoothStr = "Bluetooth";
  static constexpr const char *m_AboutStr = "About";
  static constexpr const char *m_PowerStr = "Power";
  static constexpr const char *m_FeedbackStr = "Feedback";
  static constexpr const char *m_DiagnosticsStr = "Diagnostics";
  static constexpr const char *m_StorageStr = "Storage";
  static constexpr const char *m_ExportSettingsStr = "Export Settings";
  static constexpr const char *m_ImportSettingsStr = "Import Settings";
  static constexpr const char *m_CardInfoStr = "Card Info";

  // settings->power
  static constexpr const char *m_BatteryStr = "Battery";

  // settings->feedback
  static constexpr const char *m_FeedbackEventsStr = "Feedback Events";
  static constexpr const char *m_FeedbackVolumeStr = "Volume";

  // settings->diagnostics
  static constexpr const char *m_DeviceInfoStr = "Device info";
  static constexpr const char *m_PowerStateStr = "Power state";
  static constexpr const char *m_BLEStr = "BLE";
  static constexpr const char *m_IMUDataStr = "IMU live";

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

  /** Auto-off roller values, in minutes, zero is no timeout. */
  static constexpr std::array<uint8_t, 5> m_AutoOffMinutes = {0, 5, 10, 30, 60};

  static constexpr uint8_t LOW_BATT_WARN_LEVEL = 10;
  static constexpr uint8_t LOW_BATT_OFF_LEVEL = 5;
  /** Battery samples arrive every 5 s, six in a row is 30 s of hysteresis. */
  static constexpr uint8_t LOW_BATT_QUALIFY_SAMPLES = 6;
  /** A pack above this voltage cannot be empty, treat level 0 as a bad read. */
  static constexpr uint16_t LOW_BATT_VALID_READ_MV = 3300;
  static constexpr uint32_t LOW_BATT_POWER_OFF_DELAY_MS = 30000;
  static constexpr const char *m_LowBattWarnText = "Battery low.\nPlease charge soon.";
  static constexpr const char *m_LowBattCriticalText =
      "Battery critical.\nPowering off in 30 seconds.";

  // settings->gps
  static constexpr const char *m_GPSDataStr = "GPS Data";
  static constexpr const char *m_GPSRateStr = "Update rate";
  static constexpr const char *m_GPSSentencesStr = "Sentences";
  static constexpr const char *m_GPSConstellationStr = "Constellation";
  static constexpr const char *m_GPSPowerStr = "Power saving";
  static constexpr const char *m_GPSAssistStr = "Assisted start";
  static constexpr const char *m_GPSHoldStr = "Fix Hold";
  static constexpr const char *m_GPSNMEAStr = "Raw NMEA";

  // settings->gps rollers
  static constexpr const char *m_GPSRateOptions = "Default\n1000 ms\n500 ms\n200 ms\n100 ms";
  static constexpr const char *m_GPSSentencesOptions = "Default\nRMC+GGA";
  static constexpr const char *m_GPSConstellationOptions =
      "Default\nGPS\nBDS\nGPS+BDS\nGLONASS\nGPS+GLO\nBDS+GLO\nAll";
  static constexpr const char *m_GPSPowerOptions = "Always on\nStandby (PCAS12)\nRail cycling";
  static constexpr const char *m_GPSDutyOptions = "No standby\n5 s\n10 s\n15 s";
  static constexpr const char *m_GPSAssistOptions = "Off\nPosition and time";
  static constexpr const char *m_GPSHoldOptions = "Off\n30 s\n2 min\n10 min\n60 min";

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
  static gps_data_t m_GPSData;
  static lv_timer_t *m_CamerasTimer;
  static lv_timer_t *m_LevelTimer;
  static lv_timer_t *m_IntervalPageRefresh;
  static uint32_t m_IntervalNext;
  static std::atomic<uint8_t> m_IntervalometerState;
  static std::atomic<uint16_t> m_IntervalometerRemaining;
  static bool m_IntervalCountdownActive;
  static uint8_t m_IntervalLastAnnouncedSecond;
  static std::atomic<uint32_t> m_GestureSettingsGeneration;

  static lv_timer_t *m_BulbTimer;
  static lv_timer_t *m_BulbPageRefresh;
  static uint32_t m_BulbEnd;

  lv_timer_t *m_IntervalTimer;
  lv_timer_t *m_InactivityTimer;
  // 50 Hz. Fast enough for a tap edge, and the UI task already runs at 5 ms
  // so this changes the work per tick, not the task's wake rate.
  static constexpr uint32_t GESTURE_POLL_MS = 20;
  lv_timer_t *m_GestureTimer = nullptr;
  lv_timer_t *m_GestureShutterTimer = nullptr;
  lv_timer_t *m_IconTimer;
  lv_timer_t *m_BatteryTimer;
  lv_timer_t *m_DiagnosticsTimer;
  lv_timer_t *m_CompanionPairingTimer = nullptr;
  lv_obj_t *m_CompanionPairingDialog = nullptr;
  lv_obj_t *m_CompanionPairingPrevFocus = nullptr;
  lv_obj_t *m_StorageMessageBox = nullptr;
  bool m_StorageImport = false;
  lv_obj_t *m_StorageMenuMain = nullptr;
  lv_obj_t *m_StoragePage = nullptr;
  lv_obj_t *m_StorageInfoLabel = nullptr;
  lv_obj_t *m_StorageGPXSwitch = nullptr;
  bool m_StorageVisible = false;
  uint32_t m_StorageGeneration = 0;

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
  // Non-blocking reconnect banner overlaid on the Remote shutter page: a red
  // Bluetooth icon plus "Reconnecting" (or "Reconnecting (i/n)") text. The
  // header status row also carries the reconnecting icon, but the full-screen
  // shutter view is where shots are taken, so a mid-session drop is surfaced
  // there too. Hidden until a live link drops. Built in addConnectedMenu.
  lv_obj_t *m_RemoteReconnect = nullptr;
  lv_obj_t *m_RemoteReconnectLabel = nullptr;
  // The full-screen Bulb page is the other place shots are taken and, like the
  // shutter page, hides the header status row, so it carries its own copy of the
  // reconnect banner. Hidden until a live link drops. Built in addBulbMenu.
  lv_obj_t *m_BulbReconnect = nullptr;
  lv_obj_t *m_BulbReconnectLabel = nullptr;
  lv_obj_t *m_IRConnectedButton = nullptr;
  // Second entry point to the shared spirit level page, placed on the main
  // menu so the tool works without a camera connection. Static because the
  // static showIMUWidgets() gate hides it on boards without a usable IMU.
  static lv_obj_t *m_LevelMainButton;
  ControlMode m_ControlMode = ControlMode::MENU;

  enum class DisplayState { ACTIVE, DIM, OFF };

  lv_obj_t *m_IntervalStart = nullptr;
  lv_obj_t *m_IntervalStop = nullptr;
  Intervalometer m_Intervalometer;

  lv_obj_t *m_BulbStart = nullptr;
  Bulb m_Bulb;

  status_t m_Status;
  diagnostics_t m_Diagnostics = {};
  level_t m_Level = {};
  nmea_t m_NMEA;
  lv_timer_t *m_NMEATimer = nullptr;
  bool m_FocusPressed = false;
  bool m_ShutterLock = false;
  bool m_ButtonModeFocusPressed = false;
  bool m_ButtonModeShutterPressed = false;
  bool m_ButtonModeLongPressed = false;
  uint8_t m_ButtonModeClickStreak = 0;
  uint32_t m_ButtonModeLastClick = 0;
#if defined(FURBLE_SIM)
  // The SDL sim cannot reproduce LVGL's real short-click streak timing, so a
  // scenario injects the streak the one-button dispatch should classify. Only
  // the sim build reads these; the firmware still uses the live LVGL streak.
  bool m_SimClickStreakActive = false;
  uint8_t m_SimClickStreak = 0;

  struct sim_request_t {
    std::function<void()> operation;
    std::mutex mutex;
    std::condition_variable complete;
    bool done = false;
    bool result = true;
  };

  std::mutex m_SimRequestMutex;
  std::deque<std::shared_ptr<sim_request_t> > m_SimRequests;
  std::thread::id m_SimUiThread;
  std::atomic<bool> m_SimLastActionOnUi {false};
  sim_action_result_t m_SimActionResult = sim_action_result_t::INVALID;

  bool simRunOnUi(std::function<void()> operation);
  void serviceSimRequests(void);
  void simScenarioActionOnUi(const Sim::scenario_action_t &action);
  bool simulatorHomeOnUi(void);
  bool simulatorBackOnUi(void);
  bool simPressButtonOnUi(const char *name, bool hold);

  /**
   * Count the visible labels and icons on the current page that intersect a
   * floating navigation indicator. Returns zero on a touch build, which has no
   * indicators.
   */
  uint32_t countIndicatorOverlaps(void);
#endif
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
  bool m_DisplayConsole = false;

  // cached policy settings, refreshed by the rollers and the console
  uint8_t m_AutoOffSetting = 0;
  uint8_t m_LowBattSetting = 0;
  bool m_AutoOffChargingSetting = false;
  std::optional<Power::Lock> m_ChargingSleepLock;

  uint32_t m_LowBatterySampleSeen = 0;
  uint8_t m_LowBatteryWarnCount = 0;
  uint8_t m_LowBatteryOffCount = 0;
  bool m_LowBatteryWarned = false;
  bool m_LowBatteryPowerOffPending = false;
  uint32_t m_LowBatteryPowerOffSince = 0;
  lv_obj_t *m_LowBatteryMessageBox = nullptr;
  lv_obj_t *m_LowBatteryMessage = nullptr;
  /** Focused object before the warning stole the focus, restored on close. */
  lv_obj_t *m_LowBatteryPrevFocus = nullptr;
  bool m_PoweringOff = false;
  uint8_t m_WakeGesture = 0;
  bool m_DoubleTapShutter = false;
  uint32_t m_GestureSettingsSeen = 0;
  GestureDetector m_GestureDetector;
#if defined(FURBLE_SIM)
  uint32_t m_GestureEvents = 0;
  uint32_t m_GestureShutterSends = 0;
  const char *m_GestureLast = "none";
#endif
  std::vector<lv_obj_t *> m_IMUGestureWidgets;

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

  static void setTheme(std::string name, uint8_t textSize);

  void prepareShutterControl(void);

  /** Add icon to the root window header. */
  lv_obj_t *addIcon(const lv_image_dsc_t *symbol);

  /** Set the icon symbol in the root window header. */
  void setIcon(lv_obj_t *icon, const lv_image_dsc_t *symbol);

  /** Pixels to keep clear on the right of a full width menu row. */
  static int32_t floatingIndicatorReserve(void);

  /** Add a menu item. */
  static lv_obj_t *addMenuItem(const menu_t &menu,
                               const lv_image_dsc_t *icon,
                               const char *text,
                               bool checkbox = false,
                               const int32_t col_pos = 0,
                               const int32_t row_pos = 0);

  /** Add a menu switch item. */
  lv_obj_t *addSettingItem(lv_obj_t *page, const char *symbol, Settings::type_t setting);

  /** Add camera menu item. */
  static lv_obj_t *addCameraItem(size_t index, const menu_t &menu, const CameraListMode_t mode);

  /** Create a menu entry. */
  menu_t &addMenu(const char *entry,
                  const lv_image_dsc_t *symbol,
                  bool button = true,
                  const menu_t &parent = m_MainMenu);

  /** Add the main menu to the root window content. */
  void addMainMenu(void);

  /**
   * Yield the main task once per page while the menu tree is built at boot.
   *
   * The whole tree is created synchronously on the main task before the task
   * loop starts. On the M5StickS3 that unyielded stretch runs long enough to
   * starve IDLE0 and trip the ESP-IDF task watchdog (~5 s). Yielding per page
   * lets the scheduler run IDLE0 so the watchdog stays fed. No-op in the
   * simulator, which has no task watchdog and a virtual clock.
   */
  void bootYield(void);

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

  /** Add the 'Sensors' menu entry. */
  void addSensorsMenu(const menu_t &parent);

  /** Add the gesture page: wake roller, shutter switch, false-trigger warning. */
  void addGesturesMenu(const menu_t &parent);

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

  /** Start or restart the bulb exposure. */
  void bulbStart(void);

  /** Complete an automatic bulb exposure. */
  void bulbComplete(void);

  /** Stop any bulb exposure and release the shutter. */
  void bulbStop(void);

  /** Update the bulb mode hint. BLE mode detection can replace this hook. */
  void updateBulbModeHint(void);

  /** Add spinner menu item entry. */
  lv_obj_t *addSpinItem(lv_obj_t *page, const char *item, Intervalometer::Spinner &spinner);

  /** Add the spinner page menu entry. */
  void addSpinnerPage(const menu_t &parent, const char *item, Intervalometer::Spinner &spinner);

  void addDisplayMenu(const menu_t &parent);

  void addTextSizeMenu(const menu_t &parent);

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

  /** Add the SD card storage page when the board has a card slot. */
  void addStorageMenu(const menu_t &parent);

  /** Show the confirmation dialog for a storage action. */
  void showStorageConfirm(bool import);

  /** Run the confirmed storage action. */
  void confirmStorageAction(void);

  /** Close a storage action confirmation dialog. */
  void cancelStorageAction(void);

  /** Refresh the card information label from the published SD state. */
  static void updateStorageInfo(lv_obj_t *label);

  /**
   * Track the storage page and the SD writer task state.
   *
   * Requests a mount when the page is entered, releases the mount hold when
   * it is left, and refreshes the storage widgets when the writer task
   * publishes a new state. Runs on the UI task, never blocks.
   */
  void serviceStorage(void);

  /** Add the 'IMU live' page. */
  void addIMUDataMenu(const menu_t &parent);

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

  /** Show or hide pages which need the IMU enabled. */
  static void showIMUWidgets(bool show);

  /** Show or disable gesture settings which need the IMU enabled. */
  void showIMUGestureWidgets(bool show);

  /** Create or remove the gesture timer after a setting change. */
  void updateGestureTimer(void);

  /** Poll the accelerometer and handle one detected gesture. */
  void pollGesture(void);

  /** Handle a gesture reported by the detector. */
  void handleGesture(GestureDetector::gesture_t gesture);

  /** Wake the display and reset the LVGL inactivity counter. */
  void wakeDisplayFromGesture(void);

  /** Return whether the inactivity path currently considers the display idle. */
  bool displayIsInactive(void) const;

  /** Return whether the current page can accept an IMU shutter trigger. */
  bool canTriggerGesture(void) const;

  /** Send a short shutter command pair for a double tap. */
  void fireGestureShutter(void);

  /** Describe the last reset reason. */
  static const char *getResetReason(void);

  /** Add the 'Settings' menu entry. */
  void addSettingsMenu(void);

  /** Add 'Connected' menu. */
  menu_t &addConnectedMenu(void);

  /** Add the connected Cameras status page. */
  void addCamerasMenu(const menu_t &parent);
  /** Add the spirit level page. */
  void addLevelMenu(const menu_t &parent);

  /** Update entries in connect page. */
  static void updateItems(const menu_t &menu);

  /** Update the Multi-Connect button label and state. */
  static void updateMultiConnectButton(lv_obj_t *button);

  /** Save the current active camera selection. */
  static void saveMultiConnectSelection(void);

  /** Rebuild the connected Cameras status rows, LVGL task only. */
  static void rebuildCamerasPage(menu_t &menu);

  /** Refresh connected Cameras status rows. */
  static void camerasUpdate(lv_timer_t *timer);

  /** Update one connected Cameras status row. */
  static void updateCameraRow(lv_obj_t *label, Camera *camera, Control::state_t state);

  /** Start GPS Data timer. */
  static void gpsDataStart(lv_event_t *e);

  /** Stop GPS Data timer. */
  static void gpsDataStop(lv_event_t *e);

  /** Refresh the spirit level page. */
  static void levelUpdate(lv_timer_t *timer);

  /**
   * Map a filtered accelerometer sample to the spirit level widgets.
   *
   * Shared by the live IMU timer and the simulator tilt injection seam so both
   * exercise the same sensitivity curve and bubble placement.
   */
  static void applyLevelSample(level_t *level, const float accel[3]);

  /**
   * Rotate the level page and reflow its widgets for the new orientation.
   *
   * Portrait (0 degrees) shows the circle bubble plus the gesture hint. A side
   * orientation (90 or 270 degrees) swaps the panel to landscape and shows the
   * linear bubble tube instead. The numeric readout stays on both. The display
   * rotation is scoped to the level page and restored to 0 on page exit.
   */
  static void applyLevelRotation(level_t *level, int32_t rotation);

  /** Clamp a level circle diameter to the panel content for the given size. */
  static int32_t levelDiameter(int32_t width, int32_t height);
  /** Gesture poll timer handler. */
  static void gestureUpdate(lv_timer_t *timer);

  /** Gesture shutter release timer handler. */
  static void gestureShutterRelease(lv_timer_t *timer);

  /** Stop the raw NMEA timer and capture. */
  static void gpsNMEAStop(lv_event_t *e);

  /** Handle connection request. */
  static void doConnect(lv_event_t *e);

  /** Handle disconnection. */
  static void doDisconnect(void);

  /** Show the low battery warning. */
  void showLowBatteryWarning(bool powerOff);

  /** Close the low battery warning. */
  void closeLowBatteryWarning(void);

  /** Release the shutter and power off through the platform layer. */
  void doPowerOff(void);

  /** Refresh deletion items. */
  static void refreshDelete(void);

  /** Connection timer handler. */
  static void connectTimerHandler(lv_timer_t *timer);

  /**
   * Rewrite the connected page header title to reflect a mid-session reconnect.
   *
   * On the connected page the header normally reads "Connected". While a live
   * link is being reconnected this shows "Reconnecting", or "Reconnecting (i/n)"
   * for a multi-connect session where i is the number of cameras currently down
   * and n the total in the session. Passing reconnecting == false restores
   * "Connected". A no-op unless the connected page owns the header, so a sub page
   * (Shutter, Intervalometer, ...) keeps its own title. Guarded so the label text
   * is only set when it actually changes.
   */
  void updateReconnectTitle(bool reconnecting);

  /**
   * Update the full-screen page reconnect banners for a mid-session drop.
   *
   * Drives both full-screen operational pages that hide the header status row,
   * the Remote shutter page and the Bulb page, in lockstep: shows a red
   * Bluetooth icon plus "Reconnecting" (or "Reconnecting (i/n)" for a
   * multi-connect session, i cameras down of n) overlaid on each while a live
   * link is being reconnected, and hides them once recovered. Unlike
   * updateReconnectTitle this runs regardless of the current page: each banner is
   * a child of its page, so it only renders while that page is on screen, which
   * is exactly where the header title is not rewritten. Reuses the same
   * connection state and count as updateReconnectTitle. Guarded so the label text
   * and visibility are only touched when they actually change.
   */
  void updateRemoteReconnect(bool reconnecting);

  /**
   * Apply the reconnecting state to one floating page banner (icon plus label).
   *
   * Shared by the Remote shutter and Bulb pages so both surface a mid-session
   * drop identically. See updateRemoteReconnect for the state it reads.
   */
  void updatePageReconnectBanner(lv_obj_t *banner, lv_obj_t *label, bool reconnecting);

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

  /** Handle the configurable main button event. */
  static void handleButtonMode(lv_event_t *e);

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

  /** Configure preset picker controls. */
  void configPresetControl(void);

  /** Step the active exposure preset picker. */
  void presetStep(int direction);

  /** Confirm the active exposure preset picker. */
  void presetConfirm(void);

  /** Check lock screen activity. */
  void handleLockScreen(void);

  /** Apply the console-only display mode. */
  void setDisplayMode(uint8_t mode);
};
}  // namespace Furble

#endif  // FURBLE_NO_DISPLAY

#endif
