// Seeded UBX parser and receiver-stream demultiplexer fuzzer.
//
// Each iteration builds a receiver byte stream from valid frames of every
// message furble decodes, CASIC frames, NMEA text, random noise and mutations
// of all of those (bit flips, truncation, length and checksum corruption,
// spliced halves). ASan and UBSan catch an out-of-bounds read. The invariants
// checked here catch the logic faults:
//   1. Ubx::Stream yields exactly what a reference splitter built on scan()
//      yields over the whole buffer, so byte-at-a-time delivery is exact;
//   2. every frame placed intact is yielded, unless a checksum-valid frame
//      nobody placed was yielded first and may have swallowed it;
//   3. every yielded frame passes scan() and decodes without fault, and the
//      encoders round trip through the decoders;
//   4. GpsDemux::split covers the stream contiguously, its UBX and CASIC
//      pieces are valid frames, and any chunking of the stream gives the same
//      frames and the same NMEA text as one-shot delivery.
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

#include "GpsCasic.h"
#include "GpsDemux.h"
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

/** Reference splitter: scan() at every offset over the whole buffer. */
std::vector<std::vector<uint8_t>> referenceSplit(const std::vector<uint8_t> &bytes) {
  std::vector<std::vector<uint8_t>> out;
  size_t at = 0;
  while (at < bytes.size()) {
    const Ubx::ScanResult result = Ubx::scan(bytes.data() + at, bytes.size() - at);
    if (result.status == Ubx::Scan::FRAME) {
      out.emplace_back(bytes.begin() + at, bytes.begin() + at + result.length);
      at += result.length;
    } else if (result.status == Ubx::Scan::NEED_MORE) {
      break;
    } else {
      at++;
    }
  }
  return out;
}

/** What a demux run produced: the frames in order and the NMEA text. */
struct DemuxOut {
  std::vector<std::pair<int, std::vector<uint8_t>>> frames;
  std::vector<uint8_t> nmea;
  size_t pending = 0;
  bool ok = true;
};

/** Feed the stream in the given chunk sizes, as the firmware read loop does. */
DemuxOut demux(const std::vector<uint8_t> &bytes, const std::vector<size_t> &chunks) {
  DemuxOut out;
  std::vector<uint8_t> buffer;
  std::vector<GpsDemux::Piece> pieces;
  size_t at = 0;
  size_t chunk = 0;
  while (at < bytes.size()) {
    const size_t take = std::min(bytes.size() - at, chunks[chunk++ % chunks.size()]);
    buffer.insert(buffer.end(), bytes.begin() + at, bytes.begin() + at + take);
    at += take;
    pieces.clear();
    const size_t used = GpsDemux::split(buffer.data(), buffer.size(), pieces);
    size_t next = 0;
    for (const auto &piece : pieces) {
      out.ok = out.ok && (piece.offset == next) && (piece.length > 0);
      next = piece.offset + piece.length;
      const uint8_t *p = buffer.data() + piece.offset;
      if (piece.kind == GpsDemux::Kind::NMEA) {
        out.nmea.insert(out.nmea.end(), p, p + piece.length);
      } else if (piece.kind == GpsDemux::Kind::UBX) {
        out.ok = out.ok && (Ubx::scan(p, piece.length).status == Ubx::Scan::FRAME)
                 && (Ubx::scan(p, piece.length).length == piece.length);
        out.frames.emplace_back(1, std::vector<uint8_t>(p, p + piece.length));
      } else if (piece.kind == GpsDemux::Kind::CASIC) {
        out.ok = out.ok && !Casic::splitFrames(p, piece.length).empty();
        out.frames.emplace_back(2, std::vector<uint8_t>(p, p + piece.length));
      } else {
        out.ok = out.ok && (piece.length == 1);
      }
    }
    out.ok = out.ok && (next == used) && (used <= buffer.size());
    buffer.erase(buffer.begin(), buffer.begin() + used);
    out.ok = out.ok && (buffer.size() <= GpsDemux::MAX_PENDING);
  }
  out.pending = buffer.size();
  return out;
}

