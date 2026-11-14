// Host unit test for the pure UBX codec in lib/furble/protocol/GpsUbx.{h,cpp}.
//
// Coverage:
//   a. checksum + frame   known poll frames and the Fletcher arithmetic
//   b. scan + Stream      partial frames, resync after garbage, oversize length,
//                         bad checksum, frames interleaved with NMEA and CASIC
//   c. key encoding       every value size and every key furble uses
//   d. every message      ACK, VALSET, VALGET, VALDEL, NAV-PVT, NAV-SAT,
//                         MON-VER, UPD-SOS and RXM-PMREQ round trips and guards

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "GpsUbx.h"

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

std::vector<std::vector<uint8_t>> split(const std::vector<uint8_t> &bytes) {
  Ubx::Stream stream;
  std::vector<std::vector<uint8_t>> frames;
  for (const uint8_t b : bytes) {
    if (stream.push(b)) {
      frames.push_back(stream.frame());
    }
  }
  return frames;
}

void append(std::vector<uint8_t> &out, const std::vector<uint8_t> &more) {
  out.insert(out.end(), more.begin(), more.end());
}

void testChecksumAndFrame() {
  // The MON-VER and NAV-PVT polls are the two frames every UBX guide prints.
  check(Ubx::pollMonVer() == std::vector<uint8_t>({0xB5, 0x62, 0x0A, 0x04, 0x00, 0x00, 0x0E, 0x34}),
        "MON-VER poll frame matches the published bytes");
  check(Ubx::frame(Ubx::CLASS_NAV, Ubx::ID_NAV_PVT, {})
            == std::vector<uint8_t>({0xB5, 0x62, 0x01, 0x07, 0x00, 0x00, 0x08, 0x19}),
        "NAV-PVT poll frame matches the published bytes");

  // Fletcher: A sums the bytes, B sums the running A, both mod 256.
  const uint8_t data[] = {0xff, 0xff, 0xff};
  const auto sum = Ubx::checksum(data, sizeof(data));
  check((sum[0] == 0xfd) && (sum[1] == 0xfa), "checksum wraps modulo 256");
  const auto empty = Ubx::checksum(data, 0);
  check((empty[0] == 0) && (empty[1] == 0), "empty checksum is zero");

  check(Ubx::frame(1, 2, std::vector<uint8_t>(0x10000, 0)).empty(),
        "an oversize payload is refused");
  const auto framed = Ubx::frame(0x06, 0x8A, {1, 2, 3});
  check(framed.size() == 11 && framed[4] == 3 && framed[5] == 0, "length is little endian");
}

