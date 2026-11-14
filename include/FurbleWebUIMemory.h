#ifndef FURBLE_WEBUI_MEMORY_H
#define FURBLE_WEBUI_MEMORY_H

#include <cstddef>

#include "FurbleTaskMemory.h"

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
inline constexpr size_t SUPERVISOR_STACK_RELEASE_BYTES = 4096;
/**
 * A debug build's stack protector and -Os frames need more: 4,240 bytes on
 * the Waveshare debug build (plan 314 PR 4c). tools/stack_budget.json reads it.
 */
#if defined(FURBLE_DEBUG_BUILD)
inline constexpr size_t SUPERVISOR_STACK_DEBUG_EXTRA = 256;
#else
inline constexpr size_t SUPERVISOR_STACK_DEBUG_EXTRA = 0;
#endif
inline constexpr size_t SUPERVISOR_STACK_BYTES =
    SUPERVISOR_STACK_RELEASE_BYTES + SUPERVISOR_STACK_DEBUG_EXTRA;
inline constexpr size_t HTTPD_STACK_BYTES = 8192;

/**
 * Free internal heap that must remain after the supervisor already exists,
 * before httpd_ssl_start creates its task and per-server bookkeeping.
 *
 * Breakdown of the allocations that land on the internal heap:
 * - httpd_data, two client socket slots plus the three sockets ESP-IDF reserves
 *   internally, response headers and the copied PEM identity
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

/**
 * Enough contiguous internal RAM for the supervisor task plus TCB: 5,120 /
 * 4,608 bytes for the 4 KiB release stack, and the debug margin on top.
 */
inline constexpr size_t SUPERVISOR_INTERNAL_LARGEST_FLOOR = SUPERVISOR_STACK_BYTES + 512;
inline constexpr size_t SUPERVISOR_INTERNAL_FREE_FLOOR = SUPERVISOR_INTERNAL_LARGEST_FLOOR + 512;

/**
 * The one-shot TLS identity task. It writes NVS, so its stack stays internal.
 * The stack holds the 1 KiB key and 2 KiB certificate PEM arrays of
 * createIdentity() and the 1 KiB signature buffer of
 * mbedtls_x509write_crt_der(). tools/stack_budget.json reads this value.
 */
inline constexpr size_t IDENTITY_STACK_BYTES = 12288;

/**
 * Derived: peak mbedTLS heap of WebUI::createIdentity() on the pinned
 * mbedTLS 3.6.5 (ESP-IDF 5.5.3). The largest block is the private-key DER
 * buffer that mbedtls_pk_write_key_pem() callocs: PRV_DER_MAX_BYTES is the RSA
 * bound because esp_config.h always defines MBEDTLS_RSA_C, 47 + 3 * 1024 +
 * 5 * 512 = 5,679 bytes. While it is live the key pair (128-byte
 * mbedtls_ecp_keypair plus its d and Q limbs, about 450 bytes with block
 * headers), the four subject and issuer name nodes (about 450 bytes) and the
 * subject string are also live: about 6.6 KiB. Key generation and the ECDSA
 * signature peak lower, at about 4.6 KiB: an 8-point comb table
 * (MBEDTLS_ECP_WINDOW_SIZE 4, fixed-point optimization off) of 24 limb
 * arrays, the HMAC-DRBG digest contexts and the signing temporaries. Rounded
 * up to 8 KiB. Every WebUI board (the S3 boards and the Core2) sets
 * CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC, so this heap is PSRAM and the floor is
 * conservative. It is kept so the gate does not depend on placement.
 */
inline constexpr size_t IDENTITY_MBEDTLS_HEAP_BYTES = 8192;

/**
 * Free internal heap required before the runtime identity task is created:
 * the stack, the TCB and block overhead, and the mbedTLS working heap
 * (20,864 bytes). The largest block must hold the stack plus overhead
 * (12,672 bytes). The boot path in WebUI::initEarly() is not gated: it runs
 * before WiFi, the display and the companion take their share.
 */
inline constexpr size_t IDENTITY_INTERNAL_FREE_FLOOR =
    IDENTITY_STACK_BYTES + TaskMemory::TASK_OVERHEAD_BYTES + IDENTITY_MBEDTLS_HEAP_BYTES;
inline constexpr size_t IDENTITY_INTERNAL_LARGEST_FLOOR =
    IDENTITY_STACK_BYTES + TaskMemory::TASK_OVERHEAD_BYTES;

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

inline constexpr gate_t identityGate(size_t freeBytes, size_t largestBlock) {
  return evaluate(freeBytes, largestBlock, IDENTITY_INTERNAL_FREE_FLOOR,
                  IDENTITY_INTERNAL_LARGEST_FLOOR);
}

inline constexpr bool serverFits(size_t freeBytes, size_t largestBlock) {
  return serverGate(freeBytes, largestBlock).open;
}

inline constexpr bool supervisorFits(size_t freeBytes, size_t largestBlock) {
  return supervisorGate(freeBytes, largestBlock).open;
}

inline constexpr bool identityFits(size_t freeBytes, size_t largestBlock) {
  return identityGate(freeBytes, largestBlock).open;
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
#define FURBLE_WEBUI_IDENTITY_GATE_LOG                                      \
  "TLS identity not generated: internal free %u, largest %u (need %u/%u). " \
  "HTTPS stays closed; reboot or change a setting once more internal RAM is free to retry."

inline constexpr const char *SUPERVISOR_GATE_LOG = FURBLE_WEBUI_SUPERVISOR_GATE_LOG;
inline constexpr const char *SERVER_GATE_LOG = FURBLE_WEBUI_SERVER_GATE_LOG;
inline constexpr const char *IDENTITY_GATE_LOG = FURBLE_WEBUI_IDENTITY_GATE_LOG;

}  // namespace WebUIMemory
}  // namespace Furble

#endif
