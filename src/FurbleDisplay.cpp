#include "FurbleDisplay.h"

#include <cstring>

#include "FurbleBoardCaps.h"
#include "FurbleLilyGo.h"

/*
 * The LilyGO display path.
 *
 * M5GFX 0.2.27 has the panel class but not the module's geometry: Panel_SH110x
 * is a Panel_1bitOLED whose constructor defaults to 128x128 and whose init list
 * programs multiplex 0x7F. This driver derives a 128x64 class from it, sets
 * panel and memory height to 64 and sends multiplex 0x3F. The SH1106's 2-column
 * RAM offset is a config field, not a window call: Panel_SH110x::display() adds
 * _cfg.offset_x to the column address and there is no setWindow() in 0.2.27.
 * That offset is left at 0 and measured on the bench. Its setBrightness()
 * writes the contrast command, so brightness is a panel setting and there is
 * no backlight PWM to hold the APB clock for.
 *
 * - `lgfx::Panel_ST7789` over an ordinary SPI bus, for the T-Deck's 2.8 inch
 *   320x240 IPS panel. The same SPI bus carries the MicroSD card and the
 *   optional SX1262, so the bus is shared and every other chip select stays
 *   high while the panel is addressed.
 * - `lgfx::Panel_CO5300` over a quad-SPI bus, for the T-Watch Ultra's 2.06 inch
 *   502x410 AMOLED. M5GFX 0.2.27 hardcodes that geometry
 *   (`Panel_CO5300.hpp` sets memory and panel size to 502x410), so the port only
 *   supplies the wiring and the panel's column offset.
 *
 * Nothing else about the two boards belongs here: brightness, rails and the
 * peripheral gate are in FurbleLilyGo, and the profile row is in FurbleBoard.
 */

#if FURBLE_LILYGO_BOARD || defined(FURBLE_LILYGO_TBEAM_SUPREME)

#if defined(FURBLE_SIM)

#include <lgfx/v1/platforms/sdl/Panel_sdl.hpp>

#else  // FURBLE_SIM

#include <driver/gpio.h>
#include <lgfx/v1/Light.hpp>
#if defined(FURBLE_LILYGO_TBEAM_SUPREME)
#include <lgfx/v1/panel/Panel_SSD1306.hpp>
#include <lgfx/v1/platforms/esp32/Bus_I2C.hpp>
#else
#include <lgfx/v1/panel/Panel_CO5300.hpp>
#include <lgfx/v1/panel/Panel_ST7789.hpp>
#include <lgfx/v1/platforms/esp32/Bus_SPI.hpp>
#endif

#endif  // FURBLE_SIM

