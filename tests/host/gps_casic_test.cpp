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
//   f. splitFrames             span walking, truncation and the payload cap
//   g. constellationForTalker  every talker prefix in the implementation
//   h. fuzz                    a deterministic random and mutation sweep
//
// Everything past testMonHw() below is the adversarial half of the suite: the
// malformed, out-of-range and hostile inputs each entry point has to survive.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "GpsCasic.h"

using namespace Furble;

namespace {

int g_Failures = 0;
int g_Checks = 0;

bool check(bool condition, const char *message) {
  g_Checks++;
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
  sats.feed(nmea("GNGSA,A,3,01,02,03,11,,,,,,,,,2.5,2.0,1.5"));

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

  // A newer GSA solution replaces the old used list. PRN 03 and BeiDou PRN
  // 11 are no longer used in this update and must be cleared.
  sats.feed(nmea("GNGSA,A,3,01,02,,,,,,,,,,,2.5,2.0,"));
  const std::vector<Casic::Satellite> refreshed = sats.satellites();
  for (const auto &s : refreshed) {
    if ((s.prn == 1) || (s.prn == 2)) {
      check(s.used, "PRNs retained by a newer GSA remain used");
    } else {
      check(!s.used, "PRNs removed by a newer GSA are cleared");
    }
  }
  check(sats.dop().valid, "an empty optional GSA system ID does not invalidate DOP");

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

bool testGsaSystemId() {
  std::cout << "test: GNGSA system ID keeps used flags scoped to one constellation\n";
  const int before = g_Failures;

  Casic::NmeaSatellites sats;
  sats.feed(nmea("GPGSV,1,1,01,01,40,100,45,1"));
  sats.feed(nmea("BDGSV,1,1,01,01,40,100,45,1"));
  sats.feed(nmea("GNGSA,A,3,01,,,,,,,,,,,,2.5,2.0,1.5,1"));

  check(sats.used() == 1, "a GNGSA system ID marks only the selected system used");
  for (const auto &sat : sats.satellites()) {
    if (sat.constellation == Casic::CONSTELLATION_GPS) {
      check(sat.used, "GPS is marked used by system ID 1");
    } else if (sat.constellation == Casic::CONSTELLATION_BEIDOU) {
      check(!sat.used, "BeiDou is not marked used by system ID 1");
    }
  }

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

  check(sats.feed(nmea("GPGSV,1,1,01,01,40,100,100,1")),
        "an out-of-range C/N0 field is identified as GSV");
  check(sats.inView() == 8, "an out-of-range C/N0 does not replace valid data");

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

// One satellite quad as it appears in a GSV sentence. Every field is a string
// so a test can put a non-numeric or out-of-range value in any one of them.
struct Quad {
  std::string prn;
  std::string elevation;
  std::string azimuth;
  std::string snr;
};

// Build the body of a GSV sentence. The helper keeps the field count exact so a
// test can vary one field without recounting commas.
std::string gsvBody(const std::string &talker,
                    const std::string &total,
                    const std::string &index,
                    const std::string &inview,
                    const std::vector<Quad> &quads) {
  std::string body = talker + "GSV," + total + "," + index + "," + inview;
  for (const Quad &quad : quads) {
    body += "," + quad.prn + "," + quad.elevation + "," + quad.azimuth + "," + quad.snr;
  }
  // the trailing signalId field, which is not part of a satellite quad
  body += ",1";
  return body;
}

// A run of count satellites with descending C/N0, used to fill GSV sets.
std::vector<Quad> quadRun(unsigned firstPrn, unsigned firstSnr, unsigned count) {
  std::vector<Quad> quads;
  for (unsigned i = 0; i < count; i++) {
    char prn[8];
    char snr[8];
    std::snprintf(prn, sizeof(prn), "%02u", firstPrn + i);
    std::snprintf(snr, sizeof(snr), "%02u", firstSnr - i);
    quads.push_back(Quad {prn, "40", "100", snr});
  }
  return quads;
}

// Build the body of a GSA sentence with all twelve satellite slots present, so
// pdop lands on field 15 exactly where the parser looks for it. An empty
// systemId leaves the sentence at the 18 fields of NMEA 4.0.
std::string gsaBody(const std::string &talker,
                    const std::vector<std::string> &prns,
                    const std::string &pdop,
                    const std::string &hdop,
                    const std::string &vdop,
                    const std::string &systemId) {
  std::string body = talker + "GSA,A,3";
  for (size_t i = 0; i < 12; i++) {
    body += ",";
    if (i < prns.size()) {
      body += prns[i];
    }
  }
  body += "," + pdop + "," + hdop + "," + vdop;
  if (!systemId.empty()) {
    body += "," + systemId;
  }
  return body;
}

// a. checksum + frame edges. An empty payload, a payload whose length is not a
// multiple of 4, and the frame to splitFrames round trip.
bool testChecksumEdges() {
  std::cout << "test: checksum covers an empty payload and a tail word shorter than four bytes\n";
  const int before = g_Failures;

  check(Casic::checksum(0x06, 0x01, 0, nullptr) == 0x01060000u,
        "an empty payload checksums to the id, class and length header alone");
  const std::vector<uint8_t> empty;
  const std::vector<uint8_t> emptyFrame = Casic::frame(0x06, 0x01, empty);
  check(emptyFrame.size() == 10, "an empty payload frames to 6 header plus 4 checksum bytes");
  check((emptyFrame[2] == 0) && (emptyFrame[3] == 0), "the empty frame declares a zero length");

  // 6 bytes is one whole word plus a 2 byte tail. The tail word is zero padded
  // in its high bytes rather than reading past the end of the payload.
  const std::vector<uint8_t> ragged = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
  check(Casic::checksum(0x08, 0x07, 6, ragged.data()) == 0x0B0B080Cu,
        "a 6 byte payload sums one full word plus a zero padded tail word");

  // frame() will build a ragged payload but splitFrames only walks a payload
  // length that is a multiple of 4, so the ragged frame is not replayable.
  const std::vector<uint8_t> raggedFrame = Casic::frame(0x08, 0x07, ragged);
  check(raggedFrame.size() == 16, "the ragged payload still frames with the full overhead");
  check(Casic::splitFrames(raggedFrame.data(), raggedFrame.size()).empty(),
        "splitFrames refuses a frame whose payload length is not a multiple of 4");

  const std::vector<std::pair<size_t, size_t>> emptySpans =
      Casic::splitFrames(emptyFrame.data(), emptyFrame.size());
  check(emptySpans.size() == 1, "an empty payload frame round trips through splitFrames");
  if (emptySpans.size() == 1) {
    check((emptySpans[0].first == 0) && (emptySpans[0].second == 10),
          "the round tripped span covers the whole 10 byte frame");
  }

  const std::vector<uint8_t> body(64, 0xA5);
  const std::vector<uint8_t> full = Casic::frame(0x08, 0x07, body);
  const std::vector<std::pair<size_t, size_t>> fullSpans =
      Casic::splitFrames(full.data(), full.size());
  check(fullSpans.size() == 1, "a 64 byte payload frame round trips as one span");
  if (fullSpans.size() == 1) {
    check((fullSpans[0].first == 0) && (fullSpans[0].second == full.size()),
          "the round tripped span covers the whole framed message");
  }

  return g_Failures == before;
}

// b. Autobaud. A lock on every rung of the ladder in turn.
bool testAutobaudLocksAtEveryLadderRate() {
  std::cout << "test: autobaud locks at each of the six ladder rates in turn\n";
  const int before = g_Failures;

  for (size_t target = 0; target < Casic::Autobaud::LADDER.size(); target++) {
    Casic::Autobaud ab;
    // A synthetic passedChecksum counter that only moves when this test says so.
    uint32_t passed = 1000;
    uint32_t now = 0;
    ab.begin(now, passed);
    for (size_t s = 0; s < target; s++) {
      now += Casic::Autobaud::STEP_MS;
      const Casic::Autobaud::Action advance = ab.service(now, passed);
      check(advance.change && (advance.baud == Casic::Autobaud::LADDER[s + 1]),
            "a starved step advances to the next ladder rate");
    }

    passed += Casic::Autobaud::REQUIRED_SENTENCES;
    const Casic::Autobaud::Action lock = ab.service(now + 1, passed);
    check(!lock.change, "a lock never asks for a baud change");
    check(ab.state() == Casic::Autobaud::State::LOCKED, "the target ladder step locks");
    check(ab.step() == target, "the locked step index is the target step");
    check(ab.baud() == Casic::Autobaud::LADDER[target], "baud() reports the locked ladder rate");
  }

  return g_Failures == before;
}

bool testAutobaudOneSentencePerStep() {
  std::cout << "test: one passing sentence per step never reaches the two sentence lock\n";
  const int before = g_Failures;

  Casic::Autobaud ab;
  uint32_t passed = 0;
  uint32_t now = 0;
  ab.begin(now, passed);
  for (size_t s = 0; s < Casic::Autobaud::LADDER.size(); s++) {
    // exactly one sentence passes checksum on this rung
    passed++;
    now += Casic::Autobaud::STEP_MS;
    const Casic::Autobaud::Action a = ab.service(now, passed);
    if ((s + 1) < Casic::Autobaud::LADDER.size()) {
      check(a.change && (a.baud == Casic::Autobaud::LADDER[s + 1]),
            "a rung with one passing sentence still advances the ladder");
      check(ab.state() == Casic::Autobaud::State::PROBING,
            "one sentence is below REQUIRED_SENTENCES so the rung does not lock");
    } else {
      check(!a.change, "the sixth starved step asks for no further baud change");
      check(ab.state() == Casic::Autobaud::State::NO_RECEIVER,
            "six rungs of one sentence each declare NO_RECEIVER");
    }
  }
  check(ab.step() == Casic::Autobaud::LADDER.size(),
        "NO_RECEIVER leaves the step index one past the ladder");
  check(ab.baud() == Casic::Autobaud::LADDER[0],
        "baud() falls back to the first ladder rate once the step runs off the end");

  return g_Failures == before;
}

bool testAutobaudCounterWrap() {
  std::cout << "test: a passedChecksum counter wrapping past UINT32_MAX does not fake a lock\n";
  const int before = g_Failures;

  Casic::Autobaud ab;
  // Start with the counter one sentence short of wrapping.
  ab.begin(0, 0xFFFFFFFFu);
  Casic::Autobaud::Action a = ab.service(100, 0x00000000u);
  check(!a.change, "the single sentence that wrapped the counter changes nothing");
  check(ab.state() == Casic::Autobaud::State::PROBING,
        "one sentence across the wrap is one sentence, not a lock");

  // The step boundary rebases on the already wrapped counter, so the wrap does
  // not leak a large difference into the next rung either.
  a = ab.service(Casic::Autobaud::STEP_MS, 0x00000000u);
  check(a.change && (a.baud == 9600), "the starved first rung still advances across the wrap");
  check(ab.state() == Casic::Autobaud::State::PROBING, "the rebased rung does not lock");

  a = ab.service(Casic::Autobaud::STEP_MS + 100, 0x00000001u);
  check(!a.change, "one sentence on the new rung asks for no baud change");
  check(ab.state() == Casic::Autobaud::State::PROBING,
        "one sentence on the new rung is still below the lock threshold");

  a = ab.service(Casic::Autobaud::STEP_MS + 200, 0x00000002u);
  check(ab.state() == Casic::Autobaud::State::LOCKED,
        "the genuine second sentence locks after the wrap");
  check(ab.baud() == 9600, "the lock after a wrap reports the rung it happened on");

  return g_Failures == before;
}

bool testAutobaudServiceBeforeBegin() {
  std::cout << "test: service() before begin() is inert and stays IDLE\n";
  const int before = g_Failures;

  Casic::Autobaud ab;
  check(ab.state() == Casic::Autobaud::State::IDLE, "a fresh ladder starts IDLE");
  const Casic::Autobaud::Action a = ab.service(5000, 99);
  check(!a.change && (a.baud == 0), "service() before begin() asks for no baud change");
  check(ab.state() == Casic::Autobaud::State::IDLE, "service() before begin() stays IDLE");
  check(ab.step() == 0, "service() before begin() does not walk the ladder");
  check(ab.baud() == Casic::Autobaud::LADDER[0], "an IDLE ladder reports the first rate");

  return g_Failures == before;
}

// c. NmeaSatellites envelope handling: everything that is not a well formed
// NMEA line must be refused before a field is ever read.
bool testNmeaFraming() {
  std::cout << "test: NMEA framing refuses every damaged envelope\n";
  const int before = g_Failures;

  const std::string good = nmea(gsvBody("GP", "1", "1", "01",
                                        {
                                            {"01", "40", "100", "45"}
  }));

  Casic::NmeaSatellites sats;
  check(!sats.feed(""), "an empty string is not a sentence");
  check(!sats.feed("$"), "a lone dollar sign is too short to be a sentence");
  check(!sats.feed("GPGS"), "a short string with no dollar sign is refused");
  check(!sats.feed(good.substr(1)), "a sentence with its dollar sign removed is refused");
  check(!sats.feed(good.substr(0, 10)), "a line truncated before the star is refused");
  check(!sats.feed(good.substr(0, good.size() - 3)), "a line truncated at the star is refused");
  check(!sats.feed(good.substr(0, good.size() - 1)), "a half written checksum is refused");
  check(!sats.feed(good + "XY"), "trailing garbage after the checksum is refused");
  check(!sats.feed(good + " "), "a trailing space after the checksum is refused");

  std::string noStar = good;
  noStar[noStar.find('*')] = ',';
  check(!sats.feed(noStar), "a sentence with no star delimiter is refused");

  std::string badChecksum = good;
  badChecksum.back() = (badChecksum.back() == 'A') ? 'B' : 'A';
  check(!sats.feed(badChecksum), "a sentence with a bad NMEA checksum is refused");

  std::string badHex = good;
  badHex.back() = 'Z';
  check(!sats.feed(badHex), "a non-hex checksum digit is refused");

  check(sats.inView() == 0, "no damaged envelope published a satellite");
  check(sats.satellites().empty(), "no damaged envelope left a row in the table");
  check(sats.used() == 0, "no damaged envelope marked a satellite used");
  check(!sats.dop().valid, "no damaged envelope published DOP");

  // The parser hunts for the first dollar sign, so a burst of line noise ahead
  // of a good sentence does not lose it.
  Casic::NmeaSatellites offset;
  check(offset.feed(std::string("noise") + good), "a sentence after leading noise is accepted");
  check(offset.inView() == 1, "the sentence after leading noise publishes its satellite");
  check(offset.feed(good + "\r\n"), "a CR LF terminated sentence is accepted");
  check(offset.inView() == 1, "the CR LF terminated sentence republishes the same set");

  // A sentence type furble does not parse is well formed but not interesting.
  check(!offset.feed(nmea("GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,")),
        "a well formed GGA sentence is not a GSV or GSA");
  check(offset.inView() == 1, "a GGA sentence leaves the satellite table alone");

  return g_Failures == before;
}

// GSV header fields decide whether a sentence is even part of a set.
bool testGsvHeaderGuards() {
  std::cout << "test: GSV header fields are range checked before a set is started\n";
  const int before = g_Failures;

  const std::vector<Quad> quad = {
      {"01", "40", "100", "45"}
  };
  const std::string headers[] = {
      gsvBody("GP", "0", "1", "01", quad),  // total sentence count of zero
      gsvBody("GP", "9", "1", "36", quad),  // total sentence count above eight
      gsvBody("GP", "1", "0", "01", quad),  // sentence index of zero
      gsvBody("GP", "2", "3", "08", quad),  // sentence index above the total
      gsvBody("GP", "x", "1", "01", quad),  // non-numeric total
      gsvBody("GP", "1", "y", "01", quad),  // non-numeric index
      gsvBody("GP", "", "1", "01", quad),   // empty total
      gsvBody("GP", "1", "", "01", quad),   // empty index
  };
  for (const std::string &body : headers) {
    Casic::NmeaSatellites sats;
    check(sats.feed(nmea(body)), "an out-of-range GSV header is still recognised as a GSV");
    check(sats.inView() == 0, "an out-of-range GSV header publishes nothing");
    check(sats.satellites().empty(), "an out-of-range GSV header leaves the table empty");
    check(sats.used() == 0, "an out-of-range GSV header marks nothing used");
    check(!sats.dop().valid, "a GSV sentence never marks DOP valid");
  }

  Casic::NmeaSatellites shortFields;
  check(shortFields.feed(nmea("GPGSV")), "a bare GSV type with no fields is recognised");
  check(shortFields.feed(nmea("GPGSV,1,1")), "a GSV with fewer than four fields is recognised");
  check(shortFields.inView() == 0, "a GSV with fewer than four fields publishes nothing");
  check(shortFields.satellites().empty(), "a short GSV leaves the table empty");

  return g_Failures == before;
}

// A satellite quad with a field outside its NMEA range poisons the whole set,
// which must then publish nothing rather than a partial table.
bool testGsvQuadGuards() {
  std::cout << "test: an out-of-range satellite quad blocks the whole GSV set\n";
  const int before = g_Failures;

  const std::vector<std::vector<Quad>> bad = {
      {{"70000", "40", "100", "45"}},  // PRN above 65535
      {{"01", "91", "100", "45"}},     // elevation above 90
      {{"01", "40", "360", "45"}},     // azimuth above 359
      {{"01", "40", "100", "100"}},    // C/N0 above 99
      {{"abc", "40", "100", "45"}},    // non-numeric PRN
  };
  for (const std::vector<Quad> &quads : bad) {
    Casic::NmeaSatellites sats;
    check(sats.feed(nmea(gsvBody("GP", "1", "1", "01", quads))),
          "a GSV holding an out-of-range quad is still recognised as a GSV");
    check(sats.inView() == 0, "an out-of-range quad publishes no satellite");
    check(sats.satellites().empty(), "an out-of-range quad leaves the table empty");
  }

  // The malformed quad arrives in the last sentence of a two sentence set, so
  // the first four satellites are already built when it lands.
  Casic::NmeaSatellites partial;
  partial.feed(nmea(gsvBody("GP", "2", "1", "08", quadRun(1, 45, 4))));
  check(partial.inView() == 0, "the first sentence of a set publishes nothing on its own");
  std::vector<Quad> tail = quadRun(5, 41, 4);
  tail.back().elevation = "91";
  partial.feed(nmea(gsvBody("GP", "2", "2", "08", tail)));
  check(partial.inView() == 0, "a malformed quad in the last sentence blocks the whole set");
  check(partial.satellites().empty(), "a malformed set never publishes a partially built table");

  // Sentence 1 of the next set clears the malformed flag.
  partial.feed(nmea(gsvBody("GP", "1", "1", "02", quadRun(9, 20, 2))));
  check(partial.inView() == 2, "a clean set after a malformed one publishes normally");

  // Only an out-of-range number poisons a set. A field that does not parse at
  // all is treated as absent, which is how an empty C/N0 column arrives.
  Casic::NmeaSatellites lenient;
  std::vector<Quad> unparsable;
  unparsable.push_back(Quad {"05", "abc", "xyz", "zzz"});
  unparsable.push_back(Quad {"06", "", "", ""});
  lenient.feed(nmea(gsvBody("GP", "1", "1", "02", unparsable)));
  const std::vector<Casic::Satellite> table = lenient.satellites();
  check(table.size() == 2, "unparsable elevation, azimuth and C/N0 fields do not drop a satellite");
  if (table.size() == 2) {
    check((table[0].prn == 5) || (table[1].prn == 5), "the PRN is still parsed");
    for (const Casic::Satellite &sat : table) {
      check(sat.elevation == 0, "an unparsable elevation reads as zero");
      check(sat.azimuth == 0, "an unparsable azimuth reads as zero");
      check(sat.snr == 0, "an unparsable C/N0 reads as zero");
      check(!sat.snr_valid, "an unparsable C/N0 is not marked valid");
    }
  }

  return g_Failures == before;
}

// Multi-sentence GSV sets only publish once every sentence has arrived.
bool testGsvSetAssembly() {
  std::cout << "test: a GSV set publishes only when every sentence of it has arrived\n";
  const int before = g_Failures;

  Casic::NmeaSatellites sats;
  sats.feed(nmea(gsvBody("GP", "3", "1", "12", quadRun(1, 48, 4))));
  check(sats.inView() == 0, "one third of a three sentence set publishes nothing");
  sats.feed(nmea(gsvBody("GP", "3", "2", "12", quadRun(5, 44, 4))));
  check(sats.inView() == 0, "two thirds of a three sentence set publishes nothing");

  // A duplicated sentence index inside one set is a repeated burst, not more
  // satellites, so it must be dropped rather than appended.
  check(sats.feed(nmea(gsvBody("GP", "3", "2", "12", quadRun(5, 44, 4)))),
        "a duplicated GSV sentence index is recognised");
  check(sats.inView() == 0, "a duplicated sentence index does not complete the set");

  sats.feed(nmea(gsvBody("GP", "3", "3", "12", quadRun(9, 40, 4))));
  check(sats.inView() == 12, "the completed three sentence set publishes twelve satellites");
  check(sats.satellites().size() == 12, "the duplicated sentence added no extra rows");

  // A set whose second sentence never arrives keeps the previous publication.
  sats.feed(nmea(gsvBody("GP", "2", "1", "08", quadRun(20, 30, 4))));
  check(sats.inView() == 12, "an incomplete new set leaves the last published set in place");
  check(sats.satellites().size() == 12, "an incomplete new set publishes no rows of its own");

  // A sentence with a different total restarts the set cleanly.
  sats.feed(nmea(gsvBody("GP", "1", "1", "01", quadRun(30, 10, 1))));
  check(sats.inView() == 1, "a set with a new total replaces the previous publication");

  return g_Failures == before;
}

// The published table is ordered strongest first and capped at MAX_SATELLITES.
bool testSatelliteOrderingAndCap() {
  std::cout << "test: satellites() sorts by C/N0 descending and caps at MAX_SATELLITES\n";
  const int before = g_Failures;

  Casic::NmeaSatellites sats;
  const char *talkers[] = {"GP", "GL", "BD"};
  unsigned prn = 1;
  unsigned snr = 99;
  for (const char *talker : talkers) {
    for (unsigned index = 1; index <= 3; index++) {
      sats.feed(nmea(gsvBody(talker, "3", std::to_string(index), "12", quadRun(prn, snr, 4))));
      prn += 4;
      snr -= 4;
    }
  }

  check(sats.inView() == 36, "inView counts every published satellite, uncapped");
  const std::vector<Casic::Satellite> table = sats.satellites();
  check(table.size() == Casic::NmeaSatellites::MAX_SATELLITES,
        "satellites() caps the table at MAX_SATELLITES");

  bool sorted = true;
  for (size_t i = 1; i < table.size(); i++) {
    if (table[i - 1].snr < table[i].snr) {
      sorted = false;
    }
  }
  check(sorted, "the capped table is still sorted by C/N0 descending");
  check(!table.empty() && (table.front().snr == 99), "the strongest satellite survives the cap");
  check(!table.empty() && (table.back().snr == 68), "the cap keeps the strongest 32 of the 36");

  return g_Failures == before;
}

// GSA field guards, system scoping and the used list replacement rule.
bool testGsaGuards() {
  std::cout << "test: GSA guards its field count, scopes used PRNs and retains stale DOP\n";
  const int before = g_Failures;

  Casic::NmeaSatellites sats;
  sats.feed(nmea(gsvBody("GP", "1", "1", "02",
                         {
                             {"01", "40", "100", "45"},
                             {"02", "30", "200", "40"}
  })));
  sats.feed(nmea(gsaBody("GP", {"01", "02"}, "2.5", "2.0", "1.5", "1")));
  check(sats.used() == 2, "a per-system GSA marks both of its PRNs used");
  check(sats.dop().valid && near(sats.dop().pdop, 2.5f), "the GSA publishes its DOP");

  // A GSA with fewer than 17 fields is not a solution and must change nothing.
  check(sats.feed(nmea("GNGSA,A,3,01,02")), "a short GSA is still recognised as a GSA");
  check(sats.used() == 2, "a short GSA does not clear the used list");
  check(sats.dop().valid && near(sats.dop().pdop, 2.5f), "a short GSA does not clear the DOP");

  // A satellite dropped from a later GSA stops being reported as used.
  sats.feed(nmea(gsaBody("GP", {"01"}, "1.8", "1.2", "1.0", "1")));
  check(sats.used() == 1, "a PRN dropped from the newer GSA is no longer used");
  for (const Casic::Satellite &sat : sats.satellites()) {
    check(sat.used == (sat.prn == 1), "only the PRN retained by the newer GSA is flagged used");
  }

  // A GSA replaces the solution, so a DOP that does not parse has to clear the
  // previous figures. Keeping them would publish a stale DOP as valid forever.
  check(sats.feed(nmea(gsaBody("GP", {"01"}, "abc", "1.2", "1.0", "1"))),
        "a GSA with an unparsable pdop is still recognised");
  check(!sats.dop().valid, "an unparsable DOP clears the valid flag");
  check(near(sats.dop().pdop, 0.0f), "an unparsable DOP clears the previous figures");

  // An unrecognised system ID falls back to the talker prefix, and the GN
  // talker is the combined solution, which matches any constellation.
  Casic::NmeaSatellites unknownSystem;
  unknownSystem.feed(nmea(gsvBody("GP", "1", "1", "01",
                                  {
                                      {"01", "40", "100", "45"}
  })));
  unknownSystem.feed(nmea(gsvBody("BD", "1", "1", "01",
                                  {
                                      {"01", "40", "100", "45"}
  })));
  check(unknownSystem.feed(nmea(gsaBody("GN", {"01"}, "2.5", "2.0", "1.5", "7"))),
        "a GSA with an unknown system ID is still recognised");
  check(unknownSystem.used() == 2,
        "an unknown system ID falls back to the talker, and GN matches every constellation");

  return g_Failures == before;
}

// A combined GN GSA is replaced wholesale by the per-system GSAs that follow.
bool testGsaCombinedThenPerSystem() {
  std::cout << "test: a combined GN GSA is superseded by the per-system GSAs that follow\n";
  const int before = g_Failures;

  Casic::NmeaSatellites sats;
  sats.feed(nmea(gsvBody("GP", "1", "1", "01",
                         {
                             {"01", "40", "100", "45"}
  })));
  sats.feed(nmea(gsvBody("BD", "1", "1", "01",
                         {
                             {"11", "40", "100", "44"}
  })));

  // The combined solution carries no system ID, so both PRNs are used.
  sats.feed(nmea(gsaBody("GN", {"01", "11"}, "2.5", "2.0", "1.5", "")));
  check(sats.used() == 2, "the combined GN GSA marks both constellations used");

  // The GPS solution clears the combined entries and keeps only its own PRNs.
  sats.feed(nmea(gsaBody("GP", {"01"}, "2.4", "1.9", "1.4", "1")));
  check(sats.used() == 1, "the per-system GSA replaces the combined solution");
  for (const Casic::Satellite &sat : sats.satellites()) {
    check(sat.used == (sat.constellation == Casic::CONSTELLATION_GPS),
          "only the GPS satellite remains used after the GPS GSA");
  }

  // The BeiDou solution then adds its own PRNs without disturbing GPS.
  sats.feed(nmea(gsaBody("BD", {"11"}, "2.4", "1.9", "1.4", "4")));
  check(sats.used() == 2, "each per-system GSA adds its own constellation back");

  return g_Failures == before;
}

// d. EphemerisCollector refuses every malformed frame without mutating state.
bool testEphemerisMalformed() {
  std::cout << "test: EphemerisCollector refuses malformed frames without mutating its store\n";
  const int before = g_Failures;

  const std::vector<uint8_t> payload(72, 0x5A);
  const std::vector<uint8_t> good = Casic::frame(Casic::Eph::CLASS, Casic::Eph::EPH_ID, payload);

  Casic::EphemerisCollector collector;
  check(!collector.feed(nullptr, 0), "a null pointer with a zero length is refused");
  check(!collector.feed(nullptr, good.size()), "a null pointer with a plausible length is refused");
  check(!collector.feed(good.data(), 0), "a zero length frame is refused");
  check(!collector.feed(good.data(), 9), "a frame shorter than the 10 byte overhead is refused");

  std::vector<uint8_t> badSync = good;
  badSync[0] = 0xBB;
  check(!collector.feed(badSync.data(), badSync.size()),
        "a frame with a wrong first sync byte is refused");
  badSync = good;
  badSync[1] = 0xCF;
  check(!collector.feed(badSync.data(), badSync.size()),
        "a frame with a wrong second sync byte is refused");

  const std::vector<uint8_t> ragged =
      Casic::frame(Casic::Eph::CLASS, Casic::Eph::EPH_ID, std::vector<uint8_t>(6, 0));
  check(!collector.feed(ragged.data(), ragged.size()),
        "a declared payload length that is not a multiple of 4 is refused");

  check(!collector.feed(good.data(), good.size() - 4),
        "a buffer shorter than the declared length is refused");
  std::vector<uint8_t> padded = good;
  padded.push_back(0);
  check(!collector.feed(padded.data(), padded.size()),
        "a buffer longer than the declared length is refused");

  const std::vector<uint8_t> notAssist = Casic::frame(0x06, 0x01, payload);
  check(!collector.feed(notAssist.data(), notAssist.size()),
        "a non-assistance class and id is refused");
  const std::vector<uint8_t> wrongId = Casic::frame(Casic::Eph::CLASS, 0x04, payload);
  check(!collector.feed(wrongId.data(), wrongId.size()),
        "an assistance class with an unknown id is refused");

  std::vector<uint8_t> corrupt = good;
  corrupt.back() ^= 0xFF;
  check(!collector.feed(corrupt.data(), corrupt.size()), "a corrupted checksum is refused");
  corrupt = good;
  corrupt[8] ^= 0x01;
  check(!collector.feed(corrupt.data(), corrupt.size()),
        "a corrupted payload byte fails the checksum and is refused");

  check(collector.frameCount() == 0, "no malformed frame was counted");
  check(collector.data().empty(), "no malformed frame left bytes behind");

  check(collector.feed(good.data(), good.size()), "a valid assistance frame is still accepted");
  check(collector.frameCount() == 1, "the valid frame is the only counted frame");
  check(collector.data().size() == good.size(), "the store holds exactly the one frame");
  check(std::equal(good.begin(), good.end(), collector.data().begin()),
        "the frame is stored byte for byte with no re-framing");

  return g_Failures == before;
}

bool testEphemerisBudget() {
  std::cout << "test: a frame over the MAX_BYTES budget is refused rather than truncated\n";
  const int before = g_Failures;

  // 2048 is the largest payload the walker accepts, so two of these frames do
  // not fit the 4096 byte budget.
  const std::vector<uint8_t> big =
      Casic::frame(Casic::Eph::CLASS, Casic::Eph::EPH_ID, std::vector<uint8_t>(2048, 0x33));
  check(big.size() == 2058, "the largest accepted payload frames to 2058 bytes");

  Casic::EphemerisCollector collector;
  check(collector.feed(big.data(), big.size()), "the first large frame fits the budget");
  check(!collector.feed(big.data(), big.size()),
        "a second large frame would exceed MAX_BYTES and is refused");
  check(collector.data().size() == big.size(), "the refused frame appended nothing");
  check(collector.frameCount() == 1, "the refused frame was not counted");

  const std::vector<uint8_t> small =
      Casic::frame(Casic::Eph::CLASS, Casic::Eph::UTC_ID, std::vector<uint8_t>(4, 0));
  check(collector.feed(small.data(), small.size()),
        "a frame that still fits is accepted after a refusal");
  check(collector.data().size() == (big.size() + small.size()),
        "the fitting frame appended cleanly after the refusal");
  check(collector.data().size() <= Casic::EphemerisCollector::MAX_BYTES,
        "the store never exceeds MAX_BYTES");
  check(Casic::splitFrames(collector.data().data(), collector.data().size()).size() == 2,
        "the budgeted store still splits into its two frames");

  collector.clear();
  check(collector.data().empty() && (collector.frameCount() == 0), "clear() empties the store");

  return g_Failures == before;
}

// e. splitFrames keeps every span before the first bad frame and no more.
bool testSplitFramesEdges() {
  std::cout << "test: splitFrames keeps good spans and stops dead at the first bad frame\n";
  const int before = g_Failures;

  const std::vector<uint8_t> a =
      Casic::frame(Casic::Eph::CLASS, Casic::Eph::EPH_ID, std::vector<uint8_t>(72, 0x11));
  const std::vector<uint8_t> b =
      Casic::frame(Casic::Eph::CLASS, Casic::Eph::ION_ID, std::vector<uint8_t>(16, 0x22));
  const std::vector<uint8_t> c =
      Casic::frame(Casic::Eph::CLASS, Casic::Eph::UTC_ID, std::vector<uint8_t>(8, 0x33));

  std::vector<uint8_t> stream;
  stream.insert(stream.end(), a.begin(), a.end());
  stream.insert(stream.end(), b.begin(), b.end());
  stream.insert(stream.end(), c.begin(), c.end());

  check(Casic::splitFrames(nullptr, 0).empty(), "a null empty stream yields no spans");
  check(Casic::splitFrames(nullptr, 64).empty(), "a null stream with a length yields no spans");
  check(Casic::splitFrames(stream.data(), 0).empty(), "an empty buffer yields no spans");

  const std::vector<std::pair<size_t, size_t>> spans =
      Casic::splitFrames(stream.data(), stream.size());
  check(spans.size() == 3, "three valid frames split into three spans");
  if (spans.size() == 3) {
    check((spans[0].first == 0) && (spans[0].second == a.size()),
          "the first span covers the first frame");
    check((spans[1].first == a.size()) && (spans[1].second == b.size()),
          "the second span starts where the first ended");
    check((spans[2].first == (a.size() + b.size())) && (spans[2].second == c.size()),
          "the third span starts where the second ended");
  }

  // A corrupt second frame keeps the first span and drops everything after it.
  std::vector<uint8_t> corrupt = stream;
  corrupt[a.size() + b.size() - 1] ^= 0xFF;
  const std::vector<std::pair<size_t, size_t>> stopped =
      Casic::splitFrames(corrupt.data(), corrupt.size());
  check(stopped.size() == 1, "the walk stops at the corrupt frame and keeps the span before it");
  if (stopped.size() == 1) {
    check((stopped[0].first == 0) && (stopped[0].second == a.size()),
          "the span kept before the corrupt frame is the intact first frame");
  }

  // A declared length that runs past the buffer end drops the truncated frame.
  const std::vector<std::pair<size_t, size_t>> truncated =
      Casic::splitFrames(stream.data(), stream.size() - 4);
  check(truncated.size() == 2, "a frame whose declared length runs past the end is dropped");

  // A payload length above the 2048 cap is refused even though frame() built it.
  const std::vector<uint8_t> oversized =
      Casic::frame(Casic::Eph::CLASS, Casic::Eph::EPH_ID, std::vector<uint8_t>(2052, 0));
  check(Casic::splitFrames(oversized.data(), oversized.size()).empty(),
        "a declared payload length above the 2048 cap is refused");

  // A partial frame after the last whole one is ignored rather than guessed at.
  std::vector<uint8_t> trailing = stream;
  trailing.push_back(0xBA);
  trailing.push_back(0xCE);
  check(Casic::splitFrames(trailing.data(), trailing.size()).size() == 3,
        "a partial frame at the end of the buffer is ignored");

  return g_Failures == before;
}

// f. parseMonHw zeroes every field when the payload is short.
bool testEphemerisFreshness() {
  std::cout << "test: the ephemeris freshness rule against the receiver clock\n";
  const int before = g_Failures;

  // 2026-09-06T12:35:19Z, the modern simulator fixture.
  constexpr int64_t capture = 1788698119;
  constexpr uint32_t window = 4 * 60 * 60;
  using Casic::Eph::Freshness;

  check(Casic::Eph::freshness(capture, capture, window) == Freshness::REPLAY,
        "the same instant replays");
  check(Casic::Eph::freshness(capture, capture + window - 1, window) == Freshness::REPLAY,
        "one second inside the window replays");
  check(Casic::Eph::freshness(capture, capture + window, window) == Freshness::REPLAY,
        "the window boundary is inclusive");
  check(Casic::Eph::freshness(capture, capture + window + 1, window) == Freshness::TOO_OLD,
        "one second past the window is too old");
  check(Casic::Eph::freshness(capture, capture - 1, window) == Freshness::IMPLAUSIBLE,
        "a receiver clock behind the capture cannot bound an age");
  check(Casic::Eph::freshness(0, capture, window) == Freshness::IMPLAUSIBLE,
        "a capture before the GPS epoch is not a timestamp");
  check(Casic::Eph::freshness(capture, 0, window) == Freshness::IMPLAUSIBLE,
        "a receiver time before the GPS epoch is not a timestamp");

  // A week unpowered. furble's own kept clock reports this as about two
  // minutes old, because it restores as the last persisted epoch plus the
  // monotonic time since boot and knows nothing about the time it was off.
  // The receiver reports it correctly, which is why the receiver is the clock
  // this rule uses.
  check(Casic::Eph::freshness(capture, capture + (7 * 24 * 60 * 60), window) == Freshness::TOO_OLD,
        "a week later is too old");

  return g_Failures == before;
}

bool testMonHwEdges() {
  std::cout << "test: parseMonHw zeroes every field when the payload is short\n";
  const int before = g_Failures;

  const std::vector<uint8_t> shortBuf(55, 0xFF);
  const Casic::MonHw hw = Casic::parseMonHw(shortBuf.data(), shortBuf.size());
  check(!hw.valid, "55 bytes is one short of a MON-HW payload");
  check(hw.noise == 0, "a short payload leaves noise zeroed");
  check(hw.agc == 0, "a short payload leaves agc zeroed");
  check(hw.antenna_status == 0, "a short payload leaves antenna_status zeroed");
  check(hw.jam_indicator == 0, "a short payload leaves jam_indicator zeroed");
  check(!Casic::parseMonHw(shortBuf.data(), 0).valid, "a zero length payload is invalid");
  check(!Casic::parseMonHw(nullptr, 0).valid, "a null zero length payload is invalid");

  std::vector<uint8_t> longBuf(64, 0);
  longBuf[0] = 0x01;
  longBuf[8] = 0x02;
  const Casic::MonHw longHw = Casic::parseMonHw(longBuf.data(), longBuf.size());
  check(longHw.valid, "a payload longer than 56 bytes is still decoded");
  check((longHw.noise == 1) && (longHw.antenna_status == 2),
        "the trailing bytes past the decoded subset are ignored");

  return g_Failures == before;
}

// g. Every talker prefix in the implementation, plus an unknown pair.
bool testConstellationForTalker() {
  std::cout << "test: every talker prefix maps to its constellation\n";
  const int before = g_Failures;

  check(Casic::constellationForTalker('G', 'P') == Casic::CONSTELLATION_GPS, "GP is GPS");
  check(Casic::constellationForTalker('G', 'L') == Casic::CONSTELLATION_GLONASS, "GL is GLONASS");
  check(Casic::constellationForTalker('G', 'A') == Casic::CONSTELLATION_GALILEO, "GA is Galileo");
  check(Casic::constellationForTalker('B', 'D') == Casic::CONSTELLATION_BEIDOU,
        "BD is BeiDou on older firmware");
  check(Casic::constellationForTalker('G', 'B') == Casic::CONSTELLATION_BEIDOU,
        "GB is BeiDou on newer firmware");
  check(Casic::constellationForTalker('G', 'Q') == Casic::CONSTELLATION_QZSS, "GQ is QZSS");
  check(Casic::constellationForTalker('P', 'Q') == Casic::CONSTELLATION_QZSS, "PQ is QZSS");
  check(Casic::constellationForTalker('Q', 'Z') == Casic::CONSTELLATION_QZSS, "QZ is QZSS");
  check(Casic::constellationForTalker('G', 'N') == Casic::CONSTELLATION_UNKNOWN,
        "GN is the combined talker, not a constellation");
  check(Casic::constellationForTalker('X', 'Y') == Casic::CONSTELLATION_UNKNOWN,
        "an unknown pair maps to unknown");
  check(Casic::constellationForTalker('g', 'p') == Casic::CONSTELLATION_UNKNOWN,
        "the talker mapping is case sensitive");

  return g_Failures == before;
}

// h. A deterministic fuzz loop. Every mutation is a shape a noisy UART or a
// half written NVS blob can hand the parsers, so the invariants below must
// hold for any byte string at all.
void mutate(std::string &subject, std::mt19937 &rng) {
  if (subject.empty()) {
    return;
  }
  switch (rng() % 4) {
    case 0:
    {
      // flip one bit
      const size_t at = rng() % subject.size();
      subject[at] = static_cast<char>(static_cast<uint8_t>(subject[at]) ^ (1u << (rng() % 8)));
      break;
    }
    case 1:
      // truncate
      subject.resize(rng() % subject.size());
      break;
    case 2:
      // insert one byte
      subject.insert(rng() % subject.size(), 1, static_cast<char>(rng() & 0xff));
      break;
    default:
    {
      // delete a comma separated field, or one byte when there is no field
      const size_t comma = subject.find(',');
      if (comma == std::string::npos) {
        subject.erase(rng() % subject.size(), 1);
      } else {
        const size_t next = subject.find(',', comma + 1);
        subject.erase(comma, (next == std::string::npos) ? std::string::npos : (next - comma));
      }
      break;
    }
  }
}

bool testFuzz() {
  std::cout << "test: a deterministic fuzz loop holds every invariant and terminates\n";
  const int before = g_Failures;

  const std::vector<uint8_t> ephFrame =
      Casic::frame(Casic::Eph::CLASS, Casic::Eph::EPH_ID, std::vector<uint8_t>(72, 0x5A));
  const std::vector<uint8_t> ionFrame =
      Casic::frame(Casic::Eph::CLASS, Casic::Eph::ION_ID, std::vector<uint8_t>(16, 0x11));
  std::vector<uint8_t> binaryStream = ephFrame;
  binaryStream.insert(binaryStream.end(), ionFrame.begin(), ionFrame.end());

  const std::string valid = nmea(gsvBody("GP", "1", "1", "04", quadRun(1, 45, 4)));
  std::vector<std::string> seeds;
  seeds.push_back(valid);
  seeds.push_back(nmea(gsvBody("BD", "2", "1", "08", quadRun(11, 40, 4))));
  seeds.push_back(nmea(gsaBody("GN", {"01", "02", "03"}, "2.5", "2.0", "1.5", "1")));
  seeds.push_back(std::string(reinterpret_cast<const char *>(ephFrame.data()), ephFrame.size()));
  seeds.push_back(
      std::string(reinterpret_cast<const char *>(binaryStream.data()), binaryStream.size()));
  seeds.push_back(std::string(56, '\x7f'));

  std::mt19937 rng(0xCA51C0DEu);
  Casic::NmeaSatellites sats;
  Casic::EphemerisCollector collector;
  bool satsBounded = true;
  bool collectorBounded = true;
  bool spansSane = true;
  bool monHwGuarded = true;

  for (unsigned i = 0; i < 4000; i++) {
    std::string subject;
    if ((i % 3) == 0) {
      // pure random bytes
      subject.resize(rng() % 128);
      for (char &c : subject) {
        c = static_cast<char>(rng() & 0xff);
      }
    } else {
      // a mutated fixture, which stays close enough to valid to reach deeper
      subject = seeds[rng() % seeds.size()];
      const unsigned rounds = 1 + (rng() % 3);
      for (unsigned round = 0; round < rounds; round++) {
        mutate(subject, rng);
      }
    }

    // Keep real satellites and stored frames present so the cap and budget
    // invariants have something to hold on to while the damaged input arrives.
    // A mutated frame practically never balances its checksum, so the store
    // would otherwise stay empty for the whole loop.
    if ((i % 50) == 0) {
      sats.feed(valid);
      collector.feed(ephFrame.data(), ephFrame.size());
    }

    sats.feed(subject);
    if ((sats.satellites().size() > Casic::NmeaSatellites::MAX_SATELLITES)
        || (sats.used() > sats.inView())) {
      satsBounded = false;
    }

    const uint8_t *bytes = reinterpret_cast<const uint8_t *>(subject.data());
    collector.feed(bytes, subject.size());
    if (collector.data().size() > Casic::EphemerisCollector::MAX_BYTES) {
      collectorBounded = false;
    }

    size_t previousEnd = 0;
    for (const std::pair<size_t, size_t> &span : Casic::splitFrames(bytes, subject.size())) {
      if ((span.first < previousEnd) || (span.second == 0)
          || ((span.first + span.second) > subject.size())) {
        spansSane = false;
      }
      previousEnd = span.first + span.second;
    }

    if (Casic::parseMonHw(bytes, subject.size()).valid && (subject.size() < 56)) {
      monHwGuarded = false;
    }

    if ((i % 500) == 499) {
      sats.clear();
    }
  }

  check(satsBounded, "the satellite table never exceeds MAX_SATELLITES under fuzz");
  check(collectorBounded, "the ephemeris store never exceeds MAX_BYTES under fuzz");
  check(spansSane, "fuzzed splitFrames spans are ascending, non-overlapping and inside the buffer");
  check(monHwGuarded, "parseMonHw never reports valid for a payload shorter than 56 bytes");

  // The parsers are still correct after four thousand damaged inputs.
  Casic::NmeaSatellites after;
  check(after.feed(valid), "a valid sentence still parses after the fuzz loop");
  check(after.inView() == 4, "the valid sentence publishes its four satellites");
  Casic::EphemerisCollector store;
  check(store.feed(ephFrame.data(), ephFrame.size()),
        "a valid assistance frame still stores after the fuzz loop");

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
  testGsaSystemId();
  testNmeaMalformed();
  testEphemerisCollector();
  testMonHw();
  testChecksumEdges();
  testAutobaudLocksAtEveryLadderRate();
  testAutobaudOneSentencePerStep();
  testAutobaudCounterWrap();
  testAutobaudServiceBeforeBegin();
  testNmeaFraming();
  testGsvHeaderGuards();
  testGsvQuadGuards();
  testGsvSetAssembly();
  testSatelliteOrderingAndCap();
  testGsaGuards();
  testGsaCombinedThenPerSystem();
  testEphemerisMalformed();
  testEphemerisBudget();
  testSplitFramesEdges();
  testEphemerisFreshness();
  testMonHwEdges();
  testConstellationForTalker();
  testFuzz();

  if (g_Failures != 0) {
    std::cout << "gps casic harness: FAIL (" << g_Failures << " of " << g_Checks << " checks)\n";
    return 1;
  }
  std::cout << "gps casic harness: PASS (" << g_Checks << " checks)\n";
  return 0;
}
