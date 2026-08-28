#ifndef FURBLE_BT_DEBUG_HEX_H
#define FURBLE_BT_DEBUG_HEX_H

#include <cstddef>
#include <cstdint>

namespace Furble {

/** Encode binary diagnostics as bounded lowercase hex.
 *
 * The returned count is the number of source bytes retained.  The output is
 * always terminated when outputBytes is nonzero, including for a null input.
 */
inline size_t btHexEncode(const uint8_t *input,
                          size_t inputBytes,
                          char *output,
                          size_t outputBytes) {
  if (outputBytes == 0) {
    return 0;
  }

  static constexpr char HEX[] = "0123456789abcdef";
  const size_t retained = input == nullptr                     ? 0
                          : (outputBytes - 1) / 2 < inputBytes ? (outputBytes - 1) / 2
                                                               : inputBytes;
  for (size_t index = 0; index < retained; ++index) {
    const uint8_t value = input[index];
    output[index * 2] = HEX[value >> 4];
    output[index * 2 + 1] = HEX[value & 0x0f];
  }
  output[retained * 2] = '\0';
  return retained;
}

}  // namespace Furble

#endif
