# 322 - Hardware abstraction for the next boards

Status: hal-1 and hal-2 implemented, not merged. Branches `refactor/hal-1-drivers` (pure drivers) and
`refactor/hal-2-interfaces` (board profile and power backend, existing boards
only). No new board env, no support claim and no hardware run in this work.

Related: [287](287-mcu-portability-and-bundle-hardware.md) (board survey),
[127](127-nordic-port-baseline.md) (port contract and the coupling ratchet
from `refactor/port-1-ratchet`), [30](30-m5stack-framework.md) (board support
library gaps). The porting stack (`refactor/port-1-ratchet`,
`refactor/port-2-caps`, `refactor/port-3-routing`) is the base for hal-2.

## Why

The hardware research of 2026-11-13 ranks four next boards: LilyGO T-Beam
Supreme, M5 Cardputer Adv, LilyGO T-LoRa Pager and LilyGO T-Watch Ultra. Three
of them are not M5 boards, so M5Unified cannot detect them and every
`M5.getBoard()` switch falls through to its default. Today a new board means
editing about thirty scattered switches. The goal is that a new board costs one
profile row, one panel config and the chip drivers it lacks.

## Phase 1 audit

Pinned versions (`platformio.ini`): M5Unified 0.2.20, M5GFX 0.2.27, M5PM1
1.0.6, ESP-IDF 5.5.3 (`framework-espidf@3.50503.0`). Read from the installed
library sources.

### What the pinned dependencies already provide

| Chip or function | Pinned support | Usable on a non-M5 board | Gap |
| :--- | :--- | :--- | :--- |
| AXP2101 PMIC | `m5::AXP2101_Class` | Yes. The constructor takes any `I2C_Class` and address, and `In_I2C.begin(port, sda, scl)` takes custom pins. `Power_Class::begin()` only selects it for known boards, so `M5.Power` stays `pmic_unknown` on a LilyGO board. | `setBACKUP()` is an empty body (VBACKUP charge is commented out). No DCDC enable. `getBatteryLevel()` reads REG A4 without checking battery present or gauge enable. No rail readback for DCDC or DLDO2. Not host-testable. |
| BQ27220 gauge | `m5::BQ27220_Class` | Yes, same constructor shape. | Voltage and current only. No StateOfCharge, TimeToEmpty or BatteryStatus. |
| BQ25896 charger | none | - | Charge state on the Pager; the BQ27220 current sign covers charging versus discharging, so no driver yet. |
| XL9555 IO expander | none (M5Unified has PI4IOE5V6408 and M5IOE1 behind `IOExpander_Base`) | - | Needed for Pager GPS_EN and Watch Ultra rails. |
| TCA8418 keyboard | none in M5Unified (lives in the separate M5Cardputer library) | - | Cardputer Adv and Pager keyboards. |
| SX1262 radio | none | - | Must be put to sleep on T-Beam, Pager, Watch Ultra and the Cardputer Cap. |
| SH1106 OLED | `lgfx::Panel_SH110x` with `Bus_I2C` | Yes | No 128x64 1-bit UI tier in furble. |
| ST7789 / ST7789V2 | `Panel_ST7789` | Yes; Cardputer Adv is a known board (`board_M5CardputerADV`) | None. |
| ST7796U | not present (only a comment in `Panel_Device.hpp`) | - | Add a `Panel_LCD` subclass with the ST7796 init table from upstream LovyanGFX (FreeBSD licence). |
| CO5300 AMOLED | `Panel_CO5300` on `Panel_AMOLED` | Yes. `Bus_SPI` has `pin_io0`..`pin_io3` for quad SPI on the S3. | AMOLED has no backlight; brightness is a panel command. |
| CST9217 touch | not present (CST816S, CST226, FT5x06, GT911, CHSC6540 only) | - | New `ITouch` subclass for the Watch Ultra. |
| Rotary encoder | none needed: ESP-IDF PCNT | - | Pager input class. |
| Cardputer Adv battery | `Power_Class` ADC on GPIO10, ratio 2.0 | Yes, M5 board | None. |

The rule that follows: on M5 boards keep `M5.Power`, `M5PM1` and M5GFX as they
are. Write a register driver only where the table shows a gap, and put it in
`lib/hal/` so it is host-tested.

