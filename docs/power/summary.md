# Power research: executive summary

Status: consolidation of the seven lane reports, plus twenty-five implemented
safe-now changes. No bench measurement was made by any lane. The implemented
changes are bounded behavior fixes, not measured savings:
P-001 (60 s first-boot scan timeout), P-002 (stop the scan on panel sleep),
P-003 (50 percent initiator scan duty during connect), P-005 (peer requests do
not disable the saver unless faster than idle), P-009 (disabled GPS task
blocks), P-011 (fixed-baud silent receiver drops to absent), P-014 (no
flushes while the panel sleeps), P-007 (RTOS idle IRAM option), P-015 (battery
rows only on the visible page), P-021 (console mode turns the PM1 LED off),
P-016 (icon timer at 500 ms), P-030 (MQTT owner reloads on demand), P-024
(the Waveshare release drops the console), P-028 (wrong-passphrase and
no-AP stop), P-029 (no hidden-SSID join scan), P-033 (300 s transient retry
cap), P-034 (lazy Wi-Fi init), P-013 (250 ms camera-task idle), P-008 (hot-path logs
at DEBUG), P-032 (wired-node station gating), P-020 (20 ms PM1 poll), P-018 (staged
dim-to-off), P-010 (GPS deadline wait), P-012 (250 ms control tick), and P-037
(reconnect backoff default). Each has a test; see the matrix section
"Implemented safe-now changes".

The matrix has 201 rows: 39 safe now, 77 needs measurement, and 85 rejected. Per
lane: BLE 42, SoC 31, Display 25, GPS and IMU 30, Wi-Fi 40, Board 33. Overlapping
items were merged; no-change topics were kept as rejected rows.

No measurement in this branch is bench-measured. Section 1.1 of
`measurement-plan.md` separates the rows that need a current probe, a board, or
a camera from the rows that are actionable from the simulator and code alone.
Every milliamp figure in this directory is measured elsewhere, datasheet,
modeled, or inferred; none is local.

The large wins are not radio micro-tuning. They are sleep policy, display duty,
and receiver duty. The mechanism for the biggest one is already merged and
default off. Almost every number is modeled or inferred.

## What is closed and what still needs hardware

Closed without bench hardware: the opportunity matrix, the datasheet register
including per-board coverage in section 13, the lowest-power GPS profile in
`gps-profile.md`, the measurement plan, and twenty-five safe-now changes with
tests.

Still open: every current and energy number. That includes the board floor,
display sweep, radio current, GPS receiver current, PMIC rails, battery runtime,
and real camera states. It is a hardware requirement, not a documentation gap.
`measurement-plan.md` section 1.1 lists each group and the instrument it needs.

## Top five opportunities

| ID | Change | Evidence | Estimated saving | Why it is top five |
| --- | --- | --- | --- | --- |
| P-040 | Default `SLEEP_CONN` on, or make Battery Saver the documented path | measured floor plus modeled states | About 40 mA connected with the screen off; 43.6 to 3.61 mA modeled | Largest addressable number; mechanism merged; soak is the work |
| P-071, P-072 | Scope the display APB lock; on the S3 use RC_FAST LEDC with `KEEP_ALIVE` | modeled plus code and datasheet | 5 to 25 mA screen on for the AXP192 boards; 10 to 30 mA inferred on the S3 | The only lever that cuts the MCU term while the screen is on; high flicker risk |
| P-073 | Default inactivity to 60 s and Screen off to Off, or make Battery Saver the default | modeled | About 40 to 64 mA after the timeout; up to 81.2 mA at full brightness | Turns the largest idle state into the smallest; product decision |
| P-086 | GPS duty rework: longer intervals with a cached fix, shorter wake dwell | inferred plus modeled | About 1.5 mA today; up to about 19 mA of the 23 mA GPS budget | GPS is the second-largest always-on load; the clamp and dwell eat the duty |
| P-001, P-002, P-045, P-046 | Bound and stop the discovery scan; targeted reconnect at 50 percent duty and 10 to 15 s | inferred | Up to half the scan receive current; 6x less scan-on per failed attempt | Worst sustained BLE states; small and reversible |

