#ifndef FURBLE_GPS_DEMUX_H
#define FURBLE_GPS_DEMUX_H

#include <cstddef>
#include <cstdint>
#include <vector>

/*
 * Receiver stream demultiplexer: NMEA text, CASIC binary frames and UBX binary
 * frames share one UART. Pure, so the firmware GPS task, the host tests and the
 * fuzz target run the same code.
 *
 * NMEA is 7-bit text, so neither binary sync pair (CASIC BA CE, UBX B5 62) can
 * appear inside a sentence. Bytes up to the next sync pair are NMEA. A frame
 * that fails its length or checksum check drops only its first sync byte, and
 * the search resumes right after it, so a real frame hidden behind a false
 * sync is still found. A trailing lone sync byte is kept, because its partner
 * may be the first byte of the next read.
 */

namespace Furble {
namespace GpsDemux {

enum class Kind : uint8_t {
  NMEA,  /**< text for the NMEA parser */
  CASIC, /**< a checksum-valid CASIC frame */
  UBX,   /**< a checksum-valid UBX frame */
  DROP,  /**< one rejected sync byte */
};

struct Piece {
  Kind kind;
  size_t offset;
  size_t length;
};

/** CASIC frame limits, plan 32 PR32b: 6 byte header, 4 byte checksum. */
constexpr size_t CASIC_HEADER = 6;
constexpr size_t CASIC_OVERHEAD = 10;
constexpr uint16_t CASIC_MAX_PAYLOAD = 2048;

/** The longest tail split() can leave buffered: a whole CASIC frame less a byte. */
constexpr size_t MAX_PENDING = CASIC_OVERHEAD + CASIC_MAX_PAYLOAD - 1;

/**
 * Split the head of a receive buffer into pieces, in stream order.
 *
 * Appends to out and returns the number of bytes the pieces cover. The caller
 * drops those and keeps the rest for the next read; they are the start of a
 * frame that has not fully arrived, or a lone trailing sync byte.
 */
size_t split(const uint8_t *data, size_t length, std::vector<Piece> &out);

}  // namespace GpsDemux
}  // namespace Furble

#endif
