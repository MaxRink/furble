# 547 - Simulator memory coverage, review feedback

Review of PR 547 (`test/sim-memory-100`, head `9a476c9e`) against `18bbcb5a`.
Verdict: request changes. Findings 1 and 2 block; 3 to 6 should be fixed in the
same pass; 7 is optional. Posted as a PR review comment because the
authenticated account is the PR author and GitHub refuses a formal
request-changes from the author.

The pass is real work: I rebuilt both simulator binaries and the host targets,
reverted each model change one at a time, and every one of them is load-bearing.
The findings are about what the plan claims, not about the mechanism it added.

## 1. plan 370 states a link-time guarantee the shim does not provide (plans/370-sim-memory-coverage.md:57-60)

```text
`esp_heap_caps_init()` and a bare `malloc()` family are absent by
construction: the shim declares neither, so a firmware call would fail to link
rather than silently succeed on the host heap.
```

True for `esp_heap_caps_init()`. False for `malloc`/`calloc`/`realloc`/`free`:
`sim/shim/esp_heap_caps.h:4-17` includes `<cstdlib>` and `<stdlib.h>`, which
declare the whole family, and no simulator source defines those symbols, so the
host libc resolves them.

```text
$ clang++ -std=c++17 -c -I sim/shim -I sim -I include -o /tmp/revwork/mallocprobe.o /tmp/revwork/mallocprobe.cpp
COMPILED
$ nm -u /tmp/revwork/mallocprobe.o
_free
_malloc
$ clang++ -std=c++17 -I sim/shim -I sim -I include -o /tmp/revwork/mallocprobe /tmp/revwork/mallocprobe.cpp && /tmp/revwork/mallocprobe
host block 0x101404f40
link+run rc=0
```

A firmware `malloc()` therefore compiles, links and escapes the model silently,
which is the defect class this PR closes. Nothing enforces the claim either:
`grep -rln 'malloc' tools/*.py` finds only `derive_sim_heap.py`, and no gate
inspects firmware allocation calls. Fix by adding a gate that fails on a bare
malloc-family call in `src/`, `lib/` or `include/`, or by replacing the sentence
with "no call site today, and a new one would not be caught".

## 2. The double-free window collapses on address reuse (sim/heap_count.cpp:104-107, 115-118)

```cpp
void markAllocatedLocked(registry_t &counts, void *pointer) {
  counts.freed.erase(pointer);          // freedOrder keeps the stale entry
}
...
  while (counts.freedOrder.size() > FREED_HISTORY_BLOCKS) {
    counts.freed.erase(counts.freedOrder.front());   // may erase a live entry
    counts.freedOrder.pop_front();
  }
```

A free, reuse and second free of one address leaves two entries in
`freedOrder`. When the stale one reaches the front it erases the address from
`freed` although the newer entry is still inside the window, so the address
becomes untracked with its most recent free only a few allocations back. A
later double free then reaches `std::free`, which is the glibc abort this PR
replaces. Replay of the algorithm with the capacity scaled to 4:

```text
free A      -> freed, tracked
free A      -> freed, tracked          # reuse leaves a stale order entry
A tracked after 5 other frees? False
double free of A -> MISSED -> reaches std::free -> glibc abort
```

The known limit at plans/370-sim-memory-coverage.md:265-270 describes a 4,096
address window. What the code implements is 4,096 releases after the first free
of each address, which for a reused address is much less. No false positives, so
this is not urgent. Removing the address's order entry on reallocation, or
stamping entries, closes it.

## 3. "unit + seed" overstates the plain `new` coverage (plans/370-sim-memory-coverage.md:76)

```text
| `new`/`malloc` failure by exact size and owner | none | unit + seed | `malloc-injected`, `alloc_fail` |
```

The only `alloc_fail` fixture fails the reservation probe at
`sim/SimMQTTClient.cpp:441-448`, not the plain path at `sim/heap_count.cpp:217-221`.
No scenario or fixture drives that block. Reverting the SimMQTTClient probe
leaves `panic-mqtt-reconnect-alloc` at exit 0 with the retries running, which
shows that fixture is the only seed consumer. Drop "+ seed", or add a fixture
that fails a real `new` through `alloc_fail`.

## 4. The camera fixture is described as pool exhaustion, and is not (AGENTS.md:127, docs/sim-coverage.md:118-119)

Both texts list "an exhausted internal pool during a camera connect".
`sim/fixtures/hardware/panic-camera-connect-alloc.txt` seeds
`heap_internal_free 60000` and `heap_internal_largest 50000`, a healthy pool, and
fails through `seed task_create_fail FauxNY-40`. The abort is an injected
`xTaskCreate` failure: a legitimate modelled allocation failure, but not
exhaustion. Reword to "a failed per-camera task allocation during a connect".

