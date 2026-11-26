# 347 - Core OTA debug flash headroom

## Problem

`m5stack-core-debug` is a mandatory CI lane: `CLAUDE.md` says an image that
outgrows its OTA slot fails CI, and `tools/check_ci_workflows.py` requires the
profile in the discover matrix. It is a dual-OTA profile on a 4 MB part, so
`firmware.bin` must fit the 1700K app slot of `partitions_two_ota_large.csv`,
1,740,800 bytes.

On master `7caf0468` it measures 1,740,369 bytes: 431 bytes of headroom. A
firmware change of a few hundred bytes turns the lane red, and with the lane
mandatory every firmware PR is then blocked until someone finds the bytes
again. PR #493 (EXT_5V rail ownership) needed 848 and could not land.

## Cause

Plan 106 group A capped the compiled log maximum for every ESP-IDF component at
the runtime default, INFO (`CONFIG_LOG_MAXIMUM_LEVEL=3`): every DEBUG and
VERBOSE format string in every component was compiled into `.flash.rodata`
while never being printed. The debug envs then re-enabled full logging for
furble's own translation units through `-DLOG_LOCAL_LEVEL=ESP_LOG_VERBOSE`,
which is the one place the cap does not reach. `src/`, `lib/furble/` and
`lib/hal/` carry a large number of `ESP_LOGD` and `ESP_LOGV` sites, and
`esp_log_level_set()` can raise the runtime level to print them, so those
strings are live on every debug image.

On five of the six debug envs that rodata is free: their slots are 3 MB to 6 MB
wide or their images are far below the slot. `m5stack-core-debug` is the
exception. The 4 MB part keeps two 1700K OTA slots, the profile already merges
`build_type = debug` with `-Os -g2 -ggdb2`, and it is the one debug env that
cannot afford the stack protector's 24 KB (plan 314, PR 13). Its per-TU VERBOSE
strings are the remaining discretionary weight.

## Change

`[env:m5stack-core-debug]` compiles furble at `ESP_LOG_INFO` instead of
`ESP_LOG_VERBOSE`; `platformio.ini` records why. Nothing else in the profile
changes: `-Os -g2 -ggdb2`, `FURBLE_CONSOLE`, `FURBLE_TASK_WDT`,
`FURBLE_DEBUG_BUILD`, `sdkconfig.debug` (heap tracing and the end-of-stack
watchpoint), the partition table, the console commands and the stack and size
budget steps all stay as they are.

Measured with the pinned toolchain (`xtensa-esp-elf 14.2.0+20251107`),
`FURBLE_VERSION=dev FURBLE_TEST=0 pio run -e m5stack-core-debug`.

Back to back on base `7caf0468`, so only the flag differs:

| Tree | PlatformIO program size | Slack to 1,740,800 |
| :--- | ---: | ---: |
| master, `-DLOG_LOCAL_LEVEL=ESP_LOG_VERBOSE` | 1,740,369 | 431 |
| this change, `-DLOG_LOCAL_LEVEL=ESP_LOG_INFO` | 1,714,977 | 25,823 |

The change itself is 25,392 bytes of program size. On the rebased base
`88e33738` the gate that decides CI reports:

```
PASS m5stack-core-debug: flash 1,715,664/1,740,800, dram 55,948/57,344, iram 118,496/119,808
```

25,136 bytes of flash headroom, where master had 431 bytes of program slack and
no flash-bin slack at all. PlatformIO reports the summed program segments at
link time while `tools/check_size_budget.py` measures `firmware.bin`, which is a
few hundred bytes larger because it carries the segment headers and padding, so
the two figures are not interchangeable: the lane had 431 bytes by one measure
and was already at or past the wall by the other, which is why a firmware PR of
a few hundred bytes turned it red.

Two clean builds of one tree differ by tens of bytes because ESP-IDF archive
order moves longcall relaxation (plan 106 review, finding 3), so these are one
build each and the headroom, not a byte-exact delta, is the claim.

## Cost

On this one image, `ESP_LOGD` and `ESP_LOGV` calls in furble's own translation
units are compiled out, so `log <tag> <level>` in the console can no longer
raise them: the console still lists and sets levels, and every furble
`ESP_LOGI` and `ESP_LOGW` line, which is what the banner, battery, rail,
connection and error reporting use, is unchanged. The other five debug envs
keep `ESP_LOG_VERBOSE`, and the release images never had the strings at all
because the sdkconfig cap already dropped them.

This is the same trade plan 106 group A accepted for every component when it
capped `CONFIG_LOG_MAXIMUM_LEVEL`, and plan 314 accepted when it dropped the
stack protector from this profile rather than from the others. A debug image on
a 4 MB part cannot hold everything the larger parts can.

## Alternatives rejected

- **newlib nano formatting** (plan 106 item 3) frees an estimated 20 to 40 KB on
  every image, but it changes float and width formatting for GPS coordinates,
  battery percent, timers and version strings, and plan 106 requires a sim and
  hardware verification pass before it can land. Too large for the fix this
  lane needs, and unrelated to the PR it unblocks.
- **Feature gating** (plan 106 group C) frees much more, 66 KB for `FURBLE_SD`
  alone, but each gate is a user-visible capability change on the Core and
  needs its own PR, UI degradation work and docs.
- **Raising the slot.** `partitions_two_ota_large.csv` is ESP-IDF's 1700K
  two-OTA table, so the app partition cannot grow while keeping two OTA slots.
  The single-factory `partitions_single_factory_4m.csv` would fit, but the
  dual-OTA profile is the one that exercises update and rollback, which is why
  `m5stack-core-usb-debug` carries the factory layout and this profile does
  not.
- **Dropping an assertion or the task watchdog** would trade behaviour for
  bytes, and both are diagnostic value the lane exists for.

## Gates

| Gate | Result |
| :--- | :--- |
| `FURBLE_VERSION=dev FURBLE_TEST=0 pio run -e m5stack-core-debug` | SUCCESS, program size 1,714,977 of 1,740,800 bytes at `7caf0468`, `firmware.bin` 1,715,664 at the rebased `88e33738` |
| `python3 tools/stack_budget.py --env m5stack-core-debug` | every task PASS (`main` 7888, `control` 3568, `gps` 3664, `sd` 4704), no new recursion or dynamic bound |
| `python3 tools/check_size_budget.py --env m5stack-core-debug` | `PASS m5stack-core-debug: flash 1,715,664/1,740,800, dram 55,948/57,344, iram 118,496/119,808` |
| `python3 tools/pre_review.py --base origin/master` | `PASS: 10 gates` |
| `sh sim/scripts/check-doc-tokens.sh` | `sim documentation tokens are present` |
| `python3 -m unittest tests.test_check_ci_workflows tests.test_check_static_analysis tests.test_build_inventory` | pass; the profile is still discovered as a mandatory Core debug env |
| PlatformIO CI on this PR | `build (m5stack-core-debug)` and every other firmware job pass |

## Follow-ups

The lane has 25 KB of headroom after this instead of 431 bytes, which is the
point. Plan 106 item 3 (newlib nano) is the next structural saving if the
firmware needs more than that, and item 7 (`FURBLE_SD`) is the largest single
gate.
