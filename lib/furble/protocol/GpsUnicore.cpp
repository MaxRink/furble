#include "GpsUnicore.h"

#include <array>
#include <cstdio>

namespace Furble {
namespace Unicore {

namespace {

Command command(GpsText::Op op, const std::string &body, const std::string &ack) {
  Command entry;
  entry.dialect = GpsText::Dialect::UNICORE;
  entry.op = op;
  entry.body = body;
  entry.ack = ack;
  entry.ack_id = 0;
  return entry;
}

/** "$CFGSYS,h" plus the mask, lowercase as the document's own example prints it. */
std::string hexMask(uint32_t mask) {
  char text[16];
  std::snprintf(text, sizeof(text), "h%lx", static_cast<unsigned long>(mask));
  return text;
}

bool sentenceEnabled(GpsText::SentenceSet set, uint8_t id) {
  const bool fix = (id == MSG_GGA) || (id == MSG_RMC);
  const bool detail = (id == MSG_GSA) || (id == MSG_GSV);
  switch (set) {
    case GpsText::SentenceSet::PRUNE:
      return fix;
    case GpsText::SentenceSet::SATELLITE:
      return fix || detail;
    case GpsText::SentenceSet::STANDARD:
      return (id <= MSG_VTG);
  }
  return false;
}

constexpr std::array<uint8_t, 6> MESSAGE_IDS = {
    MSG_GGA, MSG_GLL, MSG_GSA, MSG_GSV, MSG_RMC, MSG_VTG,
};

}  // namespace

Command productInfo(void) {
  return command(GpsText::Op::PRODUCT_INFO, "PDTINFO", "PDTINFO");
}

Command navRate(uint16_t ms) {
  // measRate and navRate must be equal or the receiver answers FAIL (UC 3.3
  // note page 43). DRNavRate is only read by the integrated products, and an
  // empty last field is the document's own omitted-parameter form (UC 3.1,
  // page 24), so it is left empty rather than written as a number the receiver
  // would have to ignore.
  const std::string value = std::to_string(ms);
  return command(GpsText::Op::FIX_RATE, "CFGNAV," + value + "," + value + ",", "CFGNAV");
}

Command messageRate(uint8_t id, uint8_t rate) {
  return command(GpsText::Op::SENTENCES,
                 "CFGMSG," + std::to_string(CLASS_ALL_PORTS) + "," + std::to_string(id) + ","
                     + std::to_string(rate),
                 "CFGMSG");
}

uint32_t sysMask(uint8_t mask) {
  uint32_t out = 0;
  if ((mask & GpsText::GNSS_GPS) != 0) {
    out |= SYS_GPS_L1CA;
  }
  if ((mask & GpsText::GNSS_BDS) != 0) {
    out |= SYS_BDS_B1I;
  }
  if ((mask & GpsText::GNSS_GLO) != 0) {
    out |= SYS_GLO_L1;
  }
  if ((mask & GpsText::GNSS_GAL) != 0) {
    out |= SYS_GAL_E1;
  }
  if ((mask & GpsText::GNSS_QZSS) != 0) {
    out |= SYS_QZSS_L1CA;
  }
  if ((mask & GpsText::GNSS_SBAS) != 0) {
    out |= SYS_SBAS;
  }
  return out;
}

bool gnssSupported(uint8_t mask) {
  const uint8_t all = static_cast<uint8_t>(mask & GpsText::GNSS_ALL);
  // One system has to be on: a receiver with no constellation is not a state
  // furble can ask for.
  if (all == 0) {
    return false;
  }
  // "QZSS can function effectively only when GPS is enabled" (UC 3.3, page 46)
  if (((all & GpsText::GNSS_QZSS) != 0) && ((all & GpsText::GNSS_GPS) == 0)) {
    return false;
  }
  // "SBAS can function effectively only when GPS or BDS is enabled"
  if (((all & GpsText::GNSS_SBAS) != 0) && ((all & (GpsText::GNSS_GPS | GpsText::GNSS_BDS)) == 0)) {
    return false;
  }
  return true;
}

Command system(uint8_t mask) {
  if (!gnssSupported(mask)) {
    return Command();
  }
  return command(GpsText::Op::GNSS, "CFGSYS," + hexMask(sysMask(mask)), "CFGSYS");
}

bool port(uint32_t baud, Command &out) {
  if ((baud < BAUD_MIN) || (baud > BAUD_MAX)) {
    return false;
  }
  // inPro bit0 keeps the UNICORE command input on, outPro bit0 and bit1 are
  // UNICORE plus NMEA. "Do not disable the UNICORE input" (UC 3.3, page 33).
  out = command(GpsText::Op::BAUD, "CFGPRT,1,0," + std::to_string(baud) + ",1,3", "CFGPRT");
  return true;
}

Command save(void) {
  return command(GpsText::Op::SAVE, "CFGSAVE", "OK");
}

Command clear(void) {
  return command(GpsText::Op::SAVE, "CFGCLR", "OK");
}

Command restart(uint8_t mode) {
  uint32_t mask = CLEAR_HOT;
  if (mode == 1) {
    mask = CLEAR_WARM;
  } else if (mode >= 2) {
    mask = CLEAR_COLD;
  }
  // $RESET,0,h00 hot, h01 warm, h85 cold (UC 3.2, page 29). A zero clear mask
  // is the hot start the table's H00 example prints.
  char text[24];
  std::snprintf(text, sizeof(text), "RESET,0,h%02lx", static_cast<unsigned long>(mask));
  return command(GpsText::Op::RESET, text, "OK");
}

Plan plan(const GpsText::Request &request) {
  Plan plan;

  if (request.sentences_set) {
    plan.sentences = static_cast<uint8_t>(request.sentences);
    plan.sentences_set = true;
    for (const uint8_t id : MESSAGE_IDS) {
      plan.commands.push_back(messageRate(id, sentenceEnabled(request.sentences, id) ? 1 : 0));
    }
  }

  if (request.rate_ms != 0) {
    if ((request.rate_ms < NAV_RATE_MIN_MS) || (request.rate_ms > NAV_RATE_MAX_MS)) {
      plan.rate_refused = true;
    } else {
      plan.rate_ms = request.rate_ms;
      plan.commands.push_back(navRate(request.rate_ms));
    }
  }

  if (request.platform != 0) {
    plan.platform_refused = true;
  }

  if (request.psm != GpsText::PSM_OFF) {
    plan.psm_refused = true;
  }

  if (request.gnss_set != 0) {
    const uint8_t mask = static_cast<uint8_t>(request.gnss & GpsText::GNSS_ALL);
    plan.gnss = mask;
    if (gnssSupported(mask)) {
      plan.sys_mask = sysMask(mask);
      // the receiver resets after this command (UC 3.3, page 44), so it is last
      plan.commands.push_back(system(mask));
    } else {
      plan.gnss_refused = true;
    }
  } else {
    plan.gnss = request.gnss;
  }

  return plan;
}

}  // namespace Unicore
}  // namespace Furble
