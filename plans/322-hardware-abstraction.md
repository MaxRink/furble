# 322 - Hardware abstraction for the next boards

Status: in progress. Branches `refactor/hal-1-drivers` (pure drivers) and
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

Planned, see the implementation state below.

- `Board::Profile`: one constant row per board, selected once at boot. It
  carries the short name, battery caps and capacity, battery source, RTC
  backing, IR pin, motion wake pin, timed-wake kind, PMIC button kind, SD bus
  sharing, `canPowerOff`, `modemSleep` and the brightness floor. Non-UI modules
  read the row instead of switching on `M5.getBoard()`.
- Power backend: the battery read, charging, power off, timed wake, named
  rails and PMIC button behind one class whose implementation is chosen at link
  time, the way `sim/FurblePlatformSim.cpp` replaces `Platform` today. The M5
  implementation keeps the current `M5.Power` and M5PM1 code.
- Display backend description: a panel config per board plus the brightness
  and sleep rules (APB lock while lit, 120 ms SLPIN/SLPOUT dwell).
- Input class description: buttons, encoder, keyboard, touch.

### T-Beam Supreme profile row (not added)

```
{ name "T-Beam S", battery {level, voltage, charging; no current}, source
  PMIC_GAUGE, capacity 0 (user 18650), rtcBatteryBacked true (PCF8563 on VBAT),
  irPin none, motionWakePin none, timedWake NONE, pmicButton AXP2101_PEK,
  sdSharesDisplayBus false, canPowerOff true, modemSleep true (32 kHz
  crystal), minBrightness 32, rails {GPS ALDO4, LORA ALDO3, SENSOR ALDO1|ALDO2,
  SD BLDO1|BLDO2}, protected {DCDC1} }
```

## Implementation state

- hal-1: `lib/hal/` drivers and `tests/host/hal_drivers_test.cpp` (CTest
  `hal-drivers`). `lib/hal` added to the portable contract, the static analysis
  scope and its workflow path filter. Nothing in firmware includes `lib/hal`
  yet, so every firmware image is unchanged.