namespace Furble {
namespace Display {
namespace {

constexpr int32_t PANEL_WIDTH = TBEAM_PANEL_WIDTH;
constexpr int32_t PANEL_HEIGHT = TBEAM_PANEL_HEIGHT;
constexpr uint32_t I2C_FREQ = 400000;
// The init list leaves the panel at contrast 0x00, and the UI only applies the
// stored brightness once it is constructed. The splash draws before that, so
// begin() leaves the panel at a visible mid contrast and the UI overrides it.
constexpr uint8_t BOOT_CONTRAST = 128;
// LilyGO T-Beam Supreme schematic V3.1, OLED Display block: R51 and R52 strap
// the SH1106 SA0 pin, and the sheet note gives 0x3C with R52 fitted and 0x3D
// with R51 fitted. Probe in the documented order.
constexpr uint8_t ADDRESS_PRIMARY = 0x3C;
constexpr uint8_t ADDRESS_SECONDARY = 0x3D;
// The SH1106 has 132 columns of RAM and a 128-pixel panel window. The module's
// wiring decides whether the window starts at column 0 or 2; M5GFX applies the
// value as _cfg.offset_x and it is a bench check (plan 390 open items).
constexpr uint8_t PANEL_OFFSET_X = 0;
constexpr int8_t PIN_SDA = 17;
constexpr int8_t PIN_SCL = 18;
constexpr int8_t I2C_PORT = 0;

#if defined(FURBLE_SIM)

// Only the two colours the panel can show. The input is LVGL's byte-swapped
// RGB565 (lv_draw_sw_rgb565_swap() runs before the flush callback), so undo the
// swap before taking the luminance.
constexpr uint16_t MONO_WHITE = 0xFFFF;
constexpr uint16_t MONO_BLACK = 0x0000;

// The boot screen draws with the panel's own primitives rather than through
// push(), so the model has to threshold those colours too: on the real panel
// Panel_1bitOLED dithers them to two levels, and a screenshot that kept the
// RGB565 values would not be what the module shows.
uint16_t toMonoRgb(uint16_t rgb) {
  const uint32_t r = (rgb >> 11) & 0x1F;
  const uint32_t g = (rgb >> 5) & 0x3F;
  const uint32_t b = rgb & 0x1F;
  const uint32_t luminance = (r * 299 * 255 / 31 + g * 587 * 255 / 63 + b * 114 * 255 / 31) / 1000;
  return (luminance >= 128) ? MONO_WHITE : MONO_BLACK;
}

uint16_t toMono(uint16_t swapped) {
  const uint16_t rgb = static_cast<uint16_t>((swapped >> 8) | (swapped << 8));
  const uint32_t r = (rgb >> 11) & 0x1F;
  const uint32_t g = (rgb >> 5) & 0x3F;
  const uint32_t b = rgb & 0x1F;
  // Integer ITU-R BT.601 weights on the 5/6/5 fields, scaled to 0..255.
  const uint32_t luminance = (r * 299 * 255 / 31 + g * 587 * 255 / 63 + b * 114 * 255 / 31) / 1000;
  return (luminance >= 128) ? MONO_WHITE : MONO_BLACK;
}

/**
 * The simulated panel.
 *
 * The SDL panel draws a window at the board's real geometry and takes the pixel
 * format the UI pushes. It models the panel, not its controller: panel init
 * commands and bus timing are not exercised in the simulator, which is why the
 * controller configuration is pinned by the host test against the facts row
 * instead.
 */
class SimPanel: public lgfx::LGFX_Device {
 public:
  SimPanel(void) {
#if defined(FURBLE_LILYGO_TBEAM_SUPREME)
    // The T-Beam's panel is a fixed 128x64 module with no facts row: its
    // geometry is the constant the driver programs.
    auto cfg = m_Panel.config();
    cfg.panel_width = TBEAM_PANEL_WIDTH;
    cfg.panel_height = TBEAM_PANEL_HEIGHT;
    cfg.memory_width = TBEAM_PANEL_WIDTH;
    cfg.memory_height = TBEAM_PANEL_HEIGHT;
    cfg.offset_x = 0;
    cfg.offset_y = 0;
#else
    const Facts &f = facts();
    auto cfg = m_Panel.config();
    // The SDL panel has no controller to program, so it models the driven
    // size: the native geometry with the same rotation swap M5GFX applies on
    // hardware. A wrong fact then shows up as a wrong window here.
    cfg.panel_width = drivenWidth(f.panel);
    cfg.panel_height = drivenHeight(f.panel);
    cfg.memory_width = drivenWidth(f.panel);
    cfg.memory_height = drivenHeight(f.panel);
    cfg.offset_x = f.panel.offsetX;
    cfg.offset_y = f.panel.offsetY;
#endif
    cfg.offset_rotation = 0;
    cfg.bus_shared = false;
    m_Panel.config(cfg);
    m_Panel.setScaling(SIM_SCALE, SIM_SCALE);
    setPanel(&m_Panel);
  }

  bool start(void) { return lgfx::LGFX_Device::init_without_reset(false); }

  lgfx::Panel_sdl m_Panel;
};

#else  // FURBLE_SIM

/*
 * The 128x64 module, as a class derived from M5GFX's SH110x driver. The base
 * constructor sets 128 rows and its init list programs multiplex 0x7F, so a
 * 64-common module would decode pages 8 to 15 as pages 0 to 7 and draw the
 * bottom half of a 128-row buffer onto the top of the panel. Both the geometry
 * and the init list are therefore set here, and the multiplex value comes from
 * the same constant the simulator builds its window with.
 */
class Sh1106Panel64: public lgfx::Panel_SH110x {
 public:
  Sh1106Panel64(void) {
    _cfg.panel_width = TBEAM_PANEL_WIDTH;
    _cfg.panel_height = TBEAM_PANEL_HEIGHT;
    _cfg.memory_width = TBEAM_PANEL_WIDTH;
    _cfg.memory_height = TBEAM_PANEL_HEIGHT;
    _cfg.offset_x = PANEL_OFFSET_X;
    _cfg.offset_y = 0;
  }

