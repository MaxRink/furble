# Board power opportunities

Status: research note. This document changes no firmware. It records what the
tree does today, what each board can still save, and what must be measured
before a saving is claimed. No new measurement was made for this document. The
only recorded local measurements are in `plans/00-hardware-experiments.md`. The
widely cited 3.3 mA StickS3 floor is tagged `measured-local` in the model, but
its drain row is not in that plan; section 3.5 explains the provenance gap.

This is the board and PMIC lane of `docs/power/`. It also carries cross-lane
rows where a board rail, PMIC mode, or GPIO changes the cost of a BLE, display,
or SoC decision. The coordinator merges those under their primary lane in
`opportunity-matrix.md`. Camera parity work is out of scope.

The short version, ranked by the size of the prize and not by readiness:

1. `SLEEP_CONN` off is worth about 40 mA whenever the StickS3 is connected with
   the screen off (model, S3 only). The mechanism is merged and defaults off.
2. Deep sleep between intervalometer frames is worth 70 to 87 percent of the
   interval energy on the StickS3 (model plus vendor off current).
3. The display `APB_FREQ_MAX` lock is held on every board while the panel is on.
   On the three AXP192 backlight boards it is not needed for the backlight and
   may cost 5 to 25 mA while the screen is on (inferred).
4. The M5Stack Core holds its IP5306 boost on for the whole session, so a load
   below 45 mA can no longer cut it. A 43 to 81 mA screen-off state can now be
   a few mA (inferred, floor unknown) without risking the old light-load cut.
5. GPS always-on costs 23 to 31.6 mA while enabled. The current 15 s duty clamp
   saves only about 1.5 mA in the model; a longer interval with a cached fix is
   the real win, and it is unimplemented.
6. Auto-off still defaults off, so 25 to 81 mA can run when the user forgets
   the device. The display defaults now shorten that window: fresh devices seed
   a 60 s inactivity timeout and screen off on every board, the Core included,
   because its IP5306 boost is held on for the session.

None of those numbers is a bench measurement on the named board except where
the text says `measured-local`.

## 1. Scope, sources, and evidence labels

### 1.1 What was read

- Build and board metadata: `boards/`, `platformio.ini`, `partitions_*.csv`,
  every `sdkconfig.*` file.
- Firmware: `src/FurblePower.cpp`, `src/FurblePlatform.cpp`,
  `src/FurbleGPS.cpp`, `src/FurbleUI.cpp`, `src/FurbleControl.cpp`,
  `src/FurbleSD.cpp`, `src/FurbleIMU.cpp`, `src/FurbleIR.cpp`,
  `src/FurbleFeedback.cpp`, `src/FurbleEthernet.cpp`, `src/FurbleSettings.cpp`,
  `src/FurbleConsole.cpp`, `src/main.cpp`, and the matching headers.
- Power model and tooling: `tools/power-model/board-currents.yaml`,
  `tools/power-model/README.md`, `sim/power_profiler.cpp`.
- Plans read in full or in the cited sections: `00`, `12`, `13`, `15`, `19`,
  `26`, `42`, `71`, `77`, `95`, `98`, `98b`, `111`, `143`. Plans `01`, `02`,
  `06`, `07`, `08`, `10`, `11`, `20`, `24`, `63`, and `125` are cited only
  through the audit, the plan index, or a grep; they were not read in full for
  this note.
- Pinned libraries, at the versions in `platformio.ini`: M5Unified 0.2.20,
  M5GFX 0.2.27, M5PM1 1.0.6.
- Datasheets opened for this document: AXP192, IP5306, M5PM1, ESP32-S3. The
  other component numbers are taken from the repo model, which tags them
  `datasheet` and carries the URL, and the datasheet table says which rows were
  re-opened.
- Display and touch documents re-opened on 2026-11-26 for section 7 only:
  ST7735S v1.1, ST7789V v1.3, ST7789P3 v0.1, ST7796S v1.0, ST7796U v1.0,
  ILI9342C V101, CO5300 V0.01 and V0.00, SH1106 V2.3 and CST9217 V1.0. Each row
  carries the page count and the hash. No vendor PDF is committed.

### 1.2 What was not available

- No board schematic PDF, netlist, or resistor value is present in this tree.
  The StickS3 schematic and the Waveshare schematic are referenced by URL only.
  Every claim that depends on a resistor value or a board net is labelled
  `unverified` and belongs in the measurement plan.
- No private hardware corpus or teardown note is present in this worktree. The
  hardware evidence available is the repo plans, the vendor documentation, and
  the pinned library source listed in section 9.
- The only board recorded as attached in this repo is the M5StickS3
  (`CLAUDE.md`). StickC, StickC Plus, Plus2, Core, Core2, Waveshare, and the
  headless devkit need hardware before any number for them can move from
  `inferred` to `measured-local`.
- No external power analyzer result is recorded in the tree. The simulator is
  not an analyzer. `power log` is an on-device gauge log, and it has its own
  limits, documented in section 6.
- The widely cited 3.3 mA StickS3 connected floor is tagged `measured-local`,
  but the plan 00 results table does not contain the drain run that produced
  it. The figure is repeated by plans 98 and 111 and by the model README. Treat
  the provenance as incomplete until the run is re-recorded.

### 1.3 Evidence labels

| Label | Meaning |
| :--- | :--- |
| `code` | Read directly in this tree at the cited line. |
| `vendor-doc` | Vendor product page, manual, or schematic note. Not a measurement. |
| `datasheet` | Primary component datasheet, at the revision in section 7. |
| `model` | Plan 63 simulator estimate. S3 only, pre-plan-158, and not calibrated. |
| `plan-estimate` | A number stated in a repo plan, not measured. |
| `measured-local` | Measured on this project's hardware and recorded in `plans/00-hardware-experiments.md`. The 3.3 mA StickS3 floor is the one exception: it is tagged this way in the model, but its drain row is not in plan 00. |
| `inferred` | Arithmetic from the labels above. No measurement. |
| `unverified` | No source in the tree, or a board fact that needs a schematic or a bench check. |

A label can be combined, for example `code + datasheet`. The label applies to
the sentence it is attached to, not to the whole item.

## 2. Board inventory

The release matrix is six environments, but the `m5stick-c-plus` image covers
two different power paths: the Plus has an AXP192, the Plus2 has no PMIC. The
`m5stack-core2` image also covers the M5Tough, which is untested and uses LDO3
for the backlight instead of the vibration motor. Treat the family rows below
as separate boards when a rail is involved.

| Build env | Boards covered | MCU | PMIC and charger | Gauge | Rails, storage, peripherals | Sleep and wake |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| `m5stick-c` | M5StickC | ESP32 | AXP192, 100 mA charge, 4.2 V | Voltage, current, charge state | DCDC1 3.35 V MCU, LDO2 backlight, LDO3 3.0 V LCD, EXTEN Grove 5 V, ST7735S, IR on G9, LED on G10, no SD | Light sleep 0.8 mA plus AXP192, deep sleep 10 uA plus AXP192, off 27 uA, no PMIC timed wake |
| `m5stick-c-plus` | M5StickC Plus and Plus2 | ESP32 | Plus: AXP192, 100 mA. Plus2: no PMIC, external charger | Plus: voltage, current, charge. Plus2: ADC voltage only | Plus: LDO2 backlight, LDO3 LCD, EXTEN. Plus2: PWM backlight G27, HOLD G4, battery divider G38, LED and IR G19, ST7789V, no SD | Plus as above. Plus2 light 0.8 mA plus divider, deep 10 uA plus divider, power off through HOLD |
| `m5stick-s3` | M5StickS3 | ESP32-S3-PICO-1-N8R8, 8 MB quad flash and 8 MB octal PSRAM, so SPICS1 and GPIO33 to GPIO37 are PSRAM pins | M5PM1, charger on by default, profile not documented | Voltage and charge state on PM1 GPIO0, no current | 3.3 V DCDC and LDO, 5 V boost for Grove and EXT_5V and IR, L3B on PM1 GPIO2, ST7789P3 backlight G38, BMI270, ES8311 and AW8737, IR TX G46 and RX G42, no SD, USB Serial/JTAG | Light sleep 240 uA plus 140 uA PSRAM plus PM1, deep sleep 7 uA plus PM1, off 14.02 uA, L1 52.47 uA, L2 102.40 uA, PM1 timer wake |
| `m5stack-core` | M5Stack Core Basic and Gray | ESP32 | IP5306, 150 mA charge, 4.2 V | Four-level only, 100, 75, 50, 25, 0 percent | 3.3 V rail, backlight G32, ILI9342C, speaker, SD; IMU on the Gray variant, part not recorded in this tree | IP5306 boost held on for the running session, so no light-load cut while awake; the manual Off clears that bit and deep sleeps, and the light-load cut then removes the boost after 64 s. No shutdown command, so the Off is deferred rather than immediate |
| `m5stack-core2` | Core2 and M5Tough (untested) | ESP32 | AXP192, 360 mA charge on Core2 (M5Unified target; vendor page 0.219 A) | Voltage, current, charge state | DCDC3 backlight, LDO2 3.3 V LCD and SD, LDO3 vibration on Core2 and backlight on Tough, AXP192 GPIO0 enables one BUS_5V net for Port A and the M-Bus, ILI9342C, FT6336U touch, MPU6886, NS4168, SD, BM8563 (Tough has BT1 backup; Core2 2023.2 removed it) | Light sleep 0.8 mA plus AXP192 plus touch, deep sleep 10 uA plus AXP192, off 27 uA, touch wake on G39, Tough RTC alarm wake |
| `waveshare-s3-eth` | Waveshare ESP32-S3-ETH | ESP32-S3R8, 8 MB octal PSRAM on GPIO33 to GPIO37 | None, no battery | None | USB-C 5 V or PoE HAT, W5500 on SPI, WS2812 on G21, microSD in 1-line mode and unused by furble, no display or IMU; GPS only through the Pico header | Tickless idle off, so no automatic light sleep; DFS is left on; mains powered |
| `esp32-s3-headless` | ESP32-S3-DevKitC-1 | ESP32-S3, 8 MB PSRAM in sdkconfig | None, no battery | None | USB or 5 V, USB Serial/JTAG, no display, PMIC, or RTC | Tickless idle on, light sleep 240 uA plus PSRAM, mains or development |
| `m5stack-core-usb-debug` | Core, developer only | ESP32 | IP5306 | Four-level | USB-UART console, single factory partition, PM profiling, LVGL sysmon and perf monitor | UART console holds `APB_FREQ_MAX`, so no light sleep; not a power target |

### 2.1 Board notes and opportunity coverage

**M5StickC (`m5stick-c`).** AXP192 with three enabled rails in normal use:
DCDC1 for the ESP32, LDO2 for the backlight, LDO3 at 3.0 V for the LCD. DCDC3
is set to 0 by M5Unified. The AXP192 has no whole-chip operating current in its
datasheet, only per-block quiescent currents. The ESP32 build selects the
internal RC RTC clock (`CONFIG_ESP32_RTC_CLK_SRC_INT_RC=y`), not an external
32.768 kHz crystal, so the connected BLE floor is modem sleep and the 1.9 mA
crystal mode is not available. The panel is an ST7735S at 80x160; its sleep-in
current is 15 uA typical. The board has no SD slot, so SD power is not a term.
Opportunities: PM-04, PM-07, PM-10, PL-03, PL-05, PL-06, PL-07, PL-08, LK-02,
RL-11. No hardware is attached in this repo.

**M5StickC Plus and Plus2 (`m5stick-c-plus`).** These are two boards in one
image. The Plus is an AXP192 board with a 1.14 inch ST7789V and a buzzer. The
Plus2 has no PMIC: the battery is read from an ADC divider on GPIO38, the
backlight is PWM on GPIO27, and the power hold is GPIO4. The Plus2 charger part
is not in the tree; charge current and termination are not firmware-controlled.
The ADC divider is permanently connected. On the Plus2,
`M5.Power.powerOff()` reaches `esp_deep_sleep_start()` and M5Unified 0.2.20 does
not arm a wake source on that path, so the "off" behavior of the shared image
needs a hardware check before auto-off is enabled there. Opportunities: PM-04,
PM-07, PM-10, PM-11, PL-03, PL-05, PL-06, PL-07, PL-08, LK-02, RL-10, RL-11. No
hardware is attached.

**M5StickS3 (`m5stick-s3`).** The best instrumented board and the only one with
a GPS burst lock and a PM1 timer wake. It is also the only board with a
vendor-published 14.02 uA off figure and a PM1 shutdown path. The M5PM1 also
powers the 5 V boost that feeds Grove, EXT_5V, and the IR transmitter and
receiver. That coupling is the reason the IR feature depends on the GPS rail
setting today. The BMI270 is read at 1 Hz for gestures and 50 Hz on the level
page; the gyro is never used by furble. The 8 MB octal PSRAM adds about 140 uA
to light sleep per the ESP32-S3 datasheet, which the S3 energy model does not
charge. Octal mode is also what takes pins: on this SiP, SPICS1 and GPIO33 to
GPIO37 are connected to the Octal SPI PSRAM and are not available for other
uses (ESP32-S3-PICO-1 Series Datasheet v1.2, Table 1-1 p. 9 and pin table
note 4 p. 13; ESP32-S3 Series Datasheet v2.2, 2.3.5 p. 26 and Table 2-14
with its notice, p. 31). Furble uses none of them: the IR transmitter is
GPIO46 and the motion wake lands on GPIO13 (`src/FurbleBoard.cpp` StickS3
row), and the GPS UART takes the Port A pins through
`Board::gpsPins()` (`M5.getPin(m5::port_a_pin1)` and `port_a_pin2`,
`src/FurbleBoard.cpp:180-181`).
Opportunities: PM-01, PM-02, PM-03, PM-05, PM-07, RL-01, RL-02, RL-03,
RL-11, LK-01, PL-01, PL-02, PL-04, PL-05, PL-06, PL-07, PL-08, NB-03. This is
the only board on the bench.

**M5Stack Core (`m5stack-core`).** The IP5306 is the defining constraint. M5Unified
writes `SYS_CTL2 = 0x6C` (the 64 s light-load window) and `SYS_CTL0 = 0x31`,
which clears bit 1, the "BOOST output normally open" bit. Furble sets that bit
at boot and reads it back, and keeps it set for the session, so a load below
45 mA no longer removes the boost while the device is awake: display-off is a
low-current state rather than a power cut (plan 354). The manual Off is the
exception and its whole point: `M5.Power.powerOff()` sleeps the panel, clears
that bit and deep sleeps, so the light-load cut removes the boost 64 s later.
The IP5306 has no shutdown command, so the Off is deferred, not immediate. The
nominal cell is 110 mAh. The four-level gauge cannot
support a fine-grained percentage policy. The Gray variant may carry an IMU;
the part is not recorded in this tree, so RL-11 covers the StickS3 BMI270 and
the StickC, Plus, and Core2 MPU6886 only. Opportunities: PM-06 (implemented),
PM-07, PM-09, PL-03, PL-05, PL-06, PL-07, PL-08, RL-06. No hardware is
attached.

**M5Stack Core2 (`m5stack-core2`).** An AXP192 board with DCDC3 as the
backlight, LDO2 at 3.3 V for the LCD and SD, LDO3 for the vibration motor, and
AXP192 GPIO0 as the enable for one BUS_5V boost that feeds both Grove Port A and
the M-Bus. The touch controller is deliberately left running so a
tap can wake the screen. The MPU6886 is initialized when the IMU setting is on;
furble only reads acceleration, so the gyro current is likely wasted. The NS4168 amp
is shut down until a tone is due. Opportunities: PM-04, PM-07, PM-08, PM-10,
PL-03, PL-05, PL-06, PL-07, PL-08, RL-06, RL-11. Tough shares the image but is
untested and uses LDO3 for the backlight, so do not apply the Core2 LDO3
assumption to Tough.

**Waveshare ESP32-S3-ETH (`waveshare-s3-eth`).** There is no battery and no
PMIC. Every "saving" on this board is mains input current or heat, not
runtime. `CONFIG_FREERTOS_USE_TICKLESS_IDLE` is off, so it never enters
automatic light sleep; DFS is still enabled by the runtime `esp_pm_configure`
call, so the "hold a fixed frequency" sentence in plan 42 is not what the
build does. The W5500, the WS2812, and the unused microSD slot are the loads.
The module is the ESP32-S3R8, whose 8 MB PSRAM is octal, so GPIO33 to GPIO37
carry DQ4 to DQ7 and DQS/DM and must not be used for anything else (Waveshare
product page; ESP32-S3 Series Datasheet v2.2, Table 1-1 p. 13 and Table 2-14
with its notice, p. 31). The slot only matters if a card is left in it.
Opportunities: NB-01, NB-02, NB-06. No hardware is attached.

**ESP32-S3 headless (`esp32-s3-headless`).** A development board with no
battery, no PMIC, and no display. Tickless idle is on and the 5 ms UI loop is
the only housekeeping limit. It is useful as a low-cost S3 power test bed, but
it has no PMIC, no PSRAM-backed display, and no battery, so its numbers do not
transfer to the StickS3. Opportunities: NB-03, NB-05, NB-06. No hardware is
attached.

