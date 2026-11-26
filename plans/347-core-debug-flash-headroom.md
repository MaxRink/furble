# 347 - Core OTA debug flash headroom

## Problem

`m5stack-core-debug` is a mandatory CI lane: `CLAUDE.md` says an image that
outgrows its OTA slot fails CI, and `tools/check_ci_workflows.py` requires the
profile in the discover matrix. It is a dual-OTA profile on a 4 MB part, so
`firmware.bin` must fit the 1700K app slot of `partitions_two_ota_large.csv`,
1,740,800 bytes.

The lane fits on master as #518 left it, because #518 took 24,832 bytes out of
it. Before #518 it was 535 bytes under the slot, and PR #493 (EXT_5V rail
ownership) needed 848, so that branch was over. With a mandatory lane, one
blocked PR blocks every firmware PR behind it, which is why the lane needs
headroom larger than the drift of a single feature.

## Cause

Two separate things were confused in #518, and the confusion is what this plan
corrects.

1. **The lane was not over the slot.** CI measured 1,740,265 bytes with 535
   bytes of slack on the #518 base, and #493 is what needed 848. There was no
   emergency to fix, only headroom to buy.
2. **The bytes #518 bought were the image's diagnostics, not waste.**
   `-DLOG_LOCAL_LEVEL=...` is a compiler flag in `build_flags`, so it sets the
   compiled log maximum of *every* translation unit in the image, not just
   furble's. It overrides the image-wide `CONFIG_LOG_MAXIMUM_LEVEL=3` cap that
   plan 106 group A put in every sdkconfig, so at `ESP_LOG_VERBOSE` the Core
   image carries the DEBUG and VERBOSE format strings of `src/`, `lib/furble/`,
   `lib/hal/`, LVGL, M5GFX, M5Unified, lwIP, net80211, NimBLE and every other
   component that logs. Two `esp_hw_support` literals that only ever compile at
   DEBUG and VERBOSE, "APLL will working at" and "device registered on bus", are
   in the built Core debug image while `sdkconfig.m5stack-core:1720` caps the
   maximum level at 3. The #518 plan text said the cap reached "furble's own
   translation units"; it did not, and it also silently reached
   `m5stack-core-usb-debug`, which inherits the profile and has a 4,063,232-byte
   factory slot with about 2.3 MB free.

The waste this plan removes instead is real dead weight: `m5::bmi270_config_file`
is an 8,192-byte Bosch BMI270 initialisation blob with the driver that uploads
it, and M5Unified's IMU probe references the array, so `--gc-sections` cannot
drop it. The Core's IMU is an MPU6886 or MPU9250 and the image is pinned to
`board_M5Stack` by `M5GFX_BOARD`, so that probe can never succeed there.

## Change

1. `patches/m5unified_bmi270_blob.patch` and
   `patches/m5unified_bmi270_probe.patch` guard the M5Unified BMI270 driver and
   the probe that calls it behind `FURBLE_IMU_BMI270`, which defaults to 1 so a
   tree without the define behaves exactly as upstream and a board that carries
   the part cannot lose it by omission. `patches/m5unified_bmi270.py` applies
   both from `post:patches/`, next to the M5GFX pin.
2. `platformio.ini` passes `-DFURBLE_IMU_BMI270=0` on `m5stack-core`, which the
   two Core debug profiles inherit with the rest of the flag list, and
   `-DFURBLE_IMU_BMI270=1` explicitly on `m5stack-core2` and `m5stick-s3`.
3. `[env:m5stack-core-debug]` compiles at `ESP_LOG_VERBOSE` again, the same
   level as the other five debug envs, so `m5stack-core-usb-debug` returns to
   VERBOSE with it.

Nothing else moves: `-Os -g2 -ggdb2`, `FURBLE_CONSOLE`, `FURBLE_TASK_WDT`,
`FURBLE_DEBUG_BUILD`, `sdkconfig.debug`, the partition table, the console
commands, every assertion, watchdog and panic path, and the stack and size
budget steps all stay as they are.

