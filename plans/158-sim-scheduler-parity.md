# 158 - simulator scheduler and hardware parity

## Objective

Make the host simulator behave identically to firmware wherever the behavior
can be represented on the host. The target is 100 percent hardware identity,
not merely a stable screenshot or a passing UI scenario. Every remaining
physical boundary must be measured, bounded, and explicitly gated.

This first PR is deliberately limited to the simulator scheduler and orderly
task lifecycle. It creates the foundation on which the production connection
stack and calibrated peripheral models can be added without hiding timing,
ownership, or teardown defects.

## Implementation state

Phase 2 is implemented by plan 163: production `Control`, `Camera`,
`CameraList`, `Scan`, `Device` and every vendor class now build in the SDL
simulator over the shared MockNimBLE boundary and the virtual camera peers,
the four simulator substitutes are deleted, and `link_lies` is gone in favour
of transport-level faults that never touch control state. Steps 1, 2 and 4 of
Phase 2 are done. Step 3 (generation and cancellation tests for every handshake
phase) is partly covered by the existing host suite and step 5 (a differential
trace against hardware) is not started.

Phase 2 also surfaced one Phase 3 item that must not be described as closed: a
task that only wakes on a virtual-clock deadline can be starved while the UI
thread drives virtual time forward in large steps. It shows up as a control
task that stops re-polling `Camera::connectCancelled()` inside a long vendor
wait, so an interactive disconnect falls back to its 30 s cap. Plan 163 records
the reproducer.

The first PR under this plan implements the first scheduler foundation: a shared 64-bit
virtual clock, virtual FreeRTOS and esp_timer deadlines, queue-front sends,
joinable task workers and one timer dispatcher, cooperative shutdown, orderly
simulator exit, and focused deadline, wrap, queue, timer, and shutdown tests.
It also joins the
M5GFX debugger worker before SDL teardown because the dependency otherwise
discards that thread handle and crashes a normal process exit.

The full scenario gate exposed one cross-task deadlock that the old wall-clock
queue shim had hidden. `GPS::task()` held its service mutex while waiting on a
timed UART queue, while the UI settings callback needed that mutex before the
UI could advance virtual time. GPS settings transitions now set the enable gate
false and send a private front-of-queue wake event before waiting for the
service mutex. The task discards that event under the disabled gate, preserving
atomic GPS processing and settings resets without a simulator-only time seam.
The UI no longer waits behind an idle UART receive on hardware either.

A final lifecycle review found a second ownership boundary: ESP-IDF timer
deletion is deferred through the timer task, so a running callback may still
use its argument after `esp_timer_delete()` returns. Simulator rig teardown now
quiesces and joins all socket workers, joins firmware tasks and the timer
dispatcher, and only then destroys the rig service that owns timer callback
arguments. This preserves the hardware deletion model without a simulator-only
synchronous-delete guarantee.

The same review exposed companion state races while exercising that lifetime:
the timer callback and disconnect path could release the same plain shutter
flag concurrently, while session and notification workers could race the
status cache. `CompanionService` now serializes trigger state, timer and
disconnect release, and status compare/update under its mutex. Focused host
coverage overlaps timed release with disconnect, requires exactly one release,
and overlaps two unchanged status publications without a duplicate notify.

The delete-other lifecycle slice now retains each task record in the scheduler
registry through reset and models `running`, `stop_requested`, `finished`,
`joining`, and `joined`. An external `vTaskDelete()` requests cooperative
unwind, waits for the target to publish `finished`, and claims the single join;
concurrent stop callers wait for that claimant. Self-delete remains
nonblocking and unwinds at the task boundary. This cooperative unwind is safer
for caller-owned simulator state than abrupt cleanup, but it is not abrupt
FreeRTOS cleanup parity: code that never reaches a shim boundary can still
delay quiescence. Production Companion remains blocked on this boundary until
its worker-owned shutdown path is implemented and reviewed.
The simulator-only task shim exposes the retained lifecycle and join waiter /
claim state so host barriers can wait for exact protocol states rather than
infer them from sleeps or task-body flags.

Unexpected task exceptions are recorded on the retained task record, request
the simulator's failure-result seam (upgrading an earlier success to nonzero),
and wake the scheduler before the task publishes `finished`. Focused host
coverage joins that worker and verifies the nonzero result.