### Where furble assumes M5 hardware (master `e66e8807`)

`M5.getBoard()` appears 31 times: `src/FurblePlatform.cpp` 15,
`src/FurbleUI.cpp` 11, and one each in `FurbleBootScreen.cpp`,
`FurbleFeedback.cpp`, `FurbleIR.cpp`, `FurbleSD.cpp` and `FurbleTimeKeeper.cpp`.

| Path | Where | Board knowledge today |
| :--- | :--- | :--- |
| Battery caps and capacity | `Platform::initBattery()` | Per-board table: AXP192 boards full caps, StickS3 no current, Plus2 ADC, Core IP5306 level only, everything else none. |
| Battery read | `Platform::readBattery()` | `M5.Power` everywhere except the StickS3, which reads M5PM1 VBAT and GPIO0 charge (active low). Headless `main.cpp` and the UI also read `M5.Power` directly (port-3 moves these behind Platform). |
| Charging | same | `M5.Power.isCharging()` or M5PM1 GPIO0. |
| GPS rail | `FurbleGPS.cpp`, `main.cpp`, `FurbleUI.cpp` | `M5.Power.setExtOutput(…, ext_PA)` (port-3 routes it through GPS). |
| Power off | `Platform::powerOff()` | M5PM1 `shutdown()` on the StickS3, `M5.Power.powerOff()` elsewhere. UI hides power policies on the Core (IP5306). |
| Timed wake | `Platform::canTimedWake()`, `powerOffUntil()`, `consumeTimedWake()` | StickS3 M5PM1 timer plus RTC RAM marker; Plus2 BM8563 alarm plus GPIO4 hold. |
| PMIC button | `Platform::update()` | StickS3 polls M5PM1 at 50 Hz; StickC, StickC Plus and Tough fake a click streak (`m_PMICHack`). |
| Motion wake | `armMotionWake()` and friends | GPIO35 on StickC and StickC Plus, M5PM1 GPIO4 to GPIO13 on the StickS3. |
| Status LED at display off | `Platform::setDisplayOff()` | StickS3 M5PM1 PWR_CFG bit 4. |
| Watchdog | `watchdogEnable()`/`Feed()` | StickS3 M5PM1 only. |
| RTC backing | `knownBatteryBackedRtc()` in TimeKeeper | StickC, StickC Plus, Core2. |
| IR pin | `pinForBoard()` in FurbleIR | G9 StickC/Plus, G19 Plus2, G46 StickS3. |
| SD bus | `isCoreBoard()` in FurbleSD | Core and Core2 share the display SPI bus. |
| Boot name | `boardName()` | Short name per board. |
| Feedback outputs | `Feedback::init()` | LED pin, buzzer, vibration per board. |
| Brightness floor | `UI::UI()` | 48 on StickC/Plus, 32 elsewhere. |
| Modem sleep switch | UI power menu | StickS3 only. |
| Display sleep | `UI` display state machine | Holds `ESP_PM_APB_FREQ_MAX` while lit, waits the 120 ms SLPIN/SLPOUT dwell, sleeps through `M5.Display`. |
| Buttons and touch | `FurbleUI.cpp` | `M5.BtnA/B/C/PWR`, `M5.Touch`. |
| IMU | `FurbleIMU*.cpp`, console, UI | `M5.Imu` plus BMI270 and MPU6886 engines on `M5.In_I2C`. |

The remaining UI switches (layout, legends, floating indicators) are UI
geometry, not hardware, and stay in the UI.

### Per-board gaps

T-Beam Supreme (ESP32-S3, 8 MB flash, 8 MB PSRAM). Pins from LilyGO's
`examples/Factory/utilities.h` and the rail map from `LoRaBoards.cpp` in
LilyGo-LoRa-Series:

