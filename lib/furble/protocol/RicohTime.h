#ifndef FURBLE_PROTOCOL_RICOH_TIME_H
#define FURBLE_PROTOCOL_RICOH_TIME_H

#include <array>
#include <cstdint>

namespace Furble {
namespace RicohProtocol {

/**
 * Ricoh Date Time Zone packet, 7 bytes.
 *
 * uint16 little-endian year, then month, day, hour, minute, and second.
 * The camera firmware reads exactly this layout and has no timezone field.
 */
inline std::array<uint8_t, 7> encodeDateTime(unsigned int year,
                                             unsigned int month,
                                             unsigned int day,
                                             unsigned int hour,
                                             unsigned int minute,
                                             unsigned int second) {
  return {
      static_cast<uint8_t>(year & 0xff), static_cast<uint8_t>((year >> 8) & 0xff),
      static_cast<uint8_t>(month),       static_cast<uint8_t>(day),
      static_cast<uint8_t>(hour),        static_cast<uint8_t>(minute),
      static_cast<uint8_t>(second),
  };
}

}  // namespace RicohProtocol
}  // namespace Furble

#endif  // FURBLE_PROTOCOL_RICOH_TIME_H