The next scheduler slice adds a deterministic dispatch gate at simulator
FreeRTOS boundaries. Each task retains its requested priority, creation order,
and ready order. A runnable task with the greatest priority is admitted first;
equal-priority tasks rotate by a stable ready sequence. Deadline waiters are
released as one virtual-time batch before host condition-variable wakeups are
observed, and queue waiters are selected by priority then wait order. This
removes the host thread wake race from same-tick scheduling while preserving
the FreeRTOS priority rule. Zero-tick delays are explicit scheduler yields. The gate is
cooperative at shim boundaries: arbitrary CPU work between boundaries still
cannot model instruction-level preemption or core affinity and remains an
explicit parity gap.

Queue wake selection now applies the FreeRTOS priority rule before the FIFO wait
tie-break and releases exactly one waiter per queue item. Timed queue waits
latch `ready`, `timeout`, or `cancelled` before the waiter is scheduled, so a
post-deadline item cannot retroactively satisfy the receive. Queue deletion
wakes active users and defers reclamation until their queue-use guards drain;
task-owned deletion temporarily yields the scheduler turn so that drain can
complete without a lock-held deadlock.

The esp_timer shim models its serialized dispatcher as the ESP-IDF 5.5.3
`ESP_TASK_TIMER_PRIO` timer service task (`configMAX_PRIORITIES - 3`, normally
22). A due timer marks that service runnable in the same virtual-time batch as
FreeRTOS waiters before the first dispatch, and callbacks execute only after
the same scheduler gate admits the service. Equal-deadline timers retain arm
order.
Task creation publishes its ready record before starting the host worker, so
the scheduler never observes a task only after `xTaskCreate` returns.

Phase 1 is not complete. Multiple workers that become ready on the same tick
now pass through a deterministic priority dispatcher at scheduler boundaries.
CPU execution consumes no virtual time, instruction-level preemption and core
affinity are not represented, and some non-FreeRTOS peripheral workers still
need conversion to scheduler events. Current reports and traces must not claim
instruction-level task timing or hardware timing.

`vTaskDelete(other)` is cooperative: it requests stop and waits for the target
to publish quiescence before the caller can destroy caller-owned state. The
simulator's join is safer than abrupt cleanup, but it is not abrupt FreeRTOS
cleanup parity when a worker never reaches another shim boundary. Production
Companion remains blocked pending a worker-owned shutdown path. Queues are also
owner-destroyed rather than globally registered.

One-shot timer callbacks now share a single serialized dispatcher, preserve arm
order for equal deadlines, can cancel another due timer before its callback,
and can delete themselves. The scheduler clock remains microsecond based for
esp_timer deadlines while the FreeRTOS tick surface stays millisecond based.
Focused host coverage verifies an exact sub-millisecond deadline.

## Findings driving the order

The Sol review found that the pre-plan-158 SDL build was a useful UI harness,
but was not a hardware-faithful simulator:

- Control, Camera, CameraList, and Scan are simulator policy substitutes. The
  fake connection path cannot reproduce production ownership, cancellation,
  callback ordering, timeout, or link truth failures.
- The host used detached threads, wall-clock queue waits, host timers, and a
  virtual firmware clock. The current foundation replaces those waits and
  lifecycles; its deterministic dispatch gate resolves same-tick ordering at
  scheduler boundaries, while CPU execution still consumes no virtual time.
- The production Control review reported concurrency leads involving stale
  connect generations, callback-before-delete client lifetime, same-Camera
  reconnect, target queue allocation, and release discarded by queue reset.
  These are review leads, not accepted defects, until a pinned sanitizer log
  or focused reproducer is added to the evidence ledger.
- The current liveness scenarios deliberately inject divergence into the fake
  path. They are valuable interim guards, but passing them does not validate
  production Control.
- Power reports are advisory. A missing or dead GPS can hold the S3
  `NO_LIGHT_SLEEP` lock forever because the initial ACQUIRING probe deadline is
  zero. The reported 0.31 mA screen-off value is also invalid because the
  simulated 200 Hz UI work consumes zero simulated time. Wake overhead,
  negotiated BLE duty, brightness, GPS cold start, PMIC behavior, and board
  rails are not yet fully modeled.

## Phase 1: deterministic unified scheduler

Implement one virtual scheduler shared by FreeRTOS delays, queue deadlines,
and `esp_timer` callbacks.

- Make virtual time the source of truth for `xTaskGetTickCount`, delays,
  timed queue receives, and timers. Wall time may remain only as an outer
  watchdog for a hung test.
- Add the FreeRTOS queue operations needed by production code, including
  queue-front sends, and preserve exact deadline and tick-wrap semantics.
