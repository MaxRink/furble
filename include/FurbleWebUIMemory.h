#ifndef FURBLE_WEBUI_MEMORY_H
#define FURBLE_WEBUI_MEMORY_H

#include <cstddef>

namespace Furble {
namespace WebUIMemory {

/**
 * Internal-RAM cost of the two WebUI tasks.
 *
 * Both stacks must stay in internal RAM. The supervisor calls
 * httpd_ssl_start/stop and the httpd task runs Settings reads and writes, so
 * neither task may live in PSRAM: the flash cache is disabled while NVS commits
 * and a PSRAM stack cannot be resumed from a flash operation.
 */
inline constexpr size_t SUPERVISOR_STACK_BYTES = 3072;
inline constexpr size_t HTTPD_STACK_BYTES = 8192;

/**
 * Free internal heap that must remain after the supervisor already exists,
 * before httpd_ssl_start creates its task and per-server bookkeeping.
 *
 * Breakdown of the allocations that land on the internal heap:
 * - httpd_data, five total socket slots (two client slots after the three
 *   ESP-IDF reserves), response headers and the copied PEM identity
 * - one esp_tls_t (about 2.2 KiB) for the active TLS session
 * - the 1 KiB request scratch buffer
 * - the largest JSON reply (settings) and the lwIP pbufs for it
 *
 * The floor is deliberately conservative. The server logs the measured
 * free/largest pair at start and refuses to run below the floor instead of
 * crashing or boot-looping.
 */
inline constexpr size_t SERVER_INTERNAL_FREE_FLOOR = 20480;
inline constexpr size_t SERVER_INTERNAL_LARGEST_FLOOR = 12288;

/** Enough contiguous internal RAM for the 3 KiB supervisor task plus TCB. */
inline constexpr size_t SUPERVISOR_INTERNAL_FREE_FLOOR = 4096;
inline constexpr size_t SUPERVISOR_INTERNAL_LARGEST_FLOOR = 3584;

/**
 * One admission decision against a measured internal-heap pair.
 *
 * The measurement is recorded in the decision so the caller can log the exact
 * pair it rejected. The simulator heap model runs this same function, so a
 * scenario asserts the firmware gate rather than a copy of its thresholds.
 */
struct gate_t {
  bool open;
  size_t freeBytes;
  size_t largestBlock;
  size_t freeFloor;
  size_t largestFloor;
};

inline constexpr gate_t evaluate(size_t freeBytes,
                                 size_t largestBlock,
                                 size_t freeFloor,
                                 size_t largestFloor) {
  return {freeBytes >= freeFloor && largestBlock >= largestFloor, freeBytes, largestBlock,
          freeFloor, largestFloor};
}

inline constexpr gate_t serverGate(size_t freeBytes, size_t largestBlock) {
  return evaluate(freeBytes, largestBlock, SERVER_INTERNAL_FREE_FLOOR,
                  SERVER_INTERNAL_LARGEST_FLOOR);
}

inline constexpr gate_t supervisorGate(size_t freeBytes, size_t largestBlock) {
  return evaluate(freeBytes, largestBlock, SUPERVISOR_INTERNAL_FREE_FLOOR,
                  SUPERVISOR_INTERNAL_LARGEST_FLOOR);
}

inline constexpr bool serverFits(size_t freeBytes, size_t largestBlock) {
  return serverGate(freeBytes, largestBlock).open;
}

inline constexpr bool supervisorFits(size_t freeBytes, size_t largestBlock) {
  return supervisorGate(freeBytes, largestBlock).open;
}

/**
 * Production log formats, shared with the simulator so its fail-closed output
 * is the sentence a device prints rather than a paraphrase.
 */
// ESP_LOG_ATTR_STR() needs a string literal when it is passed to ESP_LOGx;
// keep the text in macros and expose the same text as constants for the
// simulator.
#define FURBLE_WEBUI_SUPERVISOR_GATE_LOG \
  "WebUI supervisor not created: internal free %u, largest %u (need %u/%u)"
#define FURBLE_WEBUI_SERVER_GATE_LOG                                \
  "HTTPS stays closed: internal free %u, largest %u (need %u/%u). " \
  "See docs/webui.md for the board budget."

inline constexpr const char *SUPERVISOR_GATE_LOG = FURBLE_WEBUI_SUPERVISOR_GATE_LOG;
inline constexpr const char *SERVER_GATE_LOG = FURBLE_WEBUI_SERVER_GATE_LOG;

}  // namespace WebUIMemory
}  // namespace Furble

#endif
