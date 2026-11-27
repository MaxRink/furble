#include "FurbleDisplay.h"

#include <cstring>

#include "FurbleBoardCaps.h"
#include "FurbleLilyGo.h"

/*
 * The LilyGO display path.
 *
 * M5Unified knows none of these boards, so the panel is built here from the
 * M5GFX controller class plus the board's real wiring. Two devices cover the
 * family's panels:
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

#if FURBLE_LILYGO_BOARD

#if defined(FURBLE_SIM)

#include <lgfx/v1/platforms/sdl/Panel_sdl.hpp>

#else  // FURBLE_SIM

#include <driver/gpio.h>
#include <lgfx/v1/Light.hpp>
#include <lgfx/v1/panel/Panel_CO5300.hpp>
#include <lgfx/v1/panel/Panel_ST7789.hpp>
#include <lgfx/v1/platforms/esp32/Bus_SPI.hpp>

#endif  // FURBLE_SIM

namespace Furble {
namespace Display {
namespace {

using namespace Furble::LilyGo;

#if defined(FURBLE_SIM)

// The simulator window scale. sim/CMakeLists.txt and sim/build.sh define
// M5GFX_SCALE=2 for the M5 models; the LilyGO models use the same value so a
// scenario's window is the same size class as the others.
#if defined(M5GFX_SCALE)
constexpr uint_fast8_t SIM_SCALE = M5GFX_SCALE;
#else
constexpr uint_fast8_t SIM_SCALE = 1;
#endif

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

/**
 * A digital backlight.
 *
 * The T-Deck's backlight is an AW9364 driven by a pulse train on GPIO42, not a
 * PWM dimmer: the panel's brightness is the number of pulses the driver sees.
 * M5GFX's Light_PWM would hold a duty cycle instead, which that part does not
 * read as a level. This port therefore drives the pin as on or off and reports
 * the limitation in plans/387; the 16-level pulse interface is a follow-up that
 * needs the AW9364 datasheet or a scope capture, neither of which the vendor
 * publishes.
 */
class LightDigital: public lgfx::ILight {
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

#endif  // FURBLE_SIM

#if defined(FURBLE_SIM)
SimPanel g_Panel;
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
#if !defined(FURBLE_SIM)
  // The board's peripheral gate and PMIC rails come up before the panel: on the
  // T-Watch Ultra the panel supply is switched by the XL9555 output that
  // powerOn() sets, and on the T-Deck the panel sits behind GPIO10.
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
  return s_Ready ? screen().width() : 0;
}

int32_t height(void) {
  return s_Ready ? screen().height() : 0;
}

}  // namespace Display
}  // namespace Furble

#endif  // FURBLE_LILYGO_BOARD