- Replace detached task threads with joinable task records. Stopping a run
  must signal blocked delays and queue waits, wake every task, and join every
  task. The current ownership order is UI return, Scan join, companion-rig
  worker quiescence, task join, timer join, rig service destruction, UI
  destruction, simulator-thread join, M5GFX worker join, then SDL close.
  Queues remain owner-destroyed rather than globally
  registered, so leak checks are still required.
- Replace worker `_Exit` paths with an orderly exit request and result code.
  The driver must return only after the scheduler and all workers are stopped.
- Keep scheduler observation pure. A state query or profiler read must not
  advance time, feed a watchdog, mutate connection state, or consume an event.

### Phase 1 acceptance

- A queue item, a virtual deadline, and shutdown each wake a waiting task with
  the same precedence as the firmware contract.
- A task scheduled at an exact deadline runs once, and uint32 tick wrap is
  covered by a focused test.
- Same-tick runnable tasks select greatest FreeRTOS priority, and equal-
  priority zero-tick yields rotate deterministically by ready order.
- Sub-millisecond `esp_timer` deadlines remain exact even though the FreeRTOS
  tick surface is millisecond based.
- Repeated runs with the same board and script produce the same event trace,
  counters, and exit status without relying on host scheduling.
- Normal exit, assertion failure, timeout, and injected worker failure all
  join tasks and release queues, timers, locks, and peers under ASan and UBSan.
- Existing simulator scenarios and host tests still pass. A test that cannot
  prove behavior at the scheduler boundary is not accepted as parity evidence.

## Phase 2: production connection vertical slice

After Phase 1, remove the simulator policy substitutes in one vertical slice:

1. Build production Control, Camera, CameraList, and Scan sources in SDL.
2. Put MockNimBLE at the actual host-device boundary, with virtual peers for
   advertising, pairing, registration, notifications, link loss, timeout,
   peer reset, and callback ordering.
3. Add generation and cancellation tests for stale connect, disconnect during
   every handshake phase, same-target reconnect, queue allocation failure,
   release while a queue is reset, and interval/manual-shutter quiescence.
4. Run the existing UI and liveness scenarios over that stack. Remove
   `link_lies` as a required workaround once transport-level faults express
   the same failure without changing Control state behind its back.
5. Compare a production event trace against a hardware trace with the same
   inputs. State publication, queue operations, timer deadlines, link state,
   and cleanup must match before the scenario is marked parity-complete.

Plan 159 defines the fail-closed virtual-peer evidence model. Public and common
GitHub implementations enrich peer behavior, but exact camera certification
requires a complete model and firmware capture corpus.

## Phase 3: board, peripheral, and power parity

Add exact board profiles for every supported panel and model each available
peripheral at the same firmware boundary: PMIC, display, backlight, GPS/UART,
IMU, SD, input, BLE radio, and sleep/DFS policy. Calibrate state currents,
wake costs, clock behavior, scan and connection duty, GPS cold and warm start,
and failure behavior from hardware traces. Invalid or missing calibration must
fail closed and mark the scenario uncalibrated, never produce a plausible
number silently.

Run differential traces for boot, menu idle, connect, reconnect, screen dim,
screen off, GPS acquisition, GPS standby, intervalometer, battery transitions,
and every supported board. Gate release claims on explicit tolerances per
observable state. Keep model-only power optimization comparisons advisory
until the corresponding hardware trace exists.

## Dependencies and boundaries

Phase 1 is independently mergeable and must precede the production connection
vertical slice. Phase 2 must precede connection, sleep, or liveness parity
claims. Phase 3 must precede absolute current, runtime, or default-power
decisions. Plans 63, 98, 111, 113, and 155 remain useful coverage and analysis
documents, but their current estimates and fake-path passes are incomplete.

Physical radio interference, analog current, sensor noise, silicon wake
latency, unavailable peripherals, and camera firmware behavior cannot be
perfectly recreated in a host process. They are not excuses to lower the
target: each is a named boundary with a measured distribution, a tolerance,
and a release gate. Any behavior outside those bounds is a parity failure or
an explicitly documented unsupported case.

## Verification and handoff

Phase 1 requires focused scheduler tests, existing host tests, all simulator
scenario suites, ASan and UBSan, and a repeated-run event-trace comparison.
The PR must report known timing and teardown differences, exact test commands,
and the remaining seam inventory. A later PR may claim production connection
parity only after its MockNimBLE trace tests and independent Sol review pass.
