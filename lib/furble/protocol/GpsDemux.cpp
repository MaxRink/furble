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

/**
 * Find the terminator of the vendor line that starts at `start`.
 *
 * Sets `stop` just past the line ending and returns true. A CR LF pair counts as
 * one ending, and a CR that ends the buffer is an ending on its own: the caller
 * must not wait for the LF of a line that already completed, because the stop
 * index of a complete line can equal the buffer length.
 */
bool lineEnd(const uint8_t *data, size_t start, size_t length, size_t &stop) {
  for (size_t i = start; i < length; i++) {
    if ((data[i] == '\r') || (data[i] == '\n')) {
      stop = i + 1;
      if ((data[i] == '\r') && (stop < length) && (data[stop] == '\n')) {
        stop++;
      }
      return true;
    }
  }
  return false;
}

}  // namespace

size_t split(const uint8_t *data, size_t length, std::vector<Piece> &out) {
  size_t at = 0;
  while (at < length) {
    const size_t available = length - at;

    if ((available >= 2) && syncPair(data + at)) {
      const uint8_t *frame = data + at;

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
      continue;
    }

    if (data[at] == '$') {
      switch (GpsText::prefix(data + at, available)) {
        case GpsText::Prefix::MAYBE:
          // the name is not complete yet, so wait rather than misroute it
          return at;
        case GpsText::Prefix::YES:
        {
          size_t end = 0;
          if (!lineEnd(data, at, length, end)) {
            if (available > GpsText::MAX_LINE) {
              // a line this long is noise: drop a byte and look again
              out.push_back({Kind::DROP, at, 1});
              at++;
              continue;
            }
            return at;
          }
          if ((end - at) > GpsText::MAX_LINE) {
            // a finished line past the cap is dropped whole: parse() is never
            // handed more than MAX_LINE, whatever the terminator says
            out.push_back({Kind::DROP, at, end - at});
            at = end;
            continue;
          }
          out.push_back({Kind::VENDOR, at, end - at});
          at = end;
          continue;
        }
        case GpsText::Prefix::NO:
          // an ordinary NMEA sentence, handled as text below
          break;
      }
    }

    // NMEA text up to the next sync pair or the next $-prefixed vendor name.
    const size_t scan = (data[at] == '$') ? (at + 1) : at;
    size_t next = length;
    for (size_t i = scan; i < length; i++) {
      if (((i + 1) < length) && syncPair(data + i)) {
        next = i;
        break;
      }
      if ((data[i] == '$') && (GpsText::prefix(data + i, length - i) != GpsText::Prefix::NO)) {
        next = i;
        break;
      }
    }

    if (next == length) {
      const uint8_t last = data[length - 1];
      const size_t keep = ((last == CASIC_SYNC_0) || (last == Ubx::SYNC_1)) ? 1 : 0;
      if ((length - keep) > at) {
        out.push_back({Kind::NMEA, at, length - keep - at});
      }
      return length - keep;
    }

    // next is always past `at`: the scan above starts at at + 1 when the byte
    // at `at` is a '$' whose prefix was no vendor name, so this always advances
    out.push_back({Kind::NMEA, at, next - at});
    at = next;
  }
  return at;
}

}  // namespace GpsDemux
}  // namespace Furble
