# Power research: executive summary

Status: consolidation of the seven lane reports. Documentation only. No
firmware, defaults, model entries, or baselines change. No bench measurement was
made by any lane.

The matrix has 204 consolidated rows: 39 safe now, 76 needs measurement, and 89
rejected. The rows come from 188 raw lane IDs; overlapping items were merged.
Per lane: BLE 42, SoC 32, Display 25, GPS and IMU 30, Wi-Fi 40, Board 35.

The short version: the large wins are not radio micro-tuning. They are sleep
policy, display duty, and receiver duty. The mechanisms for the biggest one are
already merged and default off. Almost every number is modeled or inferred, and
the anchor measurement for the S3 floor has incomplete provenance.

## Top five opportunities

| ID | Change | Evidence | Estimated saving | Why it is top five |
| --- | --- | --- | --- | --- |
| P-040 | Default `SLEEP_CONN` on for the StickS3, or make Battery Saver the documented path | measured floor (different conditions) plus modeled states | About 40 mA connected with the screen off; 43.6 to 3.61 mA modeled; about 6 h to 69 h on the 250 mAh cell | Largest addressable number in the tree, mechanism already merged and hardware verified, one-line policy change plus a soak |
| P-071 and P-072 | Scope the display APB lock; on the S3 move the LEDC backlight to RC_FAST with `KEEP_ALIVE` | modeled plus code and datasheet | 5 to 25 mA screen on for the AXP192 boards; 10 to 30 mA inferred on the S3 | The only lever that cuts the MCU term while the screen is on, which is the common case. High flicker and corruption risk |
| P-073 | Default inactivity to 60 s and Screen off to Off, or make Battery Saver the default | modeled | About 40 to 64 mA after the timeout; up to 81.2 mA at the model's full-brightness basis | Turns the largest idle state into the smallest one with an existing mechanism. Product decision, not a mechanism gap |
| P-086 | Rework GPS duty: longer intervals with a cached-fix policy, shorter wake dwell, and add GPS power settings to Battery Saver | inferred plus modeled | About 1.5 mA today; up to about 19 mA of the 23 mA GPS budget after the rework | GPS is the second-largest always-on load after the display. The 15 s clamp and 5 s wake dwell eat most of the current duty window |
| P-001, P-002, P-045, P-046 | Bound the discovery scan, stop it on panel sleep, and cut the targeted reconnect scan to 50 percent duty and 10 to 15 s | inferred | Full scan receive load is on the order of the 93 mA S3 ceiling; targeted scan saving up to half the receive current; 6x less scan-on per failed attempt | Scan phases are the worst sustained BLE states, and these are the smallest, most reversible changes in the BLE lane |

Honorable mentions: P-076 auto-off (the whole idle current when the user forgets
the device), P-065 interval deep sleep (70 to 87 percent of interval energy, but
blocked on implementation and the plan 26 watchdog conflict), and P-068 Core
IP5306 boost keep-on (20 to 60 mA screen off, Core only).

## Top five measurements

| Measurement | State and boundary | Why it decides the most |
| --- | --- | --- |
| P09 | Connected, screen off, `SLEEP_CONN` on, StickS3, B1, USB detached, three runs | It either reproduces or replaces the 3.3 mA anchor. P-040 and P-041, and most battery-life claims, depend on it |
| P01, P02, D06, E14, E15 | Menu idle at brightness 128 and 240, CPU 160 and 80; brightness sweep; APB lock A/B; S3 RC_FAST A/B | It separates the MCU term from the backlight term. Without it, the 5 to 30 mA display claims cannot be ranked |
| G05 | GPS standby at 5/10/15/60 s, B2 for the unit and B1 for the board, with active and standby residency | In-circuit `$PCAS12` standby current is the uncertain term in every GPS duty, backoff, and motion row. If standby is above about 5 mA, those rows lose most of their value |
| P06, P07, P08, P09 | Connected idle at fast and saver intervals, screen on and off, three runs each | It fits current against 1/interval, which decides whether `CONN_SAVER`, the fast-floor change, and the slower idle profile are worth a default |
| E06 plus a camera advertising capture | Targeted reconnect with the camera off for 10 minutes; `bt scan 60 duplicates` per vendor in standby and reconnect | The camera advertising interval gates every scan-duty and scan-duration row. Without it, P-045, P-046 and P-047 are conditional |

## Rejected items

89 rows are rejected or recorded as no change. They fall into six classes:

