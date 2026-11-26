# 347 review feedback

Adversarial review of PR 524, the reversal of the #518 `ESP_LOG_INFO` cap. The
review did not read the diff and agree with it: every claim the PR makes was
rebuilt or re-derived from a built ELF, with
`FURBLE_VERSION=dev FURBLE_TEST=0 pio run -e <env>`, on the base the PR was
opened against, `81139818`, and on the base it now sits on, `85b89931`.

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

Master also moved nine commits, to `85b89931`, while the review ran, including
`src/` work in the SD refusal, Sony discovery, StickC pin and memory lanes. The
branch was rebased onto it and every lane rebuilt, because a margin measured on
the base a PR was opened on is not the margin that ships. Both bases are
reported below.

## Findings

| # | Severity | Finding | Resolution | Evidence |
| --- | --- | --- | --- | --- |
| 1 | Important | plan 347's gates row and the PR body claimed "the three Core ELFs contain no `bmi270` symbol at all". That is false. The guarded `m5stack-core-debug` ELF carries 16 furble-side BMI270 symbols, `Furble::IMU::BMI270Backend` and its operators, vtable and destructors, plus `Furble::IMU::createBMI270Backend`, because the guard is on M5Unified's driver and `src/FurbleIMUMotionBMI270.cpp` is untouched and still probes by chip ID on every board. A gate row that a one-line `nm` disproves is worse than no row. | Fixed. The row now claims exactly what is gone, no `m5` symbol whose name contains `bmi270`, which is `m5::BMI270_Class` and `m5::bmi270_config_file`, and names the furble-side symbols that stay and why. | `nm -S firmware.elf`, filtered to `bmi270` and then to the `_ZN2m5` mangling: 0 matches on the three PR Core ELFs, 12 on base master's `m5stack-core-debug`, `m5stick-s3` and `m5stack-core2`. `nm` also lists 16 `_ZN6Furble3IMU...BMI270Backend` symbols in the guarded Core ELF. |
| 2 | Important | plan 347 gave the guarded `m5stack-core-usb-debug` two different sizes in one document: the per-env table said 1,756,768 with a 9,120-byte guard, the gates table said 1,756,784 with 9,104, and the PR body repeated the second. A document that quotes one measurement two ways cannot be checked, and this is the lane the PR is also fixing. | Fixed. The per-env table now carries the gates figure, so every number in the plan comes from one build, and the drift paragraph says so and lists the figures this review rebuilt on another host. The rebuilt lane measured 1,756,448 here, which is neither of the printed pairs but is inside the few hundred bytes of longcall drift the plan already documents, so the defect was the duplicated figure and not the guard size. | Two `firmware.bin` builds of the same env on the same base, plus the plan's own drift paragraph. |
| 3 | Minor | plan 347 justified its "this is not furble's own logging" argument with "`src/`, `lib/furble/` and `lib/hal/` hold 37 `ESP_LOGD` and `ESP_LOGV` sites in total". The tree holds 33 `ESP_LOGD` sites and no `ESP_LOGV` site at all. | Fixed to 33 and the fact that there is no VERBOSE site in furble, which strengthens the argument rather than weakening it. | `grep -rnE "ESP_LOG[VD]\(" src lib/furble lib/hal` gives 33, none of them `ESP_LOGV`; 15 in `src/`, 18 in `lib/furble/`, 0 in `lib/hal/`. |
| 4 | Important | `docs/power/soc.md` cited `sdkconfig.m5stick-s3:2582-2594` and `sdkconfig.m5stick-c:2152-2157` as the LOG level block, five times. Those lines hold the USB-OTG host section and the PThreads section. The LOG block is at `sdkconfig.m5stick-s3:1975-1985` and `sdkconfig.m5stick-c:1710-1720`. The PR rewrote the sentence around them for exactly the reason that #518 mis-described its own scope, and left the numbers, so the corrected bullet quoted two right `platformio.ini` ranges next to two wrong sdkconfig ones. The citation gate cannot see this: two code spans in one clause are checked for path and range only, and 2582 is inside a 3,309-line file. | Fixed in all five places, not only the bullet this PR edits, so the document does not cite one fact two ways. | Corrected ranges re-read: both start at `# CONFIG_LOG_DEFAULT_LEVEL_NONE is not set` and end at `CONFIG_LOG_MAXIMUM_LEVEL=3`. `tools/check_doc_citations.py` still reports 0 errors of 2,757 citations in 147 files. |
| 5 | Minor | `m5stack-core2` passes `-DFURBLE_IMU_BMI270=1` with no stated reason, and no page in this repo gives that board a Bosch part: `docs/components/bmi270.md:7` makes the StickS3 the only board that carries it, and both `docs/components/mpu6886.md:8` and the board table at `docs/power/datasheet-register.md:388` give `m5stack-core2` an MPU-6886. plan 347's Cost section read "Only the StickS3 carries the part. It names the macro at 1, Core2 names it at 1 as well", which never says why a board documented with an InvenSense part keeps an 8 KiB Bosch blob that the Core, also documented with an InvenSense part, now drops. Left as written, the next reader either accepts a false premise about the Core2 or deletes a define nobody explained. | Not a code change, and the safe direction is already taken: the define equals the default, so the lane keeps the part either way, and its ELF still has the blob. Clarified instead. No doc now claims the Core2 carries a BMI270; a comment beside the `platformio.ini` flag, a Cost paragraph and a follow-up say the define is the conservative choice on a part inventory nothing here audited, that the repo documents an MPU-6886 there, and that the lane has more than 4 MB of free app slot. | `m5stack-core2` measures 1,912,976 on both trees with `m5::bmi270_config_file` at `0x2000` in the ELF, so the define changes nothing today. |
| 6 | Minor | The new `docs/crash-diagnostics.md` paragraph said "Release images cap at `CONFIG_LOG_MAXIMUM_LEVEL=3`". That is true of the six release envs, but the tree also ships the developer `esp32-s3-headless` hub image with `CONFIG_LOG_MAXIMUM_LEVEL=5`, so the first reader who checks a second image gets "this doc is wrong about its scope", which is the failure that made #518 worth reverting. | Fixed with one clause and a citation, so the third case is stated rather than left for the reader. | `sdkconfig.esp32-s3-headless:1981-1991` is `CONFIG_LOG_DEFAULT_LEVEL=3` with `CONFIG_LOG_MAXIMUM_LEVEL_VERBOSE=y` and `CONFIG_LOG_MAXIMUM_LEVEL=5`. PlatformIO resolves an unspecified path to `sdkconfig.<PIOENV>`, so that file is the headless image's config. |
| 7 | Critical | The plan argued for the guard with "restoring the log maximum alone costs 25,632 bytes on this base and leaves 320, which is thinner than the 431 bytes #518 was written to escape". On the master this branch now sits on that is false and it is false in the direction that understates the PR: `ESP_LOG_VERBOSE` with no guard links at 1,742,137 bytes against the 1,740,800-byte slot and PlatformIO's `checkprogsize` fails the build, so the restoration alone does not produce a 320-byte margin, it produces no image at all. A plan that says the guard improves a margin invites the next reader to try dropping it. | Fixed. The verified-figures table now carries both bases, the failing row is quoted with the exact build error, and the section says the guard is what makes the restoration possible on `85b89931` rather than what widens its margin. The follow-up sentence that promised 9 KB "instead of 431 bytes" is corrected the same way. | `pio run -e m5stack-core-debug` with `-DFURBLE_IMU_BMI270=1` and only that flag changed: `Flash: 100.1% (used 1742137 bytes from 1740800 bytes)`, `Error: The program size (1742137 bytes) is greater than maximum allowed (1740800 bytes)`, `[checkprogsize] Explicit exit, status 1`. The same tree at `=0` builds at 1,733,504. |

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