 protected:
  const uint8_t *getInitCommands(uint8_t listno) const override {
    // Panel_SH110x's list with the multiplex ratio of a 64-row panel. The COM
    // pin configuration it already sends (0x12) is the 64-row alternative
    // layout, so only the ratio differs from the 128-row default.
    static constexpr uint8_t list0[] = {
        CMD_DISP_OFF,
        CMD_SETSTARTLINE,
        CMD_READMODIFYWRITE_END,
        CMD_PAGEADDRESSINGMODE,
        CMD_SETDISPSTARTLINE,
        0x00,
        CMD_SETDISPLAYCLOCKDIV,
        0x50,
        CMD_SETMULTIPLEX,
        Detail::sh1106MuxRatio(TBEAM_PANEL_HEIGHT),
        CMD_SETDISPLAYOFFSET,
        0x00,
        CMD_DCDC,
        0x8B,
        CMD_SEGREMAP,
        CMD_COMSCANINC,
        CMD_SETPRECHARGE,
        0x20,
        CMD_SETVCOMDETECT,
        0x35,
        CMD_DISPLAYALLON_RESUME,
        CMD_SETCONTRAST,
        0x00,
        CMD_SETCOMPINS,
        0x12,
        CMD_DISP_ON,
        0xFF,
        0xFF,  // end
    };
    switch (listno) {
      case 0:
        return list0;
      default:
        return nullptr;
    }
  }
};

class I2cPanel: public lgfx::LGFX_Device {
 public:
  explicit LightDigital(int8_t pin) : m_Pin(pin) {}

  bool init(uint8_t brightness) override {
    if (m_Pin < 0) {
      return false;
    }
    const auto gpio = static_cast<gpio_num_t>(m_Pin);
    (void)gpio_set_direction(gpio, GPIO_MODE_OUTPUT);
    setBrightness(brightness);
    return true;
  }

  void setBrightness(uint8_t brightness) override {
    if (m_Pin < 0) {
      return;
    }
    (void)gpio_set_level(static_cast<gpio_num_t>(m_Pin), brightness == 0 ? 0 : 1);
  }

 private:
  int8_t m_Pin;
};

/** The T-Deck's ST7789 over the shared SPI bus. */
class SpiPanel: public lgfx::LGFX_Device {
 public:
  SpiPanel(void) : m_Light(-1) {
    const Facts &f = facts();

    auto bus_cfg = m_Bus.config();
    bus_cfg.spi_host = static_cast<spi_host_device_t>(f.spi.host);
    bus_cfg.freq_write = f.spi.frequency;
    bus_cfg.freq_read = 16000000;
    bus_cfg.pin_sclk = f.spi.sck;
    bus_cfg.pin_miso = f.spi.miso;
    bus_cfg.pin_mosi = f.spi.mosi;
    bus_cfg.spi_mode = 0;
    bus_cfg.spi_3wire = true;
    bus_cfg.use_lock = true;
    // The MicroSD card shares this bus, so a file read and a flush must not
    // interleave: bus_shared below makes M5GFX take the bus lock around a
    // transaction, which is the same arrangement the M5 boards use.
    // A non-zero channel selects SPI_DMA_CH_AUTO in Bus_SPI::init(); the value
    // itself is not a channel number.
    bus_cfg.dma_channel = 1;
    m_Bus.config(bus_cfg);

    auto panel_cfg = m_Panel.config();
    panel_cfg.pin_cs = f.panel.cs;
    panel_cfg.pin_rst = f.panel.rst;
    panel_cfg.pin_busy = -1;
    // Native 240x320; start() then selects rotation 1, which is the landscape
    // orientation the UI draws in.
    panel_cfg.panel_width = f.panel.width;
    panel_cfg.panel_height = f.panel.height;
    panel_cfg.memory_width = f.panel.width;
    panel_cfg.memory_height = f.panel.height;
    panel_cfg.offset_x = f.panel.offsetX;
    panel_cfg.offset_y = f.panel.offsetY;
    panel_cfg.offset_rotation = 0;
    panel_cfg.invert = f.panel.invert;
    panel_cfg.rgb_order = false;
    panel_cfg.bus_shared = true;
    panel_cfg.dlen_16bit = false;
    m_Panel.config(panel_cfg);

    m_Panel.setBus(&m_Bus);
    if (f.panel.backlight >= 0) {
      m_Light = LightDigital(f.panel.backlight);
      m_Panel.setLight(&m_Light);
    }
    setPanel(&m_Panel);
  }

