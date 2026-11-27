# 337 - Simulator memory-management shims

Status: implemented on branch `fix/sim-mem-shims`, not merged. Gaps 1 and 3 add
two missing `esp_heap_caps.h` surfaces; gap 2 makes one modelled failure obey
the board abort policy. Gap 4 records what remains.

Related: [306](implemented/306-sim-memory-crash-repro.md) (the modelled heap
abort), [314](314-memory-resilience.md) (the device allocation counter and the
caps routing, including the #472 capability-set aggregation this plan rebased
onto), [docs/sim-memory-profiler.md](../docs/sim-memory-profiler.md) (the model
reference).

## Why

The simulator models the device heap behind `sim/shim/esp_heap_caps.h`, and the
firmware talks to the device heap through the same names. Three surfaces were
missing or wrong, and each one let a firmware path stay uncovered:

1. The shim declared `heap_caps_aligned_alloc`, `heap_caps_malloc`,
   `heap_caps_malloc_prefer` and `heap_caps_free`, but not
   `heap_caps_calloc`, `heap_caps_zalloc` or `heap_caps_realloc`. A firmware
   translation unit that calls `heap_caps_calloc` or `heap_caps_realloc` could
   not be compiled in the simulator at all. The only caller today is
   `lib/furble/BtDebugJournal.cpp`, whose calloc block sits under
   `#if defined(ESP_PLATFORM)`, a macro the simulator does not define: the
   simulator builds the `new[]` branch, so the calloc path had no host coverage
   and the modelled pool never saw the journal's ring, 128 records on an S3
   PSRAM build.
2. `heap_caps_get_info` and `multi_heap_info_t` did not exist in the shim, while
   `src/FurbleConsole.cpp` reads them for `perf heap`. The console is not part
   of the simulator build, so nothing failed to compile and nothing reported the
   omission.
3. A PSRAM task-stack request on a board whose PSRAM is absent or failed logged
   `SIM FAIL` and returned false from `Furble::Sim::heapTaskCreate()` without
   recording the failure. The pool-exhaustion branch eight lines below records
   it with `noteFailureLocked()`, which is what runs the board abort policy. The
   silent return let a firmware `pdFAIL` fallback pass for a configuration the
   device cannot run, because on hardware a `heap_caps_malloc` for a PSRAM
   stack aborts.

## Gap 1: heap_caps_calloc and heap_caps_realloc

`sim/shim/esp_heap_caps.h` now declares both, modelled on ESP-IDF 5.5.3
`components/heap/heap_caps_base.c`:

- `heap_caps_calloc(count, size, caps)` rejects an overflowing block count and
  reports the wrapped `count * size` through the failed-alloc hook, exactly as
  `heap_caps_calloc_base()` plus `heap_caps_calloc()` do. Otherwise it calls
  `heap_caps_malloc(total, caps)` and zeroes the block. Composing through
  `heap_caps_malloc` is the point: the charge, the caps filtering and the abort
  policy are shared with malloc rather than reimplemented.
- `heap_caps_realloc(pointer, size, caps)` keeps the `heap_caps_realloc_base()`
  edge cases: a null pointer is a malloc, a zero size frees and returns null,
  and a failed growth leaves the old block untouched. The model has no in-place
  growth, so it always allocates the new block, copies and frees the old one,
  which charges the peak the device also reaches when `multi_heap_realloc`
  cannot grow in place.

The copy needs the old block's size. The model exposes
`furble_sim_caps_block_size()`, which the simulator's counter answers with the
host allocator's usable size (`sim/heap_count.cpp`) and a host unit target
answers through the weak fallback in `sim/heap_model.cpp`.

`lib/furble/BtDebugJournal.cpp` still takes its `new[]` branch in the simulator.
Adding the names removed the compile blocker, it did not change which branch the
file selects, so the calloc path is not covered by a scenario yet.

