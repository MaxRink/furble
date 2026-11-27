# 385 - HAL register verification pass 3

Status: implementation on `fix/hal-datasheet-verification`. Two driver defects
and one simulator-double defect fixed, with host cases that fail when each fix is
reverted. No firmware behaviour change to any board: nothing under `src/` or
`include/` changes, because both driver defects are in `lib/hal`, whose only
caller is `tests/host/hal_drivers_test.cpp`, and the third is a comment in a
simulator shim.

Related: [353](../353-hal-verification.md) (pass 2, the same four drivers),
[322](322-hardware-abstraction.md) (the drivers and the board gap
tables), [333](../333-datasheet-corpus.md) (the component pages this pass corrects),
[354](../382-gnss-datasheet-verification.md) (the page-level method this pass
follows).

## Why

Pass 2 closed four defects in the same four drivers. This pass reads the drivers
again and asks a different question: not "is the constant the one the document
prints", but "does the code do what the document says the part does at the level
of a level, a latch, a range and an order". The three classes it found were a
wait that could pass at zero delay, a clamp that turned an invalid field into a
valid one, and a register address in a model comment that named a different
register.

It also records what every other part on the list actually is: four of the eleven
named parts have no driver in this tree at all, which is a coverage gap rather
than a defect, and the record says so instead of implying the whole list is
covered.

## What was verified, and how

Every row below names the document revision and the section or page. Where this
pass could re-read the document text it says so; where it could only re-read the
component page's recorded citation, the row says that too. Nothing here is
VERIFIED from reading the code and agreeing with it.

Two sources are used that pass 2 did not open:

- The pinned M5Unified 0.2.20 sources under `.pio/libdeps/*/M5Unified`, which is
  the board support library the firmware actually links. It is not a datasheet,
  so it corroborates a register fact rather than settling one, but it is the
  second independent implementation of the same register map and a disagreement
  between it and a driver is a defect in one of the two.
- Mirrored text of the TI BQ27220 technical reference manual (SLUUBD4A, revised
  November 2022) on a document host, which renders pages 17, 22, 23, 25, 63 and
  64 of 91 as text. That is enough to re-read Table 2-1, sections 2.7 to 2.12,
  section 2.21, section 5.1 and section 5.3 verbatim.

## Defects

| # | Severity | Defect | Document | Fix | Test evidence |
| ---: | :--- | :--- | :--- | :--- | :--- |
| 1 | medium | `SX1262::sleep()` waited only for BUSY **low** after releasing NRESET, and `waitBusy()` samples the pin before its first delay. The **inferred** window between the reset release and the start of the calibration is a low line, so the ready wait returned at zero delay and SetSleep could go out while the command's accepted-from state was still STDBY. The header claimed the step "waits for the level", which it did only when the level had already changed. The inference is labelled as one: the document gives the mechanism (9.1 page 60 sets BUSY high in STARTUP at power-up or after a reset, 8.3.1 page 55 drops it as the radio leaves Sleep, 13.1.12 page 80 has it high during calibration) and no clause or table for the window itself. | DS.SX1261-2.W.APP Rev 2.2, sections 8.1 page 53, 8.3.1 page 55, 9.1 page 60, 13.1.12 page 80, and section 13.1.1 page 72 (SetSleep is accepted from STDBY only) | `sleep()` now waits for BUSY **high** within a new `BUSY_ASSERT_BUDGET_US` (1,000 us) and then for low within the existing 10,000 us ready budget. The header's total bound moves from 15,700 us to 16,700 us. The two early returns are unchanged in cost: a line that never rises spends 1,200 us, and a line stuck high spends the 200 us reset plus the 10,000 us ready budget, **10,200 us**, because the rise wait is satisfied by its first sample and never delays. Both sums are pinned exactly in the test. | `sx1262 waits for BUSY to rise after reset`, `sx1262 polls BUSY until it rises before SetSleep`, `sx1262 BUSY that never rises fails`, `sx1262 no command before BUSY has risen`, `sx1262 BUSY that never rises stops inside the assert budget`, the re-pinned `sx1262 header total bound is the documented 16,700 us`, and the two spend checks `sx1262 stuck-high spend is the reset pulse plus the ready budget` and `sx1262 stuck BUSY spends reset plus the ready budget, 10,200 us`. Reverting the added wait fails four of them, and the spend checks fail on any constant change that moves either sum. |
| 2 | medium | `BQ27220::read()` clamped StateOfCharge() to 100 when the word read above 100. The manual gives the field a range of 0 to 100 and **no** out-of-range sentinel, and every neighbouring 16-bit field in the same chapter does have one (`TimeToEmpty()` 0xFFFF, `ChargingVoltage()` and `ChargingCurrent()` 65535). An all-ones SOC word from a gauge that is not estimating was therefore reported as a full battery, which is the one answer that hides a dead gauge. | SLUUBD4A section 2.21 page 25 of 91 (range 0 to 100), section 2.11 page 23 (the 0xFFFF sentinel it does not have), sections 2.23 and 2.24 page 25 | The clamp is gone. A word above 100 makes `read()` return false and leaves `reading` untouched, which is the contract the header already had for a failed transfer. | `bq27220 soc above 100 is not a reading` (101), `bq27220 soc 0xFFFF is not a reading` and `bq27220 soc 100 is a reading`. Reverting the clamp fails the first two. |
| 3 | low | `sim/shim/M5PM1.h` documented `PWR_CFG` as register `0x00` in three places. `0x00` is `DEVICE_ID`; `PWR_CFG` is `0x06`. The code in the shim was already right, so this is a wrong comment in a double, the class of error that makes a later reader model the wrong register. | M5PM1 Chip User Manual V1.9 register list, `PWR_CFG` 0x06 page 13 of 33, as recorded in `docs/components/m5pm1.md`; M5Unified 0.2.20 `M5PM1_Class.cpp` line 15 (`DEVICE_ID` 0x00) and `:18` (`PWR_CFG` 0x06) | The three comments name 0x06 and the identity register, with the library lines cited. | No host case: a comment carries no behaviour. The behavioural half of the same shim has no test either: the platform PMIC cases drive `tests/host/boundaries/platform_shims/M5PM1.h`, a different double that already recorded 0x06 at its lines 25 to 34, and nothing asserts `sim/shim/M5PM1.h`'s LED or power-config path. |

