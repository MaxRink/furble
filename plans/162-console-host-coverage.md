# 162 - developer console host coverage and a build inventory gate

CLAUDE.md names the USB console as the automation surface for this project.
A measured coverage audit on 792815cd found the opposite in practice: union
firmware line coverage was 65.95 percent, and ten firmware files totalling
4831 of 24794 SLOC, 19.5 percent of the codebase, were compiled by neither the
host suite nor the simulator. `src/FurbleConsole.cpp` was the largest of them
at 1851 lines, so every command a host script types was unmeasured, and nothing
in CI stopped the next firmware source from landing outside both build lists.

This plan closes both halves of that gap.

## Console coverage

`src/FurbleConsole.cpp` is compiled into the host suite as
`console_commands_test`, against a faithful double of the small ESP-IDF console
API: a registration table, the built in help command, and a tokenizing
`esp_console_run()`. The console transport is a USB-Serial/JTAG receive queue a
test types into, which is the StickS3 path the shipping debug image uses.
Production `Console::init()` therefore registers the production command table
and starts the production console task, and every command handler runs
unmodified.

Real firmware is linked wherever a host model already exists: the Control state
machine and the Camera stack against MockNimBLE, Settings and Preferences
against the in-memory NVS store, Power, and the provisioning decoder. The
hardware-bound subsystems are doubled at their boundary (GPS receiver, BtDebug
HCI dumping, IR, feedback, SD, the PMIC platform), so a command's parsing and
dispatch still execute and the test asserts the call landed with the right
arguments.

The suite asserts the command surface as a contract: the registered top-level
set exactly, and each command's accepted subcommand set from both sides, so an
unknown subcommand is rejected and every documented one is not. On top of that
it drives the state reporting commands, a settings roundtrip through the real
store, the whole GPS command tree including the NMEA checksum the console
computes, the power and performance counters, the bluetooth debug tree, the
flash preparation state machine, log level control, a real provisioning blob,
the error paths with their return codes, the line editor typed at the real
console task, and the debug dumps against a live virtual camera connected
through the real Control state machine.

The mock NimBLE device gained the four accessors the `debug ble` dump reports
from: `isInitialized`, `getAddress`, `getPower` and `getClientByPeerAddress`.
There are no changes to any `src/` file.

Measured with the audit's method (clang `-fprofile-instr-generate
-fcoverage-mapping`, `llvm-profdata` and `llvm-cov`), `src/FurbleConsole.cpp`
line coverage moved from 0.00 percent to just over 90 percent across 609
assertions. Repeat runs measure 90.07 to 90.42 percent, the small spread being
the timing-dependent power log tick. The remaining gap is the compile-time `command()` table builder, the transient
Control states, and the camera type names that need a target of each vendor.

## Build inventory gate

`tests/test_build_inventory.py` asserts that every `src/*.cpp` and every
`lib/furble`, `lib/furble/protocol`, `lib/preferences` and `lib/blowfish`
source appears in at least one of three places: the host CMake source lists,
the simulator build list, or an explicit exemption. CMake path variables are
resolved generically from the `set()` statements in the file, so a new source
list is picked up without touching the checker.

The simulator carries its firmware source list twice, in `sim/CMakeLists.txt`
and `sim/build.sh`, with a keep-in-sync note. The gate parses both and asserts
they agree, so the note is now enforced.

`tests/build_inventory_exemptions.json` carries one line of reason per
exemption. It is seeded only with what is genuinely hardware-bound after this
work, plus the four files an in-flight simulator effort will cover, marked
`planned: plans/16x` so they stay visible rather than being silently written
off. A stale exemption pointing at a file that no longer exists fails, and so
does an exemption for a file that is already built, which keeps the list honest
as coverage lands.

## Implementation state

Landed as one PR. Host suite 86 tests green (85 before this work), python suite
91 tests green, clang-format 21 clean.

Both gates are mutation verified. Adding an unlisted `src/Foo.cpp` fails the
inventory test, as does removing `src/FurbleConsole.cpp` from the host source
list. Removing the `time` command registration from the production table fails
the console suite on the exact command set assertion and on every `time`
assertion, with a non-zero exit.

## Deviations

None in `src/`. The console suite required no host seam there, so the whole
change is test-side.

One thing did not survive first contact with Linux. The detached console task
loops forever, exactly as it does on device, so returning from `main()` let the
runtime destroy the globals it was still blocked on. That segfaulted under
glibc and passed by luck on macOS. The suite now parks the task at a known
point inside the transport read before it returns, where it sleeps and touches
nothing else for the rest of the process.

## Follow-ups

- `src/FurblePlatform.cpp`, `src/FurbleFeedback.cpp`, `src/FurbleIR.cpp` and
  `src/FurbleCompanion.cpp` are exempted as planned simulator work. Each should
  drop out of the exemption file when its build lands.
- `cameraTypeName()` needs one target of each vendor to cover, which means
  linking the remaining vendor camera classes into the console target.
- Plan number 162 was taken because 161 is claimed by the in-flight
  feat/sim-real-control PR. If that PR lands first the numbers stand; if it is
  renumbered, this one does not move.