**Core USB debug (`m5stack-core-usb-debug`).** A developer rescue profile, not
a release board. It has one factory partition, so no OTA. Its UART console holds
`ESP_PM_APB_FREQ_MAX` for the life of the console, which blocks light sleep
entirely. The debug fragment also turns on `CONFIG_PM_PROFILING`, FreeRTOS run
time stats, and the LVGL sysmon and perf monitor. Never use it for a power
measurement and never quote its current as a release number. Opportunity: none.
See NB-04.

## 3. How power is controlled today

### 3.1 PMIC, charger, and gauge state at boot

**AXP192 (StickC, Plus, Core2, Tough).** M5Unified writes a fixed register block
at `M5.begin()`: DCDC1 3.35 V, VBUS-IPSOUT pass-through, VOFF 3.0 V, battery
monitor on, charge 4.2 V at 100 mA, RTC LDO backup, IRQ masks, ADC all on
(`0x82 = 0xFF`), temperature ADC on (`0x83 = 0x80`), ADC rate 25 Hz
(`0x84 = 0x32`). The board switch then sets StickC and Plus to DCDC3 off and
LDO3 3.0 V for the LCD, Core2 to LDO2 3.3 V, LDO3 0 V, speaker off, and a
`setChargeCurrent(390)` call that selects the AXP192's 360 mA step (register
`0x33` low nibble 3), and Tough to LDO2 3.3 V, speaker off, DCDC3 off. The
register-level detail in this section is what M5Unified and M5GFX write, read
back from the pinned library sources; the AXP192 datasheet copy was not re-opened
in the plan 350 pass, and the StickC and Plus schematics were not available, so
the bit assignment beyond those writes is `unverified`. The AXP192
datasheet gives off mode 27 uA, suspend 86 uA, DCDC1 26 uA no load, DCDC2 and
DCDC3 20 uA each, LDO2 and LDO3 100 uA each, and LDOIO0 90 uA. There is no
whole-chip operating figure. The "about 2 mA AXP192 floor" in plan 98 is a
plan estimate, not a datasheet line; the block sum with DCDC1, LDO2, LDO3, and
LDOIO0 enabled is about 316 uA before any load.

**IP5306 (Core).** M5Unified writes `0x00 = 0b00110001`, `0x01 = 0b00011101`,
`0x02 = 0b01101100`, `0x20 = 0x00`, `0x21 = 0x09`, `0x22 = 0x02` (4.2 V plus
28 mV boost), `0x23 = 0xAE`, `0x24 = 0b11000001` (150 mA charge). Bit 1 of
`0x00`, "BOOST output normally open", starts clear; furble sets it at boot and
reads it back (plan 354). In `0x02`, bits 3:2 = 11 select the 64 s light-load
window, not 32 s, per the Injoinic IP5306 register document V1.21 (revision
V1.4), SYS_CTL2. In `0x21`, bits 7:6 = 00 set the battery-side stop-charge
current threshold to 200 mA, above the 150 mA charge current. The register
document's lowest option is 200 mA, so the current-based full detection cannot
trip and only the voltage check stops the charge; plan 354 records this as an
open follow-up rather than changing the charge profile. The datasheet
specifies 50 uA standby, 100 uA input quiescent, and a 45 mA light-load
threshold. The board has no current gauge, only the four-level fuel display.

**M5PM1 (StickS3).** The PM1 manual gives `PWR_CFG` default `0x17`: LED enable
on, 5 V boost off, 3.3 V LDO on, 3.3 V DCDC on, charge on (M5PM1 Chip User
Manual V1.9, page 13). M5Unified writes `I2C_CFG = 0x00` at begin, and the
M5PM1 library requests the same field with `SLP_TO = 0` plus the speed bit. If
both writes succeed, PM1 I2C idle sleep is disabled, because `SLEEP[3:0]` of 0
disables it (manual, section 6 I2C Idle Sleep, page 27, and the same field's
note on page 15). Idle sleep is also invalid while PWM is enabled and while
entering Download mode (page 28), so turning it on later is not a free change.
A failed write would leave the retained value, which is why plan 26 owes a
readback. The vendor rule behind the retry wrapper is explicit: after an
idle-sleep wake the first I2C transaction only wakes the PMIC and fails, and
the next transaction is the first valid one (manual, `I2C_CFG` note 2, page
15; M5StickS3 low-power page), and a wake that does not receive a complete
address within 300 ms puts the part back to sleep (same note). Furble does
retry: `m5pm1Access()` runs an access twice before it counts a failure
(`include/FurblePlatform.h:370-394`), and `wakeM5PM1()` spends a discarded
voltage read so the retry is the real one (`src/FurblePlatform.cpp:278-283`).
The PM1 button poll and the three `begin()` calls reach the library directly,
so a failed button read costs one 20 ms poll, not a lost press. M5Unified also
clears the PM1 watchdog count at begin. Furble later arms the watchdog for 45
s and feeds it every 1 s.

**Chargers.** There is no runtime charge control in furble. The AXP192 boards
use the M5Unified charge current, 100 mA on StickC and Plus, and the Core2 call
is `setChargeCurrent(390)`, which the AXP192 table turns into register `0x33`
low nibble 3, 360 mA; the vendor page gives a 0.219 A charging current, and no
AXP192 step matches it exactly, so plan 354 records the difference as a
follow-up. The Core uses 150 mA with a 200 mA end-of-charge threshold. The PM1 uses its default profile; M5Unified's
`M5PM1_Class::setChargeCurrent` and `setChargeVoltage` are stubs that return
false, and the public manual does not document a charge-current register. The
Plus2 has no PMIC and no firmware charge control. This matters for measurement:
battery-side current includes charge current unless the charger is disabled at
the PMIC.

**Gauges and ADC cost.** The AXP192 has a 12-bit ADC for battery voltage,
battery current, ACIN, VBUS, APS, and temperature. M5Unified enables every
channel, and furble reads level, voltage, and current every 5 s. The AXP192 ADC
itself runs continuously at 25 Hz once enabled, which is already the lowest
sample rate in `REG 84H`. The ESP32-S3 boards have no ESP32 ADC battery path;
the PM1 reports voltage. The Plus2 is the only board that uses an ESP32 ADC
divider, on GPIO38 with a 2.0 ratio. That divider is permanently connected and
is a fixed leakage term, not a conversion cost. On the Plus2 the ESP32 ADC
conversion is on demand every 5 s and is negligible; the divider current is the
cost. On the AXP192 boards the cost is the divider-free ADC block, not the
sample rate.

**Battery capacities known to firmware.** StickC 95 mAh, Plus 120 mAh, Plus2
200 mAh, StickS3 250 mAh, Core2 500 mAh, Core 110 mAh, Tough 390 mAh. These are
vendor nominal figures: the Core page gives 110 mAh @ 3.7 V and the Core2 page
changed from 390 mAh to 500 mAh in 2023.10. The firmware deliberately reports no
runtime estimate where the capacity or current is unknown; a capacity alone is
not a runtime. Do not invent one in a power document.

### 3.2 Rails and what can be switched

| Board | Rail | Load | Switched by firmware? |
| :--- | :--- | :--- | :--- |
| StickC, Plus | DCDC1 3.35 V | ESP32 | No, always on while running |
| StickC, Plus | LDO2 1.8 to 3.3 V | LCD backlight | Yes, M5GFX writes the LDO2 voltage; 0 disables the rail |
| StickC, Plus | LDO3 3.0 V | LCD logic | No, stays on while the panel is powered |
| StickC, Plus | EXTEN | Grove 5 V (UNVERIFIED) | Yes, `M5.Power.setExtOutput`; already on after `M5.begin()`, see below |
| Core | 3.3 V boost output | ESP32, LCD, SD, speaker | No, the IP5306 boost is the whole rail |
| Core2 | DCDC3 | LCD backlight | Yes, M5GFX voltage dimming |
| Core2 | LDO2 3.3 V | LCD and SD | No, stays on |
| Core2 | LDO3 | Vibration motor on Core2 | Yes, `M5.Power.setVibration` |
| Core2, Tough | AXP192 GPIO0 | One BUS_5V boost feeding Grove Port A and the M-Bus | Yes, `M5.Power.setExtOutput`; the request is advisory and the PMIC can refuse an enable on a low battery with VBUS current and no ACIN |
| StickS3 | 3.3 V DCDC and LDO | ESP32, panel, PM1 | No, PM1 default on |
| StickS3 | 5 V boost | Grove, EXT_5V, IR TX and RX | Yes, `M5.Power.setExtOutput` through PM1 `PWR_CFG` bit 3 |
| StickS3 | L3B | Hat rail, PM1 GPIO2 | Not touched by furble |
| All Core boards | SD slot | microSD | No load switch; the card stays powered when inserted |
| Waveshare | microSD, WS2812, W5500 | Unused slot, status LED, Ethernet | SD slot unused; WS2812 off; W5500 started at boot |

**EXTEN is already on at boot on the StickC and Plus.** Two writes during
`M5.begin()` reach it: M5Unified 0.2.20 calls `Power.setExtOutput(cfg.output_power)`
with the default `output_power = true` (`M5Unified.hpp`, `M5Unified.cpp`), which
reaches `Axp192.setEXTEN(true)` on that board pair, and M5GFX 0.2.27
`Light_M5StickC::init()` writes AXP192 `0x12 = 0x4D`. Bit 6 of that register is
EXTEN per `AXP192_Class::setEXTEN()`. So the Grove 5 V rail is live before the
GPS driver runs, `setExtOutput(true)` is normally a no-op there, and a GPS unit
on Port A is powered from boot even when GPS is disabled in settings. The
EXTEN-to-Grove-5V routing itself stays `unverified`: the StickC and Plus
schematics were not available in this pass, and neither were the AXP192 register
details beyond the values M5Unified writes (plan 350).

Two consequences follow. First, the only true peripheral power controls in
furble are the Grove 5 V rail and the panel backlight. Everything else is
either always on or has no load switch. Second, "turn off the SD card" is not
available in firmware on either Core board; unmounting stops transactions but
does not remove power.

**GPS and IMU rails.** The GPS unit is on Grove Port A and is powered from the
board's 5 V rail. On the Core2 and Tough that 5 V is one BUS_5V net shared by
Port A and the M-Bus (Tough schematic: SY7088 U8 output, J4 Port A pin 2 and J7
M-Bus pin 1), so `Rail::GPS` is the whole external 5 V net and a GPS power
cycle also drops anything else on either connector. The enable is a request:
M5Unified refuses it when there is no ACIN, VBUS current is present and the
battery is at or below 8 percent, and `Platform::setRail()` returns the final
readback so the GPS driver restarts a receiver it had stopped. The IMU is on
the board 3.3 V rail with no separate switch on any supported board. The IMU can be put into a low-power mode over its own
register interface, but the rail cannot be cut.

### 3.3 GPIO, pull-ups, and leakage

The relevant pull-ups are:

| Board | Pin | Pull | Why it matters |
| :--- | :--- | :--- | :--- |
| StickC, Plus | GPIO35 | External only, input-only pin | Shared net with the MPU6886 INT, the BM8563 INT and the AXP192 IRQ output. Active low. Current depends on the external resistor, which is not in the tree. |
| StickC, Plus, Plus2, Core, Core2, S3 | I2C SDA and SCL | Board pull-ups | Zero steady current when idle. Only the transaction current matters. |
| StickS3 | GPIO13 | Internal pull-up, 45 kOhm typical | PM1 aggregated IRQ, active low. About 73 uA while the line is held low. |
| StickS3 | GPIO46 | IR TX, driven only when the feature is used | Floating when IR is off. No measured leakage. |
| Core, Core2, Waveshare | SD CS | Driven by the SPI driver | Floating CS can let the card drive MISO. Not measured. |

The ESP32-S3 datasheet gives 45 kOhm typical for the internal weak pull-up. At
3.3 V that is about 73 uA per asserted active-low line. The classic ESP32 has
no internal pull on GPIO35, so the StickC family depends entirely on the board
resistor. Do not quote a current for that net without the schematic value.

Pull-down choices are simpler: `armMotionWake()` disables the internal
pull-down on every wake pin, and no other pin in the power path is configured
with a pull-down. A pull-down on an active-low wake line would hold the line
low, burn current through the external pull-up, and can block light sleep. Any
future wake pin must keep the pull-down off unless the line is active high. The
ESP32-S3 internal pull-down has the same 45 kOhm order as the pull-up, so a
contended pull-down is the same few tens of microamps per line.

The firmware sets `CONFIG_ESP_SLEEP_GPIO_ENABLE_INTERNAL_RESISTORS=y` on every
board and leaves `CONFIG_PM_SLP_DISABLE_GPIO` unset. The first only matters in
deep sleep, which the firmware never enters. The second means pads hold their
state through light sleep, which is what the LED and IR paths rely on.

### 3.4 USB serial and Ethernet

**ESP32 boards.** The console is UART0 through the USB-UART bridge. The bridge
is powered from VBUS and draws nothing from the battery when unplugged. The five
battery release builds have no console. The ESP32 console builds (the `-debug`
envs) hold `ESP_PM_APB_FREQ_MAX` for the life of the console
(`src/FurbleConsole.cpp:2838-2847`), which blocks light sleep by the IDF mode
mapping. This is stated in the source comment and is not a measurement. The
StickS3, headless, and Waveshare console builds use USB Serial/JTAG instead and
take the lock described below.

**StickS3.** The console is native USB Serial/JTAG. The build sets
`CONFIG_USJ_NO_AUTO_LS_ON_CONNECTION=y`. The Kconfig help says that while the
USB Serial/JTAG port is connected, a `ESP_PM_NO_LIGHT_SLEEP` lock is held. So a
USB-attached StickS3 cannot show the light-sleep floor, by design. This is the
single most important measurement caveat for the board (section 6.2).

**Waveshare.** The W5500 is an SPI MAC plus PHY, not an internal EMAC. The code
sets 20 MHz SPI, INT on GPIO10, reset on GPIO9, and leaves the PHY running from
`Ethernet::init()` to `Ethernet::stop()`. There is no link-down power-down and
no PHY power-down call. The board is mains powered, so that is a heat and
input-current question, not a battery question.

### 3.5 Sleep states that are actually reachable

| Board | Connected BLE floor | Light sleep | Deep sleep | PMIC off | Wake sources |
| :--- | :--- | :--- | :--- | :--- | :--- |
| StickC, Plus | 14.1 mA modem sleep, internal RC RTC clock | 0.8 mA ESP32 plus AXP192 blocks | 10 uA ESP32 plus AXP192 operating | 27 uA | PWR button, ESP timer in deep sleep |
| Plus2 | 14.1 mA modem sleep | 0.8 mA plus divider | 10 uA plus divider | HOLD low | PWR button, ESP timer, HOLD |
| Core | 14.1 mA modem sleep | With the IP5306 boost held on | With the IP5306 boost held on | None, software off refused (no IP5306 shutdown command) | PWR button; no software wake source |
| Core2 | 14.1 mA modem sleep | 0.8 mA plus AXP192 plus touch | 10 uA plus AXP192 | 27 uA | Touch G39, PWR button, ESP timer |
| StickS3 | About 3.3 mA reported measured-local for the current parameters | 240 uA plus 140 uA PSRAM plus PM1 | 7 uA plus PM1 | 14.02 uA | PM1 timer, PM1 GPIO4 IMU, buttons |
| Waveshare | Modem sleep configured, no battery | Disabled by tickless off | Not used | None | Mains only |
| Headless | Modem sleep configured | 240 uA plus PSRAM | Not used | None | Mains only |

The StickS3 3.3 mA value is the connected floor cited across the repo as
`measured-local`. It is reported for the stock connection parameters, not for
the `CONN_SAVER` idle parameters, and the conditions are not trace-equivalent to
the simulator's 3.61 mA what-if. It is a floor for the current parameter set,
not a universal board floor. There is also a provenance gap: the plan 00
results table records the crystal and GPS experiments, but not a 3.3 mA drain
row. The figure is repeated by plans 98 and 111 and by the model README, yet
the original trace is not in the tree. Treat it as a reported observation, not
a reproducible measurement, until the run is re-recorded.

### 3.6 The orderly power-off sequence

`Platform::powerOff()` is the single exit point for the Off menu, the low
battery shutdown, the auto-off policy and the console `power off` command, so
the order of the three steps below is fixed:

1. `TimeKeeper` flushes the wall-clock checkpoint.
2. `SD::powerOff()` posts `POWER_OFF` to the SD writer task. The writer closes
   the GPX track and unmounts the card, then gives the completion semaphore.
   The caller waits 3 s, extends the wait once by 5 s, and refuses if the
   writer still has not answered.
3. Only then does the rail drop, through the M5PM1 `shutdown()` on the StickS3
   or `M5.Power.powerOff()` on the AXP192 and IP5306 boards.

`SD::powerOff()` refuses in two shapes, because both leave the track open. A
writer that is still busy takes the request and misses both waits. A writer that
has stopped draining cannot take the request at all, because its queue is full
of the track points whose file it holds; that returns `false` after the 100 ms
send timeout rather than after a wait, since there is nothing to acknowledge.

