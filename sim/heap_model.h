#ifndef FURBLE_SIM_HEAP_MODEL_H
#define FURBLE_SIM_HEAP_MODEL_H

#include <cstddef>
#include <cstdint>
#include <string>

namespace Furble::Sim {

/*
 * Internal-heap accounting model for the host simulator.
 *
 * The SDL build runs on a host heap that is orders of magnitude larger than
 * the StickS3's internal SRAM, so an allocation that panics on hardware always
 * succeeds here. That gap hid the WebUI and MQTT boot loops completely. This
 * model carries the measured internal free/largest pair a scenario seeds and
 * charges the task and service allocations that the firmware makes after the
 * pressure point, so the production gates and failure branches run against
 * numbers that resemble the device.
 *
 * It is not a general-purpose allocator and it does not model fragmentation
 * beyond the largest-block figure. It models one contiguous internal pool.
 */

/** Replace the modelled pool. Applies before boot tasks are charged. */
void heapSet(size_t freeBytes, size_t largestBlock);

/**
 * Start charging task stacks and modelled service allocations to the pool.
 *
 * Called after the boot-time tasks the seeded measurement already includes
 * (WiFi, BLE, companion and the display) exist. Tasks created before this
 * point are part of the measured baseline, not new allocations.
 */
void heapActivate(void);

bool heapActive(void);

size_t heapFree(void);
size_t heapLargest(void);
size_t heapMinimumFree(void);
uint32_t heapAllocationFailures(void);
const char *heapLastFailedTask(void);

/**
 * External PSRAM pool model. The mode is one of `absent`, `quad`, `octal` or
 * `init_fail`. An init failure keeps the pool absent and is reportable, which
 * is the shape a bad octal timing or a missing module produces.
 */
void heapSetPsram(const std::string &mode, size_t freeBytes, size_t largestBlock);
bool heapPsramPresent(void);
const char *heapPsramMode(void);
const char *heapPsramState(void);
size_t heapPsramFree(void);
size_t heapPsramLargest(void);
bool heapPsramInitFailed(void);

/**
 * Task stack placement. `internal` is the production default. `psram` models a
 * configuration fault: a PSRAM stack cannot be resumed while the flash cache is
 * disabled, so any live PSRAM-backed task makes a flash write fail closed.
 */
void heapSetTaskStackRegion(const std::string &region);
const char *heapTaskStackRegion(void);

/** Model one flash write with the cache disabled. */
void heapFlashWrite(void);
uint32_t heapFlashWrites(void);
bool heapFlashUnsafe(void);

/** Charge one modelled allocation. Returns false when it cannot fit. */
bool heapReserve(size_t bytes);

/** Return a modelled allocation to the pool. */
void heapRelease(size_t bytes);

/** Return one modelled task stack to its internal or PSRAM pool. */
void heapReleaseTask(size_t bytes, bool external);

/**
 * Owner-tagged allocation for the memory profiler. The tag is used for
 * per-owner accounting, leak detection and exact-size failure injection.
 * `persistent` allocations are lifetime buffers and are exempt from the
 * scenario-end leak check.
 */
uint64_t heapAllocTagged(size_t bytes, const char *owner, bool external, bool persistent = false);
void heapFreeTagged(uint64_t id);

/** Fail the next `count` allocations of this size and owner. Empty owner is a
 * wildcard. Used by exact-size allocation fault scenarios. */
void heapSetAllocFailure(size_t bytes, const char *owner, uint32_t count);

/** Allocations that must fail exactly once, then succeed on the retry. */
void heapSetAllocFailureOnce(size_t bytes, const char *owner);

/**
 * Exact-size failure probe for the heap_caps shim. It does not create a live
 * allocation, so the display buffers stay part of the seeded baseline.
 */
bool heapShouldFailAllocation(size_t bytes, const char *owner);

/** Scenario-end leak accounting for non-persistent tagged allocations. */
uint32_t heapLeakCount(void);
std::string heapLeakReport(void);
std::string heapOwnerReport(void);

/** Timeline samples and JSON reports for the memory profiler. */
void heapSampleTimeline(void);
std::string heapTimelineJson(void);
size_t heapTimelineCount(void);
size_t heapMinimumLargest(void);
uint32_t heapAllocationCount(void);

/**
 * Force the next task creation with this name to fail, or every task creation
 * when name is "all". Used to exercise a production allocation-failure branch
 * without depending on a measured heap size.
 */
void heapSetTaskCreateFail(const std::string &name);
bool heapTaskCreateMustFail(const char *name);

/** Record and log one allocation failure in the ESP-IDF heap shape. */
void heapNoteAllocationFailure(size_t bytes, const char *task);

/**
 * Model one xTaskCreate allocation. Returns false after logging the failure
 * and recording the task name. chargedBytes receives the amount to release
 * when the task finishes, or zero when the model is inactive. external is true
 * when the charge came from the PSRAM pool rather than internal RAM.
 */
bool heapTaskCreate(const char *name, size_t stackBytes, size_t *chargedBytes, bool *external);

extern "C" size_t furble_sim_heap_free(uint32_t capabilities);
extern "C" size_t furble_sim_heap_largest(uint32_t capabilities);
extern "C" size_t furble_sim_heap_minimum(uint32_t capabilities);

}  // namespace Furble::Sim

#endif
