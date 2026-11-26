# 347 review feedback

Adversarial review of PR 524, the reversal of the #518 `ESP_LOG_INFO` cap. The
review did not read the diff and agree with it: every claim the PR makes was
rebuilt or re-derived on base `81139818` and head `5c09dd24`, with
`FURBLE_VERSION=dev FURBLE_TEST=0 pio run -e <env>`.

The PR has two halves and each can fail in one direction while the other hides
it. Restoring the log maximum fails silently if the flag comes back but the
DEBUG and VERBOSE call sites do not, and the guard fails silently if it reaches
a board that carries the part, because the only symptom is a motion wake that
never fires. So the review set out to prove both halves from the built ELFs,
not from the source.

Two sessions built on this host at the same time and the volume sat at 97 to 98
percent, so the host reclaimed build directories under `/tmp` mid-review. Every
figure below was measured; where an artifact did not survive to a second gate
run, the entry says so instead of quoting the PR.

## Findings

| # | Severity | Finding | Resolution | Evidence |
| --- | --- | --- | --- | --- |
| 1 | Important | plan 347's gates row and the PR body claimed "the three Core ELFs contain no `bmi270` symbol at all". That is false. The guarded `m5stack-core-debug` ELF carries 16 furble-side BMI270 symbols, `Furble::IMU::BMI270Backend` and its operators, vtable and destructors, plus `Furble::IMU::createBMI270Backend`, because the guard is on M5Unified's driver and `src/FurbleIMUMotionBMI270.cpp` is untouched and still probes by chip ID on every board. A gate row that a one-line `nm` disproves is worse than no row. | Fixed. The row now claims exactly what is gone, no `m5` symbol whose name contains `bmi270`, which is `m5::BMI270_Class` and `m5::bmi270_config_file`, and names the furble-side symbols that stay and why. | `nm -S firmware.elf`, filtered to `bmi270` and then to the `_ZN2m5` mangling: 0 matches on the three PR Core ELFs, 12 on base master's `m5stack-core-debug`, `m5stick-s3` and `m5stack-core2`. `nm` also lists 16 `_ZN6Furble3IMU...BMI270Backend` symbols in the guarded Core ELF. |
| 2 | Important | plan 347 gave the guarded `m5stack-core-usb-debug` two different sizes in one document: the per-env table said 1,756,768 with a 9,120-byte guard, the gates table said 1,756,784 with 9,104, and the PR body repeated the second. A document that quotes one measurement two ways cannot be checked, and this is the lane the PR is also fixing. | Fixed. The per-env table now carries the gates figure, so every number in the plan comes from one build, and the drift paragraph says so and lists the figures this review rebuilt on another host. The rebuilt lane measured 1,756,448 here, which is neither of the printed pairs but is inside the few hundred bytes of longcall drift the plan already documents, so the defect was the duplicated figure and not the guard size. | Two `firmware.bin` builds of the same env on the same base, plus the plan's own drift paragraph. |
| 3 | Minor | plan 347 justified its "this is not furble's own logging" argument with "`src/`, `lib/furble/` and `lib/hal/` hold 37 `ESP_LOGD` and `ESP_LOGV` sites in total". The tree holds 33 `ESP_LOGD` sites and no `ESP_LOGV` site at all. | Fixed to 33 and the fact that there is no VERBOSE site in furble, which strengthens the argument rather than weakening it. | `grep -rnE "ESP_LOG[VD]\(" src lib/furble lib/hal` gives 33, none of them `ESP_LOGV`; 15 in `src/`, 18 in `lib/furble/`, 0 in `lib/hal/`. |
| 4 | Important | `docs/power/soc.md` cited `sdkconfig.m5stick-s3:2582-2594` and `sdkconfig.m5stick-c:2152-2157` as the LOG level block, five times. Those lines hold the USB-OTG host section and the PThreads section. The LOG block is at `sdkconfig.m5stick-s3:1975-1985` and `sdkconfig.m5stick-c:1710-1720`. The PR rewrote the sentence around them for exactly the reason that #518 mis-described its own scope, and left the numbers, so the corrected bullet quoted two right `platformio.ini` ranges next to two wrong sdkconfig ones. The citation gate cannot see this: two code spans in one clause are checked for path and range only, and 2582 is inside a 3,309-line file. | Fixed in all five places, not only the bullet this PR edits, so the document does not cite one fact two ways. | Corrected ranges re-read: both start at `# CONFIG_LOG_DEFAULT_LEVEL_NONE is not set` and end at `CONFIG_LOG_MAXIMUM_LEVEL=3`. `tools/check_doc_citations.py` still reports 0 errors of 2,757 citations in 147 files. |
| 5 | Minor | `m5stack-core2` passes `-DFURBLE_IMU_BMI270=1` with no stated reason, and the tree says two things about that board. `docs/components/bmi270.md` says the StickS3 is the only board that carries the part; `docs/components/mpu6886.md:8` says `m5stack-core2` and a M5Tough carry an MPU6886. plan 347's Cost section read "Only the StickS3 carries the part. It names the macro at 1, Core2 names it at 1 as well", which does not explain why one MPU6886 board keeps an 8 KiB Bosch blob that the other MPU6886 board now drops. | Not a code change, and the safe direction is already taken: the define equals the default, so the lane keeps the part either way, and its ELF still has the blob. Clarified instead: a comment beside the `platformio.ini` flag and a Cost paragraph plus a follow-up now say the Core2 define is the conservative choice on a part inventory nothing here audited, on a lane with more than 4 MB of free app slot, and name the two pages that disagree. | `m5stack-core2` measures 1,912,976 on both trees with `m5::bmi270_config_file` at `0x2000` in the ELF, so the define changes nothing today. |
| 6 | Minor | The new `docs/crash-diagnostics.md` paragraph said "Release images cap at `CONFIG_LOG_MAXIMUM_LEVEL=3`". That is true of the six release envs, but the tree also ships the developer `esp32-s3-headless` hub image with `CONFIG_LOG_MAXIMUM_LEVEL=5`, so the first reader who checks a second image gets "this doc is wrong about its scope", which is the failure that made #518 worth reverting. | Fixed with one clause and a citation, so the third case is stated rather than left for the reader. | `sdkconfig.esp32-s3-headless:1981-1991` is `CONFIG_LOG_DEFAULT_LEVEL=3` with `CONFIG_LOG_MAXIMUM_LEVEL_VERBOSE=y` and `CONFIG_LOG_MAXIMUM_LEVEL=5`. PlatformIO resolves an unspecified path to `sdkconfig.<PIOENV>`, so that file is the headless image's config. |