A refused shutdown returns `false` and leaves the rail up. `Platform::powerOff()`
returns before it disables the watchdog. `UI::doPowerOff()` had already disabled
it for the teardown, so it re-arms it from the saved `WATCHDOG` setting, clears
its power-off latch and logs `Power off failed, resuming.`. The user sees the
device stay on with the camera already disconnected and the panel still lit.
Tracking does not advance while the writer is stuck: `SD::logPoint()` drops the
points queued behind it and the track file already open is the data the refusal
protects. The next point reopens the track once the writer catches up.

A refusal also holds the two automatic paths off for a 30 s backoff, because
each attempt blocks the UI task for the whole handshake; an explicit Off or
console request is not gated. On a critically low battery the warning box
returns, so the still-on device has a visible reason. The console `power off`
reply is unchanged and reports the queueing, not the outcome.

Only a board with no SD slot keeps the plain off path and drops the rail as
before, because it has no queue and no writer task. Those four paths are the
whole scope of the handshake. A timed shutdown (`Platform::powerOffUntil()` with
a non-zero delay) and a restart (`Platform::restart()`) skip it, so both leave
the track for the next mount to repair, which is the same data-loss class this
refusal avoids.

The 5 s extension and the 30 s retry backoff are chosen bounds, not
measurements: no card has been timed for a worst-case mount or `fsync()`. On the
board rows, `sdSlot` and `canPowerOff` meet only on Core2, so the SD refusal is
reachable on that image; the StickS3 reports no SD slot and its refusal is the
PMIC one. No host target compiles the firmware `Platform::powerOff()`, and the
simulator supplies its own, so the firmware build covers that early return while
the boundary tests cover `SD::powerOff()`. Hardware validation is pending:
pulling a card mid-write and asking for power off has not been tried on a Core2.

Evidence: `code`. The bounds are `POWER_OFF_WAIT_MS`, `POWER_OFF_EXTENDED_WAIT_MS`
and `REQUEST_SEND_TIMEOUT_MS` in `src/FurbleSD.cpp`, and the retry bound is
`POWER_OFF_RETRY_MS` in `include/FurbleAutoOff.h`. The two refusals are asserted
by the `boundary-sd-shutdown-refused` and `boundary-sd-shutdown-undelivered`
host tests. The 8 s total bound is well inside the 45 s M5PM1 watchdog the UI
disables before it calls in.

## 4. Opportunity catalog

Each entry gives the change, the current behavior, the evidence label, the
estimated saving, the risk, the effort, the required test, and the citation.
Estimates are not measurements unless the label says `measured-local`.

### 4.1 PMIC, charger, gauge, and sleep states

#### PM-01 StickS3: deep sleep between intervalometer frames

- Change: before a long interval gap, persist the resume state, arm the PM1
  timer with `M5PM1.timerSet(seconds, M5PM1_TIM_ACTION_POWERON)`, then call
  `shutdown()`. Resume on boot through the existing auto-connect path.
- Current behavior: no `esp_deep_sleep`, `timerSleep`, or `shutdown` call exists
  in the application outside `Platform::powerOff()`. A running intervalometer
  keeps the link and the device awake for the whole gap. With the screen on,
  the S3 model is 84.5 mA connected. With the screen off and `SLEEP_CONN` off
  it is 43.6 mA.
- Evidence: `code` (no deep sleep call, `grep` returns zero), `plan` (plan 19),
  `model` (84.5 and 43.6 mA), `vendor-doc` (14.02 uA off).
- Estimated saving: for a 120 s interval with 15 s awake, 84.5 mA for 15 s plus
  14 uA for 105 s is about 0.35 mAh per frame, against 2.82 mAh if awake for
  the whole interval. That is about 87 percent. If reconnect takes 30 s, the
  saving is about 75 percent. Model plus vendor off current, not measured.
- Risk: high. The camera may drop remote mode on disconnect, the PM1 watchdog
  and the resume path interact, stale resume state can fire an unintended
  sequence, and the bricking traps in plan 26 apply. This is not a default.
- Effort: large. New setting, resume blob, boot ordering, failure policy, and
  an overnight test.
- Required test: 8 hours on battery, Fujifilm, 120 s interval, 240 frames.
  Compare the frame count and EXIF spacing, report the battery delta, then
  repeat with the camera powered off for one interval to test the reconnect
  policy.
- Citation: `plans/implemented/19-interval-deep-sleep.md`; M5PM1 manual V1.9 `SYS_CMD`
  `0x0C` value `0xA1`; M5Stack StickS3 product page power table.

#### PM-02 StickS3: characterize the PM1 L1 and L2 states

- Change: measure what L1 and L2 actually are, what they retain, and how fast
  they resume. Use the result to decide between full shutdown and a retained
  low-power state for short gaps.
- Current behavior: unused. Furble only calls `shutdown()`. The product page
  lists L1 at 52.47 uA and L2 at 102.40 uA but does not define the states.
- Evidence: `vendor-doc`, `unverified` semantics.
- Estimated saving: none against shutdown, which is 14.02 uA. The value is
  resume time. At a 38 uA penalty for L1 versus off, one saved second of 84 mA
  awake pays for about 2,200 seconds of L1. The break-even is a measurement,
  not a claim.
- Risk: medium. An undefined state can lose RTC RAM, the rail state, or the
  wake source, and a latched state can look like a brick.
- Effort: medium. Register pokes on the bench, then a resume-time measurement.
- Required test: for each of off, L1, L2, and light sleep, measure current,
  wake latency to first shutter, retained state, and recovery after a failed
  wake. Repeat ten times.
- Citation: M5Stack StickS3 product page power table; M5PM1 manual V1.9
  (does not define L1 or L2).

#### PM-03 StickS3: rate-limit the PM1 button poll

- Change: implemented. `PlatformPolicy::pm1PollDue()` gates the read to one per
  `PM1_POLL_MS` (20 ms), a tenfold cut from one read per 5 ms UI pass. The
  suggestion here was 50 ms; 20 ms keeps the press edge at 50 Hz, which is what
  the header argues for.
- Current behavior: `Platform::update()` calls `m_M5PM1.btnGetState()` only when
  `pm1PollDue()` is true, so the PM1 sees at most 50 I2C reads per second
  instead of 200. That read calls the library directly and is not wrapped by
  `m5pm1Access()`, so a read lost to a PM1 idle-sleep wake costs one poll, not a
  press. M5Unified's own PMIC button read stays on its
  `BTNPWR_MIN_UPDATE_MSEC` of 4 ms. The PM1 `SLP_TO` field is zero in the
  current library path, so the PM1 is not idle-sleeping between reads, and the
  manual makes idle sleep invalid while PWM is enabled and while entering
  Download mode in any case (M5PM1 Chip User Manual V1.9, pages 27 and 28).
- Evidence: `code` (`src/FurblePlatform.cpp:980-996`,
  `include/FurblePlatformPolicy.h:19-24`, M5Unified 0.2.20
  `M5Unified.cpp:3414` and `M5Unified.hpp:647`), `datasheet` (`I2C_CFG`).
- Estimated saving: 40 to 150 uA average (`inferred`), unchanged from the
  pre-change estimate. The measurement below is still owed.
- Risk: low. A 20 ms sample still catches a human click, and hold and double
  click work at 50 Hz.
- Effort: done.
- Required test: 10 minutes screen off on battery, 50 Hz versus 200 Hz, with
  the analyzer. Verify single click, double click, long press, and the PM1
  watchdog feed.
- Citation: M5PM1 manual V1.9 `I2C_CFG` `0x09` `SLP_TO`, section 6 page 27, the
  page 28 note, and the page 15 first-transaction note; M5Unified 0.2.20
  `M5Unified.cpp`.

#### PM-04 AXP192 boards: trim the ADC channel mask

- Change: after `M5.begin()`, write `0x82 = 0xCC` to keep battery voltage,
  battery current, VBUS voltage and VBUS current, and `0x83 = 0x00` to disable
  the internal temperature channel. Keep `0x84` at 25 Hz. This is what
  `include/FurblePlatformPolicy.h` does today.
- Current behavior: M5Unified writes `0x82 = 0xFF` (every channel),
  `0x83 = 0x80` (temperature on), and `0x84 = 0x32` (25 Hz) on every AXP192
  board. Furble reads battery level, voltage, and current, and VBUS voltage.
  ACIN voltage and current, APS, TS and temperature are unused.
- VBUS current is not unused: M5Unified's legacy AXP192 `setExtOutput()` path
  reads it on the Core2 and Tough to refuse external power when the battery is
  low and no ACIN supply is present, so bit 2 stays set. The StickC and Plus
  path only calls `Axp192.setEXTEN()`, so the channel is dead weight there; one
  mask is written on every AXP192 board, and trimming it only on the StickC
  family is the plan 350 follow-up, deferred because the AXP192 register details
  could not be verified in that pass.
- Evidence: `code` (M5Unified 0.2.20 `Power_Class.cpp` register block and
  `setExtOutput()`), `datasheet`.
- Estimated saving: under 100 uA, possibly under 10 uA (`inferred`). The AXP192
  datasheet does not give a separate ADC block current, so this must be
  measured before it is claimed. It is a trim, not a headline.
- Risk: low. The battery gauge must keep working; the charge status is read
  from `0x00`, not from the ADC. Verify the battery page and the low-battery
  policy after the change.
- Effort: small.
- Required test: battery page, `power log`, and a 1 hour analyzer run against
  the stock mask on one AXP192 board. Verify level, voltage, current, and
  charge state.
- Citation: AXP192 datasheet `REG 82H`, `REG 83H`, `REG 84H`.

#### PM-05 StickS3: clear the status LED in console display mode

- Change: route the console display mode through the same display-off policy as
  a normal panel sleep, including `Platform::setDisplayOff(true)` and the icon
  timer pause. Restore the LED on exit.
- Current behavior: `UI::setDisplayMode()` calls `M5.Display.sleep()` and
  releases the APB lock, but does not call `Platform::setDisplayOff(true)`.
  The PM1 `PWR_CFG` LED bit stays at its default 1, so the green LED stays lit
  for the whole console session. The same gap exists between boot and the first
  UI display-off transition.
- Evidence: `code` (`src/FurbleUI.cpp:10150-10167`,
  `src/FurblePlatform.cpp:221-254`), `datasheet` (`PWR_CFG` bit 4).
- Estimated saving: 1 to 3 mA while the LED is on (`inferred`). The LED
  current is not in the tree and is not measured. The LED is already cleared on
  a normal display-off, so this only affects console mode and the boot window.
- Risk: low. The only behavior change is a dark LED where the user asked for a
  console-only display.
- Effort: small.
- Required test: enter console display mode, confirm the LED is dark and the
  panel is asleep, exit, confirm the LED returns. Measure the LED branch
  current if the schematic is available.
- Citation: M5PM1 manual V1.9 `PWR_CFG` `0x06` bit 4; `plans/implemented/71-ui-bug-batch.md`.

#### PM-06 Core: hold the IP5306 boost on for low-current states

- Change: implemented in plan 354. `Platform::keepIp5306BoostOn()` sets
  `REG_SYS_CTL0` bit 1 ("BOOST output normally open") at boot through
  M5Unified and reads it back, and the bit stays set for the whole session.
  The one path that clears it is `M5.Power.powerOff()`, which is the manual Off:
  it sleeps the panel first, so the post-call load is under 45 mA and the
  light-load cut removes the boost 64 s later.
- Previous behavior: M5Unified wrote `0x00 = 0b00110001` and
  `0x02 = 0b01101100`, leaving bit 1 clear. A screen-off Core fell below
  45 mA and the IP5306 cut the boost, which powered the device off.
- Evidence: `code` (`include/FurbleIP5306.h`, `src/FurblePlatform.cpp`,
  M5Unified `IP5306_Class.cpp` and `Power_Class.cpp`), `datasheet` (IP5306
  light-load threshold 45 mA; register document V1.21 SYS_CTL0 bit 1 and
  SYS_CTL2 bits 3:2).
- Estimated saving: 20 to 60 mA in the screen-off state (`inferred`). The upper
  bound is the current 43 to 81 mA model range. The floor depends on the
  IP5306 boost quiescent current, which the public datasheet does not give, so
  the real floor could be a few mA or worse. Measure before claiming a runtime.
- Risk: medium. The device no longer auto powers off on a light load, which is
  the point, but a wedged firmware can drain the cells to the protection
  cutoff. The boost may run hot at no load. This is not hardware-verified yet.
- Effort: small code, medium test.
- Required hardware test: 30 minutes screen off on battery with keep-on,
  confirm no power cut; measure pack current and board temperature.
- Citation: IP5306 datasheet V1.01 (45 mA, 50 uA standby, 100 uA input
  quiescent); IP5306 register document V1.21 SYS_CTL0 bit 1, SYS_CTL2 bits 3:2;
  M5Unified 0.2.20 `IP5306_Class.cpp` and `Power_Class.cpp`.

#### PM-07 All battery boards: auto-off default

- Change: default `AUTO_OFF` to 10 or 15 minutes on the battery boards. Keep
  the Core excluded: PM-06 settled the light-load cut, but the IP5306 still has
  no shutdown command, so auto power off cannot remove the Core's power.
- Current behavior: `AUTO_OFF` defaults to 0 (never). The mechanism is
  implemented and defaults off. A forgotten device runs at the menu or
  connected current until the cell is flat. The Core is excluded because its
  only off path is a software deep sleep with no wake source and the boost
  still up.
- Evidence: `code` (`src/FurbleSettings.cpp` defaults, plan 13), `model`.
- Estimated saving: the whole idle current, 25.5 to 84.5 mA, whenever the user
  forgets the device. The end state is 27 uA on AXP192 and 14.02 uA on the
  StickS3 (`datasheet` plus `vendor-doc`).
- Risk: low to medium. The user may want a long-running idle display. Auto-off
  is suppressed while connected, scanning, or charging by default, so the
  intervalometer and a live link are not cut.
- Effort: small, plus a documentation change.
- Required test: 30 minutes idle, confirm the power off at the threshold;
  confirm no power off while connected, scanning, or charging; confirm button
  wake; repeat on StickC, Plus, Core2, and StickS3.
- Citation: `plans/implemented/13-auto-off-low-batt.md`; AXP192 off 27 uA; StickS3 14.02 uA.

#### PM-08 Core2: put the touch controller in a low-power mode with the panel

- Change: use the FT6336U monitor or hibernate mode when the panel is off and
  wake on the touch interrupt, instead of polling the controller for the whole
  screen-off period.
- Current behavior: the touch controller is deliberately left running so a tap
  can wake the screen (`plans/implemented/12-display-off.md`). M5Unified's `Touch_Class`
  has no power API, so furble cannot change it today.
- Evidence: `code` (`src/FurbleUI.cpp` display path, `M5.Touch` usage),
  `plan`, `unverified` datasheet detail.
- Estimated saving: 0.5 to 2 mA while the screen is off (`inferred`). The
  controller current is not in the tree and the datasheet was not opened for
  this document, so the range is a placeholder, not a claim.
- Risk: medium to high. A wrong mode loses tap-to-wake, which is the only wake
  path on a touch board. The I2C transaction that sets the mode must not race
  the touch read.
- Effort: medium to large. Direct register work outside M5Unified, plus a wake
  path that proves the interrupt still works.
- Required test: 10 minutes screen off, 20 out of 20 taps wake the screen, no
  lost touch after wake, analyzer delta against the stock build.
- Citation: `plans/implemented/12-display-off.md`; M5Stack Core2 touch documentation; the
  FT6336U datasheet (not opened here).

#### PM-09 Core: ESP deep sleep with boost keep-on

- Change: for long interval gaps, use ESP32 deep sleep with a timer wake and
  the IP5306 boost held on, instead of staying awake. The plan predates the
  session keep-on fix, which now holds the boost on for every state.
- Current behavior: no deep sleep code. Plan 19 excludes the Core because its
  PMIC cannot self wake from a power-off state. That is true for PMIC power
  off, but ESP32 deep sleep with the boost held on is a different path, and
  M5Unified's `deepSleep()` sets the keep-on bit for exactly this case.
- Evidence: `code` (M5Unified `deepSleep`), `datasheet` (ESP32 deep sleep
  10 uA), `plan` (the exclusion).
- Estimated saving: uncertain. If the boost quiescent is 1 to 3 mA, deep sleep
  is 1 to 3 mA against 43 to 84 mA, a large win. If the boost is worse, the win
  shrinks or disappears. The floor is the measurement.
- Risk: high. The boost is always on, the battery can drain to cutoff, the
  camera must reconnect, and the wake source must be proven on the Core.
- Effort: large. This is plan 19 work on a board the plan currently excludes.
- Required test: 5 minutes deep sleep with keep-on, wake by timer, measure
  current and temperature, repeat ten times; then a 1 hour interval run.
- Citation: ESP32 Series Datasheet (deep sleep 10 uA, as recorded in the repo
  model); IP5306 datasheet V1.01; M5Unified 0.2.20 `Power_Class.cpp`.

#### PM-10 AXP192 boards: timed deep sleep

