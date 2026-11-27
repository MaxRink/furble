# 340 - GPS UART install failure

Status: implemented on branch `fix/gps-uart-install`, rebased onto fork master at
`81139818` on top of the #518 Core OTA log-level change and pushed for review.
Simulator evidence only; no hardware run is claimed. The independent review
record, including the publish-order and fake-error-code fixes, is
[340-review-feedback.md](340-review-feedback.md).

## Motivation

`GPS::getInstance()` starts the receiver once per boot. It called
`uart_driver_install()` and discarded the `esp_err_t`, then created the GPS task
and entered `xQueueReceive(m_Queue, ...)` in the task loop. When the install
fails there is no receive queue: `m_Queue` stays `NULL`, and the first
`xQueueReceive()` on it asserts on hardware, so the device panic-loops on every
boot with no way to reach the menu. The simulator's FreeRTOS shim returns
`pdFALSE` for a null queue instead of asserting, so the simulator showed a busy
loop on the disabled and running paths rather than a panic.

The install can fail for reasons the firmware cannot influence: internal RAM is
already held by WiFi, BLE, the display buffers or the WebUI, and
`uart_driver_install()` allocates the driver object, its ring buffers and its
event queue from `MALLOC_CAP_INTERNAL`, because every board sets
`CONFIG_UART_ISR_IN_IRAM` (esp_driver_uart `UART_MALLOC_CAPS`). It reports
`ESP_FAIL` when any of those allocations returns NULL.

## Design

- `getInstance()` keeps the result and logs it once with the ESP-IDF error name
  and the numeric code, `Failed to install gps uart: <name> (0x<code>)`, because
  release boards build without the esp_err name table (plan 106). On anything
  but `ESP_OK` it clears the queue handle, marks the instance, reports the
  receiver absent and returns before the port configuration calls and before
  `xTaskCreate()`. The existing task-create failure style is unchanged: that
  path still logs and `abort()`s.
- A new `std::atomic<bool> m_UartInstallFailed` is the one owner of the state.
  It also gates the retry: `getInstance()` is called from the UI, the console,
  the companion service and the simulator query path, and an unguarded retry
  would run a failed install on every call and log it each time.
- The receiver state is derived in `getReceiverState()`: a failed install reads
  `absent` for the whole session. That is the state the console's `gps` command,
  the GPS Data page receiver row and the simulator's `gps.receiver` query all
  read, so the UI and the console cannot disagree even though `disable()` stores
  `UNKNOWN` when the GPS setting is off.
- `enable()` returns before arming anything when the install failed: no baud
  selection, no configuration queue, no cycle deadlines, no power lock and no
  allocation. It reports the same absent state that the autobaud no-receiver
  path leaves behind, and leaves `m_Enabled` at its setting value the way that
  path does.
- `reset()` returns before `uart_flush()` and `xQueueReset()` when there is no
  queue. `xQueueReset()` on a null handle is the same class of assert as the
  receive.
- The absent path allocates nothing and touches no null queue: the install
  failure happens before the queue exists, `enable()` returns immediately, and
  the GPS task is never created.

Not changed: the `UART_SCLK_XTAL` selection and its `else` branch, the autobaud
ladder, the absent retry, the UBX/NMEA backend logic and the task-create abort.

## Consequences

- A boot with a failed install reaches the main menu, reports `receiver absent`
  and `baud 0`, holds no sleep lock and writes no crash record.
- `enabled` still follows the GPS setting, matching the no-receiver path where
  the receiver is absent while the setting is on.
- The cycle status word stays at its default (`cycle disabled`) because no cycle
  ever starts. The receiver row on the GPS Data page reports `absent` instead, so
  the reader never sees a cycle state that implies activity.
- The install is not retried in-session. Recovery needs a reboot, which is what
  the harness already does after a failed boot.
- The derived state also removes a false claim. `GPS_BAUD` defaults to a fixed
  9600, so `enable()` published `present` for a port that never installed. That
  is exactly what the pre-fix scenario run reports (`gps.receiver` `present`),
  and the receiver now reads `absent` on both baud paths.

## Simulator evidence

- `sim/fake_uart.cpp`: `uart_driver_install()` returns `ESP_FAIL`
  and writes no queue handle when the new `furble_sim_uart_set_install_failure()`
  flag is set. The flag is off by default, so every existing scenario is
  unchanged.