## The INFO cap this plan first shipped, and why it was reverted

#518 merged `-DLOG_LOCAL_LEVEL=ESP_LOG_INFO` into this profile and described it
as fitting the slot. Two builds of the same tree that differ only in that flag
measure the trade: 1,740,528 bytes at `ESP_LOG_VERBOSE` and 1,715,696 at
`ESP_LOG_INFO`, 24,832 bytes on the #518 base. That is not furble's own
logging: `src/`, `lib/furble/` and `lib/hal/` hold 33 `ESP_LOGD` sites and no
`ESP_LOGV` site at all, which cannot account for 24,832 bytes, and the rest is
the DEBUG and VERBOSE strings of ESP-IDF, LVGL, M5GFX and M5Unified. The lane
exists for field diagnostics, so selling its log maximum
for bytes is the one trade it cannot make, and the console `log <tag> debug`
path plus the crash record's retained tail both lost lines to it. Rejected: the
cap is reverted, and the 8 KiB BMI270 blob pays for the restoration instead.

## Candidates measured

Every row is a comparison of two built images from this tree, except where the
row says what the measurement was, and none of them is an estimate. Base
`81139818`, `FURBLE_VERSION=dev FURBLE_TEST=0 pio run -e <env>`.

| Candidate | What was measured | Bytes |
| :--- | :--- | ---: |
| BMI270 blob and probe | `m5stack-core-debug` at `ESP_LOG_VERBOSE`, `-DFURBLE_IMU_BMI270=0` against the same tree at `=1`: 1,731,648 against 1,740,480. `m5::bmi270_config_file` (8,192 bytes) and every `m5::BMI270_Class` symbol are absent from the guarded ELF | 8,832 |
| Duplicate string literals | the built `.flash.rodata` holds 76 duplicated printable runs, 2,793 bytes, and every copy is inside `libpp.a(lmac.o)`, `libnet80211.a`, `libspi_flash.a(esp_flash_api.o)` or another ESP-IDF or precompiled object. This repo's TUs emit `.rodata.str1.1`, which ld merges image-wide, so a dedup change here recovers **0** | 0 |
| Repeated format strings | the largest repeat is `%s mode switch cause sta destroyed` 15 times, in `libnet80211.a`; `%s %s %u` 35 times, in `libpp.a(lmac.o)`. Both are precompiled vendor objects, and shortening a format string is not possible in this repo | 0 |
| Per-file log tags | 9 TUs declare a literal `LOG_TAG`, all 9 distinct, 57 bytes of literals in total, and ld already merges the identical ones. Sharing them recovers **0** and would cost every console `log <tag>` filter its meaning | 0 |
| printf-heavy cold paths | the newlib formatter is already linked for every caller: `_svfprintf_r` 11,210 bytes, `_vfprintf_r` 11,429 and `__ssvfiscanf_r` 7,505, all referenced from the console and OTA and MQTT paths. Removing one cold `printf`-shaped call site leaves the formatter linked, so it recovers **0** | 0 |
| Tables that could be const or moved to flash | every font and icon table is already in `.flash.rodata` (`glyph_bitmap` 32,484 + 20,639 + 11,090 + 6,671 + 4,549, `kern_class_values` 4 x 2,989, `icon_camera_map` 2,875). The whole app layer holds 1,132 bytes of `.data` across all `src/*.o`, and moving it to flash would not shrink the image because the initialiser still ships | 0 |
| Dead code kept alive by a symbol reference | the BMI270 row is this class of waste; no other candidate of that size exists in the symbol table, whose next largest non-font data objects are 2,048 bytes (`GPS::getInstance()::instance`, `vflash_mem`) and are live | 0 |

## Verified figures

| Tree | `firmware.bin` | Slack to 1,740,800 |
| :--- | ---: | ---: |
| base `81139818`, merged `ESP_LOG_INFO`, no guard | 1,714,848 | 25,952 |
| `ESP_LOG_VERBOSE`, no guard | 1,740,480 | 320 |
| `ESP_LOG_VERBOSE`, BMI270 guard, this PR | 1,731,648 | 9,152 |

