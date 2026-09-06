# Per-board current model

`board-currents.yaml` is the machine readable input for the plan 63 simulation
energy model. It holds one table per release board with subsections
`mcu`, `radio`, `display`, `gps`, `pmic`, `peripherals`. Every entry is
`{value_ma, min, max, source, confidence}` plus an optional `note`. All values
are milliamps unless the key name says otherwise (`cold_start_refix_s` carries
seconds). The `_shared` block holds YAML anchors; consumers should read the
five keys under `boards:` and let the loader expand aliases.

## Confidence tags

- `datasheet`: read from the primary component datasheet. The cited URL was
  fetched during compilation and the number verified present.
- `published-measurement`: vendor published spec table or a methodical third
  party measurement.
- `estimated`: derived, interpolated, or reasoned from an analogous part.
  The note or this README carries the reasoning.
- `measured-local`: measured on this project's hardware. Recorded in
  `plans/00-hardware-experiments.md`, which is ground truth and overrides any
  external source on conflict.

## How each family of numbers was obtained

### MCU: ESP32 (M5StickC, C Plus, Core, Core2)

ESP32 Series Datasheet v5.3, Tables 4-2 and 5-4:
https://documentation.espressif.com/esp32_datasheet_en.pdf

The datasheet has no separate "active, RF off" table. Table 4-2 calls the
CPU-running RF-off state "Modem-sleep" and gives a range per frequency: the
low end is all peripheral clocks disabled, the high end all enabled. Those
ranges land in `min`/`max` and `value_ma` is the midpoint. Dual core figures
apply (PICO-D4 and D0WD are dual core). Light sleep 0.8 mA, deep sleep with
RTC timer plus RTC memory 10 uA, hibernation 5 uA, all from the same table.

### MCU: ESP32-S3 (M5StickS3)

ESP32-S3 Series Datasheet v2.2, Tables 5-8 to 5-10:
https://documentation.espressif.com/esp32-s3_datasheet_en.pdf

`active_cpu_*` rows are dual core running 32 bit access; `idle_waiti_*` rows
are both cores in WAITI. `min` is Typ1 (all peripheral clocks disabled),
`max` is Typ2 (all enabled), `value_ma` the midpoint. The FN8 variant has no
PSRAM, so the datasheet PSRAM adders do not apply. Flash access adds about
10 mA (SPI 2-line at 80 Mbit/s) on top of these rows. Light sleep 240 uA,
deep sleep 7 uA (RTC memory only) to 8 uA (plus RTC peripherals).

### Radio

ESP32 classic (Table 5-4): BLE TX at 0 dBm is 130 mA typ at 50 percent TX
duty; BT/BLE RX is 95 to 100 mA. The datasheet lists no 3, 6, or 9 dBm rows,
so those are `estimated` by linear interpolation using the slope of the
ESP32-S3 table (176 mA at 0 dBm to 193 mA at 9 dBm, about 1.9 mA per dB).

ESP32-S3 (Table 5-8): whole chip peak currents at 100 percent TX duty.
0 dBm 176 mA, 9 dBm 193 mA, RX 93 mA. The 3 and 6 dBm entries are linear
interpolation between the two datasheet rows, tagged `estimated`. Note the
two chips' datasheet radio figures use different duty cycle conventions and
are not directly comparable.

Average connected currents come from the Espressif NimBLE power_save example
README (measured, connection interval not stated):
https://github.com/espressif/esp-idf/tree/master/examples/bluetooth/nimble/power_save
ESP32 modem sleep 14.1 mA, light sleep with 32 kHz crystal 1.9 mA. ESP32-S3
modem sleep 17.9 mA, light sleep from main XTAL 3.3 mA. The S3 main XTAL
figure matches the locally measured 3.3 mA connected idle floor exactly
(`plans/00-hardware-experiments.md`); the 230 uA 32 kHz mode is impossible on
the StickS3 because the board has no external 32.768 kHz crystal, which was
proven locally by the BLE_INIT boot log.

No Espressif per-connection-event charge figure exists in the primary
sources. As a methodology reference for event-level modelling: Nordic's
online power profiler validation measured 14.022 uC per BLE advertising
event (0 dBm, 31 byte payload, 3.0 V, nRF52832, Agilent N6705B):
https://devzone.nordicsemi.com/nordic/nordic-blog/b/blog/posts/nrf52-online-power-profiler
That is nRF52 silicon and must not be used as an ESP32 number.

### Displays

Panel controller currents exclude the backlight.

- ST7735S (StickC): Sitronix datasheet v1.1, Table 3, normal mode 0.9 mA typ
  / 2.0 mA max, sleep-in 15 uA typ / 30 uA max:
  https://cdn.sparkfun.com/assets/9/0/2/5/8/ST7735S_v1.1.pdf
