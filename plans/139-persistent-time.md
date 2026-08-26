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
  explicit graceful `flush()` during restart/power-off. Normal sync writes are
  separated by a five-minute wear guard; GPS service ticks do not write flash.
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
full simulator build passes. The current fork master has no WiFi/NTP producer;
the NTP enum and arbitration path are ready for that existing follow-up. The
host policy test is the deterministic semantic
gate for virtual-clock drift, weaker-source rejection, backward-correction
rejection, persistence rate decisions, and cross-process reboot/power-cycle
semantics. A UI wall-clock page and stateful simulator reboot action remain
separate follow-up work because the current simulator has no wall-clock UI.