- Change: for long interval gaps on StickC, Plus, and Core2, put the panel to
  sleep, release the APB lock, and enter ESP32 deep sleep with an RTC timer
  wake. The AXP192 stays in operating mode to hold DCDC1.
- Current behavior: no deep sleep code. Plan 19 excludes these boards because
  the AXP192 has no PMIC timed wake. The ESP32 RTC timer is a separate wake
  path and does work while the AXP192 holds the rail.
- Evidence: `code`, `datasheet` (ESP32 deep sleep 10 uA; AXP192 per-block
  quiescent), `plan-estimate` (the about 2 mA floor).
- Estimated saving: from 25.5 to 50 mA active to an estimated 0.2 to 0.3 mA
  deep-sleep floor, dominated by the AXP192 operating quiescent (`inferred`).
  The exact floor is unmeasured and the "2 mA" plan figure is not a datasheet
  line.
- Risk: high. The AXP192 stays on, the display must be asleep before the CPU
  stops, the camera must reconnect, and the 27 uA PMIC off state is still
  better for untimed idle. Use it only for timed gaps.
- Effort: large.
- Required test: 5 minutes deep sleep, timer wake, current, panel state, and
  reconnect; then a 1 hour interval run on StickC Plus and Core2.
- Citation: ESP32 Series Datasheet (deep sleep 10 uA); AXP192 datasheet
  electrical characteristics; `plans/implemented/19-interval-deep-sleep.md`.

#### PM-11 Plus2: verify the off path before auto-off

- Change: characterize `M5.Power.powerOff()` on the Plus2 and the GPIO4 HOLD
  latch. If the path leaves the device in a deep sleep with no wake source,
  either fix the wake source or exclude the Plus2 from auto-off.
- Current behavior: the `m5stick-c-plus` image runs on the Plus2. The Plus2 has
  no PMIC; M5Unified's `_powerOff()` has no `pmic_adc` case, so with
  `withTimer=false` it reaches `esp_deep_sleep_start()` without arming a wake
  source. The PWR button may or may not reset the rail, which is unverified.
  The result can look like a dead device.
- Evidence: `code` (M5Unified 0.2.20 `Power_Class.cpp`), `unverified` hardware.
- Estimated saving: none directly. This is a gate for PM-07 on the Plus2 and a
  protection against a support incident.
- Risk: high if the path is wrong. A user can be left with a device that only
  recovers by a long press or USB power.
- Effort: small to verify, medium to fix.
- Required test: manual Off and auto-off on a Plus2, wake by PWR single click,
  double click, and USB. Record the HOLD pin and rail state at each step.
- Citation: M5Unified 0.2.20 `Power_Class.cpp` `_powerOff`; M5Stack StickC Plus2
  documentation (HOLD on GPIO4).

### 4.2 Rails, peripherals, and storage

#### RL-01 GPS: rework the duty policy before changing the default

- Change: keep the default always-on for now, but add a longer duty interval
  with a cached-fix policy and a shorter wake dwell. At minimum, add the
  existing `GPS_POWER` and `GPS_DUTY` settings to the Battery Saver bundle so
  an opted-in user gets standby without a separate menu trip.
- Current behavior: `GPS_POWER` and `GPS_DUTY` both default to 0, so the
  receiver runs continuously at 23 to 31.6 mA whenever GPS is enabled. The
  existing duty mode is clamped to 15 s. Plan 98 attributes the small saving to
  the 5 s minimum wake wait and that clamp. The baseline model is 21.5 mA
  against 23.0 mA always-on, only about 1.5 mA saved. The audit's 60 s interval
  with a 5 s burst models at about 4 mA, but it is unimplemented and it
  challenges the 30 s `MAX_AGE_MS` freshness budget.
- Evidence: `code` (`DUTY_SECONDS`, `MIN_WAKE_WAIT_MS`, `MAX_AGE_MS`), `model`
  (21.5 and 23.0 mA, 4 mA), `measured-local` (PCAS12 standby works, no backup
  supply, a 107.7 s re-fix after a 60 s rail cut).
- Estimated saving: about 1.5 mA now (`model`). Up to about 19 mA if the
  interval and wake dwell are redesigned and a cached fix is acceptable
  (`model`). The `$PCAS12` in-circuit standby current is still an estimate of
  0.5 mA, range 10 uA to 2 mA.
- Risk: medium. A fix older than 30 s stops geotagging silently. The receiver
  may ignore `$PCAS12` on a different unit. Do not cut the rail: there is no
  backup supply and a rail cut costs a measured 107.7 s re-fix.
- Effort: small for the Battery Saver bundle, medium for the interval rework.
- Required test: 1 hour with a live fix, log fix age and NMEA gaps at 5, 10,
  15, and 60 s duty; compare battery drain; verify a geotag write on each
  vendor path that supports GPS.
- Citation: `plans/00-hardware-experiments.md` Experiment B; `plans/implemented/15-gps-power.md`;
  M5Stack Unit GPS v1.1 (31.64 mA at 5 V); AT6668 datasheet DS-AT6668 V1.4
  section 4.3 page 9 (37 mA tracking with BDS, GPS and GLONASS, 8 uA battery
  backup, 30 uA sleep); CASIC R6 `$PCAS12` command, section 2.7.11 page 44.

#### RL-02 GPS: stop back-powering the receiver when the 5 V rail is off

- Change: when `setRailPower(false)` is called, put the UART TX pin in a high
  impedance state, or drive it low if the unit's RX input tolerates it, and
  restore the pin when the rail returns.
- Current behavior: the rail is cut, but UART2 TX stays configured and idles
  high into the unpowered unit's RX input. That can leak through the unit's
  input protection and the onboard regulator. The same applies in the
  absent-receiver state, where the rail is held off.
- Evidence: `code` (`src/FurbleGPS.cpp:118-122`, UART setup at `:188-215`),
  `unverified` unit input topology.
- Estimated saving: 0 to 1 mA while the rail is off (`inferred`). It only
  applies in rail-cycle mode or when no receiver is present, so it is not a
  normal-use win. It matters for the absent-receiver case, which can last the
  whole session.
- Risk: medium. A floating TX line can pick up noise; driving it low can
  back-feed the unit in the other direction. The correct choice depends on the
  unit schematic.
- Effort: small.
- Required test: with the rail off, measure pack current with TX floating,
  driven high, and driven low. Verify no false NMEA and no GPS wake.
- Citation: `src/FurbleGPS.cpp`; M5Stack Unit GPS v1.1 documentation (5 V
  input; no backup supply).

#### RL-03 StickS3: give the IR rail an owner, and account for its load

- Change: enable the 5 V boost for IR transmit and IR receive as well as for
  GPS, with one owner and a reference count, or document that IR requires GPS
  to be enabled. Account for the IR receiver current whenever the boost is on.
- Current behavior: the only `setExtOutput` caller is the GPS path. M5Unified
  leaves EXT_5V off by default, and the StickS3 product page says EXT_5V feeds
  the Grove port, the Hat EXT_5V pin, and the IR transmitter and receiver. With
  GPS off, the IR hardware is unpowered, so the IR feature cannot work. With
  GPS on, the IR receiver is powered even when IR is not used.
- Evidence: `code`, `vendor-doc` (StickS3 EXT_5V note and pinmap), `datasheet`
  (PM1 `PWR_CFG` bit 3).
- Estimated saving: no direct saving. The accounting item is the IR receiver
  load, estimated at 0.5 to 1 mA while the boost is on (`inferred`, not
  measured). The functional fix is worth more than the current.
- Risk: medium. If IR disables the boost while GPS is running, the GPS rail
  drops and the receiver cold-starts. If GPS disables the boost while IR is
  transmitting, the IR frame is lost.
- Effort: small to medium.
- Required test: IR transmit with GPS off, GPS NMEA continuity while IR
  transmits, and a current measurement of the boost with and without an IR
  receiver attached.
- Citation: M5Stack StickS3 product page EXT_5V note and pinmap; M5PM1 manual
  V1.9 `PWR_CFG`; `src/FurbleGPS.cpp:118-122` and `:188-215`.

#### RL-04 Core2: LDO2 and LDO3 rail scope

- Change: none recommended. Keep LDO2 at 3.3 V while the panel is powered and
  keep LDO3 at 0 V except for vibration events. Do not apply the Core2 LDO3
  assumption to the M5Tough, where LDO3 is the backlight.
- Current behavior: LDO2 powers the LCD and the SD slot and stays on. LDO3 is
  set to 0 V at boot and is driven only by `M5.Power.setVibration`. The
  NS4168 amp enable is set low at boot and the speaker is ended after init.
- Evidence: `code` (M5Unified board init, `src/FurbleFeedback.cpp`),
  `datasheet` (LDO2 and LDO3 100 uA each no load).
- Estimated saving: 0. This is already optimal given the wiring. Cutting LDO2
  would remove the panel and SD supply; there is no separate switch for the SD
  slot.
- Risk: n/a.
- Effort: none.
- Required test: none. Recheck only if a future board revision adds an SD load
  switch.
- Citation: M5Unified 0.2.20 `Power_Class.cpp`; AXP192 datasheet LDO2 and LDO3
  quiescent current.

#### RL-05 StickC and Plus: LDO3 LCD power during panel sleep

- Change: none recommended. LDO3 at 3.0 V powers the LCD logic and must stay on
  while the panel is initialized. Cutting it would power the panel off and
  lose the init state.
- Current behavior: LDO3 stays on. LDO2, the backlight, is cut by M5GFX when
  brightness reaches 0. DCDC3 is already off on these boards.
- Evidence: `code` (M5Unified board init, M5GFX `Light_M5StickC`),
  `datasheet` (LDO3 100 uA no load).
- Estimated saving: 0 without a panel re-init path. About 100 uA of LDO3
  quiescent is the price of keeping the panel alive.
- Risk: high if changed. A dark or garbage panel and a lost init state.
- Effort: none.
- Required test: none.
- Citation: M5Unified 0.2.20 `Power_Class.cpp`; M5GFX 0.2.27
  `Light_M5StickC`; AXP192 datasheet.

#### RL-06 Core and Core2: SD card standby current

- Change: none available. There is no load switch on either SD slot. Keep the
  card unmounted when logging is off and keep CS deasserted.
- Current behavior: `SD::init()` supports only the Core and Core2. The card is
  mounted when GPX logging or the SD page needs it, and unmounted when not.
  Unmounting stops transactions but the card remains powered.
- Evidence: `code` (`src/FurbleSD.cpp:49-57`, `:680-700`, `:922-940`),
  `unverified` card standby current.
- Estimated saving: 0 in firmware. The card's own standby current is a fixed
  overhead, typically 0.1 to 0.5 mA for a microSD card, and must be measured
  with the card inserted and removed. That is an inference, not a datasheet
  number for the card in the slot.
- Risk: n/a.
- Effort: none.
- Required test: 10 minutes with a card inserted and logging off, card removed,
  and logging on. Report the delta.
- Citation: `src/FurbleSD.cpp`; M5Stack Core and Core2 microSD documentation.

#### RL-07 Waveshare: microSD, WS2812, and W5500 loads

- Change: none for battery. The board has no battery. Keep the unused microSD
  slot uninitialized and the WS2812 off when idle.
- Current behavior: furble's SD service rejects the board because it is not a
  Core board, so the slot is never mounted. The WS2812 is not driven by furble.
  The W5500 is started at boot and stays up.
- Evidence: `code` (`src/FurbleSD.cpp:680`, `src/FurbleEthernet.cpp`),
  `vendor-doc` (Waveshare board page), `datasheet` (W5500 Ver 1.1.0, 5.4 p. 60
  for the link and un-link currents).
- Estimated saving: 0 battery runtime, because there is no battery. The WS2812
  has a quiescent current even when off, and the W5500 has a link-dependent
  current, but both are mains input current.
- Risk: n/a for battery.
- Effort: none.
- Required test: none for a battery goal. If input power matters, measure the
  5 V input with link up, link down, and the LED off.
- Citation: Waveshare ESP32-S3-ETH wiki and schematic; `src/FurbleEthernet.cpp`.

#### RL-08 All boards: speaker, vibration, IR, and LED event rails

- Change: none. The event outputs are already bracketed.
- Current behavior: the speaker is opened only for a tone and ended after the
  pattern; the Core2 amp enable is low at boot; the vibration motor is at 0 V
  except for an event; the IR channel holds `ESP_PM_CPU_FREQ_MAX`, not an APB
  lock, only for the transmit window, and it is disabled after; the status LED
  is only driven for a feedback event.
- Evidence: `code` (`src/FurbleFeedback.cpp`, `src/FurbleIR.cpp`),
  `datasheet` (NS4168 13 mA quiescent, 1 uA shutdown).
- Estimated saving: 0. The remaining cost is the event itself, which is user
  requested.
- Risk: n/a.
- Effort: none.
- Required test: none. Verify after any change to the speaker or IR path.
- Citation: `src/FurbleFeedback.cpp`; `src/FurbleIR.cpp`; NS4168 datasheet.

#### RL-09 StickS3: ES8311 codec and AW8737 amplifier

- Change: none recommended beyond verifying that the codec is in its low-power
  state when the speaker is closed.
- Current behavior: M5Unified opens the speaker for a tone and furble calls
  `M5.Speaker.end()` after init and after each pattern. The product page says
  the IR receiver needs the speaker amplifier off, which is the same direction.
- Evidence: `code`, `vendor-doc`.
- Estimated saving: 0 if the codec is already idle. The ES8311 and AW8737
  quiescent currents are not in the tree, so a closed speaker is an assumption,
  not a measurement.
- Risk: low.
- Effort: small to verify.
- Required test: measure the 3.3 V rail current with the speaker closed, opened
  but silent, and playing a tone.
- Citation: M5Stack StickS3 product page (ES8311, AW8737); `src/FurbleFeedback.cpp`.

#### RL-10 Plus2: battery ADC divider

- Change: none available. The divider is a board fixture. Do not add a second
  ADC read path.
- Current behavior: M5Unified reads ADC1 channel 38 with a 2.0 ratio for the
  Plus2 battery. The divider is permanently connected.
- Evidence: `code` (M5Unified board init), `unverified` resistor values.
- Estimated saving: 0 in firmware. The divider current is fixed. A 100k and
  100k pair at 4.2 V is about 21 uA; a 10k and 10k pair is about 210 uA. The
  actual value needs the schematic.
- Risk: n/a.
- Effort: none.
- Required test: measure the divider current with the battery at 4.2 V and the
  ESP32 in deep sleep. Read the resistor values from the schematic.
- Citation: M5Unified 0.2.20 `Power_Class.cpp` Plus2 ADC setup; StickC Plus2
  documentation.

#### RL-11 BMI270 and MPU6886 boards: acceleration-only low power

- Change: when the IMU is enabled, configure the BMI270 or MPU6886 for
  accelerometer-only low-power operation at the rate the UI actually needs
  (1 Hz for gestures and motion, 50 Hz for the level page), instead of
  six-axis normal mode.
- Current behavior: `M5.begin(cfg)` initializes the IMU when the setting is on,
  and M5Unified configures the part for normal six-axis use. `FurbleIMU.cpp`
  reads only `getAccel`; the gyro is never read. The IMU setting defaults off
  and applies on reboot. The hardware any-motion engine in plan 20 is not the
  default.
- Evidence: `code` (`src/FurbleIMU.cpp`, `src/FurblePlatform.cpp:40`),
  `datasheet` (BMI270 685 uA normal versus 4 to 10 uA accel low power; MPU6886
  2.79 mA versus 40 uA accel low power), `model` (the peripheral term is fixed
  at 0.0035 mA, not the IMU normal-mode current).
- Estimated saving: about 0.68 mA on the StickS3 and about 2.75 mA on the
  StickC, Plus, and Core2 while the IMU is enabled (`datasheet`). On the S3
  that is about 20 percent of the 3.3 mA connected floor. The default is off,
  so the saving applies only to users who enabled the IMU.
- Risk: medium. Gesture thresholds, the spirit level, and the wake gesture
  depend on the sample rate and range. A lower ODR can miss a double tap. The
  gyro is unused today, so disabling it should be safe, but the tilt response
  must be rechecked.
- Effort: medium. M5Unified does not expose accel-only low power, so this needs
  direct register writes or a small driver.
- Required test: IMU on, level page at 50 Hz, double tap and wake gesture 20
  times, 1 hour current run on the StickS3 and Core2; compare against the stock
  six-axis configuration. Verify no I2C errors and no lost motion events.
- Citation: BMI270 datasheet `PWR_CTRL` `0x7D`, `PWR_CONF` `0x7C`, `ACC_CONF`
  `0x40`; MPU6886 datasheet `PWR_MGMT_1` `0x6B`, `PWR_MGMT_2` `0x6C`;
  `src/FurbleIMU.cpp`; `plans/implemented/20-imu-hw-motion.md`.

### 4.3 GPIO, pull-ups, and ADC leakage

#### LK-01 StickS3: GPIO13 pull-up on the PM1 IRQ line

- Change: confirm the PM1 IRQ output type. If it is push-pull, disable the
  ESP32 internal pull-up on GPIO13 and rely on the PM1 drive. If it is
  open-drain, keep the pull-up.