- `seed gps_uart_install ok|fail` is validated at the scenario boundary
  (`fail` closed on any other value) and applied before the UI is constructed,
  which is before the first `GPS::getInstance()` call.
- `sim/scenarios/e2e/gps-uart-install-failure.txt` seeds `gps true` and
  `gps_uart_install fail` and asserts:
  - `ui.page main` (the UI reaches the main menu),
  - `gps.enabled 1` and `gps.receiver absent`,
  - `gps.idle_wakes 0` and `gps.timeouts 0` (no GPS task exists to run),
  - `power.no_light_sleep 0` (the failed install holds no sleep lock),
  - `ui.gps_cycle absent` and `ui.gps_source none` on the GPS Data page,
  - `crash.pending no`, `crash.recovered no` and `crash.reset none` (no panic
    record was written),
  - `exit`, so the process exits 0.
- Registered in `sim/scenarios/manifest.json` as an `e2e` scenario owned by
  `sim-e2e`, certified on `m5stick-s3` and `m5stack-core` with the `gps`
  capability. The 80x160 StickC panel does not render the GPS Data detail rows,
  which is why the two boards that do are the certified matrix, the same split as
  `gps-no-receiver.txt`.

## Docs

- `docs/console-commands.md`: the `gps` status section records what a failed
  install reports and that nothing retries it.
- `docs/sim.md`: the `gps_uart_install` seed and its contract scenario.
- `sim/CLAUDE.md`: the GPS/UART boundary row and the fake UART seed list.

## Gates

The gate set below was rerun after the fourth rebase, onto `81139818`, the
commit that carries #511's citation gate and #518's Core OTA log level. Two
files conflicted: `plans/README.md`, where every master row is kept and the 340
rows sit after the 332 rows and before the 342 rows, and `sim/CLAUDE.md`, whose
GPS/UART row master rewrote for the shared EXT 5 V rail: the resolved row is
master's text plus the seed sentence. `src/FurbleGPS.cpp` merged without a
conflict, and `docs/sim.md`, `docs/console-commands.md` and
`sim/scenarios/manifest.json` keep every master row as well.

Host simulator builds (clang 21.0.0, `sim/build.sh`) for the two certified
panels: StickS3 135x240 and M5Stack Core 320x240.

`e2e/gps-uart-install-failure.txt`, final source, exit 0 on both panels. Every
assertion passes, and each log carries one `Failed to install gps uart:
ESP_FAIL (0xffffffff)` line. The fixed StickS3 log has no `gps`
`no_light_sleep` acquire line at all, so `power.no_light_sleep 0` is the
absence of the lock and not a released one.

The same scenario on the pre-fix source (only `src/FurbleGPS.cpp` and
`include/FurbleGPS.h` reverted, scenario and seed unchanged): exit 1 with
`ASSERT FAILED: gps.receiver expected 'absent' got 'present'`, so the scenario
fails closed on the defect rather than passing on both trees. That run also
records the second half of the defect at the same time:
`'gps' acquired 'no_light_sleep' power lock, held 1 times.` before the first
assertion, released only when the run tears down.

Neighbouring GPS scenarios on both panel binaries, all exit 0:
`gps-no-receiver`, `gps-fixed-baud-absent`, `gps-disabled-idle`, `gps-uart-ack`,
`gps-autobaud-ladder`, `gps-duty-no-fix`, `gps-receiver-detail`,
`gps-ubx-detect`.

`python3 tools/check_sim_scenarios.py`: `sim scenario manifest is complete`.

`sh sim/scripts/check-doc-tokens.sh`: `sim documentation tokens are present`.

`python3 tools/pre_review.py --base origin/master`: `PASS: 13 gates`
(whitespace, diff hygiene, settings contracts, clang-format on the 5 changed C
and C++ files, C++ safety, CI workflow contract, scenario manifest, hardware
coverage, coupling ratchet, datasheet citations, M5GFX board pinning and
documentation citations, plus the 213 scanner unit tests). The citation gate is
what caught the sixteen citations this branch's line additions made stale in
other docs; see [340-review-feedback.md](340-review-feedback.md).

## Remaining gate

No hardware run. A board check would force an install failure (for example by
exhausting internal RAM before GPS start) and confirm the menu and the
console `gps` row. The simulator covers the firmware decision path, not the
ESP-IDF allocator behaviour that triggers the failure.
