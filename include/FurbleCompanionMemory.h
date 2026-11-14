#ifndef FURBLE_COMPANION_MEMORY_H
#define FURBLE_COMPANION_MEMORY_H

#include <cstddef>

#include "FurbleTaskMemory.h"

namespace Furble {
namespace CompanionMemory {

/**
 * The retained companion service task. `perf tasks` left 1,884 bytes free on
 * the old 4 KiB stack with the companion connected and a settings write, so it
 * used about 2,212 bytes and 3 KiB leaves about 860. It commits NVS, so it
 * stays internal. tools/stack_budget.json reads this value.
 */
inline constexpr size_t TASK_STACK_BYTES = 3072;

/**
 * Derived: the GATT database the first enable builds and keeps, sized from the
 * m5stack-core2 DWARF:
 * - 2 NimBLEService (68 bytes) and 9 NimBLECharacteristic (92 bytes) objects,
 *   1,140 bytes with block headers;
 * - 9 initial attribute values of CONFIG_NIMBLE_CPP_ATT_VALUE_INIT_LENGTH 20
 *   plus 1, 360 bytes with headers;
 * - the ble_gatt_svc_def and ble_gatt_chr_def arrays NimBLEService::start()
 *   allocates (16 and 32 bytes per entry), about 450 bytes, and the service
 *   and characteristic vectors, about 100 bytes;
 * - the NimBLE attribute entry pool (20 bytes per attribute) and client
 *   configuration pool that ble_gatts_start() rebuilds, about 1 KiB;
 * - the disable and timed-shutter esp_timers, 96 bytes.
 * About 3.1 KiB, rounded up to 4 KiB. The boards without PSRAM keep all of it
 * internal. The PSRAM boards (S3 and Core2) set
 * CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL, which moves the NimBLE attribute
 * pools to PSRAM; the NimBLE-cpp objects, arrays and timers are below
 * SPIRAM_MALLOC_ALWAYSINTERNAL and stay internal. The floor is conservative
 * there.
 */
inline constexpr size_t GATT_BYTES = 4096;

/**
 * Free internal heap required before the first enable creates the task and
 * the GATT services (7,552 bytes). The largest block must hold the task stack
 * plus overhead (3,456 bytes). Later enables reuse both and are not gated.
 */
inline constexpr size_t START_INTERNAL_FREE_FLOOR =
    TASK_STACK_BYTES + TaskMemory::TASK_OVERHEAD_BYTES + GATT_BYTES;
inline constexpr size_t START_INTERNAL_LARGEST_FLOOR =
    TASK_STACK_BYTES + TaskMemory::TASK_OVERHEAD_BYTES;

/**
 * One admission decision against a measured internal-heap pair. The host test
 * and the simulator run this same function.
 */
struct gate_t {
  bool open;
  size_t freeBytes;
  size_t largestBlock;
  size_t freeFloor;
  size_t largestFloor;
};

inline constexpr gate_t startGate(size_t freeBytes, size_t largestBlock) {
  return {freeBytes >= START_INTERNAL_FREE_FLOOR && largestBlock >= START_INTERNAL_LARGEST_FLOOR,
          freeBytes, largestBlock, START_INTERNAL_FREE_FLOOR, START_INTERNAL_LARGEST_FLOOR};
}

/**
 * Production log format, shared with the simulator. ESP_LOGx needs a string
 * literal, so the text lives in a macro.
 */
#define FURBLE_COMPANION_START_GATE_LOG                                         \
  "Companion not started: internal free %u, largest %u (need %u/%u). It stays " \
  "off; enable it again or reboot to retry."

}  // namespace CompanionMemory
}  // namespace Furble

#endif