Two archived plans still state the pre-change bound: plan 322's SX1262 row and
plan 353's defect 3 and decisions sections both say 15,700 us, which was the
total before this pass added the rise wait. They are historical records of what
those passes did and are not rewritten here; the current bound is 16,700 us and
`lib/hal/SX1262.h` is the only place it is stated as a contract.

### What defect 1 changes for a board

`sleep()` can now fail where it used to report success: a radio whose BUSY never
rises within 1 ms after the reset release returns false without sending
SetSleep. That is the intended reading of section 8.3.1 (a part that never raises
BUSY has not started), and the caller contract already says a false return means
the radio is not asleep and the caller must retry or hold NRESET low. The
alternative, keeping the zero-delay ready sample, is what risked a SetSleep
being dropped and the radio idling at 0.6 mA.

## Per-driver verification tables

Column meanings: **Fact** is what furble relies on, **Document** is title,
revision, section and page, **Code** is where it is implemented, **State** is
VERIFIED when this pass re-read the document text or a page the earlier pass
recorded with the page number, CORROBORATED when only a second implementation
agrees, and UNVERIFIED when nothing in the tree settles it.

### X-Powers AXP192 (board support, two register writes)

Document: AXP192 datasheet V1.2, Aug. 8 2011, 53 pages, section 9.11 page 41, as
recorded in `docs/components/axp192.md`. This pass re-read the recorded rows
against the code and did not reopen the PDF.

| Fact | Document | Code | State |
| :--- | :--- | :--- | :--- |
| `0x82` is ADC enable 1, `0x83` is ADC enable 2, `0x84` is the ADC sample rate | section 9.11, REG 82H to REG 84H, page 41 | `src/FurblePlatform.cpp:196-199` | VERIFIED |
| `0x82` bit 7 battery voltage, bit 6 battery current, bit 5 ACIN voltage, bit 4 ACIN current, bit 3 VBUS voltage, bit 2 VBUS current, bit 1 APS voltage, bit 0 TS pin, reset value 0x83 | section 9.11, REG 82H, page 41 | `include/FurblePlatformPolicy.h:47-48` | VERIFIED |
| Writing `0x82` = 0xCC keeps battery voltage, battery current, VBUS voltage and VBUS current and clears the rest | section 9.11, REG 82H, page 41 | `include/FurblePlatformPolicy.h:44-50` | VERIFIED |
| `0x83` reset value 0x80: bit 7 internal temperature monitoring, bits 3 to 0 GPIO0 to GPIO3 | section 9.11, REG 83H, page 41 | `include/FurblePlatformPolicy.h:48` | VERIFIED |
| The 7-bit address is 0x34 and the two writes name it | section 9.10 page 28 (TWSI) | `src/FurblePlatform.cpp:197-198` | VERIFIED |
| The write is gated on the detected PMIC being an AXP192, because Core2 v1.1 keeps the board ID while carrying an AXP2101 whose `0x82` and `0x83` are regulator controls | board fact, `docs/components/axp2101.md` | `src/FurblePlatform.cpp:196` | VERIFIED |

Two consequences this pass records rather than changes:

- Clearing `0x83` bit 7 turns the internal temperature channel off.
  `AXP192_Class::getInternalTemperature()` reads it from REG 5EH, so any future
  furble consumer of that call must re-enable the channel first. No furble code
  reads temperature today (`git grep getInternalTemperature` over `src/` and
  `include/` is empty), so the trim stands.
- M5Unified 0.2.20 `AXP192_Class.cpp` lines 196 to 200 computes the battery voltage as
  `(buf[0] << 4) + buf[1]` from a four-byte read at `0x78`. The AXP192 documents
  `0x79` bits 7:4 as the low four bits of the same conversion, so the vendor
  arithmetic does not shift that byte. This is a defect in the vendored library,
  not in furble, and it is recorded here because every AXP192 board's percentage
  comes through it.

### X-Powers AXP2101 (`lib/hal/AXP2101.cpp`, no shipped caller)

Document: X-Powers AXP2101 datasheet revision 1.0, 2021-05-31, sections 6.3,
6.10, 6.12.1, 6.12.2, 6.13.2.x, as recorded in `docs/components/axp2101.md`. This
pass re-read every constant against that page and cross-checked the same register
map against M5Unified 0.2.20 `AXP2101_Class.cpp`, which drives the part on the
Core2 v1.1.