- ST7789V (StickC Plus, StickS3): Sitronix datasheet v1.3, Table 3, normal
  mode black 6.0 mA typ / 7.5 mA max, sleep-in 15 uA typ / 30 uA max:
  https://newhavendisplay.com/content/datasheets/ST7789V.pdf
- ILI9342C (Core, Core2): the ILITEK datasheet
  (https://m5stack.oss-cn-shenzhen.aliyuncs.com/resource/docs/datasheet/core/ILI9342C-ILITEK.pdf)
  tabulates no supply currents at all, only voltages and leakage. Operating
  current is therefore `estimated` at 6 to 8 mA by analogy with the same
  generation, same pixel class ST7789V. Sleep-in is `estimated` from the
  ILI9341 proxy standby ceiling of 100 uA max
  (https://cdn-shop.adafruit.com/datasheets/ILI9341.pdf, page 236) with the
  ST7789V 15 uA as the low bound.

Backlights:

- StickC: measured. lang-ship measured a 28.2 mA delta from lowest to full
  brightness on the AXP192 LDO2 rail
  (https://lang-ship.com/blog/work/m5stickc-current-1/), the M5Stack forum
  discharge table shows 33 mA
  (https://community.m5stack.com/topic/1162/getting-longer-battery-run-time).
  Tagged `published-measurement`, 28 to 33 mA.
- StickC Plus and StickS3: no isolated measurement found anywhere. Estimated
  30 to 40 mA at full brightness: identical drive scheme (LDO2 or LEDC PWM),
  panel grows from 0.96 to 1.14 inch. StickS3 scales roughly linearly with
  LEDC duty.
- Core: estimated 40 to 60 mA. The 853 nit 2 inch 320x240 class typically
  stacks 3 to 4 white LEDs at about 15 mA each from 3.3 V behind the PWM
  transistor. Whole device forum measurements
  (https://community.m5stack.com/topic/163/power-consumption) are consistent
  with this bracket.
- Core2: same LED stack, fed from AXP192 DCDC3 and dimmed by voltage (2.4 to
  3.3 V maps to brightness 0 to 100 per the vendor driver,
  https://github.com/m5stack/M5Core2/blob/master/src/AXP192.cpp). Estimated
  40 to 60 mA at 3.3 V; the 2.8 V default draws noticeably less.

### GPS (M5Stack Unit GPS v1.1, ATGM336H, CASIC AT6668)

- Whole unit: M5Stack specs "DC 5V/31.64mA" at the Grove input, including the
  onboard regulator (https://docs.m5stack.com/en/unit/Unit-GPS%20v1.1).
- Module: the ATGM336H user manual gives one continuous figure, GPS+BDS
  typical below 25 mA at 3.3 V, peak 100 mA excluding the antenna, VBAT
  backup 10 uA. Verified against a byte-identical mirror of the LCSC hosted
  manual:
  https://github.com/Albresky/GPS-ATGM336H-Library/blob/master/manual/C90770_ATGM336H-5N31_2016-12-26.pdf
  There is no acquisition versus tracking split in the manual.
- Chip: no AT6668 datasheet is findable. The sibling AT6558 datasheet
  (https://dratek.cz/docs/produkty/1/1045/at6558.pdf) gives 23 mA typ
  BD+GPS tracking, standby below 10 uA, VBAT 10 uA typ / 40 uA max, active
  antenna feed 2.4/3.0/3.6 mA. Chip level numbers used as proxy are tagged
  `estimated`.
- $PCAS12 standby in circuit: `estimated` at 0.5 mA (10 uA to 2 mA). The
  chip drops below 10 uA but the module keeps its TCXO, flash, and the
  unit's regulator powered. Locally proven to work ($PCAS12,10 stops NMEA
  for 10 s), current not yet measured. A local measurement should replace
  this entry.
- Local ground truth: no backup supply on the unit, a 5 V rail cut costs a
  full cold start, measured 107.7 s re-fix (`plans/00-hardware-experiments.md`).

### PMICs

- AXP192 (StickC, C Plus, Core2): X-Powers datasheet (English translation,
  http://images.shoutwiki.com/mindworks/8/8b/2020_infrasonic_wildfire_detector_APX192_Enhanced_Single_Cell_datasheet_en.pdf).
  Off mode 27 uA at BAT 3.8 V. There is no whole chip operating quiescent
  figure; the practical floor is the sum of enabled blocks (DCDC1 26 uA,
  DCDC2/3 20 uA each no-load PFM, LDO2/LDO3 100 uA each, LDOIO0 90 uA), so
  operating quiescent is `estimated` at 150 to 300 uA.
- IP5306 (Core): Injoinic datasheet v1.01
  (https://www.laskakit.cz/user/related_files/ip5306.pdf). Standby 50 uA,
  input quiescent 100 uA. The important behavior: the boost converter shuts
  off after 32 s of continuous load below 45 mA. Any sub-45 mA sleep state
  on the Core gets hard powered off by the PMIC, so low power states must
  either keep the load above the threshold or accept power off.
- M5PM1 (StickS3): M5Stack publishes device level figures at 4.2 V
  (https://docs.m5stack.com/en/core/StickS3): power off 14.02 uA (matches
  the locally measured 14 uA timed-off), L1 52.47 uA, L2 102.40 uA, L3A
  36.69 mA, full load 519.02 mA. The page does not define the L state
  semantics and gives no chip-only quiescent figure.

### Peripherals

- BMI270 (StickS3): Bosch datasheet BST-BMI270-DS000-08
  (https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmi270-ds000.pdf).
  Normal accel+gyro 685 uA, accel-only low power 4 to 10 uA depending on
  ACC_CONF, suspend 3.5 uA. All typ at VDD 1.8 V.
- MPU-6886 (Core2): TDK datasheet
  (https://m5stack.oss-cn-shenzhen.aliyuncs.com/resource/docs/datasheet/core/MPU-6886-000193%2Bv1.1_GHIC_en.pdf).
  6-axis low noise 2.79 mA typ / 3.0 mA max, accel low power at 100 Hz 40 uA
  typ, sleep 6 uA typ. Table 3 is marked "target spec".
- Vibration motor (Core2): M5Stack confirms the AXP LDO3 rail but names no
  part and no current. Estimated from a representative 10x3 mm 3 V coin
  motor, Vybronics VC1030B028F, 64 mA typical operating, 85 mA rated max
  (https://www.vybronics.com/coin-vibration-motors/with-brushes/v-c1030b028f).
  Start and stall draw more.
- Speaker: Core2 uses an NS4168 class D amp, 13 mA quiescent at 5 V, 1 uA
  shutdown (https://www.chipsourcetek.com/Uploads/file/NS4168.pdf). Moderate
  volume into the 1 W speaker is estimated at 40 to 80 mA from 5 V:
  150 to 300 mW electrical at about 80 percent efficiency plus quiescent.
  Full 2.5 W output would be about 625 mA. The Core speaker estimate reuses
  the same arithmetic.
- Buzzer (StickC Plus): typical passive piezo figure, estimated 5 to 30 mA.
- IR LED (StickC family, StickS3): the published schematic pinmap confirms
  IR_LED on GPIO9
  (https://m5stack.oss-cn-shenzhen.aliyuncs.com/resource/docs/schematic/Core/M5StickC/20191118__StickC_A04_3110_Schematic_Rebuild_PinMap.pdf)
  but contains no resistor values. Estimated pulse current 20 mA:
  (3.3 V - 1.3 V Vf) / 100 ohm assumed series resistance. A 38 kHz NEC style
  burst averages 5 to 10 mA during transmission. The ESP32 GPIO absolute
  limit of about 40 mA bounds it from above.

## Known model limitations

Two of these matter whenever a scenario's subject is a periodic timer.

- **Timer fires are billed at the UI loop quantum, not at their own cost.**
  `sim/power_profiler.cpp` marks a UI cycle awake if any registered timer fired
  in it, and then bills the whole interval at the board's `mcu_80` figure. A
  timer whose callback is one I2C read and a few dozen float operations is
  therefore charged the same as one that runs for the full 5 ms tick. The
  baseline is artificial in the other direction: `UI::task()` runs
  `lv_task_handler()` every 5 ms regardless, and the model still reports that
  as full light-sleep residency. Treat a delta produced this way as a ceiling
  with roughly an order of magnitude of headroom, not as an estimate of the
  work, and settle the real number on hardware.
- **Sensor current does not follow sensor use.** A scenario that enables the
  IMU is still billed the constant `peripherals` entry, which for the StickS3
  is `bmi270_suspend`. A 50 Hz accelerometer read needs normal mode, listed
  separately in `board-currents.yaml` at 0.685 mA, and nothing charges it.

Both are tracked for a follow-up. Until they are fixed, `compare.py` is a
regression guard against a scenario getting worse, not a source of absolute
numbers. Note also that it only fails on increases, so a baseline cannot catch
a change that lowers the estimate, such as a timer period going up.

## Numbers that could not be sourced (all tagged estimated)

- ESP32 and ESP32-S3 BLE TX at 3 and 6 dBm (and 3/6/9 dBm entirely for the
  classic ESP32): not in either datasheet, interpolated.
- ILI9342C operating and sleep-in current: absent from its own datasheet.
- Backlight current for StickC Plus, StickS3, Core, Core2: no isolated
  measurement published anywhere found.
- GPS module $PCAS12 in-circuit standby current: chip figure only.
- AXP192 whole chip operating quiescent: only per-block figures published.
- Core2 vibration motor, speaker volume currents, StickC Plus buzzer,
  IR LED pulse current: no part level sources published by M5Stack.

The estimated backlight and $PCAS12 standby entries are the two with the
largest impact on model accuracy. Both are measurable on the attached
hardware and should be replaced with measured-local values when the plan 63
sim lands.
