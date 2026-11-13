// Seeded UBX parser fuzzer.
//
// Each iteration builds a receiver byte stream from valid frames of every
// message furble decodes, NMEA text, CASIC frames, random noise and mutations
// of all of those (bit flips, truncation, length and checksum corruption,
// spliced halves), then feeds it through Ubx::Stream byte by byte and hands
// every frame to every decoder. ASan and UBSan catch an out-of-bounds read.
// The invariants checked here catch the logic faults:
//   1. every frame the stream yields passes scan() and its checksum,
//   2. no yielded payload exceeds MAX_PAYLOAD,
//   3. every unmutated frame placed in a stream with no stray sync bytes
//      before it is yielded intact, in order,
//   4. the encoders round trip through the decoders.
//
// Usage: gps_ubx_fuzz --seed N --iterations N

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>

#include "GpsUbx.h"

using namespace Furble;

namespace {

int g_Failures = 0;

void check(bool condition, const char *message, uint32_t seed, uint32_t iteration) {
  if (!condition) {
    std::fprintf(stderr, "FAIL seed %u iteration %u: %s\n", seed, iteration, message);
    g_Failures++;
  }
}

std::vector<uint8_t> randomBytes(std::mt19937 &rng, size_t count) {
  std::vector<uint8_t> out(count);
  for (auto &b : out) {
    b = static_cast<uint8_t>(rng());
  }
  return out;
}

/** A valid frame of a message furble decodes, with randomised content. */
std::vector<uint8_t> validFrame(std::mt19937 &rng) {
  switch (rng() % 9) {
    case 0:
    {
      Ubx::NavPvt pvt = {};
      const auto raw = randomBytes(rng, Ubx::NavPvt::LENGTH);
      Ubx::parseNavPvt(raw.data(), raw.size(), pvt);
      return Ubx::encodeNavPvt(pvt);
    }
    case 1:
    {
      std::vector<Ubx::NavSatSv> svs(rng() % 40);
      for (auto &sv : svs) {
        sv = {static_cast<uint8_t>(rng() % 7),   static_cast<uint8_t>(rng()),
              static_cast<uint8_t>(rng() % 60),  static_cast<int8_t>(rng()),
              static_cast<int16_t>(rng() % 360), static_cast<uint32_t>(rng())};
      }
      return Ubx::encodeNavSat(static_cast<uint32_t>(rng()), svs);
    }
    case 2:
    {
      Ubx::MonVer ver;
      ver.sw = "ROM SPG 5.10";
      ver.hw = "000A0000";
      for (size_t i = rng() % 8; i > 0; i--) {
        const auto raw = randomBytes(rng, rng() % 31);
        ver.extensions.emplace_back(raw.begin(), raw.end());
      }
      return Ubx::encodeMonVer(ver);
    }
    case 3:
      return Ubx::frame(Ubx::CLASS_ACK, (rng() & 1) ? Ubx::ID_ACK_ACK : Ubx::ID_ACK_NAK,
                        randomBytes(rng, 2));
    case 4:
      return Ubx::sosOutput((rng() & 1) ? Ubx::SOS_ACK : Ubx::SOS_RESTORED,
                            static_cast<uint8_t>(rng() % 4));
    case 5:
    {
      std::vector<Ubx::KeyValue> items(1 + (rng() % 10));
      for (auto &item : items) {
        item = {(static_cast<uint32_t>(1 + (rng() % 5)) << 28) | (rng() & 0x0fffffff),
                (static_cast<uint64_t>(rng()) << 32) | rng()};
      }
      return Ubx::valGetResponse(static_cast<uint8_t>(rng() % 8), items);
    }
    case 6:
      return Ubx::pmReq({static_cast<uint32_t>(rng()), static_cast<uint32_t>(rng()),
                         static_cast<uint32_t>(rng())});
    case 7:
      return Ubx::valSet(Ubx::LAYER_RAM, {
                                             {Ubx::Key::RATE_MEAS, rng() % 2000}
      });
    default:
      return Ubx::frame(static_cast<uint8_t>(rng()), static_cast<uint8_t>(rng()),
                        randomBytes(rng, rng() % 64));
  }
}

std::vector<uint8_t> noise(std::mt19937 &rng) {
  switch (rng() % 4) {
    case 0:
    {
      const std::string text = "$GNGGA,123519.00,4807.038,N,01131.000,E,1,08,0.9,545.4,M*47\r\n";
      return std::vector<uint8_t>(text.begin(), text.end());
    }
    case 1:
      return {0xBA, 0xCE, 0x04, 0x00, 0x05, 0x01, 0x06, 0x07, 0x00, 0x00, 1, 2, 3, 4};
    case 2:
      return randomBytes(rng, rng() % 48);
    default:
    {
      // Sync pairs with junk behind them, the classic resync trap.
      std::vector<uint8_t> out = {Ubx::SYNC_1, Ubx::SYNC_2};
      const auto tail = randomBytes(rng, rng() % 12);
      out.insert(out.end(), tail.begin(), tail.end());
      return out;
    }
  }
}

void mutate(std::mt19937 &rng, std::vector<uint8_t> &bytes) {
  if (bytes.empty()) {
    return;
  }
  switch (rng() % 5) {
    case 0:
      bytes[rng() % bytes.size()] ^= static_cast<uint8_t>(1u << (rng() % 8));
      break;
    case 1:
      bytes.resize(rng() % bytes.size());
      break;
    case 2:
      if (bytes.size() > 5) {
        bytes[4] = static_cast<uint8_t>(rng());
        bytes[5] = static_cast<uint8_t>(rng());
      }
      break;
    case 3:
      bytes.back() = static_cast<uint8_t>(bytes.back() + 1);
      break;
    default:
    {
      const auto extra = validFrame(rng);
      bytes.resize(bytes.size() / 2);
      bytes.insert(bytes.end(), extra.begin() + (extra.size() / 2), extra.end());
      break;
    }
  }
}

/** Run every decoder on a yielded frame. Returns false on a broken invariant. */
bool decodeAll(const Ubx::Stream &stream) {
  const auto &frame = stream.frame();
  if (Ubx::scan(frame.data(), frame.size()).status != Ubx::Scan::FRAME) {
    return false;
  }
  if (stream.payloadLength() > Ubx::MAX_PAYLOAD) {
    return false;
  }
  const uint8_t *p = stream.payload();
  const size_t n = stream.payloadLength();
  Ubx::Ack ack;
  Ubx::NavPvt pvt;
  std::vector<Ubx::NavSatSv> svs;
  Ubx::MonVer ver;
  Ubx::Sos sos;
  Ubx::ValGet get;
  Ubx::PmReq pm;
  uint8_t layers;
  std::vector<Ubx::KeyValue> items;
  std::vector<uint32_t> keys;
  Ubx::parseAck(stream.id(), p, n, ack);
  if (Ubx::parseNavPvt(p, n, pvt)) {
    // A decoded PVT re-encodes to the same 92 bytes in every field it carries.
    Ubx::NavPvt again;
    const auto encoded = Ubx::encodeNavPvt(pvt);
    if (!Ubx::parseNavPvt(encoded.data() + 6, 92, again) || (again.lat != pvt.lat)
        || (again.hAcc != pvt.hAcc) || (again.valid != pvt.valid)) {
      return false;
    }
    (void)pvt.positionValid();
    (void)pvt.timeValid();
  }
  if (Ubx::parseNavSat(p, n, svs) && (svs.size() != p[5])) {
    return false;
  }
  if (Ubx::parseMonVer(p, n, ver)) {
    (void)ver.field("FWVER");
    if ((ver.sw.size() > 30) || (ver.hw.size() > 10)) {
      return false;
    }
  }
  Ubx::parseSos(p, n, sos);
  if (Ubx::parseValGet(p, n, get) && (get.items.size() > Ubx::MAX_KEYS)) {
    return false;
  }
  Ubx::parsePmReq(p, n, pm);
  Ubx::parseValSet(p, n, layers, items);
  Ubx::parseKeyList(p, n, layers, keys);
  return true;
}

}  // namespace

