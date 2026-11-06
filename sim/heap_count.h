#ifndef FURBLE_SIM_HEAP_COUNT_H
#define FURBLE_SIM_HEAP_COUNT_H

/*
 * Device allocation counting for the host simulator.
 *
 * The simulator replaces the global operator new/delete and compiles LVGL's C
 * library allocator against furble_sim_fw_malloc, so firmware allocations
 * draw from the modelled heap the way they draw from TLSF on the device. An
 * allocation is a device allocation when it runs on a device task thread and
 * outside a host scope. The simulator's own work on those threads (scenario
 * queries, scheduler bookkeeping, the virtual camera peers) runs inside a
 * HostAllocScope and stays on the host heap.
 */

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void *furble_sim_fw_malloc(size_t size);
void *furble_sim_fw_realloc(void *pointer, size_t size);
void furble_sim_fw_free(void *pointer);

/* Entered and left by HostAllocScope; also called by lib/testing doubles. */
void furble_sim_host_alloc_enter(void);
void furble_sim_host_alloc_exit(void);

/*
 * Host unit targets link simulator modules without the device counter. These
 * weak no-ops let them link; sim/heap_count.cpp provides the strong
 * definitions in the simulator.
 */
#if !defined(FURBLE_SIM_HEAP_COUNT_IMPL)
__attribute__((weak)) void furble_sim_host_alloc_enter(void) {}
__attribute__((weak)) void furble_sim_host_alloc_exit(void) {}
#endif

#ifdef __cplusplus
}

namespace Furble {
namespace Sim {

/** Mark the calling thread as a device task thread. */
void heapMarkDeviceThread(const char *name);
#if !defined(FURBLE_SIM_HEAP_COUNT_IMPL)
__attribute__((weak)) void heapMarkDeviceThread(const char *) {}
#endif

/**
 * Re-enter device counting inside a host scope, for a harness step that calls
 * straight into a firmware handler.
 */
class DeviceAllocScope {
 public:
  DeviceAllocScope();
  ~DeviceAllocScope();
  DeviceAllocScope(const DeviceAllocScope &) = delete;
  DeviceAllocScope &operator=(const DeviceAllocScope &) = delete;

 private:
  int m_SavedDepth;
};

/** Keep the simulator's own allocations in this scope off the device heap. */
class HostAllocScope {
 public:
  HostAllocScope() { furble_sim_host_alloc_enter(); }
  ~HostAllocScope() { furble_sim_host_alloc_exit(); }
  HostAllocScope(const HostAllocScope &) = delete;
  HostAllocScope &operator=(const HostAllocScope &) = delete;
};

}  // namespace Sim
}  // namespace Furble
#endif

#endif
