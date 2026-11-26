#ifndef FURBLE_HOST_SETTINGS_M5UNIFIED_H
#define FURBLE_HOST_SETTINGS_M5UNIFIED_H

namespace m5 {
enum class board_t {
  board_M5Stack,
  board_M5StackCore2,
  board_M5StickC,
  board_M5StickCPlus,
  board_M5StickCPlus2,
  board_M5StickS3,
  board_M5Tough,
  // profileFor() names the row the Waveshare ESP32-S3-ETH image reports.
  board_M5StampS3,
};

// Unscoped, like m5::pin_name_t in M5Unified 0.2.20, so m5::port_a_pin1 names
// the enumerator the way production code does.
enum pin_name_t {
  sd_spi_cs,
  // FurbleBoard::gpsPins() asks for Port A; the stub answers every pin with 4.
  port_a_pin1,
  port_a_pin2,
};
}  // namespace m5

namespace lgfx {
constexpr int bus_spi = 1;

struct bus_config_t {
  int spi_host = 0;
};

class Bus {
 public:
  virtual ~Bus() = default;
  virtual int busType() const { return bus_spi; }
};

class Bus_SPI: public Bus {
 public:
  bus_config_t config() const { return {}; }
};

class Panel {
 public:
  Bus *getBus() { return &m_Bus; }

 private:
  Bus_SPI m_Bus;
};
}  // namespace lgfx

class SettingsTestDisplay {
 public:
  int width() const { return 240; }
  int height() const { return 135; }
  lgfx::Panel *getPanel() { return &m_Panel; }

 private:
  lgfx::Panel m_Panel;
};

class SettingsTestM5 {
 public:
  m5::board_t getBoard() const { return m5::board_t::board_M5Stack; }
  int getPin(m5::pin_name_t) const { return 4; }

  SettingsTestDisplay Display;
};

inline SettingsTestM5 M5;

#endif
