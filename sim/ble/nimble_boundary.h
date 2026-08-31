#ifndef FURBLE_SIM_NIMBLE_BOUNDARY_H
#define FURBLE_SIM_NIMBLE_BOUNDARY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The names and ownership rules mirror the small NPL surface used by the
 * Apache NimBLE host.  This adapter intentionally omits controller, HCI and
 * security implementations.  A caller must treat an absent operation as
 * unsupported instead of silently using a C++ mock shortcut.
 */
typedef uint32_t ble_npl_time_t;

typedef struct ble_npl_event {
  void (*fn)(struct ble_npl_event *event);
  void *arg;
  uint64_t due_ms;
  uint64_t sequence;
  uint8_t queued;
} ble_npl_event;

typedef struct ble_npl_callout {
  void (*fn)(struct ble_npl_callout *callout);
  void *arg;
  ble_npl_time_t due_ticks;
  uint64_t sequence;
  uint8_t queued;
} ble_npl_callout;

void furble_ble_npl_reset(void);
uint64_t furble_ble_npl_now(void);
void furble_ble_npl_advance(uint64_t milliseconds);
int furble_ble_npl_eventq_put(ble_npl_event *event, uint64_t delay_ms);
int furble_ble_npl_eventq_remove(ble_npl_event *event);
uint32_t furble_ble_npl_run_due(void);
uint32_t furble_ble_npl_pending(void);
int furble_ble_npl_callout_reset(ble_npl_callout *callout, ble_npl_time_t delay_ticks);
int furble_ble_npl_callout_stop(ble_npl_callout *callout);
uint32_t furble_ble_npl_callout_pending(void);

#ifdef __cplusplus
}
#endif

#endif