## 5. `heap_caps_malloc_prefer` is missing from the inventory (plans/370-sim-memory-coverage.md:39-56)

The inventory is the audit artifact and it omits a called API:
`src/FurbleUI.cpp:123`. It has a coverage row at :73 and an abort case
(`prefer-exhausted`), so only the inventory table is short. Add the row.

## 6. The heap-trace allocation is unreachable and not listed as a limit (src/FurbleConsole.cpp:2156, 2181)

The fifth `heap_caps_malloc` site allocates the trace buffer behind
`#if defined(CONFIG_HEAP_TRACING_STANDALONE)`. The modelled board turns tracing
off, so the sim compiles the stub, and `sdkconfig.debug:7` turns standalone
tracing on for a build where `heap_trace_init_standalone()` and
`heap_trace_start()` also allocate with no `sim/shim/esp_heap_trace.h`. One of
the 5 call sites therefore has no failure injection in either configuration.
Add it to Known limits next to the `esp_get_free_heap_size` entry.

## 7. Optional: `psram_free` has no ceiling (sim/driver.cpp:2792-2820)

The new block rejects `psram_largest > psram_free` and a missing `psram_mode`.
It does not reject `psram_free` above the modelled module, and `heapSetPsram()`
takes the value unconditionally, so a scenario can model a 200 MiB PSRAM on an
8 MiB part. No current scenario does. A clamp keeps the model physical.

## What held, with the commands

- Host targets at this head, all PASS: `sim_heap_caps_test`,
  `sim_heap_caps_shim_test`, `sim_webui_memory_model_test`,
  `memory_pressure_test`.
- Each model change is load-bearing. Removing the plain-path probe:
  `FAIL: malloc-injected exited 92, expected the panic status 3`. Removing the
  `freed` branch: `FAIL: double-free did not exit normally`. Removing the `skip`
  decrement: `the skipped occurrence must be served` and `the occurrence named by
  the rule must fail`. Removing the PSRAM seeding: the psram-exhausted fixture
  exits 0 with no abort. Removing the SimMQTTClient probe: the reconnect fixture
  exits 0.
- Abort contract on all seven fixtures with both binaries: exit 3, the exact
  `abort() was called: Mem alloc fail. size ... caps ...` line, the matching
  `SIM PANIC: allocation failure in task ...`, and a record that survives the
  next boot carrying `crash.alloc_size`, `crash.alloc_caps`, `crash.alloc_task`.
  None is panic injection. The reconnect fixture logs
  `MQTT attempt 1 failed, retrying in 10000 ms` before the abort and its record
  reads `alloc_size = 3072`, `alloc_task = mqtt_client`.
- No budget, floor or seed weakened: `tools/derive_sim_heap.py` and
  `sim/coverage/board_heap.json` are untouched, there is no `src/`, `include/` or
  `lib/` change, and `python3 tools/check_sim_coverage.py` reports 206 / 23 here
  against 206 / 20 on `origin/master`, so the 198 to 206 correction is real and
  the crash delta is the three fixtures.
- `memory-fragmented-largest-only` exits 0 with the gate message the plan
  quotes. Seven registry-touching scenarios pass with no `SIM PANIC` and no
  false double free: `memory-counted-camera-connect`, `memory-webui-fail-closed`,
  `memory-pressure-webui-shed`, `main-menu-gap-pages`,
  `display-settings-pages`, `bughunt/about-scroll-reachability`,
  `connected-gap-pages`.
- Four unit cases and one scenario are coverage of pre-existing model code and
  would pass on `origin/master`: `testPsramExhaustion`, `testFragmentation`,
  `calloc-overflow`, `prefer-exhausted`, and the fragmentation scenario. That is
  fine for a coverage PR; the before/after table should not be read as eight new
  model capabilities.

## Resolution

All seven findings are fixed in this PR. Head after the fix is the branch tip of
`test/sim-memory-100`; the evidence below is from that head, rebuilt from
scratch for both simulator binaries and the host targets.