void testScanAndStream() {
  const auto pvt = Ubx::frame(Ubx::CLASS_NAV, Ubx::ID_NAV_PVT, std::vector<uint8_t>(92, 7));

  // Every proper prefix waits; the whole frame is reported with its length.
  for (size_t i = 0; i < pvt.size(); i++) {
    check(Ubx::scan(pvt.data(), i).status == Ubx::Scan::NEED_MORE, "prefix needs more");
  }
  const auto whole = Ubx::scan(pvt.data(), pvt.size());
  check(whole.status == Ubx::Scan::FRAME && whole.length == pvt.size(), "whole frame found");

  // A wrong second sync byte, an oversize length and a bad checksum are BAD.
  std::vector<uint8_t> bad = pvt;
  bad[1] = 0x63;
  check(Ubx::scan(bad.data(), bad.size()).status == Ubx::Scan::BAD, "bad second sync");
  bad = pvt;
  bad[4] = 0xff;
  bad[5] = 0x7f;
  check(Ubx::scan(bad.data(), bad.size()).status == Ubx::Scan::BAD, "oversize length");
  bad = pvt;
  bad.back() ^= 1;
  check(Ubx::scan(bad.data(), bad.size()).status == Ubx::Scan::BAD, "bad checksum");
  const uint8_t notSync = 'x';
  check(Ubx::scan(&notSync, 1).status == Ubx::Scan::BAD, "a non-sync byte is BAD");

  // Resync: garbage, a partial frame, NMEA, a stray sync pair and a CASIC
  // frame around two good frames. Exactly the two good frames come out.
  std::vector<uint8_t> stream = {0x00, 0xB5, 0xB5, 0x62, 0x01};
  const std::string nmea = "$GPGGA,1,2,3*00\r\n";
  append(stream, std::vector<uint8_t>(nmea.begin(), nmea.end()));
  append(stream, pvt);
  append(stream, {0xBA, 0xCE, 0x04, 0x00, 0x06, 0x01, 1, 2, 3, 4, 0, 0, 0, 0});
  bad = pvt;
  bad[10] ^= 0x55;
  append(stream, bad);
  const auto ack = Ubx::frame(Ubx::CLASS_ACK, Ubx::ID_ACK_ACK, {0x06, 0x8A});
  append(stream, ack);
  const auto frames = split(stream);
  check(frames.size() == 2, "two frames survive the garbage");
  check(frames.size() == 2 && frames[0] == pvt && frames[1] == ack, "frames are intact");

  // A frame whose header hides inside a rejected oversize header is found.
  std::vector<uint8_t> nested = {0xB5, 0x62, 0x01, 0x07, 0xff, 0xff};
  append(nested, ack);
  check(split(nested).size() == 1, "frame inside a rejected header is found");

  // Byte-by-byte delivery equals one-shot delivery.
  Ubx::Stream s;
  size_t count = 0;
  for (const uint8_t b : ack) {
    count += s.push(b) ? 1 : 0;
  }
  check(count == 1 && s.cls() == Ubx::CLASS_ACK && s.id() == Ubx::ID_ACK_ACK
            && s.payloadLength() == 2 && s.payload()[1] == 0x8A,
        "stream accessors");
  s.clear();
  check(s.frame().empty(), "clear drops the frame");

  // The stream counts rejected framed attempts but not loose noise.
  Ubx::Stream counted;
  for (const uint8_t b : bad) {
    counted.push(b);
  }
  for (const uint8_t b : std::string("noise")) {
    counted.push(static_cast<uint8_t>(b));
  }
  check(counted.rejected() == 1, "one rejected frame counted");

  // The empty-payload maximum and the MAX_PAYLOAD boundary.
  const auto max = Ubx::frame(1, 2, std::vector<uint8_t>(Ubx::MAX_PAYLOAD, 1));
  check(Ubx::scan(max.data(), max.size()).status == Ubx::Scan::FRAME, "MAX_PAYLOAD accepted");
  const auto over = Ubx::frame(1, 2, std::vector<uint8_t>(Ubx::MAX_PAYLOAD + 1, 1));
  check(Ubx::scan(over.data(), over.size()).status == Ubx::Scan::BAD, "MAX_PAYLOAD + 1 refused");
}

void testKeys() {
  check(Ubx::valueSize(0x10000000) == 1, "size 1 is one bit in one byte");
  check(Ubx::valueSize(0x20000000) == 1, "size 2 is one byte");
  check(Ubx::valueSize(0x30000000) == 2, "size 3 is two bytes");
  check(Ubx::valueSize(0x40000000) == 4, "size 4 is four bytes");
  check(Ubx::valueSize(0x50000000) == 8, "size 5 is eight bytes");
  check(Ubx::valueSize(0x00000000) == 0, "size 0 is reserved");
  check(Ubx::valueSize(0x60000000) == 0, "size 6 is reserved");
  check(Ubx::valueSize(0x70000000) == 0, "size 7 is reserved");

  // Each key's size field agrees with the type the interface description lists.
  struct Expect {
    uint32_t key;
    size_t size;
  };
  const Expect keys[] = {
      {Ubx::Key::RATE_MEAS,            2},
      {Ubx::Key::RATE_NAV,             2},
      {Ubx::Key::NAVSPG_DYNMODEL,      1},
      {Ubx::Key::SIGNAL_GPS_ENA,       1},
      {Ubx::Key::SIGNAL_GPS_L1CA,      1},
      {Ubx::Key::SIGNAL_SBAS_ENA,      1},
      {Ubx::Key::SIGNAL_SBAS_L1CA,     1},
      {Ubx::Key::SIGNAL_GAL_ENA,       1},
      {Ubx::Key::SIGNAL_GAL_E1,        1},
      {Ubx::Key::SIGNAL_BDS_ENA,       1},
      {Ubx::Key::SIGNAL_BDS_B1,        1},
      {Ubx::Key::SIGNAL_BDS_B1C,       1},
      {Ubx::Key::SIGNAL_QZSS_ENA,      1},
      {Ubx::Key::SIGNAL_QZSS_L1CA,     1},
      {Ubx::Key::SIGNAL_QZSS_L1S,      1},
      {Ubx::Key::SIGNAL_GLO_ENA,       1},
      {Ubx::Key::SIGNAL_GLO_L1,        1},
      {Ubx::Key::PM_OPERATEMODE,       1},
      {Ubx::Key::PM_POSUPDATEPERIOD,   4},
      {Ubx::Key::PM_ACQPERIOD,         4},
      {Ubx::Key::PM_ONTIME,            2},
      {Ubx::Key::PM_DONOTENTEROFF,     1},
      {Ubx::Key::PM_WAITTIMEFIX,       1},
      {Ubx::Key::PM_UPDATEEPH,         1},
      {Ubx::Key::ANA_USE_ANA,          1},
      {Ubx::Key::MSGOUT_NAV_PVT_UART1, 1},
      {Ubx::Key::MSGOUT_NAV_SAT_UART1, 1},
      {Ubx::Key::UART1OUTPROT_UBX,     1},
      {Ubx::Key::UART1OUTPROT_NMEA,    1},
  };
  for (const auto &expect : keys) {
    check(Ubx::valueSize(expect.key) == expect.size, "key size matches its type");
  }
}