| Class | Count | Representative IDs | Reason |
| --- | --- | --- | --- |
| Radio micro-tuning with no duty effect | 12 | P-118, P-119, P-120, P-122, P-123, P-124, P-132 | Latency, supervision timeout, MTU, CCCD count, geotag cadence, buffers, and advertising report flow control do not change receiver duty |
| Already-correct configuration | 15 | P-126, P-135, P-136, P-137, P-143, P-145 | Modem sleep mode, main XTAL, PM_SLP_IRAM_OPT, CPU power-down, and the 32 kHz crystal are already in their low-power or impossible state |
| Safety and robustness removals | 8 | P-139, P-142, P-186, P-188 | Removing watchdogs, brownout detection, the charging lock, or sleep pad holds buys microamps and risks a reset or a brick |
| Rail cuts that remove function | 9 | P-177, P-178, P-179, P-183 | Core2 LDO2, StickC LDO3, the SD slot, and the Plus2 divider have no separate switch or would remove the panel or card |
| Display ideas that trade UI for little or nothing | 13 | P-148, P-149, P-150, P-151, P-160 | Idle mode, 8-bit color, dark theme, larger buffers, and CABC either corrupt the UI or save less than 1 mA against a 35 mA backlight |
| Network features that are worse or out of scope | 12 | P-193, P-195, P-196, P-198, P-199, P-203 | SoftAP, DTIM control, a longer MQTT keepalive, removing TLS, `WIFI_PS_NONE`, channel pinning, and 5 GHz either regress availability or do not exist on this silicon |
| GPS protocol ideas with no energy case | 6 | P-162, P-163, P-164, P-165, P-166, P-167 | Custom parser, short rail cycling, PCAS00, SBAS/QZSS, character wake, and UBX are correctness or protocol mismatches |
| Capability and no-change notes | 6 | P-116, P-117, P-125, P-133, P-134, P-190 to P-192 | Duplicate filtering, match-first, 9 versus 6 connections, RPA handling, coex workarounds, and the developer console |

Two rejections are contested and are recorded rather than resolved: the 5 s PM1
watchdog feed (SoC wants a measurement, Board rejects it) and disabling the USB
Serial/JTAG console in release (SoC proposes it, Board says it removes the only
flashing path).

## Honest limits

1. **No lane measured its own claim.** The only local measurements quoted are
   earlier project observations: the 3.3 mA connected floor, 14 uA power off,
   107.7 s GPS cold re-fix, 710 to 91 characters per second after pruning, and
   the no-32 kHz-crystal boot log. Everything else is modeled, inferred, a
   datasheet, or a vendor publication.
2. **Only the StickS3 is on the bench.** StickC, Plus, Plus2, Core, Core2,
   Waveshare, and the headless devkit need hardware before their rows can leave
   `inferred`.
3. **Only Fujifilm has camera hardware.** Eight of the nine camera families are
   code-reviewed and FauxNY-tested only. Their power rows stay `unknown`. FauxNY
   cannot produce a family measurement.
4. **The simulator is a relative S3 model, not a measurement.** It has no scan
   state, no advertising charge, no brightness input, no wake cost, no PSRAM
   term, no PMIC term, and a fixed 0.0035 mA peripheral term. Its connected
   baseline still contains a 3.2989 mA radio term that the current code would not
   produce, and the 3.9 percent delta hides inside the 10 percent band.
5. **The 3.3 mA anchor has incomplete provenance.** It is tagged
   `measured-local`, but the plan 00 results table does not contain the drain run
   that produced it. Treat it as a reported observation until P09 reproduces it.
6. **The screen-off baseline is 0.510703 mA, not the 0.25 mA cited in plan 98
   and in three lanes.** Do not average or compare the two.
7. **The AXP192 operating floor is contested.** Plan 98 says about 2 mA; the
   Board lane's block sum is about 316 uA. The answer changes the value of the
   APB lock and AXP192 deep sleep rows by an order of magnitude.
8. **Plan 98 is partly stale.** The power gate workflow exists, Wi-Fi is
   initialized in `src/FurbleWiFi.cpp`, and the model's connected floor is now
   zero, not a constant. Use plan 98 as history, not as current status.
9. **The largest timelapse win is not implementable yet.** No `esp_deep_sleep`
   call exists, and the plan 26 PM1 watchdog conflict is unresolved. P-065 is
   blocked on code, not on measurement.
10. **The Core cannot hold a steady state below 45 mA.** Its screen-off behavior
    is an IP5306 cutoff after 32 s, not a firmware sleep state. A "Core sleep
    runtime" claim is not available.
11. **Some lane targets are below the instrument.** P-019's 0.05 mA target
    cannot be resolved on a 1 mA range. Use a dedicated low range or close it as
    unresolved, not as a small saving.
12. **Camera parity is out of scope.** The camera rows in the bench plan measure
    link power. They do not assert protocol parity, feature coverage, or vendor
    support. The camera parity PR stays independent.
13. **No errata or application note is cited by any lane.** The register records
    that gap rather than filling it. Several vendor documents were not opened:
    W5500 current, FT6336U, ES8311, AW8737, BM8563, and the board schematics.

## What this change set does and does not do

It consolidates the seven lanes into one ranked matrix, one bench plan, and one
document register. It records ten lane disagreements instead of resolving them
silently. It changes no default and quotes no number as measured that was not
measured.

It does not prove that any saving is real. It does not close a single row. It
does not replace the bench campaign.
