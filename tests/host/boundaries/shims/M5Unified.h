#ifndef FURBLE_BOUNDARY_M5UNIFIED_H
#define FURBLE_BOUNDARY_M5UNIFIED_H

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>

// M5Unified.hpp provides esp_timer_get_time to the production platform. Only
// the target that compiles src/FurblePlatform.cpp needs it here, so the include
// is opt in: pulling esp_timer.h into every boundary target would need a shim
// on their include path for no benefit.
#if defined(FURBLE_BOUNDARY_M5UNIFIED_TIMER)
#include <esp_timer.h>
#endif

namespace lgfx {

enum bus_type_t {
  bus_unknown = 0,
  bus_spi,
  bus_i2c,
  bus_sd,
};

class IBus {
 public:
  virtual ~IBus() = default;
  virtual bus_type_t busType() const { return bus_unknown; }
};

class Bus_SPI: public IBus {
 public:
  struct config_t {
    int spi_host = 1;
  };

  bus_type_t busType() const override { return bus_spi; }
  config_t config() const { return m_Config; }
  void setHost(int host) { m_Config.spi_host = host; }

 private:
  config_t m_Config;
};

class Panel_Device {
 public:
  IBus *getBus() { return m_Bus; }
  void setBus(IBus *bus) { m_Bus = bus; }

 private:
  IBus *m_Bus = nullptr;
};

// The production Platform's display seam forwards to M5.Display, so the stub
// display has to be a device of the library's type. Only the entry points the
// platform and the boot screen reach are modelled; every drawing call is a
// no-op that marks the panel used, exactly as the SDL-free host needs.
class LGFX_Device {
 public:
  virtual ~LGFX_Device() = default;
  virtual int32_t width(void) const { return 320; }
  virtual int32_t height(void) const { return 240; }
  virtual Panel_Device *getPanel(void) { return nullptr; }
  void setBrightness(uint8_t brightness) { m_Brightness = brightness; }
  uint8_t getBrightness(void) const { return m_Brightness; }
  virtual void sleep(void) {}
  virtual void wakeup(void) {}
  uint8_t getRotation(void) const { return m_Rotation; }
  void setRotation(uint8_t rotation) { m_Rotation = rotation; }
  template <typename T>
  void pushImageDMA(int32_t, int32_t, int32_t, int32_t, const T *) {}
  virtual void waitDMA(void) {}
  virtual void readRectRGB(int32_t, int32_t, int32_t, int32_t, uint8_t *) {}

 private:
  uint8_t m_Brightness = 0;
  uint8_t m_Rotation = 0;
};

}  // namespace lgfx

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

enum pin_name_t {
  sd_spi_cs = 0,
  sd_spi_mosi,
  sd_spi_miso,
  sd_spi_sclk,
  // Port A, the external connector the GPS unit uses. A test sets the members
  // below to the value the real M5Unified pin table returns for its board;
  // they default to -1, which is the missing-pin value of that table.
  port_a_pin1,
  port_a_pin2,
};

class Speaker_Class {
 public:
  bool begin() {
    begins++;
    if (beginFailures > 0) {
      beginFailures--;
      return false;
    }
    active = true;
    return true;
  }
  void end() { active = false; }
  void setVolume(uint8_t value) { volume = value; }
  void tone(float frequency, uint32_t duration) {
    lastFrequency = frequency;
    lastDuration = duration;
    playing = true;
  }
  void stop() { playing = false; }
  bool isPlaying() const { return playing; }

  size_t begins = 0;
  size_t beginFailures = 0;
  bool active = false;
  bool playing = false;
  uint8_t volume = 0;
  float lastFrequency = 0.0F;
  uint32_t lastDuration = 0;
};

class Power_Class {
 public:
  enum is_charging_t {
    is_discharging = 0,
    is_charging = 1,
  };

  enum pmic_t {
    pmic_unknown = 0,
    pmic_axp192,
    pmic_axp2101,
    pmic_m5pm1,
    pmic_ip5306,
  };

  pmic_t getType(void) const { return type; }
  void setVibration(uint8_t level) { vibration = level; }
  void setExtOutput(bool value, int = 0) { extOutput = value; }
  int32_t getBatteryLevel() const { return batteryLevel; }
  int16_t getBatteryVoltage() const { return batteryVoltage; }
  int32_t getBatteryCurrent() const { return batteryCurrent; }
  int16_t getVBUSVoltage() const { return vbusVoltage; }
  bool getExtOutput(void) const { return extOutput; }
  is_charging_t isCharging() const { return charging ? is_charging : is_discharging; }
  void powerOff() { powerOffCalled = true; }

  uint8_t vibration = 0;
  int32_t batteryLevel = 100;
  int16_t batteryVoltage = 4000;
  int32_t batteryCurrent = 0;
  int16_t vbusVoltage = 0;
  bool charging = false;
  bool powerOffCalled = false;
  bool extOutput = true;
  pmic_t type = pmic_ip5306;
};