| # | Finding | State | Where |
| ---: | :--- | :--- | :--- |
| 1 | plan 370 claimed a link-time guarantee the shim does not provide | fixed | the claim is corrected and `tools/check_sim_heap_escape.py` now enforces it as the `simulator heap escape` pre-review gate |
| 2 | the double-free window collapses on address reuse | fixed | releases are stamped with a sequence number; `freed-window-duplicate` is the regression |
| 3 | "unit + seed" overstates the plain `new` coverage | fixed | the row says unit only and the gap is a Known limits entry |
| 4 | the camera fixture is described as pool exhaustion | fixed | AGENTS.md and docs/sim-coverage.md say "a failed per-camera task allocation during a connect" |
| 5 | `heap_caps_malloc_prefer` missing from the inventory | fixed | inventory row added with its one firmware caller |
| 6 | the heap-trace site is unreachable and unlisted | fixed | Known limits entry added |
| 7 | `psram_free` has no ceiling | fixed | seeds above the 8 MiB module fail at scenario load |

### 1. The heap escape is now a gate, not a claim

`tools/check_sim_heap_escape.py` strips comments, string literals and character
literals, then fails on a bare `malloc`, `calloc`, `realloc`, `free`, `strdup`,
`strndup`, `pvPortMalloc` or `vPortFree` in `src/`, `lib/` or `include/`. It is
registered in `tools/pre_review.py` next to the simulator coverage gate, and
`tests/test_check_sim_heap_escape.py` (6 cases) is in the scanner unit-test list
that gate runs. plans/370 loses the false sentence and states the real mechanism:
the shim includes the C library headers, the host libc resolves those symbols,
and the gate is what catches a new call.

```text
$ python3 tools/check_sim_heap_escape.py
simulator heap escape check passed for 248 firmware source(s)

# probe: static void *g_probe(void) { return malloc(64); } added to src/FurbleMQTT.cpp
$ python3 tools/check_sim_heap_escape.py
bare heap calls escape the simulator heap model:
  src/FurbleMQTT.cpp:528: malloc()
Use heap_caps_malloc/heap_caps_calloc/heap_caps_free, the replaced operator new family, or add the API to sim/shim/ with a model charge.
$ echo $?
1
```

### 2. The release window keeps the newest release of a reused address

`freed` is now an address-to-sequence map and `freedOrder` holds stamped
`(address, release)` pairs. An eviction only erases `freed` when the stamp it
pops is still the address's own, so the stale entry a reuse leaves behind cannot
take a live release with it. The bound is stated as releases: the last 4,096
releases, not the last 4,096 addresses.

`freed-window-duplicate` reproduces your replay with the real capacity: `free`,
reuse the same address, `free` again, push the second release off the front with
4,095 other releases from a different size class, then free the address a third
time. With the stamp comparison reverted the case fails exactly as your scaled
replay predicted, in the plain build and under ASan:

```text
SIM PANIC: heap block freed twice in task heap-caps-test aborts the device
  FAIL: freed-window-duplicate did not exit normally
FAIL: 1 heap_caps shim check(s)

==89079==ERROR: AddressSanitizer: attempting double-free on 0x606000000380 in thread T0:
SUMMARY: AddressSanitizer: double-free heap_count.cpp:223 in (anonymous namespace)::releaseAllocation(void*)
```

The reuse in that case is modelled with `heapSimulateReleaseAfterReuse()`, a
documented seam in `sim/heap_count.h`, because the first version made the second
release a real allocation and ASan's quarantine never returns the address. See
the sanitizer section below.

With the fix in place the suite passes and the abort is the modelled one:

```text
$ ./sim_heap_caps_shim_test
SIM PANIC: heap block freed twice in task heap-caps-test aborts the device
SIM PANIC: heap block freed twice in task heap-caps-test aborts the device
sim-heap-caps-shim: PASS
```

### 3. Plain `new` is unit-only, and the plan says so

The coverage row now reads `unit` with `malloc-injected`, and a Known limits
entry records that the `alloc_fail` seed reaches `sim/heap_count.cpp` while every
fixture that uses the seed fails an admission probe instead
(`sim/SimMQTTClient.cpp` is the only seed consumer). A fixture that fails a real
firmware `new` through `alloc_fail` is listed as open work rather than claimed.

### 4. The camera fixture wording

AGENTS.md and docs/sim-coverage.md now say "a failed per-camera task allocation
during a connect". The fixture is unchanged: it seeds a healthy 60,000/50,000
pool and fails `xTaskCreate` through `task_create_fail FauxNY-40`, which the
fixture comment already described that way.

### 5. `heap_caps_malloc_prefer` in the inventory

Row added: 1 site (`src/FurbleUI.cpp:123`), same shim, each set's pool tested
before the charge, `prefer-exhausted` when no set fits.

### 6. The heap-trace site

