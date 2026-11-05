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
 * - httpd_data, two socket slots, response headers and the copied PEM identity
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

inline constexpr bool serverFits(size_t freeBytes, size_t largestBlock) {
  return (freeBytes >= SERVER_INTERNAL_FREE_FLOOR)
         && (largestBlock >= SERVER_INTERNAL_LARGEST_FLOOR);
}

inline constexpr bool supervisorFits(size_t freeBytes, size_t largestBlock) {
  return (freeBytes >= SUPERVISOR_INTERNAL_FREE_FLOOR)
         && (largestBlock >= SUPERVISOR_INTERNAL_LARGEST_FLOOR);
}

}  // namespace WebUIMemory
}  // namespace Furble

#endif