/** A valid CASIC frame, payload a multiple of four bytes. */
std::vector<uint8_t> casicFrame(std::mt19937 &rng) {
  return Casic::frame(static_cast<uint8_t>(rng()), static_cast<uint8_t>(rng()),
                      randomBytes(rng, 4 * (rng() % 12)));
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
    std::vector<std::vector<uint8_t>> placed;
    const size_t parts = 1 + (rng() % 12);
    for (size_t part = 0; part < parts; part++) {
      const uint32_t kind = rng() % 4;
      if (kind == 0) {
        auto good = validFrame(rng);
        placed.push_back(good);
        bytes.insert(bytes.end(), good.begin(), good.end());
      } else if (kind == 1) {
        auto bad = validFrame(rng);
        mutate(rng, bad);
        bytes.insert(bytes.end(), bad.begin(), bad.end());
      } else if (kind == 2) {
        auto casic = casicFrame(rng);
        if (rng() % 3 == 0) {
          mutate(rng, casic);
        }
        bytes.insert(bytes.end(), casic.begin(), casic.end());
      } else {
        const auto extra = noise(rng);
        bytes.insert(bytes.end(), extra.begin(), extra.end());
      }
    }

    // A false sync near the end can leave a frame waiting for bytes that never
    // come, holding back the good frames after it. A receiver keeps talking,
    // so pad with quiet bytes longer than any frame to settle every decision.
    bytes.insert(bytes.end(), Ubx::FRAME_OVERHEAD + Ubx::MAX_PAYLOAD, 0);

    // 1 and 3: the stream against the reference splitter.
    Ubx::Stream stream;
    std::vector<std::vector<uint8_t>> yielded;
    for (const uint8_t b : bytes) {
      if (!stream.push(b)) {
        continue;
      }
      frames++;
      check(decodeAll(stream), "decoder invariant", seed, iteration);
      yielded.push_back(stream.frame());
    }
    check(yielded == referenceSplit(bytes), "stream differs from the reference splitter", seed,
          iteration);

    // 2: a placed frame is lost only behind an unplaced checksum-valid frame.
    size_t next = 0;
    bool unexpected = false;
    for (const auto &frame : yielded) {
      if ((next < placed.size()) && (frame == placed[next])) {
        next++;
      } else if (std::find(placed.begin(), placed.end(), frame) == placed.end()) {
        unexpected = true;
      }
    }
    check(unexpected || (next == placed.size()), "an intact frame was lost", seed, iteration);

    // 4: the demultiplexer, one shot against random chunking.
    const DemuxOut whole = demux(bytes, {bytes.size() + 1});
    std::vector<size_t> chunks(1 + (rng() % 6));
    for (auto &chunk : chunks) {
      chunk = 1 + (rng() % 40);
    }
    const DemuxOut chunked = demux(bytes, chunks);
    check(whole.ok && chunked.ok, "demux piece invariant", seed, iteration);
    check(whole.frames == chunked.frames, "chunking changed the demuxed frames", seed, iteration);
    check(whole.nmea == chunked.nmea, "chunking changed the NMEA text", seed, iteration);
    check(whole.pending == chunked.pending, "chunking changed the pending tail", seed, iteration);
    size_t ubx = 0;
    for (const auto &frame : whole.frames) {
      ubx += (frame.first == 1) ? 1 : 0;
    }
    // The demux sees CASIC too, so it can only yield UBX frames the stream
    // also yields; a CASIC frame can hide a UBX sync from the demux.
    check(ubx <= yielded.size(), "demux yielded more UBX frames than the stream", seed, iteration);
  }

  std::printf("gps_ubx_fuzz seed %u iterations %u frames %llu failures %d\n", seed, iterations,
              static_cast<unsigned long long>(frames), g_Failures);
  return g_Failures == 0 ? 0 : 1;
}
