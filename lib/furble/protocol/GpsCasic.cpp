#include "GpsCasic.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>

namespace Furble {
namespace Casic {

namespace {

constexpr uint8_t SYNC_0 = 0xBA;
constexpr uint8_t SYNC_1 = 0xCE;
constexpr size_t HEADER_SIZE = 6;
constexpr size_t CHECKSUM_SIZE = 4;
constexpr size_t FRAME_OVERHEAD = HEADER_SIZE + CHECKSUM_SIZE;
constexpr uint16_t MAX_PAYLOAD = 2048;

uint16_t readU16(const uint8_t *data) {
  return static_cast<uint16_t>(data[0]) | (static_cast<uint16_t>(data[1]) << 8);
}

uint32_t readU32(const uint8_t *data) {
  return static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8)
         | (static_cast<uint32_t>(data[2]) << 16) | (static_cast<uint32_t>(data[3]) << 24);
}

/** Split a comma separated NMEA body, dropping the leading '$' and trailing checksum. */
std::vector<std::string> splitNmea(const std::string &sentence) {
  std::vector<std::string> fields;
  size_t begin = 0;
  if ((begin < sentence.size()) && (sentence[begin] == '$')) {
    begin++;
  }

  std::string current;
  for (size_t i = begin; i < sentence.size(); i++) {
    const char c = sentence[i];
    if (c == '*') {
      break;
    }
    if (c == ',') {
      fields.push_back(current);
      current.clear();
    } else if ((c != '\r') && (c != '\n')) {
      current += c;
    }
  }
  fields.push_back(current);
  return fields;
}

bool parseUint(const std::string &text, unsigned long &out) {
  if (text.empty()) {
    return false;
  }
  char *end = nullptr;
  const unsigned long value = std::strtoul(text.c_str(), &end, 10);
  if ((end == nullptr) || (*end != '\0')) {
    return false;
  }
  out = value;
  return true;
}

bool parseFloat(const std::string &text, float &out) {
  if (text.empty()) {
    return false;
  }
  char *end = nullptr;
  const float value = std::strtof(text.c_str(), &end);
  if ((end == nullptr) || (*end != '\0')) {
    return false;
  }
  if (!std::isfinite(value)) {
    return false;
  }
  out = value;
  return true;
}

int hexValue(char c) {
  if ((c >= '0') && (c <= '9')) {
    return c - '0';
  }
  if ((c >= 'A') && (c <= 'F')) {
    return c - 'A' + 10;
  }
  if ((c >= 'a') && (c <= 'f')) {
    return c - 'a' + 10;
  }
  return -1;
}

bool validNmeaChecksum(const std::string &sentence, size_t dollar) {
  if ((dollar >= sentence.size()) || (sentence[dollar] != '$')) {
    return false;
  }
  const size_t star = sentence.find('*', dollar + 1);
  if ((star == std::string::npos) || ((star - dollar) < 5) || (star + 3 > sentence.size())) {
    return false;
  }
  const int high = hexValue(sentence[star + 1]);
  const int low = hexValue(sentence[star + 2]);
  if ((high < 0) || (low < 0)) {
    return false;
  }

  uint8_t sum = 0;
  for (size_t i = dollar + 1; i < star; i++) {
    sum ^= static_cast<uint8_t>(sentence[i]);
  }
  if (sum != static_cast<uint8_t>((high << 4) | low)) {
    return false;
  }

  // The caller normally passes a line without its terminator, but accept the
  // CR/LF that captureSentences preserves and reject any other trailing data.
  for (size_t i = star + 3; i < sentence.size(); i++) {
    if ((sentence[i] != '\r') && (sentence[i] != '\n')) {
      return false;
    }
  }
  return true;
}

}  // namespace

uint32_t checksum(uint8_t class_id, uint8_t message_id, uint16_t length, const uint8_t *payload) {
  uint32_t sum =
      (static_cast<uint32_t>(message_id) << 24) | (static_cast<uint32_t>(class_id) << 16) | length;

  for (uint16_t offset = 0; offset < length; offset += 4) {
    uint32_t word = 0;
    for (uint16_t byte = 0; (byte < 4) && ((offset + byte) < length); byte++) {
      word |= static_cast<uint32_t>(payload[offset + byte]) << (byte * 8);
    }
    sum += word;
  }

  return sum;
}

std::vector<uint8_t> frame(uint8_t class_id,
                           uint8_t message_id,
                           const std::vector<uint8_t> &payload) {
  if (payload.size() > std::numeric_limits<uint16_t>::max()) {
    return {};
  }
  const uint16_t length = static_cast<uint16_t>(payload.size());
  std::vector<uint8_t> out;
  out.reserve(FRAME_OVERHEAD + length);
  out.push_back(SYNC_0);
  out.push_back(SYNC_1);
  out.push_back(static_cast<uint8_t>(length & 0xff));
  out.push_back(static_cast<uint8_t>((length >> 8) & 0xff));
  out.push_back(class_id);
  out.push_back(message_id);
  out.insert(out.end(), payload.begin(), payload.end());

  const uint32_t sum = checksum(class_id, message_id, length, payload.data());
  out.push_back(static_cast<uint8_t>(sum & 0xff));
  out.push_back(static_cast<uint8_t>((sum >> 8) & 0xff));
  out.push_back(static_cast<uint8_t>((sum >> 16) & 0xff));
  out.push_back(static_cast<uint8_t>((sum >> 24) & 0xff));
  return out;
}

Autobaud::Action Autobaud::begin(uint32_t now, uint32_t passed_checksum) {
  m_State = State::PROBING;
  m_Step = 0;
  m_Base = passed_checksum;
  m_Deadline = now + STEP_MS;
  return {true, LADDER[0]};
}

Autobaud::Action Autobaud::service(uint32_t now, uint32_t passed_checksum) {
  if (m_State != State::PROBING) {
    return {false, 0};
  }

  if ((passed_checksum - m_Base) >= REQUIRED_SENTENCES) {
    m_State = State::LOCKED;
    return {false, LADDER[m_Step]};
  }

  if (static_cast<int32_t>(now - m_Deadline) >= 0) {
    m_Step++;
    if (m_Step >= LADDER.size()) {
      m_State = State::NO_RECEIVER;
      return {false, 0};
    }
    m_Base = passed_checksum;
    m_Deadline = now + STEP_MS;
    return {true, LADDER[m_Step]};
  }

  return {false, 0};
}

uint8_t constellationForTalker(char a, char b) {
  if ((a == 'G') && (b == 'P')) {
    return CONSTELLATION_GPS;
  }
  if ((a == 'G') && (b == 'L')) {
    return CONSTELLATION_GLONASS;
  }
  if ((a == 'G') && (b == 'A')) {
    return CONSTELLATION_GALILEO;
  }
  // BeiDou is emitted as BD by older firmware and GB by newer.
  if (((a == 'B') && (b == 'D')) || ((a == 'G') && (b == 'B'))) {
    return CONSTELLATION_BEIDOU;
  }
  // QZSS appears as GQ, PQ or QZ across firmware revisions.
  if (((a == 'G') && (b == 'Q')) || ((a == 'P') && (b == 'Q')) || ((a == 'Q') && (b == 'Z'))) {
    return CONSTELLATION_QZSS;
  }
  return CONSTELLATION_UNKNOWN;
}

NmeaSatellites::TalkerSet &NmeaSatellites::setFor(uint8_t constellation) {
  for (auto &set : m_Sets) {
    if (set.talker_constellation == constellation) {
      return set;
    }
  }
  m_Sets.push_back(TalkerSet {});
  m_Sets.back().talker_constellation = constellation;
  return m_Sets.back();
}

bool NmeaSatellites::feedGsv(uint8_t constellation, const std::vector<std::string> &fields) {
  // $<T>GSV,total,index,inview,[prn,elev,az,cn0]x4,signalId*cs
  if (fields.size() < 4) {
    return true;
  }

  unsigned long total = 0;
  unsigned long index = 0;
  if (!parseUint(fields[1], total) || !parseUint(fields[2], index) || (total == 0) || (total > 8)
      || (index == 0) || (index > total)) {
    return true;
  }

  TalkerSet &set = setFor(constellation);
  if (index == 1) {
    set.total_sentences = static_cast<uint8_t>(total);
    set.seen_mask = 0;
    set.malformed = false;
    set.building.clear();
  }
  if (total != set.total_sentences) {
    // a set boundary was missed, restart cleanly on this sentence
    set.total_sentences = static_cast<uint8_t>(total);
    set.seen_mask = 0;
    set.malformed = false;
    set.building.clear();
  }

  // A repeated sentence number is a duplicate burst, not another set of
  // satellites. Ignore it until sentence 1 starts the next set; otherwise a
  // noisy receiver can grow the temporary vector without bound.
  if ((index > 1) && (set.seen_mask & static_cast<uint8_t>(1u << (index - 1)))) {
    return true;
  }

  for (size_t base = 4; (base + 3) < fields.size(); base += 4) {
    // stop at the trailing signalId field, which is not a satellite quad
    if ((base + 4) > fields.size()) {
      break;
    }
    const bool hasSatelliteFields = !fields[base].empty() || !fields[base + 1].empty()
                                    || !fields[base + 2].empty() || !fields[base + 3].empty();
    if (!hasSatelliteFields) {
      continue;
    }
    unsigned long prn = 0;
    if (!parseUint(fields[base], prn)) {
      set.malformed = true;
      continue;
    }
    if (prn > std::numeric_limits<uint16_t>::max()) {
      set.malformed = true;
      continue;
    }
    Satellite sat = {};
    sat.prn = static_cast<uint16_t>(prn);
    sat.constellation = constellation;
    unsigned long value = 0;
    if (parseUint(fields[base + 1], value)) {
      if (value > 90) {
        set.malformed = true;
        continue;
      }
      sat.elevation = static_cast<uint8_t>(value);
    }
    if (parseUint(fields[base + 2], value)) {
      if (value > 359) {
        set.malformed = true;
        continue;
      }
      sat.azimuth = static_cast<uint16_t>(value);
    }
    if (parseUint(fields[base + 3], value)) {
      // NMEA GSV defines C/N0 as 00 to 99 dB-Hz.
      if (value > 99) {
        set.malformed = true;
        continue;
      }
      sat.snr = static_cast<uint8_t>(value);
      sat.snr_valid = true;
    } else {
      sat.snr = 0;
      sat.snr_valid = false;
    }
    sat.used = false;
    // A receiver that repeats a PRN inside one set is not reporting two
    // satellites. Publishing it twice would also make the in-view count
    // disagree with the count the GSV header itself declared.
    const bool seen =
        std::find_if(set.building.begin(), set.building.end(),
                     [&sat](const Satellite &other) {
                       return (other.prn == sat.prn) && (other.constellation == sat.constellation);
                     })
        != set.building.end();
    if (!seen) {
      set.building.push_back(sat);
    }
  }

  if (index <= 8) {
    set.seen_mask |= static_cast<uint8_t>(1u << (index - 1));
  }

  // publish once every sentence in the set has arrived
  const uint8_t complete = static_cast<uint8_t>((1u << set.total_sentences) - 1);
  if ((set.total_sentences <= 8) && !set.malformed && ((set.seen_mask & complete) == complete)) {
    set.published = set.building;
  }
  return true;
}

bool NmeaSatellites::feedGsa(uint8_t constellation, const std::vector<std::string> &fields) {
  // $<T>GSA,mode,fixMode,[sat]x12,pdop,hdop,vdop,systemId*cs
  if (fields.size() < 17) {
    return true;
  }

  unsigned long fix_mode = 0;
  if (parseUint(fields[2], fix_mode)) {
    if ((fix_mode >= 1) && (fix_mode <= 3)) {
      m_Dop.fix_type = static_cast<uint8_t>(fix_mode);
    }
  }

  // NMEA 4.1 appends a system ID to GNGSA. Prefer it over the talker prefix,
  // which is often GN for a receiver that reports one solution per system.
  uint8_t system = constellation;
  if (fields.size() >= 19) {
    unsigned long system_id = 0;
    if (parseUint(fields[18], system_id)) {
      switch (system_id) {
        case CONSTELLATION_GPS:
        case CONSTELLATION_GLONASS:
        case CONSTELLATION_GALILEO:
        case CONSTELLATION_BEIDOU:
        case CONSTELLATION_QZSS:
          system = static_cast<uint8_t>(system_id);
          break;
        default:
          break;
      }
    }
  }

  // A GSA sentence replaces the current solution for its system. Keeping old
  // PRNs makes satellites remain used after they leave the solution. A
  // combined GNGSA with no system ID replaces all systems.
  if (system == CONSTELLATION_UNKNOWN) {
    m_Used.clear();
  } else {
    m_Used.erase(std::remove_if(m_Used.begin(), m_Used.end(),
                                [system](const UsedSatellite &sat) {
                                  return (sat.constellation == system)
                                         || (sat.constellation == CONSTELLATION_UNKNOWN);
                                }),
                 m_Used.end());
  }

  for (size_t i = 3; i < 15; i++) {
    unsigned long prn = 0;
    if (parseUint(fields[i], prn) && (prn != 0) && (prn <= std::numeric_limits<uint16_t>::max())
        && (m_Used.size() < MAX_USED_SATELLITES)) {
      const UsedSatellite used = {system, static_cast<uint16_t>(prn)};
      if (std::find_if(m_Used.begin(), m_Used.end(),
                       [used](const UsedSatellite &sat) {
                         return (sat.constellation == used.constellation) && (sat.prn == used.prn);
                       })
          == m_Used.end()) {
        m_Used.push_back(used);
      }
    }
  }

  float pdop = 0;
  float hdop = 0;
  float vdop = 0;
  const bool haveVdop = (fields.size() < 18) || fields[17].empty() || parseFloat(fields[17], vdop);
  if (parseFloat(fields[15], pdop) && parseFloat(fields[16], hdop) && haveVdop) {
    m_Dop.pdop = pdop;
    m_Dop.hdop = hdop;
    m_Dop.vdop = vdop;
    m_Dop.valid = true;
  } else {
    // This sentence replaced the solution, so its DOP replaces the previous
    // figures too. Keeping them would publish stale numbers as valid forever.
    m_Dop.pdop = 0;
    m_Dop.hdop = 0;
    m_Dop.vdop = 0;
    m_Dop.valid = false;
  }

  (void)constellation;
  return true;
}

bool NmeaSatellites::feed(const std::string &sentence) {
  size_t dollar = sentence.find('$');
  if (dollar == std::string::npos) {
    dollar = 0;
  }
  if ((sentence.size() - dollar) < 6 || !validNmeaChecksum(sentence, dollar)) {
    return false;
  }

  const char c0 = sentence[dollar + 1];
  const char c1 = sentence[dollar + 2];
  const std::string type = sentence.substr(dollar + 3, 3);

  const std::vector<std::string> fields = splitNmea(sentence.substr(dollar));
  if (type == "GSV") {
    return feedGsv(constellationForTalker(c0, c1), fields);
  }
  if (type == "GSA") {
    // GN GSA carries the used list and DOP for the combined solution
    uint8_t constellation = constellationForTalker(c0, c1);
    return feedGsa(constellation, fields);
  }
  return false;
}

std::vector<Satellite> NmeaSatellites::satellites(void) const {
  std::vector<Satellite> all;
  for (const auto &set : m_Sets) {
    for (Satellite sat : set.published) {
      sat.used = std::find_if(m_Used.begin(), m_Used.end(),
                              [&sat](const UsedSatellite &used) {
                                return (used.prn == sat.prn)
                                       && ((used.constellation == CONSTELLATION_UNKNOWN)
                                           || (used.constellation == sat.constellation));
                              })
                 != m_Used.end();
      all.push_back(sat);
    }
  }

  std::stable_sort(all.begin(), all.end(),
                   [](const Satellite &a, const Satellite &b) { return a.snr > b.snr; });
  if (all.size() > MAX_SATELLITES) {
    all.resize(MAX_SATELLITES);
  }
  return all;
}

size_t NmeaSatellites::inView(void) const {
  size_t count = 0;
  for (const auto &set : m_Sets) {
    count += set.published.size();
  }
  return count;
}

size_t NmeaSatellites::used(void) const {
  size_t count = 0;
  for (const auto &set : m_Sets) {
    for (const auto &sat : set.published) {
      if (std::find_if(m_Used.begin(), m_Used.end(),
                       [&sat](const UsedSatellite &used) {
                         return (used.prn == sat.prn)
                                && ((used.constellation == CONSTELLATION_UNKNOWN)
                                    || (used.constellation == sat.constellation));
                       })
          != m_Used.end()) {
        count++;
      }
    }
  }
  return count;
}

void NmeaSatellites::clear(void) {
  m_Sets.clear();
  m_Used.clear();
  m_Dop = {0, 0, 0, 0, false};
}

namespace Eph {
bool isEphemerisMessage(uint8_t class_id, uint8_t message_id) {
  return (class_id == CLASS)
         && ((message_id == EPH_ID) || (message_id == ION_ID) || (message_id == UTC_ID));
}
}  // namespace Eph

bool EphemerisCollector::feed(const uint8_t *frame_data, size_t length) {
  if ((frame_data == nullptr) || (length < FRAME_OVERHEAD)) {
    return false;
  }
  if ((frame_data[0] != SYNC_0) || (frame_data[1] != SYNC_1)) {
    return false;
  }
  const uint16_t payload_length = readU16(frame_data + 2);
  if ((payload_length > MAX_PAYLOAD) || ((payload_length % 4) != 0)
      || (length != (FRAME_OVERHEAD + payload_length))) {
    return false;
  }
  if (!Eph::isEphemerisMessage(frame_data[4], frame_data[5])) {
    return false;
  }
  const uint32_t expected =
      checksum(frame_data[4], frame_data[5], payload_length, frame_data + HEADER_SIZE);
  const uint32_t received = readU32(frame_data + HEADER_SIZE + payload_length);
  if (expected != received) {
    return false;
  }
  if ((m_Data.size() + length) > MAX_BYTES) {
    return false;
  }

  m_Data.insert(m_Data.end(), frame_data, frame_data + length);
  m_Count++;
  return true;
}

void EphemerisCollector::clear(void) {
  m_Data.clear();
  m_Count = 0;
}

std::vector<std::pair<size_t, size_t>> splitFrames(const uint8_t *data, size_t length) {
  std::vector<std::pair<size_t, size_t>> spans;
  if ((data == nullptr) && (length != 0)) {
    return spans;
  }
  size_t offset = 0;
  while ((offset + FRAME_OVERHEAD) <= length) {
    if ((data[offset] != SYNC_0) || (data[offset + 1] != SYNC_1)) {
      break;
    }
    const uint16_t payload_length = readU16(data + offset + 2);
    if ((payload_length > MAX_PAYLOAD) || ((payload_length % 4) != 0)) {
      break;
    }
    const size_t frame_length = FRAME_OVERHEAD + payload_length;
    if ((offset + frame_length) > length) {
      break;
    }
    const uint32_t expected =
        checksum(data[offset + 4], data[offset + 5], payload_length, data + offset + HEADER_SIZE);
    const uint32_t received = readU32(data + offset + HEADER_SIZE + payload_length);
    if (expected != received) {
      break;
    }
    spans.emplace_back(offset, frame_length);
    offset += frame_length;
  }
  return spans;
}

MonHw parseMonHw(const uint8_t *payload, size_t length) {
  MonHw hw = {0, 0, 0, 0, false};
  // The CASIC section 2.14.2 layout could not be confirmed on a live unit, so
  // this reads a conservative subset and leaves the console to print the raw
  // bytes. Marked hardware-tuning-pending in plans/32-gps-advanced.md.
  if ((payload == nullptr) || (length < 56)) {
    return hw;
  }
  hw.noise = readU32(payload + 0);
  hw.agc = readU16(payload + 4);
  hw.antenna_status = payload[8];
  hw.jam_indicator = payload[9];
  hw.valid = true;
  return hw;
}

}  // namespace Casic
}  // namespace Furble