| Fact | Document | Code | State |
| :--- | :--- | :--- | :--- |
| 7-bit address 0x34 (8-bit 0x68/0x69 shifted right) | section 6.3, page 18 | `lib/hal/AXP2101.h:33` | VERIFIED |
| REG 00 bit 5 VBUS good, bit 3 battery present | section 6.13.2.1, page 31 | `lib/hal/AXP2101.cpp:28-29`, `lib/hal/AXP2101.cpp:82`, `lib/hal/AXP2101.cpp:91` | VERIFIED, and `AXP2101_Class.cpp` line 202 and line 207 test the same bits |
| REG 01 bits 6:5 are the charge direction, 01 charging, 10 discharging, 00 standby, 11 reserved | section 6.13.2.2, page 31 | `lib/hal/AXP2101.cpp:117-136` | VERIFIED, and `AXP2101_Class.cpp` line 155 and line 162 decode the same field |
| REG 34 bits 7:6 are the ADC debug channel select and bits 5:0 are vbat[13:8]; REG 35 is vbat[7:0], 1 mV per count | sections 6.10 page 28 and 6.13.2.30 page 40 | `lib/hal/AXP2101.cpp:95-103` | VERIFIED, and `AXP2101_Class.cpp` line 590 and line 596 mask 0x3F the same way |
| REG A4 is the battery percentage, read only | section 6.13.2.88, page 57 | `lib/hal/AXP2101.cpp:105-115` | VERIFIED |
| Gauge module enable is REG 18 bit 3 | section 6.13.2.14, page 34 | `lib/hal/AXP2101.cpp:30`, `lib/hal/AXP2101.cpp:106` | VERIFIED; `AXP2101_Class.cpp` lines 98 to 100 write bit 1 as the charger enable, so the two bits are distinct fields |
| DCDC enables REG 80 bits 0 to 4 | section 6.13.2.68, page 50 | `lib/hal/AXP2101.cpp:51-55` | VERIFIED |
| LDO enables REG 90 bits 0 to 7 (ALDO1-4, BLDO1, BLDO2, CPUSLDO, DLDO1) and REG 91 bit 0 (DLDO2) | sections 6.13.2.75 and 6.13.2.76, page 53 | `lib/hal/AXP2101.cpp:56-66` | VERIFIED; `AXP2101_Class.cpp` lines 53 to 59 use `1 << num` over 0x92 to 0x97 for the same six outputs |
| Six LDO voltage registers REG 92 to 97, 500 mV at code 0 to 3500 mV at code 30 in 100 mV steps, code 31 reserved | sections 6.13.2.77 to 6.13.2.82, pages 54 and 55 | `lib/hal/AXP2101.cpp:162-172` | VERIFIED; `AXP2101_Class.cpp` lines 44 to 50 subtract 500 mV and clamps at 0x1E, the same encoding |
| Backup cell charger enable REG 18 bit 2, termination voltage REG 6A bits 2:0, 2.6 V to 3.3 V in 100 mV steps | section 6.12.2 page 29 and 6.13.2.67 page 50 | `lib/hal/AXP2101.cpp:174-185` | VERIFIED |
| Power key latch REG 49 bits 3:0, write 1 to clear; enables REG 41 bits 3:0 | sections 6.13.2.44 page 44 and 6.13.2.41 page 42 | `lib/hal/AXP2101.cpp:36-40`, `lib/hal/AXP2101.cpp:187-206` | VERIFIED; `AXP2101_Class.cpp` lines 212 to 213 read `0x49` masked 0x0C and writes the same bits back, and `:350` clears the status block with 0xFF, which only a write-1-to-clear register accepts |
| Soft power off REG 10 bit 0, restart REG 10 bit 1 | section 6.13.2.7, page 32 | `lib/hal/AXP2101.cpp:208-210` | VERIFIED; `AXP2101_Class.cpp` line 167 sets the same bit |
| VRTC has no enable bit anywhere in the register list | sections 6.13.1 page 29 and 6.13.2 page 31 onwards | `lib/hal/AXP2101.h:36-52` | VERIFIED |
| Identity: REG 03 reads 0x4A | no revision 1.0 or 1.4 definition; revision 0.1 section 8.1 page 19 gives chip_id 01_0111, 0x47 on A and 0x57 on B | `lib/hal/AXP2101.cpp:25-27` | CORROBORATED: M5Unified 0.2.20 `AXP2101_Class.cpp` lines 31 to 34 rejects any value but 0x4A, so two vendor sources agree the shipping part answers 0x4A. No datasheet revision does, and the row stays open as C-19a |

### Injoinic IP5306 (board support, policy only)

Document: IP5306 register document V1.21 (the file is named V1.4), section 3
pages 3 to 8, as recorded in `docs/components/ip5306.md`; M5Unified 0.2.20
`IP5306_Class.cpp`.

| Fact | Document | Code | State |
| :--- | :--- | :--- | :--- |
| `SYS_CTL0` is 0x00 and its bit 1 is the boost-always-on select | section 3, `SYS_CTL0`, page 3 | `include/FurbleIP5306.h:33`, `include/FurbleIP5306.h:36` | VERIFIED, and `IP5306_Class.cpp` line 33 names the same bit `BOOST_OUT_BIT = 0x02` |
| `SYS_CTL2` is 0x02 and bits 3:2 select the light-load window, 00 8 s, 01 32 s, 10 16 s, 11 64 s | section 3, `SYS_CTL2`, page 4 | `include/FurbleIP5306.h:34`, `include/FurbleIP5306.h:37-38`, `include/FurbleIP5306.h:51-63` | VERIFIED; the four-value table and the 64 s default agree with `lightLoadSeconds()` case by case |
| `setPowerBoostKeepOn()` is a read-modify-write of `SYS_CTL0` bit 1, so the readback proves the bit survived | `IP5306_Class.cpp` lines 113 to 121 | `include/FurbleIP5306.h:79-85` | VERIFIED |
| Register 0x78, which M5Unified decodes as a level, is not in the vendor register list | register list, pages 3 to 8 | M5Unified `IP5306_Class.cpp` lines 50 to 60 | UNVERIFIED, carried as C-20a |

### TI BQ25896 (no driver)

No file under `src/`, `include/`, `lib/hal/` or `sim/` names the part
(`git grep -il BQ25896` over those trees is empty). `docs/components/bq25896.md`
is the only record. This is a coverage gap, not a defect: there is no code to
disagree with the document.

### TI BQ27220 (`lib/hal/BQ27220.cpp`, no shipped caller)

Document: SLUUBD4A, April 2016, revised November 2022, 91 pages. Re-read as text
this pass: Table 2-1 page 17, sections 2.7 to 2.12 page 23, sections 2.21 to
2.27 page 25, sections 5.1 and 5.3 page 63, page 64.