class Display_Class: public lgfx::LGFX_Device {
 public:
  int32_t width(void) const override {
    used = true;
    return 320;
  }
  int32_t height(void) const override {
    used = true;
    return 240;
  }
  lgfx::Panel_Device *getPanel(void) override {
    used = true;
    return &panel;
  }
  // The headless service loop must never reach a panel drawing entry point.
  // Marking every access makes the no_panel check a real probe rather than a
  // constant that no code can ever set.
  void sleep(void) override { sleeping = true; }
  bool sleeping = false;
  void setTouchCalibrate(uint16_t *) { used = true; }
  uint16_t color565(uint8_t, uint8_t, uint8_t) const { return 0; }
  uint8_t getRotation() const { return 0; }
  void setRotation(uint8_t) {}
  void wakeup(void) override {}
  void waitDMA(void) override {}
  void readRectRGB(int32_t, int32_t, int32_t, int32_t, uint8_t *) override {}
  template <typename T>
  void pushImageDMA(int32_t, int32_t, int32_t, int32_t, const T *) {}
  void fillScreen(uint16_t) const { used = true; }
  void drawRect(int32_t, int32_t, int32_t, int32_t, uint16_t) const { used = true; }
  void fillRect(int32_t, int32_t, int32_t, int32_t, uint16_t) const { used = true; }
  void drawString(const char *, int32_t, int32_t) const { used = true; }
  void setTextColor(uint16_t) const { used = true; }
  void setTextColor(uint16_t, uint16_t) const { used = true; }
  void setTextDatum(int) const { used = true; }
  void setTextSize(float) const { used = true; }
  void setTextSize(uint8_t) const { used = true; }
  bool panelUsed() const { return used; }

  lgfx::Panel_Device panel;
  mutable bool used = false;
};

// The rail test compiles the whole production Platform, so the M5Unified
// surface it touches outside the rail path is doubled here as well: the
// configuration struct, begin(), the I2C helper the platform uses for the
// BM8563 and the AXP192 ADC trim, the RTC alarm calls, the PMIC button and the
// display sleep entry point.
struct config_t {
  bool clear_display = true;
  bool output_power = true;
  bool output_charge = true;
  bool pmic_button = true;
  bool internal_imu = true;
  bool internal_rtc = true;
  bool internal_spk = true;
  bool internal_mic = true;
  bool disable_rtc_irq = true;
  int8_t pin_btn = -1;
};

class I2C_Class {
 public:
  bool writeRegister8(uint8_t addr, uint8_t reg, uint8_t data, uint32_t freq) {
    if (freq == 0) {
      return false;
    }
    registers[addr][reg] = data;
    return true;
  }

  uint8_t readRegister8(uint8_t addr, uint8_t reg, uint32_t) {
    auto device = registers.find(addr);
    if (device == registers.end()) {
      return 0xFF;
    }
    auto value = device->second.find(reg);
    return value == device->second.end() ? 0xFF : value->second;
  }

  std::map<uint8_t, std::map<uint8_t, uint8_t>> registers;
};

class Rtc_Class {
 public:
  void disableIRQ(void) {}
  void clearIRQ(void) { irqStatus = false; }
  bool isEnabled(void) const { return timerEnabled; }
  bool getIRQstatus(void) const { return irqStatus; }
  int setAlarmIRQ(int seconds) {
    timerEnabled = seconds > 0;
    return timerEnabled ? seconds : -1;
  }

  bool timerEnabled = false;
  bool irqStatus = false;
};

class Button_Class {
 public:
  void setRawState(uint32_t, bool value) {
    pressed = value;
    if (value) {
      clicks++;
    }
  }
  bool wasClicked(void) {
    const bool result = clicks > 0;
    clicks = 0;
    return result;
  }

  bool pressed = false;
  uint32_t clicks = 0;
};

class M5Unified {
 public:
  board_t getBoard() const { return board; }
  int32_t getPin(pin_name_t name) const {
    if (name == sd_spi_cs) {
      return sdCsPin;
    }
    if (name == port_a_pin1) {
      return portAPin1;
    }
    if (name == port_a_pin2) {
      return portAPin2;
    }
    return -1;
  }
  void update() {}

  config_t config(void) const { return config_t(); }
  void begin(const config_t &cfg) {
    begun = true;
    outputPower = cfg.output_power;
  }

  board_t board = board_M5StickS3;
  bool begun = false;
  bool outputPower = true;
  I2C_Class In_I2C;
  Rtc_Class Rtc;
  Button_Class BtnPWR;
  int32_t sdCsPin = 4;
  int32_t portAPin1 = -1;
  int32_t portAPin2 = -1;
  Display_Class Display;
  Speaker_Class Speaker;
  Power_Class Power;
};

}  // namespace m5

extern m5::M5Unified M5;

#endif
