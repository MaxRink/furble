#ifndef FURBLE_MQTT_MEMORY_H
#define FURBLE_MQTT_MEMORY_H

#include <cstddef>

namespace Furble {
namespace MQTTMemory {

/**
 * Internal-RAM budget for the MQTT service.
 *
 * The owner task handles the connected event, Home Assistant discovery and the
 * status JSON. The StickS3 measured a 3,084-byte high-water mark, but 4 KiB and
 * 6 KiB both overflowed during start and the first connected event. Keep the
 * 10 KiB stack the hardware build uses.
 */
inline constexpr size_t OWNER_STACK_BYTES = 10240;

/**
 * esp-mqtt client task stacks. Plain MQTT measured a 704-byte high-water mark
 * on the StickS3. TLS keeps the 6 KiB default for the handshake.
 */
inline constexpr size_t PLAIN_WORKER_STACK_BYTES = 4096;
inline constexpr size_t TLS_WORKER_STACK_BYTES = 6144;

/**
 * esp-mqtt network and outbox buffers.
 *
 * The Home Assistant hub discovery payload is about 3,005 bytes and the camera
 * payload about 1,874 bytes. esp-mqtt fragments an outgoing payload that is
 * larger than the network buffer, so 2,048 bytes does not cap the discovery
 * payload. Incoming commands are all far below this size.
 */
inline constexpr size_t BUFFER_SIZE = 2048;

/**
 * Free internal heap required before the esp-mqtt client is created.
 *
 * The plain worker is 3 KiB, the two buffers are 1.5 KiB each, and esp-mqtt
 * allocates its own structures and lwIP pbufs. The floor leaves about 2 KiB
 * beyond the known allocations instead of starting into a measured 1,739-byte
 * pool like the pre-gate StickS3 build did.
 */
inline constexpr size_t START_INTERNAL_FREE_FLOOR = 8192;
inline constexpr size_t START_INTERNAL_LARGEST_FLOOR = 4096;

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

inline constexpr gate_t startGate(size_t freeBytes, size_t largestBlock) {
  return evaluate(freeBytes, largestBlock, START_INTERNAL_FREE_FLOOR, START_INTERNAL_LARGEST_FLOOR);
}

inline constexpr gate_t discoveryGate(size_t freeBytes, size_t largestBlock) {
  return evaluate(freeBytes, largestBlock, DISCOVERY_INTERNAL_FREE_FLOOR,
                  DISCOVERY_INTERNAL_LARGEST_FLOOR);
}

inline constexpr bool startFits(size_t freeBytes, size_t largestBlock) {
  return startGate(freeBytes, largestBlock).open;
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
#define FURBLE_MQTT_START_GATE_LOG "MQTT start deferred: internal free %u, largest %u (need %u/%u)"
#define FURBLE_MQTT_DISCOVERY_GATE_LOG \
  "HA discovery skipped: internal free %u, largest %u (need %u/%u)"

inline constexpr const char *START_GATE_LOG = FURBLE_MQTT_START_GATE_LOG;
inline constexpr const char *DISCOVERY_GATE_LOG = FURBLE_MQTT_DISCOVERY_GATE_LOG;

}  // namespace MQTTMemory
}  // namespace Furble

#endif
