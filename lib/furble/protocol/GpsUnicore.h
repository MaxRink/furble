#ifndef FURBLE_GPS_UNICORE_H
#define FURBLE_GPS_UNICORE_H

#include <cstdint>
#include <string>
#include <vector>

#include "GpsText.h"

/*
 * Unicore $CFG text commands, for the UC6580 and the UFirebird II family.
 *
 * Every builder cites its command, table and page. "UC" is the Unicore
 * "UFirebird II Series Protocol Specification", UC-04-FB2 EN R1.5, November
 * 2025, which lists UC6580, UM670A, UM680A and UM681A as applicable.
 *
 * The frame is '$' name ',' fields '*' XOR CR LF, the checksum is the XOR of
 * everything between the '$' and the '*', and an input checksum is optional but
 * checked when present (UC 3.1, pages 24 and 25). Acknowledgements carry no
 * command name: the receiver answers a bare $OK (UC 3.2, page 30) or
 * $FAIL,<code> (UC 3.2, page 31), so the seam keeps one command in flight.
 */

namespace Furble {
namespace Unicore {

using GpsText::Command;

/** UART rates CFGPRT documents (UC 3.3 Table 3-9, page 33). */
constexpr uint32_t BAUD_MIN = 9600;
constexpr uint32_t BAUD_MAX = 921600;

/**
 * Fix interval bounds, milliseconds.
 *
 * Table 3-19 prints the examples 1000, 200 and 100 for MeasRate and NavRate and
 * states no range (UC 3.3, page 43), so the bounds are furble's own: the
 * shortest interval its GPS_RATE ladder asks for, 100 ms (include/FurbleGPS.h,
 * RATE_MS), and one second, which is the table's first example. The 50 in that
 * table is the DRNavRate 20 Hz example and is not a navigation rate.
 */
constexpr uint16_t NAV_RATE_MIN_MS = 100;
constexpr uint16_t NAV_RATE_MAX_MS = 1000;

/**
 * Message class 0 configures four ports or queries the current one (UC 3.3
 * Table 3-12, page 36). Writing class 0 reaches whichever port furble is on,
 * which a fixed 200 (UART1 only) would not.
 */
constexpr uint8_t CLASS_ALL_PORTS = 0;

/** NMEA message ids of CFGMSG (UC 3.3 Table 3-12, page 36). */
enum MessageId : uint8_t {
  MSG_GGA = 0,
  MSG_GLL = 1,
  MSG_GSA = 2,
  MSG_GSV = 3,
  MSG_RMC = 4,
  MSG_VTG = 5,
  MSG_ZDA = 6,
  MSG_GST = 7,
  MSG_GBS = 8,
};

/** CFGSYS sysMask bits (UC 3.3 Table 3-21, pages 44 to 46). */
constexpr uint32_t SYS_GPS_L1CA = 1UL << 0;
constexpr uint32_t SYS_GPS_L5 = 1UL << 2;
constexpr uint32_t SYS_BDS_B1I = 1UL << 4;
constexpr uint32_t SYS_BDS_B2A = 1UL << 6;
constexpr uint32_t SYS_BDS_B1C = 1UL << 7;
constexpr uint32_t SYS_GLO_L1 = 1UL << 8;
constexpr uint32_t SYS_GAL_E1 = 1UL << 12;
constexpr uint32_t SYS_GAL_E5A = 1UL << 14;
constexpr uint32_t SYS_QZSS_L1CA = 1UL << 16;
constexpr uint32_t SYS_QZSS_L5 = 1UL << 18;
constexpr uint32_t SYS_SBAS = 1UL << 20;

/**
 * $CFGDYN lock point mask and dynamic model values (UC 3.3 Table 3-41,
 * page 58). Bit 0 selects DynModel; StaticHoldThresh is left at 0 because
 * furble has no setting for it.
 */
constexpr uint8_t DYN_MASK_MODEL = 0x01;
constexpr uint8_t DYN_PORTABLE = 0;
constexpr uint8_t DYN_STATIC = 1;

/** Reset clear masks (UC 3.2 Table 3-5, page 29). */
constexpr uint32_t CLEAR_HOT = 0x00;
constexpr uint32_t CLEAR_WARM = 0x01;
constexpr uint32_t CLEAR_COLD = 0x85;

/** What a plan decided, so the driver keeps only this. */
struct Plan {
  uint16_t rate_ms = 0;      /**< fix interval sent, 0 when left alone */
  uint32_t sys_mask = 0;     /**< CFGSYS mask sent, 0 when left alone */
  uint8_t gnss = 0;          /**< effective constellation mask, furble bits */
  bool gnss_refused = false; /**< the mask broke a documented rule */
  uint8_t sentences = 0;     /**< the GpsText::SentenceSet that was sent */
  bool sentences_set = false;
  bool rate_refused = false;     /**< the rate is outside CFGNAV's range */
  bool platform_refused = false; /**< the setting has no documented DynModel */
  uint8_t platform = 0;          /**< GPS_PLATFORM value sent with CFGDYN, 0 none */
  bool psm_refused = false;      /**< Unicore documents no standby command */
  std::vector<Command> commands; /**< send order */
};

/** $PDTINFO: identification (UC 3.2 Table 3-1, page 26). */
Command productInfo(void);

/** $CFGNAV,<ms>,<ms>,: observation and navigation interval (UC 3.3 Table 3-19, page 43). */
Command navRate(uint16_t ms);

/**
 * $CFGMSG,0,<id>,<rate> (UC 3.3 Table 3-11, page 34).
 *
 * Rate 0 disables the message, 1 outputs it every fix.
 */
Command messageRate(uint8_t id, uint8_t rate);

/**
 * $CFGSYS,h<mask> (UC 3.3 Table 3-21, pages 44 to 46).
 *
 * Returns an empty Command when the constellation mask breaks a documented
 * rule; sysMask() answers that question alone.
 */
Command system(uint8_t mask);

/** The CFGSYS mask a furble constellation mask maps to. */
uint32_t sysMask(uint8_t mask);

/** Is this constellation mask one CFGSYS can express under its own rules? */
bool gnssSupported(uint8_t mask);

/** $CFGPRT,1,0,<baud>,1,3: keep UNICORE input, add NMEA output (UC 3.3 Table 3-9, page 32). */
bool port(uint32_t baud, Command &out);

/**
 * $CFGDYN,1,<DynModel>,0: portable or static lock point (UC 3.3 Table 3-41,
 * page 58).
 *
 * DynModel 0 is Portable and 1 is Static, which is exactly furble's Portable
 * and Stationary platform values. Pedestrian and Vehicle have no documented
 * counterpart and are refused rather than approximated.
 */
Command dynamicModel(uint8_t model);

/** $CFGSAVE: write the configuration to flash (UC 3.3 Table 3-24, page 47). */
Command save(void);

/** $CFGCLR: drop the stored configuration (UC 3.3 Table 3-25, page 47). */
Command clear(void);

/** $RESET,0,<mask>: hot, warm or cold start (UC 3.2 Table 3-5, page 29). */
Command restart(uint8_t mode);

/**
 * Turn the settings into the command queue.
 *
 * Order: the sentence set, the navigation rate, the dynamic model, then the
 * constellation mask, which resets the receiver (UC 3.3, page 44), so it goes
 * last. Standby has no documented command in UC R1.5 and is reported refused
 * rather than approximated; the platform setting uses $CFGDYN, and the two
 * values it cannot express are refused.
 */
Plan plan(const GpsText::Request &request);

}  // namespace Unicore
}  // namespace Furble

#endif