  bool start(void) {
    const PanelWiring &panel = facts().panel;
    if (!lgfx::LGFX_Device::init_without_reset(false)) {
      return false;
    }
    // Rotation 1 is landscape on the ST7789's fixed column/row order.
    setRotation(panel.rotation);
    return true;
  }

  lgfx::Bus_SPI m_Bus;
  lgfx::Panel_ST7789 m_Panel;
  LightDigital m_Light;
};

/** The T-Watch Ultra's CO5300 over its quad-SPI bus. */
class QspiPanel: public lgfx::LGFX_Device {
 public:
  QspiPanel(void) {
    const Facts &f = facts();

    auto bus_cfg = m_Bus.config();
    bus_cfg.spi_host = static_cast<spi_host_device_t>(f.spi.host);
    bus_cfg.freq_write = f.spi.frequency;
    bus_cfg.freq_read = 16000000;
    // The quad bus takes the four data lines instead of MISO/MOSI; M5GFX
    // switches to quad mode when all four are set (Bus_SPI.cpp).
    bus_cfg.pin_sclk = f.qspi.sck;
    bus_cfg.pin_io0 = f.qspi.io0;
    bus_cfg.pin_io1 = f.qspi.io1;
    bus_cfg.pin_io2 = f.qspi.io2;
    bus_cfg.pin_io3 = f.qspi.io3;
    bus_cfg.spi_mode = 0;
    bus_cfg.spi_3wire = true;
    bus_cfg.use_lock = true;
    bus_cfg.dma_channel = 1;  // non-zero selects SPI_DMA_CH_AUTO
    m_Bus.config(bus_cfg);

    auto panel_cfg = m_Panel.config();
    panel_cfg.pin_cs = f.panel.cs;
    panel_cfg.pin_rst = f.panel.rst;
    panel_cfg.pin_busy = -1;
    panel_cfg.panel_width = f.panel.width;
    panel_cfg.panel_height = f.panel.height;
    panel_cfg.memory_width = f.panel.width;
    panel_cfg.memory_height = f.panel.height;
    // Panel_CO5300's own init programs columns 22 to 431, and
    // Panel_AMOLED::setWindow adds config().offset_x to every window, so the
    // native geometry is 410x502 at offset 22. At rotation 1 setRotation()
    // swaps both, giving CASET 0 to 501 and RASET 22 to 431, which is the same
    // 410 columns the controller was initialised with.
    panel_cfg.offset_x = f.panel.offsetX;
    panel_cfg.offset_y = f.panel.offsetY;
    panel_cfg.offset_rotation = 0;
    panel_cfg.invert = f.panel.invert;
    panel_cfg.rgb_order = false;
    panel_cfg.bus_shared = false;
    panel_cfg.dlen_16bit = false;
    m_Panel.config(panel_cfg);

    m_Panel.setBus(&m_Bus);
    // No GPIO backlight: Panel_AMOLED::setBrightness writes the controller's
    // 0x51 brightness register, so brightness is a panel command here.
    setPanel(&m_Panel);
  }

