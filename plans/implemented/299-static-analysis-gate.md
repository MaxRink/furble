# 299 - static-analysis gate for common C/C++ defects

## Motivation

The pre-review gate covers diff hygiene, settings contracts, and a narrow C++
safety scan. It does not run a general static analyzer, so the classes the
compiler cannot see - uninitialized aggregates, format-string width mismatches,
out-of-bounds indexing, suspicious string comparisons, assignment inside a
condition, and narrowing loop counters - were found by review or by accident.

This change adds the analyzers the tree can run without a target toolchain and
makes them a required pull-request gate.

## Implementation state

- `.clang-tidy` carries the curated check set: `bugprone-*`, `clang-analyzer-*`,
  `cert-*`, `performance-*`, and `portability-*`, with the noisy families
  disabled and named in the file.
- `tools/check_static_analysis.py` runs cppcheck and clang-tidy on changed
  production sources under `src/`, `lib/furble/`, `include/`, and `sim/`.
  It accepts the host and simulator compile databases together and analyzes a
  shared source once per distinct database. A changed header is parsed by
  cppcheck directly and maps to up to four production sources that include it
  for clang-tidy. A changed file with no compile command is reported as skipped,
  not silently analyzed.
- Analyzer invocation failures fail closed: a nonzero cppcheck exit, a nonzero
  clang-tidy exit with no parsed finding, and `fatal error:` diagnostics all
  fail the gate instead of printing PASS.
- Absolute diagnostic paths are normalized against the repository root before
  the production filter, so a CI runner path does not silently drop a finding.
  C helpers such as `sim/env_mutation_guard.c` run through cppcheck in C mode.
- Duplicate findings from the host and simulator databases are printed once.
- The first full-tree pass also fixed the remaining findings: the gesture
  handler's fully covered switch, the simulator's inherited checkpoint
  descriptor, and the fuzz harness's intentional fixed default seed.
- `.github/workflows/static-analysis.yml` configures both databases and runs
  the gate on pull requests touching C/C++ paths, plus manual dispatch.
- `tests/test_check_static_analysis.py` covers the cppcheck and clang-tidy
  finding parsers and the production/test path split.
- `CONTRIBUTING.md` documents the local command; `AGENTS.md` carries the
  one-line gate pointer.

## Findings fixed by this change

- Format width mismatches: `%lu` with `uint32_t` in the console, device ID, and
  companion pairing prompt.
- `FurbleConsole` stripped a two-byte line ending after only checking for a
  non-negative `snprintf` length; the length is now at least four.
- `GpsCasic::splitNmea` kept its index loop: the leading `$` offset is
  computed once, and the parser avoids a per-sentence substring copy.
- `NikonSmart::hash` used a 16-bit loop counter against a `size_t` bound and
  could read past an odd-length input.
- `CanonTime` used an 8-bit month loop against an unsigned bound.
- `FurbleGPX` assigned inside an `if` condition; header write, offset capture,
  and closer write are now separate statements.
- `UI::Intervalometer` label pointers had no initializers.
- `sim/driver.cpp`'s deliberate crash fixture used a null dereference; it now
  raises `SIGSEGV` explicitly.
- `Device`, `FurbleUI`, and `NikonSmart` replaced old-style C casts.
- Several `strcmp`/`strcasecmp` calls now compare their result explicitly.
- Advertisement `getManufacturerData()` and device-ID/name strings are bound by
  reference where they were copied only to read.

## Deviations and limits

- cppcheck reports aggregate member initialization at the header even when the
  value is set by a designated initializer at the use site; that family,
  intentional virtual overrides, test-shim ODR reports, and the ESP-IDF macro
  parser bailout are suppressed in the checker with reasons.
- clang-tidy runs only where a host or simulator compile command exists.
  Firmware-only `#if` branches are covered by cppcheck and by the target
  builds, not by clang-tidy. A firmware compile database is future work.
- The gate analyzes changed files, not the whole tree, on pull requests. The
  full-tree mode is `--all` and is intended for scheduled or manual runs.
- No new runtime behavior is claimed; the fixes are correctness and clarity
  changes covered by the existing host suite.

## Validation

- Host `ctest`: 140/140.
- Python: 312 existing tests plus the new parser tests.
- `python3 tools/check_static_analysis.py --base origin/master` with the host
  and simulator databases: PASS.
- `python3 tools/pre_review.py --base origin/master`: PASS, 7/7 gates.
- Simulator builds and the changed scenarios remain green.
