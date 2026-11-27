#ifndef FURBLE_SIM_VENDOR_GNSS_H
#define FURBLE_SIM_VENDOR_GNSS_H

#include <cstdint>
#include <string>
#include <vector>

/*
 * Virtual vendor-text GNSS receivers: a Quectel $PAIR/$PQTM part (LC76G or
 * LC29H) and a Unicore $CFG part (UC6580 / UFirebird II).
 *
 * Neither owns a clock or a UART. The fake UART feeds bytes and a millisecond
 * timestamp and queues whatever comes back, exactly as it does for the virtual
 * u-blox M10. That keeps both models free of the simulator scheduler, so the
 * host suite drives the same state machines the scenarios drive.
 *
 * Modelled from the documents cited in plans/367-gnss-vendor-text-drivers.md:
 * Quectel "LC26G (AB)&LC76G&LC86G Series GNSS Protocol Specification" V1.1
 * (Q76) and "LC29H Series&LC79H (AL) GNSS Protocol Specification" V1.2 (Q29),
 * and Unicore "UFirebird II Series Protocol Specification" UC-04-FB2 EN R1.5
 * (UC). A behaviour the documents do not state is marked as an assumption
 * where it is decided.
 */

namespace Furble {
namespace Sim {

class VirtualQuectel {
 public:
  struct Options {
    /**
     * LC76G (PA) and (PB) do not support PAIR050, so the fix rate stays at
     * 1 Hz (Q76 2.3.9 note 2, page 38). The model answers result 3.
     */
    bool noFixRate = false;
    /**
     * LC29H (BA, CA, DA, EA) do not support PAIR066 (Q29 2.4.15 note 4,
     * page 65). The model answers result 3.
     */
    bool noSearchMode = false;
    /** A backup domain keeps RAM across a rail cut, so no save is needed. */
    bool backup = false;
    const char *version = "LC29HAANR01A04S";
    const char *buildDate = "2022/11/04";
    const char *buildTime = "16:39:48";
  };

  enum class State : uint8_t {
    OFF, /**< no supply: the rail is cut */
    ON,  /**< powered, whether or not the GNSS engine is running */
  };

  struct Settings {
    uint16_t rate_ms = 1000;
    /* PAIR062 types 0 to 9. The default set is GGA, GLL, GSA, GSV, RMC, VTG,
       which Q29 2.4.13 note 3 page 61 states. */
    uint8_t sentence[10] = {1, 1, 1, 1, 1, 1, 0, 0, 0, 0};
    bool gps = true;
    bool glonass = false;
    bool galileo = true;
    bool bds = true;
    bool qzss = true;
    bool sbas = false;
    uint8_t nav_mode = 0;
    uint32_t baud = 115200;
  };

  void reset(const Options &options, uint32_t now);

  /** Bytes from the host. Returns the receiver's replies, in order. */
  std::vector<uint8_t> receive(const uint8_t *data, size_t length, uint32_t now);

  /** Rail power from the host board. */
  void setPower(bool on, uint32_t now);

  /** Advance the standby timer. */
  void tick(uint32_t now);

  /** Is the receiver producing navigation output right now? */
  bool outputting(uint32_t now) const;
  /** Fix interval in ms, PAIR050. */
  uint32_t periodMs(void) const { return m_Settings.rate_ms; }
  /** Has the current start reached its first fix? */
  bool fixed(uint32_t now) const;

  State state(void) const { return m_State; }
  static const char *stateName(State state);
  /** The PQTMVERNO version string the model reports. */
  const char *model(void) const { return m_Options.version; }
  /** The build date and time the model reports, as one string. */
  std::string firmware(void) const {
    return std::string(m_Options.buildDate) + " " + m_Options.buildTime;
  }
  /** True while $PAIR650 holds the GNSS in backup mode. */
  bool sleeping(uint32_t now) const;
  /** True after a $PAIR003 with no $PAIR002 since. */
  bool gnssOff(void) const { return m_GnssOff; }
  /** The settings in RAM, which a save copies to flash. */
  const Settings &settings(void) const { return m_Settings; }
  bool saved(void) const { return m_Saved; }

  /* Counters the scenarios assert on. */
  uint32_t commands = 0;
  uint32_t acks = 0;
  uint32_t naks = 0;
  uint32_t unsupported = 0;
  uint32_t badChecksum = 0;
  uint32_t saves = 0;
  uint32_t backups = 0;
  uint32_t restarts = 0;
  uint32_t unknown = 0;

  /** Last $PAIR650 interval that was accepted, seconds. */
  uint32_t standbyS = 0;

 private:
  std::vector<uint8_t> handle(const std::string &line, uint32_t now);
  std::vector<uint8_t> ack(uint16_t id, uint8_t result);
  void store(void);

  Options m_Options;
  State m_State = State::OFF;
  Settings m_Settings;
  Settings m_Flash;
  bool m_Saved = false;
  bool m_EverPowered = false;
  bool m_GnssOff = false;
  bool m_SleepForever = false;
  uint32_t m_WakeAt = 0;
  std::string m_Partial;
  uint32_t m_FirstFixAt = 0;
};

class VirtualUnicore {
 public:
  struct Options {
    /** A flash-version product, so CFGSAVE is available (UC 3.3, page 47). */
    bool flash = true;
    /** A receiver that answers no $CFG command at all, for a silence case. */
    bool silent = false;
    const char *model = "UC6580";
    const char *config = "G1B1L1E1";
    const char *hw = "V1.0";
    const char *fw = "R6.0.3.0Build7926";
  };

  enum class State : uint8_t {
    OFF, /**< no supply: the rail is cut */
    ON,  /**< powered */
  };

  struct Settings {
    uint16_t rate_ms = 1000;
    /* CFGMSG class 0 ids 0 to 8. The document states no default set, so the
       model ships GGA, GLL, GSA, GSV, RMC and VTG on and the rest off, the same
       default furble writes. Assumption. */
    uint8_t sentence[9] = {1, 1, 1, 1, 1, 1, 0, 0, 0};
    uint32_t sys = 0;
    uint32_t baud = 115200;
  };

  void reset(const Options &options, uint32_t now);

  std::vector<uint8_t> receive(const uint8_t *data, size_t length, uint32_t now);
  void setPower(bool on, uint32_t now);
  void tick(uint32_t now);

  bool outputting(uint32_t now) const;
  uint32_t periodMs(void) const { return m_Settings.rate_ms; }
  bool fixed(uint32_t now) const;

  State state(void) const { return m_State; }
  static const char *stateName(State state);
  /** The PDTINFO product name and constellation flag the model reports. */
  std::string model(void) const { return std::string(m_Options.model) + " " + m_Options.config; }
  /** The PDTINFO firmware version the model reports. */
  const char *firmware(void) const { return m_Options.fw; }
  const Settings &settings(void) const { return m_Settings; }
  bool saved(void) const { return m_Saved; }

  uint32_t commands = 0;
  uint32_t acks = 0;
  uint32_t fails = 0;
  uint32_t badChecksum = 0;
  uint32_t saves = 0;
  uint32_t resets = 0;
  uint32_t unknown = 0;

 private:
  std::vector<uint8_t> handle(const std::string &line, uint32_t now);
  std::vector<uint8_t> ok(void);
  std::vector<uint8_t> fail(uint8_t code);
  void store(void);

  Options m_Options;
  State m_State = State::OFF;
  Settings m_Settings;
  Settings m_Flash;
  bool m_Saved = false;
  bool m_EverPowered = false;
  std::string m_Partial;
  uint32_t m_FirstFixAt = 0;
};

}  // namespace Sim
}  // namespace Furble

#endif