void testValSetGetDel() {
  const std::vector<Ubx::KeyValue> items = {
      {Ubx::Key::RATE_MEAS,          200                  },
      {Ubx::Key::NAVSPG_DYNMODEL,    3                    },
      {Ubx::Key::PM_POSUPDATEPERIOD, 10                   },
      {0x50000001,                   0x0102030405060708ULL},
  };
  const auto set = Ubx::valSet(Ubx::LAYER_RAM | Ubx::LAYER_BBR, items);
  check(set.size() == 8 + 4 + (4 + 2) + (4 + 1) + (4 + 4) + (4 + 8), "VALSET length");
  check(set[2] == Ubx::CLASS_CFG && set[3] == Ubx::ID_CFG_VALSET, "VALSET header");
  check(set[6] == 0 && set[7] == 3, "VALSET version 0 and layers");
  // RATE_MEAS key little endian then value 200 in two bytes.
  check(set[10] == 0x01 && set[13] == 0x30 && set[14] == 200 && set[15] == 0, "first item bytes");

  uint8_t layers = 0;
  std::vector<Ubx::KeyValue> parsed;
  check(Ubx::parseValSet(set.data() + 6, set.size() - 8, layers, parsed), "VALSET parses back");
  check(layers == 3 && parsed.size() == 4 && parsed[3].value == 0x0102030405060708ULL
            && parsed[0].value == 200,
        "VALSET round trip");

  // A value wider than its key is truncated to the key's size.
  const auto wide = Ubx::valSet(Ubx::LAYER_RAM, {
                                                    {Ubx::Key::NAVSPG_DYNMODEL, 0x1234}
  });
  check(
      Ubx::parseValSet(wide.data() + 6, wide.size() - 8, layers, parsed) && parsed[0].value == 0x34,
      "a wide value is truncated");

  check(Ubx::valSet(Ubx::LAYER_RAM, {}).empty(), "empty VALSET refused");
  check(Ubx::valSet(0, items).empty(), "no layer refused");
  check(Ubx::valSet(0x08, items).empty(), "unknown layer bit refused");
  check(Ubx::valSet(Ubx::LAYER_RAM,
                    {
                        {0x00000001, 1}
  })
            .empty(),
        "reserved size refused");
  check(Ubx::valSet(Ubx::LAYER_RAM,
                    std::vector<Ubx::KeyValue>(Ubx::MAX_KEYS + 1, {Ubx::Key::RATE_MEAS, 1}))
            .empty(),
        "65 keys refused");

  const auto get = Ubx::valGet(Ubx::GET_RAM, {Ubx::Key::RATE_MEAS, Ubx::Key::ANA_USE_ANA});
  check(get.size() == 8 + 4 + 8 && get[3] == Ubx::ID_CFG_VALGET && get[7] == 0, "VALGET poll");
  uint8_t layer = 99;
  std::vector<uint32_t> keys;
  check(Ubx::parseKeyList(get.data() + 6, get.size() - 8, layer, keys) && layer == 0
            && keys.size() == 2 && keys[1] == Ubx::Key::ANA_USE_ANA,
        "VALGET poll parses back");
  check(Ubx::valGet(3, {Ubx::Key::RATE_MEAS}).empty(), "VALGET layer code 3 refused");
  check(Ubx::valGet(Ubx::GET_DEFAULT, {}).empty(), "empty VALGET refused");

  const auto response = Ubx::valGetResponse(Ubx::GET_RAM, items);
  Ubx::ValGet value = {};
  check(Ubx::parseValGet(response.data() + 6, response.size() - 8, value), "VALGET response");
  check(value.items.size() == 4 && value.items[1].value == 3, "VALGET response round trip");
  std::vector<uint8_t> truncated(response.begin() + 6, response.end() - 3);
  check(!Ubx::parseValGet(truncated.data(), truncated.size(), value), "short value refused");
  std::vector<uint8_t> version0(response.begin() + 6, response.end() - 2);
  version0[0] = 0;
  check(!Ubx::parseValGet(version0.data(), version0.size(), value), "poll version refused");
  const uint8_t reserved[] = {1, 0, 0, 0, 0x01, 0x00, 0x00, 0x00, 9};
  check(!Ubx::parseValGet(reserved, sizeof(reserved), value), "reserved key size refused");

  const auto del = Ubx::valDel(Ubx::LAYER_BBR, {Ubx::Key::PM_OPERATEMODE});
  check(del.size() == 16 && del[3] == Ubx::ID_CFG_VALDEL && del[7] == Ubx::LAYER_BBR, "VALDEL");
  check(Ubx::valDel(Ubx::LAYER_RAM, {Ubx::Key::PM_OPERATEMODE}).empty(), "RAM VALDEL refused");
  check(!Ubx::parseKeyList(del.data() + 6, 7, layer, keys), "ragged key list refused");
}