| Need | Chip and pins | Covered by | Gap |
| :--- | :--- | :--- | :--- |
| Battery %, V, charging | AXP2101 on I2C1 SDA 42 SCL 41, IRQ 40 | `lib/hal/AXP2101` | Board adapter (I2C bus) |
| GPS rail | ALDO4 3.3 V; GPS UART RX 9 TX 8, `GPS_EN_PIN` 7, PPS 6, 9600 baud | `AXP2101::setRail(ALDO4)` | Route the GPS rail through the power backend |
| GPS backup | V_BCKP on AXP2101 VRTC (RTCLDO1), always on while VBAT is present | No code: VRTC has no switch | LilyGO disables VBACKUP charge; keep it off unless a cell is fitted |
| LoRa | SX1262 CS 10, SCK 12, MOSI 11, MISO 13, RST 5, BUSY 4, DIO1 1, on ALDO3 | `lib/hal/SX1262::sleep()` | SPI port adapter, NSS hold through sleep; or switch ALDO3 off |
| Sensors and SD | ALDO1/2 sensors, BLDO1/2 SD, DCDC3/4/5 M.2 | `AXP2101::setRail()` | Switch off what furble does not use |
| Display | SH1106 128x64 on I2C SDA 17 SCL 18 | `Panel_SH110x` + `Bus_I2C` | 1-bit 128x64 UI tier |
| Input | BOOT (GPIO0) only, plus AXP2101 PEK | `AXP2101::readPowerKey()` | One-button input class |
| RTC | PCF8563 IRQ 14, 32 kHz crystal on XTAL_32K | `m5::RTC8563_Class` on a custom bus | Profile row `rtcBatteryBacked` |
| IMU | QMI8658 on SPI (CS 34, INT 33) | none | Motion backend or software only |

T-Watch Ultra: AXP2101 (as above), CO5300 over QSPI (M5GFX), CST9217 touch
(missing driver), MIA-M10Q GNSS (UBX, owned by the `feat/ubx-*` stack), V_BCKP
from VRTC, SX1262 sleep, crystal. Gaps: CST9217 `ITouch`, round AMOLED UI
geometry, brightness as a panel command.

T-LoRa Pager: ST7796U 480x222 (missing panel class), BQ27220 (`lib/hal`),
BQ25896 (not needed yet), XL9555 with GPS_EN (`lib/hal`), TCA8418 keyboard
(missing), rotary encoder (PCNT), MIA-M10Q (UBX stack), SX1262 sleep. Gaps:
ST7796 panel, keyboard and encoder input classes, wide UI layout.

Cardputer Adv: M5Unified knows the board, display (ST7789V2) and ADC battery.
Gaps: TCA8418 keyboard input class, a profile row, and the Cap LoRa-1262 radio
sleep if the Cap is fitted.

## Design

### Phase 3: pure drivers (`lib/hal/`, branch hal-1)

- `HalBus.h`: `RegisterBus` with `read`/`write` plus `read8`, `write8`,
  `update8`. The SX1262 has its own `SX1262::Port` (reset, BUSY, one SPI
  transfer, delay).
- `AXP2101`: probe, battery present, VBUS good, VBAT, percent (refused without
  battery or gauge), charge direction, rail on/off with a protected-rail mask,
  ALDO/BLDO voltage, VBACKUP charge, PEK interrupt enable and read-and-clear,
  soft power off. VRTC has no enable bit in the register map, so `Rail` has no
  VRTC member and power off keeps it up. Datasheet note: revision 1.0 section
  6.13.2.75 labels REG 90 bit 4 "aldo1" twice; it is BLDO1.
- `BQ27220`: one `read()` of Voltage, BatteryStatus (BATTPRES, DSG), Current,
  TimeToEmpty and StateOfCharge. Read-only; never unseals or writes.
- `XL9555`: input, output (level written before direction), write, read.
  Read-modify-write against the live register, no cache.
- `SX1262::sleep()`: NRESET pulse, bounded BUSY wait, SetSleep 0x84 0x00, 500
  us settle, BUSY high check.

Sources: X-Powers AXP2101 datasheet revision 1.0 (2021-05-31); TI SLUUBD4A
(BQ27220 TRM, revised November 2022); XINLUDA XL9535/XL9555 Rev 2.3; Semtech
DS.SX1261-2.W.APP Rev 2.2 (December 2024). Each register carries its section.
`lib/hal` is a portable contract root in `tools/check_portability_inventory.py`,
so no SDK token can enter it.

### Phase 2: interfaces (branch hal-2)

