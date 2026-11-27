#ifndef FURBLE_PLATFORM_TEST_M5UNIFIED_H
#define FURBLE_PLATFORM_TEST_M5UNIFIED_H

#include <cstdint>

// The M5Unified boundary for the real src/FurblePlatform.cpp. Only what that
// source calls is declared, and every method is a no-op or a fixed value: the
// PMIC behind the motion wake path is sim/shim/M5PM1.h, not this stub. Nothing
// here may decide a test outcome.
namespace m5 {

enum board_t {
  board_unknown = 0,
  board_M5StickC,
  board_M5StickCPlus,
  board_M5StickCPlus2,
  board_M5StickS3,
  board_M5Stack,
  board_M5StackCore2,
  board_M5Tough,
  board_M5StackCoreS3,
  board_M5StampS3,
};

enum ext_output_t {
  ext_PA = 0,
};

// Board::gpsPins() reads Port A through M5Unified's pin table, so the names it
// asks for have to exist here. The aliases are declared as the real header
// declares them, because a row may be written with any of the three spellings.
enum pin_name_t {
  in_i2c_scl,
  in_i2c_sda,
  port_a_pin1,
  port_a_scl = port_a_pin1,
  ex_i2c_scl = port_a_pin1,
  port_a_pin2,
  port_a_sda = port_a_pin2,
  ex_i2c_sda = port_a_pin2,
};

class I2C_Class {
 public:
  uint8_t readRegister8(uint8_t, uint8_t, uint32_t = 0) const { return 0; }
  bool writeRegister8(uint8_t, uint8_t, uint8_t, uint32_t = 0) const { return true; }
};

class RTC_Class {
 public:
  bool isEnabled() const { return false; }
  void disableIRQ() {}
  void clearIRQ() {}
  uint8_t getIRQstatus() const { return 0; }
  int setAlarmIRQ(int seconds) {
    (void)seconds;
    return 0;
  }
};

class Power_Class {
 public:
  enum pmic_t { pmic_unknown = 0, pmic_axp192, pmic_axp2101, pmic_m5pm1 };
  enum is_charging_t { is_discharging = 0, is_charging = 1 };

  pmic_t getType() const { return pmic_m5pm1; }
  int32_t getBatteryLevel() const { return 100; }
  int16_t getBatteryVoltage() const { return 4000; }
  int32_t getBatteryCurrent() const { return 0; }
  int16_t getVBUSVoltage() const { return 0; }
  is_charging_t isCharging() const { return is_discharging; }
  bool getExtOutput() const { return extOutput; }
  void setExtOutput(bool on, int) { extOutput = on; }
  void setVibration(uint8_t) {}
  void powerOff() {}

  bool extOutput = false;
};

class Button_Class {
 public:
  void setRawState(uint32_t, bool) {}
  bool wasClicked() const { return false; }
};

// The feedback driver links into this target for one predicate, so its speaker
// methods have to exist. None of them is called: the wake path never signals a
// feedback event.
class Speaker_Class {
 public:
  bool begin() { return true; }
  void end() {}
  void setVolume(uint8_t) {}
  void tone(float, uint32_t) {}
  void stop() {}
  bool isPlaying() const { return false; }
};

// The wake and power-off tests compile the real Platform, whose display seam
// forwards to M5Unified. Nothing here draws, so every entry point is a no-op;
// the sizes are the Core's panel.
class Display_Class {
 public:
  int32_t width() const { return 320; }
  int32_t height() const { return 240; }
  void sleep() {}
  void wakeup() {}
  void setBrightness(uint8_t) {}
  uint8_t getRotation() const { return 0; }
  void setRotation(uint8_t) {}
  void setTouchCalibrate(uint16_t *) {}
  uint16_t color565(uint8_t, uint8_t, uint8_t) const { return 0; }
  void fillScreen(uint16_t) const {}
  void fillRect(int32_t, int32_t, int32_t, int32_t, uint16_t) const {}
  void drawRect(int32_t, int32_t, int32_t, int32_t, uint16_t) const {}
  void drawString(const char *, int32_t, int32_t) const {}
  void setTextColor(uint16_t) const {}
  void setTextColor(uint16_t, uint16_t) const {}
  void setTextDatum(uint8_t) const {}
  void setTextSize(uint8_t) const {}
  void waitDMA() {}
  template <typename T>
  void pushImageDMA(int32_t, int32_t, int32_t, int32_t, const T *) {}
  void readRectRGB(int32_t, int32_t, int32_t, int32_t, uint8_t *) {}
  // The display seam's panelBus() asks the panel for its bus. Nothing in these
  // tests shares an SD card, so a null bus is the truthful answer.
  struct Panel {
    void *getBus() { return nullptr; }
  };
  Panel *getPanel() { return &m_Panel; }

 private:
  Panel m_Panel;
};

class config_t {
 public:
  bool clear_display = true;
  bool internal_imu = false;
  bool internal_spk = false;
  bool internal_mic = false;
  bool pmic_button = true;
  bool disable_rtc_irq = false;
  // Platform::init() clears this under FURBLE_HAS_M5PM1 so the rail
  // manager, not M5Unified, commits the EXT_5V port.
  bool output_power = true;
};

class M5Unified {
 public:
  config_t config() const { return {}; }
  void begin(const config_t &) {}
  board_t getBoard() const { return board; }
  void update() {}
  // Only Board::gpsPins() calls this, and the GPS path is not the wake path,
  // so the value is the missing-pin one and nothing here decides a test.
  static int8_t getPin(pin_name_t) { return -1; }

  board_t board = board_M5StickS3;
  I2C_Class In_I2C;
  RTC_Class Rtc;
  Power_Class Power;
  Button_Class BtnPWR;
  Display_Class Display;
  Speaker_Class Speaker;
};

}  // namespace m5

extern m5::M5Unified M5;

#endif  // FURBLE_PLATFORM_TEST_M5UNIFIED_H
