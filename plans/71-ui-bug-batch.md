# UI bug batch

## Motivation

Hardware testing found several input and feedback failures in the LVGL UI:

- Approving or rejecting the companion pairing prompt could leave the encoder
  group focused on a deleted modal, making every button appear dead.
- Diagnostics and settings content could trap encoder focus away from the
  shared header back arrow.
- A long press on the left button had no page-independent escape path.
- The connection progress prompt could remain visible after the camera was
  already active.
- The StickS3 green indicator could flicker while the display was off at low
  battery.

This plan keeps the existing settings and wire protocol unchanged. The Level
quick wins are recorded below because the requested Level page is not present
on the target base branch.

## Design

### Companion pairing focus recovery

Capture `lv_group_get_focused(m_Group)` before creating the message box. All
close paths use one helper that asynchronously closes a valid dialog, restores
the captured object only when `lv_obj_is_valid()` accepts it, and clears both
members. The timer cancellation path and both approve and reject callbacks use
the helper.

### Shared diagnostics and settings focus

Use an idempotent group helper for every focusable control created by the
`addMenu` and `addSettingItem` families. The shared menu header back button is
also explicitly placed in the encoder group. Menu page entry and menu control
configuration clear encoder edit mode, so a roller or slider cannot make
left/right operate only on the current widget. This fixes the common focus
setup rather than adding page-specific exceptions.

### Universal left-button escape

Each raw button read callback checks the left indev before display wake and
before LVGL group processing. An 800 ms press is handled once, clears any
pending wake-input swallow state, wakes the display when needed, and sends a
click to the shared menu back button. The normal release is swallowed after a
handled long press. The blind-remote shutter and focus buttons remain on their
existing paths, while a long press on the left button wakes and navigates back.

### Connection progress

The connection timer runs every 50 ms, is made ready immediately when a
connection starts, and is paused as soon as `Control::STATE_ACTIVE` is seen.
The existing active transition closes the prompt and restores the menu, so the
next 50 ms observation is the only remaining UI polling delay.

### StickS3 display-off LED policy

The green StickS3 indicator is not the feedback light. The pinned M5Unified
source has no `Power_Class::setLed()` implementation for `board_M5StickS3`.
The feedback capability table also marks StickS3 light output unsupported.
The M5StickS3 schematic shows the green status LED driven by the PMIC
`LED_EN_PP` path through a transistor, while M5PM1 GPIO0 is a separate charge
status input. M5PM1 1.0.6 exposes bit 4 of `PWR_CFG` as the LED enable default
level and `setLedEnLevel()` writes that bit.

When the display sleeps, the platform now reads and saves that bit, sets the
PMIC LED enable level low, and restores the original value on wake. PMIC
accesses use the existing retry wrapper. Application-controlled feedback
lights are independently stopped and suppressed while the display is off.

Primary source references:

- https://raw.githubusercontent.com/m5stack/M5Unified/0.2.13/src/utility/Power_Class.cpp
- https://raw.githubusercontent.com/m5stack/M5PM1/1.0.6/src/M5PM1.h
- https://raw.githubusercontent.com/m5stack/M5PM1/1.0.6/src/M5PM1.cpp
- https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1207/K150_Stick_S3_PRJ_V0.6_20251111_2025_11_17_16_10_24.pdf

### Level quick wins scope

The requested Level overview icon and bubble update code are not in
`fork/master`. The IMU live page, Level page, bubble state, and their menu icon
are introduced by the separate `fork/feat/16-imu-spirit-level` branch, which
also adds `Settings::IMU`. Importing that feature would violate this batch's
base-branch and no-new-settings constraints. The Level icon and 50 ms bubble
audit are therefore deferred to the feature branch and no setting or wire ID
is added here.

## Verification

- Inspect every `lv_group_add_obj` call in `src/FurbleUI.cpp`; only the shared
  idempotent helper should add objects directly.
- Check pairing close paths, invalid-object guards, and left long-press
  handling by code review.
- Run clang-format 21 on all changed C++ and header files.
- Run the requested builds with `FURBLE_VERSION=dev FURBLE_TEST=0` for
  `m5stick-s3` and `m5stick-s3-debug`. If the first build hits the known
  TinyGPSPlus dependency quirk, retry it once.
- Hardware check: enter pairing from About and another nested page, approve
  and reject; traverse diagnostics and Bluetooth settings to the back arrow;
  repeat left long presses to reach the main menu; connect one camera and
  confirm the prompt disappears on active; sleep the StickS3 at low battery
  and confirm the green indicator is dark, then confirm its prior PMIC level
  returns after wake.

## Implementation state

The C++ changes are implemented in this worktree. The Level quick wins remain
deferred for the base-branch scope reason above.

## Verification results

- `clang-format` 21.1.2 completed on all changed C++ and header files.
- `FURBLE_VERSION=dev FURBLE_TEST=0 pio run -e m5stick-s3` reached the
  PlatformIO dependency step but was blocked by the host-owned
  `/Users/A92615428/.platformio/platforms.lock`. The writable-core retry was
  blocked while cloning TinyGPSPlus because this environment could not resolve
  `github.com`.
- `FURBLE_VERSION=dev FURBLE_TEST=0 pio run -e m5stick-s3-debug` hit the same
  host-owned PlatformIO core permission failure. Its writable-core retry was
  blocked by the same TinyGPSPlus GitHub resolution failure.
- No firmware build completed in this network-restricted environment, so
  hardware verification remains pending.
