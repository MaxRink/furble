#ifndef FURBLE_MQTT_MEMORY_H
#define FURBLE_MQTT_MEMORY_H

#include <cstddef>
#include <cstdint>

namespace Furble {
namespace MQTTMemory {

/**
 * Internal-RAM budget for the MQTT service.
 *
 * The owner task handles the connected event, Home Assistant discovery and the
 * status JSON. `perf tasks` left 3,084 bytes free on the StickS3, so the owner
 * used about 7,156 bytes, and 4 KiB and 6 KiB both overflowed during start and
 * the first connected event. Keep the 10 KiB stack the hardware build uses.
 */
inline constexpr size_t OWNER_STACK_BYTES = 10240;

/**
 * esp-mqtt client task stacks. `perf tasks` left 704 of 4,096 bytes free on
 * the StickS3, about 3,392 used, while the event handler still held the event
 * copy on this stack. With the copy in `m_Incoming` the stack walk resolves
 * 1,936 bytes, so the plain worker uses 3 KiB. TLS keeps the 6 KiB default for
 * the handshake.
 */
inline constexpr size_t PLAIN_WORKER_STACK_BYTES = 3072;
inline constexpr size_t TLS_WORKER_STACK_BYTES = 6144;

/**
 * esp-mqtt network and outbox buffers.
 *
 * The Home Assistant hub discovery payload is about 3,005 bytes and the camera
 * payload about 1,874 bytes. esp-mqtt fragments an outgoing payload that is
 * larger than the network buffer, so 1,536 bytes does not cap the discovery
 * payload. Incoming commands are all far below this size.
 */
inline constexpr size_t BUFFER_SIZE = 1536;

/**
 * Owner event queue.
 *
 * The queue holds four 2,348-byte command copies on the 32-bit target (a
 * 256-byte topic and a 2,048-byte payload plus framing). It contains copied
 * characters and client handles only, never a flash or NVS pointer. Four items
 * are 9,392 bytes. On boards with PSRAM the queue is allocated there so it does
 * not take that internal RAM before the client can start. Boards without PSRAM
 * fall back to the internal heap.
 */
inline constexpr size_t EVENT_QUEUE_BYTES = 9392;

/**
 * One event copy: a 2,348-byte command copy on the 32-bit target.
 *
 * The owner preallocates two copies in one block, next to the queue and in the
 * same memory, and never frees them. The esp-mqtt client task builds each event
 * in the first (`m_Incoming`) because its plain stack is 3 KiB. The owner
 * receives each queued event into the second (`m_Received`). A copy on the
 * owner stack put 2,384 to 2,400-byte frames on `MQTT::taskStep` and
 * `MQTT::processLifecycleEvents`, and the two nested on the connected path.
 */
inline constexpr size_t EVENT_COPY_BYTES = 2348;
inline constexpr size_t EVENT_COPIES = 2;
inline constexpr size_t EVENT_COPIES_BYTES = EVENT_COPIES * EVENT_COPY_BYTES;

/**
 * What one heap block costs beyond its payload. A queue also carries its
 * 84-byte StaticQueue_t control block (from the target DWARF), in the same
 * block for xQueueCreate() and a second block for xQueueCreateWithCaps(), and
 * every block pays the TLSF header and the comprehensive-poisoning words. A
 * placement check against the bare payload could choose a pool that then
 * cannot hold the block, and every board aborts on a failed allocation.
 */
inline constexpr size_t BLOCK_OVERHEAD_BYTES = 128;
inline constexpr size_t EVENT_QUEUE_BLOCK_BYTES = EVENT_QUEUE_BYTES + BLOCK_OVERHEAD_BYTES;
inline constexpr size_t EVENT_COPIES_BLOCK_BYTES = EVENT_COPIES_BYTES + BLOCK_OVERHEAD_BYTES;

/** Everything the owner allocates for events: the queue and the two copies. */
inline constexpr size_t EVENT_STORAGE_BYTES = EVENT_QUEUE_BLOCK_BYTES + EVENT_COPIES_BLOCK_BYTES;

/**
 * Free internal heap required before the esp-mqtt client is created.
 *
 * The plain worker is 3 KiB, the two buffers are 1.5 KiB each, and esp-mqtt
 * allocates its own structures and lwIP pbufs. The floor leaves about 2 KiB
 * beyond the known allocations instead of starting into a measured 1,739-byte
 * pool like the pre-gate StickS3 build did.
 *
 * The TLS worker is 6 KiB rather than 3 KiB, so a secure URI gets its own
 * floors. Reusing the plain floor would admit a start that cannot hold the TLS
 * worker plus the same buffers and overhead. The TLS floors are the plain
 * floors plus the 3 KiB worker difference. When PSRAM is unavailable the
 * event queue and the two event copies are allocated internally, so the
 * caller adds `EVENT_STORAGE_BYTES` as `extraInternalBytes` and the floor
 * covers both.
 */
inline constexpr size_t START_INTERNAL_FREE_FLOOR = 8192;
inline constexpr size_t START_INTERNAL_LARGEST_FLOOR = 4096;
inline constexpr size_t START_TLS_INTERNAL_FREE_FLOOR = 11264;
inline constexpr size_t START_TLS_INTERNAL_LARGEST_FLOOR = 7168;

/**
 * Free internal heap required before the 10 KiB owner task is created.
 *
 * The owner is created before the client start gate runs. Admitting it with
 * only the start floor creates a 10 KiB task that cannot start a client, which
 * is the 5,199-byte hardware case. Add the owner stack to the start floor so
 * the owner only exists when the worker and buffers can follow. The queue is
 * PSRAM on the StickS3 and is not part of this floor.
 */
inline constexpr size_t OWNER_INTERNAL_FREE_FLOOR = START_INTERNAL_FREE_FLOOR + OWNER_STACK_BYTES;
inline constexpr size_t OWNER_TLS_INTERNAL_FREE_FLOOR =
    START_TLS_INTERNAL_FREE_FLOOR + OWNER_STACK_BYTES;
inline constexpr size_t OWNER_INTERNAL_LARGEST_FLOOR = 12288;

/**
 * Free internal heap required before Home Assistant discovery is built.
 *
 * Discovery is optional. A failed discovery used to take the device down
 * because the cJSON tree, the payload string and the retained enqueue all
 * competed for the last few hundred bytes. Require the headroom measured for
 * MQTT plus discovery and skip only the optional records when it is missing.
 */
inline constexpr size_t DISCOVERY_INTERNAL_FREE_FLOOR = 10240;
inline constexpr size_t DISCOVERY_INTERNAL_LARGEST_FLOOR = 4096;

/**
 * One admission decision against a measured internal-heap pair.
 *
 * The measurement is recorded in the decision so the caller can log the exact
 * pair it rejected. The host test and the simulator run this same function, so
 * they assert the firmware thresholds rather than a copy.
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

// extraLargestBlock is the biggest single internal allocation among the extra
// bytes; it defaults to all of them as one block.
inline constexpr gate_t startGate(size_t freeBytes,
                                  size_t largestBlock,
                                  bool secure = false,
                                  size_t extraInternalBytes = 0,
                                  size_t extraLargestBlock = SIZE_MAX) {
  const size_t baseFree = secure ? START_TLS_INTERNAL_FREE_FLOOR : START_INTERNAL_FREE_FLOOR;
  const size_t baseLargest =
      secure ? START_TLS_INTERNAL_LARGEST_FLOOR : START_INTERNAL_LARGEST_FLOOR;
  const size_t extraLargest =
      extraLargestBlock < extraInternalBytes ? extraLargestBlock : extraInternalBytes;
  const size_t freeFloor = baseFree + extraInternalBytes;
  const size_t largestFloor = extraLargest > baseLargest ? extraLargest : baseLargest;
  return evaluate(freeBytes, largestBlock, freeFloor, largestFloor);
}

inline constexpr gate_t ownerGate(size_t freeBytes, size_t largestBlock, bool secure = false) {
  return secure ? evaluate(freeBytes, largestBlock, OWNER_TLS_INTERNAL_FREE_FLOOR,
                           OWNER_INTERNAL_LARGEST_FLOOR)
                : evaluate(freeBytes, largestBlock, OWNER_INTERNAL_FREE_FLOOR,
                           OWNER_INTERNAL_LARGEST_FLOOR);
}

inline constexpr gate_t discoveryGate(size_t freeBytes, size_t largestBlock) {
  return evaluate(freeBytes, largestBlock, DISCOVERY_INTERNAL_FREE_FLOOR,
                  DISCOVERY_INTERNAL_LARGEST_FLOOR);
}

inline constexpr bool startFits(size_t freeBytes,
                                size_t largestBlock,
                                bool secure = false,
                                size_t extraInternalBytes = 0,
                                size_t extraLargestBlock = SIZE_MAX) {
  return startGate(freeBytes, largestBlock, secure, extraInternalBytes, extraLargestBlock).open;
}

inline constexpr bool ownerFits(size_t freeBytes, size_t largestBlock, bool secure = false) {
  return ownerGate(freeBytes, largestBlock, secure).open;
}

inline constexpr bool discoveryFits(size_t freeBytes, size_t largestBlock) {
  return discoveryGate(freeBytes, largestBlock).open;
}

/**
 * Production log formats, shared with the simulator so its fail-closed output
 * is the sentence a device prints rather than a paraphrase.
 */
// ESP_LOG_ATTR_STR() needs a string literal when it is passed to ESP_LOGx;
// keep the text in macros and expose the same text as constants for the
// simulator.
#define FURBLE_MQTT_OWNER_GATE_LOG \
  "MQTT owner not created: internal free %u, largest %u (need %u/%u)"
#define FURBLE_MQTT_START_GATE_LOG "MQTT start deferred: internal free %u, largest %u (need %u/%u)"
#define FURBLE_MQTT_DISCOVERY_GATE_LOG \
  "HA discovery skipped: internal free %u, largest %u (need %u/%u)"

inline constexpr const char *OWNER_GATE_LOG = FURBLE_MQTT_OWNER_GATE_LOG;
inline constexpr const char *START_GATE_LOG = FURBLE_MQTT_START_GATE_LOG;
inline constexpr const char *DISCOVERY_GATE_LOG = FURBLE_MQTT_DISCOVERY_GATE_LOG;

}  // namespace MQTTMemory
}  // namespace Furble

#endif