int main(int argc, char **argv) {
  uint32_t seed = 1;
  uint32_t iterations = 1000;
  for (int i = 1; i + 1 < argc; i += 2) {
    if (std::strcmp(argv[i], "--seed") == 0) {
      seed = static_cast<uint32_t>(std::strtoul(argv[i + 1], nullptr, 10));
    } else if (std::strcmp(argv[i], "--iterations") == 0) {
      iterations = static_cast<uint32_t>(std::strtoul(argv[i + 1], nullptr, 10));
    }
  }

  std::mt19937 rng(seed);
  uint64_t frames = 0;
  for (uint32_t iteration = 0; iteration < iterations; iteration++) {
    std::vector<uint8_t> bytes;
    std::vector<std::vector<uint8_t>> expected;
    // A stray sync byte earlier in the stream can legally start a frame that
    // swallows a later good one, so exact delivery is only promised while no
    // mutated or noisy part has been placed ahead of it.
    bool clean = true;
    const size_t parts = 1 + (rng() % 12);
    for (size_t part = 0; part < parts; part++) {
      const uint32_t kind = rng() % 3;
      if (kind == 0) {
        auto good = validFrame(rng);
        if (clean) {
          expected.push_back(good);
        }
        bytes.insert(bytes.end(), good.begin(), good.end());
      } else if (kind == 1) {
        auto bad = validFrame(rng);
        mutate(rng, bad);
        bytes.insert(bytes.end(), bad.begin(), bad.end());
        clean = false;
      } else {
        const auto extra = noise(rng);
        bytes.insert(bytes.end(), extra.begin(), extra.end());
        clean = clean && (std::find(extra.begin(), extra.end(), Ubx::SYNC_1) == extra.end());
      }
    }

    Ubx::Stream stream;
    size_t matched = 0;
    for (const uint8_t b : bytes) {
      if (!stream.push(b)) {
        continue;
      }
      frames++;
      check(decodeAll(stream), "decoder invariant", seed, iteration);
      if ((matched < expected.size()) && (stream.frame() == expected[matched])) {
        matched++;
      }
    }
    check(matched == expected.size(), "a clean frame was lost", seed, iteration);
  }

  std::printf("gps_ubx_fuzz seed %u iterations %u frames %llu failures %d\n", seed, iterations,
              static_cast<unsigned long long>(frames), g_Failures);
  return g_Failures == 0 ? 0 : 1;
}
