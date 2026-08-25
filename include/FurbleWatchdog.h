#ifndef FURBLE_WATCHDOG_H
#define FURBLE_WATCHDOG_H

#include <cstdint>

namespace Furble {
namespace Watchdog {

/** M5PM1 watchdog timing shared by firmware, simulator, and host tests. */
static constexpr uint32_t PM1_TIMEOUT_MS = 10000;
static constexpr uint8_t PM1_TIMEOUT_S = static_cast<uint8_t>(PM1_TIMEOUT_MS / 1000);
static constexpr uint32_t PM1_FEED_PERIOD_MS = 1000;

static_assert(PM1_TIMEOUT_MS >= 3 * PM1_FEED_PERIOD_MS,
              "M5PM1 watchdog feed period must retain a three-feed margin");

}  // namespace Watchdog
}  // namespace Furble

#endif