| Fact | Document | Code | State |
| :--- | :--- | :--- | :--- |
| A standard command is a two-byte word at an even command code, least significant byte at the lower code | section 2.1, Table 2-1, page 17 | `lib/hal/BQ27220.cpp:32-38` | VERIFIED |
| Voltage() is 0x08 and 0x09, unsigned, mV, 0 to 6000 mV | section 2.6, page 22 | `lib/hal/BQ27220.cpp:14` | VERIFIED |
| BatteryStatus() is 0x0A and 0x0B | section 2.7, page 23 | `lib/hal/BQ27220.cpp:15` | VERIFIED |
| BatteryStatus() low byte bit 3 is BATTPRES and bit 0 is DSG | Table 2-6 page 23 | `lib/hal/BQ27220.cpp:21-22` | VERIFIED with a caveat, see the confusing-section row below and the `bq27220.md` note on the text render |
| Current() is 0x0C and 0x0D, signed, mA, updated every second | section 2.8, page 23 | `lib/hal/BQ27220.cpp:16`, `lib/hal/BQ27220.cpp:58` | VERIFIED |
| TimeToEmpty() is 0x16 and 0x17, minutes, and 65535 means the battery is not being discharged | section 2.11, page 23 | `lib/hal/BQ27220.cpp:17`, `lib/hal/BQ27220.h:32` | VERIFIED |
| StateOfCharge() is 0x2C and 0x2D, an unsigned percentage "with a range of 0 to 100", and the field has no out-of-range sentinel | section 2.21, page 25 | `lib/hal/BQ27220.cpp:18`, `lib/hal/BQ27220.cpp:59-70` | VERIFIED, and the clamp defect above is fixed against it |
| ChargingVoltage() 0x30, ChargingCurrent() 0x32, BTPDischargeSet() 0x34, BTPChargeSet() 0x36, OperationStatus() 0x3A | sections 2.23 to 2.27, page 25 | not used by the driver | VERIFIED, recorded so the command map is complete |
| 7-bit address fixed as 1010101, so 0xAA or 0xAB on the wire | section 5.1, page 63 | `lib/hal/BQ27220.h:29` | VERIFIED |
| At least 66 us of bus-free time between all packets, and 1-byte writes above 100 kHz | section 5.3, page 63 | `lib/hal/BQ27220.cpp:44-50`, `lib/hal/BQ27220.h:35` | VERIFIED |
| A read above command 0x6B is NACKed, so the five commands this driver reads are all inside the served range | section 5.1, page 63 | `lib/hal/BQ27220.cpp:27-29` | VERIFIED, and it is why 0x2C is the highest code the driver uses |
| No standard command more than twice per second, or the gauge watchdog can reset the part | section 5.3, page 64 | `lib/hal/BQ27220.h:46` (`MIN_POLL_INTERVAL_MS`) | VERIFIED from the page recorded in the component page; this pass's text mirror returned only the timing diagram on page 64 |

### XINLUDA XL9555 (`lib/hal/XL9555.cpp`, no shipped caller)

Document: XINLUDA XL9535/XL9555 datasheet Rev 2.3, section 9.1 Table 3 and
section 9.2.1 Table 6, section 9.2.5, as recorded in `docs/components/xl9555.md`.
The part is an input-compatible clone of the PCA9555, so the family document is
used as corroboration and is labelled as such.

| Fact | Document | Code | State |
| :--- | :--- | :--- | :--- |
| Address byte 0100 A2 A1 A0, so 7-bit 0x20 to 0x27 | section 9.1, Table 3 | `lib/hal/XL9555.h:27` | VERIFIED; PCA9555 Rev. 1 (25 April 2023) section 6.1 gives the same address form |
| Command bytes 0x00/0x01 input, 0x02/0x03 output, 0x04/0x05 polarity, 0x06/0x07 configuration | section 9.2.1, Table 6 | `lib/hal/XL9555.cpp:8-10` | VERIFIED; PCA9555 section 6.1 and Figures 6 to 10 give the same four pairs |
| A configuration bit of 1 is an input with the weak pull-up, 0 is an output | section 9.2.5 | `lib/hal/XL9555.cpp:24-34` | VERIFIED |
| The pointer walks inside a 16-bit port pair and wraps back, so a two-byte access from 0x03 answers ports 1 and 0 | section 9.2.1 | `tests/host/hal_drivers_test.cpp:215-221` (the `PORT_PAIR` model) | VERIFIED; PCA9555 documents it in words: "if the first byte is sent to Output Port 1 (register 3), the next byte is stored in Output Port 0", and on the read side "if Input Port 1 is read, the next byte read is Input Port 0" |
| Power-on state: configuration all inputs, output latch all ones, polarity zero | Table 6 | `tests/host/hal_drivers_test.cpp:493-498` | VERIFIED |

The address strap is the one thing the driver cannot express: `ADDRESS_BASE` is a
constant no code reads, because `RegisterBus` is already bound to one address.
A board with two expanders, or with A2 strapped high, has to pass the shifted
address to its own bus adapter. That is a coverage gap, not a defect, and it is
in the test-gap list below with the other unused address constants.

### Semtech SX1262 (`lib/hal/SX1262.cpp`, no shipped caller)

Document: DS.SX1261-2.W.APP Rev 2.2, December 2024, sections 8.1 page 53,
8.2.2 page 54, 8.3.1 page 55, 9.1, 9.3, 13.1.1, Table 3-5 page 17 and Table 8-2
page 56, as recorded in `docs/components/sx1262.md`.