void testAck() {
  Ubx::Ack ack = {};
  const uint8_t payload[] = {0x06, 0x8A};
  check(Ubx::parseAck(Ubx::ID_ACK_ACK, payload, 2, ack) && ack.ack && ack.cls == 0x06
            && ack.id == 0x8A,
        "ACK-ACK");
  check(Ubx::parseAck(Ubx::ID_ACK_NAK, payload, 2, ack) && !ack.ack, "ACK-NAK");
  check(!Ubx::parseAck(Ubx::ID_ACK_ACK, payload, 1, ack), "short ACK refused");
  check(!Ubx::parseAck(0x02, payload, 2, ack), "unknown ACK id refused");
}

Ubx::NavPvt samplePvt() {
  Ubx::NavPvt pvt = {};
  pvt.iTOW = 123456000;
  pvt.year = 2026;
  pvt.month = 9;
  pvt.day = 6;
  pvt.hour = 12;
  pvt.min = 35;
  pvt.sec = 19;
  pvt.valid = 0x07;
  pvt.tAcc = 25;
  pvt.nano = -120000;
  pvt.fixType = 3;
  pvt.flags = 0x01 | (2 << 2);
  pvt.numSV = 14;
  pvt.lon = 115166667;
  pvt.lat = 481173000;
  pvt.height = 592300;
  pvt.hMSL = 545400;
  pvt.hAcc = 1800;
  pvt.vAcc = 2600;
  pvt.gSpeed = 6300;
  pvt.headMot = 4500000;
  pvt.sAcc = 300;
  pvt.pDOP = 145;
  return pvt;
}

