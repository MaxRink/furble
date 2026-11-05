#ifndef FURBLE_BOUNDARY_M5UNIFIED_H
#define FURBLE_BOUNDARY_M5UNIFIED_H

#include <cstddef>
#include <cstdint>
#include <string>

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

class Bus_SPI : public IBus {
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
};

enum ext_output_t {
  ext_PA = 0,
};

enum pin_name_t {
  sd_spi_cs = 0,
  sd_spi_mosi,
  sd_spi_miso,
  sd_spi_sclk,
};

class Speaker_Class {
 public:
  bool begin() {
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

  void setVibration(uint8_t level) { vibration = level; }
  void setExtOutput(bool, int) {}
  int32_t getBatteryLevel() const { return batteryLevel; }
  int16_t getBatteryVoltage() const { return batteryVoltage; }
  int32_t getBatteryCurrent() const { return batteryCurrent; }
  int16_t getVBUSVoltage() const { return vbusVoltage; }
  is_charging_t isCharging() const {
    return charging ? is_charging : is_discharging;
  }
  void powerOff() { powerOffCalled = true; }

  uint8_t vibration = 0;
  int32_t batteryLevel = 100;
  int16_t batteryVoltage = 4000;
  int32_t batteryCurrent = 0;
  int16_t vbusVoltage = 0;
  bool charging = false;
  bool powerOffCalled = false;
};

class Display_Class {
 public:
  int32_t width() const { return 320; }
  int32_t height() const { return 240; }
  lgfx::Panel_Device *getPanel() { return &panel; }
  bool panelUsed() const { return used; }

  lgfx::Panel_Device panel;
  bool used = false;
};

class M5Unified {
 public:
  board_t getBoard() const { return board; }
  int32_t getPin(pin_name_t name) const {
    if (name == sd_spi_cs) {
      return sdCsPin;
    }
    return -1;
  }
  void update() {}

  board_t board = board_M5StickS3;
  int32_t sdCsPin = 4;
  Display_Class Display;
  Speaker_Class Speaker;
  Power_Class Power;
};

}  // namespace m5

extern m5::M5Unified M5;

#endif
