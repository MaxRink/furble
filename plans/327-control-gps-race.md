# 327 - Control target-set locking for the GPS fan-out

## Motivation

`Control::updateGPS()` runs on the GPS task (`src/FurbleGPS.cpp:2671`). It walked
`m_Targets` with no lock from `src/FurbleControl.cpp:673`. That vector is mutated
under `m_Mutex` by four other paths. Line numbers in this section are those of
the parent commit, `4af8772`:

| Mutator | Task | What it does |
| --- | --- | --- |
| `addActive()` (`:1245`-`:1291`) | UI, console, companion | `push_back()` into `m_Targets`, reallocating its buffer |
| `disconnect()` (`:1069`, `:1075`) | UI, companion | moves every target to the drain set, then `clear()` |
| `retireCancelledTargetsLocked()` (`:1144`-`:1159`) | control, `connectAll()` | moves declined targets out, then `erase()` |
| restart force-complete (`:1120`-`:1130`) | restart entry points | `clear()` under the lock |

A 1 Hz GPS fix landing on any of them read the vector's own storage, `_M_start`
and `_M_finish`, while it was being replaced. The walk could therefore follow a
freed or reallocated buffer, and the per-target call after it dereferenced a
`Target` the vector no longer owned.

The same function also had a second, quieter defect. `allConnected()` was
defined at `:681` and called from two places with different lock states:

- `connectAll()` at `:394`, inside the `lock_guard` opened at `:373`.
- The control task's `STATE_ACTIVE` branch at `:532`, holding nothing.

The branch went on to fan queued commands out to `m_Targets` at `:544`, also
unlocked. One function, two callers, two lock states, and a third unlocked walk
next to it: the kind of contract that only holds until the next caller.

## Design

- `Control::updateGPS()` takes `m_Mutex` for the walk and the per-target store,
  in one critical section, and releases it before posting `CMD_GPS_UPDATE` on the
  control queue. `Target::updateGPS()` is two struct assignments with no radio
  call, no allocation and no delay, which is what makes that acceptable and is
  now a documented caller-must-hold contract in `include/FurbleControl.h`.
- The queue post stays outside the lock, so the control queue's own lock is
  never taken under `m_Mutex`. The control task shares that queue lock with this
  post, and keeping the nesting one-way is what keeps the pair easy to reason
  about when the receive side later grows a Control call.
- `allConnected()` keeps taking the lock, for the UI, companion and host callers
  that hold nothing. Its body moved to `allConnectedLocked()`, marked as
  requiring `m_Mutex`, which `connectAll()` and the control task now use. The
  comment explaining why an empty session is not "all connected" moved with the
  body, unchanged.
- The control task's `STATE_ACTIVE` branch takes `m_Mutex` for the liveness check
  and again for the command fan-out. `Target::sendCommand()` is a non-blocking
  queue post, the same call `sendCameraCommand()` and `disconnect()` already make
  while holding that lock. `sampleAdaptivePower()` and `setState()` keep their
  original order and stay outside the lock.
- No lock is added, removed or reordered. `m_Mutex` remains the outermost
  Control lock: the governor mutex is only ever taken under it and released, and
  a hook's own locks come last (`include/CLAUDE.md`).

## Implementation state

Done on `fix/control-gps-race`, stacked on master `4af8772`. Host-only: no
hardware was flashed or measured.

The new probe is `tests/host/control_gps_race_test.cpp`, registered as
`control-gps-race` (ThreadSanitizer, through `tests/host/run_tsan_race.sh`) and
`control-gps-race-plain` (the same source without the sanitizer, so the walk also
runs in the normal suite and under ASan and UBSan). It runs one thread calling
`Control::updateGPS()` in a loop while the main thread alternates `addActive()`
and `disconnect()` for 250 cycles.

