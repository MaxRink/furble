# PR139: persistent wall-clock time

## Status

Implemented in the isolated `feat/persistent-time` work tree. This plan adds
one shared policy for NTP, GPS, companion, RTC, and NVS time. It does not make
the monotonic timer a wall clock. Durations continue to use the monotonic tick.

## Hardware findings

M5Unified initializes an external calendar RTC only when it detects one on the
internal I2C bus. `M5.Rtc.isEnabled()` is therefore the runtime capability
boundary. The application reports both availability and whether the board is a
known battery-backed design.

- StickC and StickC-Plus expose a BM8563 RTC. The official StickC
  specification lists the BM8563 and its battery, see
  https://docs.m5stack.com/en/core/m5stickc.
- Core2 exposes a BM8563 and a dedicated rechargeable RTC cell, see
  https://docs.m5stack.com/en/core/Core2_v1.3?id=rtc.
- StickS3 uses M5PM1 power levels and an RTC wake timer, but the official
  product selector does not list a calendar RTC. M5PM1 keeps the RTC wake
  peripheral alive in low-power L1 operation. It is not treated as a
  battery-backed calendar RTC by this feature, see
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
- NVS is a CRC32 and versioned record. A restore adds one hour of explicit
  uncertainty because power-off duration is unknown. Records outside the
  seven-day uncertainty budget are rejected.
- Lower-priority sources cannot displace a stronger source. Large backward
  corrections are rejected unless a stronger source supplies them.
- NVS is written for the first valid source, a meaningful correction, or an
  explicit graceful `flush()` during restart/power-off. Normal sync writes are
  separated by a five-minute wear guard; GPS service ticks do not write flash.

## Verification

`tests/host/time_keeper_policy_test.cpp` covers CRC corruption, invalid dates,
source priority, backward rejection, monotonic drift, reboot restore, and
write-rate decisions. The firmware hooks NTP callbacks, GPS UTC samples,
companion UTC samples, startup RTC/NVS restore, and graceful shutdown flush.
The GPS aiding cache no longer bypasses this arbitration by writing the system
clock directly.

The simulator source manifest includes the same policy implementation and the
full simulator build passes. The host policy test is the deterministic semantic
gate for virtual-clock drift, weaker-source rejection, backward-correction
rejection, persistence rate decisions, and cross-process reboot/power-cycle
semantics. A UI wall-clock page and stateful simulator reboot action remain
separate follow-up work because the current simulator has no wall-clock UI.