Every cell in that table was measured on both bases, `81139818` and `85b89931`,
and the two agree, so the restoration is not an artefact of the rebase.

The USB profile is the half of this the PR had to prove hardest, because #518
never touched its flags directly: it inherited the cap from
`m5stack-core-debug`. It was capped on master, 1,740,592 bytes in a 4,063,232
byte factory slot with 2,322,640 bytes free, and it is uncapped again on the PR
head, 1,756,448 bytes with 2,306,784 free.

## The BMI270 guard, proved both ways

`nm -S` and `objdump -d` on the built ELFs. The blob is a `static constexpr`
array, so its symbol is the internal-linkage `_ZN2m5L18bmi270_config_fileE`.
Counting names that contain `m5::BMI270_Class` gives 15 on a lane that keeps the
driver: 12 functions, the two constructor and destructor alias pairs, the vtable
and the typeinfo. A count of the `_ZN2m5` mangling alone says 12, because the
vtable and typeinfo are `_ZTV` and `_ZTI`. Both counts are in the table so the
two ways of asking cannot disagree.

On the master this branch ships against, `85b89931`:

| Env | `m5::bmi270_config_file` | `m5::BMI270_Class` names | `call8` to `begin` | DEBUG and VERBOSE strings |
| --- | --- | ---: | ---: | --- |
| `m5stick-s3` | 0x2000 at `0x3c171b60` | 15 | 1 | none, it is a release image at INFO |
| `m5stack-core2` | 0x2000 | 15 | 1 | none, release image at INFO |
| `m5stack-core` | absent | 0 | 0 | none, release image at INFO |
| `m5stack-core-debug` | absent | 0 | 0 | all four present |
| `m5stack-core-usb-debug` | absent | 0 | 0 | all four present |