- Current behavior: `armMotionWake()` configures GPIO13 as an input with the
  internal pull-up enabled, because the code treats the PM1 IRQ as open-drain
  active low. While the line is held low, the pull-up sinks about 73 uA.
- Evidence: `code` (`src/FurblePlatform.cpp:461-472`), `datasheet` (45 kOhm
  typical pull-up).
- Estimated saving: 0 to 73 uA (`inferred`), depending on how long the line is
  asserted. A latched IRQ that never releases is the worst case, and that is
  also a light-sleep blocker, so the fix has a second benefit.
- Risk: low to medium. A floating input can cause false motion events or block
  sleep. Verify the PM1 output type first.
- Effort: small.
- Required test: scope or meter GPIO13 in idle, motion, and latched states;
  repeat the motion wake gate; measure current with the pull-up on and off.
- Citation: ESP32-S3 Series Datasheet v2.2, internal weak pull-up 45 kOhm
  typical; `src/FurblePlatform.cpp`; M5PM1 manual V1.9 GPIO and IRQ registers.

#### LK-02 StickC and Plus: GPIO35 external pull-up on the shared SYS_INT net

- Change: none. The net carries three open-drain sources: the MPU6886
  interrupt, the BM8563 RTC interrupt, and the AXP192 IRQ output. Making the IMU
  push-pull would fight the other two. The external pull-up is required.
- Current behavior: GPIO35 is input-only on the ESP32 and has no internal pull.
  The board resistor holds the net high. The line sinks current while any source
  asserts it. M5Unified enables the AXP192 power-key IRQ (IRQ enable 3,
  `0x42 = 0x03`), so a power-button press also pulls the net low; masking that
  source would also remove the `0x46` status that `M5.BtnPWR` reads, so the
  firmware instead treats any low level as "wake and count activity".
- Evidence: `code` (`src/FurblePlatform.cpp`), `unverified` resistor value and
  schematic routing.
- Estimated saving: 0 without a board change. The average depends on the
  resistor and the assertion duty, which are not in the tree.
- Risk: high if changed. A push-pull IMU on a shared open-drain net can short
  the RTC output.
- Effort: none.
- Required test: none. If the net is ever changed, measure the resistor and the
  assertion duty first.
- Citation: `src/FurblePlatform.cpp`; MPU6886 datasheet interrupt pin
  configuration (not opened here); BM8563 documentation; M5Unified 0.2.20
  `Power_Class.cpp` AXP192 IRQ enable block and `AXP192_Class::getPekPress()`.

#### LK-03 All boards: IR LED pin when the feature is off

- Change: drive the IR pin to its off level at boot when IR is disabled. On the
  Plus2 GPIO19 drives an NPN stage for both the red LED and the emitter, so it is
  active high and "off" is a low level (plan 350).
- Current behavior: `IR::createChannel()` configures the pin only when a frame
  is transmitted. Otherwise the pin is left at its reset state. On the StickC
  family that is GPIO9, shared with the flash in DIO mode. On the StickS3 it is
  GPIO46. On the Plus2 it is GPIO19.
- Evidence: `code` (`src/FurbleIR.cpp:120-136`, `:155-180`), `unverified`
  leakage.
- Estimated saving: unknown, likely below 10 uA (`inferred`). An IR LED with a
  floating gate or base can leak, but there is no measurement.
- Risk: medium on the Plus2. Driving GPIO19 high keys the emitter through the
  same transistor, so the off level is 0 there. The current firmware leaves the
  pin an input until the LED feature is enabled and hands the pad back to the
  GPIO path after every IR frame, where an unrecorded level is parked at the
  board's inactive level rather than left floating.
- Effort: small on StickC, Plus, and S3.
- Required test: measure the IR pin current with the feature off and the pin
  floating versus driven low. Verify no IR emission with a phone camera.
- Citation: `src/FurbleIR.cpp`; M5Stack StickC and StickS3 pinmaps.

#### LK-04 All boards: internal pulls during sleep

- Change: none recommended while deep sleep is unused. Revisit only if PM-01 or
  PM-10 lands.
- Current behavior: every board sets
  `CONFIG_ESP_SLEEP_GPIO_ENABLE_INTERNAL_RESISTORS=y` and leaves
  `CONFIG_PM_SLP_DISABLE_GPIO` unset. The first applies to deep sleep, which the
  firmware never enters. The second keeps pad state through light sleep.
- Evidence: `code` (sdkconfig files).
- Estimated saving: 0 today. In deep sleep, each held pull-up can cost tens of
  microamps, so a pin audit is required before any deep sleep feature ships.
- Risk: medium. Disabling the internal pulls can float a wake pin.
- Effort: medium, and only after a deep sleep feature exists.
- Required test: for each deep sleep pin, measure current with the internal
  pull on and off, and verify the wake source.
- Citation: ESP-IDF Kconfig `ESP_SLEEP_GPIO_ENABLE_INTERNAL_RESISTORS` and
  `PM_SLP_DISABLE_GPIO`; ESP32-S3 datasheet pull-up resistance.

### 4.4 Policy, radio, display, and housekeeping

#### PL-01 StickS3: `SLEEP_CONN` default

- Change: implemented. `SLEEP_CONN` now defaults on for the StickS3 only; the
  per-vendor soak remains the promotion gate for calling the saving measured.
- Current behavior: `SLEEP_CONN` defaults to true on the StickS3 and false on
  the other boards, which have no modem-sleep controller or GPS burst lock. An
  existing stored choice is preserved. The UI hides it on every board except
  the StickS3. With it off, the Control task holds
  `NO_LIGHT_SLEEP` for the whole connection. With the screen off, the model is
  43.6 mA against 3.61 mA with it on.
- Evidence: `code` (`src/FurbleControl.cpp:1116-1133`,
  `src/FurbleSettings.cpp`), `model`, `plan`.
- Estimated saving: about 40 mA connected with the screen off (`model`). On
  the 250 mAh cell that is about 6 hours against 69 hours. This is the single
  largest addressable number in the tree.
- Risk: medium to high. Modem sleep plus light sleep adds wake latency. A
  camera with a short supervision timeout can drop the link, and the first
  shutter after an idle gap can be slower. Only Fujifilm is available for a
  soak.
- Effort: small code, large soak.
- Required test: the plan 111 states 3 and 4 measurement pair, plus a repeated
  per-vendor soak with no disconnects and an acceptable first shutter latency
  after a 30 s idle gap.
- Citation: `plans/implemented/07-ble-sleep.md`, `plans/98-power-optimization-audit.md`,
  `plans/111-power-recommendations.md`.

#### PL-02 All boards: `CONN_SAVER` default

- Change: default `CONN_SAVER` on, or keep it in the Battery Saver bundle and
  measure the floor before changing the global default.
- Current behavior: `CONN_SAVER` defaults to false. The link stays at the
  stock 37.5 to 62.5 ms interval. With it on, the idle interval is 250 to
  300 ms, about a 6x drop in connection events.
- Evidence: `code` (`plans/10` implementation, `Settings::connSaverEffective`),
  `model` (the model cannot see the interval change and charges a constant
  connected floor).
- Estimated saving: 0.5 to 2 mA connected idle (`inferred`). The true value is
  the difference between the 3.3 mA floor at stock parameters and the floor at
  250 to 300 ms. It is unmeasured.
- Risk: low to medium. Longer intervals can delay the first command and may
  interact with vendor supervision timeouts. The setting is already hardware
  verified for the interval change.
- Effort: small.
- Required test: 30 minute connected idle at stock and at the idle parameters,
  same camera and screen state, analyzer plus `power log` on a bench build.
- Citation: `plans/implemented/10-adaptive-conn-params.md`;
  `plans/98-power-optimization-audit.md`.

#### PL-03 AXP192 boards: scope the display APB lock

- Change: stop holding `APB_FREQ_MAX` for the whole screen-on period. Start with
  a scoped lock around the panel bus and touch transactions, and test whether
  the lock can be released at idle on the StickC, Plus, and Core2. On those
  boards the backlight is an AXP192 voltage or PMIC PWM output and does not need
  the APB clock; the panel SPI bus and the touch I2C bus still might.
- Current behavior: `UI::UI()` acquires `APB_FREQ_MAX` on every board and holds
  it until the panel sleeps. The ESP-IDF power manager maps that lock to
  `PM_MODE_APB_MAX`, which is above `PM_MODE_APB_MIN`; light sleep requires a
  mode below `APB_MIN`. So the lock blocks light sleep for the whole screen-on
  period, on every board. The source comment attributes the lock to the
  backlight PWM, which is an ESP32 LEDC only on the Core, Plus2, and StickS3.
- Evidence: `code` (`src/FurbleUI.cpp:371-374`, `1305-1318`, `1333-1335`),
  `code` (ESP-IDF `pm_impl.c` mode mapping), `datasheet` (ESP32 active current
  at 80 MHz), `inferred` saving.
- Estimated saving: up to about 24 mA on the ESP32 AXP192 boards if the MCU
  could sleep between UI frames (`inferred`). A realistic range with redraws is
  5 to 15 mA. This is a screen-on saving and it applies to the states where the
  user actually uses the device.
- Risk: high. The display SPI, the I2C touch controller, and the LVGL timing
  all assume a stable clock. On the AXP192 boards the backlight is safe, but the
  panel bus and touch may not be. This is the flicker and corruption class of
  bug.
- Effort: medium. A board capability predicate, a lock owner change, and a
  soak on each board.
- Required test: 1 hour screen-on idle and a 100 cycle menu navigation on
  StickC, Plus, and Core2, lock held versus scoped versus released. Check panel
  corruption, touch, I2C errors, and current.
- Citation: M5GFX 0.2.27 `Light_M5StickC` (LDO2) and
  `Light_M5StackCore2` (DCDC3); `_set_pwm_backlight` for Core, Plus2, and
  StickS3; ESP-IDF `components/esp_pm/pm_impl.c`.

#### PL-04 StickS3: clock the LEDC backlight from a source that survives DFS

- Change: prototype an LEDC clock source that does not require the APB lock,
  such as RC_FAST, and use a scoped lock around panel bus transactions, so the
  MCU can drop frequency or light sleep at idle while the panel is on. Keep the
  APB lock as the fallback.
- Current behavior: the backlight is LEDC PWM on GPIO38. `UI::UI()` holds
  `APB_FREQ_MAX` while the panel is on, which blocks DFS and light sleep. The
  audit calls this the only path to cutting the 40 mA MCU term with the screen
  on.
- Evidence: `code`, `plan`, `inferred` saving.
- Estimated saving: up to about 39 mA (`inferred`); realistically 5 to 15 mA
  with redraws. The S3 model attributes 40.2 mA to the MCU and 41.0 mA to the
  display in the screen-on state.
- Risk: high. RC_FAST drifts, brightness linearity changes, and low duty
  flicker is possible. This is the same flicker class as PL-03.
- Effort: medium to high. It likely needs an M5GFX or direct LEDC change, not
  an application-only change.
- Required test: brightness sweep 1 to 100 percent, 1 hour soak, camera
  flicker check, current at 80 and 160 MHz with the lock released.
- Citation: ESP-IDF LEDC clock source documentation; M5GFX 0.2.27
  `_set_pwm_backlight`; `plans/98-power-optimization-audit.md` action 8.

#### PL-05 All boards: screen-off housekeeping and the 5 ms UI loop

- Change: while the display is off and no connection transition is in flight,
  lengthen the UI loop from 5 ms to 20 or 50 ms and slow the battery and
  inactivity timers where the policy allows. Keep the 1 s watchdog feed and
  the 1 s policy tick.
- Current behavior: `UI::task()` delays 5 ms. That bounds automatic light sleep
  to about 2 ms per idle period, so the chip wakes roughly 200 times per
  second. The icon timer pauses at display off, but the battery timer (5 s),
  inactivity timer (1 s), and companion timer (250 ms when pairing) continue.
- Evidence: `code` (`src/FurbleUI.cpp:575-582`, `:748-775`, `:10174-10205`),
  `inferred` saving.
- Estimated saving: 0.5 to 4 mA with the screen off (`inferred`). If a wake
  costs 0.2 to 0.5 ms at 40 mA, 200 wakes per second cost 1.6 to 4 mA before
  any useful work. This is exactly the term the 3.61 mA what-if does not model.
- Risk: medium. Input latency, the 120 ms panel dwell in the direction each
  panel requires (both directions on the ILI9342C boards), the connect timer
  cadence, and the watchdog feed margin must all be rechecked.
- Effort: small to medium.
- Required test: screen-off idle 30 minutes at 5, 20, and 50 ms; measure
  current, wake count from the profiler, button latency, and watchdog margin.
- Citation: `src/FurbleUI.cpp`; `plans/98-power-optimization-audit.md` action
  9; ESP-IDF tickless idle documentation.

#### PL-06 All boards: scan duty and passive scan

- Change: default `SCAN_MODE` to Balanced, and prototype passive scanning for
  reconnect-by-address, where scan responses are not needed.
- Current behavior: `SCAN_MODE` defaults to Full (100 percent duty) and the
  scan is always active (`setActiveScan(true)`). A camera left off can keep a
  flat 5 s reconnect retry scanning at full duty forever when
  `RECON_BACKOFF` is off, which is the default.
- Evidence: `code` (`Scan::start`, `Settings::scanModeEffective`), `datasheet`
  (radio RX peak), `model` (no scan state).
- Estimated saving: up to 20 to 40 mA while scanning (`inferred` from the
  93 mA S3 and 97.5 mA ESP32 RX peaks and the duty). The saving applies during
  discovery and reconnect attempts, not during a connection.
- Risk: low to medium. Passive scan can miss a camera that only answers scan
  requests. Reconnect by address should not need the response, but that is per
  vendor.
- Effort: small for the default, medium for passive.
- Required test: discovery time and success rate for each vendor mode at Full,
  Balanced, Low, and passive; current during a scan on the analyzer.
- Citation: `plans/implemented/08-scan-tuning.md`; ESP32 and ESP32-S3 datasheets radio
  current; NimBLE scan API.

#### PL-07 All boards: display inactivity and screen-off defaults

- Change: implemented. Fresh devices seed `INACTIVITY` to 60 s and
  `DISPLAY_OFF` to Off (mode 1) on every board. The Core no longer needs the
  Dim workaround because its IP5306 boost is held on for the session (plan 354).
  The 30-minute soak remains the verification gate.
- Current behavior: `INACTIVITY` seeds to 2 (60 s) and `DISPLAY_OFF` to 1 (Off)
  on an unset key on every board, the Core included. Existing stored choices
  are preserved. Dim keeps the APB lock and the MCU active; the model puts the S3
  at 50.6 mA dimmed, of which only 10.4 mA is the display.
- Evidence: `code` (`src/FurbleSettings.cpp`, `src/FurbleUI.cpp`),
  `model`, `plan`.
- Estimated saving: about 40 mA when the user forgets the device (`model`).
  The exact value depends on the board's backlight and panel. Blanking a
  StickC backlight alone removes about 28 to 33 mA (published measurement in
  the repo model).
- Risk: low to medium. A dark screen can look like a dead device. Keep wake on
  any input and a short first timeout.
- Effort: small, plus docs and screenshots.
- Required test: 30 minutes idle on each battery board, screen off at the
  threshold, wake on the first press, no missed input, current measured.
- Citation: `plans/implemented/12-display-off.md`, `plans/98-power-optimization-audit.md`;
  ST7735S v1.1 Table 3 page 30, ST7789V v1.3 Table 3 page 40, ST7789P3 v0.1
  Table 3 page 34, and ILI9342C V101, which has no supply current table at all.
  All four were read on 2026-11-26.

#### PL-08 All boards: tickless and RTOS idle tuning

- Change: evaluate `CONFIG_PM_RTOS_IDLE_OPT` and a lower `CONFIG_FREERTOS_HZ`
  (100 instead of 1000) on one board before changing the matrix. Keep
  `FREERTOS_IDLE_TIME_BEFORE_SLEEP=3` until the wake cost is measured.
- Current behavior: `CONFIG_PM_RTOS_IDLE_OPT` is unset on every board.
  `CONFIG_FREERTOS_HZ=1000` and `FREERTOS_IDLE_TIME_BEFORE_SLEEP=3` on every
  board except Waveshare, which disables tickless idle entirely. The UI loop
  is 5 ms, so the practical sleep window is short regardless.
- Evidence: `code` (sdkconfig files), `plan`.
- Estimated saving: below 0.2 mA (`inferred`). Tickless idle already
  suppresses the tick during sleep; the gain is in the awake overhead and the
  idle entry threshold, not in the tick rate alone.
- Risk: medium. Timing-sensitive code includes the GPS UART, the PM1 watchdog,
  the display dwell, and the BLE timers.
- Effort: medium. A config change touches every board and the CI matrix.
- Required test: 30 minute idle on one S3 and one ESP32 board at 1000 and
  100 Hz, PM_RTOS_IDLE_OPT on and off; check GPS NMEA, watchdog, display, and
  BLE.
- Citation: ESP-IDF Kconfig `PM_RTOS_IDLE_OPT`, `FREERTOS_HZ`,
  `FREERTOS_IDLE_TIME_BEFORE_SLEEP`; sdkconfig files.