| Fact | Document | Code | State |
| :--- | :--- | :--- | :--- |
| NRESET is a pulse, typically 100 us low | section 8.1, page 53 | `lib/hal/SX1262.h:84` (200 us), `lib/hal/SX1262.cpp:26-28` | VERIFIED |
| BUSY is high while the chip calibrates after reset and drops in STDBY_RC | section 9.1 page 60, section 8.3.1 page 55, section 13.1.12 page 80 | `lib/hal/SX1262.cpp:30-44` | VERIFIED, and the fix above is the code change this row produced. The low window after the release stays inferred, see defect 1 |
| SetSleep is accepted from STDBY only | section 13.1.1, page 72 | `lib/hal/SX1262.cpp:41-49` | VERIFIED |
| SetSleep is opcode 0x84 and takes one sleepConfig byte | section 13.1.1 with Table 13-1, page 72 | `lib/hal/SX1262.h:82-83` | VERIFIED: the opcode and the one-byte form were read on page 72 in the review of this PR. The sleepConfig bit encoding stays CITED from Table 13-2, which was not read |
| After SetSleep the chip must not see SPI for 500 us | section 13.1.1 Caution, page 72 | `lib/hal/SX1262.h:86`, `lib/hal/SX1262.cpp:52` | VERIFIED, read as the Caution on page 72 in the review of this PR |
| BUSY is held high for the whole sleep | section 9.3 | `lib/hal/SX1262.cpp:54-57` | VERIFIED |
| SLEEP to STDBY_RC is 340 us typical warm and 3500 us typical cold; the document has no reset-to-ready row | Table 8-2, page 56 | `lib/hal/SX1262.h:85` (10 ms ready budget) | VERIFIED |
| STDBY_RC is 0.6 mA and the sleep configuration here is 160 nA | Table 3-5, page 17 | `lib/hal/SX1262.h:11`, `lib/hal/SX1262.h:36` | VERIFIED |
| Known limitations: 500 kHz bandwidth modulation quality with a workaround in register 0x0889, PA clamping, implicit header timeout, inverted IQ | section 15, pages 109 and 110 | none, the driver only sleeps the part | VERIFIED, no erratum document exists for this part |

### TDK InvenSense MPU-6886 (shipped, `src/FurbleIMUMotionMPU6886.cpp`)

Document: DS-000193 revision 1.1, 03/29/2017, 59 pages, sections 5.1 page 24,
6.2 page 25, 8.17 page 37, 8.20 to 8.22 pages 39 and 40, 8.25 page 41, 8.28
page 42, 8.36 page 46, 8.43 page 49 and Table 15 pages 29 to 31, as recorded in
`docs/components/mpu6886.md`. Corroborated against M5Unified 0.2.20
`MPU6886_Class.cpp` lines 31 to 60.

| Fact | Document | Code | State |
| :--- | :--- | :--- | :--- |
| 7-bit address b110100X, so 0x68 with SA0 low | section 6.2, page 25 | `src/FurbleIMUMotionMPU6886.cpp:27` | VERIFIED |
| WHO_AM_I is 0x75 and reads 0x19 | section 8.43 page 49, Table 15 page 31 | `src/FurbleIMUMotionMPU6886.cpp:28-29`, `src/FurbleIMUMotionMPU6886.cpp:110` | VERIFIED |
| ACCEL_CONFIG is 0x1C and bits 4:3 are ACCEL_FS_SEL, 10b the plus or minus 8 g range | section 8.17, page 37 | `src/FurbleIMUMotionMPU6886.cpp:30`, `src/FurbleIMUMotionMPU6886.cpp:121` | VERIFIED, and `MPU6886_Class.cpp` line 38 writes the same 0x10 in its own init table, so the driver neither widens nor narrows the range the library already set |
| ACCEL_CONFIG_2 is 0x1D, the sample rate divider 0x19, INT_PIN_CFG 0x37 with bit 5 LATCH_INT_EN, INT_ENABLE 0x38, INT_STATUS 0x3A clear on read with WOM_X/Y/Z at bits 7/6/5 | Table 15 pages 29 and 30, sections 8.25 page 41 and 8.28 page 42 | `src/FurbleIMUMotionMPU6886.cpp:31-35`, `src/FurbleIMUMotionMPU6886.cpp:56-57`, `src/FurbleIMUMotionMPU6886.cpp:68-75` | VERIFIED |
| ACCEL_INTEL_CTRL is 0x69 with ACCEL_INTEL_EN at bit 7 and ACCEL_INTEL_MODE at bit 6 | section 8.36, page 46 | `src/FurbleIMUMotionMPU6886.cpp:39`, `src/FurbleIMUMotionMPU6886.cpp:138` (0xC2) | VERIFIED for bits 7 and 6; bit 1, which 0xC2 also sets, is not defined by the sections read and stays UNVERIFIED |
| PWR_MGMT_1 0x6B bit 6 SLEEP and bit 5 CYCLE, PWR_MGMT_2 0x6C gyro-axis standby bits 2:0 | Table 15, page 30 | `src/FurbleIMUMotionMPU6886.cpp:116-122`, `src/FurbleIMUMotionMPU6886.cpp:147-148` | CORROBORATED: the sequence matches M5Unified's init and the vendor wake-on-motion example, and only the register addresses were re-read |
| The three WOM thresholds are 0x20, 0x21 and 0x22 | Table 15 page 29, sections 8.20 to 8.22 pages 39 and 40 | `src/FurbleIMUMotionMPU6886.cpp:36-38`, `src/FurbleIMUMotionMPU6886.cpp:134-137` | VERIFIED, including the vendor defect in section 5.1 step 4 (it names 0x1F, which Table 15 does not contain) |
| One WOM threshold count is 4 mg | nothing in the document | `include/FurbleIMUMotionScale.h:118` | UNVERIFIED, carried as C-26a with the bench tilt test it needs |

Restore-path check this pass added: `disarm()` puts ACCEL_CONFIG_2, SMPLRT_DIV
and INT_PIN_CFG back to `0x00`, `0x03` and `0xC0`. All three are M5Unified's own
init values (`MPU6886_Class.cpp` lines 39 to 44), and ACCEL_CONFIG is not restored because
the driver writes the same `0x10` the library already wrote. The three constants
are therefore correct as a restore, not as a guess.

### Bosch BMI270 (shipped, `src/FurbleIMUMotionBMI270.cpp`)

Document: BST-BMI270-DS000-08 revision 1.6, March 2026, 150 pages, sections
4.8.2 pages 43 and 44, 5.2.1 page 75, 5.2.28 page 82, 5.2.33 page 85, 5.2.39
page 87, 5.2.40 pages 88 to 93 with the feature pages on 92 and 93, 5.2.58
page 109, 5.2.60 and 5.2.61 page 111, 5.2.63 page 112, 5.2.84 page 119, 5.2.85
page 120, as recorded in `docs/components/bmi270.md`. Corroborated against
M5Unified 0.2.20 `BMI270_Class.cpp` lines 37 to 64.

