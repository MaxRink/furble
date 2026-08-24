# 126: ESP-IDF optimization and reproducibility audit

Status: research complete. This document is a plan, not an authorization to
change release defaults. It was prepared against fork/master at
`06e9333fda349846d76d8d34d2f4014259fd4e11` on 2026-08-24. No production code,
sdkconfig, or dependency was changed during the audit.

The companion umbrella plan, [125-esp-optimization-program.md](125-esp-optimization-program.md),
already defines the measurement contract and broad work streams. This document
adds the current-tree findings, the reproducible-build design, the SDK release
ladder, and concrete acceptance gates. It should be read as a refinement of
plan 125, not as a second list of unmeasured tuning guesses.

## Current inventory

The release inputs currently have five board environments and five debug
variants in `platformio.ini`, plus the non-release `esp32-s3-headless`
validation environment. The file pins `espressif32@6.12.0` and
`framework-espidf@3.50402.0`, which is the ESP-IDF 5.4.2 package family. The
exact installed package revision must be recorded before an upgrade because a
PlatformIO platform can change its default framework independently of the
explicit `platform_packages` override.

The direct dependencies are M5PM1 1.0.6, M5GFX 0.2.19, M5Unified 0.2.13,
LVGL 9.4.0, and a TinyGPSPlus fork at commit `92ac8c2`. There is no committed
PlatformIO package lock, ESP-IDF component lock, Python hash lock, or container
digest. `src/idf_component.yml` pins LVGL 9.4.0 and NimBLE C++ 2.5.0, while
`components/icons/idf_component.yml` uses a lower-bound LVGL range. The two
manifests therefore do not express one identical dependency lock, and a clean
build can resolve a different component unless the package cache happens to be
warm.

All six committed sdkconfigs enable build-time date metadata and leave
`CONFIG_APP_REPRODUCIBLE_BUILD` disabled. All six also enable comprehensive
heap poisoning. The S3 release config enables PSRAM, Wi-Fi, SPI Ethernet, and
verbose-capable IDF logging even though the current master application does not
yet exercise the Wi-Fi/MQTT/Ethernet path. The release profiles use `-Os` and
the debug profiles add verbose logging and the console.

The BLE profiles currently allow nine connections, fifteen bonds, sixteen
CCCDs, a 16 KiB NimBLE host stack, 256 byte preferred ATT MTU, central and
peripheral roles, observer and broadcaster roles, and a wide set of standard
NimBLE services. These values may be correct for the roadmap, but they are not
yet justified by a measured maximum workload. Right-sizing must wait for the
companion and multiconnect features to settle.

Power management and BLE modem sleep are enabled. The repository's local
constraints are important: GPS UART clocks must remain XTAL-stable under DFS,
the display backlight owns an APB lock while active, DMA display buffers must
be internal, M5PM1 needs a retry after idle wake, and deep sleep removes the
digital peripherals. These are safety constraints, not optimization targets.

`src/CMakeLists.txt` gates the display and icon component for headless builds,
but most peripheral sources remain in the common source list. SD/FATFS, IR,
audio, GPS, companion, OTA, and provisioning are therefore candidates for
measured feature gating only after their runtime contracts are complete. The
build also applies `patches/ble_gap.patch` to a file inside the ESP-IDF package
at build time. That patch is a reproducibility input and must be verified as an
exact, one-time transformation.

The CI PlatformIO package cache key hashes `platformio.ini`; it does not hash
the sdkconfig files, patch files, component manifests, Python lock data, or
the compiler and IDF package identities. The restore key is broad. This is
useful for speed, but it is not an adequate release reproducibility gate.

## Evidence and interpretation

The following are normative primary sources. URLs were checked on 2026-08-24.

* [ESP-IDF reproducible builds](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/reproducible-builds.html)
  says `CONFIG_APP_REPRODUCIBLE_BUILD` makes ELF and BIN output independent of
  source/build paths and build time. It also explicitly says the result still
  depends on the ESP-IDF version, build tools, and cross-compiler.
* [ESP-IDF binary-size guidance](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/performance/size.html)
  recommends `-Os`, section garbage collection, and measured LTO. The current
  [Kconfig compiler reference](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/kconfig-reference.html)
  warns that LTO can increase task stack usage and can increase rather than
  reduce binary size.
* [Power management](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/power_management.html)
  documents reference-counted CPU, APB, and light-sleep locks. Every proposed
  lock change therefore needs ownership and leak tests.