| Gate | Result |
| --- | --- |
| `control-gps-race` on the parent commit, 3 runs | all 3 alike: 3 race reports, then a SEGV during reallocation; wrapper exit 1 |
| `control-gps-race-plain` on the parent commit, reviewer host, 8 runs | not deterministic: 1 SIGSEGV (rc 139) and 2 passes in the first 3 runs, 6 of 8 passing |
| `control-gps-race-plain` on the parent commit, this host, 8 runs | 8 SIGSEGV (rc 139) in 8 sequential runs |
| `control-gps-race-plain` on the parent commit, ASan and UBSan build | deterministic, reproduced here: `member call on null pointer` at `FurbleControl.cpp:674` |
| `control-gps-race` with the fix | 0 race reports, `control-gps-race: PASS` |
| `control-gps-race-plain` with the fix, 5 runs | rc 0 on all 5, about 17,000 fixes per run |
| `ctest -R "^(control|tx-power)" -E fuzz` with the fix | 32 of 32 tests pass |
| `sh tools/run_sanitized_host_tests.sh --jobs 6` (ASan and UBSan, full suite) | 194 of 194 tests pass |
| `python3 tools/pre_review.py --base origin/master` | 10 gates pass |

The plain twin is a smoke test, not the gate. Its SIGSEGV needs the walk to catch
`m_Targets` mid-reallocation, so the rate follows the host's allocator and load:
1 SIGSEGV in 3 runs on the reviewer's host, 8 of 8 here. A pass from it proves
nothing. The deterministic forms both name the same defect: ThreadSanitizer
reported it on all 3 of 3 runs, and the ASan and UBSan build names the unlocked
dereference itself, on its first run here: `FurbleControl.cpp:674:13: runtime
error: member call on null pointer of type 'Furble::Control::Target'`, then the
same null `this` one frame down in `Target::updateGPS()` and an ASan SEGV writing
through it. That line number is the parent commit's
(`target->updateGPS(gps, local);` inside the walk); the fix moves it, so read it
as the defect's location and not as a line in the current file.

## Deviations and open items

- **The requested shared_ptr snapshot was not implemented, because there is no
  shared_ptr to copy.** `m_Targets` and `m_ZombieTargets` hold
  `std::vector<std::unique_ptr<Control::Target>>` (`include/FurbleControl.h`).
  Copying targets out to call them outside the lock would mean moving
  that ownership to `shared_ptr`, which costs a heap allocation on the GPS task
  every fix (`CONFIG_HEAP_ABORT_WHEN_ALLOCATION_FAILS=y` makes a failure an
  abort, plan 314), breaks the `TARGET_BYTES` admission accounting in
  `addActive()`, and lets the GPS task run a `Target` destructor. The lock gives
  the same guarantee the shared_ptr copy was for, that a target cannot be freed
  between the copy and the call, without either cost. Say so at review if the
  ownership move is actually wanted.
- **Open: the target-side GPS handoff.** `Target::m_GPS` and `m_Timesync` are
  written by `updateGPS()` under `m_Mutex` and read by the target task in the
  `CMD_GPS_UPDATE` case with no lock (`FurbleControl.cpp:198`). That is a
  separate, pre-existing race that predates this fix and is unchanged by it: the
  write is now serialized against other `Control` tasks, not against the target
  task's read. Fixing it needs a per-target handoff (a queue payload or a
  target-owned mutex) and its own probe. The new probe deliberately never reaches
  `STATE_ACTIVE`, so the sanitizer gate stays pointed at the vector.
- **Open: no sanitizer probe for the `allConnected()` and fan-out half.** The
  probe stays out of `STATE_ACTIVE` for the reason above, so the liveness check
  and the command fan-out that branch now does under `m_Mutex` are covered by
  the plain Control suite only, with no ThreadSanitizer probe of their own. A
  probe that drives them has to reach `STATE_ACTIVE`, which also drives the
  handoff read, and a report from it could not be attributed to one defect or
  the other. It waits for the handoff fix.
- The empty-session branch of `allConnectedLocked()` is still only reachable
  through the window recorded in plan 170, and still has no test.
- `plans/314-memory-resilience.md` was the wrong home for this: nothing here
  changes a memory budget or an admission gate, and the repo already keeps a
  race fix in its own numbered plan (325).
