#ifndef FURBLE_HOST_SETTINGS_M5UNIFIED_H
#define FURBLE_HOST_SETTINGS_M5UNIFIED_H

namespace m5 {
enum class board_t {
  board_M5Stack,
  board_M5StackCore2,
  board_M5StickS3,
};

enum class pin_name_t {
  sd_spi_cs,
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