Known limits now records that the fifth `heap_caps_malloc` site allocates the
`heap trace` buffer behind `CONFIG_HEAP_TRACING_STANDALONE`, is compiled out of
the simulator, and that the debug sdkconfigs turn standalone tracing on for a
build where `heap_trace_init_standalone()` and `heap_trace_start()` also allocate
with no `sim/shim/esp_heap_trace.h`. It has no failure injection in either
configuration.

### 7. `psram_free` ceiling

The seed is capped at the modelled 8 MiB module (`hwSetPsramMode`), and the docs
say so. All three rejections exit 2 at scenario load:

```text
$ furble-sim --script big-psram.txt      # seed psram_free 209715200
Invalid psram_free: 209715200 exceeds the modelled 8388608-byte PSRAM module
$ furble-sim --script no-mode.txt        # psram_free without psram_mode
psram_free and psram_largest require psram_mode quad or psram_mode octal, not a profile
$ furble-sim --script bad-largest.txt    # psram_largest above psram_free
Invalid psram_largest: 8192 exceeds psram_free 4096
```

### Re-run after the fix

Both simulator binaries and the host targets were rebuilt from the fixed
sources, then:

```text
$ sh sim/scripts/run-panic-fixtures.sh
PASS: panic-webui-pre-gate-alloc        PASS: panic-camera-connect-alloc
PASS: panic-icon-no-psram               PASS: panic-psram-exhausted-task-alloc
PASS: panic-psram-task-no-psram         PASS: panic-mqtt-task-alloc-fail
PASS: panic-mqtt-reconnect-alloc
Allocation abort gate passed.

$ sh sim/scripts/run-memory.sh
PASS memory-boot-combination
PASS memory-mqtt-low-heap-retry-limit
PASS memory-mqtt-unreachable
PASS memory-pressure-mqtt-shed
All 4 MQTT memory scenarios passed.

sim_heap_caps_test: PASS        sim_heap_caps_shim_test: PASS
sim_webui_memory_model_test: PASS   memory_pressure_test: passed

$ sim/build/furble-sim --script sim/scenarios/e2e/memory-fragmented-largest-only.txt
state webui.gate_message = WebUI supervisor not created: internal free 60000, largest 3000 (need 5120/4608)
exit 0, 16 assertions
```

`memory-counted-camera-connect`, `memory-webui-fail-closed` and
`main-menu-gap-pages` still exit 0 with no assertion failure and no panic.

## Follow-up: the sanitized `sim-heap-caps-shim` failure

State: fixed. CI's "ASan and UBSan host tests" failed on `sim-heap-caps-shim`
while the plain target passed. Reproduced with the sanitizer runner, not the
plain binary:

```text
$ sh tools/run_sanitized_host_tests.sh -R '^sim-heap-caps-shim$'
    Start 31: sim-heap-caps-shim
1/1 Test #31: sim-heap-caps-shim ...............***Failed    0.24 sec
  FAIL: freed-window-duplicate exited 95, expected the panic status 3
FAIL: sanitized host tests failed
```

There is no ASan or UBSan diagnostic in that report. The failure is my own
`_exit(95)` guard: the case reallocated the block it had just released to produce
the duplicate order entry, and AddressSanitizer quarantines a released block, so
the allocator never returns the same address and the guard fires. The model
bookkeeping is clean under the sanitizer; the *test* depended on allocator
behaviour, which is a defect in the test.

The case now models the reuse with `heapSimulateReleaseAfterReuse()` in
`sim/heap_count.h` instead of a second allocation. The seam records the second
release and leaves the first one's order entry stale, which is the state the
window must survive; it frees nothing and the simulator never calls it. No
sanitizer option was weakened, no case was skipped, and the check still fails when
the stamp comparison is reverted:

```text
# stamp comparison reverted, sanitized build
1/1 Test #31: sim-heap-caps-shim ...............***Failed    0.51 sec
==89079==ERROR: AddressSanitizer: attempting double-free on 0x606000000380 in thread T0:
SUMMARY: AddressSanitizer: double-free heap_count.cpp:223 in (anonymous namespace)::releaseAllocation(void*)
  FAIL: freed-window-duplicate did not exit normally
```

With the fix, the three heap-related host targets pass under the sanitizer with
no diagnostic:

```text
$ sh tools/run_sanitized_host_tests.sh -R '^(sim-heap-caps|sim-heap-caps-shim|sim-webui-memory-model)$'
1/3 Test #12: sim-webui-memory-model ...........   Passed    0.17 sec
2/3 Test #13: sim-heap-caps ....................   Passed    0.17 sec
3/3 Test #31: sim-heap-caps-shim ...............   Passed    0.25 sec
100% tests passed out of 3
sanitized host tests: PASS
```
