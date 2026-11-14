#ifndef FURBLE_SIM_UBX_M10_H
#define FURBLE_SIM_UBX_M10_H

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "protocol/GpsUbx.h"

namespace Furble {
namespace Sim {

/**
 * A virtual u-blox M10 receiver: the UBX side of MAX-M10S and MIA-M10Q.
 *
 * It owns no clock and no UART. The fake UART feeds it the host's bytes and a
 * millisecond timestamp, queues whatever it answers, and asks it for one
 * navigation epoch at a time. That keeps it free of the simulator scheduler, so
 * the host suite tests the same state machine the scenarios drive.
 *
 * Modelled from the official documents listed in plans/321-ublox-m10-driver.md:
 * the configuration database with RAM, BBR and default layers and the VALSET
 * validity rules (IFD 3.10, 4.3, 4.9.21), MON-VER, ACK/NAK, RXM-PMREQ software
 * standby (IM 3.6.3.2), CFG-RST, UPD-SOS without a flash, power save modes, and
 * the start types with the data sheet TTFF and current figures. Anything the
 * documents do not state is marked as an assumption where it is decided.
 */
class VirtualM10 {
 public:
  struct Options {
    bool vbckp = false; /**< a backup supply keeps BBR and the RTC over a rail cut */
    bool flash = false; /**< an external flash, which UPD-SOS needs */
    const char *module = "MAX-M10S";
    bool hot = false; /**< power up hot instead of cold */
    /** A key this firmware does not know, so VALSET and VALGET NAK it. 0 for none. */
    uint32_t unknownKey = 0;
  };

  enum class State : uint8_t {
    OFF,     /**< no supply */
    ON,      /**< navigating */
    STOPPED, /**< CFG-RST controlled GNSS stop */
    STANDBY, /**< RXM-PMREQ software standby */
  };

  void reset(const Options &options, uint32_t now);

  /** Bytes from the host. Returns the receiver's replies, in order. */
  std::vector<uint8_t> receive(const uint8_t *data, size_t length, uint32_t now);

  /** Rail power from the host board. */
  void setPower(bool on, uint32_t now);

  /** Advance timers: standby expiry. Call before asking for an epoch. */
  void tick(uint32_t now);

  /** Is the receiver producing epochs right now? */
  bool outputting(uint32_t now);

  /** Measurement period in ms, CFG-RATE-MEAS or the PSMOO update period. */
  uint32_t periodMs(void) const;

  /** Has the current start reached its first fix? */
  bool fixed(uint32_t now) const;

  /** The UBX part of one epoch, NAV-PVT and NAV-SAT as configured. */
  std::vector<uint8_t> epoch(uint32_t now,
                             uint16_t year,
                             uint8_t month,
                             uint8_t day,
                             uint32_t secondOfDay);

  /** Effective value of a key: RAM, then BBR, then the default. */
  uint64_t value(uint32_t key) const;
  bool hasBbr(uint32_t key) const { return m_Bbr.count(key) != 0; }

  State state(void) const { return m_State; }
  static const char *stateName(State state);
  const char *startName(void) const { return m_StartName; }

  /** Average supply current since reset, from the data sheet figures, in uA. */
  uint32_t averageMicroamps(uint32_t now);

  uint32_t valsets = 0;
  uint32_t naks = 0;
  uint32_t valgets = 0;
  uint32_t pmreqs = 0;
  uint32_t sosCreates = 0;
  uint32_t sosPolls = 0;
  uint32_t monVerPolls = 0;
  uint32_t resets = 0;
  uint32_t coldStarts = 0;
  uint32_t warmStarts = 0;
  uint32_t hotStarts = 0;
  uint32_t ignoredBytes = 0;

 private:
  bool known(uint32_t key) const;
  void start(uint32_t now, bool keepBbr);
  void restart(uint16_t mask, uint32_t now);
  std::vector<uint8_t> handle(const Ubx::Stream &frame, uint32_t now);
  bool valid(const std::map<uint32_t, uint64_t> &ram) const;
  uint64_t valueIn(const std::map<uint32_t, uint64_t> &ram, uint32_t key) const;
  void account(uint32_t now);
  uint32_t currentMicroamps(void) const;

  Options m_Options;
  Ubx::Stream m_Stream;
  State m_State = State::OFF;
  std::map<uint32_t, uint64_t> m_Ram;
  std::map<uint32_t, uint64_t> m_Bbr;
  std::vector<uint8_t> m_Backup;
  bool m_HaveBackup = false;
  uint8_t m_Restored = Ubx::SOS_RESTORE_NONE;
  uint32_t m_FixAt = 0;
  uint32_t m_StandbyUntil = 0;
  bool m_StandbyForever = false;
  bool m_WakeOnUart = false;
  bool m_EverFixed = false;
  uint32_t m_LastFix = 0;
  bool m_RtcValid = false;
  uint32_t m_LastEpoch = 0;
  uint16_t m_Year = 2026;
  uint8_t m_Month = 9;
  uint8_t m_Day = 6;
  uint32_t m_SecondOfDay = 45319;
  const char *m_StartName = "cold";
  uint32_t m_AccountAt = 0;
  uint64_t m_ChargeMicroampMs = 0;
  uint32_t m_Started = 0;
};

}  // namespace Sim
}  // namespace Furble

#endif
