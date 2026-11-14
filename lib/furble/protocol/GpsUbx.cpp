#include "GpsUbx.h"

#include <algorithm>
#include <cstring>
#include <initializer_list>

namespace Furble {
namespace Ubx {

namespace {

uint16_t readU2(const uint8_t *data) {
  return static_cast<uint16_t>(data[0] | (data[1] << 8));
}

uint32_t readU4(const uint8_t *data) {
  return static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8)
         | (static_cast<uint32_t>(data[2]) << 16) | (static_cast<uint32_t>(data[3]) << 24);
}

int32_t readI4(const uint8_t *data) {
  return static_cast<int32_t>(readU4(data));
}

void putU(std::vector<uint8_t> &out, uint64_t value, size_t bytes) {
  for (size_t i = 0; i < bytes; i++) {
    out.push_back(static_cast<uint8_t>((value >> (8 * i)) & 0xff));
  }
}

void setU(std::vector<uint8_t> &out, size_t offset, uint64_t value, size_t bytes) {
  for (size_t i = 0; i < bytes; i++) {
    out[offset + i] = static_cast<uint8_t>((value >> (8 * i)) & 0xff);
  }
}

/** A fixed-width, NUL-padded character field, cut at its first NUL. */
std::string fixedString(const uint8_t *data, size_t width) {
  const auto *begin = reinterpret_cast<const char *>(data);
  const auto *end = static_cast<const char *>(std::memchr(begin, '\0', width));
  return std::string(begin, (end == nullptr) ? width : static_cast<size_t>(end - begin));
}

void putFixedString(std::vector<uint8_t> &out, const std::string &text, size_t width) {
  for (size_t i = 0; i < width; i++) {
    out.push_back((i < text.size()) ? static_cast<uint8_t>(text[i]) : 0);
  }
}

/** Walk key/value pairs from cfgData. False on a reserved size or a short tail. */
bool parseItems(const uint8_t *data, size_t length, std::vector<KeyValue> &out) {
  size_t offset = 0;
  while (offset < length) {
    if ((length - offset) < 4) {
      return false;
    }
    const uint32_t key = readU4(data + offset);
    const size_t size = valueSize(key);
    offset += 4;
    if ((size == 0) || ((length - offset) < size)) {
      return false;
    }
    uint64_t value = 0;
    for (size_t i = 0; i < size; i++) {
      value |= static_cast<uint64_t>(data[offset + i]) << (8 * i);
    }
    offset += size;
    if (out.size() >= MAX_KEYS) {
      return false;
    }
    out.push_back({key, value});
  }
  return true;
}

void putItems(std::vector<uint8_t> &out, const std::vector<KeyValue> &items) {
  for (const auto &item : items) {
    putU(out, item.key, 4);
    putU(out, item.value, valueSize(item.key));
  }
}

bool itemsValid(const std::vector<KeyValue> &items) {
  return (items.size() <= MAX_KEYS)
         && std::all_of(items.begin(), items.end(),
                        [](const KeyValue &item) { return valueSize(item.key) != 0; });
}

}  // namespace

size_t valueSize(uint32_t key) {
  // IFD 4.2 "Configuration items": the size field is bits 28 to 30.
  switch ((key >> 28) & 0x07) {
    case 0x01:  // one bit, carried in one byte
    case 0x02:
      return 1;
    case 0x03:
      return 2;
    case 0x04:
      return 4;
    case 0x05:
      return 8;
    default:
      return 0;
  }
}

std::array<uint8_t, 2> checksum(const uint8_t *data, size_t length) {
  uint8_t a = 0;
  uint8_t b = 0;
  for (size_t i = 0; i < length; i++) {
    a = static_cast<uint8_t>(a + data[i]);
    b = static_cast<uint8_t>(b + a);
  }
  return {a, b};
}

std::vector<uint8_t> frame(uint8_t cls, uint8_t id, const std::vector<uint8_t> &payload) {
  if (payload.size() > 0xffff) {
    return {};
  }
  std::vector<uint8_t> out;
  out.reserve(FRAME_OVERHEAD + payload.size());
  out.push_back(SYNC_1);
  out.push_back(SYNC_2);
  out.push_back(cls);
  out.push_back(id);
  putU(out, payload.size(), 2);
  out.insert(out.end(), payload.begin(), payload.end());
  const auto sum = checksum(out.data() + 2, out.size() - 2);
  out.push_back(sum[0]);
  out.push_back(sum[1]);
  return out;
}

ScanResult scan(const uint8_t *data, size_t length) {
  if ((length >= 1) && (data[0] != SYNC_1)) {
    return {Scan::BAD, 1};
  }
  if ((length >= 2) && (data[1] != SYNC_2)) {
    return {Scan::BAD, 1};
  }
  if (length < HEADER_SIZE) {
    return {Scan::NEED_MORE, 0};
  }
  const uint16_t payload = readU2(data + 4);
  if (payload > MAX_PAYLOAD) {
    return {Scan::BAD, 1};
  }
  const size_t total = FRAME_OVERHEAD + payload;
  if (length < total) {
    return {Scan::NEED_MORE, 0};
  }
  const auto sum = checksum(data + 2, HEADER_SIZE - 2 + payload);
  if ((sum[0] != data[total - 2]) || (sum[1] != data[total - 1])) {
    return {Scan::BAD, 1};
  }
  return {Scan::FRAME, total};
}

bool Stream::push(uint8_t byte) {
  m_Buffer.push_back(byte);
  while (!m_Buffer.empty()) {
    const ScanResult result = scan(m_Buffer.data(), m_Buffer.size());
    if (result.status == Scan::NEED_MORE) {
      return false;
    }
    if (result.status == Scan::FRAME) {
      m_Frame.assign(m_Buffer.begin(), m_Buffer.begin() + result.length);
      m_Buffer.erase(m_Buffer.begin(), m_Buffer.begin() + result.length);
      return true;
    }
    // A rejected header that had a full sync pair was a real attempt.
    if ((m_Buffer.size() >= HEADER_SIZE) && (m_Buffer[0] == SYNC_1) && (m_Buffer[1] == SYNC_2)) {
      m_Rejected++;
    }
    m_Buffer.erase(m_Buffer.begin());
  }
  return false;
}

void Stream::clear(void) {
  m_Buffer.clear();
  m_Frame.clear();
}

bool parseAck(uint8_t msg_id, const uint8_t *payload, size_t length, Ack &out) {
  if ((length != 2) || ((msg_id != ID_ACK_ACK) && (msg_id != ID_ACK_NAK))) {
    return false;
  }
  out = {payload[0], payload[1], msg_id == ID_ACK_ACK};
  return true;
}

bool NavPvt::positionValid(void) const {
  return gnssFixOk() && !invalidLlh() && (fixType >= 2) && (fixType <= 4);
}

bool NavPvt::timeValid(void) const {
  return validDate() && validTime() && fullyResolved() && (year >= 1980) && (month >= 1)
         && (month <= 12) && (day >= 1) && (day <= 31) && (hour <= 23) && (min <= 59)
         && (sec <= 60);
}

bool parseNavPvt(const uint8_t *p, size_t length, NavPvt &out) {
  if (length != NavPvt::LENGTH) {
    return false;
  }
  out.iTOW = readU4(p + 0);
  out.year = readU2(p + 4);
  out.month = p[6];
  out.day = p[7];
  out.hour = p[8];
  out.min = p[9];
  out.sec = p[10];
  out.valid = p[11];
  out.tAcc = readU4(p + 12);
  out.nano = readI4(p + 16);
  out.fixType = p[20];
  out.flags = p[21];
  out.flags2 = p[22];
  out.numSV = p[23];
  out.lon = readI4(p + 24);
  out.lat = readI4(p + 28);
  out.height = readI4(p + 32);
  out.hMSL = readI4(p + 36);
  out.hAcc = readU4(p + 40);
  out.vAcc = readU4(p + 44);
  out.gSpeed = readI4(p + 60);
  out.headMot = readI4(p + 64);
  out.sAcc = readU4(p + 68);
  out.pDOP = readU2(p + 76);
  out.flags3 = readU2(p + 78);
  return true;
}

std::vector<uint8_t> encodeNavPvt(const NavPvt &pvt) {
  std::vector<uint8_t> p(NavPvt::LENGTH, 0);
  setU(p, 0, pvt.iTOW, 4);
  setU(p, 4, pvt.year, 2);
  p[6] = pvt.month;
  p[7] = pvt.day;
  p[8] = pvt.hour;
  p[9] = pvt.min;
  p[10] = pvt.sec;
  p[11] = pvt.valid;
  setU(p, 12, pvt.tAcc, 4);
  setU(p, 16, static_cast<uint32_t>(pvt.nano), 4);
  p[20] = pvt.fixType;
  p[21] = pvt.flags;
  p[22] = pvt.flags2;
  p[23] = pvt.numSV;
  setU(p, 24, static_cast<uint32_t>(pvt.lon), 4);
  setU(p, 28, static_cast<uint32_t>(pvt.lat), 4);
  setU(p, 32, static_cast<uint32_t>(pvt.height), 4);
  setU(p, 36, static_cast<uint32_t>(pvt.hMSL), 4);
  setU(p, 40, pvt.hAcc, 4);
  setU(p, 44, pvt.vAcc, 4);
  setU(p, 60, static_cast<uint32_t>(pvt.gSpeed), 4);
  setU(p, 64, static_cast<uint32_t>(pvt.headMot), 4);
  setU(p, 68, pvt.sAcc, 4);
  setU(p, 76, pvt.pDOP, 2);
  setU(p, 78, pvt.flags3, 2);
  return frame(CLASS_NAV, ID_NAV_PVT, p);
}

bool parseNavSat(const uint8_t *p, size_t length, std::vector<NavSatSv> &out) {
  out.clear();
  if ((length < 8) || (p[4] != 1)) {
    return false;
  }
  const size_t count = p[5];
  if (length != (8 + (12 * count))) {
    return false;
  }
  out.reserve(count);
  for (size_t i = 0; i < count; i++) {
    const uint8_t *sv = p + 8 + (12 * i);
    out.push_back({sv[0], sv[1], sv[2], static_cast<int8_t>(sv[3]),
                   static_cast<int16_t>(readU2(sv + 4)), readU4(sv + 8)});
  }
  return true;
}

std::vector<uint8_t> encodeNavSat(uint32_t iTOW, const std::vector<NavSatSv> &svs) {
  if (svs.size() > 0xff) {
    return {};
  }
  std::vector<uint8_t> p;
  putU(p, iTOW, 4);
  p.push_back(1);
  p.push_back(static_cast<uint8_t>(svs.size()));
  putU(p, 0, 2);
  for (const auto &sv : svs) {
    p.push_back(sv.gnssId);
    p.push_back(sv.svId);
    p.push_back(sv.cno);
    p.push_back(static_cast<uint8_t>(sv.elev));
    putU(p, static_cast<uint16_t>(sv.azim), 2);
    putU(p, 0, 2);  // prRes
    putU(p, sv.flags, 4);
  }
  return frame(CLASS_NAV, ID_NAV_SAT, p);
}

std::string MonVer::field(const char *key) const {
  const std::string prefix = std::string(key) + "=";
  for (const auto &extension : extensions) {
    if (extension.compare(0, prefix.size(), prefix) == 0) {
      return extension.substr(prefix.size());
    }
  }
  return {};
}

bool parseMonVer(const uint8_t *p, size_t length, MonVer &out) {
  if ((length < 40) || (((length - 40) % 30) != 0)) {
    return false;
  }
  out.sw = fixedString(p, 30);
  out.hw = fixedString(p + 30, 10);
  out.extensions.clear();
  for (size_t offset = 40; offset < length; offset += 30) {
    out.extensions.push_back(fixedString(p + offset, 30));
  }
  return true;
}

std::vector<uint8_t> encodeMonVer(const MonVer &ver) {
  std::vector<uint8_t> p;
  putFixedString(p, ver.sw, 30);
  putFixedString(p, ver.hw, 10);
  for (const auto &extension : ver.extensions) {
    putFixedString(p, extension, 30);
  }
  return frame(CLASS_MON, ID_MON_VER, p);
}

bool parseSos(const uint8_t *p, size_t length, Sos &out) {
  if ((length != 8) || ((p[0] != SOS_ACK) && (p[0] != SOS_RESTORED))) {
    return false;
  }
  out = {p[0], p[4]};
  return true;
}

bool parseValGet(const uint8_t *p, size_t length, ValGet &out) {
  out.items.clear();
  if ((length < 4) || (p[0] != 1)) {
    return false;
  }
  out.layer = p[1];
  out.position = readU2(p + 2);
  return parseItems(p + 4, length - 4, out.items);
}

std::vector<uint8_t> pollMonVer(void) {
  return frame(CLASS_MON, ID_MON_VER, {});
}

std::vector<uint8_t> valSet(uint8_t layers, const std::vector<KeyValue> &items) {
  if (items.empty() || !itemsValid(items) || ((layers & 0x07) == 0) || ((layers & ~0x07) != 0)) {
    return {};
  }
  std::vector<uint8_t> p = {0, layers, 0, 0};
  putItems(p, items);
  return frame(CLASS_CFG, ID_CFG_VALSET, p);
}

std::vector<uint8_t> valGet(uint8_t layer, const std::vector<uint32_t> &keys) {
  if (keys.empty() || (keys.size() > MAX_KEYS)
      || ((layer != GET_RAM) && (layer != GET_BBR) && (layer != GET_FLASH)
          && (layer != GET_DEFAULT))) {
    return {};
  }
  std::vector<uint8_t> p = {0, layer, 0, 0};
  for (const uint32_t key : keys) {
    putU(p, key, 4);
  }
  return frame(CLASS_CFG, ID_CFG_VALGET, p);
}

std::vector<uint8_t> valDel(uint8_t layers, const std::vector<uint32_t> &keys) {
  // Only BBR and Flash can be deleted from, IFD 3.10.3.
  if (keys.empty() || (keys.size() > MAX_KEYS) || ((layers & 0x06) == 0)
      || ((layers & ~0x06) != 0)) {
    return {};
  }
  std::vector<uint8_t> p = {0, layers, 0, 0};
  for (const uint32_t key : keys) {
    putU(p, key, 4);
  }
  return frame(CLASS_CFG, ID_CFG_VALDEL, p);
}

std::vector<uint8_t> valGetResponse(uint8_t layer, const std::vector<KeyValue> &items) {
  if (!itemsValid(items)) {
    return {};
  }
  std::vector<uint8_t> p = {1, layer, 0, 0};
  putItems(p, items);
  return frame(CLASS_CFG, ID_CFG_VALGET, p);
}

std::vector<uint8_t> sosCommand(uint8_t command) {
  if ((command != SOS_CREATE) && (command != SOS_CLEAR)) {
    return {};
  }
  return frame(CLASS_UPD, ID_UPD_SOS, {command, 0, 0, 0});
}

std::vector<uint8_t> sosPoll(void) {
  return frame(CLASS_UPD, ID_UPD_SOS, {});
}

std::vector<uint8_t> cfgRst(uint16_t nav_bbr_mask, uint8_t reset_mode) {
  return frame(CLASS_CFG, ID_CFG_RST,
               {static_cast<uint8_t>(nav_bbr_mask & 0xff), static_cast<uint8_t>(nav_bbr_mask >> 8),
                reset_mode, 0});
}

std::vector<uint8_t> sosOutput(uint8_t command, uint8_t response) {
  return frame(CLASS_UPD, ID_UPD_SOS, {command, 0, 0, 0, response, 0, 0, 0});
}

std::vector<uint8_t> pmReq(const PmReq &request) {
  std::vector<uint8_t> p = {0, 0, 0, 0};
  putU(p, request.duration_ms, 4);
  putU(p, request.flags, 4);
  putU(p, request.wakeup_sources, 4);
  return frame(CLASS_RXM, ID_RXM_PMREQ, p);
}

bool parsePmReq(const uint8_t *p, size_t length, PmReq &out) {
  if ((length == 16) && (p[0] == 0)) {
    out = {readU4(p + 4), readU4(p + 8), readU4(p + 12)};
    return true;
  }
  return false;
}

bool parseValSet(const uint8_t *p, size_t length, uint8_t &layers, std::vector<KeyValue> &items) {
  items.clear();
  // Version 0 only: furble never sends a transaction, so neither does the model.
  if ((length < 4) || (p[0] != 0)) {
    return false;
  }
  layers = p[1];
  return parseItems(p + 4, length - 4, items);
}

bool parseKeyList(const uint8_t *p, size_t length, uint8_t &layer, std::vector<uint32_t> &keys) {
  keys.clear();
  if ((length < 4) || (p[0] != 0) || (((length - 4) % 4) != 0) || (((length - 4) / 4) > MAX_KEYS)) {
    return false;
  }
  layer = p[1];
  for (size_t offset = 4; offset < length; offset += 4) {
    keys.push_back(readU4(p + offset));
  }
  return true;
}

uint8_t gnssFromConstellation(uint8_t constellation) {
  uint8_t mask = constellation & (GNSS_MASK_GPS | GNSS_MASK_BDS | GNSS_MASK_GLO);
  if ((mask & GNSS_MASK_GPS) != 0) {
    mask |= GNSS_MASK_QZSS | GNSS_MASK_SBAS;
  }
  return mask;
}

bool gnssValid(uint8_t mask) {
  constexpr uint8_t MAJOR = GNSS_MASK_GPS | GNSS_MASK_BDS | GNSS_MASK_GLO | GNSS_MASK_GAL;
  if (((mask & MAJOR) == 0) || ((mask & ~GNSS_MASK_ALL) != 0)) {
    return false;
  }
  // SBAS and QZSS work only with GPS (DS-MAX 1.3, IM-MAX 2.1.3).
  if (((mask & (GNSS_MASK_QZSS | GNSS_MASK_SBAS)) != 0) && ((mask & GNSS_MASK_GPS) == 0)) {
    return false;
  }
  return true;
}

uint8_t maxRateHz(uint8_t mask) {
  const bool bds = (mask & GNSS_MASK_BDS) != 0;
  const bool glo = (mask & GNSS_MASK_GLO) != 0;
  const int majors = ((mask & GNSS_MASK_GPS) != 0) + ((mask & GNSS_MASK_GAL) != 0) + bds + glo;
  // DS-MAX and DS-MIA Table 2 and 3: single GNSS 18 Hz, GPS+GAL 10 Hz,
  // GPS+GAL+GLO 6 Hz, GPS+GAL+B1C 8 Hz, four GNSS 4 Hz, the B1I default set
  // 3 Hz. Pairs other than GPS+GAL are not listed; they take the pair figure.
  if (majors <= 1) {
    return 18;
  }
  if (bds && !glo) {
    return 3;  // B1I, the costly BeiDou signal
  }
  if (majors == 2) {
    return 10;
  }
  if (majors == 3) {
    return glo ? 6 : 8;
  }
  return 4;
}

uint8_t dynModelForPlatform(uint8_t platform) {
  // IFD Table 23: 0 portable, 2 stationary, 3 pedestrian, 4 automotive.
  static constexpr uint8_t MODELS[] = {0xff, 0, 2, 3, 4};
  return (platform < sizeof(MODELS)) ? MODELS[platform] : 0xff;
}

Plan plan(const Request &request) {
  Plan out = {};
  // Bits the request leaves alone keep the module preset (DS 1.3).
  const uint8_t gnss = static_cast<uint8_t>((request.gnss & request.gnss_set)
                                            | (GNSS_MASK_MODULE_DEFAULT & ~request.gnss_set));
  out.gnss = gnss;
  out.gnss_refused = (request.gnss_set != 0) && !gnssValid(gnss);
  const uint8_t effective = out.gnss_refused ? GNSS_MASK_MODULE_DEFAULT : gnss;
  const bool bds = (effective & GNSS_MASK_BDS) != 0;
  const bool glo = (effective & GNSS_MASK_GLO) != 0;
  const bool b1c = bds && glo;

  uint8_t psm = (request.psm <= PSM_ON_OFF) ? request.psm : PSM_OFF;
  // Power save with BeiDou B1C is refused (RN 7.2, IM-MAX 3.6.2).
  if ((psm != PSM_OFF) && b1c) {
    out.psm_refused = true;
    psm = PSM_OFF;
  }
  out.psm = psm;

  uint16_t rate = request.rate_ms;
  if (rate != 0) {
    const uint16_t floor =
        static_cast<uint16_t>((1000 + maxRateHz(effective) - 1) / maxRateHz(effective));
    if (rate < floor) {
      rate = floor;
      out.rate_clamped = true;
    }
  }
  if (psm == PSM_CYCLIC) {
    // PSMCT runs at 1 Hz or 2 Hz (IM-MAX 3.6.2.3).
    rate = ((rate != 0) && (rate <= 500)) ? 500 : 1000;
  }
  out.rate_ms = rate;

  out.base.push_back({Key::MSGOUT_NAV_PVT_UART1, 1});
  out.base.push_back({Key::ANA_USE_ANA, request.ana ? 1u : 0u});
  if (rate != 0) {
    out.base.push_back({Key::RATE_MEAS, rate});
  } else {
    out.unset.push_back(Key::RATE_MEAS);
  }
  if (request.dyn_model != 0xff) {
    out.base.push_back({Key::NAVSPG_DYNMODEL, request.dyn_model});
  } else {
    out.unset.push_back(Key::NAVSPG_DYNMODEL);
  }

  if (!out.gnss_refused) {
    const auto on = [gnss](uint8_t bit) { return (gnss & bit) ? 1u : 0u; };
    // Only the constellations the request decides are written. The others are
    // deleted from BBR, so they return to the receiver preset at its next
    // start instead of keeping what an earlier setting stored.
    const auto group = [&out, &request](uint8_t bits, std::initializer_list<KeyValue> items) {
      if ((request.gnss_set & bits) != 0) {
        out.signals.insert(out.signals.end(), items);
      } else {
        for (const auto &item : items) {
          out.unset.push_back(item.key);
        }
      }
    };
    group(GNSS_MASK_GPS, {
                             {Key::SIGNAL_GPS_ENA,  on(GNSS_MASK_GPS)},
                             {Key::SIGNAL_GPS_L1CA, 1                }
    });
    group(GNSS_MASK_GAL, {
                             {Key::SIGNAL_GAL_ENA, on(GNSS_MASK_GAL)},
                             {Key::SIGNAL_GAL_E1,  1                }
    });
    // BeiDou B1I does not run beside GLONASS, B1C does (IM-MAX 2.1.3). B1I is
    // the module preset and the signal AssistNow Autonomous covers, so it is
    // used whenever GLONASS is off.
    group(GNSS_MASK_BDS | GNSS_MASK_GLO, {
                                             {Key::SIGNAL_BDS_ENA, bds ? 1u : 0u          },
                                             {Key::SIGNAL_BDS_B1,  (bds && !glo) ? 1u : 0u},
                                             {Key::SIGNAL_BDS_B1C, b1c ? 1u : 0u          }
    });
    group(GNSS_MASK_GLO, {
                             {Key::SIGNAL_GLO_ENA, on(GNSS_MASK_GLO)},
                             {Key::SIGNAL_GLO_L1,  1                }
    });
    group(GNSS_MASK_QZSS, {
                              {Key::SIGNAL_QZSS_ENA,  on(GNSS_MASK_QZSS)},
                              {Key::SIGNAL_QZSS_L1CA, 1                 },
                              {Key::SIGNAL_QZSS_L1S,  on(GNSS_MASK_QZSS)}
    });
    group(GNSS_MASK_SBAS,
          {
              {Key::SIGNAL_SBAS_ENA,  on(GNSS_MASK_SBAS)},
              {Key::SIGNAL_SBAS_L1CA, 1                 }
    });
  }

  if (psm == PSM_ON_OFF) {
    // PSMOO update period at least 5 s (IFD 4.9.15).
    out.power.push_back({Key::PM_POSUPDATEPERIOD, std::max<uint32_t>(5, request.period_s)});
  } else {
    out.unset.push_back(Key::PM_POSUPDATEPERIOD);
  }
  // OPERATEMODE last; FULL is written explicitly so a power save mode a
  // previous session stored in BBR cannot survive the setting being cleared.
  out.power.push_back({Key::PM_OPERATEMODE, (psm == PSM_CYCLIC)   ? 2u
                                            : (psm == PSM_ON_OFF) ? 1u
                                                                  : 0u});
  return out;
}

void Detect::begin(uint32_t now) {
  m_Result = Result::WAITING;
  m_Attempts = 1;
  m_Deadline = now + ATTEMPT_MS;
}

Detect::Result Detect::service(uint32_t now, bool &poll) {
  poll = false;
  if ((m_Result != Result::WAITING) || (static_cast<int32_t>(now - m_Deadline) < 0)) {
    return m_Result;
  }
  if (m_Attempts < ATTEMPTS) {
    m_Attempts++;
    m_Deadline = now + ATTEMPT_MS;
    poll = true;
    return m_Result;
  }
  m_Result = Result::OTHER;
  return m_Result;
}

}  // namespace Ubx
}  // namespace Furble