`include/FurbleBoard.h` and `src/FurbleBoard.cpp`: one `Board::Profile` row
per board model. The row is chosen from `M5.getBoard()` because one image
serves several boards: the StickC Plus image also boots on a Plus2, the Core2
image on a Tough and both Core2 PMIC variants. `profileFor()` is a switch that
returns a reference to a constant row, so `profile()` costs what the old
switches cost and needs no init order. Rows carry the short name, battery caps
and capacity, RTC backing, IR pin, motion wake path and pin, timed-wake kind,
power hold pin, PMIC click streak, SD slot, `canPowerOff`, `modemSleep`, the
brightness floor and the feedback outputs. Compile-time facts stay in
`FurbleBoardCaps.h` from port-2; UI geometry stays in the UI.

`include/FurblePowerBackend.h`: the contract a board's power hardware meets.
It names battery caps and sample (moved here from `Platform`, which inherits
the same `battery_caps_t` and `battery_t` names) and the named rail `GPS`,
the only rail with a caller. `meetsPowerBackend<P>()` checks the nine members
every `Platform` defines (battery caps, read and capacity, `setRail`,
power-button clicks, power off and the three timed-wake calls) by comparing
`decltype(&P::member)` with the documented signature and rejecting a
polymorphic type. `FurblePlatform.h` asserts it after the class, so the
firmware, the simulator and every host target that includes the real header
check it. There are no virtual functions. `Platform` derives from the class
for the types and defines the members in `src/FurblePlatform.cpp` for the M5
boards and in `sim/FurblePlatformSim.cpp` for the simulator, so the link picks
the one implementation and unused power paths are still removed by
`--gc-sections`. The fuel gauge is not a separate interface: `readBattery()`
covers it. A battery source (gauge versus voltage estimate) and more rails are
added with their first caller.

Display backend, as a description rather than code until a second panel class
exists. Every board keeps the rules the UI enforces today: hold
`ESP_PM_APB_FREQ_MAX` while the backlight is lit, release it while the panel
sleeps and reacquire it before wake, and wait 120 ms between SLPIN and SLPOUT
(ST7789, ILI934x). Per
board the description is: panel class, bus, pins, size, rotation, brightness
method (LEDC backlight, PMIC backlight rail, or panel contrast/brightness
command), and the minimum brightness, which is already a profile field.

| Board | Panel and bus | Brightness |
| :--- | :--- | :--- |
| T-Beam Supreme | `Panel_SH110x` 128x64, `Bus_I2C` port 0, SDA 17, SCL 18, address 0x3C | contrast command; no backlight, no APB lock needed for LEDC |
| T-Watch Ultra | `Panel_CO5300`, `Bus_SPI` quad (`pin_io0`..`pin_io3`) | AMOLED brightness command |
| T-LoRa Pager | ST7796U 480x222 (new `Panel_LCD` subclass), `Bus_SPI` | LEDC backlight, keep the APB lock rule |
| Cardputer Adv | `Panel_ST7789` via M5GFX autodetect | as the M5 boards |

Input classes, also a description: physical buttons (M5 `BtnA/B/C`, the PMIC
key as `BtnPWR`), touch (`M5.Touch`), and two classes furble lacks: a rotary
encoder (Pager, ESP-IDF PCNT, maps to LVGL `LV_INDEV_TYPE_ENCODER`) and a
keyboard (TCA8418 on Cardputer Adv and Pager, maps to LVGL keypad). The
T-Beam has one BOOT button plus the AXP2101 key, which is the one-button tier
in the table below.

### Phase 4: wire-in, existing boards only (branch hal-2)

Every non-UI `M5.getBoard()` switch now reads the profile: Platform battery
caps, click streak, Plus2 hold pin, motion wake, timed wake, TimeKeeper RTC
backing, IR pin, SD slot, boot name, Feedback outputs, and the UI brightness
floor, modem sleep switch and power-off policies. `M5.getBoard()` drops from
31 calls to 7: the profile lookup and six UI geometry switches. The M5PM1
specific paths still sit under `FURBLE_HAS_M5PM1`; the profile only says which
path the board takes. The GPS rail goes through `Platform::setRail(GPS)` with
the same set, read back and single retry. `tests/host/board_profile_test.cpp`
pins every row to the values the removed switches returned, copied from the
switches at c4367912, not from the table.