#### PL-09 All boards: BLE TX power and 2M PHY

- Change: none recommended now. The default TX power is already selectable and
  the adaptive path exists. LE 2M PHY would shorten airtime but needs a
  NimBLE change and per-vendor testing.
- Current behavior: TX power defaults to P9 and adaptive TX defaults off. The
  shutter is a short two-write exchange, so the absolute energy is small. The
  connected floor dominates.
- Evidence: `code`, `model` (TX events are rare), `datasheet` (176 to 193 mA
  peak on S3).
- Estimated saving: below 0.5 mA average (`inferred`). A lower TX power saves
  only during the rare TX event. LE 2M PHY saves airtime on the same events.
- Risk: low for TX power, medium for 2M PHY (vendor compatibility).
- Effort: small for TX power, medium for 2M PHY.
- Required test: 1 hour connected idle and 100 shutter presses at P9, P6, and
  P3; range check at the intended distance; 2M PHY vendor matrix.
- Citation: `plans/implemented/11-adaptive-tx-power.md`; ESP32-S3 datasheet radio current.

#### PL-10 All boards: the charging `NO_LIGHT_SLEEP` lock

- Change: none recommended without a stated reason. The lock is deliberate.
- Current behavior: while the sampled battery state says charging, the UI holds
  `NO_LIGHT_SLEEP`, so the panel can turn off but the CPU cannot light sleep.
  The origin of the lock is not explained in the plan or the code comment.
- Evidence: `code` (`src/FurbleUI.cpp:9907-9913`), `plan` (13).
- Estimated saving: 0 battery runtime. The device is on external power while
  charging. Removing the lock would reduce USB input current and heat, not
  battery drain.
- Risk: medium. The lock may exist to keep charge-status I2C reads stable or to
  avoid a charge/display interaction. Removing it without a reason is a
  regression risk.
- Effort: small to test.
- Required test: 1 hour charging with the lock on and off; check charge status,
  charge current, panel behavior, and USB input current.
- Citation: `plans/implemented/13-auto-off-low-batt.md`; `src/FurbleUI.cpp`.

### 4.5 Network, headless, and developer profiles

#### NB-01 Waveshare: state the real power policy and measure the input

- Change: document that the node does not enter automatic light sleep because
  tickless idle is off, and that DFS is still enabled at runtime. If a fixed
  frequency is the real requirement, add an explicit PM lock or a board
  predicate. If not, leave it and remove the "fixed frequency" claim.
- Current behavior: `sdkconfig.waveshare-s3-eth` has
  `# CONFIG_FREERTOS_USE_TICKLESS_IDLE is not set` and
  `# CONFIG_PM_DFS_INIT_AUTO is not set`, while `Power::configure()` still calls
  `esp_pm_configure` with the stored max frequency and
  `light_sleep_enable = true`. Tickless off means no automatic light sleep. DFS
  can still scale the CPU because the runtime call enables it. Plan 42 says
  "hold a fixed APB and CPU frequency", which is not what the build does. The
  same file leaves `CONFIG_ESP_PHY_MAC_BB_PD` unset while the other S3 configs
  set it; that is a consistency fix, not a battery saving.
- Evidence: `code` (sdkconfig, `src/FurblePower.cpp:37-43`), `plan` (42).
- Estimated saving: 0 battery runtime. This is a correctness and latency item
  for a mains node.
- Risk: medium for latency if light sleep is ever enabled; low for DFS alone.
  The W5500 SPI is clocked at 20 MHz, well below the APB limit.
- Effort: small.
- Required test: `power stats` lock dump, `pm` config readback, MQTT ping
  latency over 1 hour idle, and a link soak with DFS on and off.
- Citation: `sdkconfig.waveshare-s3-eth`;
  `plans/implemented/42-waveshare-eth-node.md`; ESP-IDF power management documentation.

#### NB-02 Waveshare: W5500 PHY power-down

- Change: none recommended for a battery goal. If input power or heat becomes a
  requirement, evaluate the W5500 PHY power-down mode on link loss and on
  `Ethernet::stop()`.
- Current behavior: the W5500 is initialized and started at boot. There is no
  link-down power-down call and no explicit PHY reset or power-down in
  `Ethernet::stop()` beyond `esp_eth_stop()` and driver uninstall.
- Evidence: `code` (`src/FurbleEthernet.cpp`), `datasheet` (W5500 Ver 1.1.0,
  5.3 p. 59 and 5.4 p. 60).
- Estimated saving: 0 battery runtime, because the board has no battery. The
  datasheet has the figures: IDD1 132 mA in normal operation and IDD2 13 mA in
  PHY power down (5.3 p. 59), and 100M link 128 mA, 10M link 75 mA, un-link
  65 mA, 100M transmitting 132 mA, 10M transmitting 79 mA, power down 13 mA
  (5.4 p. 60). A full PHY power-down could therefore remove about 52 mA of
  un-link current, but that is chip current on a mains board, it drops the
  link, and no board-level measurement exists. No local measurement exists, so
  NB-02 stays a gate rather than a saving.
- Risk: medium. Powering the PHY down can drop the link and delay recovery.
- Effort: medium.
- Required test: 5 V input current with link up, link down, and PHY powered
  down; link recovery time after power-down, against the datasheet bounds: RSTn
  low at least 500 us (1.1 pin 37 p. 10), TRC minimum 500 us and PLL lock within
  1 ms of release (5.5.1 and Figure 22 p. 60), regulator wake 10 us (5.5.2
  p. 60).
- Citation: WIZnet W5500 datasheet Ver 1.1.0, 1.1 p. 10, 5.3 p. 59, 5.4 p. 60,
  5.5.1 and Figure 22 p. 60, 5.5.2 p. 60; `src/FurbleEthernet.cpp`;
  `plans/implemented/42-waveshare-eth-node.md`.

#### NB-03 StickS3 and headless: USB Serial/JTAG current and the USJ lock

- Change: none for shipping. Keep USB Serial/JTAG because it is the only
  flashing and console path on the StickS3. Treat the USB-attached state as a
  measurement constraint, not a power opportunity.
- Current behavior: `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG_ENABLED=y` and
  `CONFIG_USJ_NO_AUTO_LS_ON_CONNECTION=y`. While a host is attached, the USJ
  driver holds `ESP_PM_NO_LIGHT_SLEEP`, so the device cannot light sleep. When
  detached, the lock is released. The PHY current itself is not in the tree.
- Evidence: `code` (sdkconfig, ESP-IDF Kconfig), `datasheet` (ESP32-S3 USB
  Serial/JTAG).
- Estimated saving: below 0.1 mA when detached (`inferred`); more when
  attached, but attached is not a battery state. Disabling USJ would break
  flashing, so it is not a shipping change.
- Risk: high if disabled. The StickS3 has no USB-UART bridge.
- Effort: small for a bench-only build.
- Required test: release build, battery only, USB detached, analyzer; compare
  with a bench-only build that disables USJ. Never flash that build as a
  release.
- Citation: ESP-IDF Kconfig `USJ_NO_AUTO_LS_ON_CONNECTION`; ESP32-S3 Series
  Datasheet v2.2; sdkconfig files.

#### NB-04 Core USB debug: a measurement exclusion, not a target

- Change: none. Keep the profile for rescue and CI.
- Current behavior: the UART console holds `APB_FREQ_MAX` for the life of the
  console, which blocks light sleep. The debug fragment adds
  `CONFIG_PM_PROFILING`, FreeRTOS run time stats, and the LVGL sysmon and perf
  monitor. The single factory partition means no OTA.
- Evidence: `code` (`src/FurbleConsole.cpp:2838-2847`, `sdkconfig.debug`,
  `sdkconfig.m5stack-core-usb-debug`).
- Estimated saving: 0. This profile is not a release configuration. Its current
  is not a release current.
- Risk: n/a. Do not optimize it.
- Effort: none.
- Required test: none. Use it only to confirm that a measurement taken with a
  console is invalid.
- Citation: `sdkconfig.debug`; `sdkconfig.m5stack-core-usb-debug`;
  `src/FurbleConsole.cpp`.

#### NB-05 Headless: lengthen the 5 ms loop

- Change: raise the headless `vUITask` delay from 5 ms to 20 or 50 ms. The GPS
  service is already on a 1 s deadline and the console is polled by its own
  driver.
- Current behavior: `vUITask` calls `Platform::update()`,
  `UI::serviceRequests()`, a scan callback drain, and a 1 s GPS service on a
  5 ms loop. On a mains devkit that is only wake overhead.
- Evidence: `code` (`src/main.cpp:283-296`), `inferred` saving.
- Estimated saving: 0.1 to 0.3 mA on the devkit (`inferred`). It does not
  transfer to a battery board because those builds use `UI::task()`.
- Risk: low. Console response and MQTT keepalive must be checked.
- Effort: small.
- Required test: 1 hour idle, console command latency, MQTT keepalive, and
  current at 5, 20, and 50 ms.
- Citation: `src/main.cpp`; ESP32-S3 datasheet light sleep; ESP-IDF tickless
  idle.

#### NB-06 Waveshare and headless: PSRAM and model coverage

- Change: add the 8 MB octal PSRAM addend to the S3 light-sleep model and add
  per-board report selection. Do not use the S3 report for a non-S3 board.
- Current behavior: the simulator reads only the `esp32s3` anchors plus fixed
  PMIC and peripheral terms (52.47 uA and 3.5 uA). `board-currents.yaml` carries
  five board keys but the profiler does not consume them. The ESP32-S3 datasheet
  adds 140 uA for 8 MB 8-line PSRAM in light sleep, which the 240 uA MCU term
  does not include.
- Evidence: `code` (`sim/power_profiler.cpp`, `tools/power-model/README.md`),
  `datasheet` (ESP32-S3 Table 5-10 footnote).
- Estimated saving: 0 device current. This is a model correction, and it
  changes every S3 light-sleep estimate by about 0.14 mA.
- Risk: low. A model change can move CI baselines; update them deliberately.
- Effort: small.
- Required test: regenerate the nine baselines, run `compare.py`, and add a
  per-board report test.
- Citation: `tools/power-model/README.md`; `sim/power_profiler.cpp`; ESP32-S3
  Series Datasheet v2.2 Table 5-10.

## 5. Ranked lists

### 5.1 Safe now

These changes are local, do not change a shipped default, and reduce waste
without needing a new measurement to justify the direction. Each still needs
the listed test.

| Rank | ID | Change | Estimated saving | Gate |
| :--- | :--- | :--- | :--- | :--- |
| 1 | PM-03 | Rate-limit the StickS3 PM1 button poll to 50 ms | 40 to 150 uA inferred | Button and watchdog test on the attached S3 |
| 2 | PM-04 | Trim the AXP192 ADC channel mask | under 100 uA inferred | Battery page and charge-state test on an AXP192 board |
| 3 | NB-05 | Raise the headless UI loop to 50 ms | 0.1 to 0.3 mA inferred | Console and MQTT latency on the devkit |
| 4 | PM-05 | Clear the StickS3 status LED in console mode | 1 to 3 mA inferred, console only | Console mode LED and panel test |

PM-05 has the largest number in this table, but it only applies in console
mode, which is a development mode. It is still safe and free.

### 5.2 Needs measurement

Ordered by expected saving, not by readiness. The ID column names the catalog
row, the estimated saving column is the current best number, and the hardware
column is the dependency. Only the StickS3 is attached in this repo; every
other board needs hardware.

| Rank | ID | Change | Estimated saving | Hardware |
| :--- | :--- | :--- | :--- | :--- |
| 1 | PL-01 | `SLEEP_CONN` default after soak | about 40 mA connected, screen off | StickS3, attached |
| 2 | PM-06 | Core IP5306 boost keep-on (implemented, plan 354; hardware soak pending) | 20 to 60 mA screen off, floor unknown | Core |
| 3 | PL-07 | Display inactivity and Off defaults | about 40 mA when forgotten | all battery boards |
| 4 | PM-07 | Auto-off default | 25 to 84 mA when forgotten | all battery boards |
| 5 | PM-01 | StickS3 interval deep sleep | 70 to 87 percent of interval energy | StickS3, attached |
| 6 | PM-10 | AXP192 timed deep sleep | 25 to 50 mA to about 0.2 to 0.3 mA | StickC, Plus, Core2 |
| 7 | PL-03 | Release the APB lock on AXP192 backlight boards | 5 to 25 mA screen on | StickC, Plus, Core2 |
| 8 | PL-04 | StickS3 LEDC clock without the APB lock | 5 to 15 mA screen on | StickS3, attached |
| 9 | PL-06 | Balanced and passive scan | up to 20 to 40 mA while scanning | all boards |
| 10 | RL-01 | GPS duty rework and Battery Saver bundle | 1.5 mA now, up to 19 mA with rework | any Grove GPS board |
| 11 | PM-09 | Core deep sleep with keep-on | 1 to 3 mA floor, unknown | Core |
| 12 | RL-11 | Accel-only low power on the IMU | 0.68 to 2.75 mA while IMU on | S3, StickC, Plus, Core2 |
| 13 | PL-02 | `CONN_SAVER` default | 0.5 to 2 mA connected | Sticks and Core2 |
| 14 | PL-05 | Screen-off UI loop and timers | 0.5 to 4 mA screen off | StickS3 first |
| 15 | PM-08 | Core2 touch low-power mode | 0.5 to 2 mA screen off | Core2 |
| 16 | RL-02 | GPS TX back-power when the rail is off | 0 to 1 mA rail off | any Grove GPS board |
| 17 | RL-03 | StickS3 IR and EXT_5V rail ownership | 0 to 1 mA IR receiver accounting | StickS3, attached |
| 18 | PL-08 | Tickless and RTOS idle tuning | below 0.2 mA | one S3 and one ESP32 board |
| 19 | LK-01 | StickS3 GPIO13 pull-up | 0 to 73 uA while asserted | StickS3, attached |
| 20 | PM-02 | PM1 L1 and L2 characterization | no direct saving; resume trade | StickS3, attached |
| 21 | NB-01 | Waveshare sleep and DFS policy | 0 battery; latency item | Waveshare |
| 22 | PM-11 | Plus2 off-path gate | 0 direct; prevents a dead-looking device | Plus2 |
| 23 | NB-06 | PSRAM addend and per-board model coverage | 0 device; about 0.14 mA model correction | model only |

### 5.3 Rejected

These are real opportunities that do not survive the evidence. The reason is
given so the next reader does not reopen them without new facts.

| ID | Change | Current behavior and why rejected | Estimated saving | Risk, effort, test | Evidence |
| :--- | :--- | :--- | :--- | :--- | :--- |
| PM-12 | Add an immediate software power off to the Core | The IP5306 has no shutdown command, so the only cut is the light-load one: the manual Off clears the keep-on bit and deep sleeps, and the boost goes 64 s later. An immediate off would need a hardware change or a different PMIC | 0 | n/a (rejected) | `code`, IP5306 register document V1.21 |
| PM-13 | Feed the PM1 watchdog every 5 s instead of every 1 s | Saves only a few microamps; the 45 s timeout already has a 45x margin and the feed shares the button-poll bus | 0 | n/a (rejected) | `code`, M5PM1 `WDT_CNT` |
| PM-14 | Change the AXP192 or IP5306 charge current for runtime | Charge current does not affect discharge runtime; it only changes charge time and heat | 0 | n/a (rejected) | `code`, datasheets |
| PM-15 | Use the AXP192 coulomb counter for a better gauge | Accuracy only, no energy saving | 0 | n/a (rejected) | `code`, AXP192 datasheet |
| PM-16 | Use the PM1 L1 or L2 state to save energy versus off | Off is 14.02 uA; L1 is 52.47 uA and L2 is 102.40 uA. The only possible win is resume time | 0 | n/a (rejected) | `vendor-doc` |
| RL-04 | Cut Core2 LDO2 when the panel sleeps | LDO2 powers the panel and SD; cutting it removes both | 0 | n/a (rejected) | `code`, M5Unified |
| RL-05 | Cut StickC or Plus LDO3 when the panel sleeps | LDO3 powers the LCD logic; cutting it loses the panel state | 0 | n/a (rejected) | `code`, M5Unified |
| RL-06 | Cut SD power when logging is off | No load switch on either Core SD slot | 0 | n/a (rejected) | `code`, board docs |
| RL-07 | Optimize Waveshare peripherals for runtime | No battery; mains node | 0 | n/a (rejected) | `code`, board docs |
| RL-08 | Change the speaker, vibration, IR, or LED event brackets | Already opened only for the event and closed after | 0 | n/a (rejected) | `code` |
| RL-09 | Change the StickS3 codec or amplifier policy | The speaker is already ended after init and after each pattern; the remaining current is unmeasured but the policy is correct | 0 | n/a (rejected) | `code` |
| RL-10 | Remove the Plus2 battery divider | Board fixture, no firmware control | 0 | n/a (rejected) | `code`, schematic needed |
| LK-02 | Make the StickC MPU6886 interrupt push-pull | The SYS_INT net is shared with the BM8563 RTC and is open-drain; push-pull can short the RTC | 0 | n/a (rejected) | `code` |
| LK-03 | Drive every IR pin low at boot | The driven stage polarity is a profile fact on the StickC family (plan 350): the StickC and Plus are active low and the Plus2 is active high, so one blanket level would key one family. The StickS3 polarity is still unverified, and the other boards show no measured leakage | 0 | n/a (rejected) | `code` |
| LK-04 | Disable internal pulls during sleep now | Deep sleep is unused; the change can float a wake pin | 0 | n/a (rejected) | `code`, sdkconfig |
| PL-09 | Change TX power or add 2M PHY as a power project | The average saving is below 0.5 mA; the connected floor dominates | 0 | n/a (rejected) | `code`, `model` |
| PL-10 | Remove the charging `NO_LIGHT_SLEEP` lock | External power, no battery runtime; the lock's origin is undocumented | 0 | n/a (rejected) | `code` |
| NB-02 | Power down the W5500 PHY | No battery; the link is the node's job | 0 | n/a (rejected) | `code`, W5500 datasheet v1.1.0 |
| NB-03 | Disable USB Serial/JTAG in release | It is the only flashing and console path on the StickS3 | 0 | n/a (rejected) | `code`, sdkconfig |
| NB-04 | Optimize the Core USB debug profile | Developer rescue profile, UART console defeats light sleep by design | 0 | n/a (rejected) | `code` |

