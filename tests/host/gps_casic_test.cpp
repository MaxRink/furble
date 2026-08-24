// Host unit test for the pure CASIC and NMEA helper logic in
// lib/furble/protocol/GpsCasic.{h,cpp}. The module has no ESP-IDF, NVS or BLE
// dependency, so it compiles and links standalone here, mirroring the other
// pure-logic suites in this directory.
//
// Coverage:
//   a. checksum + frame        the id-first CASIC checksum and framing
//   b. Autobaud                the ladder step-down, NO_RECEIVER and lock paths
//   c. NmeaSatellites          GSV/GSA parse, DOP, used flags, reassembly
//   d. EphemerisCollector      assistance-frame capture and splitFrames replay
//   e. parseMonHw              best-effort MON-HW decode and short-buffer guard

#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "GpsCasic.h"

using namespace Furble;

namespace {

int g_Failures = 0;

bool check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "  FAIL: " << message << '\n';
    g_Failures++;
  }
  return condition;
}

// Compute and append an NMEA checksum: XOR of every char between '$' and '*',
// uppercase, formatted as *XX. body is the sentence without the leading '$' and
// without the trailing checksum.
std::string nmea(const std::string &body) {
  uint8_t sum = 0;
  for (char c : body) {
    sum ^= static_cast<uint8_t>(c);
  }
  char tail[8];
  std::snprintf(tail, sizeof(tail), "*%02X", sum);
  return "$" + body + tail;
}

bool near(float a, float b) {
  return std::fabs(a - b) < 0.001f;
}

// a. checksum + frame. The id-first operand order is load bearing; the plan32
// CFG-MSG vector pins it.
bool testChecksumAndFrame() {
  std::cout << "test: CASIC checksum is id-first and frame appends it little-endian\n";
  const int before = g_Failures;

  // CFG-MSG enabling NAV-DOP: class 0x06 id 0x01 payload {01,01,01,00}.
  const std::vector<uint8_t> payload = {0x01, 0x01, 0x01, 0x00};
  const uint32_t sum = Casic::checksum(0x06, 0x01, 4, payload.data());
  check(sum == 0x01070105u, "plan32 CFG-MSG checksum equals 0x01070105");

  const std::vector<uint8_t> f = Casic::frame(0x06, 0x01, payload);
  check(f.size() == 14, "frame is 6 header + 4 payload + 4 checksum bytes");
  check((f[0] == 0xBA) && (f[1] == 0xCE), "frame starts with the BA CE sync word");
  check((f[2] == 0x04) && (f[3] == 0x00), "frame carries the payload length little-endian");
  check((f[4] == 0x06) && (f[5] == 0x01), "frame carries class then id");

  const uint32_t appended = static_cast<uint32_t>(f[10]) | (static_cast<uint32_t>(f[11]) << 8)
                            | (static_cast<uint32_t>(f[12]) << 16)
                            | (static_cast<uint32_t>(f[13]) << 24);
  check(appended == sum, "the appended little-endian checksum equals checksum() for the payload");

  return g_Failures == before;
}

// b. Autobaud. begin() jumps to the fastest rate; starved steps walk the ladder
// down to NO_RECEIVER; two passing sentences in a step lock.
bool testAutobaudLadderDown() {
  std::cout << "test: autobaud steps down the whole ladder to NO_RECEIVER when starved\n";
  const int before = g_Failures;

  Casic::Autobaud ab;
  Casic::Autobaud::Action a = ab.begin(0, 0);
  check(a.change && (a.baud == 115200), "begin changes to 115200, the fastest ladder rate");
  check(ab.state() == Casic::Autobaud::State::PROBING, "begin enters PROBING");

  const uint32_t step = Casic::Autobaud::STEP_MS;
  const uint32_t expected[] = {9600, 38400, 57600, 19200, 4800};
  uint32_t now = step;
  for (uint32_t i = 0; i < 5; i++) {
    a = ab.service(now, 0);
    check(a.change && (a.baud == expected[i]), "starved step advances to the next ladder rate");
    check(ab.state() == Casic::Autobaud::State::PROBING, "still probing while walking the ladder");
    now += step;
  }

  a = ab.service(now, 0);
  check(!a.change, "the step past the last ladder rate makes no further baud change");
  check(ab.state() == Casic::Autobaud::State::NO_RECEIVER,
        "walking off the end of the ladder declares NO_RECEIVER");

  return g_Failures == before;
}