  bool start(void) {
    const PanelWiring &panel = facts().panel;
    if (panel.rst >= 0) {
      // The panel has a reset line and init_without_reset() does not touch it, so
      // the port pulses it here: low for the controller's 10 ms reset width, then
      // the 120 ms settle the driver allows before sleep-out and the first
      // command. Without this the panel comes up in whatever state the previous
      // rail cycle left it in.
      const auto rst = static_cast<gpio_num_t>(panel.rst);
      (void)gpio_set_direction(rst, GPIO_MODE_OUTPUT);
      (void)gpio_set_level(rst, 0);
      lgfx::delay(10);
      (void)gpio_set_level(rst, 1);
      lgfx::delay(120);
    }
    if (!lgfx::LGFX_Device::init_without_reset(false)) {
      return false;
    }
    setRotation(panel.rotation);
    return true;
  }

  lgfx::Bus_SPI m_Bus;
  lgfx::Panel_CO5300 m_Panel;
};

#if defined(FURBLE_LILYGO_TBEAM_SUPREME)
// LilyGO T-Beam Supreme: a 1.3 inch 128x64 SH1106 on the board's own I2C bus 0
// at SDA 17 and SCL 18, with the 7-bit address strapped by R51 and R52 per
// radio band. The magnetometer shares the bus and also answers 0x3C on some
// variants, which is why both addresses are probed and the decision is the
// header's choosePanelAddress().
constexpr int8_t TBEAM_SDA = 17;
constexpr int8_t TBEAM_SCL = 18;
constexpr int8_t TBEAM_I2C_PORT = 0;
constexpr uint32_t TBEAM_I2C_FREQ = 400000;
constexpr uint8_t TBEAM_ADDRESS_PRIMARY = 0x3C;
constexpr uint8_t TBEAM_ADDRESS_SECONDARY = 0x3D;
// The init list leaves contrast at 0x00, and the UI applies the stored
// brightness only once it exists, so begin() leaves the panel visible.
constexpr uint8_t TBEAM_BOOT_CONTRAST = 128;
// The SH1106 has 132 columns of RAM for a 128 pixel window; the module's wiring
// decides whether that window starts at column 0 or 2 (bench check).
constexpr uint8_t TBEAM_OFFSET_X = 0;

/**
 * The 128x64 module, as a class derived from M5GFX's SH110x driver.
 *
 * The base constructor sets 128 rows and its init list programs multiplex 0x7F,
 * which a 64-common part decodes as pages 0 to 7 repeated. Both the geometry and
 * the init list are set here, and the multiplex value comes from the same
 * constant the simulator window is built with.
 */
class Sh1106Panel64: public lgfx::Panel_SH110x {
 public:
  Sh1106Panel64(void) {
    _cfg.panel_width = TBEAM_PANEL_WIDTH;
    _cfg.panel_height = TBEAM_PANEL_HEIGHT;
    _cfg.memory_width = TBEAM_PANEL_WIDTH;
    _cfg.memory_height = TBEAM_PANEL_HEIGHT;
    _cfg.offset_x = TBEAM_OFFSET_X;
    _cfg.offset_y = 0;
  }

 protected:
  const uint8_t *getInitCommands(uint8_t listno) const override {
    static constexpr uint8_t list0[] = {
        CMD_DISP_OFF,
        CMD_SETSTARTLINE,
        CMD_READMODIFYWRITE_END,
        CMD_PAGEADDRESSINGMODE,
        CMD_SETDISPSTARTLINE,
        0x00,
        CMD_SETDISPLAYCLOCKDIV,
        0x50,
        CMD_SETMULTIPLEX,
        Detail::sh1106MuxRatio(TBEAM_PANEL_HEIGHT),
        CMD_SETDISPLAYOFFSET,
        0x00,
        CMD_DCDC,
        0x8B,
        CMD_SEGREMAP,
        CMD_COMSCANINC,
        CMD_SETPRECHARGE,
        0x20,
        CMD_SETVCOMDETECT,
        0x35,
        CMD_DISPLAYALLON_RESUME,
        CMD_SETCONTRAST,
        0x00,
        CMD_SETCOMPINS,
        0x12,
        CMD_DISP_ON,
        0xFF,
        0xFF,  // end
    };
    switch (listno) {
      case 0:
        return list0;
      default:
        return nullptr;
    }
  }
};

/** The T-Beam's SH1106 over the board's own I2C bus. */
class Sh1106Panel: public lgfx::LGFX_Device {
 public:
  Sh1106Panel(void) {
    auto cfg = m_Bus.config();
    cfg.freq_write = TBEAM_I2C_FREQ;
    cfg.freq_read = TBEAM_I2C_FREQ;
    cfg.pin_scl = TBEAM_SCL;
    cfg.pin_sda = TBEAM_SDA;
    cfg.i2c_port = TBEAM_I2C_PORT;
    cfg.i2c_addr = TBEAM_ADDRESS_PRIMARY;
    // SH110x command and data control bytes, the M5GFX defaults.
    cfg.prefix_cmd = 0x00;
    cfg.prefix_data = 0x40;
    cfg.prefix_len = 1;
    m_Bus.config(cfg);
    m_Panel.setBus(&m_Bus);
    setPanel(&m_Panel);
  }

