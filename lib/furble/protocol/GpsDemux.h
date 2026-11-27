#ifndef FURBLE_GPS_DEMUX_H
#define FURBLE_GPS_DEMUX_H

#include <cstddef>
#include <cstdint>
#include <vector>

#include "GpsText.h"

/*
 * Receiver stream demultiplexer: NMEA text, CASIC binary frames, UBX binary
 * frames and vendor text command lines share one UART. Pure, so the firmware
 * GPS task, the host tests and the fuzz target run the same code.
 *
 * NMEA is 7-bit text, so neither binary sync pair (CASIC BA CE, UBX B5 62) can
 * appear inside a sentence. Bytes up to the next sync pair, or the next vendor
 * command line, are NMEA. A frame that fails its length or checksum check drops
 * only its first sync byte, and the search resumes right after it, so a real
 * frame hidden behind a false sync is still found. A trailing lone sync byte is
 * kept, because its partner may be the first byte of the next read.
 *
 * A vendor command line is a complete '$' sentence whose name starts a Quectel
 * $PAIR or $PQTM message or a Unicore $CFG, $OK, $FAIL or $PDTINFO one; see
 * GpsText.h. Those names never collide with a standard NMEA talker. A finished
 * line longer than GpsText::MAX_LINE, terminator included, is dropped whole, so
 * parse() never sees more than the cap. A line that has not finished by the cap
 * loses its '$' and the search resumes, which bounds the buffer: the tail is
 * then ordinary NMEA text, and the NMEA parser rejects it on its checksum.
 */

namespace Furble {
namespace GpsDemux {

enum class Kind : uint8_t {
  NMEA,   /**< text for the NMEA parser */
  CASIC,  /**< a checksum-valid CASIC frame */
  UBX,    /**< a checksum-valid UBX frame */
  VENDOR, /**< one complete vendor text command line, terminator included */
  DROP,   /**< one rejected sync byte */
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

/** The longest tail split() can leave buffered: a whole frame, or a whole line. */
constexpr size_t CASIC_PENDING = CASIC_OVERHEAD + CASIC_MAX_PAYLOAD - 1;
constexpr size_t MAX_PENDING =
    (CASIC_PENDING > GpsText::MAX_LINE) ? CASIC_PENDING : GpsText::MAX_LINE;

/**
 * Split the head of a receive buffer into pieces, in stream order.
 *
 * Appends to out and returns the number of bytes the pieces cover. The caller
 * drops those and keeps the rest for the next read; they are the start of a
 * frame that has not fully arrived, a vendor line still arriving, or a lone
 * trailing sync byte.
 */
size_t split(const uint8_t *data, size_t length, std::vector<Piece> &out);

}  // namespace GpsDemux
}  // namespace Furble

#endif