bool testAutobaudLockFirstStep() {
  std::cout << "test: autobaud locks on the very first step when sentences pass\n";
  const int before = g_Failures;

  Casic::Autobaud ab;
  ab.begin(0, 0);
  const Casic::Autobaud::Action a = ab.service(100, Casic::Autobaud::REQUIRED_SENTENCES);
  check(!a.change, "a lock does not change the baud");
  check(ab.state() == Casic::Autobaud::State::LOCKED, "two passing sentences lock the first step");
  check(ab.baud() == 115200, "the locked baud is the first ladder rate");

  const Casic::Autobaud::Action b = ab.service(200, Casic::Autobaud::REQUIRED_SENTENCES + 5);
  check(!b.change, "no further baud change after a lock");
  check(ab.state() == Casic::Autobaud::State::LOCKED, "the lock is sticky");

  return g_Failures == before;
}

bool testAutobaudLockLaterStep() {
  std::cout << "test: autobaud locks on a later ladder step when sentences pass there\n";
  const int before = g_Failures;

  Casic::Autobaud ab;
  ab.begin(0, 0);
  // Starve the first step so the ladder advances to 9600, then feed a passing
  // burst. The baseline resets on each step, so two sentences relative to the
  // step's baseline lock it.
  Casic::Autobaud::Action a = ab.service(Casic::Autobaud::STEP_MS, 0);
  check(a.change && (a.baud == 9600), "the ladder reaches 9600 before the lock");

  a = ab.service(Casic::Autobaud::STEP_MS + 100, Casic::Autobaud::REQUIRED_SENTENCES);
  check(!a.change, "the lock on a later step makes no baud change");
  check(ab.state() == Casic::Autobaud::State::LOCKED, "the later step locks");
  check(ab.baud() == 9600, "baud() reports the locked step rate");

  return g_Failures == before;
}

// c. NmeaSatellites. Two GPS sentences, one BeiDou sentence and a GSA solution.
bool testNmeaParse() {
  std::cout << "test: NmeaSatellites parses GSV in-view, GSA used PRNs and DOP\n";
  const int before = g_Failures;

  Casic::NmeaSatellites sats;
  // 8 GPS satellites across two sentences, four per sentence, total field = 2.
  sats.feed(nmea("GPGSV,2,1,08,01,40,100,45,02,30,200,40,03,20,300,35,04,10,050,30,1"));
  sats.feed(nmea("GPGSV,2,2,08,05,45,010,50,06,35,020,42,07,25,030,38,08,15,040,33,1"));
  // 3 BeiDou satellites in one sentence.
  sats.feed(nmea("BDGSV,1,1,03,11,50,111,48,12,40,222,44,13,30,333,39,1"));
  // Combined GSA solution: PRNs 01,02,03,11 used, fix mode 3, DOP 2.5/2.0/1.5.
  sats.feed(nmea("GPGSA,A,3,01,02,03,11,,,,,,,,,2.5,2.0,1.5,1"));

  check(sats.inView() == 11, "inView equals the 8 GPS plus 3 BeiDou satellites fed");
  check(sats.used() == 4, "used equals the four GSA PRNs that are also in view");

  const Casic::DopInfo dop = sats.dop();
  check(dop.valid, "the GSA DOP is marked valid");
  check(near(dop.pdop, 2.5f), "pdop parses as 2.5");
  check(near(dop.hdop, 2.0f), "hdop parses as 2.0");
  check(near(dop.vdop, 1.5f), "vdop parses as 1.5");
  check(dop.fix_type == 3, "fix_type is the 3D fix mode");

  const std::vector<Casic::Satellite> table = sats.satellites();
  check(table.size() == 11, "satellites() returns every in-view satellite");
  bool sorted = true;
  for (size_t i = 1; i < table.size(); i++) {
    if (table[i - 1].snr < table[i].snr) {
      sorted = false;
    }
  }
  check(sorted, "satellites() is sorted by SNR descending");
  check(!table.empty() && (table[0].snr == 50), "the strongest satellite (SNR 50) is first");

  // The GSA-listed PRNs are marked used, the rest are not.
  size_t markedUsed = 0;
  for (const auto &s : table) {
    const bool inGsa = (s.prn == 1) || (s.prn == 2) || (s.prn == 3) || (s.prn == 11);
    check(s.used == inGsa, "satellite used flag matches its GSA membership");
    if (s.used) {
      markedUsed++;
    }
  }
  check(markedUsed == 4, "exactly the four GSA PRNs are flagged used");

  return g_Failures == before;
}