* [Sleep modes](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/sleep_modes.html)
  states that Wi-Fi and Bluetooth are powered down in deep and light sleep and
  must be stopped before entering those modes unless the modem-sleep/light-sleep
  connection path is used.
* [Heap allocation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/mem_alloc.html)
  requires capability-aware allocation for DMA and internal memory. [External
  RAM guidance](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/external-ram.html)
  warns that PSRAM is inaccessible while flash cache is disabled, including
  during flash writes, NVS, OTA, and deep-sleep transitions.
* [Heap debugging](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/heap_debug.html)
  documents heap task tracking and explicitly warns that it has substantial
  RAM and allocator-performance overhead. It belongs in diagnostic profiles,
  not silently in release images.
* [OTA](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/ota.html)
  documents two-sector OTA data, rollback confirmation, and anti-rollback. The
  secure-version eFuse has a finite bit budget, so anti-rollback is a release
  policy decision, not a test-only toggle.
* [Security overview](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/security/security.html)
  and [Secure Boot v2](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/security/secure-boot-v2.html)
  recommend Secure Boot with Flash Encryption for production and document that
  security eFuses affect JTAG and ROM download recovery. Never use the only
  recoverable board for an eFuse experiment.
* [SPI master](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/peripherals/spi_master.html)
  documents DMA channel ownership and transaction limits. This applies to the
  W5500 path as well as display traffic.