On master `81139818` the same three probes find the blob at `0x2000` and the
`call8` to `m5::BMI270_Class::begin` in `m5stack-core-debug`, and at
`4010b1a3` on `m5stack-core2`, which is the address the PR quotes. So the guard
reaches the Core family and only the Core family, and the probe a board needs is
still there on both lanes that keep the driver.

The A/B that isolates the guard from the log maximum, first on `81139818`: the
same tree built twice with only `-DFURBLE_IMU_BMI270` changed, at
`ESP_LOG_VERBOSE` both times. With `=1` the image is 1,740,768 bytes with the
blob and the probe back. With `=0` it is 1,731,648 bytes with both gone. The
guard is 9,120 bytes and it is exactly those two objects.

Repeated on `85b89931`, which is the measurement that matters: with `=1` the
link produces 1,742,137 bytes, `m5::bmi270_config_file` is back at `0x3f420fd8`
with 12 `m5::BMI270_Class` function symbols, and that image fails its size check
by 1,337 bytes. With `=0` the same tree links 1,733,504 bytes with both gone and
passes. The guard is 8,633 bytes there, and the absence of the blob is the
difference between a build and no build.

## Byte table

Every row is a `firmware.bin` from a build of that tree, and each row is one
build. Two clean builds of one tree differ by a few hundred bytes here, and by
as much as 1,264 on `m5stick-s3` between the gate run and the re-verification
build, which changes no conclusion because that lane has 1,281,552 bytes spare.

| Env | master `81139818` | PR head `5c09dd24` | Change |
| --- | ---: | ---: | ---: |
| `m5stack-core-debug` | 1,714,848 (`ESP_LOG_INFO`, blob) | **1,731,648** (`ESP_LOG_VERBOSE`, no blob) | +16,800 |
| `m5stack-core` | 1,627,632 (release, blob) | 1,618,496 (release, no blob) | -9,136 |
| `m5stack-core-usb-debug` | 1,740,592 (`ESP_LOG_INFO`, blob) | 1,756,448 (`ESP_LOG_VERBOSE`, no blob) | +15,856 |
| `m5stick-s3` | 1,862,480 | 1,862,480 | 0 |
| `m5stack-core2` | 1,912,976 | 1,912,976 | 0 |

The same five lanes, rebuilt after the rebase onto the master this branch now
sits on:

| Env | master `85b89931` | PR head, rebased | Slot | Margin |
| --- | ---: | ---: | ---: | ---: |
| `m5stack-core-debug` | 1,717,408 (`ESP_LOG_INFO`, blob) | **1,733,504** (`ESP_LOG_VERBOSE`, no blob) | 1,740,800 | **7,296** |
| `m5stack-core-debug`, guard removed | 1,717,408 | **1,742,137, build fails** | 1,740,800 | -1,337 |
| `m5stack-core` | not rebuilt | 1,620,240 (release, no blob) | 1,740,800 | 120,560 |
| `m5stack-core-usb-debug` | not rebuilt | 1,758,672 (`ESP_LOG_VERBOSE`, no blob) | 4,063,232 | 2,304,560 |
| `m5stick-s3` | not rebuilt | 1,864,176 (blob and probe kept) | 3,145,728 | 1,281,552 |
| `m5stack-core2` | not rebuilt | 1,914,688 (blob and probe kept) | 6,291,456 | 4,376,768 |

