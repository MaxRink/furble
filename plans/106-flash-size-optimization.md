# 106 - flash size optimization

## Goal

Find and rank every firmware flash and RAM reduction opportunity, so the
default M5StickS3 build stays lean and the WiFi hub track (plan 33, PRs #66
MQTT, #53 provisioning, #31 headless) has room to land. PR #66 adds an MQTT
client plus Home Assistant discovery. That work pulls in networking and TLS
code on top of an app that already sits at 72.6% of its OTA slot. This plan
measures where the flash goes today and lists what to trim, in priority order.

Analysis base: fork master `633fd8f`, 2026-08-22, env `m5stick-s3` release,
built with `FURBLE_VERSION=dev FURBLE_TEST=0 pio run -e m5stick-s3`. All byte
numbers come from that build's `firmware.bin`, `firmware.elf` sections
(`xtensa-esp32s3-elf-size -A`), and the linker map
`.pio/build/m5stick-s3/furble.map` (per archive attribution). App layer object
sizes come from `xtensa-esp32s3-elf-size` on each `src/*.o`.

## Method

- Built the release S3 image and read `firmware.bin` size against the OTA app
  slot from `partitions_two_ota_large.csv` (ota_0 and ota_1 are 1700K each,
  1,740,800 bytes).
- Broke the image into flash sections and per archive contributions from the
  map. Library (`.a`) attribution is exact. Direct app object attribution in
  the raw map is unreliable, so the app layer total was measured by sizing each
  linked `src/*.o` instead.
- Read all five `sdkconfig.*`, `sdkconfig.debug`, `platformio.ini`, the two
  patch scripts, `src/idf_component.yml`, `components/icons`, and the LVGL and
  NimBLE Kconfig state in `sdkconfig.m5stick-s3`.
- Mapped which features are compile time gated versus always linked across
  `src/` and `lib/furble/`.

## Baseline numbers, S3 release

Image: `firmware.bin` = 1,264,496 bytes. OTA app slot = 1,740,800 bytes.
Flash used = 72.6%. Free headroom in the slot = 476,304 bytes (27.4%).
Static RAM (internal DRAM): `.data` 16,884 + `.bss` 20,632 = 37,516 bytes,
plus heap at runtime.

Flash image by section:

| Section | Bytes | Note |
|---|---:|---|
| .flash.text | 838,366 | code executed from flash cache |
| .flash.rodata | 294,536 | const data, strings, image arrays |
| .iram0.text | 113,195 | code copied to IRAM at boot |
| .dram0.data | 16,884 | initialised RAM data |
| .iram0.vectors | 1,027 | |
| .flash.appdesc | 256 | |

Top flash consumers by archive (exact map attribution) plus the app layer:

| Component | Bytes | % of image | Notes |
|---|---:|---:|---|
| LVGL (liblvgl__lvgl.a) | 193,893 | 15.3 | widgets, draw SW, fonts |
| App layer (all src/Furble*.cpp) | 132,377 | 10.5 | see per file table below |
| BT controller (libbtdm_app.a) | 87,892 | 6.9 | radio controller, fixed cost |
| newlib (libc.a) | 79,031 | 6.2 | full printf/scanf formatting |
| NimBLE host (libbt.a) | 72,360 | 5.7 | host stack |
| esp_hw_support | 58,785 | 4.6 | clocks, sleep, MSPI |
| mbedcrypto (libmbedcrypto.a) | 44,074 | 3.5 | NimBLE secure pairing (AES, ECDH) |
| NimBLE C++ (libh2zero) | 35,217 | 2.8 | |
| RF PHY blob (libphy.a) | 34,258 | 2.7 | fixed cost |
| icons (libicons.a) | 27,416 | 2.2 | 65 LVGL image arrays |
| SD/MMC (libsdmmc.a) | 26,731 | 2.1 | GPX logging only |
| HAL (libhal.a) | 26,381 | 2.1 | |
| FATFS (libfatfs.a) | 20,603 | 1.6 | GPX logging only |
| M5GFX | 18,380 | 1.5 | |
| RMT driver (libesp_driver_rmt.a) | 13,727 | 1.1 | IR trigger only |
| vendor cameras (libfurble.a) | 13,290 | 1.1 | all vendors |
| I2S driver (libesp_driver_i2s.a) | 12,899 | 1.0 | audio feedback only |
| M5Unified | 11,715 | 0.9 | |
| SDSPI driver | 6,731 | 0.5 | SD card only |

App layer (src/) by object, text plus data (flash bound):

| Object | Bytes |
|---|---:|
| FurbleUI.cpp.o | 52,177 |
| FurbleGPS.cpp.o | 19,898 |
| FurbleSD.cpp.o | 9,656 |
| FurbleControl.cpp.o | 9,355 |
| FurbleCompanion.cpp.o | 7,102 |
| FurbleCompanionService.cpp.o | 6,717 |
| FurbleSettings.cpp.o | 5,869 |
| FurblePlatform.cpp.o | 5,389 |
| FurbleFeedback.cpp.o | 3,174 |
| FurbleGPX.cpp.o | 2,350 |
| FurbleIR.cpp.o | 2,085 |
| FurbleBootScreen.cpp.o | 1,613 |
| (remaining src objects) | ~7,000 |

### Things that are already optimal, do not chase

- Compiler is `-Os` for app and bootloader (`CONFIG_COMPILER_OPTIMIZATION_SIZE`).
  C++ exceptions and RTTI are off. Function and data sections plus
  `--gc-sections` are on by default.
- WiFi, Ethernet, mbedTLS TLS layers and the mbedTLS certificate bundle are all
  enabled in `sdkconfig.m5stick-s3` but contribute 0 bytes to the image. The
  map confirms `libesp_wifi.a`, `libnet80211.a`, `libpp.a`,
  `libwpa_supplicant.a` and `libmbedtls.a` each add 0, and the
  `x509_crt_bundle` symbol is absent. `--gc-sections` drops them because
  nothing on master references them. Disabling these in sdkconfig is worth
  doing as hygiene and to keep the WiFi hub track honest, but it is not a flash
  win today. See item Q.
- All six Montserrat font sizes (10, 12, 14, 16, 22, 28) are referenced by the
  UI, so no font can be dropped.
- No LVGL demos or examples are compiled. No IMU or spirit level code exists
  (`cfg.internal_imu = false`). No MQTT or WiFi code exists on master yet.

## Feature gating reality

Almost nothing in furble is compile time optional. The only real feature macro
is `FURBLE_CONSOLE`, which gates the USB console and is set only in the
`*-debug` envs, so it already costs the release build nothing. Every functional
feature below is always compiled and always linked into the default S3 build.
Runtime enable and disable is done through NVS settings, not the linker. To
make a feature opt in, a `FURBLE_*` macro plus a CMake source exclusion has to
be created. `FurbleConsole.cpp` (`#if defined(FURBLE_CONSOLE)`) is the pattern
to copy.

Key structural blocker for per vendor trimming: `lib/furble/CameraList.cpp`
enumerates every vendor class in its `match()` and `load()` methods, which
forces all of them to link. Per vendor gating means breaking that single
registration point.

Peripheral drivers map cleanly onto features, so gating a feature also drops
its driver:

- SD card (GPX logging): libsdmmc 26,731 + libfatfs 20,603 + sdspi 6,731 +
  FurbleSD 9,656 + FurbleGPX 2,350 = about 66,000 bytes.
- IR trigger: libesp_driver_rmt 13,727 + FurbleIR 2,085 = about 15,800 bytes.
- Audio feedback: libesp_driver_i2s 12,899 + part of FurbleFeedback = about
  14,000 bytes.
- Companion GATT and OTA: FurbleCompanion 7,102 + FurbleCompanionService 6,717
  = about 13,800 bytes, plus it is the only user of the NimBLE GATT server and
  peripheral role, so gating it unlocks item P.
- GPS: FurbleGPS 19,898 + FurbleGPX 2,350 + TinyGPSPlus library = about
  22,000 bytes plus lib.

## Ranked optimizations

Savings are estimates until measured by the CI size report (see below). Group A
is config only, no source change, low behavior risk. Group B is LVGL and icon
trimming. Group C is the structural feature gating that actually creates
headroom for the WiFi hub track.

### Group A, config quick wins (one PR, all five sdkconfig files)

1. Cap the compiled log level. `CONFIG_LOG_MAXIMUM_LEVEL` is VERBOSE (5) while
   the runtime default is INFO (3). Every DEBUG and VERBOSE log format string
   in every component is compiled into `.flash.rodata` and never printed. Set
   `CONFIG_LOG_MAXIMUM_LEVEL_INFO=y` in the five release sdkconfigs and add
   `CONFIG_LOG_MAXIMUM_LEVEL_VERBOSE=y` to `sdkconfig.debug` so debug builds
   and the console keep full logging. Saving: est 8 to 20 KB. Risk: low. Effort:
   low. Behavior: none, runtime level is already INFO.

2. Silence assertions in release. `CONFIG_COMPILER_OPTIMIZATION_ASSERTION_LEVEL`
   is 2 (full, with strings). Set the ESP-IDF and HAL assertion level to SILENT
   so the checks stay but the `__FILE__` and message strings are dropped from
   rodata. Keep level 2 in `sdkconfig.debug`. Saving: est 15 to 40 KB. Risk:
   low to medium, a failing assert aborts without a message. Effort: low. Do
   SILENT first, not full disable.

3. Enable newlib nano formatting. `CONFIG_NEWLIB_NANO_FORMAT` is off; the ROM
   already has nano (`CONFIG_ESP_ROM_HAS_NEWLIB_NANO_FORMAT=y`). Nano replaces
   the full printf/scanf family and shrinks libc's 79 KB. Saving: est 20 to
   40 KB. Risk: medium, float and width formatting differ. Effort: low config
   plus verification. Behavior: verify GPS coordinate, battery percent, timer
   and version strings still render, in sim and on hardware, before merge.
   Own PR because of the formatting risk.

4. Drop the esp_err name table. Set `CONFIG_ESP_ERR_TO_NAME_LOOKUP=n`.
   `esp_err_to_name` then returns the hex code instead of a name. Saving: est 4
   to 8 KB. Risk: low. Effort: trivial. Behavior: error logs show hex.

### Group B, LVGL and icons

5. Disable unused LVGL widgets and the second theme. `ARCLABEL`, `BUTTONMATRIX`
   and `TEXTAREA` are enabled but never created in the code, and only
   `THEME_DEFAULT` is used while `THEME_SIMPLE` is also on. Disable them in all
   five sdkconfigs. Saving: est 5 to 12 KB. Risk: low to medium, the menu or
   roller may pull a widget internally, so build and sim-verify each toggle.
   Effort: medium.

6. Audit the 65 icon arrays for unused entries. libicons is 27,416 bytes. Any
   icon not referenced by the UI is dead weight. Cross check each
   `LV_IMG_DECLARE` symbol against usage, remove the unused, and confirm the
   image cache covers the ones drawn at 64x64. Saving: est 2 to 8 KB. Risk:
   low. Effort: medium.

### Group C, feature gating (each its own PR, needed for the WiFi hub track)

These add a `FURBLE_*` macro plus a CMake exclusion so the feature and its
driver drop out of a lean or WiFi hub build. The default S3 build keeps them
on. Ordered by saving.

7. `FURBLE_SD` for SD card and GPX logging. Largest single gateable chunk, about
   66 KB (sdmmc, fatfs, sdspi, FurbleSD, FurbleGPX). Risk: medium, the UI
   storage cell and GPS logging path must degrade cleanly. Effort: medium to
   high. Coordinate with the plan 24 and PR #41 SD work.

8. `FURBLE_GPS` for GPS and GPX. About 22 KB plus TinyGPSPlus. GPS is a
   headline S3 feature so it stays on by default; the value is letting a WiFi
   hub or minimal build drop it. Risk: medium, wide UI coupling. Effort: medium.

9. `FURBLE_IR` for the IR trigger. About 15.8 KB (rmt driver plus FurbleIR).
   Self contained, clean opt in. Risk: low. Effort: medium.

10. `FURBLE_FEEDBACK` or an audio subgate. About 14 KB when the I2S speaker path
    is excluded. Risk: medium, board capability detection. Effort: medium.

11. `FURBLE_COMPANION` for the companion GATT service and BLE OTA. About 13.8 KB
    of app code, and it is the only user of the NimBLE GATT server and
    peripheral role. Gating it enables item P. Risk: medium. Effort: medium.

12. Trim NimBLE for a central only profile. With Companion gated off, disable
    `CONFIG_BT_NIMBLE_ROLE_PERIPHERAL`, `CONFIG_BT_NIMBLE_GATT_SERVER` and
    `CONFIG_BT_NIMBLE_ROLE_BROADCASTER`, leaving central and observer. Also
    review the extended feature set that furble does not use for camera
    discovery: `CONFIG_BT_NIMBLE_EXT_SCAN`, `LE_CODED_PHY`,
    `ENABLE_PERIODIC_SYNC`, `50_FEATURE_SUPPORT`, and the twelve built in
    standard GATT services (PROX, ANS, CTS, HTP, IPSS, TPS, IAS, LLS, SPS, HR,
    BAS, DIS) that furble never registers. Saving: est 5 to 20 KB, and RAM.
    Risk: medium to high, every vendor must still be discovered and paired, so
    verify against the FauxNY test camera plus real Fujifilm. Effort: medium.
    Some of these standard services may already be dropped by `--gc-sections`;
    measure before and after.

13. Per vendor camera gating (`FURBLE_VENDOR_*`). libfurble is only 13,290 bytes
    for all vendors and infrastructure, so per vendor savings are 1 to 3 KB
    each and are blocked by the `CameraList.cpp` registration point. Low value,
    high refactor cost. Only worth it for a single vendor build. Deprioritize.

### Group D, hygiene and RAM

14. Scope out WiFi, Ethernet, mbedTLS TLS and the certificate bundle in the
    release sdkconfigs (`CONFIG_ESP_WIFI_ENABLED`, `CONFIG_ETH_ENABLED`, the
    mbedTLS TLS server and client, `CONFIG_MBEDTLS_CERTIFICATE_BUNDLE`). No
    measurable flash effect today, they are already dropped by the linker, but
    it reduces build time and RAM reservation and prevents these from silently
    bloating the image once the WiFi hub track links real network code. Risk:
    low. Effort: low. When PR #66 lands, it must re-enable exactly what MQTT and
    TLS need, and choose the smallest certificate bundle, not the full 200 cert
    default.

15. RAM notes, not flash. `CONFIG_BT_NIMBLE_MAX_CONNECTIONS` is 9 and
    `MAX_BONDS` is 15, both larger than the multiconnect model needs. Reducing
    them frees NimBLE static RAM. The NimBLE host task stack is 16384 bytes.
    Track these under the power and RAM work in plan 98, not here, but revisit
    if a build runs short on internal DRAM.

### Not recommended near term

- Link time optimization (LTO). Not enabled. Can save 5 to 10% but ESP-IDF LTO
  is fragile around IRAM and section attributes and often breaks the build.
  Skip unless a specific build is desperate for space.
- Trimming mbedcrypto (44 KB). NimBLE secure pairing (FujifilmSecure and
  others) needs AES and ECDH, so removing it breaks pairing. Not worth the risk.

## Quick wins versus larger refactors

- Quick wins, config only, no source change, high confidence: items 1, 2, 4, 5,
  6 and 14. Realistic combined saving roughly 35 to 90 KB, each measured by the
  size report. Item 3 (newlib nano) is a quick config change but needs its own
  PR for formatting verification, worth another 20 to 40 KB.
- Larger refactors, source plus CMake, needed to actually create headroom for
  the WiFi hub track: items 7 to 12, introducing `FURBLE_*` gates one feature
  per PR. A lean build profile that turns off SD, IR, feedback, companion and
  GPS could free roughly 130 KB or more, which is the release valve for MQTT.

## Recommended sequence

1. Group A config PR, items 1, 2, 4, plus item 5 LVGL trim and item 14 hygiene.
   One PR touching only the five sdkconfigs (and `sdkconfig.debug` for item 1).
   Measure each toggle with the size report, verify on the StickS3.
2. newlib nano PR, item 3, on its own so formatting can be verified in isolation.
3. Icon audit PR, item 6.
4. Feature gate PRs, items 7 then 11 (which unlocks 12) then 9, 10, 8. One
   feature per PR, each with a CMake exclusion and a lean CI build env, each
   hardware verified. Item 11 before 12 so the NimBLE server can be dropped.
5. Land the NimBLE central only trim, item 12, once Companion is gateable.

## Measuring each change

The size instrumentation already exists. `.github/workflows/main.yml` builds
every release and debug env, extracts the PlatformIO flash and RAM summary into
`firmware-size-<env>.json`, and the `size-report` job posts a sticky PR comment
with the flash and RAM delta against the latest master build. The flash slot is
hardcoded to 1700K (1,740,800 bytes) to match the OTA partition. Every
optimization PR therefore gets an automatic measured delta with a warning flag
if flash grows over 1% or RAM grows at all. Use it as the source of truth for
the estimates above, do not trust the estimates alone.

## OTA and partition context

The two OTA layout gives each app slot 1700K. At 72.6% today there is 476 KB of
slack, but the app cannot grow past 1700K without repartitioning, which is plan
34's subject. The WiFi hub track (plans 31, 33, PRs #53, #66, #90) adds a WiFi
stack, an MQTT client, TLS to the broker and Home Assistant discovery. A
realistic estimate is +150 to +300 KB once network code is actually linked and
inited, which would push the image to roughly 82 to 90% of the slot. That is
the motivation for this plan: bank the Group A quick wins now, and build the
Group C feature gates so a WiFi hub image can drop SD, IR, feedback and audio to
stay inside 1700K without a repartition.

## Cross references

- plan 33 wifi-hub, plan 31 s3-psram, plan 34 ota-partitions: the demand side.
- plan 98 power-optimization-audit: RAM and energy, distinct axis from flash.
- plan 24 sd-gpx-logging, PR #41: coordinate the `FURBLE_SD` gate.
- plan 61 camera catalog, PR #124: vendor library growth pushes libfurble up.

## Implementation state: Group A quick wins (feat/106-flash-size-quickwins)

Landed as a single config-only PR off fork master `9a2f7be`, touching only the
five committed `sdkconfig.*` files. No source, no runtime behavior change. The
same 13 line changes are applied identically to all five board configs.

### Items implemented

1. Item 1, cap compiled log level to INFO. The plan named
   `CONFIG_LOG_MAXIMUM_LEVEL_INFO=y`, which is not a real symbol in the ESP-IDF
   log Kconfig choice. The correct way to cap the maximum at the default (INFO,
   level 3) is `CONFIG_LOG_MAXIMUM_EQUALS_DEFAULT=y`, with
   `# CONFIG_LOG_MAXIMUM_LEVEL_VERBOSE is not set` and
   `CONFIG_LOG_MAXIMUM_LEVEL=3`. The runtime default was already INFO
   (`CONFIG_LOG_DEFAULT_LEVEL=3`), so no DEBUG or VERBOSE line was ever printed
   at runtime; only their format strings are dropped from `.flash.rodata`.
   Deviation from the plan: there is no separate `sdkconfig.debug`. The debug
   envs share the release sdkconfig by design (see CLAUDE.md and
   platformio.ini) and already force full logging in furble's own translation
   units through the existing `-DLOG_LOCAL_LEVEL=ESP_LOG_VERBOSE` build flag, so
   the `*-debug` builds keep their console debug logging without a second
   config file.

2. Item 2, silence release assertions. Set
   `CONFIG_COMPILER_OPTIMIZATION_ASSERTIONS_SILENT=y` and
   `CONFIG_COMPILER_OPTIMIZATION_ASSERTION_LEVEL=1` (plus the legacy
   `CONFIG_OPTIMIZATION_*` aliases). The build then derived
   `CONFIG_HAL_DEFAULT_ASSERTION_LEVEL=1`, so the HAL assertion level follows to
   SILENT too, matching the plan intent, and that derived symbol is committed
   identically across all five files. SILENT keeps the assertion check and abort,
   it only drops the `__FILE__` and message strings, so control flow on the
   happy path is byte for byte identical. Because debug envs share this config,
   a failing assert in a debug build now aborts without the message string too.
   That is a diagnostic reduction for debug builds, not a functional change, and
   was accepted rather than introducing separate debug sdkconfig files (a
   structural change the CLAUDE.md design explicitly avoids).

4. Item 4, drop the esp_err name table. Set
   `# CONFIG_ESP_ERR_TO_NAME_LOOKUP is not set`. `esp_err_to_name` now returns
   the hex code instead of a name string.

5. Item 5, disable unused LVGL widgets and the second theme. Disabled
   `LV_USE_ARCLABEL`, `LV_USE_BUTTONMATRIX` and `LV_USE_THEME_SIMPLE`, all
   proven unused by a full grep of `src/`, `include/` and `lib/`.
   Deviation from the plan: the plan also listed `LV_USE_TEXTAREA` as unused. It
   is NOT safe to disable. `lv_spinbox` derives from the textarea class
   (`lv_spinbox.c`: `.base_class = &lv_textarea_class`) and furble uses spinbox
   in `src/FurbleCalibrate.cpp`. TEXTAREA is kept. BUTTONMATRIX is safe because
   furble uses neither `lv_calendar` nor `lv_keyboard` (its only dependents in
   this LVGL 9.x), and `lv_msgbox`/`lv_dropdown` no longer reference it.

### Measured size delta (firmware.bin, FURBLE_VERSION=dev FURBLE_TEST=0)

| Env | Before (bytes) | After (bytes) | Delta | % |
|---|---:|---:|---:|---:|
| m5stick-s3 | 1,264,736 | 1,183,888 | -80,848 | -6.39 |
| m5stick-c | 1,199,984 | 1,127,216 | -72,768 | -6.06 |
| m5stick-c-plus | 1,204,544 | 1,130,288 | -74,256 | -6.16 |
| m5stack-core | 1,252,144 | 1,177,920 | -74,224 | -5.93 |
| m5stack-core2 | 1,252,240 | 1,178,032 | -74,208 | -5.93 |

For the primary M5StickS3 the OTA slot use drops from 72.65% to 68.01% of the
1,700K app partition, banking about 79 KB of headroom for the WiFi hub track.
The bulk of the saving is the log level cap and silent assertions removing
DEBUG/VERBOSE format strings and assert `__FILE__`/message strings from
`.flash.rodata` across every ESP-IDF component; the LVGL widget and theme
disables contribute the remainder.

### Deferred

- Item 3, newlib nano formatting. Deferred to its own PR as the plan directs.
  furble relies on float and width formatting (GPS coordinates, battery percent,
  timer and version strings), which nano changes, so it needs isolated
  verification in the sim and on hardware.
- Item 6, icon audit. 18 of the 65 icon arrays are provably unreferenced
  (`icon_restart_alt_24`, `icon_remote_gen_24`, and 16 plain size-alias twins
  such as `icon_delete`, `icon_settings`). They are already dropped from the
  linked image by `--gc-sections` (confirmed absent from the m5stick-s3
  `firmware.elf` while used icons like `icon_delete_24` are present), so
  removing their source files saves zero flash today. Deferred as a pure source
  hygiene change with no size benefit.
- Item 14, WiFi/Ethernet/mbedTLS hygiene scope-out. Out of scope for this pass;
  no flash effect today and it belongs with the WiFi hub track that will
  re-enable exactly what it needs.
- Group C feature gating (items 7 to 13), the `FURBLE_*` compile gates. These
  are structural source and CMake changes with behavior and UX implications and
  remain one PR per feature, unchanged from the plan.