## 6. Measurement plan

This is the board-lane input to the coordinator `measurement-plan.md`. Where
the two overlap, the coordinator file owns the shared instrument and scenario
details; this section keeps the board-specific boundary, logging, and state
constraints.

### 6.1 Boundary and instrument

Use one electrical boundary per claim and state it in the report.

- Battery boundary: an inline analyzer between the cell and the board, with the
  charger disabled. This is the only boundary that can support a battery
  runtime claim or a light-sleep floor claim.
- USB boundary: an inline analyzer on the 5 V USB input, with the battery
  present or absent as stated. This is valid for active and screen-on states,
  and invalid for comparing against the battery boundary.
- For the StickS3, the battery boundary is the only one that can show the
  3.3 mA floor, because USB Serial/JTAG holds `NO_LIGHT_SLEEP` while attached.
- For the Core, the IP5306 boost is held on for the session, so a run cannot
  end in a light-load cutoff. Verify the keep-on readback in the boot log
  before trusting a below-45 mA state.

Record the analyzer model, firmware, range, sample rate, supply voltage, cable,
battery presence, USB state, room temperature, and the exact build hash. Use a
calibrated analyzer with at least 1 kS/s and a range that resolves the expected
current. Retain raw samples, not only a displayed average.

### 6.2 Firmware and logging constraints

This is the part the existing plan 111 protocol does not resolve.

- The console is compiled only into console builds (`FURBLE_CONSOLE`): the
  `-debug` envs, the `esp32-s3-headless` env, and the `waveshare-s3-eth` env,
  which extends headless. On the ESP32 boards, the console holds
  `ESP_PM_APB_FREQ_MAX` for its lifetime, which blocks light sleep. On the
  StickS3, headless, and Waveshare, the console uses USB Serial/JTAG, and the
  USJ connection holds `NO_LIGHT_SLEEP`. So `power log` cannot measure the
  light-sleep states it is meant to validate, on any board.
- Debug builds also add `CONFIG_PM_PROFILING`, FreeRTOS run time stats, and the
  LVGL sysmon and perf monitor. Those change display work and timing.
- Therefore split the work:
  1. Current runs on a release build with a fixed, scripted state sequence and
     no USB attached where the state allows it.
  2. State and lock evidence on a debug build, reported separately and never
     mixed into the current table.
  3. If a release run needs state markers, use a bench-only build that writes
     markers to RTC memory or NVS and is read after the run, or a spare GPIO
     toggled at each transition. Do not use the USB console for a light-sleep
     current run.
- The simulator reports are `model`, not measurement. `compare.py` is a
  regression guard, not a calibration.

### 6.3 State matrix

| State | Setup | Boards | What it answers |
| :--- | :--- | :--- | :--- |
| B1 | Battery, release build, menu idle, screen on, default brightness | all battery boards | Screen-on floor and the APB lock cost |
| B2 | Battery, panel off, disconnected | all battery boards | Panel sleep and housekeeping cost |
| B3 | Battery, connected, screen on | all battery boards | Connected floor at stock parameters |
| B4 | Battery, connected, screen off, `SLEEP_CONN` off | StickS3 | The 43.6 mA what-if |
| B5 | Battery, connected, screen off, `SLEEP_CONN` on | StickS3 | The 3.61 mA what-if and the real floor |
| B6 | Battery, connected, screen off, `CONN_SAVER` on | StickS3, ESP32 boards if supported | The interval-scaled radio term |
| B7 | Battery, GPS always-on, connected | any Grove GPS board | GPS always-on term |
| B8 | Battery, GPS `$PCAS12` standby at 5, 10, 15, and 60 s | any Grove GPS board | Standby current and fix freshness |
| B9 | Battery, scan Full, Balanced, Low, passive | all boards | Scan duty and passive scan |
| B10 | Battery, PMIC off | AXP192 boards and StickS3 | The 27 uA and 14.02 uA vendor figures |
| B11 | Battery, ESP deep sleep, timer wake | StickC, Plus, Core2, Core | Deep-sleep floor with the PMIC on |
| B12 | Battery, screen off, IP5306 keep-on off and on | Core | The 45 mA cutoff and the keep-on floor |
| B13 | USB 5 V, charge on and off | all battery boards | Charge current and the charging lock |
| B14 | USB 5 V, link up and link down | Waveshare | W5500 and board input current |
| B15 | USB 5 V, USB attached and detached | StickS3 | The USJ lock and PHY term |
| B16 | Battery, panel off, touch active and idle | Core2 | Touch controller term |

### 6.4 Procedure

1. Flash the release build for the board under test. Record the build hash and
   the exact settings. For a state that needs a console, use the debug build
   only for state and lock observation, not for current.
2. Disable charging at the PMIC for a battery-boundary run. AXP192: clear the
   charge enable bit in `0x33`. IP5306: clear the charge enable bit in `0x00`.
   M5PM1: clear `PWR_CFG` bit 0. Re-enable it after the run. Record the
   register value before and after.
3. Reach the state and let it settle for at least 60 s. For a display state,
   wait through the 120 ms panel dwell in each direction the panel requires,
   then confirm the panel is in the intended state.
4. Capture at least 10 minutes per state, three repeats, with the analyzer and
   the state marker aligned. Reject a run that changes state, reconnects,
   charges, or receives a touch it did not intend.
5. Report mean, median, p95, wake-event energy, and a confidence interval.
   Report the analyzer burden and the battery voltage window. Do not compare
   runs at different battery voltages without noting the boost efficiency
   change.
6. For each state, record the PM lock counts and the actual PM configuration
   from the debug run. A current number without its lock state is not usable.
7. Keep the raw traces and the firmware log with the report. A number without
   the trace is not evidence.

### 6.5 Acceptance gates

- Do not change a default until the relevant state is measured on the named
  board at the battery boundary, with the charger disabled.
- A model-to-bench gap larger than the model's stated tolerance is a model bug
  first, not a firmware win. Update the model and the baselines before ranking.
- For `SLEEP_CONN`, require a stable state B5 for at least 30 minutes, no
  disconnects, and an acceptable first shutter after a 30 s idle gap, on every
  vendor that will be claimed. A Fujifilm-only soak cannot establish a default
  for the other vendors.
- For PM-06, require that the Core stays alive for 30 minutes below 45 mA with
  the boost held on, and that the manual off path still removes power.
- For PL-03 and PL-04, require no panel corruption, no touch loss, and no
  flicker over a 1 hour soak before quoting a saving.
- For PM-01 and PM-10, require the overnight frame count to match the
  configured count and the EXIF spacing to stay within the stated tolerance.
- For any off-state or deep-sleep claim, require the analyzer's own offset and
  resolution to be reported. A 14 uA claim from a 1 mA range is not a
  measurement.

### 6.6 What would change the ranking

- If the S3 light-sleep floor is dominated by wake overhead rather than the
  radio, PL-05 and PL-04 move above PL-02.
- If the Core boost quiescent current is above about 10 mA, PM-06 and PM-09
  drop out of the top ranks.
- If the AXP192 operating quiescent is near 2 mA as plan 98 suggests, PM-10
  loses most of its value. PL-03 still saves its screen-on delta, but the
  relative runtime gain shrinks and PM-07 (power off) becomes the largest win on
  those boards.
- If `$PCAS12` standby current is near 2 mA, the duty-cycle saving shrinks.
  Measure it before choosing a default; the current 1.5 mA baseline is a
  different mechanism.
- If the Core2 touch controller is below 100 uA when idle, PM-08 drops out.
- If the per-vendor soak shows link instability with `SLEEP_CONN` and
  `CONN_SAVER`, the two headline items become Battery Saver only and the
  default recommendation is withdrawn.

## 7. Datasheet register

This is the board-lane input to the coordinator `datasheet-register.md`. The
coordinator may merge duplicate parts from other lanes. The verification column
says whether the document was opened for this note. The repo model already
carries URLs and `datasheet` tags for the not-reopened rows. Do not treat a
not-reopened row as a new verification.

| Part | Document and revision | Register or limit used here | Verification |
| :--- | :--- | :--- | :--- |
| AXP192 | X-Powers AXP192 datasheet, enhanced single cell Li-battery PMIC | `REG 82H` ADC enable 1: bit 7 battery voltage, bit 6 battery current, bits 5 to 2 ACIN and VBUS, bit 1 APS, bit 0 TS. Default `0x83`. | Opened for this note. No revision string in the fetched copy. |
| AXP192 | same | `REG 83H` ADC enable 2: bit 7 internal temperature. Default `0x80`. | Opened. |
| AXP192 | same | `REG 84H` ADC sample rate, default `0x32` (25 Hz); M5Unified writes the same; `REG 12H` rail enables; `REG 28H` LDO2 and LDO3 voltage; `REG 27H` DCDC3; `REG 33H` charge enable, 4.2 V, current. | Opened. |
| AXP192 | same | Off mode 27 uA at BAT 3.8 V; suspend 86 uA; DCDC1 26 uA no load; DCDC2 and DCDC3 20 uA each; LDO2 and LDO3 100 uA each; LDOIO0 90 uA. | Opened. |
| IP5306 | Injoinic IP5306 datasheet V1.01 | Automatic load detection below 45 mA; 32 s at the power-on default of `SYS_CTL2` bits 3:2 = `01`; standby 50 uA; input quiescent 100 uA. | Opened. The public datasheet does not tabulate the register map. |
| IP5306 | Injoinic IP5306 register document V1.21, document revision V1.4 (M5Stack copy) | `SYS_CTL0` `0x00` bit 1 BOOST output normally open; `SYS_CTL2` `0x02` bits 3:2 light-load shutdown 8/32/16/64 s; `Charger_CTL1` `0x21` bits 7:6 battery-side stop-charge 200/400/500/600 mA; `CHG_DIG_CTL0` `0x24` charge current. | Opened. M5Unified writes `0x00 = 0x31` (bit 1 clear) and `0x02 = 0x6C` (64 s). |
| IP5306 | M5Unified 0.2.20 `Power_Class.cpp` (register writes and bitfield comments) and `IP5306_Class.cpp` (register constants and the keep-on setter) | `REG_SYS_CTL0` `0x00` bit 1 boost normally open; `REG_SYS_CTL2` `0x02` light-load time field, board init `0x02 = 0x6C` = bits 3:2 `11` = 64 s per the M5Unified bitfield comment; `REG_CHG_DIG_CTL0` `0x24` charge current. | Opened as vendor code, not a datasheet. |
| M5PM1 | M5Stack M5PM1 Chip User Manual V1.9 | `PWR_CFG` `0x06`: bit 4 LED, bit 3 5 V boost, bit 2 3.3 V LDO, bit 1 3.3 V DCDC, bit 0 charge. Default `0x17`. | Opened. Page 13. |
| M5PM1 | same | `I2C_CFG` `0x09`: `SLP_TO` bits 3:0, 0 disables idle sleep. The first transaction after a wake fails. | Opened. Page 15. |
| M5PM1 | same | Section 6: `SLEEP[3:0]` sets the idle sleep time, 0 disables. Idle sleep is invalid while PWM is enabled and while entering Download mode. | Opened. Pages 27 and 28. |
| M5PM1 | same | Section 8: IRQ Status 1/2/3 at `0x40`/`0x41`/`0x42`, masks at `0x43`/`0x44`/`0x45` where a set bit masks. The IRQ line is released only when all three status registers are cleared. The `0x2F` note ties WAKE_SRC to IRQ Status 3 bit 1, but the manual gives WAKE_SRC as `0x05` in Table 3 and as `0x04` on page 29. | Opened. Pages 9, 28 and 29. Address inconsistent inside V1.9. |
| M5PM1 | same | `WDT_CNT` `0x0A` seconds, 0 disables; `WDT_KEY` `0x0B` feed `0xA5`; `SYS_CMD` `0x0C` `0xA1` power off, `0xA2` restart, `0xA3` Download (`KEY` `0xA` plus `CMD`). | Opened. Page 15. The M5PM1 library 1.0.6 builds the same values from `M5PM1_SYS_CMD_KEY` `0xA0`. |
| M5PM1 | same | `HOLD_CFG` `0x07`: retain 5 V, LDO, and GPIO state after power off. | Opened. Page 13. |
| M5StickS3 low power page | https://docs.m5stack.com/en/arduino/m5sticks3/m5pm1, web page, no revision | After an idle-sleep wake, the first ESP32-S3 to M5PM1 communication only wakes the PMIC and fails; valid communication happens on the next transaction. | Opened. |
| M5PaperMono page | https://docs.m5stack.com/en/arduino/papermono/m5pm1_m5ioe1, web page, no revision | L1 to L3B all source from L0 (SYS_VBUS) rather than the preceding level, so each level is independent; after power on, L1, L2 and L3A are enabled automatically. | Opened. Used for the multi-level model, not as a StickS3 statement. |
| ESP32-S3 | Espressif ESP32-S3 Series Datasheet v2.2 | Table 5-10: light sleep 240 uA; deep sleep 7 uA with RTC memory, 8 uA with RTC peripherals; add 140 uA for 8 MB 8-line PSRAM at 3.3 V. | Opened. |
| ESP32-S3 | same | Internal weak pull-up `R_PU` 45 kOhm typical. | Opened. |
| ESP32-S3 | same | Table 5-8 radio peak: BLE TX 176 mA at 0 dBm, 193 mA at 9 dBm, RX 93 mA. | Opened. |
| ESP32 | Espressif ESP32 Series Datasheet v5.3, as recorded in `tools/power-model/README.md` | Table 4-2 and 5-4: light sleep 0.8 mA, deep sleep 10 uA, BLE TX 130 mA at 0 dBm at 50 percent duty. | Not reopened here. Repo model, datasheet-tagged. |
| ESP32 | Espressif ESP32 Technical Reference Manual v5.9, https://documentation.espressif.com/esp32_technical_reference_manual_en.pdf | 19.3.2 p. 312: UART clock is 80 MHz APB_CLK or REF_TICK, selected by `UART_TICK_REF_ALWAYS_ON` (1 APB, 0 REF_TICK). 7.2.4.2 and Table 7.2-5 p. 169: REF_TICK is derived from APB_CLK and its divider must be reprogrammed per CPU clock source. 30.2.1 and 30.2.2 p. 728: eight channels, each TX or RX but not both, sharing one 512 x 32-bit RAM in eight 64-word blocks. | Opened for this note, 2026-11-25. |
| ESP32-S3 | Espressif ESP32-S3 Technical Reference Manual v1.8, https://documentation.espressif.com/esp32-s3_technical_reference_manual_en.pdf | 26.3 and 26.4.1 p. 926: three UART Core sources, APB_CLK, RC_FAST_CLK and XTAL_CLK, by `UART_SCLK_SEL`; fractional divider 1 to 256. `UART_SCLK_SEL` p. 961: 1 APB, 2 RC_FAST, 3 XTAL. 37.2 p. 1415 and 37.3.2.2 p. 1417: four TX plus four RX channels sharing a 384 x 32-bit RAM in eight 48-word blocks. 33.3.2 p. 1248 and 33.3.5 p. 1249: RTS 1 with DTR 0 resets the SoC, SRST resets the digital system but not the controller. 33.4.2 p. 1253: needs the PLL and APB_CLK at 40 MHz or more, gated in light sleep, powered down in deep sleep. | Opened for this note, 2026-11-25. |
| ESP32-S3-PICO-1 | Espressif ESP32-S3-PICO-1 Series Datasheet v1.2 | Table 1-1 p. 9: N8R2 is 8 MB quad flash plus 2 MB quad PSRAM, N8R8 is 8 MB quad flash plus 8 MB octal PSRAM. Pin table note 4 p. 13: on N8R8, SPICS1 and GPIO33 to GPIO37 belong to the octal PSRAM and are unavailable. | Opened for this note, 2026-11-25. |
| ESP32-S3 module pins | Espressif ESP32-S3 Series Datasheet v2.2 | 2.3.5 p. 26 and Table 2-14 p. 31: GPIO33 to GPIO37 carry DQ4 to DQ7 and DQS/DM only in octal mode, and the in-package flash and PSRAM pins must not be used for anything else. | Opened for this note, 2026-11-25. |
| SX1261/2 | Semtech SX1261/2 datasheet DS.SX1261-2.W.APP Rev 2.2, December 2024, ECO 067443. The Semtech product page link is form and login gated, so the citable copies are https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1198/DS_SX1261_2_V2-2.pdf and https://wiki.lilygo.cc/datasheet/DS_SX1261_SX1262.pdf | 8.1 p. 53: hold NRESET low typically 100 us. 8.3.1 p. 55 and Table 8-2 p. 56: NSS rising to BUSY rising 600 ns maximum, BUSY held high through 20 kOhm in sleep; switching SLEEP to STBY_RC 3500 us cold and 340 us warm, STBY_RC to STBY_XOSC 31 us, to FS 50, to RX 83, to TX 126, STBY_XOSC to FS 40, to TX 105, to RX 62, FS to RX 41, to TX 76, RX to FS 15, to TX 92. Table 8-1 t10 p. 54: NSS falling to SCK 100 us minimum when leaving sleep. Section 15 p. 109 to 111: four known limitations. | Opened for this note, 2026-11-25. Not fitted on any supported board. |
| W5500 | WIZnet W5500 datasheet Ver 1.1.0, 17DEC2022, https://docs.wiznet.io/img/products/w5500/W5500_ds_v110e.pdf | 1.1 pin 37 p. 10: hold RSTn low at least 500 us. 5.5.1 and Figure 22 p. 60: TRC minimum 500 us, TPL RSTn to internal PLOCK maximum 1 ms. 5.5.2 p. 60: regulator wake 10 us. 3.1 p. 29, 4.1 p. 32 and SHAR `0x0009` to `0x000E` p. 33. 5.3 p. 59: IDD1 132 mA, IDD2 13 mA in PHY power down. 5.4 p. 60: 100M link 128 mA, 10M link 75 mA, un-link 65 mA. Revision history p. 66 to 67: v1.1.0 changed only the RPU minimum and the package text. | Opened for this note, 2026-11-25. The corpus link to `W5500_ds_v1.0.6.pdf` is a 404; 1.1.0 is the cited revision. |
| ST7789V | Sitronix ST7789V datasheet v1.3, 2014/03, 316 pages, sha256 8ecf0e43 | Table 3 page 40: normal 6.0 mA typical and 7.5 mA max, sleep-in 15 uA typical and 30 uA max. Table 9 page 48 reset timing. | Opened 2026-11-26 at pages 40, 48, 182 and 184. |
| ST7789P3 | Sitronix ST7789P3 datasheet v0.1, 2022/03, 276 pages, sha256 26e62a24 | Table 3 page 34: normal 6.0 mA typical and 7.5 mA max, partial plus idle 5.0 mA typical and 6.0 mA max, sleep-in 20 uA typical and 50 uA max. Table 8 page 41 reset timing, TRW 10 us minimum and TRT 120 ms maximum. | Opened 2026-11-26 at pages 34, 41, 143 and 145. This is the StickS3 panel. |
| ST7796S | Sitronix ST7796S datasheet v1.0, 2014/11, 279 pages, sha256 212f7a8e | Sleep in and out on pages 159 and 160: 5 ms after SLPIN before any other command, and 120 ms from SLPOUT to the next SLPIN only. | Opened 2026-11-26. |
| ST7796U | Sitronix ST7796U datasheet v1.0, 2020/02, 287 pages, sha256 c907c64e | Same dwell rows on pages 163 and 164. The host file is named `ST7796S_spec.pdf`, the cover says ST7796U. | Opened 2026-11-26. |
| ILI9342C | ILITEK ILI9342C datasheet V101, document ILI9342C_DTS_V101_20111214, 235 pages and complete, sha256 604a35c0 | No supply current table. Section 18.2 page 227 gives only plus or minus 0.1 uA logic input leakage. Section 8.2.11 and 8.2.12, pages 100 and 101, require 120 ms in both directions. | Opened 2026-11-26 at pages 100, 101, 227 and 235. |
| CO5300 | Chipone CO5300 datasheet V0.01, 2023/07/24, 226 pages, sha256 2ad0461a | Page 103: deep stand-by 2 uA VDDI plus 1 uA VCI, Sleep In 110 uA plus 25 uA on DSI LP and 85 uA plus 25 uA on DSI ULP. | Opened 2026-11-26. |
| SH1106 | Sino Wealth SH1106 datasheet V2.3, 49 pages, sha256 ab5ce5f3 | Page 35: sleep current 5 mA maximum on both the VDD1 and VDD2 row and the external VPP row at TA 25 C. | Opened 2026-11-26 at pages 35 and 49. |
| CST9217 | Hynitron CST9217 datasheet V1.0, 2022-08-11, 10 pages, sha256 74cf16e6 | Page 5: 1.3 mA at 100 Hz, 12 uA monitor at 30 Hz, 2 uA sleep. Page 9 reset timing. No register map. | Opened 2026-11-26, all ten pages. |
| ST7735S | Sitronix ST7735S datasheet v1.1, 2011-11-21, 201 pages, sha256 0322a527 | Table 3 page 30: normal 0.9 mA typical and 2.0 mA max, sleep-in 15 uA typical and 30 uA max. | Opened 2026-11-26 at page 30. |
| BMI270 | Bosch BST-BMI270-DS000-08 | `PWR_CTRL` `0x7D`, `PWR_CONF` `0x7C`, `ACC_CONF` `0x40`. Normal accel and gyro 685 uA; suspend 3.5 uA; accel low power 4 to 10 uA. | Not reopened here. Repo model, datasheet-tagged. |
| MPU6886 | TDK MPU-6886 datasheet v1.1 | `PWR_MGMT_1` `0x6B`, `PWR_MGMT_2` `0x6C`. Six-axis 2.79 mA typical; sleep 6 uA; accel low power 40 uA. | Not reopened here. Repo model, datasheet-tagged. |
| AT6668 | CASIC AT6668 datasheet DS-AT6668 V1.4 | 37 mA typical tracking with BDS, GPS and GLONASS at 3.3 V; 8 uA battery backup; 30 uA sleep with ON_OFF low; 2.5 mA antenna detect; 45 to 65 mA antenna short-circuit limit; 160 ms reset; 26 MHz TCXO. Sections 1.2 and 4.3, pages 1 and 9. | Opened on 2026-11-25; the per-fact record is on `docs/components/at6668.md`. |
| ATGM336H | ATGM336H module manual | Module current below 25 mA typical, 100 mA peak excluding antenna; VBAT 10 uA. | Not reopened here. Repo model, datasheet-tagged. |
| NS4168 | NS4168 datasheet | Quiescent 13 mA at 5 V; shutdown 1 uA. | Not reopened here. Repo model, datasheet-tagged. |
| W5500 | WIZnet W5500 datasheet v1.1.0, dated 17DEC2022 | `RSTn` low at least 500 us, `TPL` up to 1 ms from release to internal PLOCK; `IDD1` 132 mA; `IDD2` 13 mA in PHY power-down; link currents 128, 75 and 65 mA. | Opened for this change. Not a local measurement, and no post-release `TPL` wait is implemented. |