## Checked and sound

The findings above are claims, not behaviour. These are the things the PR could
have broken to buy bytes, checked one at a time and found intact.

* **The probe guard wraps the BMI270 arm only.** M5Unified 0.2.20's
  `IMU_Class::begin` has three `if (_imu == imu_t::imu_none)` blocks, at lines
  74, 122 and 164. The patch opens its `#if` before 74 and closes it before 122,
  so the guarded region is the BMI270 arm and the blank line after it. The
  SH200Q arm at 122 and the BMM150 and AK8963 probing after it are outside the
  guard and still compile. No board lost a probe it needs.
* **No enum changed.** `imu_t`, including `imu_bmi270`, lives in the unpatched
  `IMU_Class.hpp`, so `src/FurbleConsole.cpp:2612` still compiles its
  `case m5::imu_bmi270` on every env. The guard removes the code that can
  return that value on the Core, not the value itself, which is why all fifteen
  envs in `platformio.ini` still compile.
* **Furble's own BMI270 backend is unchanged and still degrades correctly.** Its
  `arm()` reads the chip ID at register `0x00` and refuses anything that does
  not answer `0x24`, so on a guarded Core it fails the same way it failed
  before the PR, and `MotionSource::arm` falls through to
  `createMPU6886Backend`. Nothing in `src/` reaches the part through
  `m5::BMI270_Class`.
* **Only the Core family opts out.** `-DFURBLE_IMU_BMI270=0` appears once, on
  `[env:m5stack-core]`, and both Core debug profiles inherit it through
  `${env:m5stack-core.build_flags}`. `m5stick-c` and `m5stick-c-plus` never set
  the macro and so keep the default of 1, which is the direction that cannot
  lose a part.
