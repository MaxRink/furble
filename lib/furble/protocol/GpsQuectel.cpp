/*
 * FURBLE_GPS_TEXT=0 drops the vendor text backend from a build with no flash
 * for it (the Core debug profile). The Quectel dialect is only reachable
 * through src/FurbleGPSText.cpp, which is stubbed in that build, so this
 * file compiles to nothing there.
 */
#if !defined(FURBLE_GPS_TEXT) || FURBLE_GPS_TEXT

#include "GpsQuectel.h"

#include <array>

namespace Furble {
namespace Quectel {

namespace {

/** A set command answered by $PAIR001,<id>,<result>. */
Command ack(uint16_t id, GpsText::Op op, const std::string &body) {
  Command command;
  command.dialect = GpsText::Dialect::QUECTEL;
  command.op = op;
  command.body = body;
  command.ack = "PAIR001";
  command.ack_id = id;
  return command;
}

/** A PQTM command answered by a reply that carries the command name. */
Command pqtm(GpsText::Op op, const std::string &name, const std::string &fields) {
  Command command;
  command.dialect = GpsText::Dialect::QUECTEL;
  command.op = op;
  command.body = fields.empty() ? name : (name + "," + fields);
  command.ack = name;
  command.ack_id = 0;
  return command;
}

constexpr std::array<uint32_t, 8> BAUDS = {9600,   19200,  38400,  57600,
                                           115200, 230400, 460800, 921600};

bool baudListed(uint32_t rate) {
  for (const uint32_t entry : BAUDS) {
    if (entry == rate) {
      return true;
    }
  }
  return false;
}

/** The six sentence types both documents define, in PAIR062 order. */
constexpr std::array<uint8_t, 6> SENTENCE_TYPES = {
    SEN_GGA, SEN_GLL, SEN_GSA, SEN_GSV, SEN_RMC, SEN_VTG,
};

/** Is this sentence on in the requested set? */
bool sentenceEnabled(GpsText::SentenceSet set, uint8_t type) {
  const bool fix = (type == SEN_GGA) || (type == SEN_RMC);
  const bool detail = (type == SEN_GSA) || (type == SEN_GSV);
  switch (set) {
    case GpsText::SentenceSet::PRUNE:
      return fix;
    case GpsText::SentenceSet::SATELLITE:
      return fix || detail;
    case GpsText::SentenceSet::STANDARD:
      // "GGA, GLL, GSA, GSV, RMC and VTG messages are output by default"
      // (Q29 2.4.13 note 3, page 62)
      return true;
  }
  return true;
}

}  // namespace

Command verNo(void) {
  return pqtm(GpsText::Op::VERSION, "PQTMVERNO", "");
}

Command messageRate(uint8_t rate) {
  // PQTMEPE version 2 is the only PQTM message the LC76G document lets this
  // command configure (Q76 2.4.1 page 96).
  return pqtm(GpsText::Op::MSG_RATE, "PQTMCFGMSGRATE", "W,PQTMEPE," + std::to_string(rate) + ",2");
}

Command fixRate(uint16_t ms) {
  return ack(ID_FIX_RATE, GpsText::Op::FIX_RATE, "PAIR050," + std::to_string(ms));
}

Command sentenceRate(uint8_t type, uint8_t rate) {
  return ack(ID_SENTENCE_RATE, GpsText::Op::SENTENCES,
             "PAIR062," + std::to_string(type) + "," + std::to_string(rate));
}

bool gnssSupported(uint8_t mask) {
  // GPS is the system every documented combination contains (Q76 2.3.15 note 2
  // page 44). QZSS rides on GPS and SBAS is PAIR410, so neither is counted.
  if ((mask & GpsText::GNSS_GPS) == 0) {
    return false;
  }
  uint8_t majors = 0;
  for (const uint8_t bit : {GpsText::GNSS_GLO, GpsText::GNSS_GAL, GpsText::GNSS_BDS}) {
    if ((mask & bit) != 0) {
      majors++;
    }
  }
  // GPS alone, one more system, or all three. Two of the three is the gap the
  // document leaves: it lists GPS+GLONASS, GPS+Galileo, GPS+BDS and the full
  // GPS+GLONASS+Galileo+BDS, and nothing in between.
  return (majors == 0) || (majors == 1) || (majors == 3);
}

Command searchMode(uint8_t mask) {
  if (!gnssSupported(mask)) {
    return Command();
  }
  const bool gps = (mask & GpsText::GNSS_GPS) != 0;
  const bool glo = (mask & GpsText::GNSS_GLO) != 0;
  const bool gal = (mask & GpsText::GNSS_GAL) != 0;
  const bool bds = (mask & GpsText::GNSS_BDS) != 0;
  const bool qzss = (mask & GpsText::GNSS_QZSS) != 0;
  const std::string body = "PAIR066," + std::to_string(gps) + "," + std::to_string(glo) + ","
                           + std::to_string(gal) + "," + std::to_string(bds) + ","
                           + std::to_string(qzss) + ",0";
  return ack(ID_SEARCH_MODE, GpsText::Op::GNSS, body);
}

Command sbasEnable(bool enable) {
  return ack(ID_SBAS, GpsText::Op::SBAS, std::string("PAIR410,") + (enable ? "1" : "0"));
}

Command navMode(uint8_t mode) {
  return ack(ID_NAV_MODE, GpsText::Op::NAV_MODE, "PAIR080," + std::to_string(mode));
}

Command baud(uint32_t rate) {
  if (!baudListed(rate)) {
    return Command();
  }
  // Port type 0 is UART and port index 0 is the module's command port, which
  // both documents spell UART0 or UART1 and both number 0 (Q76 2.3.56 page 78,
  // Q29 2.4.53 page 100).
  return ack(ID_BAUD, GpsText::Op::BAUD, "PAIR864,0,0," + std::to_string(rate));
}

Command lockSleep(void) {
  return ack(ID_LOCK_SLEEP, GpsText::Op::POWER, "PAIR382,1");
}

Command powerOff(void) {
  return ack(ID_POWER_OFF, GpsText::Op::POWER, "PAIR003");
}

Command powerOn(void) {
  return ack(ID_POWER_ON, GpsText::Op::POWER, "PAIR002");
}

Command powerBackOn(const std::string &model) {
  // LC86G answers nothing to PAIR002 (Q76 2.3.2 note page 34), so a hot start
  // is the documented way back for it. Every other part, and a model furble
  // could not read, takes the power-on or, when unknown, the hot start that all
  // parts document.
  if ((model.rfind("LC86G", 0) == 0) || model.empty() || (model == "error")) {
    return restart(0);
  }
  return powerOn();
}

Command saveSettings(void) {
  return ack(ID_SAVE_SETTINGS, GpsText::Op::SAVE, "PAIR513");
}

Command saveParameters(void) {
  return pqtm(GpsText::Op::SAVE, "PQTMSAVEPAR", "");
}

Command saveNavigation(void) {
  return ack(ID_SAVE_NAV, GpsText::Op::SAVE, "PAIR511");
}

bool standby(uint32_t seconds, Command &out) {
  if ((seconds != 0) && ((seconds < BACKUP_MIN_S) || (seconds > BACKUP_MAX_S))) {
    return false;
  }
  out = ack(ID_BACKUP, GpsText::Op::STANDBY, "PAIR650," + std::to_string(seconds));
  return true;
}

Command restart(uint8_t mode) {
  switch (mode) {
    case 0:
      return ack(ID_HOT_START, GpsText::Op::RESET, "PAIR004");
    case 1:
      return ack(ID_WARM_START, GpsText::Op::RESET, "PAIR005");
    default:
      return ack(ID_COLD_START, GpsText::Op::RESET, "PAIR006");
  }
}

Plan plan(const GpsText::Request &request) {
  Plan plan;

  if (request.sentences_set) {
    plan.sentences = static_cast<uint8_t>(request.sentences);
    plan.sentences_set = true;
    for (const uint8_t type : SENTENCE_TYPES) {
      // A rate of 1 or 0 only: LC29H (BA, CA, DA, EA) accepts no other value
      // (Q29 2.4.13 note 2 page 61), and furble never asks for a divisor.
      plan.commands.push_back(sentenceRate(type, sentenceEnabled(request.sentences, type) ? 1 : 0));
    }
  }

  if (request.rate_ms != 0) {
    if ((request.rate_ms < FIX_RATE_MIN_MS) || (request.rate_ms > FIX_RATE_MAX_MS)) {
      plan.rate_refused = true;
    } else {
      plan.rate_ms = request.rate_ms;
      plan.commands.push_back(fixRate(request.rate_ms));
    }
  }

  if (request.platform != 0) {
    // PAIR080 is 0 normal, 1 fitness, 2 to 4 reserved, 5 drone, 6 reserved and
    // 7 swimming (Q76 2.3.23 pages 50 and 51). Portable is normal and
    // Pedestrian is fitness. Stationary and Vehicle have no documented
    // equivalent, so the receiver is left alone rather than given a mode that
    // means something else.
    if (request.platform == 1) {
      plan.commands.push_back(navMode(0));
    } else if (request.platform == 3) {
      plan.nav_mode = 1;
      plan.commands.push_back(navMode(1));
    } else {
      plan.platform_refused = true;
    }
  }

  if (request.gnss_set != 0) {
    const uint8_t decided = static_cast<uint8_t>(request.gnss_set & GpsText::GNSS_ALL);
    const uint8_t mask = static_cast<uint8_t>(request.gnss & GpsText::GNSS_ALL);
    plan.gnss = mask;
    if ((decided & GpsText::GNSS_SBAS) != 0) {
      plan.sbas = (mask & GpsText::GNSS_SBAS) != 0;
      if (plan.sbas && (plan.nav_mode == 1)) {
        // "When the navigation mode is Fitness or Swimming mode, SBAS is not
        // supported" (Q76 2.3.34 note page 60)
        plan.sbas_refused = true;
        plan.sbas = false;
      } else {
        plan.commands.push_back(sbasEnable(plan.sbas));
      }
    }
    const uint8_t without_sbas = static_cast<uint8_t>(mask & ~GpsText::GNSS_SBAS);
    if ((decided & ~GpsText::GNSS_SBAS) != 0) {
      if (gnssSupported(without_sbas)) {
        // queued after the SBAS write: this one goes last
        plan.commands.push_back(searchMode(without_sbas));
      } else {
        plan.gnss_refused = true;
      }
    }
  } else {
    plan.gnss = request.gnss;
  }

  return plan;
}

}  // namespace Quectel
}  // namespace Furble

#endif  // FURBLE_GPS_TEXT