Honorable mentions: P-076 auto-off, P-065 interval deep sleep (blocked on code),
and P-068 Core IP5306 boost keep-on.

## Top five measurements

| Measurement | State and boundary | Why it decides the most |
| --- | --- | --- |
| P09 | Connected, screen off, `SLEEP_CONN` on, StickS3, B1, USB detached, three runs | Reproduces or replaces the 3.3 mA anchor; P-040 and P-041 depend on it |
| P01, P02, D06, E14, E15 | Menu idle at 128 and 240 brightness, CPU 160 and 80; sweep; APB lock and RC_FAST A/B | Separates the MCU term from the backlight term |
| G05 | GPS standby at 5/10/15/60 s, B2 and B1, with active and standby residency | In-circuit `$PCAS12` standby is the uncertain term in every GPS policy |
| P06, P07, P08, P09 | Connected idle at fast and saver intervals, screen on and off, three runs | Fits current against 1/interval; decides `CONN_SAVER` and the interval rows |
| E06 plus a camera advertising capture | Targeted reconnect with the camera off; `bt scan 60 duplicates` per vendor | The advertising interval gates every scan-duty and scan-duration row |

## Rejected items

85 rows are rejected or recorded as no change:

- Radio micro-tuning with no duty effect: latency, supervision timeout, MTU,
  CCCD count, geotag cadence, NimBLE buffers, advertising flow control.
- Already correct or impossible: controller low-power config, the missing 32 kHz
  crystal, main XTAL, modem sleep mode, IRAM and CPU power-down, profiling.
- Safety or function removals: watchdogs, brownout, charging lock, sleep pad
  holds, Core2 LDO2, StickC LDO3, the SD slot, the Plus2 divider.
- UI or network trades that cost more than they save: ST7789 idle mode, 8-bit
  color, dark theme, CABC, SoftAP, a longer MQTT keepalive, removing TLS,
  `WIFI_PS_NONE`, channel pinning, 5 GHz.
- Protocol mismatches: short rail cycling, `$PCAS00`, SBAS or QZSS, character
  wake, UBX, a custom parser.

Two further items are contested and recorded rather than resolved: the 5 s PM1 watchdog
feed (P-077) and disabling the USB Serial/JTAG console in release (P-205).

## Honest limits

1. **No lane measured its own claim.** The only local values are earlier project
   observations (3.3 mA floor, 14 uA off, 107.7 s cold re-fix, 710 to 91
   characters/s) and vendor publications. Everything else is modeled or inferred.
2. **Only the StickS3 and Fujifilm hardware are on the bench.** Every other board
   row and eight camera families stay `unknown`. FauxNY cannot produce a family
   measurement.
3. **The simulator is a relative S3 model.** It has no scan state, brightness
   input, wake cost, PSRAM, PMIC, or peripheral term. Its connected baseline
   still contains a 3.2989 mA radio term that the current code would not produce.
4. **The 3.3 mA anchor has incomplete provenance.** It is tagged
   `measured-local`, but the plan 00 results table does not contain the drain run.
5. **Two numbers are contested.** The screen-off baseline is 0.510703 mA, not the
   0.25 mA cited in plan 98 and three lanes. The AXP192 floor is about 2 mA in
   plan 98 and about 316 uA by the Board lane's block sum.
6. **Plan 98 is partly stale, and the biggest code win is absent.** The CI gate
   exists, Wi-Fi is initialized, and the model's connected floor is zero. No
   `esp_deep_sleep` call exists, the plan 26 watchdog conflict is open, and the
   Core cannot hold a steady state below 45 mA.
7. **Camera parity is out of scope.** The camera rows measure link power, not
   protocol parity. No errata or application note is cited, and several vendor
   documents were not opened: W5500 current, FT6336U, ES8311, AW8737, BM8563, and
   the board schematics.

## What this change set does

It consolidates the seven lanes into one ranked matrix, one bench plan, and one
document register, and records twenty-three lane disagreements instead of resolving
them silently. It changes no default and proves no saving.
