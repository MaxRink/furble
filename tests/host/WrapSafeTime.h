#pragma once

#include <cstdint>

namespace Furble::Host {

// Unsigned subtraction keeps short elapsed-time comparisons valid when the
// millisecond counter wraps. Callers must use timeouts shorter than one full
// uint32_t counter period.
constexpr bool timeoutPending(uint32_t start, uint32_t now, uint32_t timeout) {
  return static_cast<uint32_t>(now - start) < timeout;
}

}  // namespace Furble::Host
