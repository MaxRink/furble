#include "GpsDemux.h"

#include "GpsCasic.h"
#include "GpsUbx.h"

namespace Furble {
namespace GpsDemux {

namespace {

constexpr uint8_t CASIC_SYNC_0 = 0xBA;
constexpr uint8_t CASIC_SYNC_1 = 0xCE;

uint16_t readU16(const uint8_t *data) {
  return static_cast<uint16_t>(data[0] | (data[1] << 8));
}

uint32_t readU32(const uint8_t *data) {
  return static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8)
         | (static_cast<uint32_t>(data[2]) << 16) | (static_cast<uint32_t>(data[3]) << 24);
}

bool syncPair(const uint8_t *data) {
  return ((data[0] == CASIC_SYNC_0) && (data[1] == CASIC_SYNC_1))
         || ((data[0] == Ubx::SYNC_1) && (data[1] == Ubx::SYNC_2));
}

}  // namespace

size_t split(const uint8_t *data, size_t length, std::vector<Piece> &out) {
  size_t at = 0;
  while (at < length) {
    size_t header = length;
    for (size_t i = at; i + 1 < length; i++) {
      if (syncPair(data + i)) {
        header = i;
        break;
      }
    }

    if (header == length) {
      const uint8_t last = data[length - 1];
      const size_t keep = ((last == CASIC_SYNC_0) || (last == Ubx::SYNC_1)) ? 1 : 0;
      if ((length - keep) > at) {
        out.push_back({Kind::NMEA, at, length - keep - at});
      }
      return length - keep;
    }

    if (header > at) {
      out.push_back({Kind::NMEA, at, header - at});
      at = header;
      continue;
    }

    const uint8_t *frame = data + at;
    const size_t available = length - at;

    if (frame[0] == Ubx::SYNC_1) {
      const Ubx::ScanResult result = Ubx::scan(frame, available);
      if (result.status == Ubx::Scan::NEED_MORE) {
        return at;
      }
      if (result.status == Ubx::Scan::BAD) {
        out.push_back({Kind::DROP, at, 1});
        at++;
        continue;
      }
      out.push_back({Kind::UBX, at, result.length});
      at += result.length;
      continue;
    }

    if (available < CASIC_HEADER) {
      return at;
    }
    const uint16_t payload = readU16(frame + 2);
    if ((payload > CASIC_MAX_PAYLOAD) || ((payload % 4) != 0)) {
      out.push_back({Kind::DROP, at, 1});
      at++;
      continue;
    }
    const size_t total = CASIC_OVERHEAD + payload;
    if (available < total) {
      return at;
    }
    const uint32_t expected = Casic::checksum(frame[4], frame[5], payload, frame + CASIC_HEADER);
    if (expected != readU32(frame + CASIC_HEADER + payload)) {
      out.push_back({Kind::DROP, at, 1});
      at++;
      continue;
    }
    out.push_back({Kind::CASIC, at, total});
    at += total;
  }
  return at;
}

}  // namespace GpsDemux
}  // namespace Furble
