# 322 - Hardware abstraction for the next boards

Status: hal-1 and hal-2 merged in #462 (`fd51e96c`), on the capability macros and
coupling ratchet from #460 (`f4cf0d32`). No new board env, no support claim and
no hardware run in this work. Firmware includes none of the drivers yet: the
first non-M5 board is what wires them in, as the sequence below records.
Archived to `plans/implemented/` on 2026-11-25.

The T-Beam Supreme facts in this plan were re-read against the LilyGO
documents on 2026-11-25 and nine findings changed it: the ALDO1 rail feeds the
display, BLDO2 is the external pin header, the RTC is battery backed, the OLED
address is not fixed, GPIO7 is not a documented enable on the M10S trim,
`M5UnitOLED` cannot drive the panel, four profile columns cannot express the
board, the GPS UART pins belong in the pin seam, and the PSRAM is quad. The
corrections are applied below and listed with their sources in the
`Verification pass (2026-11-25)` section at the end.

Related: [287](../287-mcu-portability-and-bundle-hardware.md) (board survey),
[127](../127-nordic-port-baseline.md) (port contract and the coupling ratchet
from `refactor/port-1-ratchet`), [30](../30-m5stack-framework.md) (board support
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
| SH1106 OLED | `lgfx::Panel_SH110x` with `Bus_I2C` | Yes, as a custom `LGFX_Device`. Not through `M5UnitOLED`: M5GFX 0.2.27 `src/M5UnitOLED.h` hardcodes `panel_width` 64 with `offset_x` 32, defaults to the M5 Port A pins and address 0x3C, and so cannot drive a 128x64 panel on SDA 17 and SCL 18. | No 128x64 1-bit UI tier in furble. The panel must be configured 128x64 explicitly and the column offset is a bench check. Sino Wealth SH1106 V2.3, 49 pages, sha256 ab5ce5f3, read 2026-11-26: sleep current is 5 mA maximum on both supply rows, not microamps (page 35). There is no vendor copy of this datasheet; the Pololu and Crystalfontz copies are the same V2.3 text. |
| ST7789 / ST7789V2 | `Panel_ST7789` | Yes; Cardputer Adv is a known board (`board_M5CardputerADV`) | None. The StickS3 panel is the sibling ST7789P3, v0.1, 276 pages, sha256 26e62a24, read 2026-11-26: 6.0 mA typical and 7.5 mA maximum at work, and a higher sleep-in than the ST7789V at 20 uA typical, 50 uA maximum (Table 3 page 34). |
| ST7796U | not present (only a comment in `Panel_Device.hpp`) | - | Add a `Panel_LCD` subclass with the ST7796 init table from upstream LovyanGFX (FreeBSD licence). ST7796U v1.0, 2020/02, 287 pages, sha256 c907c64e, read 2026-11-26. It is a Sitronix part, not a module name, and Sitronix's AIoT DDI table lists ST7796U, UI, HU and U2 without ST7796S. MIPI DSI is in both the U and the S document (section 8.5), so DSI is not what separates them; the U is the 16.7M-colour 287-page document and the S the 262K-colour 279-page one. LilyGo's own Pager hardware document names ST7796U over SPI with 480x222, 2.33 inch and 262K colours. |
| CO5300 AMOLED | `Panel_CO5300` on `Panel_AMOLED` | Yes. `Bus_SPI` has `pin_io0`..`pin_io3` for quad SPI on the S3. | AMOLED has no backlight; brightness is a panel command. Chipone CO5300 V0.01, 226 pages, sha256 2ad0461a, read 2026-11-26: deep stand-by 2 uA VDDI plus 1 uA VCI, and Sleep In 110 uA plus 25 uA on DSI LP or 85 uA plus 25 uA on DSI ULP (page 103). V0.00 has no deep stand-by row. |
| CST9217 touch | not present (CST816S, CST226, FT5x06, GT911, CHSC6540 only) | - | New `ITouch` subclass for the Watch Ultra. Hynitron CST9217 V1.0, 2022-08-11, 10 pages, sha256 74cf16e6, read 2026-11-26: 1.3 mA at 100 Hz, 12 uA in monitor at 30 Hz, 2 uA asleep, default I2C 8-bit address 0xB4 and 0xB5, so 0x5A, configurable in the chip. The document has no register map. Zephyr has no CST92xx driver, only cst8xx for CST816S, T and D and CST820, and Hynitron's own product page for the part, `http://www.hynitron.com/product/wearable/204`, links no datasheet PDF. |
| Rotary encoder | none needed: ESP-IDF PCNT | - | Pager input class. |
| Cardputer Adv battery | `Power_Class` ADC on GPIO10, ratio 2.0 | Yes, M5 board | None. |

The rule that follows: on M5 boards keep `M5.Power`, `M5PM1` and M5GFX as they
are. Write a register driver only where the table shows a gap, and put it in
`lib/hal/` so it is host-tested.

### Where furble assumes M5 hardware (master `e66e8807`)

`M5.getBoard()` appears 31 times: `src/FurblePlatform.cpp` 15,
`src/FurbleUI.cpp` 11, and one each in `FurbleBootScreen.cpp`,
`FurbleFeedback.cpp`, `FurbleIR.cpp`, `FurbleSD.cpp` and `FurbleTimeKeeper.cpp`.

Current count after hal-2 (#462): eight call sites. Six are switches in
`src/FurbleUI.cpp`, four of them geometry at `:843`, `:10560`, `:10668` and
`:11348`, and two are input wiring at `:2000` and `:13536`. The seventh is the
profile lookup at `src/FurbleBoard.cpp:83`, the eighth a simulator board check
at `sim/FurblePlatformSim.cpp:295`. The audit below is the state at `e66e8807`.

| Path | Where | Board knowledge today |
| :--- | :--- | :--- |
| Battery caps and capacity | `Platform::initBattery()` | Per-board table: AXP192 boards full caps, StickS3 no current, Plus2 ADC, Core IP5306 level only, everything else none. |
| Battery read | `Platform::readBattery()` | `M5.Power` everywhere except the StickS3, which reads M5PM1 VBAT and GPIO0 charge (active low). Headless `main.cpp` and the UI also read `M5.Power` directly (port-3 moves these behind Platform). |
| Charging | same | `M5.Power.isCharging()` or M5PM1 GPIO0. |
| GPS rail | `FurbleGPS.cpp`, `main.cpp`, `FurbleUI.cpp` | `M5.Power.setExtOutput(…, ext_PA)` (port-3 routes it through GPS). |
| Power off | `Platform::powerOff()` | M5PM1 `shutdown()` on the StickS3, `M5.Power.powerOff()` elsewhere, refused when the row has no power-off path (plan 349). UI hides power policies on the Core (IP5306). |
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
| Display sleep | `UI` display state machine | Holds `ESP_PM_APB_FREQ_MAX` while lit, waits the 120 ms panel dwell that the ILI934x needs in both directions and the ST parts need from SLPOUT to the next SLPIN, sleeps through `M5.Display`. |
| Buttons and touch | `FurbleUI.cpp` | `M5.BtnA/B/C/PWR`, `M5.Touch`. |
| IMU | `FurbleIMU*.cpp`, console, UI | `M5.Imu` plus BMI270 and MPU6886 engines on `M5.In_I2C`. |

The remaining UI switches (layout, legends, floating indicators) are UI
geometry, not hardware, and stay in the UI.

### Per-board gaps

T-Beam Supreme (ESP32-S3, 8 MB flash, 8 MB quad PSRAM). Pins from LilyGO's
`examples/Factory/utilities.h`, the rail map from `LoRaBoards.cpp` in
LilyGo-LoRa-Series, and the wiring from the schematics at their title-block
revisions V3.0 and V3.1 (source IDs resolve in plan 318):

| Need | Chip and pins | Covered by | Gap |
| :--- | :--- | :--- | :--- |
| Battery %, V, charging | AXP2101 on I2C1 SDA 42 SCL 41, IRQ 40 [L32, sheet Power rev V3.0] | `lib/hal/AXP2101` | Board adapter (I2C bus) |
| GPS rail | ALDO4 3.3 V into the GNSS VCC pin [L32, sheet Radio & GNSS rev V3.0]; UART RX 9 TX 8, PPS 6 through a series 1N4148, GPIO7 on WAKE_UP for the L76K trim only [L14, L32] | `AXP2101::setRail(ALDO4)` | Route the GPS rail through the power backend. The pin 7 enable is UNVERIFIED for the M10S trim. |
| GPS backup | V_BCKP (L76K pin 6) on AXP2101 VRTC, which has no enable bit and stays up while VBAT is present [L32; AXP2101 datasheet rev 1.0 section 6.5.4] | No code: VRTC has no switch | Keep the VBACKUP charge off, as LilyGO does [L33]. The M10S trim's backup path is unverified: there is no M10S schematic. |
| LoRa | SX1262 CS 10, SCK 12, MOSI 11, MISO 13, RST 5, BUSY 4, DIO1 1, on ALDO3 [L33] | `lib/hal/SX1262::sleep()` | SPI port adapter, NSS hold through sleep; or switch ALDO3 off |
| Rails | ALDO1 display (FPC pin 6), BME280 and magnetometer; ALDO2 sensor; BLDO1 microSD; BLDO2 external pin header; DCDC3/4/5 M.2 [L14, L31, L30] | `AXP2101::setRail()` | ALDO1 must stay on. The verification pass records what furble may switch off. |
| Display | SH1106 128x64 on I2C0 SDA 17 SCL 18, address 0x3C or 0x3D by strap [L14, L31] | `Panel_SH110x` + `Bus_I2C` | 1-bit 128x64 UI tier; probe both addresses |
| Input | BOOT (GPIO0) only, plus AXP2101 PEK | `AXP2101::readPowerKey()` | One-button input class |
| RTC | PCF8563 with its own 32.768 kHz crystal, IRQ 14; VDD from VDD3V3 through a 1N4148 and a 1K resistor, and from BAT1 [L30, L31] | `m5::RTC8563_Class` on a custom bus | Profile row `rtcBatteryBacked` is true. Whether BAT1 is fitted is open, and the part is unsettled: the wiki feature list says PCF85063ATL where its address table and the schematic say PCF8563. |
| IMU | QMI8658 on SPI (CS 34, INT 33); the quad PSRAM keeps GPIO33 to GPIO37 free [L14] | none | Motion backend or software only |

T-Watch Ultra: AXP2101 (as above), CO5300 over QSPI (M5GFX), CST9217 touch
(missing driver), MIA-M10Q GNSS (UBX, merged in #459), V_BCKP
from VRTC, SX1262 sleep, crystal. Gaps: CST9217 `ITouch`, round AMOLED UI
geometry, brightness as a panel command. The CST9217 register knowledge must
come from the MIT `cst92xx` Rust crate, `src/registers.rs` at commit
`0e4cede3`, and be attributed there. The Linux `hynitron_cstxxx.c` driver is
GPL-2.0-only and the ESPHome CST92xx component is GPLv3, so neither may be
copied into this MIT tree.

T-LoRa Pager: ST7796U 480x222 (missing panel class; LilyGo's own hardware
document names the driver IC, the size and the 262K colour depth), BQ27220
(`lib/hal`), BQ25896 (not needed yet), XL9555 with GPS_EN (`lib/hal`), TCA8418
keyboard (missing), rotary encoder (PCNT), MIA-M10Q (UBX, #459), SX1262 sleep.
Gaps: ST7796 panel, keyboard and encoder input classes, wide UI layout. It is
the first board with the BQ27220, so its `RegisterBus` adapter must override
`delayUs()` with a real 66 us bus-free wait (SLUUBD4A section 5.3): the seam
defaults to a no-op and the gauge then answers with stale or corrupt data.

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
  us settle, BUSY high check. Every number comes from the datasheet, and the
  vendor page for the part serves no direct PDF link, so the byte-identical
  mirrors are cited:
  - NRESET held low for 200 us in the driver against "typically 100 us" for a
    factory reset, which is followed by the standard calibration (8.1, p. 53).
  - BUSY low before the command, with the wait budget at 10 ms against a cold
    start of 3500 us and a warm start of 340 us (Table 8-2, p. 56). BUSY is
    asserted at most 600 ns after the NSS rising edge that ends a write
    command, and in sleep it is held high through a 20 kOhm resistor (8.3.1,
    p. 55).
  - The 500 us settle after SetSleep is the datasheet's own window: the device
    is unresponsive for around 500 us while it saves the configuration, and
    the host must send no SPI command during it (13.1.1, p. 72). It was not an
    invented margin.
  - Waking costs at least 100 us from the NSS falling edge to the first SCK
    edge when leaving sleep, and MISO may take up to 150 us (Table 8-1 t10 and
    t11, 8.2.2, p. 54).
  - The rest of the table, if a later driver needs a mode change: STBY_RC to
    STBY_XOSC 31 us, to FS 50 us, to RX 83 us, to TX 126 us; STBY_XOSC to FS
    40 us, to TX 105 us, to RX 62 us; FS to RX 41 us, to TX 76 us; RX to FS
    15 us, to TX 92 us (Table 8-2, p. 56).
  There is no Semtech errata document for the part; section 15 "Known
  Limitations", pages 109 to 111, is the functional substitute, and none of its
  four items applies to a sleep-only driver.

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
and capacity, RTC backing, whether an RTC chip is reachable on the internal bus,
IR pin, motion wake path and pin, timed-wake kind, power hold pin, PMIC click
streak, SD slot, `canPowerOff`, `modemSleep`, the brightness floor and the
feedback outputs. Compile-time facts stay in
`FurbleBoardCaps.h` from port-2; UI geometry stays in the UI.

`include/FurblePowerBackend.h`: the contract a board's power hardware meets.
It names battery caps and sample (moved here from `Platform`, which inherits
the same `battery_caps_t` and `battery_t` names) and the named rail `EXT_5V`,
the only rail with a switch. That rail was called `GPS` until plans/364: the
M5StickS3 bit it names is the EXT_5V port, which also feeds the Grove 5 V
output and the infrared circuit, so the name now describes what it powers
rather than one consumer. Ownership of that rail is not part of this contract:
`setRail` is the owner's own switch, and clients hold the rail through
`Platform::railAcquire()`/`railRelease()` with a named `RailHolder`. See
plans/implemented/364-ext5v-rail.md. `meetsPowerBackend<P>()` checks the nine members
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
sleeps and reacquire it before wake, and keep the panel dwell per panel. The
documents read on 2026-11-26 split it two ways. ILI9342C V101 sections 8.2.11
and 8.2.12 need 120 ms in both directions. ST7789V v1.3, ST7789V2 v1.0,
ST7789P3 v0.1 (sections 9.1.11 and 9.1.12), ST7796S v1.0 and ST7796U v1.0
(sections 9.2.12 and 9.2.13) need 120 ms from SLPOUT to the next SLPIN only,
plus 5 ms after SLPIN and after SLPOUT before any other command. Never write
this as a one-way rule for every panel or as a two-way rule for every panel.
Per
board the description is: panel class, bus, pins, size, rotation, brightness
method (LEDC backlight, PMIC backlight rail, or panel contrast/brightness
command), and the minimum brightness, which is already a profile field.

| Board | Panel and bus | Brightness |
| :--- | :--- | :--- |
| T-Beam Supreme | `Panel_SH110x` 128x64, `Bus_I2C` port 0, SDA 17, SCL 18, address 0x3C or 0x3D by strap, probed 0x3C first | contrast command; no backlight, no APB lock needed for LEDC; column offset a bench check |
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
31 calls to 7 in the firmware: the profile lookup and six UI switches, four
geometry at `:843`, `:10560`, `:10668` and `:11348`, and two input wiring at
`:2000` and `:13536`. The simulator adds one more check at
`sim/FurblePlatformSim.cpp:295`, which is why the census above counts eight. The
M5PM1
specific paths still sit under `FURBLE_HAS_M5PM1`; the profile only says which
path the board takes. The EXT_5V rail goes through the rail manager with the
same set, read back and single retry. A `gpsRail` profile column names the
boards where M5Unified 0.2.20 `setExtOutput()` switches Port A: StickC, StickC
Plus, StickS3, Core2 and Tough. On every other row, the Plus2, the Core and
the unknown row included, `setRail()` returns false without a bus access;
M5Unified did nothing there before either. plans/364 later replaced the
`M5.Power.setExtOutput()` call on the StickS3 with a direct, serialized M5PM1
`PWR_CFG` 0x06 write, because the status LED wrote the same byte through
Furble's own driver and the two could lose each other's update. The AXP rows
keep the M5Unified path: there is no second writer of EXTEN or LDOIO0, so
there is nothing to serialize and no `PWR_CFG` interlock to apply. `tests/host/board_profile_test.cpp`
pins every row to the values the removed switches returned, copied from the
switches at c4367912, not from the table. The GPS rail values are copied from
M5Unified.

### Adding the T-Beam Supreme

The row it would get (columns as in `src/FurbleBoard.cpp`):

```
constexpr Profile TBEAM_SUPREME = {"T-Beam S", {true,  true,  false, true},  0,   true,  false, -1,
    MotionWake::NONE,      -1, TimedWake::NONE,       -1, false, false, true,  true,  32,
    false, false, false, -1, false};
```

The row has all 22 columns of `include/FurbleBoard.h:46-81`, and
`rtcBatteryBacked` is the only value that changed in the 2026-11-25 pass.
`rtcOnBus` is the column after it: it says whether M5Unified can reach an RTC
chip on the internal bus, and this row leaves it `false` because M5Unified has no
board row to run a probe for the T-Beam Supreme. Plan 363 reads that column for
its boot guard. Plan 350 later appended two more columns, `ledActiveLow` and
`irMarkLow`, so this row needs them before it compiles; both describe a driven
stage and neither has a value for a board with no LED or emitter. The trailing
`false` is `gpsRail`: `M5.Power` does not switch this board's GPS rail, the
AXP2101 does.

Battery level, voltage and charging come from the AXP2101 E-gauge and status
registers; it has no current sense. Capacity is 0 because the 18650 is the
user's. RTC backing is true: both schematic revisions feed the PCF8563 VDD
from VDD3V3 through a 1N4148 and a 1K series resistor and from BAT1, so the
clock keeps time with the 18650 out while BAT1 is fitted [L30, L31]. Whether
BAT1 is populated is the only open question there. No IR, no motion wake
(QMI8658 on SPI is not supported), no timed wake (the AXP2101 has no wake
timer; a PCF8563 alarm on GPIO14 could be added later). `modemSleep` is true
because of the 32 kHz crystal, once the sdkconfig selects it. SD stays false:
its slot is on its own SPI bus, not the display bus `FurbleSD` expects.

Rails, and the one that must never be switched off:

| Rail | Feeds | Boot state |
| :--- | :--- | :--- |
| ALDO1 | OLED display (FPC pin 6), BME280 with its 4.7K I2C pull-ups, magnetometer [L14, L31] | on, always |
| ALDO2 | sensor rail [L14] | on, as the factory code leaves it [L33] |
| ALDO3 | SX1262 radio [L14] | on until `SX1262::sleep()`, then off |
| ALDO4 | GNSS VCC (L76K pin 8) [L32] | on, this is the `GPS` rail |
| BLDO1 | microSD, its VDD and pull-ups [L14, L30] | on, as the factory code leaves it [L33] |
| BLDO2 | external pin header [L14, L31] | on; switching it off is safe and a bench decision |
| DCDC3/4/5 | M.2 socket [L14] | on, as the factory code leaves it |
| DCDC1 | the SoC | on, protected |
| DCDC2, DLDO1/2, VBACKUP | unused, and the backup cell charge | off, as the factory code does [L33] |

The factory code does pulse ALDO1, ALDO2 and BLDO1 low for 250 ms on a cold
boot before re-enabling them, with the comment that it restarts the SD card
and the QMC sensor [L33]. That is a bus reset, not a steady state. Turning
ALDO1 off at boot, as this plan used to say, would blank the display, drop the
sensor and magnetometer supply and remove the I2C pull-ups from the bus.

Column-semantics gap, recorded rather than papered over. Four of the 19 columns
cannot say what this board is, so the row keeps today's values and these
meanings:

- `batteryCapacityMah` 0 means unknown. This board's cell is the user's, so the
  capacity is unknowable at build time rather than unknown at runtime.
- `sdSlot` false means no slot on the display SPI bus. This board has a
  microSD slot on its own bus, which `FurbleSD` cannot drive.
- `minBrightness` 32 is a backlight floor. The SH110x has no backlight and
  brightness is a contrast command, so any value in this column is a
  placeholder.
- `gpsRail` false means `M5.Power` does not switch a Port A output. This board
  switches ALDO4 through the AXP2101, so the rail exists and the column cannot
  say so.

A column set that told the truth would separate "unknown capacity" from
"user-supplied cell", "slot present, different bus" from "no slot", "no
backlight" from a floor, and "rail on another controller" from "no rail". That
touches `include/FurbleBoard.h` and all nine rows, and is not part of this
plan.

Display: a custom `lgfx::LGFX_Device` with `Panel_SH110x` and `Bus_I2C` on port
0, SDA 17, SCL 18, 128x64 is the right device. `M5UnitOLED` in M5GFX 0.2.27
cannot be reused: it hardcodes `panel_width` 64 with `offset_x` 32 and defaults
to the M5 Port A pins and address 0x3C. Set 128x64 explicitly and keep the
column offset as a bench check. The address is 0x3C or 0x3D, strapped by R51
and R52 for the radio band variant [L31], and the magnetometer shares bus 0 at
0x1C, 0x3C or 0x7C depending on the variant [L14]. The vendor factory code
scans both addresses and decides between OLED and QMC6310N by reading register
0x00, where 0x80 means the magnetometer [L33]. Probe 0x3C, then 0x3D.

GPS enable: GPIO7 is a WAKE_UP line on the L76K trim only. The ESP32S3 sheet
maps GPIO7 to the net GPS_WAKEUP and the Radio and GNSS sheet drives the L76K
WAKE_UP input with it [L32], and the wiki marks the row L76K only [L14]. The
vendor header defines `GPS_EN_PIN 7` without distinguishing trims [L33]. The
MAX-M10S has no enable pin: its pin 5 is EXTINT and its pin 7 is V_IO [U1 table
10], and there is no M10S schematic. The enable claim is UNVERIFIED for that
trim, and what supplies V_IO is a bench check.

PSRAM: the 8 MB PSRAM is quad, so GPIO33 to GPIO37 stay usable; the IMU takes
33 and 34 and the peripheral SPI 35, 36 and 37 [L14]. An octal-PSRAM S3 board
takes those pins, which is why the row must not assume them lost.

Exact steps:

1. `platformio.ini` env `lilygo-tbeam-supreme` (ESP32-S3, 8 MB flash, 8 MB
   PSRAM, `partitions_two_ota_8m.csv`, board identity macro
   `FURBLE_LILYGO_TBEAM_SUPREME`) and `sdkconfig.lilygo-tbeam-supreme` with
   `CONFIG_RTC_CLK_SRC_EXT_CRYS`. Add it to every matrix the CLAUDE.md build
   section lists.
2. `FurbleBoardCaps.h`: two new macros, `FURBLE_HAS_AXP2101_HAL` and
   `FURBLE_PANEL_128X64`, each 1 for this board, plus `FURBLE_HAS_M5PM1 0` and
   `FURBLE_HAS_SD 0`. Neither new macro exists yet.
3. `src/FurbleBoard.cpp`: the row above, returned by `profile()` under
   `FURBLE_HAS_AXP2101_HAL` because `M5.getBoard()` reports `board_unknown`.
   The GPS UART pins do not go in this row: RX, TX and baud would make 22
   columns and put wiring in a table of runtime facts. They belong in the pin
   seam, the two `M5.getPin()` calls in `GPS::getInstance()`
   (`src/FurbleGPS.cpp:167-169`), which return -1 on this board and must return
   RX 9 and TX 8 for it. The baud is already the `GPS_BAUD` setting, whose
   default is `BAUD_9600` (`src/FurbleSettings.cpp:741`).
4. A `RegisterBus` adapter over `m5::I2C_Class` (`In_I2C.begin(I2C_NUM_1, 42,
   41)`) and an `SX1262::Port` adapter over `spi_master` and GPIO, with
   `gpio_hold_en` on NSS so the radio stays asleep through MCU sleep.
5. `Platform` under `FURBLE_HAS_AXP2101_HAL`: build `HAL::AXP2101` with
   DCDC1 protected and bring the rails up in the factory order (ALDO4, ALDO3,
   ALDO1, ALDO2, BLDO1, BLDO2, DCDC3/4/5), run `SX1262::sleep()` and then
   switch off ALDO3 only. ALDO1 stays on unconditionally: it feeds the OLED,
   the BME280 and magnetometer, and the I2C pull-ups. ALDO2 and BLDO1 stay on
   as the vendor code leaves them; switching BLDO2 off (external pin header)
   is a bench decision, not a boot default. Keep the VBACKUP charge off (no
   cell fitted; the GNSS backup is VRTC). `readBattery()` from the driver,
   `setRail(GPS)` maps to ALDO4, `powerOff()` to `AXP2101::powerOff()`,
   `getPWRClickCount()` from `readPowerKey()` short presses with
   `enablePowerKeyIrq(true)` at boot.
6. Display: an `lgfx::LGFX_Device` with `Panel_SH110x` and `Bus_I2C` (port 0,
   SDA 17, SCL 18, 128x64) made the primary display, and a 1-bit 128x64 UI
   tier. Set `panel_width` 128 and `panel_height` 64 explicitly, do not use
   M5GFX's `M5UnitOLED`, probe address 0x3C then 0x3D, and record the column
   offset measured on the bench. That tier is the largest single item.
7. Input: BOOT (GPIO0) fed into `M5.BtnA` from `Platform::update()` the way
   the StickS3 feeds `BtnPWR`, the AXP2101 key as `BtnPWR`, and the one-button
   navigation tier.
8. Simulator: a 128x64 panel model and the board in the profile matrix.
9. Docs and gates: a `board-profile` case, the README supported controllers,
   `docs/supported-hardware.md`, `docs/wiki/Getting-Started.md`, the size and
   stack budgets, a bench measurement of BLE-connected light sleep, and bench
   checks for the OLED address and column offset, BAT1, and the M10S V_IO
   supply.

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
- hal-3 (`fix/hal-verification`, from master): the four defects an independent
  second verification pass found in the hal-1 drivers and their test. The
  AXP2101 power-key edges are reachable, BQ27220 paces the bus through the new
  `RegisterBus::delayUs()` seam, `SX1262::sleep()` waits for BUSY inside a
  stated bound, and the test states datasheet values with a fake bus that
  honours each chip's addressing rule. Record:
  [353-hal-verification.md](../353-hal-verification.md).

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
  status source. `GPS::setRailPower()` now calls `Platform::setRail(EXT_5V)`,
  and since plans/364 it calls `Platform::railAcquire()`/`railRelease()` so the
  receiver is one of three named holders instead of the rail's only client.
- hal-3 keeps the AXP2101 edge events instead of removing them: they are the
  only way to see the key go down before a short press completes, and enabling
  all four key bits costs two interrupt sources that hold the IRQ line asserted
  until REG 49 is drained, documented on `enablePowerKeyIrq()`. `powerOff()`
  does not drain REG 49, so a board that powers off from the key clears it or
  ignores its first read after boot. BQ27220 pacing is a driver duty through a
  seam whose default is a no-op, so the first board with the gauge (the T-LoRa
  Pager) must override `RegisterBus::delayUs()`: the Pager paragraph above
  records that, and the AXP2101-only T-Beam is unaffected.

### Board pinning and the probes it removes

`platformio.ini` pins `M5GFX_BOARD` on the images that run on exactly one M5
board, and `post:patches/m5gfx_board.py` applies
`patches/m5gfx_board.patch` before M5GFX is compiled. Two things follow, and
both matter to a port:

- M5GFX drops a probe only while the pinned board matches an arm of its
  `autodetect` switch. A pinned board that M5GFX cannot configure, such as
  the display-less `board_M5StampS3` on the Waveshare ESP32-S3-ETH, used to
  fall through to `board_unknown`, and the retry loop in `M5GFX::init_impl`
  ran every ESP32-S3 probe five times. The patch now returns that family
  before the first probe. Plan [352](352-m5gfx-board-pin.md) holds the defect,
  the mechanism and the artifact evidence.
- M5Unified keys its own identification probe and its In_I2C master off
  `Display.getBoard()`. A known board skips the probe, and a pin table row
  without internal I2C pins leaves `In_I2C` closed. On the Waveshare that is
  the `board_M5StampS3` row, 255 on both pins. `FURBLE_HAS_INTERNAL_I2C` in
  `include/FurbleBoardCaps.h` states the same thing to `Platform::init`.

A new board inherits the rule: pin it only when M5GFX names it in an
`autodetect` arm, or add it to the patch's no-probe case list with its own
evidence. `tools/check_m5gfx_pinning.py` fails a branch where either half
regresses.

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

### Verification pass 2 (hal-3)

An independent pass re-read the four drivers against the datasheets and the
test against both. Four defects, all confirmed and fixed on
`fix/hal-verification`; the table with sources and test evidence is in
[353-hal-verification.md](../353-hal-verification.md).

| Defect | Source | Fix |
| :--- | :--- | :--- |
| AXP2101 edge events could never latch: `enablePowerKeyIrq()` wrote REG 41 bits 3:2 only, while `PowerKey::falling` and `PowerKey::rising` decode REG 49 bits 1:0, whose enables default to 0. | Section 6.12.1 page 28, section 6.13.2.41 page 42 | One `IRQ1_KEY_MASK` enables and decodes REG 41/49 bits 3:0; the interrupt cost is documented on the call, where REG 49 latches hold the IRQ line asserted until drained and `powerOff()` does not drain them. |
| BQ27220 issued five standard commands back to back with no bus-free time and no stated poll limit. | SLUUBD4A section 5.3 page 63 | `RegisterBus::delayUs()` seam, 66 us asked for between packets in the driver, 2 Hz cap documented and stated as `MIN_POLL_INTERVAL_MS`. |
| `SX1262::sleep()` sampled BUSY once at 500 us, so a late-asserting healthy part looked broken. | Table 8-2 has no standby-to-sleep row | Bounded poll for BUSY high; the 15,700 us total bound is stated in the header. |
| The test compared the driver with itself: `NO_ESTIMATE`, `railBit()`, the SX1262 bound from two driver constants, a reset minimum of `100 + 500`, and a fake bus that walked up addresses across command pairs. | SLUUBD4A Tables 2-1 and 2-6, section 2.11; XL9555 section 9.2.1 | Datasheet values in the test; the fake bus models linear, 16-bit command-word and 16-bit port-pair addressing, logs a misuse and keeps one interleaved event log. |

Gate results on the fix: `hal-drivers` passes, `python3
tools/check_portability_inventory.py` is clean, `python3
tools/pre_review.py --base origin/master` passes all ten gates, and the three
driver mutations (bits 3:2 only, no pacing, single BUSY sample) each fail the
new assertions. No firmware file changed and no image is affected, because
nothing in firmware includes `lib/hal` yet.

## Verification pass (2026-11-25)

Every T-Beam Supreme claim in this plan was re-read against the LilyGO
documents at their own revisions, the pinned M5GFX 0.2.27 sources, the u-blox
MAX-M10S data sheet and the AXP2101 datasheet. Nine findings changed text in
this plan. The corrections above are applied in place; this table is the
record of what was wrong and where the corrected fact comes from. Nothing in
`src/`, `lib/` or `sim/` changed.

| # | What this plan said | Corrected to | Source |
| ---: | :--- | :--- | :--- |
| 1 | Step 5: switch ALDO1/2 off at boot, and switch off what furble does not use. | ALDO1 feeds the OLED display (FPC pin 6), the BME280 with its 4.7K I2C pull-ups and the magnetometer, so it must stay on. The vendor factory code ends with ALDO1, ALDO2, BLDO1 and BLDO2 enabled and only pulses ALDO1, ALDO2 and BLDO1 low for 250 ms on a cold boot as a bus reset. | L14 "AXP2101 Power Channels" (ALDO1 = BME280 + Display + MAG Sensor, ALDO2 = Sensor); L31 "OLED Display" (CN1 pin 6 to A_LDO1) and "Sensor" (U4 VDD and VDDIO, R26 and R27 4.7K pull-ups, both on A_LDO1); L33 T_BEAM_S3_SUPREME branch |
| 2 | Gap row "ALDO1/2 sensors, BLDO1/2 SD", and step 5 switches BLDO2 off. | BLDO1 is the microSD rail. BLDO2 is the external pin header. The factory code comments both BLDO blocks "Sdcard", which is where the mistake came from. | L14 "AXP2101 Power Channels" (BLDO1 SD Card, BLDO2 External pin header); L31 "Bottom" headers (PM1 pin 6 to B_LDO2); L30 TF CARD block (card VDD and the 10K SD_CS pull-up on B_LDO1) |
| 3 | The row set `rtcBatteryBacked` false, "until the PCF8563 supply is traced". | It is true. Both revisions feed the PCF8563 VDD from VDD3V3 through a 1N4148 and a 1K series resistor and from BAT1, so the RTC is battery backed. Whether BAT1 is populated is the only open question. Two smaller fixes ride along: the 32 kHz crystal on XTAL_32K belongs to the SoC, not the RTC, and the wiki is inconsistent about the part (feature list PCF85063ATL, address table and schematic PCF8563). | L31 "RTC" block (U2 pin 8 VDD, D2 1N4148, R3 1K, BAT1, C2 100nF); L30 "RTC" block (D4 1N4148, 1K, BAT1, C9 100nF); L14 "Key Features" and "I2C Device Addresses"; L32 "ESP32S3" sheet (X2 32.768 kHz on XTAL_32K) |
| 4 | Display address 0x3C, fixed, in the gap row, the display table and step 6. | The address is 0x3C or 0x3D. R51 and R52 strap it, the sheet's note gives VHF R52=0R R51=NC 0x3C and UHF R52=NC R51=0R 0x3D, and the wiki lists 0x3C/0x3D. The magnetometer shares bus 0 at 0x1C, 0x3C or 0x7C by variant, so an address alone does not identify the device: the factory scan reads register 0x00 and takes 0x80 as the QMC6310N. Probe 0x3C, then 0x3D. | L31 "OLED Display" note "Slave 7bit address definition"; L14 "I2C Device Addresses"; L33 `display_address` (0x3C, "It might be 0x3D") and the 0x3C/0x3D cases in `scanDevices()` |
| 5 | Gap row "`GPS_EN_PIN` 7" as a GPS enable. The same pass corrected plan 318's GNSS table, which had described the M10S trim as the same board and net as the L76K trim. | GPIO7 is a WAKE_UP line on the L76K trim only: the ESP32S3 sheet maps GPIO7 to GPS_WAKEUP and the Radio and GNSS sheet drives the L76K WAKE_UP input with it. The MAX-M10S has no enable input, its pin 5 is EXTINT and its pin 7 is V_IO, and there is no M10S schematic. The enable is UNVERIFIED for that trim and V_IO joins the bench checks. The vendor header defines `GPS_EN_PIN` 7 without distinguishing trims. | L32 "ESP32S3" sheet (GPIO7 to GPS_WAKEUP) and "Radio and GNSS" sheet (L76K pin 5 WAKE_UP, pin 6 V_BCKP, pin 7 RESERVED); L14 "Pin Mapping" (GNSS Wake-up (L76K only) 7); U1 table 10 (pin 5 EXTINT, pin 6 V_BCKP, pin 7 V_IO); L33 `utilities.h` |
| 6 | The dependency table offered `Panel_SH110x` with `Bus_I2C` as usable, and step 6 named an address but not a panel size. | `Panel_SH110x` with `Bus_I2C` is right, but not through `M5UnitOLED`: M5GFX 0.2.27 hardcodes `panel_width` 64 with `offset_x` 32 and defaults to the M5 Port A pins and 0x3C. The custom device must set 128x64 explicitly and the column offset is a bench check. | M5GFX 0.2.27 `src/M5UnitOLED.h`, `init_impl()` (`cfg.panel_width = 64; cfg.offset_x = 32;`) and the `M5UNITOLED_SDA`/`SCL`/`ADDR` defaults |
| 7 | The row was presented as complete at 19 columns with no note on what the columns cannot express. | `rtcBatteryBacked` is the only value change. `minBrightness`, `batteryCapacityMah`, `sdSlot` and `gpsRail` cannot be expressed honestly with the current column semantics, recorded above as a gap rather than filled with invented values. | `include/FurbleBoard.h:36-54` column comments; `src/FurbleBoard.cpp` table |
| 8 | Step 3: add GPS UART pins (RX 9, TX 8, 9600 baud) to the row. | That would make 22 columns and put wiring in a table of runtime facts. The pins belong in the pin seam, the two `M5.getPin()` calls in `GPS::getInstance()`, which return -1 on this board. The baud is already the `GPS_BAUD` setting, default `BAUD_9600`. | `src/FurbleGPS.cpp:167-169`; `include/FurbleSettings.h:216-217`; `src/FurbleSettings.cpp:741` |
| 9 | Nothing said about PSRAM width, so the plan could not say whether GPIO33 to GPIO37 survive. | The 8 MB PSRAM is quad (QSPI), so GPIO33 to GPIO37 stay usable: the IMU takes 33 and 34, the peripheral SPI 35, 36 and 37. An octal-PSRAM S3 board would take those pins. | L14 "Product Parameters" (PSRAM 8 MB (QSPI)) and "Pin Mapping"; L31 ESP32-S3 module block |

Source key for this table. L14, L30, L31, L32 and L33 resolve in full in plan
318's Sources section, added there in the same pass. U1 is the u-blox MAX-M10S
data sheet UBX-20035208, already a source of plan 318. M5GFX 0.2.27 and the
AXP2101 datasheet revision 1.0 are the pinned library and the datasheet this
plan's hal-1 already cites (`lib/hal/AXP2101.h`), section 6.13.2.75 for the
REG 0x90 enable bits and section 6.5.4 for the off state.

Still unsettled by the documents, and left as open items rather than guessed:
whether BAT1 is populated, which RTC part is fitted (PCF8563 or PCF85063ATL),
what supplies the M10S module's V_IO, which radio band maps to the VHF and UHF
straps, and the panel's column offset.

## Follow-up: the Waveshare board row (plan 349)

`src/FurbleBoard.cpp` gained a dedicated row for the Waveshare ESP32-S3-ETH,
keyed on `m5::board_t::board_M5StampS3`, so the board no longer resolves to the
unknown-board row. That row and the platform refusal are specified in
[349-waveshare-board-row.md](../349-waveshare-board-row.md).

The row needed one column this plan did not have. `canPowerOff` reads as "the
board can power off", but its consumers are the UI policy gates: the Auto off and
Low battery rollers and their timers. The Core reports false there because its
IP5306 gives a coarse level and no charging state, while M5Unified still powers
that board down through the IP5306 boost keep-off. Gating `Platform::powerOff()`
on the same column would have refused the Core's manual Off, and the two
`m5stack-core` power-off scenarios assert that it succeeds (through the
simulator seam, so they pin the intent and the regression surface, not the
silicon). The new `powerOffPath` column answers "the platform can remove power",
is true for every existing row including the unknown-board row, and is false
only on the Waveshare row.

`Board::profile()` is still not cached. The row is a constant table entry and the
lookup is the same switch it was.