* [ESP-IDF version policy](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/versions.html)
  recommends an in-service release and says each minor line is supported for
  thirty months. The [Espressif release index](https://github.com/espressif/esp-idf/releases)
  was checked on 2026-08-24 and lists 5.5.5 and 6.0.2 as stable releases; its
  indexed 6.1 entry is a beta. Re-check the release index when this plan is
  implemented because release status is time-sensitive and the roadmap is not
  a release manifest.
* The [PlatformIO Espressif 32 registry](https://registry.platformio.org/platforms/platformio/espressif32)
  was checked on 2026-08-24 and lists platform 7.0.1 as current. Its
  [ESP-IDF framework package registry](https://registry.platformio.org/tools/platformio/framework-espidf)
  lists package `4.60001.0` (ESP-IDF 6.0.1) as current. PlatformIO platform
  and framework package versions are separate inputs, so an Espressif 6.0.2
  source release must not be treated as available through a PlatformIO package
  until the exact package or override has been resolved and built.

The following are useful field reports or secondary leads. They are not proof
that furble has the same failure:

* [ESP-IDF #18743](https://github.com/espressif/esp-idf/issues/18743), opened
  in 2026, reports an intermittent Wi-Fi scheduler wedge on an ESP32-S3
  unicore device under 5.5.4 and newer development code. Treat unicore Wi-Fi
  as a hardware soak gate after every SDK upgrade.
* [ESP-IDF #17871](https://github.com/espressif/esp-idf/issues/17871) reports
  an ESP32-S3 Wi-Fi plus NimBLE coexistence failure with an Android hotspot.
  The report is environment-specific, so add an Android-hotspot matrix case
  rather than changing coexistence defaults from the report alone.
* [ESP-IDF #13073](https://github.com/espressif/esp-idf/issues/13073) reports
  unexpectedly high S3 current in the NimBLE power-save example. It supports
  measuring radio residency and crystal/power-domain behavior, not blindly
  replacing the current modem-sleep settings.
* [ESP-IDF #14504](https://github.com/espressif/esp-idf/issues/14504) reports
  an S3 SPI/W5500 heap assertion. [Arduino #9648](https://github.com/espressif/arduino-esp32/issues/9648)
  reports OTA crashes with W5500 and PSRAM. These are adjacent implementations,
  but justify a W5500 plus OTA plus PSRAM stress test before enabling the
  Waveshare node as a release target.
* [ESP-IDF #12650](https://github.com/espressif/arduino-esp32/issues/12650)
  reports an S3 W5500 interrupt stack canary failure. Keep the IRQ path and
  stack high-water mark in the Ethernet hardware gate.
* Espressif's [ULP LP-core article](https://developer.espressif.com/blog/2025/04/ulp-lp-core-get-started/)
  and [wake-stub article](https://developer.espressif.com/blog/2025/09/deep-sleep-wake-stub-get-started/)
  are design leads for future interval wake work. They do not justify moving
  BLE, camera, or PMIC sequencing into a wake stub.
* Espressif's [simple-boot article](https://developer.espressif.com/blog/2025/06/simple-boot-explained/)
  and [RAM optimization article](https://developer.espressif.com/blog/2025/11/esp32c2-ram-optimization/)
  are useful boot and memory measurement ideas. They are not furble-specific
  benchmarks.

## Findings and priority

### P0: release integrity blockers

1. There is no reproducible-build assertion. Enable the IDF reproducible-build
   option in a dedicated branch, then prove path, time, clean-build, cached-build,
   and second-checkout invariance before calling it a release property.
2. Dependency identity is incomplete. Add exact PlatformIO platform, framework,
   compiler, CMake, Ninja, Python, component, M5, LVGL, TinyGPSPlus, and patch
   identities to a generated report. Do not rely on a package cache directory.
3. Build-time dates are enabled in every committed sdkconfig. They must be
   disabled or shown to be normalized by the reproducible-build option. The
   version string must be a source identity, for example `dev+g<8 hex commit>`;
   release tags remain explicit and are not derived from wall time.
4. The build-time NimBLE patch is currently applied by a mutable script to a
   mutable package tree. Verify the preimage hash, apply once, verify the
   postimage hash, and fail closed on either mismatch. Include both hashes in
   the build report.
5. CI's broad cache restore can hide dependency drift. Release builds need an
   exact cache key or a cache miss, plus a scheduled clean build. Build-cache
   acceleration must never alter the artifact hash.

### P1: high-value, measurement-first work

1. Move comprehensive heap poisoning, heap task tracking, and expensive memory
   instrumentation to debug/diagnostic profiles after a baseline. Keep a
   separate diagnostic CI job. Do not trade away corruption detection until a
   clean and stress test demonstrates the release profile is safe.
2. Right-size NimBLE only after companion and multiconnect PRs are merged. Use
   a matrix for one, two, and the maximum supported camera/companion sessions.
   Keep ATT MTU, MSYS pools, ACL buffers, host stack, CCCDs, bonds, and scan
   duplicate cache tied to measured queue high-water marks.
3. Add a PM lock ledger and event-driven idle. Every lock has an owner, acquire
   edge, release edge, cancellation edge, and destruction edge. Simulate DFS
   while GPS UART, LEDC, SPI display, BLE, and future W5500 are active.
4. Treat Wi-Fi plus BLE on unicore S3 as a compatibility matrix, not a single
   smoke test. Exercise an Android hotspot, ordinary AP, broker reconnect,
   TLS, BLE scan, paired camera, and USB console. All Wi-Fi API calls stay out
   of timer callbacks and the BLE host task.
5. Add W5500 SPI DMA, IRQ, link flap, DHCP, OTA, PSRAM, and power-loss tests.
   The Waveshare PoE HAT is optional, so model absent HAT, HAT without
   negotiated PoE, negotiated PoE, USB-only, and power loss as separate states.
6. Add signed-OTA policy before network OTA is declared alpha. Test image
   rejection, rollback, interrupted download, wrong slot, resume, NVS
   durability, and version policy. Anti-rollback and irreversible eFuses need a
   written release decision and sacrificial hardware.

### P2: conditional optimizations

1. Measure `-Os`, `-O2`, and selective LTO. LTO is not a default recommendation:
   it can change stack shape and expose undefined behavior. Accept only a
   reproducible size or latency win with stack, BLE, OTA, and 24-hour soak data.
2. Evaluate newlib nano, silent assertion strings, error-name lookup, unused
   Wi-Fi feature flags, unused NimBLE services, and log maximum level one at a
   time. Keep human-readable errors in debug and diagnostics.
3. For S3 only, benchmark PSRAM placement of LVGL buffers, immutable tables,
   network queues, and non-DMA camera data. Never move OTA/NVS/deep-sleep stacks,
   DMA buffers, or cache-off code to PSRAM without an explicit capability test.
4. Measure LVGL invalidation counts, dirty areas, icon decompression, SPI bytes,
   frame time, and wake-to-first-frame. Changed-value guards and event-driven
   refreshes are low-risk; buffer count, SPI frequency, and cache changes need
   display hardware.
5. Gate SD/FATFS, IR, audio, GPS, companion, and network sources only after
   feature contracts are stable. Each gate needs its own board-size delta and
   a no-feature build so the default feature set cannot accidentally disappear.

## Reproducible-build design

Implement this as independently mergeable slices.

### 126.1 Identity and clean-room report

Add a host-side tool that emits sorted JSON containing commit, dirty state,
PlatformIO version, platform package, framework package, cross-compiler version,
CMake/Ninja/Python versions, component manifests, library SHAs, all sdkconfig
SHA-256 values, source patch pre/post hashes, build flags, partition CSV hash,
and target board. Fail the release job if the tree is dirty or an identity is
unknown. The report itself must not contain absolute user paths.

### 126.2 ESP-IDF reproducible configuration

Enable `CONFIG_APP_REPRODUCIBLE_BUILD` in every committed sdkconfig, including
the headless validation config, after checking its generated sdkconfig diff.
Build every release, debug, and headless environment. Remove or normalize
compile-time dates.
Build each board twice from two absolute paths and with two `SOURCE_DATE_EPOCH`
values. Compare bootloader, partition table, OTA data inputs, ELF, BIN, map,
and checksums. If map files contain intentionally unstable tool diagnostics,
compare the normalized artifact set and document the exception.

Use the IDF-generated prefix maps. Do not add ad-hoc path substitutions that
silently diverge between host systems. Search the application and dependencies
for `__DATE__`, `__TIME__`, unordered input enumeration, random build IDs, and
host paths. The simulator must also use a fixed fake clock for screenshots and
scenario output.

### 126.3 Dependency and cache lock

Pin the platform, framework package, registry libraries, VCS commits, Python
packages, and component versions. A generated lock report is acceptable if the
PlatformIO format cannot express all identities. Cache keys must include OS,
Python, PlatformIO, platform/framework/compiler, board, sdkconfig hash, patch
hash, and manifest hash. Keep a no-cache scheduled job and a cache-poison test
that changes one input and proves the artifact changes or the build fails.

### 126.4 Version and signing split

Use `dev+g<short commit>` for development images and explicit semver/tag values
for releases. The version source must not include local dirty suffixes in a
release artifact. Build unsigned deterministic images for reproducibility
comparison, then sign in a separate controlled release step. Record the signing
key identity without recording private key material. If the signing process is
not byte deterministic, compare the unsigned payload and verify the signature
independently.

### 126.5 Acceptance

The slice is complete only when clean and cached builds from two checkouts
produce identical unsigned ELF/BIN/bootloader/partition artifacts for all five
release boards, or an explicitly reviewed toolchain exception is recorded.
Debug and headless artifacts must meet the same rule. All host/sim tests, all
five release builds, five debug builds, the headless build, size reporting, OTA
image validation, and the existing firmware checks must remain green.

## Concrete PR sequence

Each item below is one PR unless the review finds a safety reason to split it.
Every PR updates its plan state and includes exact base/head SHAs.

| Slice | Scope | Sim/host gate | Hardware gate |
| --- | --- | --- | --- |
| 126.1 | identity report and dependency inventory | report schema, no absolute paths, drift fixture | none |
| 126.2 | reproducible sdkconfig and version normalization | two-path/two-time artifact compare | boot and console smoke once board returns |
| 126.3 | exact cache key and clean-build CI | cache poison and clean/cached hash compare | none |
| 126.4 | NimBLE patch pre/postimage verification | patched/unpatched/mismatch fixtures | BLE scan/connect smoke |
| 126.5 | profile split for diagnostic heap/log settings | all profile builds, heap stress and fuzz | 24-hour release soak |
| 126.6 | compiler and selective LTO matrix | host plus deterministic size report | BLE, shutter, watchdog, OTA soak |
| 126.7 | NimBLE resource matrix | FauxNY, companion, multiconnect, malformed adverts | paired Fujifilm and reconnect cycles |
| 126.8 | PM lock/event ledger | lock leak, cancellation, DFS fake-clock tests | GPS UART, display, BLE idle, interval wake |
| 126.9 | scan/parser allocation and duplicate bounds | corpus, truncation, overflow, fuzz, allocation budget | discovery latency/current |
| 126.10 | Wi-Fi/BLE/MQTT lifecycle | fake AP/broker, TLS errors, bounded outbox | real AP, Android hotspot, broker |
| 126.11 | W5500 transport and PoE state model | link/DHCP/IRQ/OTA fault injection | Waveshare bare, USB-only, optional PoE HAT |
| 126.12 | OTA trust and rollback | signed/unsigned, slot, resume, power-loss model | alternating slots and recovery |
| 126.13 | NVS wear and transactional settings | full/corrupt/power-loss/1000 no-op writes | reset durability |
| 126.14 | PSRAM and DMA placement | capability assertions and cache-off fault model | display stress, OTA, deep sleep |
| 126.15 | LVGL/SPI measurement and safe refresh | invalidation and screenshot gates | tearing, flicker, frame/wake latency |
| 126.16 | SDK 5.5.5 upgrade | all sim/host/protocol/fuzz tests | full radio, GPS, OTA, sleep matrix |
| 126.17 | SDK 6.0.2 compatibility branch | migration build and API/deprecation audit | repeat 126.16 hardware matrix |

## SDK and dependency upgrade ladder

Do not combine an SDK upgrade with an optimization default. First preserve the
current 5.4.2-family build as a named baseline and capture all artifacts and
hardware measurements. Evaluate the current PlatformIO 7.0.1 platform in its
own compatibility PR before changing the framework package. Then test the
latest 5.5 bugfix line, currently 5.5.5, with the same PlatformIO board
definitions and the smallest required migration patch. The upgrade must
include the migration guide, release-note issues, NimBLE patch applicability,
M5 dependency compatibility, and regenerated sdkconfigs as reviewed inputs.

Only after 5.5.5 is green should the team evaluate the Espressif 6.0.2 source
release in a compatibility branch. The PlatformIO registry currently exposes
an ESP-IDF 6.0.1 package, not 6.0.2, so the implementation PR must record the
exact package provenance or use a separately verified framework override. Read
the [5.x to 6.0 migration guide](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/migration-guides/index.html)
before changing APIs. Keep any 6.1 beta or other pre-release out of release
qualification; re-check this statement against the release index at merge
time. Upgrade M5PM1, M5GFX, M5Unified, LVGL, NimBLE C++, and TinyGPSPlus one at
a time, with a lock report and map delta for each.

The rollback point for every upgrade is the previous lock report, sdkconfig
set, patch preimage, and artifact manifest. A migration is not complete when it
compiles: it must pass deterministic sim, all host/protocol/fuzz tests, all
ten board builds, size limits, OTA validation, and the applicable hardware
matrix.

## Metrics and acceptance budget

Record before and after values for each optimization. A candidate is rejected
when it has an unexplained failure, a missed notification, a reconnect increase,
or a regression greater than 5 percent in boot-to-console, shutter round trip,
wake-to-first-frame, minimum internal heap, or task stack high-water margin.
Power claims must identify measurement point, supply, USB state, display state,
radio state, peripherals, sample duration, and instrument. USB current is not a
battery-life claim.

The release dashboard should contain:

* exact firmware, bootloader, partition, text, rodata, IRAM, DRAM, RTC, PSRAM,
  and free-slot sizes;
* reset-to-app, first-frame, and console-ready times;
* free/minimum/largest internal and PSRAM blocks, allocation failures, and task
  stack high-water marks;
* active, display-idle, BLE-scan, BLE-connected, GPS, Wi-Fi, MQTT, Ethernet,
  interval-sleep, and wake current;
* discovery, connect, reconnect, shutter, notification, MQTT delivery, OTA,
  and rollback timings;
* LVGL frames, invalidations, dirty-area bytes, SPI bytes, and icon-cache hits;
* NVS writes, free entries, migration results, and power-loss outcomes; and
* 24-hour idle, 1000 scan cycles, 1000 connect cycles, and 100 interval wakes.

## Simulator requirements

No optimization PR should require hardware unless its claim is physically
irreducible. Add or extend deterministic seams for fake clock, PM lock and DFS,
BLE transport and negotiated MTU, advertisements and duplicate filtering,
network/AP/broker/TLS, W5500 link and IRQ, display/DMA capabilities, GPS bytes
and rail state, NVS commit/power loss, OTA slots/signatures, heap capabilities,
task stack accounting, and build identity. Use malformed inputs, fuzzing,
overflow sweeps, sanitizers, and long scenarios. Assert state transitions and
external effects, not private implementation order.

Every hardware-only gate must have an explicit reason in the PR body. With the
hardware currently disconnected, complete the identity, reproducibility,
profile, compiler, parser, PM fake, network fake, OTA model, and simulator
work before scheduling any board run.