### Adding the T-Beam Supreme

The row it would get (columns as in `src/FurbleBoard.cpp`):

```
constexpr Profile TBEAM_SUPREME = {"T-Beam S", {true,  true,  false, true},  0,   false, -1,
    MotionWake::NONE,      -1, TimedWake::NONE,       -1, false, false, true,  true,  32,
    false, false, false, -1};
```

Battery level, voltage and charging come from the AXP2101 E-gauge and status
registers; it has no current sense. Capacity is 0 because the 18650 is the
user's. RTC backing stays false until the PCF8563 supply is traced on the
schematic. No IR, no motion wake (QMI8658 on SPI is not supported), no timed
wake (the AXP2101 has no wake timer; a PCF8563 alarm on GPIO14 could be added
later). `modemSleep` is true because of the 32 kHz crystal, once the sdkconfig
selects it. SD stays false: its slot is on its own SPI bus, not the display
bus `FurbleSD` expects. Rails: GPS ALDO4, LoRa ALDO3, sensors ALDO1/ALDO2, SD
BLDO1/BLDO2, M.2 DCDC3/4/5; protected DCDC1.

Exact steps:

1. `platformio.ini` env `lilygo-tbeam-supreme` (ESP32-S3, 8 MB flash, 8 MB
   PSRAM, `partitions_two_ota_8m.csv`, board identity macro
   `FURBLE_LILYGO_TBEAM_SUPREME`) and `sdkconfig.lilygo-tbeam-supreme` with
   `CONFIG_RTC_CLK_SRC_EXT_CRYS`. Add it to every matrix the CLAUDE.md build
   section lists.
2. `FurbleBoardCaps.h`: `FURBLE_HAS_AXP2101_HAL 1`, `FURBLE_HAS_M5PM1 0`,
   `FURBLE_HAS_SD 0`, a new `FURBLE_PANEL_128X64 1`.
3. `src/FurbleBoard.cpp`: the row above, returned by `profile()` under
   `FURBLE_HAS_AXP2101_HAL` because `M5.getBoard()` reports `board_unknown`.
   Add GPS UART pins (RX 9, TX 8, 9600 baud) to the row; `FurbleGPS` reads
   Port A pins from M5Unified, which has none for this board.
4. A `RegisterBus` adapter over `m5::I2C_Class` (`In_I2C.begin(I2C_NUM_1, 42,
   41)`) and an `SX1262::Port` adapter over `spi_master` and GPIO, with
   `gpio_hold_en` on NSS so the radio stays asleep through MCU sleep.
5. `Platform` under `FURBLE_HAS_AXP2101_HAL`: build `HAL::AXP2101` with
   DCDC1 protected; at boot switch off ALDO1/2, BLDO1/2 and DCDC3/4/5, run
   `SX1262::sleep()` and then switch off ALDO3; keep VBACKUP charge off (no
   cell fitted; GNSS backup is VRTC). `readBattery()` from the driver,
   `setRail(GPS)` maps to ALDO4,
   `powerOff()` to `AXP2101::powerOff()`, `getPWRClickCount()` from
   `readPowerKey()` short presses with `enablePowerKeyIrq(true)` at boot.
6. Display: an `lgfx::LGFX_Device` with `Panel_SH110x` and `Bus_I2C` (port 0,
   SDA 17, SCL 18, 0x3C, 128x64) made the primary display, and a 1-bit 128x64
   UI tier. That tier is the largest single item.
7. Input: BOOT (GPIO0) fed into `M5.BtnA` from `Platform::update()` the way
   the StickS3 feeds `BtnPWR`, the AXP2101 key as `BtnPWR`, and the one-button
   navigation tier.
8. Simulator: a 128x64 panel model and the board in the profile matrix.
9. Docs and gates: a `board-profile` case, the README supported controllers,
   `docs/supported-hardware.md`, `docs/wiki/Getting-Started.md`, the size and
   stack budgets, and a bench measurement of BLE-connected light sleep.

## Implementation state