  /**
   * Probe both strapped addresses, then bring the panel up at the one that
   * answered. An address probe is a start condition plus the address byte, and
   * M5GFX reports the NACK from i2c_wait() through endTransaction(), not
   * through beginTransaction(): the begin call returns once the address is in
   * the FIFO, and the acknowledgement is only known after the stop. Both
   * addresses are always probed so that a panel at 0x3D is still found when the
   * magnetometer answers 0x3C, and a NACK on both leaves begin() false.
   */
  bool start(void) {
    if (!lgfx::i2c::init(TBEAM_I2C_PORT, TBEAM_SDA, TBEAM_SCL).has_value()) {
      return false;
    }
    const bool ack_primary = probe(ADDRESS_PRIMARY);
    const bool ack_secondary = probe(ADDRESS_SECONDARY);
    const uint8_t address = Detail::choosePanelAddress(ack_primary, ack_secondary);
    if (address == 0) {
      return false;
    }
    auto cfg = m_Bus.config();
    cfg.i2c_addr = address;
    m_Bus.config(cfg);
    m_Address = address;
    if (!lgfx::LGFX_Device::init_without_reset(false)) {
      return false;
    }
    // The init list leaves contrast at 0x00, so the splash would be blank until
    // the UI applies the stored brightness. A mid contrast makes it visible and
    // the UI's own setBrightness overrides it.
    setBrightness(BOOT_CONTRAST);
    return true;
  }

  lgfx::Bus_I2C m_Bus;
  Sh1106Panel64 m_Panel;

 private:
  /** True when the device at `address` acknowledged the address byte. */
  bool probe(uint8_t address) {
    const bool started =
        lgfx::i2c::beginTransaction(I2C_PORT, address, I2C_FREQ, false).has_value();
    // The stop is where the NACK surfaces, so its result is the probe. It is
    // also sent when the start failed, to release the bus.
    const bool acked = lgfx::i2c::endTransaction(I2C_PORT).has_value();
    return started && acked;
  }

