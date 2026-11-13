# PR139: persistent wall-clock time

## Status

Implemented in the isolated `feat/persistent-time-policy` work tree. This plan
adds one shared policy for NTP, GPS, companion, RTC, and NVS time. It does not make
the monotonic timer a wall clock. Durations continue to use the monotonic tick.

## Hardware findings

M5Unified initializes an external calendar RTC only when it detects one on the
internal I2C bus. `M5.Rtc.isEnabled()` is therefore the runtime capability
boundary. The application reports both availability and whether the board is a
known battery-backed design.

- StickC and StickC-Plus expose a BM8563 RTC. The official StickC
  specification lists the BM8563 and the board battery; the RTC rail is
  supplied by the board PMU while that battery is present, see
  https://docs.m5stack.com/en/core/m5stickc.
- Core2 exposes a BM8563 and a dedicated rechargeable RTC cell, see
  https://docs.m5stack.com/en/core/Core2_v1.3?id=rtc.
- StickS3 uses M5PM1 power levels and an RTC wake timer, but the official
  product selector does not list a calendar RTC. M5PM1 keeps the RTC wake
  peripheral alive in low-power L1 operation. That wake timer is not a
  calendar clock and is not treated as a battery-backed calendar RTC by this
  feature, see
  https://docs.m5stack.com/en/arduino/m5sticks3/m5pm1?id=spk+amp and
  https://docs.m5stack.com/en/products_selector/m5stick_compare.
- ESP-IDF RTC fast/slow memory survives deep sleep only while the RTC power
  domain remains powered. It is not a substitute for a calendar RTC or NVS
  across a power loss, see
  https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/system/sleep_modes.html.

## Policy

`TimeKeeperPolicy` is dependency-free and is shared by firmware, host tests,
and the simulator build. A sample contains Unix microseconds, uncertainty,
source, and its monotonic observation point.

- NTP and GPS are authoritative and have equal priority.
- Companion time is accepted above RTC/NVS, with a wider uncertainty budget.
- RTC seeds a boot when the board exposes one and also seeds the ESP system
  wall clock.
- BM8563-equipped StickC, StickC-Plus, and Core2 boards retain calendar time
  across reboot and power loss through their backed RTC. StickS3 M5PM1 has no
  calendar RTC, so its NVS value is an explicitly stale fallback until NTP,
  GPS, or companion time corrects it.
- NVS is a CRC32 and versioned record. A restore adds one hour of explicit
  uncertainty because power-off duration is unknown. Records outside the
  seven-day uncertainty budget are rejected.
- Lower-priority sources cannot displace a stronger source. Large backward
  corrections are rejected unless a stronger source supplies them.
- NVS is written for the first valid source, a meaningful correction, or an
  age-qualified shutdown checkpoint when no battery-backed calendar RTC is
  available. An unbacked RTC remains useful across soft resets but does not
  suppress the power-loss checkpoint. Normal
  sync writes have a six-hour wear guard, which permits at most four logical
  background commits per day. Shutdown checkpoints have a three-and-a-half-hour
  guard, which keeps the combined hard bound to eight writes in every inclusive
  rolling 24-hour window. GPS service ticks do not write flash.
- Once running, uncertainty grows at a conservative 100 ppm bound for the
  monotonic oscillator, not at one millisecond per elapsed millisecond. A
  restored NVS record still carries the one-hour unknown-outage penalty, so it
  does not invent elapsed powered-off time. The seven-day uncertainty ceiling
  is a fail-closed validity bound.

RTC register writes use the void M5Unified setter and are rate-limited to one
attempt per five minutes during normal synchronization. An orderly shutdown
flush commits the final calendar value immediately. M5Unified's
`rtc_datetime_t` uses UTC calendar fields; the implementation converts with
`gmtime_r`, and the supported 2020-2100 policy range is inside its documented
1900-2099 calendar-year representation.

## Verification