void testNavPvt() {
  const Ubx::NavPvt pvt = samplePvt();
  const auto bytes = Ubx::encodeNavPvt(pvt);
  check(bytes.size() == 100, "NAV-PVT frame is 92 + 8 bytes");
  Ubx::NavPvt back = {};
  check(Ubx::parseNavPvt(bytes.data() + 6, 92, back), "NAV-PVT parses");
  check(back.lat == pvt.lat && back.lon == pvt.lon && back.hMSL == pvt.hMSL && back.hAcc == pvt.hAcc
            && back.vAcc == pvt.vAcc && back.tAcc == pvt.tAcc && back.nano == pvt.nano
            && back.numSV == 14 && back.pDOP == 145 && back.gSpeed == 6300
            && back.headMot == 4500000 && back.sAcc == 300,
        "NAV-PVT round trip");
  check(back.positionValid() && back.timeValid() && back.psmState() == 2, "NAV-PVT flags");
  // Field offsets against the interface description table.
  check(bytes[6 + 20] == 3 && bytes[6 + 23] == 14 && bytes[6 + 11] == 7, "NAV-PVT offsets");

  Ubx::NavPvt flagged = pvt;
  flagged.flags = 0;
  check(!flagged.positionValid(), "no gnssFixOK, no position");
  flagged = pvt;
  flagged.fixType = 5;
  check(!flagged.positionValid(), "time-only fix has no position");
  flagged.fixType = 1;
  check(!flagged.positionValid(), "dead reckoning only has no position");
  flagged = pvt;
  flagged.flags3 = 1;
  check(!flagged.positionValid(), "invalidLlh voids the position");
  flagged = pvt;
  flagged.valid = 0x03;
  check(!flagged.timeValid(), "an unresolved time is not usable");
  flagged = pvt;
  flagged.month = 13;
  check(!flagged.timeValid(), "an impossible month is not usable");
  check(!Ubx::parseNavPvt(bytes.data() + 6, 91, back), "short NAV-PVT refused");
  check(!Ubx::parseNavPvt(bytes.data() + 6, 93, back), "long NAV-PVT refused");
}

void testNavSat() {
  const std::vector<Ubx::NavSatSv> svs = {
      {Ubx::GNSS_GPS,     5,  41, 45, 120, 0x0f},
      {Ubx::GNSS_GALILEO, 11, 37, -3, 300, 0x04},
      {Ubx::GNSS_BEIDOU,  23, 0,  12, 7,   0x00},
  };
  const auto bytes = Ubx::encodeNavSat(1000, svs);
  std::vector<Ubx::NavSatSv> back;
  check(Ubx::parseNavSat(bytes.data() + 6, bytes.size() - 8, back) && back.size() == 3,
        "NAV-SAT parses");
  check(back[0].used() && !back[1].used() && back[1].elev == -3 && back[1].azim == 300
            && back[2].gnssId == Ubx::GNSS_BEIDOU,
        "NAV-SAT round trip");
  check(!Ubx::parseNavSat(bytes.data() + 6, bytes.size() - 9, back) && back.empty(),
        "count disagreeing with length refused");
  std::vector<uint8_t> version(bytes.begin() + 6, bytes.end() - 2);
  version[4] = 2;
  check(!Ubx::parseNavSat(version.data(), version.size(), back), "unknown version refused");
  const auto none = Ubx::encodeNavSat(0, {});
  check(Ubx::parseNavSat(none.data() + 6, 8, back) && back.empty(), "zero satellites");
  check(Ubx::encodeNavSat(0, std::vector<Ubx::NavSatSv>(256)).empty(), "256 satellites refused");
}

void testMonVer() {
  Ubx::MonVer ver = {
      "ROM SPG 5.10 (7b202e)",
      "000A0000",
      {"FWVER=SPG 5.10", "PROTVER=34.10", "MOD=MAX-M10S", "GPS;GLO;GAL;BDS"}
  };
  const auto bytes = Ubx::encodeMonVer(ver);
  check(bytes.size() == 8 + 40 + (4 * 30), "MON-VER length");
  Ubx::MonVer back;
  check(Ubx::parseMonVer(bytes.data() + 6, bytes.size() - 8, back), "MON-VER parses");
  check(back.sw == ver.sw && back.hw == ver.hw && back.extensions.size() == 4, "MON-VER fields");
  check(back.field("FWVER") == "SPG 5.10" && back.field("MOD") == "MAX-M10S"
            && back.field("PROTVER") == "34.10" && back.field("NONE").empty(),
        "MON-VER extension lookup");
  check(!Ubx::parseMonVer(bytes.data() + 6, 39, back), "short MON-VER refused");
  check(!Ubx::parseMonVer(bytes.data() + 6, 41, back), "ragged MON-VER refused");

  // A field that fills its width with no terminator is kept whole.
  std::vector<uint8_t> full(40, 'A');
  check(Ubx::parseMonVer(full.data(), full.size(), back) && back.sw.size() == 30
            && back.hw.size() == 10,
        "unterminated fixed strings are bounded");
}