- hal-1 (`refactor/hal-1-drivers`, from master): `lib/hal/` drivers and
  `tests/host/hal_drivers_test.cpp` (CTest `hal-drivers`). `lib/hal` is a
  portable contract root, part of the static analysis scope and its workflow
  path filter. Nothing in firmware includes `lib/hal`, so every image is
  unchanged.
- hal-2 (`refactor/hal-2-interfaces`, on `refactor/port-3-routing` at
  9dd2dd61 with the hal-1 commit cherry-picked): `FurbleBoard`,
  `FurblePowerBackend`, the wire-in above, `tests/host/board_profile_test.cpp`
  (CTest `board-profile`) and the regenerated coupling ratchet.

Deviations from the brief, with reasons:

- The AXP2101 got its own pure driver even though M5Unified has
  `AXP2101_Class`: that class leaves VBACKUP charging empty, has no DCDC
  switch, reports a percentage without checking battery presence, and cannot
  run on the host. M5 boards with an AXP2101 keep M5Unified.
- `Board::profile()` is not cached. It is a switch over a constant table, the
  same cost as before, and a cache would add an init-order hazard and break
  host tests that set the board per mode.
- Display and input backends are descriptions, not classes: with one panel
  family and one input family in the tree a class would be an interface with
  one implementation. They become code with the first non-M5 board.
- hal-2 is rebased on port-3 (`9dd2dd61`), which made `GPS::setRailPower()`
  the only GPS rail switch and `Platform::getBatteryStatus()` the battery
  status source. `GPS::setRailPower()` now calls `Platform::setRail(GPS)`.

### Verification evidence (hal-2 at fa44daff)

Fixed-version builds (`FURBLE_VERSION=1.0.0-ci FURBLE_TEST=0`,
`SOURCE_DATE_EPOCH=1794258087`) of the hal-2 base (port-3 plus hal-1,
`3a89ebdc`) and of hal-2 in the same build tree:

| Env | Base bin | hal-2 bin | Delta | DRAM | IRAM (budget) | Size / stack budget |
| :--- | --: | --: | --: | :--- | --: | :--- |
| m5stick-s3 | 1,848,960 | 1,847,664 (1,848,928 after review) | -1,296 (-32) | 47,208, unchanged | 132,864 (134,144) | pass / pass |
| m5stick-c | 1,490,592 | 1,490,608 | +16 | 54,780, unchanged | 114,024 (114,688) | pass / pass |
| m5stick-c-plus | 1,540,160 | 1,540,112 | -48 | 54,756, unchanged | 114,024 (114,688) | pass / pass |
| m5stack-core | 1,614,304 | 1,614,320 | +16 | 54,884, unchanged | 115,668 (116,736) | pass / pass |
| m5stack-core2 | 1,898,608 | 1,898,640 | +32 | 56,480, unchanged | 121,036 (121,856) | pass / pass |
| esp32-s3-headless | 1,759,392 | 1,759,328 | -64 | 51,952, unchanged | 140,032 (141,312) | pass / pass |
| waveshare-s3-eth | 1,696,368 | 1,696,384 | +16 | 51,332, unchanged | 132,352 (133,120) | pass / pass |

The images differ, as intended: eight `M5.getBoard()` switches became
lookups in one constant table and `setRail()` is new. For the three envs with both ELFs kept, the only section changes are the
table in `.flash.rodata` (+144 to +160 bytes) and less `.flash.text` (-112 to
-224); IRAM, DRAM and RTC sections are byte identical. The base ELFs of the
first four envs were not kept, so their section split is not recorded; their
DRAM matches the base build log and their IRAM passes the budget.

StickS3 -1,296 bytes, explained after review. Fresh rebuilds of the base
(`3a89ebdc`) and of hal-2 with the review changes give 1,848,960 and
1,848,928 bytes (-32). `esptool.py --chip esp32s3 image_info --version 2`
lists the same seven segments for all three images, with the IROM segment at
file offset 0x60018 every time, so the DROM/IROM 64 KB alignment padding did
not change. Segment by segment (bytes):

