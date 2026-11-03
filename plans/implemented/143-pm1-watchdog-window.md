# 143 - M5PM1 watchdog recovery window

## Issue

The StickS3 M5PM1 watchdog was armed for 10 seconds, shorter than the planned
30 second OTA boot-health window. A healthy pending image could therefore reset
before rollback validation had a chance to finish. The short boundary also made
long flash or startup operations vulnerable to returning the device to the
10 second recovery cadence that caused the earlier PMIC deadlock investigation.

## Design

`include/FurbleWatchdog.h` now defines a 45,000 ms timeout and a 45 second PMIC
setting. The 45 second value leaves 15 seconds after the 30 second health
deadline for task scheduling, logging, and the rollback action. It is long
enough for the health contract but still bounds an unresponsive device far
below a minute. `PM1_FEED_PERIOD_MS` remains 1,000 ms, so normal feeding and the
three-feed safety margin are unchanged. No feed, unlock, or flash-preparation
recovery path was weakened.

The PMIC timeout is a retained external boundary. The simulator uses the same
shared second value and keeps the watchdog state and download recovery
availability across a modeled ESP reset. Equality remains expiry: a stall at
45,000 ms must reset, while a
stall at 44,999 ms must survive. Deadline arithmetic remains uint32-wrap-safe.

The serial uploader now classifies dependency, port-open,
missing-acknowledgement, and mid-handshake I/O failures separately. A system
Python without pyserial is guided to the pinned
`requirements.txt` setup, while an installed PlatformIO penv is searched for
its bundled pyserial without running an installer. Dependency and port
failures never print retained-lock recovery instructions. A dry run explicitly
reports that no upload was started. The canonical spelling is
`--preflight-only`; `--dry-run` remains a compatibility alias. Both aliases
always cancel a successful prepare before returning, and return zero only when
the cancel acknowledgements confirm an armed watchdog. Download recovery stays
available intentionally, including after cancel, so a wedged device can still
be recovered. A restoration failure returns nonzero with manual recovery
guidance.

The uploader deliberately retains PlatformIO's normal build dependency and
sets the required `FURBLE_VERSION=dev` and `FURBLE_TEST=0` defaults when the
caller has not supplied them. It does not offer a no-build upload shortcut:
`.pio/build` is checkout-local, so a no-build invocation can flash a stale
image whose embedded revision does not match the source being reviewed.
If PlatformIO is missing, cannot be executed, or exits unsuccessfully after a
successful preflight, the helper attempts `flash cancel` and reports whether
watchdog restoration succeeded. A failed automatic restoration leaves a manual
cancel procedure in the error message.

## Verification

- `tests/host/sim_clock_test.cpp` uses the shared timeout and checks a wrapped
  just-before deadline and exact-boundary expiry.
- `sim/scenarios/e2e/watchdog-feed.txt` checks ordinary feeding over 44 seconds;
  `watchdog-before.txt` checks a 44 second just-before stall and
  `watchdog-stall.txt` checks expiry beyond 45 seconds. The host clock test
  covers the exact millisecond boundary without relying on boot-task
  scheduling.
- `sim/scenarios/e2e/clock-wrap.txt` repeats the same boundary across uint32
  clock wrap; `clock-wrap-before.txt` covers the just-before side.
- `sim/scripts/run-watchdog.sh` invokes all five scenarios explicitly against
  the default M5StickS3 simulator binary; CI runs this gate after the broad e2e
  matrix so a stale or wrong-profile binary cannot hide a missing `stall`
  command or PMIC path.
- `tests/host/ota_state_machine_test.cpp` now requires the 45 second watchdog
  to outlive the 30 second validation window.
- Existing `tests/host/pmic_recovery_test.cpp` continues to cover first-access
  wake retry, retained watchdog state during ROM download, and flash recovery
  failure paths.
- `tests/test_flash_prepare.py` covers all 22 preflight and upload-cleanup
  cases, including missing pyserial, port-open failure, missing
  acknowledgements, mid-handshake I/O failure, close-path exceptions, and both
  successful and failed watchdog restoration after upload failure. It also
  covers successful, failed, and exceptional restoration for both
  preflight-only aliases, including early acknowledgement completion and
  timeout behavior. The main
  workflow runs this Python test suite so these cases cannot be skipped.

## Hardware boundary

This change must not be flashed autonomously. After root review, use the
developer-console preflight so the PMIC watchdog is explicitly disabled before
writing. Verify boot logs report `M5PM1 watchdog armed for 45 seconds`, leave
the board idle for more than 45 seconds only with a controlled reset plan, and
confirm `flash prepare` followed by a failed upload does not leave the download
lock set. A physical rescue remains: restore battery power, hold the side
button while plugging USB until the green LED flashes, then reflash a known
good image. Never test by disconnecting USB while relying on an armed PMIC
watchdog.