Master's other four lanes were not rebuilt on `85b89931`. The constrained lane
is the only one whose before-and-after decides anything, and it is the only lane
on this base where the difference can go wrong.

On `81139818`, `m5stick-s3` and `m5stack-core2` are byte-for-byte the same size
on the two trees, which is the strongest form of the claim that the guard does
not reach them. The two release-family lanes move in opposite directions and
that is correct: the Core release lane has no log maximum to restore, so it only
loses the blob, 9,136 bytes on that pair against the 9,120 the same-tree A/B
isolates on the debug lane, while the two debug lanes pay for the diagnostics
they get back.

The line that decides the merge is the third row of the current-base table. On
the master this branch now sits on, `m5stack-core-debug` at `ESP_LOG_VERBOSE`
with the guard builds at **1,733,504 of 1,740,800, 7,296 bytes of margin**, and
the same tree with the guard removed does not build at all: 1,742,137 bytes,
1,337 over, `checkprogsize` exit 1. The guard is therefore not a margin
improvement on this base, it is the difference between an image and no image,
and the margin the two blocked PRs land in is 7,296 bytes, against the 80 bytes
PR 488 needs and the 848 PR 493 needs.

On the older base the same lane ends at 1,731,648 of 1,740,800, 9,152 bytes, and
there the restoration alone did fit, at 320 bytes. The plan's original argument
was written against that 320 and no longer holds.

## Gates

Final numbers, all on the rebased head over `85b89931`.

| Gate | Result |
| --- | --- |
| `check_size_budget.py --env m5stack-core-debug` | `PASS m5stack-core-debug: flash 1,733,504/1,740,800, dram 56,212/57,344, iram 119,036/119,808` |
| `check_size_budget.py --env m5stack-core` | `PASS m5stack-core: flash 1,620,240/1,740,800, dram 55,148/56,320, iram 115,668/116,736` |
| `check_size_budget.py --env m5stack-core-usb-debug` | `PASS m5stack-core-usb-debug: flash 1,758,672/4,063,232, dram 56,268/57,344, iram 119,756/120,832` |
| `check_size_budget.py --env m5stick-s3` | `PASS m5stick-s3: flash 1,864,176/3,145,728, dram 47,496/48,128, iram 114,688/115,712` |
| `check_size_budget.py --env m5stack-core2` | `PASS m5stack-core2: flash 1,914,688/6,291,456, dram 56,768/57,344, iram 121,036/121,856` |
| `stack_budget.py`, all five envs | PASS, exit 0, every task under its bound, no new recursion or dynamic bound |
| `check_m5gfx_pinning.py --env <env> --require-source` | `PASS: M5GFX board pinning` on the Core debug, Core and USB lanes |
| `python3 -m unittest discover -s tests` | `Ran 636 tests`, `OK` |
| `python3 tools/pre_review.py --base origin/master` | `PASS: 13 gates` |
| `python3 tools/check_doc_citations.py` | `0 errors of 2891 citations in 161 files` |
| `sh sim/scripts/check-doc-tokens.sh` | `sim documentation tokens are present` |
| `patches/test_m5unified_bmi270.py` | 6 tests OK, wired into `reproducible.yml` |
| `tools/check_review_threads.py --repo MaxRink/furble --pr 524` | 0 review threads; the rebased head runs its own check set |

## What this review could not do

* The `m5stack-core-usb-debug` guard is not isolated by its own same-tree A/B.
  Its 9,104-byte figure in the plan is the plan's measurement; this review
  measured the guard at 9,120 on the constrained lane, which links the same two
  objects, and proved the USB lane's blob is gone and its log maximum is back.
* Reference hardware was not flashed, so the motion-wake claim rests on the
  linked symbols and on `arm()`'s unchanged chip-ID check rather than on a
  device. That is the same evidence the PR offers, and it is now checked on the
  StickS3, the one board this repo documents a BMI270 on.