* **Both patches default the macro to 1**, so a tree built without the define
  is upstream behaviour. The patch script fails closed: `apply_patch` raises
  rather than leaving a half-applied source.
* **The reversal is recorded, not contradicted.** plan 347 keeps the #518
  measurement, says the cap "is reverted", and states why the trade was wrong,
  rather than deleting the history or leaving a section that still recommends
  it. `plans/README.md` and `plans/106` were rewritten to match.
* **No log level is lowered anywhere.** `platformio.ini`'s only level change is
  `ESP_LOG_INFO` to `ESP_LOG_VERBOSE` on `m5stack-core-debug`. No assertion,
  watchdog, panic path, console command, documented console output or partition
  table changed.

## The log maximum is back, measured on the ELF

Four strings that only exist above INFO, and where each was found. The two
ESP-IDF strings are the ones plan 347 named, and `device registered on bus` is
an `ESP_LOGV` at `components/esp_hw_support/spi_bus_lock.c:660` in ESP-IDF
5.5.3, so its presence proves the compiled maximum is 5 and not merely 4.

| String | Level | master `m5stack-core-debug` | PR `m5stack-core-debug` | PR `m5stack-core-usb-debug` | master `m5stack-core-usb-debug` |
| --- | --- | ---: | ---: | ---: | ---: |
| `APLL will working at` (`clk_ctrl_os.c:129`) | DEBUG | 0 | 1 | 1 | 0 |
| `device registered on bus` (`spi_bus_lock.c:660`) | VERBOSE | 0 | 1 | 1 | 0 |
| `Reaped %u zombie target(s)` (`src/FurbleControl.cpp`) | DEBUG | 0 | 1 | 1 | 0 |
| `Dropped camera command %d` (`src/FurbleControl.cpp`) | DEBUG | 0 | 1 | 1 | 0 |

The USB profile is the half of this the PR had to prove hardest, because #518
never touched its flags directly: it inherited the cap from
`m5stack-core-debug`. It was capped on master, 1,740,592 bytes in a 4,063,232
byte factory slot with 2,322,640 bytes free, and it is uncapped again on the PR
head, 1,756,448 bytes with 2,306,784 free.

## The BMI270 guard, proved both ways

`nm -S` and `objdump -d` on the built ELFs, on both trees. The blob is a
`static constexpr` array, so its symbol is the internal-linkage `_ZN2m5L18bmi270_config_fileE`.

| Tree | Env | `m5::bmi270_config_file` | `m5` BMI270 symbols | `call8` to `m5::BMI270_Class::begin` |
| --- | --- | --- | ---: | ---: |
| master `81139818` | `m5stack-core-debug` | 0x2000 | 12 | 1 |
| master `81139818` | `m5stick-s3` | 0x2000 | 12 | 1 |
| master `81139818` | `m5stack-core2` | 0x2000 | 12 | 1 (`4010b1a3`, the address the PR cites) |
| PR `5c09dd24` | `m5stack-core-debug` | absent | 0 | 0 |
| PR `5c09dd24` | `m5stack-core` | absent | 0 | 0 |
| PR `5c09dd24` | `m5stack-core-usb-debug` | absent | 0 | 0 |
| PR `5c09dd24` | `m5stick-s3` | 0x2000 | 12 | 1 |
| PR `5c09dd24` | `m5stack-core2` | 0x2000 | 12 | 1 |

The A/B that isolates the guard from the log maximum: the same head tree built
twice with only `-DFURBLE_IMU_BMI270` changed, at `ESP_LOG_VERBOSE` both times.
With `=1` the image is 1,740,768 bytes, the blob is back at
`_ZN2m5L18bmi270_config_fileE` and `call8` to `BMI270_Class::begin` is back.
With `=0` the image is 1,731,648 bytes and both symbols are gone. The guard is
worth 9,120 bytes and it is worth exactly those two objects.

## Byte table

Every row is a `firmware.bin` from a build of that tree.
`m5stack-core-debug`'s slot is 1,740,800 bytes.