Restoring the log maximum alone costs 25,632 bytes on this base and leaves 320,
which is thinner than the 431 bytes #518 was written to escape: the guard is
what turns the restoration into headroom.

The same two objects are the only difference on the other two Core profiles, so
the same define measures the guard there:

| Env | before the guard | with the guard | Guard |
| :--- | ---: | ---: | ---: |
| `m5stack-core-debug` (VERBOSE) | 1,740,480 | 1,731,648 | 8,832 |
| `m5stack-core` (INFO, release) | 1,627,648 | 1,618,496 | 9,152 |
| `m5stack-core-usb-debug` (VERBOSE) | 1,765,888 | 1,756,784 | 9,104 |

`m5stick-s3` (1,862,496) and `m5stack-core2` (1,912,976) pass
`-DFURBLE_IMU_BMI270=1` and the macro already defaulted to 1, so they have one
configuration rather than two: their images are the same size as on master,
`m5::bmi270_config_file` is still linked at 8,192 bytes, and the disassembly
still calls `m5::BMI270_Class::begin` from the IMU probe.

Two clean builds of one tree differ by a few hundred bytes because ESP-IDF
archive order moves longcall relaxation (plan 106 review, finding 3), so each
row is one build and the headroom, not a byte-exact delta, is the claim, and
every figure in the tables above and in Gates below is the same build. The
review on `plans/347-review-feedback.md` rebuilt five of these lanes on another
host and landed within that drift: 1,714,848 and 1,731,648 exactly, 1,740,768
unguarded, 1,756,448 for the USB lane and 1,862,480 for the StickS3. Every table
here is base `81139818`; master moved to `85b89931` while the review ran and the
branch was rebased onto it, where this lane builds at 1,733,504 of 1,740,800,
7,296 bytes of margin, and the review record carries both bases. A
second clean build of the guarded tree measured 1,731,344 bytes, 9,456 bytes of
headroom, and the two are the same tree.

## Cost

Only the StickS3 carries the part: `docs/components/bmi270.md` says so, and it
is the one lane that depends on the driver for motion wake. It names the macro
at 1 and the default is 1 anyway, so the driver stays wherever it might be
needed and only the Core family opts out. What the Core loses is a probe that
cannot succeed on hardware it is pinned to: `M5Unified` reaches the BMI270 arm
only after its MPU6886 probe has failed on the bus `board_M5Stack` selects
(`In_I2C` on GPIO 21/22), and the Core answers there as an MPU6886 or MPU9250.
`M5Unified` still probes the MPU6886, MPU9250, SH200Q, BMM150 and AK8963 all
the same, and `Furble::IMU` still creates its BMI270 backend only where
`M5Unified` reports one. Nothing in `src/` reads `M5.Imu` through
BMI270-specific types.

`m5stack-core2` also names the macro at 1, and that one is a conservative
choice rather than a measured need. `docs/components/mpu6886.md` puts an MPU6886
in the Core2 and a M5Tough, so on this repo's own part inventory the Core2 lane
could opt out the same way and take the 8 KiB. It does not, because nothing in
this PR audited the Core2 and Tough part inventory board by board the way the
Core's was audited, and its app slot has more than 4 MB free
(1,912,976 of 6,291,456 bytes). A future PR that settles that inventory can
flip it; until then the define keeps the ambiguous lane safe and says so beside
the flag in `platformio.ini`.

The Core debug profiles keep the full log maximum, so the console `log <tag>
debug` path and the crash record's retained tail behave exactly as they do on
the other five debug envs, which is the point of this PR.

## Alternatives rejected

- **Keeping the INFO cap on the USB profile.** Its factory slot is 4,063,232
  bytes with about 2.3 MB free; there was no byte pressure to justify it, and
  the profile inherits its flags from the dual-OTA one.
