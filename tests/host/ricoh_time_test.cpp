// Ricoh Date Time Zone packet regression.
//
// Firmware reads a 7-byte packet: uint16 little-endian year, then month, day,
// hour, minute, second.

#include <array>
#include <cstdint>
#include <cstdio>

#include "protocol/RicohTime.h"

int main() {
  const auto packet = Furble::RicohProtocol::encodeDateTime(2026, 11, 1, 12, 34, 56);
  const std::array<uint8_t, 7> expected = {0xea, 0x07, 0x0b, 0x01, 0x0c, 0x22, 0x38};
  if (packet != expected) {
    std::fprintf(stderr, "FAIL: Ricoh Date Time Zone packet\n");
    return 1;
  }
  std::printf("Ricoh date-time packet passed\n");
  return 0;
}