`tests/host/time_keeper_policy_test.cpp` covers CRC corruption, invalid dates,
source priority, backward rejection, monotonic drift, reboot restore, and
write-rate decisions. The firmware hooks NTP callbacks, GPS UTC samples,
companion UTC samples, startup RTC/NVS restore, and graceful shutdown flush.
The GPS aiding cache no longer bypasses this arbitration by writing the system
clock directly.

The simulator source manifest includes the same policy implementation and the
full simulator build passes. The host persistence test exercises the production
`Preferences` wrapper and counts durable `nvs_commit` calls across simulated
restart and shutdown. The current fork master has no WiFi/NTP producer;
the NTP enum and arbitration path are ready for that existing follow-up. The
host policy test is the deterministic semantic
gate for virtual-clock drift, weaker-source rejection, backward-correction
rejection, persistence rate decisions, and cross-process reboot/power-cycle
semantics. A UI wall-clock page and stateful simulator reboot action remain
separate follow-up work because the current simulator has no wall-clock UI.

## Implementation state: wall clock across soft resets

Branch `fix/time-across-soft-reset` (research item C1 in the RTC domain survey).
No plan 320 exists on master, so this owning plan records the change.

- Verified defect. With `CONFIG_LIBC_TIME_SYSCALL_USE_RTC_HRT`, the ESP system
  clock keeps counting through every reset except power-on. On a board where
  M5Unified finds no calendar RTC chip, `TimeKeeper::init()` still restored
  the NVS checkpoint and called `settimeofday()` with it. A panic or OTA
  restart rewound a GPS-quality clock to a checkpoint up to hours old and
  added the one-hour penalty, and every later soft reset added it again.
- Change. A 28-byte copy of the encoded `TimeSample` record (same magic,
  version and CRC32 as NVS) lives in `RTC_NOINIT_ATTR` memory. It is rewritten
  in RAM whenever the accepted sample changes and is anchored where the system
  clock was set to it. It costs no NVS write. After `ESP_RST_SW`, `PANIC`,
  `INT_WDT`, `TASK_WDT`, `WDT`, `DEEPSLEEP`, `USB`, `JTAG` or `CPU_LOCKUP`,
  `TimeKeeperPolicy::restoreRetained()` accepts the running system clock with
  uncertainty grown by the elapsed system time at 100 ppm plus a fixed
  one-second hand-off bound (`SOFT_RESET_UNCERTAINTY_MS`, not measured). The
  source is kept because the clock never lost continuity. A clock behind the
  record, a corrupt record or a boot without time fail closed, and a boot
  without time clears the record.
- Power-on, brownout, power glitch, eFuse, SDIO, external pin and unknown
  resets keep the RTC chip or NVS path unchanged.
- Boards with an RTC chip are unchanged. `M5.begin()` calls
  `setSystemTimeFromRtc()` before `TimeKeeper::init()`, so the system clock
  there no longer carries the pre-reset value and the retained record is not
  used. Making that strictly better needs `cfg.internal_rtc` handling, which
  is out of scope.
- Plan 19 deep-sleep wake is a full power cut (M5PM1 or BM8563 power-on), so
  its reset reason is power-on and its behavior is unchanged. A future ESP
  deep sleep path (`ESP_RST_DEEPSLEEP`) would keep the clock.
- Simulator. `setSystemTime()` was already a no-op under `FURBLE_SIM` and a
  simulator reboot is a new process. Weak `furble_sim_system_time_us()` and
  `furble_sim_set_system_time_us()` hooks default to a clock that never
  validates, so simulator behavior is unchanged and no scenario is added.
- Evidence. `time_keeper_policy_test` covers restore arithmetic, every
  single-byte corruption, length, null, backward clock and the uncertainty
  ceiling. `time_keeper_persistence_test` runs every reset reason against a
  valid or lost clock with no RTC chip, an unbacked chip and a backed chip,
  plus a chained NVS-seeded case and the cleared record. Two mutations (drop
  the RTC chip gate, trust brownout) fail it.
- Hardware validation is deferred: that the record and system clock survive
  `esp_restart()`, a forced panic and the S3 USB Serial/JTAG port-open reset
  (`ESP_RST_USB`) on the StickS3. The one-second hand-off bound is unmeasured.