| Fact | Document | Code | State |
| :--- | :--- | :--- | :--- |
| CHIP_ID 0x00 reads 0x24 | section 5.2.1, page 75 | `src/FurbleIMUMotionBMI270.cpp:27-28`, `src/FurbleIMUMotionBMI270.cpp:199` | VERIFIED |
| INT_STATUS_0 0x1C, any-motion bit 6 and no-motion bit 5 | section 5.2.28, page 82 | `src/FurbleIMUMotionBMI270.cpp:30`, `src/FurbleIMUMotionBMI270.cpp:305-308`, `src/FurbleIMUMotionBMI270.cpp:251` | VERIFIED |
| INTERNAL_STATUS 0x21, message field bits 3:0, 0x01 is init OK | section 5.2.33, page 85 | `src/FurbleIMUMotionBMI270.cpp:33-35`, `src/FurbleIMUMotionBMI270.cpp:206-207` | VERIFIED |
| FEAT_PAGE 0x2F and FEATURES 0x30 form a 16-byte window | sections 5.2.39 page 87 and 5.2.40 page 88 | `src/FurbleIMUMotionBMI270.cpp:38-40`, `src/FurbleIMUMotionBMI270.cpp:155-166` | VERIFIED |
| Any-motion is page 1 offset 0x0C with duration bits 12:0 in 50 Hz samples (20 ms) and threshold bits 10:0 over 0 to 1 g, default 0xAA = 83 mg; no-motion is page 2 offset 0x00, default 0x90 = 70 mg | section 5.2.40, pages 92 and 93 | `src/FurbleIMUMotionBMI270.cpp:83-85`, `src/FurbleIMUMotionBMI270.cpp:100-116` | VERIFIED |
| INT1_IO_CTRL 0x53 bit 3 output enable, bit 2 open drain, bit 1 level, so 0x08 is push-pull active low | section 5.2.58, page 109 | `src/FurbleIMUMotionBMI270.cpp:43-46`, `src/FurbleIMUMotionBMI270.cpp:233` | VERIFIED |
| INT_LATCH 0x55, 0x00 non-latched | section 5.2.60, page 111 | `src/FurbleIMUMotionBMI270.cpp:49`, `src/FurbleIMUMotionBMI270.cpp:234` | VERIFIED |
| INT1_MAP_FEAT 0x56 shares the INT_STATUS_0 bit assignment | section 5.2.61, page 111 | `src/FurbleIMUMotionBMI270.cpp:51`, `src/FurbleIMUMotionBMI270.cpp:235-236` | VERIFIED |
| INT_MAP_DATA 0x58 bits 3:0 map data interrupts to INT1 and must be cleared before the output is enabled | section 5.2.63, page 112 | `src/FurbleIMUMotionBMI270.cpp:58-59`, `src/FurbleIMUMotionBMI270.cpp:231-232` | VERIFIED |
| PWR_CONF 0x7C bit 0 is advanced power save and blocks the feature window; PWR_CTRL 0x7D bit 2 is the accelerometer enable | sections 5.2.84 page 119 and 5.2.85 page 120 | `src/FurbleIMUMotionBMI270.cpp:72-76`, `src/FurbleIMUMotionBMI270.cpp:212-217`, `src/FurbleIMUMotionBMI270.cpp:338-359` | VERIFIED |
| The feature window must be written with advanced power save off and the accelerometer on | sections 5.2.84 and 5.2.85, and the ordering `bmi270.c` uses | `src/FurbleIMUMotionBMI270.cpp:224-259` | VERIFIED |
| Bits 14:11 of the threshold word are the output configuration and the driver preserves them | section 5.2.40, page 92 | `src/FurbleIMUMotionBMI270.cpp:88-96`, `src/FurbleIMUMotionBMI270.cpp:169-176` | UNVERIFIED: the driver's comment says Bosch never writes them, and whether preserving the configuration-file value is equivalent to clearing it is not settled by the field list |

### M5Stack M5PM1 (shipped, board support through the pinned library)

Document: M5PM1 Chip User Manual V1.9, pages 6 to 11 and 13 to 21 of 33, as
recorded in `docs/components/m5pm1.md`; M5Unified 0.2.20 `M5PM1_Class.cpp`.

| Fact | Document | Code | State |
| :--- | :--- | :--- | :--- |
| `DEVICE_ID` is 0x00 and `PWR_CFG` is 0x06, reset 0x17 with the 5 V boost off | register list page 6 to 11, section 2 register (7) page 13 | `sim/hardware_model.cpp:906-925` | VERIFIED, and `M5PM1_Class.cpp` line 15 and line 18 name the same two addresses. The simulator shim comment that named 0x00 is defect 3 above |
| `PWR_CFG` bit 0 charge, bit 1 DCDC, bit 2 LDO, bit 3 boost, bit 4 LED | section 2 register (7), page 13 | `sim/hardware_model.cpp:910-914` | VERIFIED, and `M5PM1_Class.cpp` lines 40 to 44 name the same five bits |
| `I2C_CFG` 0x09, `SLP_TO` bits 3:0 with 0 disabling idle sleep | section 2 register (10), page 15 | `M5PM1_Class.cpp` line 64 | VERIFIED |
| The first I2C transaction after idle sleep fails and must be retried once | section 2 register (10) note 2, page 15 | `src/FurblePlatform.cpp:696-708` and the `m5pm1Access` wrapper | VERIFIED |
| `WDT_CNT` 0x0A seconds, `WDT_KEY` 0x0B fed 0xA5 | section 2 registers (11) and (12), page 15 | `src/FurblePlatform.cpp:331-368` | VERIFIED |
| `SYS_CMD` 0x0C, key 0xA in bits 7:4, 0xA1 power off | section 2 register (13), page 15 | `src/FurblePlatform.cpp` power-off path | VERIFIED |
| `WAKE_SRC` 0x05 is read-clear only | section 2 register (6), page 13 | `src/FurblePlatform.cpp:1097-1107` | VERIFIED |
| IRQ Status 1 0x40 and Status 2 0x41 are latches, and the clear is a write through the library's `irqClearSysAll`/`irqClearGpioAll`, not a mask write | section 2 pages 19 to 21, and `CLAUDE.md` hardware traps | `src/FurblePlatform.cpp:274-284` | VERIFIED, and `M5PM1_Class.cpp` lines 187 to 197 implements the clear as a write of 0x00 to 0x40, 0x41 and 0x42 |