bool testNmeaReassembly() {
  std::cout << "test: a partial multi-sentence GSV set does not publish until complete\n";
  const int before = g_Failures;

  Casic::NmeaSatellites sats;
  // Only the first of a two-sentence set: nothing should publish yet.
  sats.feed(nmea("GPGSV,2,1,08,01,40,100,45,02,30,200,40,03,20,300,35,04,10,050,30,1"));
  check(sats.inView() == 0, "the first half of a two-sentence GSV set publishes nothing");

  // The second sentence completes the set and publishes all eight.
  sats.feed(nmea("GPGSV,2,2,08,05,45,010,50,06,35,020,42,07,25,030,38,08,15,040,33,1"));
  check(sats.inView() == 8, "the completed GSV set publishes all eight satellites");

  return g_Failures == before;
}

bool testNmeaMalformed() {
  std::cout << "test: malformed, duplicated and out-of-range NMEA does not poison the table\n";
  const int before = g_Failures;

  Casic::NmeaSatellites sats;
  const std::string first =
      nmea("GPGSV,2,1,08,01,40,100,45,02,30,200,40,03,20,300,35,04,10,050,30,1");
  const std::string second =
      nmea("GPGSV,2,2,08,05,45,010,50,06,35,020,42,07,25,030,38,08,15,040,33,1");
  check(sats.feed(first), "a valid first GSV sentence is accepted");
  check(sats.feed(second), "a valid second GSV sentence is accepted");
  check(sats.inView() == 8, "the valid set publishes eight satellites");

  check(sats.feed(second), "a duplicate GSV sentence is recognized");
  check(sats.inView() == 8, "a duplicate sentence does not append duplicate satellites");

  std::string badChecksum = first;
  badChecksum.back() = (badChecksum.back() == '0') ? '1' : '0';
  check(!sats.feed(badChecksum), "a sentence with a bad NMEA checksum is rejected");
  check(sats.inView() == 8, "a bad-checksum sentence leaves the published table unchanged");

  check(sats.feed(nmea("GPGSV,9,1,36,01,40,100,45")),
        "an oversized GSV set is identified as GSV without being accepted into the table");
  check(sats.inView() == 8, "a GSV set with more than eight sentences is ignored safely");
  check(sats.feed(nmea("GPGSV,2,3,08,01,40,100,45")),
        "an out-of-range GSV index is identified without changing state");
  check(sats.inView() == 8, "an out-of-range GSV index leaves the table unchanged");

  check(sats.feed(nmea("GPGSV,1,1,01,01,91,100,45")),
        "an out-of-range satellite field is identified as GSV");
  check(sats.inView() == 8,
        "a new set containing an out-of-range satellite does not replace valid data");

  Casic::NmeaSatellites dop;
  check(dop.feed(nmea("GPGSA,A,3,01,,,,,,,,,,,,,nan,2.0,1.0,1")),
        "a GSA sentence with non-finite DOP is identified as GSA");
  check(!dop.dop().valid, "non-finite DOP values are not published as valid diagnostics");

  return g_Failures == before;
}

