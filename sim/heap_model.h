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

/** Charge one modelled allocation. Returns false when it cannot fit. */
bool heapReserve(size_t bytes);

/** Return a modelled allocation to the pool. */
void heapRelease(size_t bytes);

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
 * when the task finishes, or zero when the model is inactive.
 */
bool heapTaskCreate(const char *name, size_t stackBytes, size_t *chargedBytes);

extern "C" size_t furble_sim_heap_free(uint32_t capabilities);
extern "C" size_t furble_sim_heap_largest(uint32_t capabilities);
extern "C" size_t furble_sim_heap_minimum(uint32_t capabilities);

}  // namespace Furble::Sim

#endif
