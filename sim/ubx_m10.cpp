#include "ubx_m10.h"

#include <algorithm>
#include <cstring>

namespace Furble {
namespace Sim {

namespace {

using Ubx::Key::ANA_USE_ANA;
using Ubx::Key::MSGOUT_NAV_PVT_UART1;
using Ubx::Key::MSGOUT_NAV_SAT_UART1;
using Ubx::Key::NAVSPG_DYNMODEL;
using Ubx::Key::PM_OPERATEMODE;
using Ubx::Key::PM_POSUPDATEPERIOD;
using Ubx::Key::RATE_MEAS;

/*
 * Time to first fix, ms. DS-MAX and DS-MIA Table 2 for the module default
 * constellation set: cold 27 s, hot 1 s, AssistNow Autonomous 3 to 4 s. M10
 * publishes no warm start figure; 25 s is an assumption between the two that
 * no assertion depends on.
 */
constexpr uint32_t TTFF_COLD_MS = 27000;
constexpr uint32_t TTFF_WARM_MS = 25000;
constexpr uint32_t TTFF_ANA_MS = 4000;
constexpr uint32_t TTFF_HOT_MS = 1000;

/* Ephemeris stays usable for about four hours (plan 32 PR32d). Assumption. */
constexpr uint32_t EPHEMERIS_MS = 4u * 60u * 60u * 1000u;
/* AssistNow Autonomous data lasts about three days, IM-MAX 3.12.5.2. */
constexpr uint32_t ANA_MS = 3u * 24u * 60u * 60u * 1000u;

/*
 * Supply current in uA at 3.0 V, 1 Hz, module default set (DS-MAX and DS-MIA
 * 4.3): continuous tracking 9.5 / 10.5 mA, cyclic tracking 5 / 5.5 mA.
 * Software standby 46 uA at V_IO 3.3 V and hardware backup 28 uA on V_BCKP,
 * DS Table 17 / 18. All vendor claims. The PSMOO inactive state and a stopped
 * GNSS have no published figure: assumed equal to software standby and to
 * continuous tracking respectively.
 */
constexpr uint32_t STANDBY_UA = 46;
constexpr uint32_t BACKUP_UA = 28;

/** Firmware defaults, IFD appendix, with the module production preset on top. */
const std::map<uint32_t, uint64_t> &defaults(void) {
  static const std::map<uint32_t, uint64_t> table = {
      {RATE_MEAS,                   1000},
      {Ubx::Key::RATE_NAV,          1   },
      {NAVSPG_DYNMODEL,             0   },
      {Ubx::Key::SIGNAL_GPS_ENA,    1   },
      {Ubx::Key::SIGNAL_GPS_L1CA,   1   },
      {Ubx::Key::SIGNAL_SBAS_ENA,   1   },
      {Ubx::Key::SIGNAL_SBAS_L1CA,  1   },
      {Ubx::Key::SIGNAL_GAL_ENA,    1   },
      {Ubx::Key::SIGNAL_GAL_E1,     1   },
      // The modules ship GPS+GAL+BDS B1I+QZSS+SBAS (DS 1.3, IM-MAX 2.1.3),
      // not the firmware's BDS-off, B1C-on default.
      {Ubx::Key::SIGNAL_BDS_ENA,    1   },
      {Ubx::Key::SIGNAL_BDS_B1,     1   },
      {Ubx::Key::SIGNAL_BDS_B1C,    0   },
      {Ubx::Key::SIGNAL_QZSS_ENA,   1   },
      {Ubx::Key::SIGNAL_QZSS_L1CA,  1   },
      {Ubx::Key::SIGNAL_QZSS_L1S,   1   },
      {Ubx::Key::SIGNAL_GLO_ENA,    0   },
      {Ubx::Key::SIGNAL_GLO_L1,     1   },
      {PM_OPERATEMODE,              0   },
      {PM_POSUPDATEPERIOD,          10  },
      {Ubx::Key::PM_ACQPERIOD,      10  },
      {Ubx::Key::PM_ONTIME,         0   },
      {Ubx::Key::PM_DONOTENTEROFF,  0   },
      {Ubx::Key::PM_WAITTIMEFIX,    0   },
      {Ubx::Key::PM_UPDATEEPH,      1   },
      {ANA_USE_ANA,                 0   },
      {MSGOUT_NAV_PVT_UART1,        0   },
      {MSGOUT_NAV_SAT_UART1,        0   },
      {Ubx::Key::UART1OUTPROT_UBX,  1   },
      {Ubx::Key::UART1OUTPROT_NMEA, 1   },
  };
  return table;
}

std::vector<uint8_t> ack(uint8_t cls, uint8_t id, bool ok) {
  return Ubx::frame(Ubx::CLASS_ACK, ok ? Ubx::ID_ACK_ACK : Ubx::ID_ACK_NAK, {cls, id});
}

void append(std::vector<uint8_t> &out, const std::vector<uint8_t> &more) {
  out.insert(out.end(), more.begin(), more.end());
}

bool isMax(const char *module) {
  return std::strcmp(module, "MAX-M10S") == 0;
}

}  // namespace

void VirtualM10::reset(const Options &options, uint32_t now) {
  *this = VirtualM10();
  m_Options = options;
  m_AccountAt = now;
  m_Started = now;
  m_State = State::OFF;
  setPower(true, now);
}

const char *VirtualM10::stateName(State state) {
  switch (state) {
    case State::OFF:
      return "off";
    case State::ON:
      return "on";
    case State::STOPPED:
      return "stopped";
    case State::STANDBY:
      return "standby";
  }
  return "unknown";
}

uint64_t VirtualM10::valueIn(const std::map<uint32_t, uint64_t> &ram, uint32_t key) const {
  auto found = ram.find(key);
  if (found != ram.end()) {
    return found->second;
  }
  found = m_Bbr.find(key);
  if (found != m_Bbr.end()) {
    return found->second;
  }
  found = defaults().find(key);
  return (found != defaults().end()) ? found->second : 0;
}

bool VirtualM10::known(uint32_t key) const {
  return (defaults().count(key) != 0) && (key != m_Options.unknownKey);
}

uint64_t VirtualM10::value(uint32_t key) const {
  return valueIn(m_Ram, key);
}

bool VirtualM10::valid(const std::map<uint32_t, uint64_t> &ram) const {
  const auto on = [&](uint32_t key) { return valueIn(ram, key) != 0; };
  const bool gps = on(Ubx::Key::SIGNAL_GPS_ENA) && on(Ubx::Key::SIGNAL_GPS_L1CA);
  const bool gal = on(Ubx::Key::SIGNAL_GAL_ENA) && on(Ubx::Key::SIGNAL_GAL_E1);
  const bool bds = on(Ubx::Key::SIGNAL_BDS_ENA);
  const bool b1i = bds && on(Ubx::Key::SIGNAL_BDS_B1);
  const bool b1c = bds && on(Ubx::Key::SIGNAL_BDS_B1C);
  const bool glo = on(Ubx::Key::SIGNAL_GLO_ENA) && on(Ubx::Key::SIGNAL_GLO_L1);
  // At least one major GNSS (IFD 4.9.21). B1I excludes B1C and GLONASS
  // (DS-MAX 1.3 note 13, RN 4.3.6).
  if (!(gps || gal || b1i || b1c || glo) || (b1i && (b1c || glo))) {
    return false;
  }
  // CFG-RATE-MEAS minimum 25 ms (IFD 4.9.17).
  const uint64_t rate = valueIn(ram, RATE_MEAS);
  if (rate < 25) {
    return false;
  }
  // DYNMODEL 1 is undefined, 12 is the last constant (IFD Table 23).
  const uint64_t model = valueIn(ram, NAVSPG_DYNMODEL);
  if ((model == 1) || (model > 12)) {
    return false;
  }
  const uint64_t mode = valueIn(ram, PM_OPERATEMODE);
  if (mode > 2) {
    return false;
  }
  // Power save with BeiDou B1C is refused (RN 7.2, IM-MAX 3.6.2).
  if ((mode != 0) && b1c) {
    return false;
  }
  // PSMOO update period at least 5 s, 0 waits for an external event (IFD 4.9.15).
  const uint64_t period = valueIn(ram, PM_POSUPDATEPERIOD);
  if ((mode == 1) && (period != 0) && (period < 5)) {
    return false;
  }
  // PSMCT runs at 1 Hz, 2 Hz or 2 to 10 s in 1 s steps (IM-MAX 3.6.2.3).
  // Refusing other rates is an assumption: the manual says only "supported".
  if ((mode == 2) && (rate != 500) && (rate != 1000)
      && ((rate % 1000) != 0 || (rate < 2000) || (rate > 10000))) {
    return false;
  }
  return true;
}

void VirtualM10::account(uint32_t now) {
  m_ChargeMicroampMs += static_cast<uint64_t>(currentMicroamps()) * (now - m_AccountAt);
  m_AccountAt = now;
}

uint32_t VirtualM10::currentMicroamps(void) const {
  const bool max = isMax(m_Options.module);
  const uint32_t continuous = max ? 9500 : 10500;
  const uint32_t cyclic = max ? 5000 : 5500;
  switch (m_State) {
    case State::OFF:
      return m_Options.vbckp ? BACKUP_UA : 0;
    case State::STANDBY:
      return STANDBY_UA;
    case State::STOPPED:
      return continuous;
    case State::ON:
      break;
  }
  switch (value(PM_OPERATEMODE)) {
    case 2:
      return cyclic;
    case 1:
    {
      // On for about one epoch per update period, inactive otherwise.
      const uint32_t period = std::max<uint32_t>(1000, periodMs());
      return static_cast<uint32_t>((static_cast<uint64_t>(continuous) * 1000
                                    + static_cast<uint64_t>(STANDBY_UA) * (period - 1000))
                                   / period);
    }
    default:
      return continuous;
  }
}

uint32_t VirtualM10::averageMicroamps(uint32_t now) {
  account(now);
  const uint32_t elapsed = now - m_Started;
  return (elapsed == 0) ? currentMicroamps() : static_cast<uint32_t>(m_ChargeMicroampMs / elapsed);
}

void VirtualM10::start(uint32_t now, bool keepBbr) {
  account(now);
  m_State = State::ON;
  // Power up and a standby wake both load the configuration from BBR and the
  // defaults; RAM is not retained (IM-MAX 3.6.3.2).
  m_Ram.clear();
  m_Stream.clear();
  uint32_t ttff = TTFF_COLD_MS;
  m_StartName = "cold";
  if (m_Options.hot) {
    ttff = TTFF_HOT_MS;
    m_StartName = "hot";
    m_Options.hot = false;
  } else if (keepBbr && m_EverFixed && ((now - m_LastFix) < EPHEMERIS_MS)) {
    ttff = TTFF_HOT_MS;
    m_StartName = "hot";
  } else if (keepBbr && m_EverFixed && (value(ANA_USE_ANA) != 0) && ((now - m_LastFix) < ANA_MS)) {
    ttff = TTFF_ANA_MS;
    m_StartName = "assisted";
  } else if (keepBbr && m_RtcValid) {
    ttff = TTFF_WARM_MS;
    m_StartName = "warm";
  }
  if (ttff == TTFF_COLD_MS) {
    coldStarts++;
  } else if (ttff == TTFF_WARM_MS) {
    warmStarts++;
  } else {
    hotStarts++;
  }
  m_FixAt = now + ttff;
}

void VirtualM10::setPower(bool on, uint32_t now) {
  account(now);
  if (!on) {
    m_State = State::OFF;
    if (!m_Options.vbckp) {
      // No backup supply: BBR and the RTC go with V_IO (IM-MAX 4.1.2).
      m_Bbr.clear();
      m_EverFixed = false;
      m_RtcValid = false;
    }
    return;
  }
  if (m_State != State::OFF) {
    return;
  }
  bool keep = m_Options.vbckp;
  m_Restored = Ubx::SOS_RESTORE_NONE;
  if (!keep && m_HaveBackup) {
    // UPD-SOS restore from flash on start-up (IFD 3.19.1).
    m_Bbr.clear();
    for (size_t i = 0; i + 12 <= m_Backup.size(); i += 12) {
      uint32_t key = 0;
      uint64_t value = 0;
      std::memcpy(&key, m_Backup.data() + i, 4);
      std::memcpy(&value, m_Backup.data() + i + 4, 8);
      m_Bbr[key] = value;
    }
    m_HaveBackup = false;
    m_Restored = Ubx::SOS_RESTORE_OK;
    keep = true;
    // Assumption: the restored BBR carries the orbits and last position but
    // the RTC did not run while unpowered, so the start is warm, not hot.
    m_RtcValid = true;
  }
  start(now, keep);
}

void VirtualM10::restart(uint16_t mask, uint32_t now) {
  account(now);
  m_State = State::ON;
  if (mask == Ubx::RST_HOT) {
    m_FixAt = now + TTFF_HOT_MS;
    m_StartName = "hot";
    hotStarts++;
  } else if (mask == Ubx::RST_WARM) {
    m_FixAt = now + TTFF_WARM_MS;
    m_StartName = "warm";
    warmStarts++;
  } else {
    m_EverFixed = false;
    m_RtcValid = false;
    m_FixAt = now + TTFF_COLD_MS;
    m_StartName = "cold";
    coldStarts++;
  }
}

void VirtualM10::tick(uint32_t now) {
  if ((m_State == State::STANDBY) && !m_StandbyForever
      && (static_cast<int32_t>(now - m_StandbyUntil) >= 0)) {
    // V_IO stayed up, so BBR and the RTC survived the standby.
    start(now, true);
  }
}

bool VirtualM10::outputting(uint32_t now) {
  tick(now);
  return m_State == State::ON;
}

uint32_t VirtualM10::periodMs(void) const {
  if (value(PM_OPERATEMODE) == 1) {
    const uint64_t seconds = value(PM_POSUPDATEPERIOD);
    return static_cast<uint32_t>((seconds == 0 ? 10 : seconds) * 1000);
  }
  return static_cast<uint32_t>(value(RATE_MEAS));
}

bool VirtualM10::fixed(uint32_t now) const {
  return (m_State == State::ON) && (static_cast<int32_t>(now - m_FixAt) >= 0);
}

std::vector<uint8_t> VirtualM10::epoch(uint32_t now,
                                       uint16_t year,
                                       uint8_t month,
                                       uint8_t day,
                                       uint32_t secondOfDay) {
  std::vector<uint8_t> out;
  if (m_State != State::ON) {
    return out;
  }
  const bool fix = fixed(now);
  if (fix) {
    m_EverFixed = true;
    m_RtcValid = true;
    m_LastFix = now;
  }
  m_LastEpoch = now;
  m_Year = year;
  m_Month = month;
  m_Day = day;
  m_SecondOfDay = secondOfDay;

  if (value(MSGOUT_NAV_PVT_UART1) != 0) {
    Ubx::NavPvt pvt = {};
    pvt.iTOW = secondOfDay * 1000;
    pvt.year = year;
    pvt.month = month;
    pvt.day = day;
    pvt.hour = static_cast<uint8_t>((secondOfDay / 3600) % 24);
    pvt.min = static_cast<uint8_t>((secondOfDay / 60) % 60);
    pvt.sec = static_cast<uint8_t>(secondOfDay % 60);
    const uint64_t mode = value(PM_OPERATEMODE);
    // psmState: 0 not active, 3 tracking (IFD 3.15.11).
    const uint8_t psm = (mode == 0) ? 0 : 3;
    if (fix) {
      pvt.valid = 0x07;
      pvt.tAcc = 20;
      pvt.fixType = 3;
      pvt.flags = static_cast<uint8_t>(0x01 | (psm << 2));
      pvt.numSV = 12;
      // The same fix as the canned NMEA burst: 48 07.038 N, 11 31.000 E,
      // 545.4 m MSL, 22.678 kn.
      pvt.lat = 481173000;
      pvt.lon = 115166667;
      pvt.hMSL = 545400;
      pvt.height = 592300;
      pvt.hAcc = 1500;
      pvt.vAcc = 2500;
      pvt.gSpeed = 11666;
      pvt.sAcc = 400;
      pvt.pDOP = 120;
    } else {
      pvt.valid = m_RtcValid ? 0x03 : 0x00;
      pvt.tAcc = 0xffffffff;
      pvt.flags = static_cast<uint8_t>(psm << 2);
      pvt.hAcc = 0xffffffff;
      pvt.vAcc = 0xffffffff;
      pvt.pDOP = 9999;
    }
    append(out, Ubx::encodeNavPvt(pvt));
  }

  if (value(MSGOUT_NAV_SAT_UART1) != 0) {
    std::vector<Ubx::NavSatSv> svs;
    const auto add = [&](uint32_t enable, uint32_t signal, uint8_t gnss, uint8_t first, int count) {
      if ((value(enable) == 0) || (value(signal) == 0)) {
        return;
      }
      for (int i = 0; i < count; i++) {
        const uint8_t cno = static_cast<uint8_t>(fix ? 42 - (3 * i) : 0);
        const uint32_t flags = fix ? (0x08 | 0x07) : 0x01;
        svs.push_back({gnss, static_cast<uint8_t>(first + i), cno,
                       static_cast<int8_t>(20 + (10 * i)), static_cast<int16_t>(40 * i), flags});
      }
    };
    add(Ubx::Key::SIGNAL_GPS_ENA, Ubx::Key::SIGNAL_GPS_L1CA, Ubx::GNSS_GPS, 1, 4);
    add(Ubx::Key::SIGNAL_GAL_ENA, Ubx::Key::SIGNAL_GAL_E1, Ubx::GNSS_GALILEO, 3, 3);
    if (value(Ubx::Key::SIGNAL_BDS_ENA) != 0) {
      add(Ubx::Key::SIGNAL_BDS_ENA,
          value(Ubx::Key::SIGNAL_BDS_B1) ? Ubx::Key::SIGNAL_BDS_B1 : Ubx::Key::SIGNAL_BDS_B1C,
          Ubx::GNSS_BEIDOU, 11, 3);
    }
    add(Ubx::Key::SIGNAL_GLO_ENA, Ubx::Key::SIGNAL_GLO_L1, Ubx::GNSS_GLONASS, 5, 2);
    add(Ubx::Key::SIGNAL_QZSS_ENA, Ubx::Key::SIGNAL_QZSS_L1CA, Ubx::GNSS_QZSS, 2, 1);
    add(Ubx::Key::SIGNAL_SBAS_ENA, Ubx::Key::SIGNAL_SBAS_L1CA, Ubx::GNSS_SBAS, 123, 1);
    append(out, Ubx::encodeNavSat(secondOfDay * 1000, svs));
  }
  return out;
}

std::vector<uint8_t> VirtualM10::receive(const uint8_t *data, size_t length, uint32_t now) {
  std::vector<uint8_t> out;
  tick(now);
  for (size_t i = 0; i < length; i++) {
    if (m_State == State::OFF) {
      ignoredBytes++;
      continue;
    }
    if (m_State == State::STANDBY) {
      if (m_WakeOnUart) {
        // Assumption: the byte that wakes the receiver is not decoded.
        start(now, true);
      }
      ignoredBytes++;
      continue;
    }
    if (m_Stream.push(data[i])) {
      append(out, handle(m_Stream, now));
    }
  }
  return out;
}

std::vector<uint8_t> VirtualM10::handle(const Ubx::Stream &frame, uint32_t now) {
  const uint8_t cls = frame.cls();
  const uint8_t id = frame.id();
  const uint8_t *p = frame.payload();
  const size_t n = frame.payloadLength();
  std::vector<uint8_t> out;

  if ((cls == Ubx::CLASS_MON) && (id == Ubx::ID_MON_VER) && (n == 0)) {
    monVerPolls++;
    Ubx::MonVer ver;
    ver.sw = "ROM SPG 5.10 (7b202e)";
    ver.hw = "000A0000";
    ver.extensions = {"FWVER=SPG 5.10", "PROTVER=34.10", std::string("MOD=") + m_Options.module,
                      "GPS;GLO;GAL;BDS", "SBAS;QZSS"};
    return Ubx::encodeMonVer(ver);
  }

  if ((cls == Ubx::CLASS_CFG) && (id == Ubx::ID_CFG_VALSET)) {
    valsets++;
    uint8_t layers = 0;
    std::vector<Ubx::KeyValue> items;
    bool ok = Ubx::parseValSet(p, n, layers, items) && !items.empty() && ((layers & 0x07) != 0)
              && (((layers & Ubx::LAYER_FLASH) == 0) || m_Options.flash);
    for (const auto &item : items) {
      ok = ok && known(item.key);
    }
    std::map<uint32_t, uint64_t> candidate = m_Ram;
    for (const auto &item : items) {
      candidate[item.key] = item.value;
    }
    // Validity is checked only when the RAM layer is targeted (IFD 3.10.5).
    if (ok && ((layers & Ubx::LAYER_RAM) != 0)) {
      ok = valid(candidate);
    }
    if (!ok) {
      naks++;
      return ack(cls, id, false);
    }
    bool signals = false;
    if ((layers & Ubx::LAYER_RAM) != 0) {
      for (const auto &item : items) {
        signals = signals || (((item.key >> 16) & 0xff) == 0x31 && value(item.key) != item.value);
      }
      account(now);
      m_Ram = candidate;
    }
    if ((layers & Ubx::LAYER_BBR) != 0) {
      for (const auto &item : items) {
        m_Bbr[item.key] = item.value;
      }
    }
    if (signals) {
      // A signal change restarts the GNSS subsystem (IFD 4.9.21). Modelled as
      // a hot restart: ephemeris and time are kept.
      restart(Ubx::RST_HOT, now);
    }
    return ack(cls, id, true);
  }

  if ((cls == Ubx::CLASS_CFG) && ((id == Ubx::ID_CFG_VALGET) || (id == Ubx::ID_CFG_VALDEL))) {
    uint8_t layer = 0;
    std::vector<uint32_t> keys;
    bool ok = Ubx::parseKeyList(p, n, layer, keys) && !keys.empty();
    for (const uint32_t key : keys) {
      ok = ok && known(key);
    }
    if (id == Ubx::ID_CFG_VALDEL) {
      if (!ok || ((layer & Ubx::LAYER_BBR) == 0)) {
        naks++;
        return ack(cls, id, false);
      }
      for (const uint32_t key : keys) {
        m_Bbr.erase(key);
      }
      return ack(cls, id, true);
    }
    valgets++;
    if (!ok
        || ((layer != Ubx::GET_RAM) && (layer != Ubx::GET_BBR) && (layer != Ubx::GET_DEFAULT))) {
      naks++;
      return ack(cls, id, false);
    }
    std::vector<Ubx::KeyValue> items;
    for (const uint32_t key : keys) {
      if (layer == Ubx::GET_RAM) {
        items.push_back({key, value(key)});
      } else if (layer == Ubx::GET_DEFAULT) {
        items.push_back({key, defaults().at(key)});
      } else if (m_Bbr.count(key) != 0) {
        items.push_back({key, m_Bbr.at(key)});
      }
    }
    out = Ubx::valGetResponse(layer, items);
    append(out, ack(cls, id, true));
    return out;
  }

  if ((cls == Ubx::CLASS_CFG) && (id == Ubx::ID_CFG_RST) && (n == 4)) {
    resets++;
    const uint16_t mask = static_cast<uint16_t>(p[0] | (p[1] << 8));
    const uint8_t mode = p[2];
    account(now);
    if (mode == Ubx::RST_MODE_GNSS_STOP) {
      m_State = State::STOPPED;
    } else if (mode == Ubx::RST_MODE_GNSS_START) {
      restart(Ubx::RST_HOT, now);
    } else {
      // Every reset type restarts navigation; only a hardware reset would also
      // drop RAM, and furble only sends the GNSS-only reset.
      restart(mask, now);
    }
    return out;  // CFG-RST is never acknowledged (IFD 3.10.2)
  }

  if ((cls == Ubx::CLASS_RXM) && (id == Ubx::ID_RXM_PMREQ)) {
    Ubx::PmReq request = {};
    if (!Ubx::parsePmReq(p, n, request) || ((request.flags & Ubx::PMREQ_FLAG_FORCE) == 0)) {
      // The force flag is required for software standby (IM-MAX 3.6.3.2).
      return out;
    }
    pmreqs++;
    account(now);
    m_State = State::STANDBY;
    m_StandbyForever = request.duration_ms == 0;
    m_StandbyUntil = now + request.duration_ms;
    m_WakeOnUart = (request.wakeup_sources & Ubx::PMREQ_WAKE_UARTRX) != 0;
    return out;
  }

  if ((cls == Ubx::CLASS_UPD) && (id == Ubx::ID_UPD_SOS)) {
    if (n == 0) {
      sosPolls++;
      return Ubx::sosOutput(Ubx::SOS_RESTORED, m_Restored);
    }
    if ((n == 4) && (p[0] == Ubx::SOS_CREATE)) {
      sosCreates++;
      if (!m_Options.flash) {
        // No flash to write the backup to. The IFD does not say which answer
        // a flashless module gives; this models "not acknowledged".
        return Ubx::sosOutput(Ubx::SOS_ACK, 0);
      }
      m_Backup.clear();
      for (const auto &entry : m_Bbr) {
        uint8_t record[12];
        std::memcpy(record, &entry.first, 4);
        std::memcpy(record + 4, &entry.second, 8);
        m_Backup.insert(m_Backup.end(), record, record + 12);
      }
      m_HaveBackup = true;
      return Ubx::sosOutput(Ubx::SOS_ACK, 1);
    }
    if ((n == 4) && (p[0] == Ubx::SOS_CLEAR)) {
      m_HaveBackup = false;
    }
    return out;
  }

  if ((cls == Ubx::CLASS_NAV) && (n == 0) && ((id == Ubx::ID_NAV_PVT) || (id == Ubx::ID_NAV_SAT))) {
    // A poll answers with one copy whatever the periodic rate (IFD 3.5.2).
    std::map<uint32_t, uint64_t> saved = m_Ram;
    m_Ram[MSGOUT_NAV_PVT_UART1] = (id == Ubx::ID_NAV_PVT) ? 1 : 0;
    m_Ram[MSGOUT_NAV_SAT_UART1] = (id == Ubx::ID_NAV_SAT) ? 1 : 0;
    out = epoch(now, m_Year, m_Month, m_Day, m_SecondOfDay);
    m_Ram = saved;
    return out;
  }

  // Anything else is ignored, which is what a receiver does with an unknown
  // or unsupported input message.
  return out;
}

}  // namespace Sim
}  // namespace Furble
