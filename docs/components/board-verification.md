# Per-board verification table

One row per board fact, with the value the firmware ships and the source that
settles it. The row values come from `src/FurbleBoard.cpp` and the per-board
branches of `src/FurblePlatform.cpp`; the sources are the vendor product pages,
pin maps, schematics and datasheets named in [Evidence](#evidence), the pinned
board support library in `.pio/libdeps` (M5Unified 0.2.20, M5GFX 0.2.27, M5PM1
1.0.6) and the component pages under `docs/components/`.

The state of a row is one of the four words the corpus uses. VERIFIED means a
document or a source file named in the row was opened in this pass and states
the value. REPORTED means the tree already recorded the fact from a document
this pass did not reopen, and the row keeps that pass's citation. CITED means
the firmware relies on it and no document was reached. UNVERIFIED means nothing
found settles it, and the row names the measurement that would. A row that
covers two claims names the state of each half, and neither half is promoted:
"VERIFIED (path), UNVERIFIED (current)" is two facts, not one.

This page is the evidence, not a support list. Which boards furble supports is
[../supported-hardware.md](../supported-hardware.md).

## Facts that changed in this pass

| # | Row | Column | Was | Now | Why |
| :--- | :--- | :--- | :--- | :--- | :--- |
| F1 | StickC | `rtcOnBus` | false | true | The M5StickC page lists a BM8563 RTC and its pin map puts it on G21/G22, the internal I2C bus. M5Unified's PCF8563 fallback writes 0x00 and 0x0E, both writable on a BM8563, so the instance is kept. |
| F2 | StickC Plus | `rtcOnBus` | false | true | Same page family: the M5StickC Plus page lists a BM8563. |
| F3 | Core2 | `rtcOnBus` | false | true | The Core2 pin map names `BM8563 (0x51)` on G21/G22. |
| F4 | Tough | `rtcOnBus` | false | true | The Tough pin map names `BM8563 (0x51)` on G21/G22. |
| F5 | StickS3 | `ledActiveLow` | true | false | The row has no LED pad (`ledPin` -1). The status LED is the M5PM1 `PWR_CFG` bit 4 level, not a GPIO. |
| F6 | Core, Core2, Tough | `ledActiveLow` | true | false | The rows have no LED pad. The Core2's green power indicator is a charger indicator, not software driven. |
| F7 | all | comment | The `EXT_5V` paragraph in `src/FurbleBoard.cpp` was interleaved with the rail-column paragraph by an earlier merge, so it read "by two / The rail column ... / writes during M5.begin()". | Four separate paragraphs. | A comment a reader cannot parse is a defect in the record. |
| F8 | Core2 | `EXT_5V` comment in `src/FurblePlatform.cpp` | "AXP2101 LDOIO0" | `BLDO2` | M5Unified 0.2.20 `Power_Class::setExtOutput()` calls `Axp2101.setBLDO2()` for that board, and the AXP2101 has no LDOIO. |
| F9 | all | `CLAUDE.md` display-sleep trap | "ILI9342C is the panel on core, core2 and core-usb-debug ... three of the six display boards" | Four of the seven, with the Tough | M5GFX 0.2.27 maps `board_M5Tough` to `Panel_ILI9342`, and the vendor page states an ILI9342C panel. |
| F10 | all | `CLAUDE.md` RTC trap | "`M5.Rtc.isEnabled()` is false on every board except the Plus2" | True on five rows | Same evidence as F1 to F4. |
| F11 | all | `docs/components/board-map.md` profile table | Core capacity 0, Plus2 RTC backing false, Plus timed wake NONE, Tough SD false and sound false, no `powerOffPath`, `rtcOnBus`, `ledActiveLow`, `irMarkLow` or GPS columns, no Waveshare column | The shipped values | The table was a stale copy of a pre-322 state. |
| F12 | Core2, Tough | touch address | "not in this repository" | FT6336U 0x38, CHSC6540 0x2E | Both addresses are in the vendor pin maps and in the M5GFX driver configuration. |

## Evidence

Board pages, all opened 2026-11-27 for this pass:

| Key | Document | URL |
| :--- | :--- | :--- |
| M5C | M5StickC product page, SKU K016 | <https://docs.m5stack.com/en/core/m5stickc> |
| M5CP | M5StickC Plus product page, SKU K016-P | <https://docs.m5stack.com/en/core/m5stickc_plus> |
| M5CP2 | M5StickC Plus2 product page | <https://docs.m5stack.com/en/core/M5StickC%20PLUS2> |
| M5S3 | M5StickS3 product page | <https://docs.m5stack.com/en/core/StickS3> |
| M5B | M5Stack Basic product page | <https://docs.m5stack.com/en/core/basic> |
| M5C2 | M5Stack Core2 product page, SKU K010 | <https://docs.m5stack.com/en/core/Core2> |
| M5T | M5Tough product page, SKU K034 | <https://docs.m5stack.com/en/core/tough> |
| M5C3 | M5Stack CoreS3 product page | <https://docs.m5stack.com/en/core/CoreS3> |
| WS | Waveshare ESP32-S3-ETH wiki | <https://www.waveshare.com/wiki/ESP32-S3-ETH> |

Source files read in this pass, from the pinned dependencies:

| Key | File | Version |
| :--- | :--- | :--- |
| UNI | `.pio/libdeps/<env>/M5Unified/src/M5Unified.cpp`, `src/utility/Power_Class.cpp`, `src/utility/RTC_Class.cpp`, `src/utility/rtc/PCF8563_Class.cpp` | M5Unified 0.2.20 |
| GFX | `.pio/libdeps/<env>/M5GFX/src/M5GFX.cpp`, `src/lgfx/v1/panel/*` | M5GFX 0.2.27 |
| PM1 | `.pio/libdeps/<env>/M5PM1/src/M5PM1.h` | M5PM1 1.0.6 |

Component pages supply the chip-level facts and their own documents:
[bm8563.md](bm8563.md), [mpu6886.md](mpu6886.md), [bmi270.md](bmi270.md),
[ip5306.md](ip5306.md), [axp192.md](axp192.md), [m5pm1.md](m5pm1.md),
[st7735.md](st7735.md), [st7789.md](st7789.md), [ili934x.md](ili934x.md),
[cst816.md](cst816.md).

## M5StickC

Row `src/FurbleBoard.cpp:139`.

| # | Fact | Value | Evidence | State |
| :--- | :--- | :--- | :--- | :--- |
| 1 | Battery capacity | 95 mAh | M5C "Battery 95mAh@3.7V", and the version-change table records 80 to 95 mAh in 2020.3 | VERIFIED |
| 2 | Charging path and current | AXP192 on VBUS. No charge-current value is set by furble; M5Unified's AXP192 init table selects one | M5C "when VBUS is powered, the built-in battery is charged through the AXP192 PMU". The exact current is UNVERIFIED: the AXP192 register document in the tree has no revision string ([axp192.md](axp192.md)) | CITED |
| 3 | Power-off mechanism and `powerOffPath` | AXP192 shutdown through `M5.Power.powerOff()`, which cuts the rail and does not return. `powerOffPath` true | UNI `Power_Class::_powerOff()` AXP192 branch; `src/FurblePlatform.cpp:568` | VERIFIED |
| 4 | RTC exists, battery backed, `rtcOnBus`, `timedWake` | BM8563 at 0x51. Shipped `rtcOnBus` true and `rtcBatteryBacked` true: the main cell keeps the part counting across an AXP192 cut, so no coin cell is needed. `timedWake` NONE | M5C pin map `AXP192 & RTC (BM8563)` on G21/G22 with IRQ on G35; [bm8563.md](bm8563.md) 0x51; the backing is the row's own definition of the column, and the page shows no separate cell | VERIFIED (chip and pins), REPORTED (retention across a full cell removal) |
| 5 | Display controller and sleep-timing class | ST7735SV, one-direction 120 ms class | M5C "0.96 inch 80 x 160 ST7735SV"; GFX `Panel_M5StickC : Panel_ST7735S`; [st7735.md](st7735.md) row 3 is UNVERIFIED for the dwell, so the firmware applies the wider both-direction rule | VERIFIED (part), UNVERIFIED (ST7735 dwell) |
| 6 | Touch controller and address | none | M5C pin map lists no touch controller | VERIFIED |
| 7 | IMU part and wake threshold resolution | MPU6886 at 0x68 (SH200Q before 2019.8). Wake-on-motion count size UNVERIFIED | M5C `MEMS MPU6886` and version table "2019.8 SH200Q changed to MPU6886"; [mpu6886.md](mpu6886.md) C-26a: the datasheet states no mg per LSB | VERIFIED (part), UNVERIFIED (4 mg per count) |
| 8 | SD slot wiring and card detect | none | M5C pin map has no card slot | VERIFIED |
| 9 | IR emitter and polarity | G9, active low | M5C pin map `IR Transmitter: IR Pin G9`; polarity from the StickC Plus schematic page, because no StickC-specific schematic was available (plans/350) | VERIFIED (pin), REPORTED (polarity) |
| 10 | LED pin and polarity | G10, active low | M5C pin map `Red LED: LED Pin G10`; UNI `Power_Class` Light_PWM `invert = true`, `pin_bl = GPIO_NUM_10` for this board | VERIFIED |
| 11 | Speaker amplifier and enable pin | no internal speaker; a HAT SPK or SPK2 is external | UNI speaker setup: the StickC case only fills a configuration under `external_speaker.hat_spk` or `hat_spk2`. M5C lists no buzzer | VERIFIED |
| 12 | GPS pin source and pins | `BOARD_SUPPORT`, resolves to G33 and G32 | UNI `_pin_table_i2c_ex_in` ESP32 branch falls to the `board_unknown` row, EX SCL 33, SDA 32. M5C `PORT.CUSTOM G32 G33` | VERIFIED |
| 13 | Rail topology and what the switchable rail feeds | EXTEN is the Grove 5 V output; `gpsRail` true | M5C AXP192 output table lists a 5V GROVE output; UNI `setExtOutput()` calls `Axp192.setEXTEN()` for this board | VERIFIED |
| 14 | Feedback capabilities | light only | Row facts above: no speaker, LED on G10, no vibration motor on the page | VERIFIED |
| 15 | Motion wake path | GPIO35, one open-drain net: MPU6886 INT, BM8563 INT and AXP192 IRQ | M5C pin map shows `G35` across AXP192 IRQ, MPU6886 IRQ and BM8563 IRQ; UNI `_rtcIntPin = GPIO_NUM_35`, IRQ enable 0x42 = 0x03 | VERIFIED |
| 16 | `pmicClickStreak` | true | AXP192 PEK status is readable through `M5.BtnPWR`, which the streak counts | VERIFIED |

## M5StickC Plus

Row `src/FurbleBoard.cpp:143`.

| # | Fact | Value | Evidence | State |
| :--- | :--- | :--- | :--- | :--- |
| 1 | Battery capacity | 120 mAh | M5CP "Battery 120mAh@3.7V" | VERIFIED |
| 2 | Charging path and current | AXP192 on VBUS, same as the StickC | M5CP "When powered by VBUS, the AXP192 power management will charge the internal battery" | VERIFIED (path), UNVERIFIED (current) |
| 3 | Power-off mechanism and `powerOffPath` | AXP192 cut plus the BM8563 alarm as wake. `powerOffPath` true | UNI `_powerOff()`; `src/FurblePlatform.cpp:568` | VERIFIED |
| 4 | RTC exists, battery backed, `rtcOnBus`, `timedWake` | BM8563 at 0x51, `timedWake` RTC_ALARM. The part sits on the same G21/G22 bus as the IMU and the PMU, which the page shows, and the chip row is in its specification table | M5CP specification table `RTC: BM8563` and the `6-Axis IMU (MPU6886) & Power Management Chip (AXP192)` table for the bus. The BM8563 INT to AXP192 power key through R401 and C401 is a vendor statement this pass did not open | VERIFIED (chip), UNVERIFIED (INT coupling) |
| 5 | Display controller and sleep-timing class | ST7789v2, one-direction 120 ms class | M5CP "1.14 inch 135 x 240 ST7789v2"; GFX `Panel_M5StickCPlus : Panel_ST7789`; [st7789.md](st7789.md) row 3 | VERIFIED |
| 6 | Touch controller and address | none | M5CP pin map lists none | VERIFIED |
| 7 | IMU part and wake threshold resolution | MPU6886 at 0x68, count size UNVERIFIED | M5CP `MEMS MPU6886`; [mpu6886.md](mpu6886.md) C-26a | VERIFIED (part), UNVERIFIED (count) |
| 8 | SD slot wiring and card detect | none | M5CP pin map lists none | VERIFIED |
| 9 | IR emitter and polarity | G9, active low | M5CP pin map `IR Transmitter: IR Pin G9`; polarity from the StickC Plus schematic page (plans/350) | VERIFIED (pin), REPORTED (polarity) |
| 10 | LED pin and polarity | G10, active low | M5CP `Red LED: LED Pin G10`; UNI Light_PWM `invert = true` on G10 | VERIFIED |
| 11 | Speaker amplifier and enable pin | passive buzzer on G2, no amplifier | M5CP `Passive Buzzer: Buzzer Pin G2`; UNI `spk_cfg.buzzer = true`, `pin_data_out = GPIO_NUM_2` | VERIFIED |
| 12 | GPS pin source and pins | `BOARD_SUPPORT`, G33 and G32 | UNI ESP32 `board_unknown` EX row; M5CP `PORT.CUSTOM G32 G33` | VERIFIED |
| 13 | Rail topology and what the switchable rail feeds | EXTEN to the Grove 5 V output, `gpsRail` true | M5CP AXP192 output table lists 5V GROVE; UNI `setExtOutput()` | VERIFIED |
| 14 | Feedback capabilities | sound (buzzer) and light | Rows 10 and 11 | VERIFIED |
| 15 | Motion wake path | GPIO35 shared net | M5CP pin map; UNI, as the StickC | VERIFIED |
| 16 | `pmicClickStreak` | true | AXP192 PEK through `M5.BtnPWR` | VERIFIED |

## M5StickC Plus2

Row `src/FurbleBoard.cpp:147`.

| # | Fact | Value | Evidence | State |
| :--- | :--- | :--- | :--- | :--- |
| 1 | Battery capacity | 200 mAh | M5CP2 "Battery 200mAh@3.7V" | VERIFIED |
| 2 | Charging path and current | no PMIC. An external charger, battery voltage on the ADC divider at G38. No charge status, so `battery.charging` is false | M5CP2 version change 2023.12 "Removed AXP192 power management chip"; pin map `Battery Voltage Detect: G38`; UNI `_pmic = pmic_adc`, `_batAdcPin = 38`, `_adc_ratio = 2.0f` | VERIFIED |
| 3 | Power-off mechanism and `powerOffPath` | the regulator enable is the `HOLD` pin G4, held high at boot and driven low to cut. `powerOffPath` true, `powerHoldPin` 4 | M5CP2 "set HOLD (GPIO4) to 0 during program execution to cut off power"; the boot hold is `src/FurblePlatform.cpp:175-181` and the timed-wake release `src/FurblePlatform.cpp:1066-1071` | VERIFIED |
| 4 | RTC exists, battery backed, `rtcOnBus`, `timedWake` | BM8563 at 0x51, kept alive across a HOLD cut, `rtcOnBus` true, `timedWake` RTC_ALARM | M5CP2 `RTC BM8563` on G21/G22 and "wake the device via an IRQ signal triggered by the RTC timer" | VERIFIED |
| 5 | Display controller and sleep-timing class | ST7789v2, one-direction class | M5CP2 "ST7789v2"; GFX maps the board to `Panel_ST7789`; [st7789.md](st7789.md) row 3 | VERIFIED |
| 6 | Touch controller and address | none | M5CP2 pin map lists none | VERIFIED |
| 7 | IMU part and wake threshold resolution | MPU6886 at 0x68, count size UNVERIFIED | M5CP2 `IMU MPU6886`; [mpu6886.md](mpu6886.md) C-26a | VERIFIED (part), UNVERIFIED (count) |
| 8 | SD slot wiring and card detect | none | M5CP2 pin map lists none | VERIFIED |
| 9 | IR emitter and polarity | G19, shared with the red LED. The emitter is active high in the firmware, which is an inference from the NPN stage plans/350 records and not a vendor statement | M5CP2 `IR Emitter & Red LED: pin G19` and the table row `M5STICKC PLUS2: IR G19, LED G19`. plans/350 records the NPN stage as unconfirmed against a vendor PDF in this tree, so the polarity is U13 and `irMarkLow` decides the RMT inversion | VERIFIED (pin), UNVERIFIED (polarity) |
| 10 | LED pin and polarity | G19, active high, shared with the emitter | M5CP2 `Red LED x 1 (Shared control pin G19 with IR emitter tube)`; UNI Light_PWM `invert = false`, `pin_bl = StickCPlus2_LED_PIN` (GPIO 19) | VERIFIED |
| 11 | Speaker amplifier and enable pin | buzzer on G2, no amplifier | M5CP2 `Buzzer: Buzzer Pin G2`; UNI `spk_cfg.buzzer = true`, `pin_data_out = GPIO_NUM_2` | VERIFIED |
| 12 | GPS pin source and pins | `BOARD_SUPPORT`, G33 and G32 | UNI ESP32 `board_unknown` EX row; M5CP2 `PORT.CUSTOM G32 G33` | VERIFIED |
| 13 | Rail topology and what the switchable rail feeds | none. `setExtOutput()` has no case for this board so the call does nothing; `gpsRail` false | UNI `Power_Class::setExtOutput()` switch has no `board_M5StickCPlus2` case | VERIFIED |
| 14 | Feedback capabilities | sound and light, no vibration | Rows 10 and 11 | VERIFIED |
| 15 | Motion wake path | none. G35 is the power button, not an IMU interrupt, and the MPU6886 INT is not wired to it | M5CP2 pin map `BUTTON A G37, BUTTON B G39, BUTTON C (WAKE) G35`; UNI `_wakeupPin = GPIO_NUM_35` for this board | VERIFIED |
| 16 | `pmicClickStreak` | false | the board has no PMIC, so there is no `M5.BtnPWR` source | VERIFIED |

## M5StickS3

Row `src/FurbleBoard.cpp:151`. This is the only image with an M5PM1.

| # | Fact | Value | Evidence | State |
| :--- | :--- | :--- | :--- | :--- |
| 1 | Battery capacity | 250 mAh | M5S3 `Battery Capacity 250mAh` | VERIFIED |
| 2 | Charging path and current | M5PM1 charger, charge status on PM1 GPIO0 as `PYG0_CHG_STAT`, active low. No current register | M5S3 M5PM1 pin table `Battery Charge: PYG0_CHG_STAT`; UNI sets `M5pm1.setGPIOFunction(gpio0, gpio)` and `input`; `src/FurblePlatform.cpp:1449` reads it | VERIFIED |
| 3 | Power-off mechanism and `powerOffPath` | `M5PM1.shutdown()` cuts the rail. `powerOffPath` true | `src/FurblePlatform.cpp` M5PM1 shutdown path; [m5pm1.md](m5pm1.md) | VERIFIED |
| 4 | RTC exists, battery backed, `rtcOnBus`, `timedWake` | no RTC chip, so `rtcOnBus` and `rtcBatteryBacked` false. `timedWake` PMIC_TIMER with the RTC RAM marker | M5S3 pin map lists no RTC part; UNI RTC fallback probe fails without a chip; `src/FurblePlatform.cpp` timer and marker | VERIFIED |
| 5 | Display controller and sleep-timing class | ST7789P3, one-direction class | M5S3 specification table `Display: Model ST7789P3, Resolution 135x240`, and its ST7789P3 pin table; GFX drives it with `Panel_ST7789`; [st7789.md](st7789.md) row 3 | VERIFIED |
| 6 | Touch controller and address | none | M5S3 lists no touch panel | VERIFIED |
| 7 | IMU part and wake threshold resolution | BMI270 at 0x68, 1000 mg over 2048 counts, so 0.4883 mg per count | M5S3 `IMU BMI270`; [bmi270.md](bmi270.md) and `include/FurbleIMUMotionScale.h` | VERIFIED |
| 8 | SD slot wiring and card detect | none | M5S3 pin map lists none | VERIFIED |
| 9 | IR emitter and polarity | IR_TX on G46 and IR_RX on G42, both behind EXT_5V. Polarity UNVERIFIED | M5S3 `IR: IR_TX G46, IR_RX G42` and "turns off the power supply to the Grove, Hat EXT_5V interfaces, and IR TX/RX" | VERIFIED (pin and rail), UNVERIFIED (polarity) |
| 10 | LED pin and polarity | no GPIO. The status LED is the M5PM1 `PWR_CFG` bit 4 level, `ledActiveLow` false | M5PM1 1.0.6 `M5PM1_PWR_CFG_LED_CTRL = (1 << 4)`; `src/FurblePlatform.cpp` `setDisplayOff()` | VERIFIED |
| 11 | Speaker amplifier and enable pin | ES8311 codec at 0x18 into an AW8737 amplifier. Enable is M5PM1 GPIO3, register 0x11 bit 3 | M5S3 `Audio Codec ES8311`, `Speaker AW8737 power amplifier`, M5PM1 pin table `Speaker: PYG3_SPK_Pulse`; UNI `_speaker_enabled_cb_sticks3` bitOn(0x6E, 0x11, 0b00001000) | VERIFIED |
| 12 | GPS pin source and pins | `BOARD_SUPPORT`, resolves to G10 and G9 | UNI S3 branch `board_M5StickS3` EX 10, 9; M5S3 `PORT.CUSTOM G9 G10` | VERIFIED |
| 13 | Rail topology and what the switchable rail feeds | M5PM1 `PWR_CFG` bit 3 BOOST_EN feeds the Grove 5 V output, the HAT EXT_5V pin and the IR transmitter and receiver. `gpsRail` true | M5PM1 1.0.6 `M5PM1_PWR_CFG_BOOST_EN`; M5S3 EXT_5V note; `src/FurblePlatform.cpp` rail manager | VERIFIED |
| 14 | Feedback capabilities | sound only. No LED feedback because the PM1 LED is a level, not a pattern output, and the board has no vibration motor | Row 10; M5S3 lists no motor | VERIFIED |
| 15 | Motion wake path | BMI270 INT1 to M5PM1 GPIO4, PMIC IRQ to SoC GPIO13 | M5S3 M5PM1 pin table `IMU INT: PYG4_IMU_INT` and `ESP32-S3 G13`; `src/FurblePlatform.cpp` PMIC_GPIO branch | VERIFIED |
| 16 | `pmicClickStreak` | false, the PM1 button is polled directly | `src/FurblePlatform.cpp` `btnGetState()` poll under `FURBLE_HAS_M5PM1` | VERIFIED |

## M5Stack Basic (Core)

Row `src/FurbleBoard.cpp:155`.

| # | Fact | Value | Evidence | State |
| :--- | :--- | :--- | :--- | :--- |
| 1 | Battery capacity | 110 mAh | M5B `Battery 110 mAh @ 3.7 V`, version change 2020.3 from 150 mAh | VERIFIED |
| 2 | Charging path and current | IP5306, I2C variant at 0x75, with the charge state on `REG_READ0` 0x70 bit 3. furble reports that bit, so `battery.charging` is true here | M5B `The power-management IC (IP5306) is a custom I2C version; its I2C address is 0x75`; [ip5306.md](ip5306.md) row 4 (`0x70` bit 3 charge_en, read by M5Unified `IP5306_Class.cpp`); `tests/host/board_profile_test.cpp` pins it | VERIFIED (path and state), UNVERIFIED (current, because M5Unified selects 150 mA which is under the part's own 200 mA termination floor, plans/354) |
| 3 | Power-off mechanism and `powerOffPath` | no shutdown command. `M5.Power.powerOff()` clears the boost keep-on bit and deep sleeps, and the light-load cut removes the boost after the `SYS_CTL2` window. `powerOffPath` true, `canPowerOff` false | M5Unified `IP5306_Class` and `_powerOff()`; [ip5306.md](ip5306.md) SYS_CTL0 bit 1 and SYS_CTL2 bits 3:2 | VERIFIED |
| 4 | RTC exists, battery backed, `rtcOnBus`, `timedWake` | none: the page lists no RTC part, and the AXP192's RTC registers are not on the I2C bus | M5B pin map has no RTC row; [axp192.md](axp192.md) | VERIFIED |
| 5 | Display controller and sleep-timing class | ILI9342C, both-direction 120 ms class | M5B `2-inch 320 x 240 ILI9342C`; GFX `Panel_M5Stack : Panel_ILI9342`; [ili934x.md](ili934x.md) | VERIFIED |
| 6 | Touch controller and address | none | M5B pin map lists none | VERIFIED |
| 7 | IMU part and wake threshold resolution | MPU6886 on the standard body, `MPU6886/SH200Q` on the Gray variant. Count size UNVERIFIED | M5B pin map `6-axis IMU (SH200Q/MPU6886)`; [mpu6886.md](mpu6886.md) C-26a | VERIFIED (part), UNVERIFIED (count) |
| 8 | SD slot wiring and card detect | CS G4, CLK G18, MISO G19, MOSI G23, no card detect | M5B `TF Card: MOSI G23, MISO G19, CLK G18, CS G4`; UNI `_pin_table_sd` board_M5Stack 18, 23, 19, 255, 255, 4 | VERIFIED |
| 9 | IR emitter and polarity | none | M5B pin map has no IR row | VERIFIED |
| 10 | LED pin and polarity | none | M5B pin map has no LED row, and the row drives no pad | VERIFIED |
| 11 | Speaker amplifier and enable pin | speaker on G25 through the DAC, no I2S amplifier and no enable pin | M5B pin map `G25 DAC/SPK`; UNI board_M5Stack speaker `use_dac = true`, `pin_data_out = GPIO_NUM_25` | VERIFIED (path), UNVERIFIED (the buffer part, gap C-31b) |
| 12 | GPS pin source and pins | `NONE`. The board's Grove A is the internal I2C bus and M5Unified's Port A row for it is those same pins | M5B `GROVE Port A: SCL G22, SDA G21` with the IP5306 on the same pins; UNI `board_M5Stack` In and EX both 22/21 | VERIFIED |
| 13 | Rail topology and what the switchable rail feeds | none switchable. `setExtOutput()` has no case for this board | UNI `setExtOutput()` switch | VERIFIED |
| 14 | Feedback capabilities | sound only | Row 11 | VERIFIED |
| 15 | Motion wake path | none. No IMU interrupt is wired to a GPIO on the page, and the row does not claim one | M5B pin map | VERIFIED |
| 16 | `pmicClickStreak` | false. The IP5306 has no button status register on this bus | [ip5306.md](ip5306.md) | VERIFIED |

## M5Stack Core2

Row `src/FurbleBoard.cpp:159`.

| # | Fact | Value | Evidence | State |
| :--- | :--- | :--- | :--- | :--- |
| 1 | Battery capacity | 500 mAh | M5C2 `Lithium Battery 500mAh@3.7V`, version change 2023.10 from 390 mAh | VERIFIED |
| 2 | Charging path and current | AXP192, vendor charging current 0.219 A. M5Unified's `setChargeCurrent(390)` selects 360 mA, and the firmware does not override it | M5C2 `Charging current: 0.219A`; UNI `Power_Class` charge setup. This is a deliberate deviation, recorded in plans/354 | VERIFIED (both values) |
| 3 | Power-off mechanism and `powerOffPath` | AXP192 cut through `M5.Power.powerOff()`. `powerOffPath` true, `canPowerOff` true | UNI `_powerOff()` | VERIFIED |
| 4 | RTC exists, battery backed, `rtcOnBus`, `timedWake` | BM8563 at 0x51. `rtcOnBus` true. `rtcBatteryBacked` true. `timedWake` NONE because 2023.2 removed the backup cell and the firmware cannot detect the revision | M5C2 `RTC: BM8563`, pin map `BM8563 (0x51)` on G21/G22, version table `2023.2 Removed RTC button battery` | VERIFIED (chip), UNVERIFIED (backing on a given unit) |
| 5 | Display controller and sleep-timing class | ILI9342C, both-direction class | M5C2 page panel; GFX `Panel_M5StackCore2 : Panel_ILI9342`; [ili934x.md](ili934x.md) | VERIFIED |
| 6 | Touch controller and address | FT6336U at 0x38, interrupt G39 | M5C2 `Capacitive Touch IC FT6336U`, pin map `FT6336U (0x38) SDA G21 SCL G22 INT G39`; GFX `Touch_FT5x06` with `i2c_addr = 0x38`, `pin_int = GPIO_NUM_39` | VERIFIED |
| 7 | IMU part and wake threshold resolution | MPU6886 at 0x68. Count size UNVERIFIED | M5C2 `IMU MPU6886`, pin map `MPU6886 (0x68)`; [mpu6886.md](mpu6886.md) C-26a | VERIFIED (part), UNVERIFIED (count) |
| 8 | SD slot wiring and card detect | CS G4, CLK G18, MISO G38, MOSI G23, no card detect | M5C2 `microSD: MISO G38, MOSI G23, SCK G18, CS G4`; UNI `_pin_table_sd` board_M5StackCore2 18, 23, 38, 255, 255, 4 | VERIFIED |
| 9 | IR emitter and polarity | none | M5C2 pin map lists none | VERIFIED |
| 10 | LED pin and polarity | none. The green power indicator is not software driven | M5C2 `LED: Green power indicator LED` | VERIFIED |
| 11 | Speaker amplifier and enable pin | NS4168 I2S amplifier, enable is AXP192 GPIO2. A v1.1 AXP2101 board enables ALDO3 instead | M5C2 `Speaker Amplifier NS4168` and `I2S DOUT G2, LRCK G0, BCK G12`; UNI `_speaker_enabled_cb_core2()` calls `Axp192.setGPIO2(enabled)` or `Axp2101.setALDO3(enabled * 3300)` | VERIFIED |
| 12 | GPS pin source and pins | `BOARD_SUPPORT`, resolves to G33 and G32 | M5C2 `PORT-A (Red) G32/33`, pin map `PORT.A G32 G33 SDA/SCL`; UNI ESP32 `board_unknown` EX row | VERIFIED |
| 13 | Rail topology and what the switchable rail feeds | AXP192 GPIO0 (LDO mode) enables one BUS_5V net for Port A and the M-Bus. `gpsRail` true | UNI `setExtOutput()` writes AXP192 0x90 = 0x02 or 0x07; [plans/354] and [../power/board-power.md](../power/board-power.md) section 2 | VERIFIED |
| 14 | Feedback capabilities | sound and vibration | M5C2 `vibration motor`; row 11 | VERIFIED |
| 15 | Motion wake path | none | M5C2 pin map lists the MPU6886 on the I2C bus only, with no interrupt pin | VERIFIED |
| 16 | `pmicClickStreak` | false | out of scope for this pass; the value is unchanged and no document was found that settles the race between a PEK click and a touch wake | UNVERIFIED |

## M5Tough

Row `src/FurbleBoard.cpp:163`.

| # | Fact | Value | Evidence | State |
| :--- | :--- | :--- | :--- | :--- |
| 1 | Battery capacity | 390 mAh | M5Tough README, <https://github.com/m5stack/M5Tough>: "The battery capacity has been upgraded to 390mAh". The product page specification table does not carry a capacity | VERIFIED |
| 2 | Charging path and current | AXP192 | M5T `AXP192 power management chip` | VERIFIED (path), UNVERIFIED (current) |
| 3 | Power-off mechanism and `powerOffPath` | AXP192 cut. `powerOffPath` true | UNI `_powerOff()` | VERIFIED |
| 4 | RTC exists, battery backed, `rtcOnBus`, `timedWake` | BM8563 at 0x51, BT1 backup cell, `rtcOnBus` true, `timedWake` RTC_ALARM | M5T `BM8563-RTC clock, supports RTC timer wake-up` and pin map `BM8563 (0x51)`; the BT1 cell and the INT to the AXP192 PWR net are from the Tough schematic, plans/354 | VERIFIED (chip and wake claim), REPORTED (cell) |
| 5 | Display controller and sleep-timing class | ILI9342C, both-direction class | M5T `2.0" 320 x 240 ILI9342C`; GFX `Panel_M5StackCore2 : Panel_ILI9342`, which the Tough shares | VERIFIED |
| 6 | Touch controller and address | CHSC6540 at 0x2E, interrupt G39 | M5T `Touch Controller CHSC6540` and pin map `CHSC6540 (0x2E) SDA G21 SCL G22 INT G39`; GFX `Touch_CHSC6540`, `pin_int = GPIO_NUM_39` | VERIFIED |
| 7 | IMU part and wake threshold resolution | MPU6886. Count size UNVERIFIED | M5T page IMU row; UNI shares the Core2 IMU path; [mpu6886.md](mpu6886.md) C-26a | VERIFIED (part), UNVERIFIED (count) |
| 8 | SD slot wiring and card detect | CS G4, CLK G18, MISO G38, MOSI G23, no card detect | M5T `microSD: MISO G38, MOSI G23, SCK G18, CS G4`; UNI `_pin_table_sd` board_M5Tough | VERIFIED |
| 9 | IR emitter and polarity | none | M5T pin map lists none | VERIFIED |
| 10 | LED pin and polarity | none | M5T page lists no LED | VERIFIED |
| 11 | Speaker amplifier and enable pin | NS4168 16-bit I2S amplifier with a 1 W speaker, enable AXP192 GPIO2 | M5T `Speaker Setup NS4168 16-bit I2S amplifier + 1W speaker`; UNI `_speaker_enabled_cb_core2()` | VERIFIED |
| 12 | GPS pin source and pins | `BOARD_SUPPORT`, resolves to G33 and G32 | M5T `PORT.A G32 G33`; UNI ESP32 `board_unknown` EX row | VERIFIED |
| 13 | Rail topology and what the switchable rail feeds | one BUS_5V net behind AXP192 GPIO0, feeding Grove Port A and the M-Bus. `gpsRail` true | UNI `setExtOutput()`; the Tough schematic's SY7088 output to J4 and J7 (plans/354) | VERIFIED |
| 14 | Feedback capabilities | sound only | Rows 10 and 11, plus the page's absence of a motor | VERIFIED |
| 15 | Motion wake path | none | M5T pin map | VERIFIED |
| 16 | `pmicClickStreak` | true | AXP192 PEK through `M5.BtnPWR`, the same source as the StickC family | VERIFIED |

## Waveshare ESP32-S3-ETH

Row `src/FurbleBoard.cpp:177`. The image pins `board_M5StampS3`, and this row is
what that identity maps to.

| # | Fact | Value | Evidence | State |
| :--- | :--- | :--- | :--- | :--- |
| 1 | Battery capacity | none, 0 | WS board feature list has no battery and no charger | VERIFIED |
| 2 | Charging path and current | none | WS, same list | VERIFIED |
| 3 | Power-off mechanism and `powerOffPath` | none. `powerOffPath` false and `Platform::powerOff()` refuses | WS has no PMIC. UNI `Power_Class::powerOff()` on a non-M5PM1 board without an AXP falls through to `esp_deep_sleep_start()`, which would sleep with no wake source | VERIFIED |
| 4 | RTC exists, battery backed, `rtcOnBus`, `timedWake` | none | WS lists no RTC. M5Unified's PCF8563 probe has nothing to answer at 0x51 because `FURBLE_HAS_INTERNAL_I2C` is 0 and the internal bus is not opened | VERIFIED |
| 5 | Display controller and sleep-timing class | none | WS lists no display | VERIFIED |
| 6 | Touch controller and address | none | WS, same | VERIFIED |
| 7 | IMU part and wake threshold resolution | none | WS, same | VERIFIED |
| 8 | SD slot wiring and card detect | the board has a TF slot, but no card-select line that the SDSPI stack can drive, so `sdSlot` false while `FURBLE_HAS_SD` is 1 | WS `Onboard TF card slot`. The wiki renders its pin table as an image, so the pin assignment itself is UNVERIFIED and a scope on the header is what would settle it | VERIFIED (slot exists), UNVERIFIED (wiring) |
| 9 | IR emitter and polarity | none | WS, same | VERIFIED |
| 10 | LED pin and polarity | none. The board has a WS2812 on GPIO21 that furble keeps off | [../power/board-power.md](../power/board-power.md) Waveshare row records the part; the GPIO is REPORTED, not re-read this pass | REPORTED |
| 11 | Speaker amplifier and enable pin | none | WS, same | VERIFIED |
| 12 | GPS pin source and pins | `NONE`. M5Unified's `board_M5StampS3` row would hand back EX SCL 15 and EX SDA 13, and GPIO13 is the W5500 SPI clock | UNI S3 branch `board_M5StampS3` 15, 13; `src/FurbleEthernet.cpp` pin table | VERIFIED |
| 13 | Rail topology and what the switchable rail feeds | none switchable | WS, same | VERIFIED |
| 14 | Feedback capabilities | none | Rows 10 and 11 | VERIFIED |
| 15 | Motion wake path | none | WS, same | VERIFIED |
| 16 | `pmicClickStreak` | false | no PMIC | VERIFIED |

## Rows that are in flight

These boards have documentation rows and catalog entries but no
`Board::Profile` row, so the per-board branches of `src/FurblePlatform.cpp`
cannot be checked against code. Each fact below is what a port must satisfy; none
of it is verified against a profile, because there is no profile.

### M5Stack CoreS3 class

`board_M5StackCoreS3` and `board_M5StackCoreS3SE` currently fall to the unknown
row, which the board-profile test pins deliberately.

| Fact | Documented value | Evidence | State |
| :--- | :--- | :--- | :--- |
| Battery capacity | 500 mAh | M5C3 `Core Series PinMap Comparison`, 500 mAh | VERIFIED |
| Charger and PMIC | AXP2101 at 0x34, with four power-path switches | M5C3 `Power is managed by an AXP2101 PMU`; pin map `AXP2101 0x34` | VERIFIED |
| RTC | BM8563 at 0x51, with timed wake | M5C3 `RTC: BM8563`, pin map `BM8563 (0x51)` | VERIFIED |
| Display | ILI9342C or ILI9342E, 320x240, both-direction sleep class | M5C3 `2.0" @320x240 ILI9342C / ILI9342E`; version change `2026.8.7 LCD driver changed from ILI9342C to ILI9342E`, which needs M5GFX 0.2.27 or newer | VERIFIED |
| Touch | FT6336U at 0x38 | M5C3 pin map `FT6336U (0x38)`; GFX `Touch_M5StackCoreS3` | VERIFIED |
| IMU | BMI270, and the pin map addresses it at 0x69 rather than 0x68 | M5C3 `6-axis IMU BMI270` and pin map `BMI270 (0x69)`. A furble port must probe both addresses | VERIFIED |
| Speaker amplifier | AW88298 at 0x36, enabled through an AW9523 expander bit | M5C3 pin map `AW88298 (0x36)`; UNI `_speaker_enabled_cb_cores3()` uses `bitOn(aw9523_i2c_addr, 0x02, 0b00000100)` | VERIFIED |
| microSD | slot present, pin map row without pin numbers in the page text | M5C3 `microSD` table | CITED |
| IR, LED, vibration | none listed, and the page describes a power-indicator LED only | M5C3 | VERIFIED |
| GPS | Port A is G1 and G2, so a Grove GPS works, and M5Unified's row has EX 1 and 2 | M5C3 `PORT.A SDA G2, SCL G1`; UNI S3 `board_M5StackCoreS3` EX 1, 2 | VERIFIED |

### LilyGO rows

The catalog carries T-Beam Supreme, T-Deck Plus and Pro, T-Watch Ultra and S3
Plus, T-LoRa Pager, T-Echo Plus and T-Display P4 as port candidates
([../hardware-catalog.md](../hardware-catalog.md), plans/318 and plans/383). No
`Board::Profile` row, no `platformio.ini` environment and no per-board branch in
`src/FurblePlatform.cpp` exists for any of them on master, so every fact in this
section is a requirement for a port and not a verified behaviour.

| Fact | Documented value | Evidence | State |
| :--- | :--- | :--- | :--- |
| T-Beam Supreme PMU and rail switch | AXP2101, so the `lib/hal` AXP2101 driver is the one that would own it | [../hardware-catalog.md](../hardware-catalog.md); [axp2101.md](axp2101.md) | CITED |
| T-Beam Supreme battery | 18650 holder, cell not included, so capacity is not a board figure | [../hardware-catalog.md](../hardware-catalog.md) | CITED |
| T-Beam Supreme SD | microSD on the AXP2101 rails, wiring not in the tree | [../hardware-catalog.md](../hardware-catalog.md) | UNVERIFIED, needs the vendor pin map |
| LilyGO GNSS receivers | MAX-M10S (UBX) on the M10S trim, L76K (`$PCAS`) otherwise | [../hardware-catalog.md](../hardware-catalog.md), [ublox-max-m10s.md](ublox-max-m10s.md) | CITED |
| Any LilyGO row | no profile, so `Board::profile()` returns the unknown row and every column above is unset | `src/FurbleBoard.cpp` `profileFor()` has no LilyGO case | VERIFIED |

## What remains UNVERIFIED

Each row names the measurement that would settle it. None of these is a claim
that the firmware is right or wrong; they are claims that nothing in the tree can
decide.

| # | Open fact | Boards | What would settle it |
| :--- | :--- | :--- | :--- |
| U1 | The MPU6886 wake-on-motion count size, assumed 4 mg | StickC, Plus, Plus2, Core, Core2, Tough | A tilt or shaker rig that holds a known acceleration and reads back which `ACCEL_WOM_*_THR` count first asserts the interrupt, or a TDK application note that states the scale. [mpu6886.md](mpu6886.md) C-26a |
| U2 | The AXP192 charge current furble actually runs at on the Core2 and the Tough | Core2, Tough | M5Unified's AXP192 init writes `0x33 = 0b11000000`, commented "Charge 4.2V, 100mA" (`Power_Class.cpp:695`), which settles the StickC and Plus rows. The Core2 page states 0.219 A while `setChargeCurrent(390)` selects 360 mA, so the Core2 needs a current measurement with the charger active, and the Tough has no vendor figure at all |
| U3 | The Core2 RTC backup cell by revision | Core2 | A BM8563 voltage-low read after a full power cycle, or a visual check for BT1 |
| U4 | The StickC IR emitter polarity from a StickC-specific schematic | StickC | A scope on G9 with a frame in flight, or the StickC schematic PDF |
| U5 | The StickS3 IR emitter polarity | StickS3 | A scope on G46, or the StickS3 schematic PDF |
| U8 | The Waveshare TF slot wiring | Waveshare | A continuity check from the TF pads to the header, or the Waveshare pin table as text |
| U9 | The Core speaker buffer part | Core | The Basic schematic |
| U10 | The StickC Plus BM8563 INT coupling to the AXP192 power key | StickC Plus | The StickC Plus schematic, or a wake test from a full power-off |
| U11 | The StickS3 speaker amplifier settle time, stated as about 1 s before the first tone | StickS3 | A recording or a scope on PYG3_SPK_Pulse from `M5.Speaker.begin()` to the first audible tone. `CLAUDE.md` states the 1 s figure and no document in the tree states it |
| U12 | `pmicClickStreak` on the Core2 | Core2 | A bench test of whether a PEK click during a touch wake produces a `M5.BtnPWR` click |
| U13 | The Plus2 IR emitter polarity | StickC Plus2 | A scope on G19 with a frame in flight, or a Plus2 schematic that shows the transistor stage. `irMarkLow` decides the RMT inversion, so a scope is the only settling evidence |
