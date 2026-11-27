#ifndef FURBLE_GPS_QUECTEL_H
#define FURBLE_GPS_QUECTEL_H

#include <cstdint>
#include <string>
#include <vector>

#include "GpsText.h"

/*
 * Quectel $PAIR and $PQTM text commands, for the LC76G and LC29H families.
 *
 * Every builder cites its command, section and page. "Q76" is the Quectel
 * "LC26G (AB)&LC76G&LC86G Series GNSS Protocol Specification" V1.1, 2022-12-20,
 * status Released. "Q29" is the Quectel "LC29H Series&LC79H (AL) GNSS Protocol
 * Specification" V1.2, 2023-06-20, status Released. Where the two documents
 * disagree about which variant supports a command, the plan refuses or reports
 * the refusal instead of guessing, and docs/protocols/gnss-quectel-pair.md holds
 * the full table.
 *
 * The frame is NMEA shaped: '$' body '*' XOR CR LF (Q76 2.1 Table 3, page 11).
 * A set command is answered by $PAIR001,<CommandID>,<Result> (Q76 2.3.1 page
 * 32) and a PQTM set or get answers with the command name (Q76 2.4, pages 95
 * to 100).
 */

namespace Furble {
namespace Quectel {

using GpsText::Command;

/** Packet ids this driver sends or matches. The id is the command's own id. */
constexpr uint16_t ID_ACK = 1;             /* Q76 2.3.1 page 32 */
constexpr uint16_t ID_POWER_ON = 2;        /* Q76 2.3.2 page 33 */
constexpr uint16_t ID_POWER_OFF = 3;       /* Q76 2.3.3 page 34 */
constexpr uint16_t ID_HOT_START = 4;       /* Q76 2.3.4 page 34 */
constexpr uint16_t ID_WARM_START = 5;      /* Q76 2.3.5 page 35 */
constexpr uint16_t ID_COLD_START = 6;      /* Q76 2.3.6 page 36 */
constexpr uint16_t ID_FIX_RATE = 50;       /* Q76 2.3.9 page 38 */
constexpr uint16_t ID_SENTENCE_RATE = 62;  /* Q76 2.3.12 page 40 */
constexpr uint16_t ID_SEARCH_MODE = 66;    /* Q76 2.3.15 page 43 */
constexpr uint16_t ID_NAV_MODE = 80;       /* Q76 2.3.23 page 50 */
constexpr uint16_t ID_LOCK_SLEEP = 382;    /* Q76 2.3.30 page 56 */
constexpr uint16_t ID_SBAS = 410;          /* Q76 2.3.34 page 60 */
constexpr uint16_t ID_SAVE_NAV = 511;      /* Q76 2.3.44 page 67 */
constexpr uint16_t ID_SAVE_SETTINGS = 513; /* Q76 2.3.45 page 68 */
constexpr uint16_t ID_BACKUP = 650;        /* Q76 2.3.46 page 69 */
constexpr uint16_t ID_BAUD = 864;          /* Q76 2.3.56 page 78 */

/** Fix interval bounds, milliseconds (Q76 2.3.9 page 38, Q29 2.4.9 page 57). */
constexpr uint16_t FIX_RATE_MIN_MS = 100;
constexpr uint16_t FIX_RATE_MAX_MS = 1000;

/**
 * Backup mode bounds, seconds.
 *
 * Zero means "no timer", and any other value is 10 s to 62208000 s, two years
 * (Q76 2.3.46 page 69, Q29 2.4.47 page 95). furble's 5 s standby interval is
 * inside that gap, so a 5 s request is refused rather than rounded.
 */
constexpr uint32_t BACKUP_MIN_S = 10;
constexpr uint32_t BACKUP_MAX_S = 62208000;

/** NMEA sentence type ids of PAIR062 (Q76 2.3.12 page 40). */
enum SentenceType : uint8_t {
  SEN_GGA = 0,
  SEN_GLL = 1,
  SEN_GSA = 2,
  SEN_GSV = 3,
  SEN_RMC = 4,
  SEN_VTG = 5,
  SEN_ZDA = 6,
  SEN_GRS = 7,
  SEN_GST = 8,
};

/** What a plan decided, so the driver keeps only this. */
struct Plan {
  uint16_t rate_ms = 0;      /**< fix interval sent, 0 when left alone */
  uint8_t gnss = 0;          /**< effective constellation mask */
  bool gnss_refused = false; /**< the mask is not a documented combination */
  bool sbas = false;         /**< SBAS enable sent, and what it sent */
  bool sbas_refused = false; /**< SBAS was asked for but not decided by the mask */
  uint8_t sentences = 0;     /**< the GpsText::SentenceSet that was sent */
  bool sentences_set = false;
  bool platform_refused = false; /**< no documented Quectel mode for the setting */
  uint8_t nav_mode = 0;          /**< PAIR080 value sent, 0 normal */
  bool rate_refused = false;     /**< the rate is outside PAIR050's range */
  std::vector<Command> commands; /**< send order */
};

/** $PQTMVERNO: identification (Q76 2.4.5 pages 99 to 100, Q29 2.3.1 page 33). */
Command verNo(void);

/** $PQTMCFGMSGRATE,W,PQTMEPE,<rate>,2 (Q76 2.4.1 pages 96 to 97). */
Command messageRate(uint8_t rate);

/** $PAIR050,<ms> (Q76 2.3.9 page 38). */
Command fixRate(uint16_t ms);

/** $PAIR062,<type>,<rate> (Q76 2.3.12 page 40). */
Command sentenceRate(uint8_t type, uint8_t rate);

/**
 * $PAIR066,<GPS>,<GLO>,<GAL>,<BDS>,<QZSS>,0 (Q76 2.3.15 pages 43 to 44).
 *
 * Returns an empty Command when the mask is not one of the documented
 * combinations; gnssSupported() answers that question alone.
 */
Command searchMode(uint8_t mask);

/** Is this constellation mask one PAIR066 documents (Q76 2.3.15 page 44)? */
bool gnssSupported(uint8_t mask);

/** $PAIR410,<0|1> (Q76 2.3.34 page 60). */
Command sbasEnable(bool enable);

/** $PAIR080,<mode>: 0 normal, 1 fitness (Q76 2.3.23 page 50). */
Command navMode(uint8_t mode);

/** $PAIR864,0,0,<baud> (Q76 2.3.56 page 78). Empty for an undocumented rate. */
Command baud(uint32_t baud);

/** $PAIR382,1 (Q76 2.3.30 page 56): keep the command port alive across PAIR003. */
Command lockSleep(void);

/** $PAIR003 / $PAIR002: GNSS subsystem off and on (Q76 2.3.3 page 34, 2.3.2 page 33). */
Command powerOff(void);
Command powerOn(void);

/** $PAIR513 and $PQTMSAVEPAR: save settings to flash (Q76 2.3.45 page 68, 2.4.3 page 98). */
Command saveSettings(void);
Command saveParameters(void);

/** $PAIR511: save navigation data to flash (Q76 2.3.44 page 67). */
Command saveNavigation(void);

/**
 * $PAIR650,<seconds>: timed backup mode (Q76 2.3.46 page 69).
 *
 * Returns false and leaves out untouched when the interval is not 0 and below
 * BACKUP_MIN_S, because the receiver would answer a parameter error.
 */
bool standby(uint32_t seconds, Command &out);

/** $PAIR004, 005 or 006: hot, warm or cold start (Q76 2.3.4 to 2.3.6, pages 34 to 36). */
Command restart(uint8_t mode);

/**
 * Turn the settings into the command queue.
 *
 * Order: the sentence set, the fix rate, the navigation mode, SBAS and the
 * constellation last. A PAIR066 write reboots an LC76G's GNSS engine (Q76
 * 2.3.15 page 43), so it goes last and nothing furble needs at boot follows it.
 * Identification is not queued: detection already polled $PQTMVERNO once this
 * session.
 */
Plan plan(const GpsText::Request &request);

}  // namespace Quectel
}  // namespace Furble

#endif