### TI TCA8418 (no driver)

No file under `src/`, `include/`, `lib/hal/` or `sim/` names the part.
`docs/components/tca8418.md` is the only record. Coverage gap, not a defect.

## Documented defects and confusing sections

| Document | What it says | What the rest of it says, or what the code does | Where |
| :--- | :--- | :--- | :--- |
| MPU6886-DS DS-000193 revision 1.1 | Section 5.1 step 4: "Set the motion threshold in ACCEL_WOM_THR register (0x1F)" | Table 15 has no 0x1F entry. The thresholds are 0x20, 0x21 and 0x22, and sections 8.20 to 8.22 confirm it. The driver follows the register map | section 5.1 page 24 against Table 15 page 29 |
| MPU6886-DS | Section 5.1 numbers its steps 1 to 5 and then 7 | No step is missing in substance; step 7 sets the wake frequency through SMPLRT_DIV | section 5.1, page 24 |
| MPU6886-DS | Section 5.1 step 4 also gives no unit for the threshold | The document gives no mg per count figure anywhere, so the scale in `FurbleIMUMotionScale.h` is labelled an assumption | sections 8.20 to 8.22, pages 39 and 40 |
| AXP2101 revision 1.0 | REG 90 bit 4 is printed "aldo1 enable", a second copy of bit 0 in the same table | Revision 1.4 prints "bldo1 enable", revision 0.1's field name is `bldo1_en`, and the revision 1.4 history records "Revise reg 90 in Chapter6.13". The driver reads BLDO1 | section 6.13.2.75, page 53 |
| AXP2101 every revision | Register 0x03 is in the revision 0.1 map only, and it gives 0x47 or 0x57 | The shipping part answers 0x4A, which two vendor libraries check. The driver keeps 0x4A and the row stays open | revision 0.1 section 8.2 page 22 against `docs/components/axp2101.md` C-19a |
| BQ27220 SLUUBD4A | Table 2-1 page 17 prints `Current()` with the unit mAh, and names 0x2C `RelativeStateOfCharge()` | Section 2.8 page 23 says mA, and section 2.21 page 25 is headed `StateOfCharge()` | page 17 against pages 23 and 25 |
| BQ27220 SLUUBD4A | Table 2-6 page 23 is a two-column bit table, and the text render of it interleaves the High Byte and Low Byte columns | Two independent readings of the field order disagree. The Flipper Zero firmware `bq27220.h` struct (low byte, low bit first: DSG, SYSDWN, TDA, BATTPRES, AUTH_GD, OCVGD, TCA, RSVD) puts BATTPRES at bit 3, which is what the driver uses. The table is recorded as confusing, and BATTPRES stays at bit 3 on that corroboration | Table 2-6, page 23 |
| SX1262 DS.SX1261-2.W.APP | The revision history records "Modified Startup and sleep in Table 8-3" | The driver cites Table 8-2, which pass 2 also could not reopen. No value depends on the table number, only on the 3.5 ms it sizes the ready budget from | Rev 2.2 revision history, carried from plan 353 |
| IP5306 register document | Section 2 item 1 says the standard part has no I2C and an I2C build is a separate order | Both datasheets tick the I2C column for the plain part number | section 2 page 2 against IP5306-DS101 section 5 page 3 |

## Coverage gaps, not defects

| Part | Gap |
| :--- | :--- |
| BQ25896 | No driver and no caller anywhere in the tree. Documented only |
| TCA8418 | No driver and no caller anywhere in the tree. Documented only |
| AXP2101 | Exercised only by `tests/host/hal_drivers_test.cpp`; no firmware call site, so no board has run this driver |
| BQ27220 | Same: the only caller is the HAL test |
| SX1262 | Same: the only caller is the HAL test |
| XL9555 | Same: the only caller is the HAL test |
| AXP2101, BQ27220, XL9555 | `ADDRESS` and `ADDRESS_BASE` are never read by driver code, and the fake bus ignores the address, so a wrong address passes every case. The XL9555 strap (A2 to A0) cannot be expressed through `RegisterBus` at all |

## Facts still unverified

Each row names the measurement or document a device would need.

| ID | Missing | What settles it |
| :--- | :--- | :--- |
| U-1 | AXP2101 identity 0x4A at register 0x03 | A read of register 0x03 on a Core2 v1.1 or a StickS3-generation board with an AXP2101, or a vendor statement. Two libraries agree on 0x4A and no datasheet does |
| U-2 | AXP2101 backup cell charge current | Datasheet revision 1.4 section 6.12.2, not reopened this pass; revision 0.1 gives 100 uA |
| U-3 | MPU-6886 wake-on-motion mg per count | The document states no scale. A bench tilt test on a StickC, Plus or Core2 with a measured acceleration and the first level that wakes the part |
| U-4 | MPU-6886 ACCEL_INTEL_CTRL bit 1, which the driver sets through 0xC2 | A readback of register 0x69 after `arm()` on a StickC, or the section that defines bit 1 |
| U-5 | BMI270 behaviour of the preserved out_conf bits 14:11 | Bosch's own any-motion configuration code and a scope on INT1 with the config-file value against a cleared one |
| U-6 | SX1262 BUSY rise time after the NRESET release, which sizes `BUSY_ASSERT_BUDGET_US` | A scope on BUSY from the NRESET rising edge on a board that carries the part |
| U-7 | **closed by the review of this PR**: SX1262 SetSleep opcode 0x84 is Table 13-1 page 72 and the 500 us no-SPI window is the Caution on the same page. The sleepConfig bit encoding (Table 13-2) stays open | none for the opcode or the window; Table 13-2 for the encoding |
| U-8 | **closed by the review of this PR**: the twice-per-second rule is section 5.3 page 64 of SLUUBD4A verbatim. This pass's text mirror rendered that page as the timing diagram only | none, read on page 64 |
| U-9 | IP5306 register 0x78, the level M5Unified decodes | An Injoinic register list that includes 0x78, or an application note |
| U-10 | XL9555 address strap on a board with more than one expander | The board schematic, and a driver change to pass the shifted address |
| U-11 | The AXP192 battery voltage arithmetic M5Unified uses for every AXP192 board | A bench comparison of `M5.Power.getBatteryLevel()` against a meter at two pack voltages |