- **newlib nano formatting** (plan 106 item 3) frees 20 to 40 KB on every image
  but changes float and width formatting for GPS coordinates, battery percent,
  timers and version strings, and needs a sim and hardware pass first.
- **Feature gating** (plan 106 group C) frees more, 66 KB for `FURBLE_SD`
  alone, but each gate is a user-visible capability change on the Core.
- **Raising the slot.** `partitions_two_ota_large.csv` is ESP-IDF's 1700K
  two-OTA table, so the app partition cannot grow while keeping two OTA slots.
- **Dropping an assertion, the task watchdog or a log level** trades the
  behaviour the lane exists for.

## Gates

| Gate | Result |
| :--- | :--- |
| `pio run -e m5stack-core-debug` | SUCCESS, `firmware.bin` 1,731,648 of 1,740,800 bytes, `ESP_LOG_VERBOSE` |
| `pio run` on the other four | SUCCESS: `m5stack-core` 1,618,496, `m5stack-core-usb-debug` 1,756,784, `m5stick-s3` 1,862,496, `m5stack-core2` 1,912,976 |
| `tools/check_size_budget.py --env <each>` | `PASS m5stack-core-debug: flash 1,731,648/1,740,800, dram 56,212/57,344, iram 119,036/119,808`; `PASS m5stack-core: flash 1,618,496/1,740,800, dram 55,148/56,320, iram 115,668/116,736`; `PASS m5stack-core-usb-debug: flash 1,756,784/4,063,232, dram 56,268/57,344, iram 119,756/120,832`; `PASS m5stick-s3: flash 1,862,496/3,145,728, dram 47,496/48,128, iram 114,688/115,712`; `PASS m5stack-core2: flash 1,912,976/6,291,456, dram 56,768/57,344, iram 121,036/121,856` |
| `tools/stack_budget.py --env <each>` | PASS for the same five envs, every task under its bound, no new recursion or dynamic bound |
| BMI270 still linked on the boards that have it | `m5::bmi270_config_file` (8,192 bytes) and a `call8` to `m5::BMI270_Class::begin` from the IMU probe are in the `m5stick-s3` and `m5stack-core2` ELFs. The three Core ELFs have **no `m5` symbol whose name contains `bmi270`**: filter `nm -S firmware.elf` to `bmi270` and then to the `_ZN2m5` mangling and nothing matches, so `m5::BMI270_Class` and the blob are both gone. They do keep furble's own `Furble::IMU::BMI270Backend` and `Furble::IMU::createBMI270Backend`, 16 symbols, because the guard is on M5Unified and not on `src/`: that backend identifies the chip by register reads and falls through to the MPU6886 backend when the part is absent, exactly as it did before |
| `tools/pre_review.py --base origin/master` | `PASS: 13 gates` |
| `sh sim/scripts/check-doc-tokens.sh` | `sim documentation tokens are present` |
| `tools/check_doc_citations.py` | `0 errors of 2757 citations in 147 files` |
| host suite | `python -m unittest discover -s tests` 633 tests OK, and the `tests/host` CMake harness passes |
| `python3 tools/check_m5gfx_pinning.py`, `tools/check_ci_workflows.py` | both pass; the profile is still discovered as a mandatory Core debug env |

## Follow-ups

The lane has 9 KB of headroom instead of 431 bytes. Plan 106 item 3 (newlib
nano) is the next structural saving, and item 7 (`FURBLE_SD`) the largest
single gate. The StickC and StickC Plus profiles inherit the default
`FURBLE_IMU_BMI270=1`, so neither image changes here. Their parts are MPU6886
and SH200Q per `docs/components/README.md`, which suggests the same 8 KiB is
dead weight there too, but their part inventory needs checking against each
variant before a lane opts out. The same holds for `m5stack-core2`, which this
PR keeps on the driver for the reason in Cost: settle whether every Core2 and
Tough revision carries an MPU6886 before dropping its define.