// d. EphemerisCollector + isEphemerisMessage + splitFrames.
bool testEphemerisCollector() {
  std::cout << "test: EphemerisCollector stores only assistance frames and splitFrames replays\n";
  const int before = g_Failures;

  check(Casic::Eph::isEphemerisMessage(0x08, 0x07), "MSG-GPSEPH is an assistance message");
  check(Casic::Eph::isEphemerisMessage(0x08, 0x06), "MSG-GPSION is an assistance message");
  check(!Casic::Eph::isEphemerisMessage(0x06, 0x04), "a CFG frame is not an assistance message");

  const std::vector<uint8_t> ephPayload(72, 0);
  const std::vector<uint8_t> ionPayload(16, 0);
  const std::vector<uint8_t> otherPayload(4, 0);
  const std::vector<uint8_t> ephFrame = Casic::frame(0x08, 0x07, ephPayload);
  const std::vector<uint8_t> ionFrame = Casic::frame(0x08, 0x06, ionPayload);
  const std::vector<uint8_t> otherFrame = Casic::frame(0x06, 0x04, otherPayload);

  Casic::EphemerisCollector collector;
  const bool storedEph = collector.feed(ephFrame.data(), ephFrame.size());
  const bool storedOther = collector.feed(otherFrame.data(), otherFrame.size());
  const bool storedIon = collector.feed(ionFrame.data(), ionFrame.size());
  check(storedEph, "the ephemeris frame is stored");
  check(!storedOther, "the non-assistance frame is rejected");
  check(storedIon, "the ionosphere frame is stored");
  check(collector.frameCount() == 2, "only the two assistance frames were stored");
  check(collector.data().size() == (ephFrame.size() + ionFrame.size()),
        "the stored length equals the two framed sizes");

  const std::vector<std::pair<size_t, size_t>> spans =
      Casic::splitFrames(collector.data().data(), collector.data().size());
  check(spans.size() == 2, "splitFrames yields exactly two spans");
  if (spans.size() == 2) {
    check((spans[0].first == 0) && (spans[0].second == ephFrame.size()),
          "the first span covers the ephemeris frame");
    check((spans[1].first == ephFrame.size()) && (spans[1].second == ionFrame.size()),
          "the second span covers the ionosphere frame at the right offset");
  }

  // A corrupted checksum on the first frame stops the walk immediately.
  std::vector<uint8_t> corrupt = collector.data();
  corrupt[ephFrame.size() - 1] ^= 0xFF;
  const std::vector<std::pair<size_t, size_t>> corruptSpans =
      Casic::splitFrames(corrupt.data(), corrupt.size());
  check(corruptSpans.empty(), "splitFrames returns empty on a corrupted stream");

  Casic::EphemerisCollector malformed;
  check(!malformed.feed(ephFrame.data(), ephFrame.size() - 1),
        "a truncated assistance frame is rejected before storage");
  std::vector<uint8_t> trailing = ephFrame;
  trailing.push_back(0);
  check(!malformed.feed(trailing.data(), trailing.size()),
        "an assistance frame with trailing bytes is rejected");
  check(!malformed.feed(nullptr, ephFrame.size()), "a null assistance frame is rejected");
  check(malformed.feed(ephFrame.data(), ephFrame.size()),
        "a valid assistance frame remains accepted after malformed inputs");
  // Malformed input must not mutate the collector, while the valid frame is
  // accepted. The frame count is therefore exactly one.
  check(malformed.frameCount() == 1, "malformed assistance inputs do not mutate frame count");
  check(Casic::splitFrames(nullptr, 1).empty(), "splitFrames rejects a null non-empty stream");

  return g_Failures == before;
}

// e. parseMonHw. A full buffer decodes; a short buffer is invalid.
bool testMonHw() {
  std::cout << "test: parseMonHw decodes a full buffer and rejects a short one\n";
  const int before = g_Failures;

  std::vector<uint8_t> buf(56, 0);
  // noise 0x11223344 little-endian, agc 0x5566, antenna_status 0x07, jam 0x2A.
  buf[0] = 0x44;
  buf[1] = 0x33;
  buf[2] = 0x22;
  buf[3] = 0x11;
  buf[4] = 0x66;
  buf[5] = 0x55;
  buf[8] = 0x07;
  buf[9] = 0x2A;
  const Casic::MonHw hw = Casic::parseMonHw(buf.data(), buf.size());
  check(hw.valid, "a 56-byte MON-HW buffer is valid");
  check(hw.noise == 0x11223344u, "noise reads little-endian from offset 0");
  check(hw.agc == 0x5566u, "agc reads little-endian from offset 4");
  check(hw.antenna_status == 0x07, "antenna_status reads from offset 8");
  check(hw.jam_indicator == 0x2A, "jam_indicator reads from offset 9");

  std::vector<uint8_t> shortBuf(10, 0);
  const Casic::MonHw shortHw = Casic::parseMonHw(shortBuf.data(), shortBuf.size());
  check(!shortHw.valid, "a short MON-HW buffer is invalid");
  check(!Casic::parseMonHw(nullptr, buf.size()).valid, "a null MON-HW buffer is invalid");

  return g_Failures == before;
}

}  // namespace

int main() {
  testChecksumAndFrame();
  testAutobaudLadderDown();
  testAutobaudLockFirstStep();
  testAutobaudLockLaterStep();
  testNmeaParse();
  testNmeaReassembly();
  testNmeaMalformed();
  testEphemerisCollector();
  testMonHw();

  if (g_Failures != 0) {
    std::cout << "gps casic harness: FAIL (" << g_Failures << " checks)\n";
    return 1;
  }
  std::cout << "gps casic harness: PASS\n";
  return 0;
}