Note on revisions. The AXP192 copy fetched for this note carries no revision
string. Its cover says "X-Powers, 2010"; the PDF metadata dates it 2011-08-08
and the copy is 53 pages. Use those identifiers, not a revision number. The
IP5306 copy is V1.01. The M5PM1 copy is V1.9. The ESP32-S3 copy is v2.2. The
W5500 copy is v1.1.0, dated 17DEC2022; it replaces the v1.0.6 URL this note
carried, which now returns HTTP 404, and the v1.1.0 history on pages 66 to 67
changes only the RPU minimum and the package text. If a future review needs a
register claim from the not-reopened rows, open the exact revision first; do
not copy a number from this table as if it were re-verified.

## 8. Model corrections and open questions

These are not new opportunities. They are the reasons the opportunities above
cannot be ranked by the current simulator alone.

1. The simulator is S3 only. `board-currents.yaml` has five board keys, but the
   profiler reads the `esp32s3` anchors, a fixed 52.47 uA PMIC term, and a fixed
   3.5 uA peripheral term. There is no per-board report and no per-board PMIC,
   PSRAM, or board rail term.
2. The S3 light-sleep MCU term is 240 uA and omits the 140 uA 8 MB PSRAM
   addend. The datasheet MCU term is 380 uA, so the MCU term is about 37 percent
   low. With the fixed 52.47 uA PMIC term, the full light-sleep state is about
   292 uA against a datasheet-based 432 uA, about 32 percent low.
3. The model charges a constant connected floor, so it cannot see the
   `CONN_SAVER` interval change. The 3.61 mA what-if is therefore a lower
   bound on the parameter-dependent floor, not a prediction of the idle
   interval case.
4. The model does not charge wake overhead. The UI loop's 5 ms delay bounds
   light sleep to about 2 ms, so a 3.61 mA average is optimistic if each wake
   costs 0.2 to 0.5 ms of active time.
5. The model does not charge the GPS cold start, the `$PCAS12` standby current,
   or the rail-off leakage. A duty-cycle result can look better than it is.
6. The model charges a fixed 52.47 uA PMIC term (the S3 L1 figure) on every
   board and a fixed 3.5 uA peripheral term. It does not use the per-board
   AXP192 or IP5306 values, the PM1 off or L2 values, the SD card standby, the
   touch controller, the IR receiver, or the IMU normal-mode current.
7. The model has no scan state, no display brightness term, and no per-board
   backlight term.
8. The model's 3.3 mA connected floor is tagged `measured-local` for the stock
   parameters. It is not the board floor for the `CONN_SAVER` parameters, and
   the measurement conditions and the trace are not recorded in plan 00. Treat
   its provenance as incomplete.

Open questions that block a default change:

- What is the StickS3 light-sleep floor at the `CONN_SAVER` idle parameters,
  measured at the battery boundary with USB detached? The 3.3 mA figure is for
  the stock parameters, and its original run is not in the tree.
- What is the actual PM1 `SLP_TO` value after `M5.begin()` and after furble's
  `m_M5PM1.begin()`? Plan 26 still owes this readback. If it is zero, the
  retry path is defensive and the 200 Hz button poll is the real cost.
- What is the AXP192 operating quiescent current with the rails furble actually
  enables? The datasheet block sum is about 316 uA, not the 2 mA in plan 98.
- What is the IP5306 boost quiescent current with the keep-on bit set? This
  decides whether PM-06 and PM-09 are real.
- What is the `$PCAS12` in-circuit standby current? The model uses 0.5 mA with
  a 10 uA to 2 mA range.
- What is the green LED current on the StickS3, and what is the IR receiver
  current when the 5 V boost is on?
- What is the Plus2 `powerOff()` behavior? M5Unified 0.2.20 reaches
  `esp_deep_sleep_start()` with no wake source on that path.
- What is the Core2 touch controller current in active and low-power modes?
- What are the StickC GPIO35 and Plus2 battery divider resistor values?

## 9. References

### Repo

- `plans/00-hardware-experiments.md`: the only recorded local measurements
  (crystal and GPS experiments). The 3.3 mA StickS3 floor is cited from this
  plan by other documents, but its drain row is not present here.
- `plans/implemented/06-power-module.md`, `plans/implemented/07-ble-sleep.md`,
  `plans/implemented/08-scan-tuning.md`, `plans/implemented/10-adaptive-conn-params.md`,
  `plans/implemented/11-adaptive-tx-power.md`, `plans/implemented/12-display-off.md`,
  `plans/implemented/13-auto-off-low-batt.md`, `plans/implemented/15-gps-power.md`,
  `plans/implemented/19-interval-deep-sleep.md`, `plans/implemented/26-pm1-watchdog.md`,
  `plans/implemented/42-waveshare-eth-node.md`, `plans/63-sim-power-analysis.md`,
  `plans/implemented/77-battery-saver-profile.md`,
  `plans/95-engineering-lessons.md`, `plans/98-power-optimization-audit.md`,
  `plans/implemented/98b-power-followup.md`, `plans/111-power-recommendations.md`,
  `plans/implemented/143-pm1-watchdog-window.md`.
- `tools/power-model/board-currents.yaml` and `tools/power-model/README.md`.
- `src/FurblePower.cpp`, `src/FurblePlatform.cpp`, `src/FurbleGPS.cpp`,
  `src/FurbleUI.cpp`, `src/FurbleControl.cpp`, `src/FurbleSD.cpp`,
  `src/FurbleIMU.cpp`, `src/FurbleIR.cpp`, `src/FurbleFeedback.cpp`,
  `src/FurbleEthernet.cpp`, `src/main.cpp`, `src/FurbleConsole.cpp`.
- `sdkconfig.m5stick-c`, `sdkconfig.m5stick-c-plus`, `sdkconfig.m5stick-s3`,
  `sdkconfig.m5stack-core`, `sdkconfig.m5stack-core2`,
  `sdkconfig.waveshare-s3-eth`, `sdkconfig.esp32-s3-headless`,
  `sdkconfig.debug`, `sdkconfig.m5stack-core-usb-debug`.

### Vendor code at the pinned versions

- M5Unified 0.2.20, `Power_Class.cpp`, `AXP192_Class.cpp`,
  `IP5306_Class.cpp`, `M5PM1_Class.cpp`, `M5Unified.cpp`:
  https://github.com/m5stack/M5Unified/tree/0.2.20/src/utility
- M5GFX 0.2.27, `src/M5GFX.cpp`:
  https://github.com/m5stack/M5GFX/blob/0.2.27/src/M5GFX.cpp
- M5PM1 1.0.6:
  https://github.com/m5stack/M5PM1

### Datasheets and vendor pages

- AXP192:
  http://images.shoutwiki.com/mindworks/8/8b/2020_infrasonic_wildfire_detector_APX192_Enhanced_Single_Cell_datasheet_en.pdf
- IP5306: https://www.laskakit.cz/user/related_files/ip5306.pdf
- M5PM1: https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1207/M5PM1_Datasheet_EN.pdf
- ESP32-S3: https://documentation.espressif.com/esp32-s3_datasheet_en.pdf
- ESP32: https://documentation.espressif.com/esp32_datasheet_en.pdf
- ST7789V: https://newhavendisplay.com/content/datasheets/ST7789V.pdf
- ST7735S: https://cdn.sparkfun.com/assets/9/0/2/5/8/ST7735S_v1.1.pdf
- BMI270:
  https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmi270-ds000.pdf
- MPU6886:
  https://m5stack.oss-cn-shenzhen.aliyuncs.com/resource/docs/datasheet/core/MPU-6886-000193%2Bv1.1_GHIC_en.pdf
- AT6668: https://cdn.icstop.com/upload/pdfs/ca/27/ca27336d2c2d0967e811278e3e53824a.pdf
- ATGM336H:
  https://github.com/Albresky/GPS-ATGM336H-Library/blob/master/manual/C90770_ATGM336H-5N31_2016-12-26.pdf
- NS4168: https://www.chipsourcetek.com/Uploads/file/NS4168.pdf
- W5500, datasheet Ver 1.1.0 (the corpus v1.0.6 link is a 404, checked
  2026-11-25):
  https://docs.wiznet.io/img/products/w5500/W5500_ds_v110e.pdf
- ESP32 Technical Reference Manual v5.9:
  https://documentation.espressif.com/esp32_technical_reference_manual_en.pdf
- ESP32-S3 Technical Reference Manual v1.8:
  https://documentation.espressif.com/esp32-s3_technical_reference_manual_en.pdf
- ESP32-S3-PICO-1 Series Datasheet v1.2:
  https://documentation.espressif.com/esp32-s3-pico-1_datasheet_en.pdf
- SX1261/2 datasheet Rev 2.2, DS.SX1261-2.W.APP (the Semtech product page link
  is form and login gated, so these byte-identical mirrors are cited):
  https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1198/DS_SX1261_2_V2-2.pdf and
  https://wiki.lilygo.cc/datasheet/DS_SX1261_SX1262.pdf
- M5Stack StickS3: https://docs.m5stack.com/en/core/StickS3
- M5Stack Unit GPS v1.1:
  https://docs.m5stack.com/en/unit/Unit-GPS%20v1.1
- Waveshare ESP32-S3-ETH: https://www.waveshare.com/wiki/ESP32-S3-ETH
- Waveshare schematic:
  https://files.waveshare.com/wiki/ESP32-S3-ETH/ESP32-S3-ETH-Schematic.pdf

### ESP-IDF

- Power management mode mapping:
  `components/esp_pm/pm_impl.c` in the pinned framework.
- Kconfig `USJ_NO_AUTO_LS_ON_CONNECTION`:
  `components/esp_driver_usb_serial_jtag/Kconfig`.
- Kconfig `PM_RTOS_IDLE_OPT`, `FREERTOS_HZ`,
  `FREERTOS_IDLE_TIME_BEFORE_SLEEP`: the pinned framework Kconfig.