What remains for this gap: `heap_caps_zalloc` (no firmware caller, so it would
be an untested name) and the journal branch, both listed under
[What remains](#what-remains).

## Gap 2: a PSRAM task-stack failure records like its sibling

`Furble::Sim::heapTaskCreate()` now calls
`noteFailureLocked(stackBytes, name, CAPS_EXTERNAL)` on the
`psramState != "ready"` branch, the same call the exhaustion branch beside it
makes. The `SIM FAIL` line stays as the observation; the recorded failure is
what the abort policy acts on, so the run ends with the panic exit status 3 and
a crash record that names the task.

`sim/fixtures/hardware/panic-psram-task-no-psram.txt` drives it: `psram_mode
absent` plus `task_stack_region psram` fails the first task created after the
seed, which is the 8 KiB Control task. `sim/scripts/run-panic-fixtures.sh` runs
it with the other allocation fixtures and checks the abort line, the task name
and the crash record on the next boot.

What remains for this gap: the fixture covers an absent PSRAM. The
`init_failed` state reaches the same branch and has no fixture of its own; it is
the same call, so the record and the abort are already exercised, but no
scenario boots with `psram_mode init_fail` and a PSRAM task stack.

## Gap 3: heap_caps_get_info and multi_heap_info_t

The shim now defines `multi_heap_info_t` with ESP-IDF's field names and order,
and `heap_caps_get_info()` fills it through
`furble_sim_heap_info()` in `sim/heap_model.cpp`, under one model lock so the
fields describe a single instant.

The byte figures match the per-caps getters for the two sets this test uses and
for any set that names one pool: `total_free_bytes` is
`heap_caps_get_free_size()` and `largest_free_block` is
`heap_caps_get_largest_free_block()` for `MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT`
and `MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT`. The caps resolve to one pool the
same way the getters resolved before #472: the SPIRAM bit selects the external
pool and everything else selects the internal one.

#472 then made the per-caps getters aggregate every pool a set matches, through
`capsPools()` and the pinned ESP32-S3 priority table, while `multi_heap_info_t`
still describes one heap. The two surfaces now diverge for a set that names no
pool: `heap_caps_get_free_size(MALLOC_CAP_DEFAULT)` sums internal and PSRAM, and
`heap_caps_get_info(..., MALLOC_CAP_DEFAULT)` reports the internal pool alone.
`heap_caps_get_minimum_free_size()` gained a PSRAM watermark from the same
change, so a PSRAM query there returns the PSRAM figure while
`minimum_free_bytes` stays the internal one. Both are listed under
[What remains](#what-remains).

Three fields describe the model rather than a device TLSF walk:

- `total_allocated_bytes` and `allocated_blocks` count only the blocks the model
  charged. A seeded free/largest pair is a measurement that already includes
  boot allocations, so those have no modelled block and the figures are a lower
  bound. A task counts as two blocks, its stack and its TCB, which is what
  `xTaskCreate` allocates and what the model already charges.
- `free_blocks` is one for a non-empty pool, because the model is one
  contiguous region per capability set and carries no fragmentation.
- `minimum_free_bytes` is the watermark of the pool the query resolved, so it
  agrees with `heap_caps_get_minimum_free_size()` on a single pool. It did not
  when this plan merged: the field carried the internal watermark for either
  pool, which the #506 review flagged and the follow-up at the end of this file
  fixed.

The model gained a per-pool live block count for `allocated_blocks`. It is
incremented where a block is charged and decremented where one is refunded, with
guarded decrements so a free that never charged cannot wrap the count.

What remains for this gap: the per-query pool selection and the single minimum
watermark, both listed under [What remains](#what-remains), and compiling
`src/FurbleConsole.cpp` in the simulator, which is what would consume
`heap_caps_get_info` in a simulator run.

## Finding: the documented CMake entry point aborted on exit

Unrelated to the shims, found while re-running this branch's gates, and fixed
here because it makes the entry point `docs/sim.md` documents produce a binary
that aborts.

Reproduction on `a5cb2e14`, before the fix:

```sh
git worktree add --detach /tmp/master a5cb2e14
cmake -S sim -B /tmp/cmake-master \
  -DFURBLE_DEP_ROOT=$PWD/.pio/libdeps/m5stick-s3 \
  -DFURBLE_LVGL_DIR=$PWD/managed_components/lvgl__lvgl
cmake --build /tmp/cmake-master --target furble-sim -j 8
printf 'wait 100\nexit\n' > /tmp/plain-wait-exit.txt
FURBLE_SIM_PREFS=/tmp/prefs-cmake /tmp/cmake-master/furble-sim \
  --script /tmp/plain-wait-exit.txt; echo $?   # 134
```

The same script on the `sh sim/build.sh` binary from the same commit exits 0,
and both binaries print the same eleven log lines, so the abort is not in the
scenario or the model. `lldb` stops on `SIGABRT` at `__stack_chk_fail` called
from `lgfx::v1::Panel_sdl::_event_proc()`:

```text
frame #2: libsystem_c.dylib`__abort + 152
frame #3: libsystem_c.dylib`__stack_chk_fail + 96
frame #4: furble-sim`lgfx::v1::Panel_sdl::_event_proc() + 1680
```

The cause is link order. `sim/CMakeLists.txt` passed the macOS SDL3
`-needed_library` through `target_link_options`, and CMake emits link options
before the libraries, so the link line started with
`-Xlinker -needed_library -Xlinker /opt/homebrew/lib/libSDL3.dylib` and reached
`libSDL2-2.0.0.dylib` after `liblvgl.a`. ld64 binds an undefined symbol to the
first input dylib that exports it, so every SDL2 call in the binary resolved to
SDL3:

```text
$ nm -m /tmp/cmake-master/furble-sim | grep SDL_PollEvent
                 (undefined) external _SDL_PollEvent (from libSDL3)
$ nm -m sim/build/furble-sim | grep SDL_PollEvent
                 (undefined) external _SDL_PollEvent (from libSDL2-2)
```

`SDL_PollEvent` fills the caller's `SDL_Event`. SDL3's is 128 bytes and the SDL
panel compiles against SDL2's 56, so the call overran the local `SDL_Event` in
`_event_proc` and the stack canary check aborted the process. `docs/sim.md`
documents `cmake -S sim -B ...` as an entry point with the same source and board
defaults as `sim/build.sh`, so this was a build-path defect, not a scenario
fault.

The fix passes the same flag through `target_link_libraries`, which places it
after `SDL2::SDL2`. Verified by relinking the existing build tree, with no
recompilation: the binding moves to `libSDL2-2`, and the script above exits 0 on
two consecutive runs. The same pair of relinks on this branch's build tree on
`b711a44a` reproduces both directions, so the ordering, not the base, is the
cause:

```text
$ nm -m /tmp/furble-sim-prefix-order | grep SDL_PollEvent   # flag before the libraries
                 (undefined) external _SDL_PollEvent (from libSDL3)
                               exit 134
$ nm -m /tmp/simshims-cmake-branch/furble-sim | grep SDL_PollEvent   # flag after SDL2
                 (undefined) external _SDL_PollEvent (from libSDL2-2)
                               exit 0
```

`nm -m` is the check, and `sim/CLAUDE.md` records it so the next edit does not
move the flag back.

This is macOS only: it needs sdl2-compat with SDL3 beside it, so no Linux job
sees it.

## What remains

- `src/FurbleConsole.cpp` is still not compiled in the simulator, so `perf heap`
  has no simulator coverage. Compiling it is a separate change: it drags the
  console task, the UART line shim and the `FURBLE_CONSOLE` setting gates into a
  build that currently has none of them.
- `lib/furble/BtDebugJournal.cpp` still builds its host `new[]` branch in the
  simulator. Its `ESP_PLATFORM` guard is the branch selector, not the presence
  of the shim names. Compiling the calloc branch in the simulator needs a
  decision between defining `ESP_PLATFORM` for firmware sources and adding a
  narrower seam to the file, and either one needs the journal's PSRAM then
  internal fallback exercised in a scenario.
- `heap_caps_zalloc` is not in the shim (no caller).
- `heap_caps_get_info` reports one pool per query. ESP-IDF sums every heap whose
  capability set matches, so a `MALLOC_CAP_8BIT` or `MALLOC_CAP_DEFAULT` query
  reports the internal pool alone, where the per-caps getters aggregate both
  pools since #472.
- ~~The model keeps one minimum watermark in the struct, so a PSRAM
  `minimum_free_bytes` is the internal figure. A per-pool watermark in the
  struct is a model change, not a shim change.~~ Closed by the follow-up: the
  model already had `heapPsramMinimumFree()` from #472, so selecting it for an
  external read is a shim fix, not the model change this row assumed.
- `free_blocks` is one for a non-empty pool, because the model carries no
  fragmentation, so the field and any fragmentation-sensitive gate built on it
  say nothing about a device TLSF walk.
- The firmware's plain `realloc` still resolves to the host libc in the
  simulator; only `heap_caps_*` requests are modelled. No firmware source calls
  `realloc` today.

## Tests

`tests/host/sim_heap_caps_shim_test.cpp` (`ctest -R sim-heap-caps-shim`) links
`sim/heap_model.cpp` and `sim/heap_count.cpp`, so the charge runs through the
simulator's device counter rather than a host fallback, and compiles with
`CONFIG_HEAP_ABORT_WHEN_ALLOCATION_FAILS` because every furble sdkconfig sets
it. The file and the test name carry `shim` because `sim/heap-caps` pinned the
`MALLOC_CAP_*` bit values in `tests/host/sim_heap_caps_test.cpp` while this
branch was open. That branch has since merged as #472, and the two targets stay
separate: `sim-heap-caps` owns the bit values, the caps filtering and the
priority table, `sim-heap-caps-shim` owns the calloc, realloc and get_info
surfaces. Neither target name nor file path is shared, so both run in the host
job. It covers:

- the calloc charge: the PSRAM pool and largest block drop by the block cost,
  the counted PSRAM total rises by the same figure, and both are restored on
  free;
- the zeroing, on a block that was dirtied and returned first;
- the caps filtering: an `MALLOC_CAP_INTERNAL` calloc moves the internal pool
  and leaves the PSRAM pool and its counted total untouched;
- realloc: the new size is charged, the old block is refunded, the contents
  survive, `realloc(NULL, n)` allocates and `realloc(p, 0)` frees and returns
  null;
- three forked abort cases, each asserting the panic exit status 3 and the
  panic record: a PSRAM calloc on a board with no PSRAM, an exact-size injected
  failure that must name the calloc total rather than the element count, and a
  realloc that cannot grow. The record must carry the hardware-shaped
  `Mem alloc fail` line, the requested size and the `0x00000404` SPIRAM
  capability pair;
- `heap_caps_get_info` against the per-caps queries for both pools, with the
  two pools seeded an order of magnitude apart so a caps-blind implementation
  cannot satisfy both, plus the block ledger (`allocated_blocks`,
  `total_allocated_bytes`, `free_blocks`, `total_blocks`) across an allocation
  and a free. `minimum_free_bytes` is asserted against
  `heap_caps_get_minimum_free_size()` for both pools after the follow-up below.

The zeroing case is a real gate on the CI lane and a vacuous one on a macOS
developer host: glibc hands a freed block of that size straight back out of the
tcache with the pattern intact, while macOS 14 zeroes small blocks on free. The
test comment says so.

Mutation checks, each reverted before the commit, confirmed the cases fail for
the right reason:

| Change | Failing checks |
| :--- | :--- |
| calloc loses the caps argument | `the calloc charge is the requested block cost`, the PSRAM abort case exits 92 instead of 3, the record caps check |
| realloc keeps the old block charged | `the realloc charge is the new block, not both blocks`, both pool refund checks |
| realloc drops the copy | `heap_caps_realloc keeps the old contents` |
| calloc drops the memset | passes on macOS, which zeroes freed small blocks; the glibc CI lane is the gate |

## Evidence

Host, this branch on `afb8513b`, all gates run to completion:

```sh
cmake -S tests/host -B /tmp/host-build
cmake --build /tmp/host-build --target sim_heap_caps_shim_test sim_scheduler_test \
  sim_webui_memory_model_test sim_clock_test sim_heap_caps_test
ctest --test-dir /tmp/host-build \
  -R 'sim-heap-caps-shim|sim-scheduler|sim-webui-memory-model|sim-clock|sim-heap-caps$'
```

`5/5` pass: the four targets this plan names plus `sim-heap-caps`, the #472
target that owns the capability table and which the rebase had to keep green. The
abort cases print, from the forked children:

```text
[E] heap: Mem alloc fail. size 0x0100 caps 0x00000404
abort() was called: Mem alloc fail. size 0x0100 caps 0x00000404
SIM PANIC: allocation failure in task heap-caps-test aborts the device
```

Simulator, built with `sh sim/build.sh` against LVGL 9.4.0 from
`managed_components` and the pinned M5GFX, M5Unified and TinyGPSPlus sources.
LVGL is present, so this is a real scenario run, not a partial one. The new
fixture exits 3 and the next boot reports the record:

```text
[E] heap: SIM FAIL: PSRAM task stack requested with PSRAM absent
[E] heap: Mem alloc fail. size 0x2000 caps 0x00000404
abort() was called: Mem alloc fail. size 0x2000 caps 0x00000404
SIM PANIC: allocation failure in task control aborts the device
```

The next boot prints the record and the `state` queries read it back:

```text
[E] crash: CRASH RECORD: reset=panic task=control uptime=0s heap_free=204800 heap_largest=163840 clean=no alloc_fail=8192 caps=0x00000404 in=control fn=heap_caps_malloc
state crash.task = control
state crash.last_log = Mem alloc fail. size 0x2000 caps 0x00000404
state crash.alloc_size = 8192
state crash.alloc_caps = 0x00000404
state crash.alloc_task = control
state crash.alloc_function = heap_caps_malloc
```

The whole allocation-abort gate, both simulator binaries built from this
branch, exits 0 and covers the new fixture beside the three that already
existed:

```text
PASS: sim/fixtures/hardware/panic-webui-pre-gate-alloc.txt aborted and its record survived the reboot
PASS: sim/fixtures/hardware/panic-icon-no-psram.txt aborted and its record survived the reboot
PASS: sim/fixtures/hardware/panic-psram-task-no-psram.txt aborted and its record survived the reboot
PASS: sim/fixtures/hardware/panic-mqtt-task-alloc-fail.txt aborted and its record survived the reboot
Allocation abort gate passed.
```

`python3 tools/pre_review.py --base origin/master` passes all 10 gates.

## Implementation state

Not merged. Branch `fix/sim-mem-shims`, rebased onto `afb8513b` so it keeps
#472's capability-set model beside these shims, and then onto `bbe4d22c` for
the merge train once #479 and #487 landed. The second rebase had no conflict.
The host targets, `tools/pre_review.py`, the doc token check and the panic
fixtures below were rerun on the `bbe4d22c` tree; the mutation table was not
rerun. Files:
`sim/shim/esp_heap_caps.h`, `sim/heap_model.cpp`, `sim/heap_count.cpp`,
`tests/host/sim_heap_caps_shim_test.cpp`, `tests/host/CMakeLists.txt`,
`sim/fixtures/hardware/panic-psram-task-no-psram.txt`,
`sim/scripts/run-panic-fixtures.sh`, `sim/CMakeLists.txt` (the CMake entry point
finding above), `sim/CLAUDE.md`, `docs/sim-memory-profiler.md`, `docs/sim.md`,
`plans/README.md`, this plan.

Deviations from the written plan, all recorded above: the rebase resolves two
conflicts by keeping both sides, #472 changed what the per-caps getters return so
Gap 3's claims about them are restated, and the CMake entry point finding was
fixed rather than deferred because it makes a documented entry point produce an
aborting binary.

## Follow-up: the info watermark ignored the pool

Source: the retrospective review of #506 (`plans/implemented/357-review-feedback.md`).

`heap_caps_get_info()` resolves one pool per query from the SPIRAM bit, but it
took `minimum_free_bytes` from the internal pool whichever pool it resolved. So
`heap_caps_get_info(&info, MALLOC_CAP_SPIRAM).minimum_free_bytes` returned the
internal watermark while `heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM)`
returned the PSRAM one, and the code comment claimed the field matched the
getter "for every caps set". ESP-IDF sums the matching heaps and each heap
reports its own `multi_heap_get_info()` watermark, so the device never mixes the
two.

Fixed by selecting `heapPsramMinimumFree()` for the external pool. The model
already carried the figure from #472, so this was a shim bug, not a model gap.
`sim_heap_caps_shim_test` now asserts the info field against the per-caps getter
for both pools, and that the two pools report different watermarks.