| Env | master `81139818` | PR head `5c09dd24` | Change |
| --- | ---: | ---: | ---: |
| `m5stack-core-debug` | 1,714,848 (`ESP_LOG_INFO`, blob) | **1,731,648** (`ESP_LOG_VERBOSE`, no blob) | +16,800 |
| `m5stack-core` | 1,627,632 (release, blob) | 1,618,496 (release, no blob) | -9,136 |
| `m5stack-core-usb-debug` | 1,740,592 (`ESP_LOG_INFO`, blob) | 1,756,448 (`ESP_LOG_VERBOSE`, no blob) | +15,856 |
| `m5stick-s3` | 1,862,480 | 1,862,480 | 0 |
| `m5stack-core2` | 1,912,976 | 1,912,976 | 0 |

`m5stick-s3` and `m5stack-core2` are byte-for-byte the same size on the two
trees, which is the strongest form of the claim that the guard does not reach
them. The two release lanes move in opposite directions and that is correct:
the Core release lane has no log maximum to restore, so it only loses the blob,
9,136 bytes on this pair of builds against the 9,120 the same-tree A/B isolates
on the debug lane, while the two debug lanes pay for the diagnostics they get
back.

The constrained lane ends at **1,731,648 of 1,740,800, 9,152 bytes of margin**,
against 320 bytes with the log maximum restored and no guard, and against the
431 bytes #518 was written to escape. The plan's own decomposition is 25,632
bytes for the restoration less 9,120 for the guard, which nets the +16,800 this
review measured end to end.

## Gates

| Gate | Result |
| --- | --- |
| `tools/check_size_budget.py --env m5stack-core-debug` | `PASS m5stack-core-debug: flash 1,731,648/1,740,800, dram 56,212/57,344, iram 119,036/119,808` |
| `tools/check_size_budget.py --env m5stack-core` | `PASS m5stack-core: flash 1,618,496/1,740,800, dram 55,148/56,320, iram 115,668/116,736` |
| `tools/check_size_budget.py --env m5stack-core-usb-debug` | `PASS m5stack-core-usb-debug: flash 1,756,448/4,063,232, dram 56,252/57,344, iram 119,756/120,832` |
| `tools/check_size_budget.py --env m5stick-s3` | `PASS m5stick-s3: flash 1,862,480/3,145,728, dram 47,496/48,128, iram 114,688/115,712` |
| `tools/check_size_budget.py --env m5stack-core2` | image is 1,912,976 on both trees, identical to master. The local build directory did not survive the host's disk reclamation, so the number is from the build log and the budget check for this env is the green `m5stack-core2` CI job, which runs this gate after its build |
| `tools/stack_budget.py --env m5stack-core-debug` | PASS, every task under its bound, deepest is `main` at 7,888 of 16,384 bytes |
| `tools/stack_budget.py --env m5stack-core-usb-debug` | PASS, every task under its bound |
| `tools/stack_budget.py --env m5stick-s3` | PASS, 16 tasks, deepest `webui_tls` at 8,032 of 12,288 bytes. Run against the 1,862,480-byte image, which this review measured identical on both trees |
| `tools/stack_budget.py --env m5stack-core` and `m5stack-core2` | covered by the green CI jobs for those envs. The guard removes a call edge and adds none, so it cannot deepen a stack |
| `python3 tools/check_doc_citations.py` | `0 errors of 2757 citations in 147 files` after the corrections |
| `sh sim/scripts/check-doc-tokens.sh` | `sim documentation tokens are present` |
| `tools/check_review_threads.py --repo MaxRink/furble --pr 524` | 0 review threads, 30 checks passed on head `5c09dd24` |

## What this review could not do

* It could not re-measure the `m5stack-core-usb-debug` guard on its own, because
  that build was reclaimed. The 9,120-byte guard figure comes from the
  constrained lane, which links the same two objects.
* It could not keep a second gate run on `m5stack-core2` locally for the reason
  above. The image size, the blob's presence and the CI gate for that env are
  recorded instead.
* It did not flash hardware, so the motion-wake claim rests on the linked
  symbols and the unchanged `arm()` chip-ID check rather than on a device.