  uint8_t m_Address = 0;
};
#endif  // FURBLE_LILYGO_TBEAM_SUPREME

#endif  // FURBLE_SIM

#if defined(FURBLE_SIM)
SimPanel g_Panel;
#elif defined(FURBLE_LILYGO_TBEAM_SUPREME)
Sh1106Panel g_Panel;
#elif defined(FURBLE_LILYGO_TWATCH_ULTRA)
QspiPanel g_Panel;
#else
SpiPanel g_Panel;
#endif

bool s_Ready = false;

}  // namespace

lgfx::LGFX_Device &screen(void) {
  return g_Panel;
}

bool begin(void) {
  if (s_Ready) {
    return true;
  }
#if !defined(FURBLE_SIM) && !defined(FURBLE_LILYGO_TBEAM_SUPREME)
  // The board's peripheral gate and PMIC rails come up before the panel: on the
  // T-Watch Ultra the panel supply is switched by the XL9555 output that
  // powerOn() sets, and on the T-Deck the panel sits behind GPIO10. The T-Beam
  // has no such gate: Platform::init() brings its AXP2101 rails up first and
  // the OLED sits on ALDO1, which is never cut at runtime.
  if (!powerOn()) {
    return false;
  }
#endif
#if defined(FURBLE_SIM)
  s_Ready = g_Panel.init_without_reset(false);
#else
  s_Ready = g_Panel.start();
#endif
  return s_Ready;
}

bool ready(void) {
  return s_Ready;
}

int32_t width(void) {
  return g_Ready ? PANEL_WIDTH : 0;
}

int32_t height(void) {
  return g_Ready ? PANEL_HEIGHT : 0;
}

bool mono(void) {
  return true;
}

void push(int32_t x, int32_t y, int32_t w, int32_t h, const uint16_t *pixels) {
  if ((!g_Ready) || (pixels == nullptr) || (w <= 0) || (h <= 0)) {
    return;
  }
#if defined(FURBLE_SIM)
  // The model's own frame, in panel coordinates, so a partial block is
  // converted and pushed as the panel would receive it.
  static uint16_t mono[PANEL_WIDTH * PANEL_HEIGHT];
  for (int32_t row = 0; row < h; ++row) {
    const int32_t py = y + row;
    if ((py < 0) || (py >= PANEL_HEIGHT)) {
      continue;
    }
    for (int32_t column = 0; column < w; ++column) {
      const int32_t px = x + column;
      if ((px < 0) || (px >= PANEL_WIDTH)) {
        continue;
      }
      mono[py * PANEL_WIDTH + px] = toMono(pixels[row * w + column]);
    }
  }
  g_Display.pushImage(0, 0, PANEL_WIDTH, PANEL_HEIGHT, mono);
#else
  g_Display.pushImageDMA<uint16_t>(x, y, w, h, pixels);
#endif
}

void wait(void) {
  g_Display.waitDMA();
}

void setBrightness(uint8_t brightness) {
  g_Display.setBrightness(brightness);
}

uint8_t getRotation(void) {
  return g_Display.getRotation();
}

void setRotation(uint8_t rotation) {
  g_Display.setRotation(rotation);
}

void sleep(void) {
  g_Display.sleep();
}

void wakeup(void) {
  g_Display.wakeup();
}

void calibrateTouch(uint16_t *points, size_t count) {
  // The module has no touch controller. The parameters are accepted so the
  // shared UI path compiles unchanged, and ignored on purpose.
  (void)points;
  (void)count;
}

namespace {

#if defined(FURBLE_SIM)
// The simulator model is a colour window, so the direct draws are thresholded
// here to the panel's two levels. The device path passes the value through:
// Panel_1bitOLED does the conversion itself.
uint16_t panelColour(uint16_t rgb) {
  return toMonoRgb(rgb);
}
#else
uint16_t panelColour(uint16_t rgb) {
  return rgb;
}
#endif

}  // namespace

uint16_t color565(uint8_t red, uint8_t green, uint8_t blue) {
  return g_Display.color565(red, green, blue);
}

void fillScreen(uint16_t color) {
  g_Display.fillScreen(panelColour(color));
}

void fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color) {
  g_Display.fillRect(x, y, w, h, panelColour(color));
}

void drawRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color) {
  g_Display.drawRect(x, y, w, h, panelColour(color));
}

void drawString(const char *text, int32_t x, int32_t y) {
  g_Display.drawString(text, x, y);
}

void setTextColor(uint16_t color) {
  g_Display.setTextColor(panelColour(color));
}

void setTextColor(uint16_t foreground, uint16_t background) {
  g_Display.setTextColor(panelColour(foreground), panelColour(background));
}

void setTextDatum(uint8_t datum) {
  g_Display.setTextDatum(datum);
}

void setTextSize(uint8_t size) {
  g_Display.setTextSize(size);
}

void *panelBus(void) {
  auto *panel = g_Display.getPanel();
  return (panel != nullptr) ? panel->getBus() : nullptr;
}

void readRectRGB(int32_t x, int32_t y, int32_t w, int32_t h, uint8_t *rgb) {
  g_Display.readRectRGB(x, y, w, h, rgb);
}

}  // namespace Display
}  // namespace Furble

#endif  // FURBLE_LILYGO_BOARD