## The citation gate's pinned token count moved

`tests/test_check_datasheet_citations.py` pins the number of register and timing
tokens the gate sees in the real tree, so a coverage regression fails there
instead of silently in a page. This pass adds one token,
`BUSY_ASSERT_BUDGET_US`, whose comment carries a section 8.3.1 reference, and
that block comment also covers the run's other constants up to sixteen lines, so
`SLEEP_CONFIRM_BUDGET_US` is now attributed to the run rather than reported as a
page gap. The pin moves from `(34, 30, 4)` to `(35, 32, 3)`: one more token, and
one gap that is a citation rather than a gap. No other count changes, and the
per-file count for `lib/hal/SX1262.h` moves from 6 to 7. The `SLEEP_CONFIRM`
gap row stays on the SX1262 page, because the constant still has no tabulated
value behind it; the gate no longer requires the row.

## Gates

Run in `/tmp/furble-halverify` on this branch.

| Command | Result |
| :--- | :--- |
| `cmake -S tests/host -B /tmp/halverify-host-build` | configures the host suite |
| `cmake --build /tmp/halverify-host-build --target hal_drivers_test` | builds with `-Wall -Wextra -Werror` |
| `/tmp/halverify-host-build/hal_drivers_test` | `hal-drivers: PASS (241 checks)`, up from 219 |
| `python3 -m unittest tests.test_check_datasheet_citations` | OK after the pin above |
| `sh tools/run_sanitized_host_tests.sh '^hal-drivers$'` | `sanitized host tests: PASS`, 1 of 1 under ASan and UBSan |
| `ctest --test-dir /tmp/halverify-host-build --output-on-failure` | `100% tests passed out of 223` after the rebase, 221 before it |
| `python3 tools/pre_review.py --base origin/master` | 13 gates pass, exit 0, including the datasheet citation gate and the documentation citation gate |
| `python3 tools/check_datasheet_citations.py` | `32 cited, 3 page gaps, 0 errors of 35 tokens` |
| `python3 tools/check_doc_citations.py` | `0 errors of 3229 citations in 176 files` |
| `sh sim/scripts/check-doc-tokens.sh` | `sim documentation tokens are present` |
| `python3 tools/check_portability_inventory.py` | `portable contract: clean` |
| `clang-format --dry-run --Werror` on the six changed C and C++ files | clean, clang-format 21.1.2 |

The two spend checks were added in the review remediation: `sx1262 stuck-high
spend is the reset pulse plus the ready budget` compares the constant sum with
the literal 10,200 and `sx1262 stuck BUSY spends reset plus the ready budget,
10,200 us` compares the measured spend with it, so a stated cost that drifts from
the measured one fails rather than reading correctly.

Mutation checks, each run by reverting one change and rebuilding. Reverting the
SX1262 BUSY-rise wait fails `sx1262 polls BUSY until it rises before SetSleep`,
`sx1262 BUSY that never rises fails`, `sx1262 no command before BUSY has risen`
and `sx1262 BUSY that never rises stops inside the assert budget`. Restoring the
BQ27220 clamp fails `bq27220 soc above 100 is not a reading` and `bq27220 soc
0xFFFF is not a reading`. The tree was restored and the suite re-ran green after
each.

## Review of the fix

The independent review of PR 540 confirmed the direction of both driver fixes and
found two blockers and eight smaller findings. Every one is recorded with its
disposition in [540-review-feedback.md](540-review-feedback.md). The two blockers
were a stated cost the driver does not spend and a page renumbered under its own
citations:

- The stuck-BUSY spend was stated as 11,200 us. It is 10,200 us, because a line
  that is already high satisfies the rise wait on its first sample and the assert
  budget is never spent. The header, this plan and the check that guards the
  bound are corrected, and two host cases now pin the sum exactly rather than
  bounding it.
- `docs/components/sx1262.md` renumbered its constants table but left eight
  citations on the lines the added header comment had moved. Every citation in
  that page was re-derived from the post-change source and checked line by line.

## Left open

- The defect 1 fix makes a radio whose BUSY never rises a failure instead of a
  silent pass. No board carries the part yet, so the change is exercised only by
  the host cases.
- The window the rise wait guards is an inference, not a datasheet statement: no
  clause says BUSY is low between the NRESET release and the start of the
  calibration. The wait is safe either way, because a line that is already high
  satisfies it without a delay, and U-6 is the scope measurement that would
  settle it.
- `BUSY_ASSERT_BUDGET_US` is a driver choice, like `SLEEP_CONFIRM_BUDGET_US`. It
  is 1 ms against a rise the document does not bound; a scope on a real part is
  the only thing that can size it (U-6).
- The BQ27220 change makes a gauge that reports an out-of-range SOC return no
  reading at all. A caller that used to show 100 percent on such a part now keeps
  its last good sample, which is the intended behaviour but is a visible change
  the first board with the gauge will see.
- `lib/hal` is still outside the clang-format gate's directory list, so the
  driver formatting in this branch was checked by hand with clang-format 21.
