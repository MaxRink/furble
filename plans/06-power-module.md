# PR06: FurblePower pm-lock module

## Goal

Add a `FurblePower` module that owns the esp_pm configuration and counted
power-management locks. Replace the blunt `Platform::setSleep(false)` call in GPS
with a named `NO_LIGHT_SLEEP` lock. No behavior change.

## Scope

- New module `Furble::Power` with `esp_pm_configure()` ownership and RAII lock
  helpers for `ESP_PM_NO_LIGHT_SLEEP`, `ESP_PM_CPU_FREQ_MAX` and
  `ESP_PM_APB_FREQ_MAX`.
- Migrate the only existing sleep toggle (GPS on S3) to a named lock.
- All boards. The module compiles and runs everywhere because every sdkconfig has
  `CONFIG_PM_ENABLE=y`.
- Out of scope: changing CPU frequency values (PR01), enabling BLE sleep (PR07),
  GPS duty cycling (PR15).

## Files to change

| File | Anchor | Change |
|---|---|---|
| `include/FurblePower.h` | new | `Power` singleton, `LockType` enum, `Lock` RAII class |
| `src/FurblePower.cpp` | new | lock creation, counted acquire/release, `configure()` |
| `src/CMakeLists.txt` | 1-10 | add `FurblePower.cpp` to `furble_sources` |
| `src/CMakeLists.txt` | 13 | `PRIV_REQUIRES esp_pm` already present, no change |
| `src/FurblePlatform.cpp` | 14 | `instance.setSleep(true)` becomes `Power::init()` |
| `src/FurblePlatform.cpp` | 65-72 | `Platform::setSleep()` delegates to `Power` or is removed |
| `include/FurblePlatform.h` | 38-41 | `setSleep()` declaration |
| `include/FurblePlatform.h` | 46-47 | `CPU_MAX_FREQ_MHZ = 160`, `CPU_MIN_FREQ_MHZ = 40` move to `Power` |
| `src/FurbleGPS.cpp` | 120-123 | `setSleep(false)` becomes acquire of `NoLightSleep` lock |
| `src/FurbleGPS.cpp` | 130-132 | `setSleep(true)` becomes release of the same lock |

Verified current state:

- `src/FurblePlatform.cpp:65-72` builds an `esp_pm_config_t` with
  `max_freq_mhz = 160`, `min_freq_mhz = 40`, `light_sleep_enable = enable` and
  calls `esp_pm_configure()`.
- `src/FurblePlatform.cpp:14` calls `instance.setSleep(true)` before `M5.begin()`,
  so every board starts with automatic light sleep enabled.
- `src/FurbleGPS.cpp:120-123` calls `Platform::getInstance().setSleep(false)`
  under `#if defined(FURBLE_M5STICKS3)` with the comment
  "ESP32S3 UART does not function with light sleep".
- `src/FurbleGPS.cpp:130-132` calls `setSleep(true)` in `GPS::disable()`.
- `src/FurblePlatform.cpp:1` includes `esp_pm.h`. No other file in `src/`,
  `include/` or `lib/` uses `esp_pm` or `esp_light_sleep_start`.

## New settings

None.

## Menu placement

None. PR05 adds the Power-state debug page that reads this module.

## Implementation notes

- API sketch:

  ```
  enum class Power::LockType { NoLightSleep, CpuFreqMax, ApbFreqMax };
  Power &Power::getInstance();
  void Power::init();                       // esp_pm_configure once
  void Power::acquire(LockType, const char *owner);
  void Power::release(LockType, const char *owner);
  class Power::Lock { Lock(LockType, const char *owner); ~Lock(); };
  ```

- Create the three locks once with `esp_pm_lock_create()`. ESP-IDF locks are
  already recursive: a lock must be released as many times as it was acquired.
  Keep an explicit counter per lock type anyway so PR05 can dump it.
- `Power::init()` calls `esp_pm_configure()` with the same values used today
  (max 160, min 40, light sleep on). Behavior is identical after the change.
- GPS holds `NoLightSleep` while enabled. Keep the `FURBLE_M5STICKS3` guard so
  other boards keep their current behavior exactly. The guard stays because the
  workaround is S3 specific, not because the API is.
- Do not add `esp_light_sleep_start()` anywhere. Manual light sleep powers down
  the radio and drops BLE connections. Only automatic light sleep through esp_pm
  keeps a connection alive.
- Owner strings are for logging and the PR05 debug page only. Keep them static
  literals.
- Keep `Platform::setSleep()` as a thin wrapper in this PR if any call site is
  missed, then delete it in a follow up. Prefer deleting it here if the two GPS
  call sites are the only users, which the grep above confirms.

## Dependencies

- Follows PR01 (Power submenu, CPU frequency setting). PR01 is where the 160 MHz
  esp_pm value versus the 80 MHz sdkconfig default is resolved. PR06 must pick up
  whatever PR01 decided rather than reintroducing a hard coded 160.
- Blocks PR07, PR15, PR19.

## Risks

- Lock leak. An unbalanced acquire keeps the CPU awake forever and looks like a
  battery regression with no visible symptom. Mitigate with the RAII `Lock` class
  and the PR05 counter dump.
- Init order. `Power::init()` must run before any lock is acquired and before
  `M5.begin()`, matching the current call at `src/FurblePlatform.cpp:14`.
- `esp_pm_lock_create()` fails if `CONFIG_PM_ENABLE` is off. All five sdkconfigs
  set it, but check the return code and log instead of aborting.

## Verification

Build matrix, all five environments must build clean:

```
pio run -e m5stick-c -e m5stick-c-plus -e m5stack-core -e m5stack-core2 -e m5stick-s3
```

On device over USB:

1. `pio run -e m5stick-s3 -t upload`, then `pio device monitor`.
2. Boot with fresh NVS. Confirm no new log errors and no `esp_pm` assertion.
3. Toggle GPS on and off from Settings. Confirm the log shows the
   `NoLightSleep` lock acquired on enable and released on disable, and that the
   counter returns to zero.
4. With GPS on, confirm NMEA still decodes and the GPS icon reaches fix state.
   This proves the lock reproduces the old `setSleep(false)` behavior.
5. Repeat step 2 and 3 on one AXP192 board (StickC Plus) to confirm no lock is
   taken there and nothing regressed.

Battery drain, on-board instrumentation only, no external power meter:

- Run two 30 minute unplugged drain logs on M5StickS3, master versus this branch,
  in the same state (menu idle, GPS off). Log battery voltage and percent every
  30 s to the console buffer and dump after. The two curves must match within
  measurement noise. This PR must not change drain.

Camera testing:

- Only Fujifilm hardware is available. Connect one Fujifilm camera, confirm
  shutter still works. Nothing in this PR touches BLE, so other vendors are
  covered by code equivalence plus a FauxNY connect. State in the PR body that
  Sony, Nikon, Canon and Ricoh are untested on hardware.

## References

- [ESP-IDF power management API](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/system/power_management.html)
  defines `ESP_PM_CPU_FREQ_MAX`, `ESP_PM_APB_FREQ_MAX`, `ESP_PM_NO_LIGHT_SLEEP`,
  states that locks are recursive, and that automatic light sleep needs
  `CONFIG_FREERTOS_USE_TICKLESS_IDLE`.
- [ESP-IDF sleep modes](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/system/sleep_modes.html)
  distinguishes automatic light sleep through the power management API from a
  direct `esp_light_sleep_start()` call.
