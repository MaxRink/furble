#ifndef FURBLE_HAL_AXP2101_H
#define FURBLE_HAL_AXP2101_H

#include <cstdint>

#include "HalBus.h"

/*
 * X-Powers AXP2101 PMIC, the subset a non-M5 board needs.
 *
 * Source: X-Powers "AXP2101 Single Cell NVDC PMU with E-gauge" datasheet,
 * revision 1.0, 2021-05-31. Section numbers below refer to that document.
 * Boards that the board support library already drives (Core2 v1.1, CoreS3)
 * keep that path; this driver is for boards it does not know.
 *
 * VRTC (RTCLDO1) has no enable bit anywhere in the section 6.13 register map.
 * It stays on while either VBAT or the VBACKUP cell supplies the chip, and it
 * survives soft power off (section 6.5.4.5). A GNSS backup pin fed from VRTC
 * therefore keeps its almanac when the GNSS main rail is switched off, and
 * powerOff() does not cut it. Rail deliberately has no VRTC member.
 */

namespace Furble {
namespace HAL {

class AXP2101 {
 public:
  static constexpr uint8_t ADDRESS = 0x34;

  /** Switchable outputs. VRTC is not one, see the header comment. */
  enum class Rail : uint8_t {
    DCDC1,
    DCDC2,
    DCDC3,
    DCDC4,
    DCDC5,
    ALDO1,
    ALDO2,
    ALDO3,
    ALDO4,
    BLDO1,
    BLDO2,
    CPUSLDO,
    DLDO1,
    DLDO2,
  };
  static constexpr uint8_t RAIL_COUNT = 14;

  /** Bit for Rail r in a protected-rail mask. */
  static constexpr uint16_t railBit(Rail r) {
    return static_cast<uint16_t>(1U << static_cast<uint8_t>(r));
  }

  /** Battery current direction, REG 01 bits 6:5 (section 6.13.2.2). */
  enum class Charge : uint8_t {
    STANDBY,
    CHARGING,
    DISCHARGING,
  };

  /** Power key events latched in REG 49 bits 3:0 (section 6.13.2.44). */
  struct PowerKey {
    bool shortPress;
    bool longPress;
    bool falling;
    bool rising;
  };

  /**
   * @param protectedRails railBit() mask of outputs that setRail() must never
   * switch off, for example the rail feeding the SoC.
   */
  explicit AXP2101(RegisterBus &bus, uint16_t protectedRails = 0)
      : m_Bus(bus), m_ProtectedRails(protectedRails) {}

  /**
   * True when the chip answers with the AXP2101 identity.
   *
   * REG 03 is not in the revision 1.0 register map. The 0x4A identity is the
   * value X-Powers' reference library XPowersLib checks.
   */
  bool probe(void);

  /** REG 00 bit 3, battery present (section 6.13.2.1). */
  bool batteryPresent(bool &present);

  /** REG 00 bit 5, VBUS good (section 6.13.2.1). */
  bool vbusGood(bool &good);

  /** REG 34/35, 14-bit VBAT in 1 mV steps (sections 6.10 and 6.13.2.30). */
  bool batteryVoltage(uint16_t &millivolts);

  /**
   * REG A4 fuel gauge percentage (section 6.13.2.88).
   *
   * Returns false without a battery or with the gauge disabled (REG 18 bit 3),
   * because the register then holds a stale or meaningless value.
   */
  bool batteryPercent(uint8_t &percent);

  /** REG 01 bits 6:5 (section 6.13.2.2). */
  bool chargeState(Charge &state);

  /**
   * Switch one output. Refuses to switch off a protected rail.
   *
   * Enable bits are REG 80 (DCDC, section 6.13.2.68) and REG 90/91 (LDO,
   * sections 6.13.2.75 and 6.13.2.76).
   */
  bool setRail(Rail rail, bool on);

  bool railEnabled(Rail rail, bool &on);

  /**
   * Set an ALDO1-4 or BLDO1-2 output voltage, 500 to 3500 mV in 100 mV steps
   * (REG 92-97, sections 6.13.2.77 to 6.13.2.82). Other rails return false.
   * Values between steps round down.
   */
  bool setLdoVoltage(Rail rail, uint16_t millivolts);

  /**
   * Charge a VBACKUP cell (REG 18 bit 2, section 6.12.2) and set its
   * termination voltage, 2600 to 3300 mV in 100 mV steps (REG 6A, section
   * 6.13.2.67). Enable only when a rechargeable cell is fitted.
   */
  bool setBackupCharge(bool enable, uint16_t millivolts = 3300);

  /** Enable the short and long press interrupts (REG 41 bits 3:2). */
  bool enablePowerKeyIrq(bool enable);

  /** Read and clear the latched power key events (REG 49, write 1 to clear). */
  bool readPowerKey(PowerKey &key);

  /** Soft power off, REG 10 bit 0 (section 6.13.2.7). VRTC stays up. */
  bool powerOff(void);

 private:
  RegisterBus &m_Bus;
  const uint16_t m_ProtectedRails;
};

}  // namespace HAL
}  // namespace Furble

#endif
