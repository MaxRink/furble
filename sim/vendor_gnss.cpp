#include "vendor_gnss.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "protocol/GpsText.h"
#include "protocol/GpsUnicore.h"

namespace Furble {
namespace Sim {

namespace {

/*
 * Time to first fix after power-up, ms. The protocol documents state no TTFF;
 * the LC76G data sheet gives a 2 s hot start, which is the figure plan 366
 * quotes, so the model uses it. Assumption, and no scenario asserts on it.
 */
constexpr uint32_t TTFF_MS = 2000;

/** Split a decimal field, or fail. */
bool number(const std::string &text, uint32_t &out) {
  if (text.empty() || (text.size() > 10)) {
    return false;
  }
  uint32_t value = 0;
  for (const char c : text) {
    if ((c >= '0') && (c <= '9')) {
      value = (value * 10) + static_cast<uint32_t>(c - '0');
    } else {
      return false;
    }
  }
  out = value;
  return true;
}

/** Hex with the h or H prefix UC 1.3 page 4 defines, or plain decimal. */
bool uint32(const std::string &text, uint32_t &out) {
  if (text.empty()) {
    return false;
  }
  if ((text[0] == 'h') || (text[0] == 'H')) {
    if (text.size() > 9) {
      return false;
    }
    out = static_cast<uint32_t>(std::strtoul(text.c_str() + 1, nullptr, 16));
    return true;
  }
  return number(text, out);
}

std::vector<uint8_t> bytes(const std::string &sentence) {
  return std::vector<uint8_t>(sentence.begin(), sentence.end());
}

}  // namespace

/* -------------------------------------------------------------------------- */
/* Quectel                                                                    */
/* -------------------------------------------------------------------------- */

void VirtualQuectel::reset(const Options &options, uint32_t now) {
  *this = VirtualQuectel();
  m_Options = options;
  m_Settings = Settings();
  m_Flash = Settings();
  m_SleepForever = false;
  m_WakeAt = 0;
  m_FirstFixAt = now + TTFF_MS;
  setPower(true, now);
}

const char *VirtualQuectel::stateName(State state) {
  switch (state) {
    case State::OFF:
      return "off";
    case State::ON:
      return "on";
  }
  return "unknown";
}

void VirtualQuectel::setPower(bool on, uint32_t now) {
  if (on) {
    m_State = State::ON;
    if (m_EverPowered && !m_Saved && !m_Options.backup) {
      // A power-up after a cut with no backup domain and nothing saved comes
      // back at the defaults. "If RTC cannot be powered ... this command must be
      // sent every time the parameters are modified" (Q76 2.3.45 note 1, page
      // 68).
      m_Settings = Settings();
    }
    m_EverPowered = true;
    m_GnssOff = false;
    m_SleepForever = false;
    m_WakeAt = 0;
    m_FirstFixAt = now + TTFF_MS;
    return;
  }
  m_State = State::OFF;
  m_GnssOff = false;
  m_SleepForever = false;
  m_WakeAt = 0;
}

void VirtualQuectel::tick(uint32_t now) {
  if (!m_SleepForever && (m_WakeAt != 0) && (static_cast<int32_t>(now - m_WakeAt) >= 0)) {
    m_WakeAt = 0;
    m_FirstFixAt = now + TTFF_MS;
  }
}

bool VirtualQuectel::sleeping(uint32_t now) const {
  return (m_State == State::ON) && (m_SleepForever || (m_WakeAt != 0))
         && (m_SleepForever || (static_cast<int32_t>(now - m_WakeAt) < 0));
}

bool VirtualQuectel::outputting(uint32_t now) const {
  return (m_State == State::ON) && !m_GnssOff && !sleeping(now) && fixed(now);
}

bool VirtualQuectel::fixed(uint32_t now) const {
  return (m_FirstFixAt == 0) || (static_cast<int32_t>(now - m_FirstFixAt) >= 0);
}

std::vector<uint8_t> VirtualQuectel::ack(uint16_t id, uint8_t result) {
  char body[32];
  std::snprintf(body, sizeof(body), "PAIR001,%03u,%u", static_cast<unsigned>(id),
                static_cast<unsigned>(result));
  if (result == 0) {
    acks++;
  } else if (result == 3) {
    unsupported++;
    naks++;
  } else {
    naks++;
  }
  return bytes(GpsText::frame(body));
}

void VirtualQuectel::store(void) {
  m_Flash = m_Settings;
  m_Saved = true;
}

std::vector<uint8_t> VirtualQuectel::handle(const std::string &line, uint32_t now) {
  GpsText::Line parsed;
  if (!GpsText::parse(line.data(), line.size(), parsed)) {
    return {};
  }
  commands++;
  if (!parsed.checksum_ok) {
    // A PAIR command with a checksum error is a parameter error (Q76 2.3.1
    // page 32, result 4). A PQTM command answers ERROR, 1 (Q76 2.4.1 page 97).
    badChecksum++;
    if ((parsed.name.rfind("PAIR", 0) == 0) && (parsed.name.size() >= 6)) {
      const uint32_t id = static_cast<uint32_t>(std::strtoul(parsed.name.c_str() + 4, nullptr, 10));
      return ack(static_cast<uint16_t>(id), 4);
    }
    if (parsed.name.rfind("PQTM", 0) == 0) {
      return bytes(GpsText::frame(parsed.name + ",ERROR,1"));
    }
    return {};
  }

  if (parsed.name == "PQTMVERNO") {
    return bytes(GpsText::frame(std::string("PQTMVERNO,") + m_Options.version + ","
                                + m_Options.buildDate + "," + m_Options.buildTime));
  }
  if (parsed.name == "PQTMSAVEPAR") {
    store();
    saves++;
    return bytes(GpsText::frame("PQTMSAVEPAR,OK"));
  }
  if (parsed.name == "PQTMRESTOREPAR") {
    m_Settings = Settings();
    return bytes(GpsText::frame("PQTMRESTOREPAR,OK"));
  }
  if (parsed.name == "PQTMCFGMSGRATE") {
    // $PQTMCFGMSGRATE,W,PQTMEPE,<rate>,<ver> (Q76 2.4.1 page 96). Only PQTMEPE
    // is configurable, and a PQTM message needs its version.
    if ((parsed.fields.size() < 4) || (parsed.fields[0] != "W")
        || (parsed.fields[1] != "PQTMEPE")) {
      return bytes(GpsText::frame("PQTMCFGMSGRATE,ERROR,1"));
    }
    uint32_t rate = 0;
    if (!number(parsed.fields[2], rate) || (rate > 20)) {
      return bytes(GpsText::frame("PQTMCFGMSGRATE,ERROR,1"));
    }
    return bytes(GpsText::frame("PQTMCFGMSGRATE,OK"));
  }

  if (parsed.name.rfind("PAIR", 0) != 0 || parsed.name.size() < 6) {
    // $PDTINFO, $CFG* and an unknown $ sentence all mean nothing here, and an
    // unknown $PAIRxxx has no id to answer.
    unknown++;
    return {};
  }

  const uint32_t id = static_cast<uint32_t>(std::strtoul(parsed.name.c_str() + 4, nullptr, 10));
  const std::vector<std::string> &f = parsed.fields;

  switch (id) {
    case 2:  // PAIR002 power on
      m_GnssOff = false;
      powerOns++;
      m_FirstFixAt = now + TTFF_MS;
      return ack(2, 0);
    case 3:  // PAIR003 power off
      m_GnssOff = true;
      return ack(3, 0);
    case 4:
    case 5:
    case 6:  // PAIR004/005/006 restart
      restarts++;
      m_FirstFixAt = now + TTFF_MS;
      return ack(static_cast<uint16_t>(id), 0);
    case 50:
    {  // PAIR050 fix rate, 100 to 1000 ms (Q76 2.3.9 page 38)
      if (m_Options.noFixRate) {
        return ack(50, 3);
      }
      uint32_t rate = 0;
      if (f.empty() || !number(f[0], rate) || (rate < 100) || (rate > 1000)) {
        return ack(50, 4);
      }
      m_Settings.rate_ms = static_cast<uint16_t>(rate);
      return ack(50, 0);
    }
    case 62:
    {  // PAIR062 sentence rate, types -1 and 0 to 9
      if (f.size() < 2) {
        return ack(62, 4);
      }
      const bool reset = (f[0] == "-1");
      uint32_t type = 0;
      uint32_t rate = 0;
      if (!reset && !number(f[0], type)) {
        return ack(62, 4);
      }
      if (!number(f[1], rate) || (rate > 20)) {
        return ack(62, 4);
      }
      if (reset) {
        m_Settings = Settings();
        return ack(62, 0);
      }
      if (type > 9) {
        return ack(62, 4);
      }
      m_Settings.sentence[type] = static_cast<uint8_t>(rate);
      return ack(62, 0);
    }
    case 66:
    {  // PAIR066 search mode, six fields
      if (m_Options.noSearchMode) {
        return ack(66, 3);
      }
      if (f.size() < 6) {
        return ack(66, 4);
      }
      bool flags[6] = {false, false, false, false, false, false};
      for (size_t i = 0; i < 6; i++) {
        uint32_t value = 0;
        if (!number(f[i], value) || (value > 1)) {
          return ack(66, 4);
        }
        flags[i] = value != 0;
      }
      const uint8_t voted =
          static_cast<uint8_t>((flags[1] ? 1 : 0) + (flags[2] ? 1 : 0) + (flags[3] ? 1 : 0));
      // The documented combinations are GPS alone, one more system, or all
      // three (Q76 2.3.15 note 2 page 44).
      if (!flags[0] || ((voted != 0) && (voted != 1) && (voted != 3))) {
        return ack(66, 4);
      }
      m_Settings.gps = true;
      m_Settings.glonass = flags[1];
      m_Settings.galileo = flags[2];
      m_Settings.bds = flags[3];
      m_Settings.qzss = flags[4];
      m_FirstFixAt = now + TTFF_MS;
      return ack(66, 0);
    }
    case 80:
    {  // PAIR080 navigation mode: 0, 1, 5 or 7
      uint32_t mode = 0;
      if (f.empty() || !number(f[0], mode)
          || ((mode != 0) && (mode != 1) && (mode != 5) && (mode != 7))) {
        return ack(80, 4);
      }
      m_Settings.nav_mode = static_cast<uint8_t>(mode);
      return ack(80, 0);
    }
    case 382:  // PAIR382 lock system sleep
      return ack(382, 0);
    case 410:
    {  // PAIR410 SBAS enable
      uint32_t enable = 0;
      if (f.empty() || !number(f[0], enable) || (enable > 1)) {
        return ack(410, 4);
      }
      m_Settings.sbas = enable != 0;
      return ack(410, 0);
    }
    case 511:  // PAIR511 save navigation data
      saves++;
      store();
      return ack(511, 0);
    case 513:  // PAIR513 save settings
      saves++;
      store();
      return ack(513, 0);
    case 650:
    {  // PAIR650 backup mode, 0 or 10 s to two years
      uint32_t seconds = 0;
      if (f.empty() || !number(f[0], seconds)
          || ((seconds != 0) && ((seconds < 10) || (seconds > 62208000)))) {
        return ack(650, 4);
      }
      backups++;
      standbyS = seconds;
      m_SleepForever = (seconds == 0);
      m_WakeAt = (seconds == 0) ? 0 : (now + (seconds * 1000));
      return ack(650, 0);
    }
    case 864:
    {  // PAIR864,0,0,<baud>
      static constexpr uint32_t BAUDS[] = {9600,   19200,  38400,  57600,
                                           115200, 230400, 460800, 921600};
      uint32_t baud = 0;
      if (f.size() < 3) {
        return ack(864, 4);
      }
      if (!number(f[2], baud)) {
        return ack(864, 4);
      }
      bool listed = false;
      for (const uint32_t entry : BAUDS) {
        listed = listed || (entry == baud);
      }
      if (!listed) {
        return ack(864, 4);
      }
      m_Settings.baud = baud;
      return ack(864, 0);
    }
    default:
      // An id this part does not implement: result 3 (Q76 2.3.1 page 32).
      return ack(static_cast<uint16_t>(id), 3);
  }
}

std::vector<uint8_t> VirtualQuectel::receive(const uint8_t *data, size_t length, uint32_t now) {
  std::vector<uint8_t> out;
  if (m_State != State::ON) {
    return out;
  }
  for (size_t i = 0; i < length; i++) {
    const char c = static_cast<char>(data[i]);
    if ((c == '\r') || (c == '\n')) {
      if (!m_Partial.empty()) {
        const std::vector<uint8_t> reply = handle(m_Partial, now);
        out.insert(out.end(), reply.begin(), reply.end());
        m_Partial.clear();
      }
      continue;
    }
    if (c == '$') {
      // A new sentence starts here, so anything before it was noise: a real
      // receiver ignores the UBX identification poll furble sends to every
      // receiver, and that poll is twelve bytes with no line ending.
      m_Partial.clear();
    }
    if (m_Partial.size() >= GpsText::MAX_LINE) {
      // A real receiver drops a line it cannot hold. Keep it bounded.
      m_Partial.clear();
      continue;
    }
    m_Partial.push_back(c);
  }
  return out;
}

/* -------------------------------------------------------------------------- */
/* Unicore                                                                    */
/* -------------------------------------------------------------------------- */

void VirtualUnicore::reset(const Options &options, uint32_t now) {
  *this = VirtualUnicore();
  m_Options = options;
  m_Settings = Settings();
  m_Settings.sys =
      Unicore::SYS_GPS_L1CA | Unicore::SYS_GAL_E1 | Unicore::SYS_BDS_B1I | Unicore::SYS_QZSS_L1CA;
  m_Flash = m_Settings;
  setPower(true, now);
}

const char *VirtualUnicore::stateName(State state) {
  switch (state) {
    case State::OFF:
      return "off";
    case State::ON:
      return "on";
  }
  return "unknown";
}

void VirtualUnicore::setPower(bool on, uint32_t now) {
  if (on) {
    m_State = State::ON;
    if (m_EverPowered && !m_Saved) {
      // "A power off during this process may cause damage ... the configuration
      // will be restored to factory settings" (UC 3.3 page 47): anything not
      // saved comes back as the model's own defaults.
      m_Settings = Settings();
      m_Settings.sys = Unicore::SYS_GPS_L1CA | Unicore::SYS_GAL_E1 | Unicore::SYS_BDS_B1I
                       | Unicore::SYS_QZSS_L1CA;
    }
    m_EverPowered = true;
    m_FirstFixAt = now + TTFF_MS;
    return;
  }
  m_State = State::OFF;
}

void VirtualUnicore::tick(uint32_t) {}

bool VirtualUnicore::outputting(uint32_t now) const {
  return (m_State == State::ON) && fixed(now);
}

bool VirtualUnicore::fixed(uint32_t now) const {
  return (m_FirstFixAt == 0) || (static_cast<int32_t>(now - m_FirstFixAt) >= 0);
}

std::vector<uint8_t> VirtualUnicore::ok(void) {
  acks++;
  return bytes(GpsText::frame("OK"));
}

std::vector<uint8_t> VirtualUnicore::fail(uint8_t code) {
  fails++;
  return bytes(GpsText::frame("FAIL," + std::to_string(code)));
}

void VirtualUnicore::store(void) {
  m_Flash = m_Settings;
  m_Saved = true;
}

std::vector<uint8_t> VirtualUnicore::handle(const std::string &line, uint32_t now) {
  GpsText::Line parsed;
  if (!GpsText::parse(line.data(), line.size(), parsed)) {
    return {};
  }
  commands++;
  if (!parsed.checksum_ok) {
    badChecksum++;
    // "1 - Incorrect checksum" (UC 3.2 page 31)
    return fail(1);
  }
  if (m_Options.silent) {
    return {};
  }

  const std::vector<std::string> &f = parsed.fields;

  if (parsed.name == "PDTINFO") {
    return bytes(GpsText::frame(std::string("PDTINFO,") + m_Options.model + "," + m_Options.config
                                + "," + m_Options.hw + "," + m_Options.fw
                                + ",UC6580-WXYZ,PI10A2235000909"));
  }
  if (parsed.name == "RESET") {
    uint32_t type = 0;
    if (f.empty() || !number(f[0], type) || (type > 3)) {
      return fail(0);
    }
    resets++;
    m_FirstFixAt = now + TTFF_MS;
    return ok();
  }
  if (parsed.name == "CFGPRT") {
    if (f.size() < 5) {
      return fail(0);
    }
    uint32_t port = 0;
    uint32_t addr = 0;
    uint32_t baud = 0;
    uint32_t in = 0;
    uint32_t outPro = 0;
    if (!number(f[0], port) || !number(f[1], addr) || !number(f[2], baud) || !number(f[3], in)
        || !number(f[4], outPro)) {
      return fail(0);
    }
    if ((baud < Unicore::BAUD_MIN) || (baud > Unicore::BAUD_MAX)) {
      return fail(0);
    }
    if ((in & 0x1U) == 0) {
      // "Do not disable the UNICORE input" (UC 3.3 page 33)
      return fail(0);
    }
    m_Settings.baud = baud;
    return ok();
  }
  if (parsed.name == "CFGMSG") {
    if (f.size() < 3) {
      return fail(0);
    }
    uint32_t cls = 0;
    uint32_t id = 0;
    uint32_t rate = 0;
    if (!number(f[0], cls) || !number(f[1], id) || !number(f[2], rate)) {
      return fail(0);
    }
    if ((cls != 0) || (id > 8) || (rate > 100)) {
      return fail(0);
    }
    m_Settings.sentence[id] = static_cast<uint8_t>(rate);
    return ok();
  }
  if (parsed.name == "CFGNAV") {
    uint32_t meas = 0;
    uint32_t nav = 0;
    if (f.size() < 2 || !number(f[0], meas) || !number(f[1], nav)) {
      return fail(0);
    }
    // "measRate and navRate should be the same, otherwise, the message would
    // return FAIL" (UC 3.3 page 43)
    if ((meas != nav) || (nav < 50) || (nav > 1000)) {
      return fail(0);
    }
    m_Settings.rate_ms = static_cast<uint16_t>(nav);
    return ok();
  }
  if (parsed.name == "CFGSYS") {
    uint32_t mask = 0;
    if (f.empty() || !uint32(f[0], mask)) {
      return fail(0);
    }
    constexpr uint32_t ALLOWED =
        Unicore::SYS_GPS_L1CA | Unicore::SYS_GPS_L5 | Unicore::SYS_BDS_B1I | Unicore::SYS_BDS_B2A
        | Unicore::SYS_BDS_B1C | Unicore::SYS_GLO_L1 | Unicore::SYS_GAL_E1 | Unicore::SYS_GAL_E5A
        | Unicore::SYS_QZSS_L1CA | Unicore::SYS_QZSS_L5 | Unicore::SYS_SBAS;
    if ((mask == 0) || ((mask & ~ALLOWED) != 0)) {
      return fail(0);
    }
    // QZSS needs GPS and SBAS needs GPS or BDS (UC 3.3 page 46)
    if (((mask & (Unicore::SYS_QZSS_L1CA | Unicore::SYS_QZSS_L5)) != 0)
        && ((mask & Unicore::SYS_GPS_L1CA) == 0)) {
      return fail(0);
    }
    if (((mask & Unicore::SYS_SBAS) != 0)
        && ((mask & (Unicore::SYS_GPS_L1CA | Unicore::SYS_BDS_B1I)) == 0)) {
      return fail(0);
    }
    m_Settings.sys = mask;
    // "The receiver resets automatically after receiving the command"
    // (UC 3.3 page 44)
    resets++;
    m_FirstFixAt = now + TTFF_MS;
    return ok();
  }
  if (parsed.name == "CFGDYN") {
    // $CFGDYN,<mask>,<DynModel>,<StaticHoldThresh> (UC 3.3 Table 3-41,
    // page 58). Bit 0 selects DynModel, 0 is portable and 1 static.
    uint32_t mask = 0;
    uint32_t dyn = 0;
    uint32_t hold = 0;
    if ((f.size() < 3) || !number(f[0], mask) || !number(f[1], dyn) || !number(f[2], hold)) {
      return fail(0);
    }
    if (((mask & 0x1U) == 0) || (dyn > 1) || (hold > 65535)) {
      return fail(0);
    }
    m_Settings.dyn = static_cast<uint8_t>(dyn);
    m_Settings.static_hold = static_cast<uint16_t>(hold);
    return ok();
  }
  if (parsed.name == "CFGSAVE") {
    if (!m_Options.flash) {
      return fail(0);
    }
    saves++;
    store();
    return ok();
  }
  if (parsed.name == "CFGCLR") {
    m_Settings = Settings();
    m_Saved = false;
    return ok();
  }
  if (parsed.name == "CFGNMEA") {
    return bytes(GpsText::frame("CFGNMEA,H52"));
  }

  // "$FAIL ... No response to the illegal command" (UC 3.2 page 31)
  unknown++;
  return {};
}

std::vector<uint8_t> VirtualUnicore::receive(const uint8_t *data, size_t length, uint32_t now) {
  std::vector<uint8_t> out;
  if (m_State != State::ON) {
    return out;
  }
  for (size_t i = 0; i < length; i++) {
    const char c = static_cast<char>(data[i]);
    if ((c == '\r') || (c == '\n')) {
      if (!m_Partial.empty()) {
        const std::vector<uint8_t> reply = handle(m_Partial, now);
        out.insert(out.end(), reply.begin(), reply.end());
        m_Partial.clear();
      }
      continue;
    }
    if (c == '$') {
      // A new sentence starts here, so anything before it was noise: a real
      // receiver ignores the UBX identification poll furble sends to every
      // receiver, and that poll is twelve bytes with no line ending.
      m_Partial.clear();
    }
    if (m_Partial.size() >= GpsText::MAX_LINE) {
      m_Partial.clear();
      continue;
    }
    m_Partial.push_back(c);
  }
  return out;
}

}  // namespace Sim
}  // namespace Furble
