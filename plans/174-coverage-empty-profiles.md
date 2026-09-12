# 174 - two host suites that measured nothing under coverage

173 is the last number on master, landed by PR #286. This plan therefore
takes 174.

## Motivation

Issue #277, found by the PR #276 review. `control_disconnect_test` and
`control_reclaim_uaf_test` both link `tests/host/control/control_shim.cpp`,
which detached its task threads, so both suites had to end in `std::_Exit()` to
avoid tearing a live control task down against destroyed singletons.

`std::_Exit()` skips `atexit`, and the profile runtime registers
`__llvm_profile_write_file` with `atexit`. So under the coverage flags neither
suite ever wrote its counters. With the CI profile pattern
(`host-%p-%m.profraw`) the file is not created at all; with a plain `%p`
pattern it is created and left at 0 bytes. `llvm-profdata merge` accepts an
empty raw profile with exit 0 either way.

Two suites therefore contributed zero coverage on every run, the report looked
healthy, and nothing detected it. Same silently-lost-measurement family as #275
and the same shape as the simulator scenario that never finished (plan 169).

The #276 review found two more defects on the way:

- `console_shim.cpp` `xTaskCreate()` accepted a task created after
  `furbleHostStopTasks()` had copied the list it joins, so that task is never
  joined and outlives `main()` exactly as a detached task did.
  `control_e2e/freertos_shim.cpp:55` already rejects it.
- `crashed_host_tests()` latched its summary-block header on the first line
  merely containing it. ctest runs with `--output-on-failure`, so a failing
  test's own stdout is echoed above the real block: a test that printed the
  header made the parser read its output as failure rows and fabricate crash
  reports for tests that never crashed.

## Change

No firmware source is touched. Everything here is host harness and tooling.

### The control shim adopts the plan 123 task lifetime contract

`tests/host/control/freertos/FreeRTOS.h` declares `furbleHostStopTasks()` and
the `FurbleHostTaskScope` guard, matching the console and control end-to-end
shims name for name. In `control_shim.cpp`:

- Tasks stay joinable, and the task function runs inside a `try` that catches
  the shim's internal `StopTask`.
- `xQueueReceive()` and `vTaskDelay()`, the two suspension points the control
  and per-target tasks park in, throw `StopTask` once shutdown has begun, and
  the queue wait predicate reads the stop flag so a task cannot miss the
  notify. Both are suspension points in the production code holding no
  production lock, so the stack unwinds through ordinary RAII.
- Only threads the shim created ever throw, through a `thread_local` flag. The
  main thread calls the same primitives, `Control::disconnect()` sleeps in
  `vTaskDelay()` there, and it owns the shutdown so it must never unwind.
- `xTaskCreate()` refuses a task once shutdown has begun.
- `furbleHostStopTasks()` takes the task list, wakes every registered queue,
  joins each thread and frees it. Taking rather than copying makes a second
  call a no-op, and freeing keeps the AddressSanitizer suite clean now that it
  reaches LeakSanitizer at all.

Both suites now start the control task through `xTaskCreate()` instead of a
raw detached `std::thread`, hold a `FurbleHostTaskScope` in `main()`, and
return their status instead of calling `std::_Exit()`.

### The console shim refuses a task created after shutdown

Same three lines as the control shim, under the same mutex the shutdown copies
the list under, so a concurrent shutdown either sees the task and joins it or
rejects it. `console_commands_test` gains a final test that calls
`furbleHostStopTasks()` and asserts the next `xTaskCreate()` is refused and its
task body never runs.

### The coverage run fails on a lost profile, naming the test

`tests/host/CMakeLists.txt` names every test's raw profile after the test, from
a `FURBLE_PROFILE_DIR` cache entry that `tools/coverage.py` passes at configure
time. ctest gives every test the same environment, so without this a profile
carries a process id and nothing else, and a test that wrote nothing cannot be
told from a test that never ran.

`lost_test_profiles()` then compares the profiles on disk against the test list
ctest reports, and `measure_host()` fails the run naming every test whose
profile is missing or empty. A test that forks writes one profile per process
and a child that `_exit()`s writes nothing, so a test counts as lost only when
every profile it wrote is empty.

`measure_host()` configures even under `--skip-build`, because the per-test
profile naming lives in the CMake cache and a configure builds nothing.

### The ctest summary header is anchored

`crashed_host_tests()` matches the header as a whole line and takes the last
one. ctest prints the real block after every test has run, so nothing follows
it but that block.

## Verification

- `control_disconnect_test` and `control_reclaim_uaf_test` write 240 KB and
  235 KB raw profiles where they wrote nothing before, and all 94 host tests
  write a non-empty profile.
- The detector is mutation proven: `tools/coverage.py` and the CMake change on
  top of the unfixed shim fail the run naming `control-disconnect` and
  `control-reclaim-uaf`, with no other change.
- Host suite green with and without `FURBLE_COVERAGE`, 94 of 94.
- Full `--check` run across the host stack and all three simulator panels is at
  or above every floor.
- `python -m unittest discover -s tests -p 'test_*.py'`, 153 tests, including
  the new empty profile and anchored header contracts.

## Measurement

Host line coverage of `src/FurbleControl.cpp` rises from 566 of 755 lines to
568 of 755. Unioning each host measurement with one fixed set of simulator
profiles, so the only variable is the host stack:

| | union lines | union | `src/FurbleControl.cpp` in the union |
|---|---:|---:|---:|
| before | 14,898 of 20,974 | 71.03% | 590 of 758 |
| after | 14,911 of 20,974 | 71.09% | 592 of 758 |

Small, because the control end-to-end harness already walks most of what these
two suites reach. That is exactly why the loss went unnoticed for so long, and
why the detector matters more than the two points it recovered.

## The floor is left alone

The union rose, and `--ratchet` on this measurement was tried and reverted. It
raises nothing this branch earned: `src/FurbleControl.cpp` sits at 78.10%, one
point under which is below the committed 77.13%, so the floor for the file this
branch improved does not move at all. What it does move is every unrelated
value, by 0.4 to 7.7 points, off a single local run: the simulator floors were
deliberately lowered once and have not been reratcheted since, so a ratchet here
banks headroom this branch did not produce, from one machine.

Two of its effects are actively harmful. It raises `lib/furble/Camera.cpp` to
74.18%, which is 517 of 697 lines, while the second comment on issue #277
reports CI measuring that file anywhere between 503 and 524 on identical input
against a floor of 519, needing a re-run on each of the last two heads. And it
deletes the `lowered` block, which plan 163 keeps precisely so the reason for
the one deliberate lowering travels with the document.

Reratcheting the floor off a CI run, and the Camera.cpp noise band under it, are
both their own change.

## Implementation state

Implemented on `fix/coverage-empty-profiles`. No firmware source changed and no
floor changed.