| Segment | Base 3a89ebdc | hal-2 fa44daff | hal-2 review |
| :--- | --: | --: | --: |
| DROM 0x3c150020 | 373,556 | 373,700 | 373,700 |
| DRAM 0x3fca4700 | 17,648 | 17,648 | 17,648 |
| IRAM 0x40374000 | 1,988 | 1,844 | 1,844 |
| IROM 0x42000020 | 1,324,752 | 1,323,312 | 1,324,564 |
| IRAM (second) | 130,780 | 130,924 | 130,924 |
| RTC data, RTC fast | 32, 88 | 32, 88 | 32, 88 |

DROM grows by the 144-byte profile table. IRAM moves 144 bytes between its
two segments and its total (132,768) is unchanged. The whole difference is
the IROM segment, `.flash.text`: 1,440 bytes smaller at `fa44daff` and 188
bytes smaller after review. Between the two hal-2 builds the only source
change is removing the uncalled `getBatterySource()` and two enum values, yet
`nm` shows 49 functions changing size, almost all outside furble (BT
controller init and deinit, NimBLE `ble_sm_alg_*`, WPA `sae_*`) by 4 to 60
bytes each. That pattern points to the Xtensa linker's layout-dependent call
relaxation and alignment rather than code this plan added. This is inferred
from the symbol sizes; it was not traced instruction by instruction. The StickS3 image size therefore moves by up
to about 1.4 KB between unrelated layouts, and the meaningful numbers are the
DROM table, the unchanged DRAM and IRAM totals, and the budgets.

Deviation found by this comparison: the first hal-2 build made
`PowerBackend` a virtual interface. The vtable kept every power method alive,
so the headless Waveshare image linked `powerOff()`, `powerOffUntil()` and the
ESP-IDF light and deep sleep code into IRAM: +6,304 bytes of `.iram0.text`,
5,376 over its 133,120-byte budget, and +10,192 bytes of image. `fa44daff`
made the contract non-virtual and the image returned to the base footprint.

Host suite: 185 of 187 ctest pass. `golden-capture-replay` is Not Run when
its capture is absent and passes when run alone. `control-tsan-wrapper-contract`
times out at its 10 s ctest cap at host load average near 30, in the plain
and the ASan/UBSan runs alike; the wrapper does not touch any file this plan
changes. ASan/UBSan (`tools/run_sanitized_host_tests.sh --clean`): 184 of 185,
same single timeout. After `fa44daff` the 29 host tests that link `Platform`,
`FurbleBoard` or the drivers were rebuilt and pass.
`sim/scripts/run-boundary-conformance.sh`: 18 of 18. StickS3 simulator, 22
hardware-profile scenarios (battery policy, power button, motion wake, GPS
rail standby and wake, intervalometer timed wake): 22 pass on `671680e5`; the
simulator was not rebuilt after the devirtualization, which removes virtual
dispatch only. Coupling ratchet, portability contract and doc tokens: clean.
`python3 tools/pre_review.py --base origin/master` on hal-2 after `fa44daff`:
9 of 9 gates pass.

### Rebase onto port-4 (`7f408c75`)

hal-2 is rebased onto the final `refactor/port-4-camera-types`. The only
conflict was `include/CLAUDE.md`; both bullet sets are kept. Port-4's ratchet
now compares per file against `--base`, and the one `M5.getBoard()` call in
`src/FurbleBoard.cpp`, the lookup that replaced the scattered switches, counted
as growth in a new file. `tools/check_coupling_ratchet.py` now treats
`src/FurbleBoard.cpp` like the Platform adapter, with a unit test.
`check_coupling_ratchet.py --base 7f408c75` is clean and every per-file count
only drops (`M5.getBoard()` 16 to 6 outside the adapters, `M5.` calls down in
seven files).

After the rebase: `hal-drivers`, `board-profile`, `boundary-headless-service`,
`settings-nvs-roundtrip` and `time-keeper-persistence` pass. Fixed-version
builds: m5stick-s3 1,848,992 bytes (DRAM 47,208, IRAM 132,864) and
waveshare-s3-eth 1,696,432 bytes (DRAM 51,332, IRAM 132,352); size and stack
budgets pass, and DRAM and IRAM equal the pre-rebase hal-2 builds. The few
extra bytes come from port-4's own code. `pre_review --base origin/master`:
9 of 9 gates. `check-doc-tokens`: tokens present.
