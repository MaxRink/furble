#ifndef FURBLE_IP5306_H
#define FURBLE_IP5306_H

#include <cstdint>

namespace Furble {
namespace IP5306 {

/*
 * Session policy for the Injoinic IP5306 boost converter and charger.
 *
 * Register map: Injoinic "IP5306 register document" V1.21 (document revision
 * V1.4), hosted by M5Stack at
 * https://m5stack.oss-cn-shenzhen.aliyuncs.com/resource/docs/datasheet/core/IIC_IP5306_REG_V1.4_cn.pdf
 *
 *   SYS_CTL0 (0x00) bit 1  BOOST output normally open: 0 disable, 1 enable.
 *                          The datasheet calls this "BOOST 输出常开功能". With
 *                          it clear the boost is cut after the light-load
 *                          window whenever the load stays below 45 mA.
 *   SYS_CTL2 (0x02) bits 3:2 light-load shutdown time: 00 8 s, 01 32 s,
 *                          10 16 s, 11 64 s.
 *
 * M5Unified 0.2.20 writes SYS_CTL0 = 0b00110001 at M5.begin() for
 * board_M5Stack, which clears bit 1, and SYS_CTL2 = 0b01101100, which already
 * selects the 64 s window. Furble sets bit 1 for the whole session because a
 * panel-off Core draws less than 45 mA and has no PMIC shutdown command, so a
 * cut is not a recoverable power-off: the 5 V boost would stay off while the
 * ESP32 keeps running or sits in a wake-less deep sleep. See plan 354.
 */
constexpr uint8_t REG_SYS_CTL0 = 0x00;
constexpr uint8_t REG_SYS_CTL2 = 0x02;

constexpr uint8_t SYS_CTL0_BOOST_KEEP_ON = 0x02;
constexpr uint8_t SYS_CTL2_LIGHT_LOAD_MASK = 0x0C;
constexpr uint8_t SYS_CTL2_LIGHT_LOAD_64S = 0x0C;

/** True when SYS_CTL0 has the boost held on at light load. */
constexpr bool boostKeptOn(uint8_t sysCtl0) {
  return (sysCtl0 & SYS_CTL0_BOOST_KEEP_ON) != 0;
}

/** SYS_CTL0 with the boost held on, every other bit untouched. */
constexpr uint8_t withBoostKeptOn(uint8_t sysCtl0) {
  return static_cast<uint8_t>(sysCtl0 | SYS_CTL0_BOOST_KEEP_ON);
}

/** The light-load shutdown time in seconds encoded in SYS_CTL2 bits 3:2. */
constexpr uint8_t lightLoadSeconds(uint8_t sysCtl2) {
  switch (sysCtl2 & SYS_CTL2_LIGHT_LOAD_MASK) {
    case 0x00:
      return 8;
    case 0x04:
      return 32;
    case 0x08:
      return 16;
    default:
      return 64;
  }
}

/** SYS_CTL2 with the longest light-load window, every other bit untouched. */
constexpr uint8_t withLongestLightLoad(uint8_t sysCtl2) {
  return static_cast<uint8_t>((sysCtl2 & ~SYS_CTL2_LIGHT_LOAD_MASK) | SYS_CTL2_LIGHT_LOAD_64S);
}

/**
 * Hold the boost on for this session through an M5Unified-compatible PMIC.
 *
 * Pmic needs `bool setPowerBoostKeepOn(bool)` and
 * `uint8_t readRegister8(uint8_t)`, which m5::IP5306_Class provides. The
 * register write alone is not trusted: M5Unified's setter reports the I2C
 * write result, so the readback proves the bit survived.
 *
 * @return true only when SYS_CTL0 bit 1 reads back set.
 */
template <typename Pmic>
bool holdBoostOn(Pmic &pmic) {
  if (!pmic.setPowerBoostKeepOn(true)) {
    return false;
  }
  return boostKeptOn(pmic.readRegister8(REG_SYS_CTL0));
}

}  // namespace IP5306
}  // namespace Furble

#endif  // FURBLE_IP5306_H