void testSosAndPmreq() {
  check(Ubx::sosCommand(Ubx::SOS_CREATE)
            == std::vector<uint8_t>(
                {0xB5, 0x62, 0x09, 0x14, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x21, 0xEC}),
        "SOS create frame");
  check(Ubx::sosCommand(Ubx::SOS_CLEAR).size() == 12, "SOS clear frame");
  check(Ubx::sosPoll() == std::vector<uint8_t>({0xB5, 0x62, 0x09, 0x14, 0x00, 0x00, 0x1D, 0x60}),
        "SOS poll frame");
  // CFG-RST warm start, GNSS-only software reset: 01 00 02 00.
  const auto rst = Ubx::cfgRst(Ubx::RST_WARM, Ubx::RST_MODE_GNSS_ONLY);
  check(rst.size() == 12 && rst[3] == Ubx::ID_CFG_RST && rst[6] == 1 && rst[7] == 0 && rst[8] == 2
            && rst[9] == 0,
        "CFG-RST layout");
  check(Ubx::cfgRst(Ubx::RST_COLD, Ubx::RST_MODE_GNSS_STOP)[7] == 0xff, "cold mask high byte");
  check(Ubx::sosCommand(Ubx::SOS_ACK).empty(), "an output is not a command");

  Ubx::Sos sos = {};
  const auto restored = Ubx::sosOutput(Ubx::SOS_RESTORED, Ubx::SOS_RESTORE_OK);
  check(Ubx::parseSos(restored.data() + 6, 8, sos) && sos.cmd == Ubx::SOS_RESTORED
            && sos.response == Ubx::SOS_RESTORE_OK,
        "SOS restored");
  const auto acked = Ubx::sosOutput(Ubx::SOS_ACK, 1);
  check(Ubx::parseSos(acked.data() + 6, 8, sos) && sos.cmd == Ubx::SOS_ACK && sos.response == 1,
        "SOS acknowledge");
  check(!Ubx::parseSos(acked.data() + 6, 4, sos), "short SOS refused");
  const auto command = Ubx::sosOutput(Ubx::SOS_CREATE, 0);
  check(!Ubx::parseSos(command.data() + 6, 8, sos), "a command is not an output");

  const Ubx::PmReq request = {5000, Ubx::PMREQ_FLAG_BACKUP | Ubx::PMREQ_FLAG_FORCE,
                              Ubx::PMREQ_WAKE_UARTRX};
  const auto bytes = Ubx::pmReq(request);
  check(bytes.size() == 24 && bytes[2] == Ubx::CLASS_RXM && bytes[3] == Ubx::ID_RXM_PMREQ
            && bytes[6] == 0 && bytes[10] == 0x88 && bytes[11] == 0x13,
        "PMREQ 16-byte layout");
  Ubx::PmReq back = {};
  check(Ubx::parsePmReq(bytes.data() + 6, 16, back) && back.duration_ms == 5000 && back.flags == 6
            && back.wakeup_sources == 8,
        "PMREQ round trip");
  // M10 documents only the 16-byte form, so the legacy 8-byte one is refused.
  const uint8_t legacy[] = {0x10, 0x27, 0, 0, 0x02, 0, 0, 0};
  check(!Ubx::parsePmReq(legacy, 8, back), "PMREQ 8-byte form refused");
  std::vector<uint8_t> versioned(bytes.begin() + 6, bytes.end() - 2);
  versioned[0] = 1;
  check(!Ubx::parsePmReq(versioned.data(), 16, back), "unknown PMREQ version refused");
  check(!Ubx::parsePmReq(legacy, 12, back), "ragged PMREQ refused");
}

}  // namespace

int main() {
  testChecksumAndFrame();
  testScanAndStream();
  testKeys();
  testValSetGetDel();
  testAck();
  testNavPvt();
  testNavSat();
  testMonVer();
  testSosAndPmreq();

  if (g_Failures != 0) {
    std::cout << "gps ubx harness: FAIL (" << g_Failures << " of " << g_Checks << " checks)\n";
    return 1;
  }
  std::cout << "gps ubx harness: PASS (" << g_Checks << " checks)\n";
  return 0;
}
