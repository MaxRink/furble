# 387 - The LilyGO family port: T-Deck and T-Watch Ultra, and what the rest cost

Status: research, plan and implementation. This plan adds two board rows, two
firmware environments, the LilyGO family seam the two share, two simulator rows
and four simulator scenarios. It does not add the T-Beam Supreme, which is in
flight on `feat/tbeam-supreme-port` (plans 365), and it does not add any board
whose facts it could not check.

Sources were read on 2026-11-27. Vendor pages change without notice, so every
URL below is recorded with that date and the claim it supports. Facts about this
checkout were read at master `b509486b`, the base of this branch.

Tags on every external claim:

- `[vendor: S#]` a LilyGO product, wiki or library document, or a LilyGO
  schematic, listed in Sources.
- `[library: M]` read in the pinned M5GFX 0.2.27 tree. `.pio` is ignored, so
  this is a library citation, not a repository file.
- `[repo: R#]` read in this repository, at the path the label resolves to.
- `[inferred]` my conclusion, stated by no source.
- **UNVERIFIED** no readable document settles it; listed again in section 12.

## 1. Why the family and not one board

The brief for this port named six LilyGO products and asked for the scaffolding
that makes each *additional* LilyGO board cheap rather than a new half-port.
That framing is right for this tree, because a LilyGO board costs the same three
things every time and none of them is the same thing as "a new driver":

1. **A fact row.** SoC, panel wiring, bus pins, input pins, battery, GPS pins.
2. **A board row.** `Board::Profile` in src/FurbleBoard.cpp, which the UI,
   power, feedback, GPS and SD paths all read.
3. **An environment.** An sdkconfig, a partition table, a size budget and a
   stack budget.

What *used to* make a LilyGO board a half-port is that M5Unified knows none of
them: M5Unified's pin table describes M5 hardware, `M5.begin()` probes M5
panels and PMICs, and there is no `board_t` arm to hang a row on. The T-Beam
branch (plans 365) solved half of that with `Board::compiledProfile()` and a
pinned, display-less M5GFX name. This plan adds the other half: a LilyGO facts
table, a display seam that builds a real `lgfx::LGFX_Device` from that table,
and an input seam that reports the board's own switches in the three roles the
UI already has.

With those in place, a third LilyGO board whose panel controller M5GFX 0.2.27
already carries costs one facts row, one board row and one environment. The
boards below that are *not* in that class are excluded for a stated reason, not
deferred silently.

## 2. Three corrections to the brief

Recorded as corrections rather than applied silently, so the next reader does
not repeat the assumption.

1. **`lib/hal` has no TCA8418 driver, and the T-Deck has no TCA8418.** The
   directory holds `AXP2101`, `BQ27220`, `SX1262` and `XL9555` only `[repo: R1]`.
   The T-Deck keyboard is a **second ESP32-C3** running LilyGO's keyboard
   firmware and presenting itself as an I2C slave at **0x55** `[vendor: S9,
   S5]`. There is no keypad controller chip to drive, and no register map to
   copy: the slave returns **one byte per key press** `[vendor: S9]`. What this
   port therefore needed was a small I2C *client*, not a TCA8418 driver, and
   that is what it has.
2. **The T-Watch Ultra does have onboard GNSS.** The brief expected a watch with
   no GNSS. LilyGO's own hardware page lists `GNSS: u-blox MIA-M10Q`, the pin map
   gives `GNSS TX 43, RX 44, PPS 13`, the power table gives `LDO1(VRTC): GNSS
   Backup (always on)` and `BLDO1: GNSS`, and the V1.0 schematic dated 2025-07-24
   contains the part `[vendor: S12, S13, S14, S15]`. The GPS answer is therefore
   "onboard, UBX, through the existing pin seam", not "borrow the phone's". It is
   ranked accordingly in section 4.
3. **M5GFX 0.2.27's `Panel_CO5300` is written for this panel, and its native
   geometry is 410x502, not 502x410.** Plan 383 listed the geometry as
   unverified. `Panel_CO5300.hpp` sets `memory_width = panel_width = 502` and
   `memory_height = panel_height = 410`, while its own init list programs the
   column window as `0x0016` to `0x01AF` (22 to 431, that is 410 columns) and the
   row window as 0 to 501 (502 rows) `[library: M]`. The two disagree, and the
   init list is what the controller obeys: the panel is 410 columns by 502 rows
   with a 22 column offset, which LilyGO publishes as a 2.06 inch 502x410 AMOLED
   in landscape `[vendor: S12]`. So the driver class is already written for the
   exact part, but the port must not inherit its geometry numbers: it configures
   the native 410x502 at `offset_x = 22` and selects rotation 1, where
   `Panel_AMOLED::setRotation` swaps both the size and the offset and
   `setWindow` then writes CASET 0 to 501 and RASET 22 to 431, the same 410
   columns the init list programmed `[library: M]`. Configuring 502x410 at
   rotation 0 with that offset asks for CASET 22 to 523 and puts 92 columns of
   every frame off the panel.

## 3. The family table

Columns are the facts a port needs. "M5GFX panel" is whether the pinned library
already carries the controller, because that decides whether a board needs a
driver before it can have a row. Prices are the vendor's on the retrieval date
and are context only.

| Product | SoC, flash, PSRAM | Panel, controller, M5GFX | PMIC and rails | GNSS | SD / speaker | Input | Case | Battery | Port order |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **T-Deck** | ESP32-S3FN16R8, 16 MB, 8 MB OPI `[vendor: S1, S5]` | 2.8 in 320x240 IPS, **ST7789**, SPI, **yes** `[vendor: S1, S5]` | **none**; charger with a fixed 500 mA current, battery on a divider at GPIO4 `[vendor: S5]`; the 2:1 ratio is the vendor example's, not the page's `[vendor: S10]` | no receiver; Grove UART 43/44 `[vendor: S5]` | microSD CS 39 on the panel bus; MAX98357A on I2S 5/7/6 `[vendor: S5, S14]` | ESP32-C3 keyboard at 0x55, INT 46; five-switch trackball 2/3/1/15 + 0 `[vendor: S5, S9, S14]` | moulded shell part of the product; CAD published `[vendor: S6]` | **cell not in the box**; JST connector, 2000 mAh nominal `[vendor: S1, S5]` | **1, implemented** |
| **T-Deck Plus** | same mainboard `[vendor: S2, S5]` | same | same | **MIA-M10Q onboard**, UBX, on the Grove pins 43/44 `[vendor: S4, S5]` | same | same | same | **2000 mAh built in** `[vendor: S2]` | **1, same row** |
| **T-Watch Ultra** | ESP32-S3, 16 MB QSPI, 8 MB QSPI `[vendor: S11, S12]` | 2.06 in 502x410 AMOLED, **CO5300** QSPI, **yes** `[vendor: S12, S13]` | **AXP2101** 0x34; DC1 SoC, VRTC GNSS backup, ALDO1 SD, ALDO2 display, ALDO3 LoRa, ALDO4 sensor, BLDO1 GNSS, BLDO2 speaker, DLDO1 NFC `[vendor: S13]` | **MIA-M10Q onboard**, UBX, TX 43 / RX 44 / PPS 13 `[vendor: S13, S14, S15]` | microSD CS 21 on the shared bus; MAX98357A on I2S 9/10/11 `[vendor: S14]` | CST9217 touch at 0x1A (**no M5GFX driver**); BOOT on GPIO0; POWER is the PMIC key `[vendor: S13, S14; library: M]` | enclosure with strap; printable case published `[vendor: S12, S23]` | **1100 mAh built in** `[vendor: S12]` | **2, implemented (touch and PMU telemetry blocked)** |
| **T-LoRa Pager** | ESP32-S3, 16 MB, 8 MB `[vendor: S18]` | 2.33 in 222x480; controller not published (**ST7796U** per `[repo: R2]`, **UNVERIFIED**); **no M5GFX ST7796** `[library: M]` | TI charger and gauge: BQ25896 + BQ27220 per `[repo: R2]`; BQ27220 driver exists, **BQ25896 does not** `[repo: R1]` | **MIA-M10Q**, UBX, 38400 baud, enable through the expander `[vendor: S18, S19]` | microSD; ES8311 codec `[vendor: S18]` | QWERTY keypad plus encoder and side keys `[vendor: S18]` | shell and folding antenna `[vendor: S18]` | cell included `[vendor: S18]` | 3, blocked on the panel driver |
| **T-Watch S3** | ESP32-S3, 16 MB, 8 MB `[vendor: S17]` | 1.43 in round AMOLED 466x466; controller not published, **UNVERIFIED** | AXP2101 `[vendor: S17]` | **none** `[vendor: S17]` | microSD; MAX98357A `[vendor: S17]` | touch, one side button `[vendor: S17]` | watch case and strap `[vendor: S17]` | cell included `[vendor: S17]` | 4, after the Ultra proves the AMOLED path |
| **T4-S3** | ESP32-S3R8, 16 MB, 8 MB `[vendor: S20]` | 2.41 in 450x600 AMOLED, QSPI; controller not published, **RM67162 or SH8601 inferred**, **neither in M5GFX** `[library: M]` | none published; **UNVERIFIED** | **none** `[vendor: S20]` | microSD; speaker provision not published | BOOT and RESET only `[vendor: S20]` | **bare board**; a shell is an optional variant `[vendor: S20]` | **none** `[vendor: S20]` | last, blocked on a panel driver and on having no cell |
| **T-Display S3** | ESP32-S3R8, 16 MB, 8 MB `[vendor: S21]` | 1.9 in 170x320, ST7789V, **8-bit parallel** `[vendor: S21]`; M5GFX has ST7789 but this port drives it over SPI, parallel is unproven | none; battery voltage on IO04 `[vendor: S21]` | **none** | microSD provision not published | BOOT, RESET, IO14 `[vendor: S21]` | **bare board**; a shell is an optional variant `[vendor: S21]` | **none** `[vendor: S21]` | last, bare and cheapest |

Not in the table, and why:

- **T-Echo (and T-Echo Plus/Lite).** nRF52840 with 1 MB flash and 256 KB RAM
  `[vendor: S22]`, so it cannot run this tree's ESP-IDF image at all. Plan 362
  already rejected the nRF family: BLE central with the camera protocols needs a
  SoftDevice/NimBLE port that does not exist here, and the T-Echo's published
  "RAM 2MB" is its QSPI flash, not RAM `[repo: R3]`. Rejected, not deferred.
- **T-Deck Pro, T-Deck Max, T-Display S3 AMOLED, T-Embed, T3-S3.** Not asked
  for, and each would need the same three facts a row above already has; none is
  cheaper than a row in this table once its controller is verified.

## 4. Finished-article ranking: case and battery first

The device has to be a finished article, so the ranking below is by what you can
hold, not by what is cheapest to port. "Cased" means the retail unit arrives in
an enclosure with the electronics inside it; "shell sold separately" means
LilyGO sells an enclosure as a variant or a separate product; "bare" means a
PCB.

| Rank | Product | Article | Case | Battery | GPS in the box | Verdict |
| ---: | :--- | :--- | :--- | :--- | :--- | :--- |
| 1 | **T-Deck Plus** | handheld | **cased** `[vendor: S2]` | **2000 mAh built in** `[vendor: S2]` | **MIA-M10Q** `[vendor: S4]` | The only LilyGO product here that is a finished remote with GNSS, keyboard, trackball, speaker, card and cell out of one box. |
| 2 | **T-Deck** | handheld | **cased** `[vendor: S1, S6]` | connector only, **cell not in the box** `[vendor: S1]` | no receiver; Grove UART `[vendor: S5]` | Same article minus GNSS and cell. The one this port implements, because the Plus shares its row. |
| 3 | **T-Watch Ultra** | watch | **cased with a strap** `[vendor: S12]` | **1100 mAh built in** `[vendor: S12]` | **MIA-M10Q** `[vendor: S13]` | A finished wearable with GNSS. Its input depends on a touch driver this tree does not have, which is the one thing between it and rank 1. |
| 4 | **T-LoRa Pager** | handheld | **cased**, folding antenna `[vendor: S18]` | cell included `[vendor: S18]` | **MIA-M10Q** `[vendor: S18]` | A finished article with the best power subsystem (charger plus gauge), blocked on a panel driver and a charger driver. |
| 5 | **T-Watch S3** | watch | **watch case and strap** `[vendor: S17]` | cell included `[vendor: S17]` | **none** `[vendor: S17]` | Finished, but the one wearable here that cannot geotag by itself. |
| 6 | **T4-S3** | bare board | shell sold separately `[vendor: S20]` | **none** `[vendor: S20]` | **none** | Desk hardware. Dropped below the watches despite being cheaper. |
| 7 | **T-Display S3** | bare board | shell sold separately for the soldered versions `[vendor: S21]` | **none** `[vendor: S21]` | **none** | The cheapest LilyGO port and the least useful remote. |

What a case does to the antenna, where the vendor says anything:

- The **T-Deck Plus** ships with an external-antenna variant and the shell has a
  cut-out for the SMA pigtail `[vendor: S2]`; the base T-Deck's LoRa antenna is
  internal and the shell CAD includes it `[vendor: S1, S6]`. **UNVERIFIED:** the
  effect of the shell on GNSS reception. No vendor document measures it, and
  LilyGoLib publishes no antenna keep-out drawing for the GNSS.
- The **T-Watch Ultra** carries the GNSS and LoRa antennas inside a
  `63.5 x 49 x 22 mm` enclosure `[vendor: S12]`. **UNVERIFIED:** antenna
  performance with the strap fitted, and whether the printable case
  `[vendor: S23]` is dimensionally identical to the shipped one. Nothing in the
  vendor material says.
- **The trackball and the case.** This is the usability defect the brief asked
  about. The T-Deck's trackball sits under an opening in the moulded shell and
  the shell CAD includes that opening `[vendor: S6]`, but **no vendor document
  states the clearance or that the ball cannot foul the shell**; the review
  literature that exists complains about the touch panel on the Plus, not the
  trackball `[repo: R4]`. **UNVERIFIED:** that a production shell does not foul
  the trackball. A device must settle it before the trackball is claimed as
  usable hardware.

There is **no official case sold separately for the T-Deck or the T-Deck Plus**:
the shell is part of the product and LilyGO sells no `T-Deck Shell` SKU, unlike
the `T-Display S3 Shell` and `T-Embed Series Shell` it does sell `[vendor: S1,
S21]`. For a replacement, the published CAD is the third-party route: LilyGO's
own `shell/T-Deck-Shell.stp` and `T-Deck-Plus-Shell.stp` `[vendor: S6]` can be
printed or machined by any shop, which is better than naming an unverified
third-party seller.

## 5. What the family shares, and what this plan built for it

Four seams, each of which a third board reuses without new code:

1. **`include/FurbleLilyGo.h`** - the facts table (`Facts`: SPI bus, panel
   wiring and geometry, I2C bus and device addresses, input pins, speaker pins,
   peripheral gate, battery ADC) plus the two pure decoders the input devices
   are built from. A board adds a row here.
2. **`include/FurbleDisplay.h`** - the display seam. On an M5 board `screen()`
   returns `M5.Display` and is defined in src/FurbleUI.cpp beside the code that
   calls it, under the M5 arm of the same capability macro; the rest of the seam
   is an inline forwarder, so the six shipping images behave exactly as before
   and the header names no `M5.` symbol. src/FurblePlatform.cpp does not define
   it. Review 549, finding 1: an earlier draft of this plan said it did, and no
   M5 image linked as a result. On a LilyGO board the seam returns a device built from an M5GFX panel
   class plus the facts row: `lgfx::Panel_ST7789` over SPI, or
   `lgfx::Panel_CO5300` over QSPI. A board whose controller M5GFX carries needs
   no display code at all.
3. **`Furble::LilyGo::Buttons`** - the input seam, section 7.
4. **`Board::compiledProfile()`** - shared with the T-Beam branch (plans 365), so
   a single-board image answers from its build macro rather than from a detected
   board M5Unified cannot detect.

The panel configuration path deserves one paragraph of its own, because the
brief asked for one. M5GFX 0.2.27 carries `ST7789`, `ST7735`, `GC9A01`,
`ILI9342`, `SSD1306`/`SH110x`, `CO5300` and `AMOLED`; it does **not** carry
`ST7796`, `RM67162` or `SH8601` `[library: M]`. So:

- An `ST7789` board is configuration only: bus pins, CS/DC/RST, native geometry,
  the rotation the UI draws in, invert, and whether it shares its bus with a
  card. The T-Deck needs exactly that.
- A `CO5300` board is also configuration only, plus the panel's address-window
  offset and the native-versus-driven orientation: `Panel_AMOLED::setWindow()`
  adds `config().offset_x` to a window measured in the panel's own axes, and
  `Panel_CO5300`'s own init programs columns 22 to 431 of a panel that is 410
  columns by 502 rows `[library: M]`. The row therefore carries the native
  410x502 and `offset_x = 22`, and the driver selects rotation 1 for the 502x410
  landscape the UI draws in, which is the rotation whose window matches the init
  list. The host test pins both the native numbers and the derived driven size.
- An `ST7796`, `RM67162` or `SH8601` board needs a **driver** first: a panel
  class with its init sequence and window quirks. That is why the T-LoRa Pager
  and the T4-S3 are absent from the table above rather than stubbed. Writing one
  from a pin list would be the invented-values failure this repository has
  already had.

## 6. T-Deck: the row, the environment and the wires

Every column, with its source:

| Column | Value | Source |
| :--- | :--- | :--- |
| `name` | `T-Deck` | boot info block; the vendor product name |
| `battery` caps | none claimed | this port does not drive the GPIO4 divider yet, so no level, voltage, current or charging bit is offered |
| `batteryCapacityMah` | 2000 | `[vendor: S5]` "2000 mAh lithium polymer"; also S3 |
| `rtcBatteryBacked`, `rtcOnBus` | false, false | the board has no RTC part `[vendor: S5]` |
| `irPin` | -1 | no emitter `[vendor: S5]` |
| `motionWake`, `motionWakePin` | NONE, -1 | no IMU `[vendor: S5]` |
| `timedWake`, `powerHoldPin` | NONE, -1 | mechanical power switch, no PMIC, no hold pin `[vendor: S5]` |
| `pmicClickStreak` | false | no PMIC `[vendor: S5]` |
| `sdSlot`, `sdCsPin` | true, 39 | microSD CS 39 on the shared SPI bus `[vendor: S5, S14]` |
| `canPowerOff`, `powerOffPath` | false, false | nothing to switch; the switch is mechanical `[vendor: S5]` |
| `modemSleep` | false | left to the setting, as on every row |
| `minBrightness` | 16 | a slider step multiple; the backlight is a 16-level pulse driver `[vendor: S5]`, see the limitation below |
| `feedbackSound` | true | MAX98357A on I2S `[vendor: S5, S14]` |
| `feedbackLight`, `feedbackVibration` | false, false | no LED, no motor `[vendor: S5]` |
| `ledPin` | -1 | none |
| `gpsRail` | false | the receiver is on 3V3 with no switch `[vendor: S5]` |
| `ledActiveLow`, `irMarkLow` | false, false | no driven LED or emitter stage |
| `gpsPinSource`, `gpsRxPin`, `gpsTxPin` | PROFILE, 44, 43 | the T-Deck Plus puts its MIA-M10Q on the Grove pins; the base board leaves them on the connector `[vendor: S4, S5, S14]` |

Two limitations recorded rather than papered over:

- **Backlight.** The T-Deck's backlight is an AW9364 driven by a *pulse train* on
  GPIO42, with 16 levels, not a PWM dimmer `[vendor: S5]`. M5GFX's `Light_PWM`
  would hold a duty cycle that part does not read as a level. This port drives
  the pin on or off (`LightDigital` in src/FurbleDisplay.cpp), so the brightness
  slider switches the backlight rather than dimming it. The 16-level interface
  is a follow-up that needs the AW9364 datasheet, which LilyGO does not publish;
  writing it from the vendor's `BrightnessController<..., 0, 16, 30>` template
  argument alone would be a guess.
- **Battery.** The ADC pin, GPIO4, is a vendor fact `[vendor: S5]`. The 2:1 ratio
  is not: the page names the pin and no ratio, and the ratio comes from the
  vendor example, which reads the pin with `readAdcVoltage(BAT_ADC_PIN) * 2`
  `[vendor: S10]`, so a 4.2 V pack reads 2.1 V at the pin. Treat it as
  example-derived rather than datasheet-confirmed. Reading
  it needs a new ADC path in `Platform`, which this port does not add, so the
  row claims no battery telemetry and the UI shows none. That is the honest
  state, and the alternative (a voltage-to-percentage curve of my own invention)
  is exactly the kind of number this repository has removed before.

Environment `lilygo-tdeck`: `board = esp32-s3-devkitc-1`, 16 MB flash,
`partitions_two_ota_16m.csv` (two 6 MB slots), MQTT and WebUI on,
`-DFURBLE_LILYGO_TDECK`, M5GFX pinned at `board_M5StampS3` (the display-less
family, so autodetect takes the no-probe return), and `-DFURBLE_IMU_BMI270=0`
because the board has no IMU.

## 7. Input: what the keyboard and the trackball do in the UI today

The UI has three input roles: **LEFT** (previous), **RIGHT** (next) and
**SELECT** (activate), each an LVGL encoder with a `data->key` of
`m_KeyLeft`/`m_KeyRight`/`m_KeyEnter`. On the M5 boards a physical button drives
each role. A LilyGO board fills the same three roles from
`Furble::LilyGo::Buttons`, so nothing above the seam changed: the long-press
menu on the LEFT role, the companion press-edge boost and the wake-on-press gate
all still run.

The mapping is a fact of the hardware, not a design preference:

- **Trackball** (T-Deck): the **right** and **down** switches drive **RIGHT**,
  the **left** and **up** switches drive **LEFT**, and the **center** switch
  drives **SELECT**. The switches are active-low on GPIO 2 (right), 3 (up), 1
  (left), 15 (down) and 0 (center), which is the vendor's own mapping
  `[vendor: S14, S10]`. Up and Down take the horizontal roles because every list
  in this UI is one-dimensional.
- **Keyboard** (T-Deck): **Enter (0x0D) drives SELECT** and **Backspace (0x08)
  drives LEFT**, which is the UI's universal back action; Alt+C (0x0C) also maps
  to LEFT `[vendor: S9]`. **Nothing else does anything today**: the vendor key
  map is 35 keys of letters, digits, symbols, space, ALT, Mic, Enter, Backspace
  and Right Shift, and **it has no arrow keys at all** `[vendor: S9]`. So the
  keyboard cannot move focus on real hardware, and this port does not invent
  arrow behaviour for it. The letters are decoded and dropped because the UI has
  no text entry; when it gains one, the decode is already in place.
- **Side button** (T-Watch Ultra): BOOT on GPIO0 drives **SELECT** `[vendor:
  S14]`. With no touch driver (section 8) it is the only user input, so the
  Ultra's UI can activate the focused row but cannot move focus. That is a
  support limitation of this port, stated here and in `docs/supported-hardware.md`.

One-shot versus level: the keyboard sends **one byte per press** and 0x00 when
nothing is pending `[vendor: S9]`, so a key is latched as a one-shot press and
released on the next UI cycle. The trackball switches are levels, so a held
direction repeats through LVGL's long-press repeat. `Buttons::sample()` runs once
per LVGL cycle, before the input devices are read, so all three roles see the
same snapshot and a key is consumed exactly once.

## 8. T-Watch Ultra: the row, the environment, and what is blocked

Implemented:

- The **panel**: `lgfx::Panel_CO5300` on a quad-SPI `lgfx::Bus_SPI` (SCLK 40,
  D0-D3 38/39/42/45, CS 41, RST 37, all from the facts row rather than literals),
  configured at the panel's **native 410x502 with `offset_x = 22`** and driven at
  **rotation 1**, where the driver writes CASET 0 to 501 and RASET 22 to 431,
  matching the columns the controller's own init programmed. The UI draws
  502x410 `[library: M; vendor: S14, S15]`. RST is pulsed low for 10 ms and then
  high for 120 ms before init, because `init_without_reset()` never touches it.
- The **rails** the port needs: ALDO2 (display), ALDO1 (SD), BLDO2 (speaker) and
  BLDO1 (GNSS) at 3300 mV through the `lib/hal` AXP2101 driver, then XL9555 pin 7
  (display supply enable) high `[vendor: S13]`. VRTC is not switched: the
  AXP2101 has no enable for it and it keeps the receiver's backup alive
  `[repo: R1]`.
- The **environment** `lilygo-twatch-ultra`, same shape as the T-Deck's.
- The **side button** on GPIO0 as the SELECT role.
- The **GPS pins** on the UBX path: PROFILE, RX 44, TX 43 `[vendor: S14]`.

Blocked, with the artifact that would unblock each:

1. **Touch.** The controller is a CST9217 at 0x1A `[vendor: S13, S14]`. M5GFX
   0.2.27 has `Touch_CST816S` (0x15) and `Touch_CST226` (0x5A/0x1A, a different
   Hynitron protocol) and no CST92xx `[library: M]`. **UNVERIFIED:** the CST9217
   register map; LilyGO publishes a driver, not a datasheet. Writing a touch
   driver from the vendor's driver source would be a copy of an undocumented
   protocol, so this port claims no touch and the row's input is the side button
   alone. Unblocked by a CST9217 datasheet, or by a bench capture of the vendor
   driver's transactions.
2. **Battery telemetry and power-off.** The AXP2101 has both (the `lib/hal`
   driver reads voltage, percent and charge state, and can cut power), but
   wiring them into `Platform` is the power backend the T-Beam branch owns
   (plans 365). This port does not duplicate it, so the row claims no battery
   caps and no power-off path, and the UI offers no auto-off. Unblocked by that
   backend landing; then the row's `battery` and `powerOffPath` columns become
   true.
3. **The BHI260AP and the DRV2605.** Present, no backend in this tree, so no
   motion wake and no vibration are claimed `[vendor: S13]`.

## 9. Simulator

- Two hardware-model rows (`tdeck`, `twatch-ultra`) carrying the facts above,
  including `display` geometry, PMIC, gauge, SD, audio, RTC and the profile
  columns the `board-row-*` scenarios compare against `Board::profile()`.
- The two panels in the model: 320x240 and 502x410, from the same facts row the
  firmware uses, through `lgfx::Panel_sdl`. A scenario captures what the firmware
  would draw at the real geometry.
- Scenario verbs: the LilyGO images expose `btn left`, `btn right`, `btn select`
  (the input roles) and `btn enter`, `btn back`, which inject the keyboard bytes
  0x0D and 0x08 through the same decoder the I2C read feeds. The sim exercises
  the decode and the role mapping; it does not exercise the I2C transport, and
  `docs/sim.md` says so.
- Four scenarios: `board-row-tdeck`, `board-row-twatch-ultra`,
  `tdeck-keyboard-nav` (Enter and Backspace drive the UI) and
  `tdeck-gps-sd` (the GPS UART path and the SD mount path).

## 10. Tests

- `board-profile` keeps its M5 rows, and two new targets
  (`board-profile-lilygo-tdeck`, `board-profile-lilygo-twatch-ultra`) pin **every
  column** of the new rows, including that `Board::profile()` ignores the
  detected M5 board and that the GPS pins come from the row rather than
  M5Unified.
- `lilygo-family` pins the facts table (both boards, every pin), the keyboard
  and trackball decoders, and the one-shot latch.

## 11. Coordination with the T-Beam branch

The T-Beam Supreme port is in flight on `feat/tbeam-supreme-port` (plans 365)
and had no commits when this branch started; its work was read from that
worktree. Overlaps and how they resolve:

- **`Board::compiledProfile()`**: the T-Beam branch introduced the name and the
  semantics. This branch adopts the same name, signature and meaning, and both
  rows are kept. Whichever lands second resolves a small conflict in
  src/FurbleBoard.cpp and keeps both rows.
- **The panel path**: the T-Beam branch adds a display seam for its 128x64
  SH1106 (`include/FurbleDisplay.h`) with a `screen()/begin()/ready()/width()/
  height()/mono()/push()/wait()` API. This branch needs the same seam for two
  colour panels and defines the same header with the subset it uses
  (`screen()/begin()/ready()/width()/height()`); the second landing should keep
  the union and route both families through it.
- **The AXP2101 backend**: the T-Beam branch owns it. This branch uses the
  `lib/hal` AXP2101 driver only for rail bring-up and claims no battery or
  power-off, exactly so the two do not duplicate.
- **Order**: this branch is based on master `b509486b`, which contains neither
  branch, so **nothing has landed first**; the T-Deck row and the T-Beam row do
  not conflict as data.

## 12. Sources

Product and wiki pages, all retrieved 2026-11-27:

- S1 LilyGO T-Deck product page, "ESP32-S3FN16R8", "Flash 16MB", "PSRAM 8MB",
  "Battery ADC PIN IO04", "2.8 inch ST7789", the "What's In The Box" list and the
  absence of a case SKU: https://lilygo.cc/products/t-deck
- S2 LilyGO T-Deck Plus product page, "mainly added GPS module, and Built-in
  2000 mah battery", Ublox variants, external-antenna variant:
  https://lilygo.cc/products/t-deck-plus-1
- S3 LilyGO wiki, T-Deck: https://wiki.lilygo.cc/products/t-deck-series/t-deck/
- S4 LilyGO wiki, T-Deck Plus, "adds a GPS module (MIA-M10Q) with the Grove
  interface pins repurposed for GPS":
  https://wiki.lilygo.cc/products/t-deck-series/t-deck-plus/
- S5 LilyGoLib hardware page for the T-Deck, "Pin Map", "I2C Device Addresses",
  "Power Notes", "Electrical Specifications", the ESP32-C3 keyboard and the note
  that "T-Deck-Plus uses the same main GPIO mapping but adds an onboard GNSS
  module":
  https://github.com/Xinyuan-LilyGO/LilyGoLib/blob/master/docs/hardware/lilygo-t-deck.md
- S6 LilyGO T-Deck repository `shell/` directory: `T-Deck-Shell.stp`,
  `T-Deck-Plus-Shell.stp`, `T-Deck.dxf` and the shell renders, plus
  `schematic/schematic.pdf` (Altium sheet dated 2023/6/1) and
  `examples/UnitTest/utilities.h`:
  https://github.com/Xinyuan-LilyGO/T-Deck
- S7 T-Deck GPS shield schematic, MIA-M10Q with PPS and an MS412FE backup cell:
  https://github.com/Xinyuan-LilyGO/T-Deck/blob/master/schematic/T-Deck-GPS%20V1.0%20%2024-03-29.pdf
- S8 T-Deck L76K shield schematic:
  https://github.com/Xinyuan-LilyGO/T-Deck/blob/master/schematic/T-Deck-L76K%20V1.0%20%2024-08-12.pdf
- S9 T-Deck keyboard firmware, the I2C slave at 0x55, the 7x5 key map, Enter as
  0x0D, Backspace as 0x08, Alt+C as 0x0C and the one-byte reply:
  https://github.com/Xinyuan-LilyGO/T-Deck/blob/master/examples/Keyboard_ESP32C3/Keyboard_ESP32C3.ino
- S10 T-Deck board pin definitions (`BOARD_*`, the trackball `BOARD_TBOX_G0x`
  names and the I2S pins):
  https://github.com/Xinyuan-LilyGO/T-Deck/blob/master/examples/UnitTest/utilities.h
- S11 LilyGO T-Watch Ultra product page, "Battery 1100mA (4.07 Wh)", "2.06 inch
  CO5300 (QSPI) AMOLED", CST9217 touch:
  https://lilygo.cc/products/t-watch-ultra
- S12 LilyGO wiki, T-Watch Ultra, the product parameters, "The enclosure measures
  63.5 x 49 x 22 mm without the strap" and the GNSS answer:
  https://wiki.lilygo.cc/products/t-watch-series/t-watch-ultra/
- S13 LilyGoLib hardware page for the T-Watch Ultra, "Pin Map", "I2C Device
  Addresses", "Power Management Channels" and "Electrical Specifications":
  https://github.com/Xinyuan-LilyGO/LilyGoLib/blob/master/docs/hardware/lilygo-t-watch-ultra.md
- S14 LilyGoLib T-Watch Ultra Arduino variant, `DISP_*`, `TP_*`, `LORA_*`,
  `SD_CS`, `I2S_*`, `GPS_TX/GPS_RX/GPS_PPS` and `EXPANDS_*`:
  https://github.com/Xinyuan-LilyGO/LilyGoLib/blob/master/variants/lilygo_twatch_ultra/pins_arduino.h
- S15 LilyGO T-Watch Ultra schematic V1.0 dated 2025-07-24, the MIA-M10Q and PPS
  nets and the AXP2101 rail assignments:
  https://github.com/Xinyuan-LilyGO/LilyGoLib/blob/master/schematic/T-Watch%20Ultra%20V1.0%20SCH%2025-07-24.pdf
- S16 LilyGoLib T-Deck Arduino variant (`DISP_*`, `TRACKBALL_*`, `I2S_*`,
  `GPS_TX/GPS_RX`, `KB_INT`, `BAT_ADC`):
  https://github.com/Xinyuan-LilyGO/LilyGoLib/blob/master/variants/lilygo_tdeck/pins_arduino.h
- S17 LilyGO T-Watch S3 product page:
  https://lilygo.cc/products/t-watch-s3
- S18 LilyGO T-LoRa Pager product page, "U-blox GPS module (MIA-M10Q)", "TI
  Power Management Monitoring Chipset", ES8311, encoder and keypad:
  https://lilygo.cc/products/t-lora-pager
- S19 LilyGO wiki, T-LoRa Pager quick start:
  https://wiki.lilygo.cc/products/t-lora-series/t-lora-pager/
- S20 LilyGO T4 S3 product page, "2.41-inch SPI RGB AMOLED", "450(W) x RGB x
  600(H)", "Boot Button + Reset + Micro SD", the shell variants and the absence
  of a battery: https://lilygo.cc/products/t4-s3
- S21 LilyGO T-Display S3 product page, "1.9 inch ST7789V", "8-Bit Parallel
  Interface", the shell availability note and "Bat voltage detection IO04":
  https://lilygo.cc/products/t-display-s3
- S22 LilyGO T-Echo product page, "MCU NRF52840", the flash and RAM figures:
  https://lilygo.cc/products/t-echo-lilygo
- S23 LilyGoLib printable case directory for the T-Watch Ultra:
  https://github.com/Xinyuan-LilyGO/LilyGoLib/tree/master/case/t-watch-ultra

Repository citations, not URLs:

- R1 `lib/hal/` holds `AXP2101`, `BQ27220`, `SX1262` and `XL9555` only, so there
  is no TCA8418 driver and no BQ25896 driver (`lib/hal/CLAUDE.md`,
  `ls lib/hal`). The AXP2101 driver's VRTC note is
  `lib/hal/AXP2101.h` header comment.
- R2 `docs/hardware-catalog.md` device tables, the T-LoRa Pager panel and PMIC
  reading.
- R3 `plans/362-nrf-ble-only-port.md`, the nRF decision and the T-Echo's
  "RAM 2MB" correction.
- R4 `plans/88-alternate-hardware.md`, "LilyGO T-Deck and T-Deck Plus", the
  review literature and the touch-panel complaint.

Library citations, not URLs and not tracked files:

- M pinned M5GFX 0.2.27 under `.pio/libdeps/m5stick-s3/M5GFX/src/lgfx/v1/`:
  `panel/Panel_CO5300.hpp` (502x410 and the 22-column init window),
  `panel/Panel_AMOLED.cpp` (`setRotation()` derives `_colstart` from
  `config().offset_x`; `setWindow()` adds it), `panel/Panel_ST7789.hpp`,
  `platforms/esp32/Bus_SPI.cpp` (quad mode when `pin_io0..3` are set),
  `touch/` (the five drivers), `Light.hpp` (`ILight`, `Light_PWM`). The version
  is pinned at `platformio.ini:31`, and `.pio` is ignored by `.gitignore:1`.

## 13. UNVERIFIED

1. T-Deck backlight protocol: the AW9364 pulse-count levels beyond on/off.
2. T-Deck shell clearance around the trackball, and the shell's effect on the
   GNSS antenna. No vendor document states either.
3. T-Watch Ultra: whether the shipped enclosure is dimensionally the printable
   case; antenna behaviour with the strap fitted.
4. T-Watch Ultra: the CST9217 register map, and whether a CST226 driver would in
   fact drive it. Only a datasheet or a bench capture settles this.
5. T-Watch Ultra: PSRAM mode. LilyGO's page says "8 MB QSPI PSRAM"; this port
   configures octal mode, which is what the 16 MB/8 MB ESP32-S3 module family
   uses. A boot log would settle it.
6. T-Watch Ultra: whether a backup cell is fitted for the PCF85063A, so whether
   its calendar survives a power-off. No vendor statement.
7. T-LoRa Pager: the panel controller (ST7796U is a repository reading, not a
   vendor statement), the charger part beyond "TI Power Management Monitoring
   Chipset", and the exposed GPIO count.
8. T-Watch S3: the AMOLED controller, and therefore whether the Ultra's panel
   path covers it.
9. T4-S3: the AMOLED controller (RM67162 against SH8601) and the PMIC.
10. T-Display S3: whether M5GFX's ST7789 drives it over the 8-bit parallel
    interface this port has not exercised.
11. Exposed GPIO counts per LilyGO SKU: the pages show headers, not pin counts.
12. BLE 5 central behaviour and throughput per board: an SoC capability, not a
    per-board vendor guarantee.

## 14. Implementation state

Shipped in this branch:

- `include/FurbleLilyGo.h`, `src/FurbleLilyGo.cpp`, `src/FurbleLilyGoDevice.cpp`
  (facts, decoders, input I/O, rail bring-up). The facts row carries the native
  panel geometry, the quad-SPI bus, the keyboard interrupt and the peripheral
  gate, so no pin is written twice.
- `include/FurbleDisplay.h`, `src/FurbleDisplay.cpp` (display seam and both
  panel paths). On an M5 board `screen()` is defined in `src/FurbleUI.cpp`;
  `src/FurblePlatform.cpp` never defined it.
- `Board::compiledProfile()`, the `TDECK` and `TWATCH_ULTRA` rows, and the
  `sdCsPin` column with every existing row set to -1.
- `FURBLE_LILYGO_BOARD`, `FURBLE_HAS_LILYGO_PMIC`, `FURBLE_PANEL_502X410` and
  `FURBLE_PANEL_LARGE` capability macros.
- Environments `lilygo-tdeck`, `lilygo-tdeck-debug`, `lilygo-twatch-ultra` and
  `lilygo-twatch-ultra-debug`, all four in the `main.yml` release matrix, with
  `sdkconfig.lilygo-tdeck` and `sdkconfig.lilygo-twatch-ultra`.
- Host tests `board-profile-lilygo-tdeck`, `board-profile-lilygo-twatch-ultra`
  and `lilygo-family`, and four simulator scenarios.

### Builds, sizes and stacks

Both release environments build from a clean tree on this machine:

```
pio run -e lilygo-tdeck         SUCCESS  00:08:47
pio run -e lilygo-twatch-ultra  SUCCESS  00:10:17
```

and so do their debug environments, `lilygo-tdeck-debug` (00:10:53) and
`lilygo-twatch-ultra-debug` (00:11:31). All four pass both budgets with the
numbers now recorded in `tools/size_budget.json`:

| Env | flash | slot | DRAM | budget | IRAM | budget | stack |
| :--- | ---: | ---: | ---: | ---: | ---: | ---: | :--- |
| `lilygo-tdeck` | 1,887,344 | 6,291,456 | 48,128 | 49,152 | 114,944 | 115,712 | 10 of 10 tasks PASS |
| `lilygo-twatch-ultra` | 1,886,160 | 6,291,456 | 48,072 | 49,152 | 114,944 | 115,712 | 10 of 10 tasks PASS |
| `lilygo-tdeck-debug` | 2,294,288 | 6,291,456 | 53,356 | 54,272 | 135,168 | 136,192 | 10 of 10 tasks PASS |
| `lilygo-twatch-ultra-debug` | 2,292,896 | 6,291,456 | 53,308 | 54,272 | 135,168 | 136,192 | 10 of 10 tasks PASS |

Getting there needed three fixes that only a real build finds, and they are the
answer to "why was this never built": the ESP-IDF component manager now
resolves in this checkout, `src/CMakeLists.txt` called `target_include_directories`
before `idf_component_register` (a configure error), and the `esp32-s3-devkitc-1`
board manifest's 8 MB flash size rejected the 12.4 MB OTA table until
`board_upload.flash_size = 16MB` was set. `board_build.flash_size` alone does not
move that check.

### The sdkconfigs

`sdkconfig.lilygo-tdeck` and `sdkconfig.lilygo-twatch-ultra` started as copies of
`sdkconfig.m5stick-s3`, which is the same ESP32-S3 module family and the same
16 MB flash, 8 MB PSRAM module. What differs, and why:

- `CONFIG_ESPTOOLPY_FLASHSIZE_16MB` and `"16MB"`, which the module needs and the
  StickS3 file already had.
- `CONFIG_PARTITION_TABLE_CUSTOM` with `partitions_two_ota_16m.csv`, so both app
  slots are 6 MiB: these images carry MQTT and the WebUI, which the 3 MiB StickS3
  slot does not fit.
- PSRAM in octal mode at 80 MHz for the N16R8 module. The Ultra's page says
  "8 MB QSPI PSRAM", which is the wording for the same part, but this is the one
  setting in either file that a boot log should confirm: section 13 item 5 keeps
  it UNVERIFIED.
- No BMI270, no IMU pins, no PMIC key, because neither board has those parts; the
  Ultra's AXP2101 is driven by `lib/hal`, not by M5Unified.

A future board in this family should copy one of these two, not the StickS3 file,
because the partition table and the flash size are the two settings whose absence
is a build failure rather than a silent default.

### What is not done

- The Ultra's **touch** (CST9217, no M5GFX driver) and its **power backend**
  (AXP2101 battery, charging state, software power-off) are the two things that
  keep it from being a usable remote rather than a display.
- The T-Deck's **GPIO4 battery divider** read and the **AW9364 backlight** levels.
- The four simulator scenarios are committed as **not certified** in
  `sim/scenarios/manifest.json`: no CI job builds a LilyGO simulator binary yet,
  so nothing runs them. Each scenario's reason says exactly that.

Follow-ups, in the order the family should take them:

1. Certify the LilyGO simulator scenarios by adding the two simulator builds to
   CI, then mark them certified.
2. A CST9217 touch driver, gated on the datasheet or a bench capture, which makes
   the Ultra a usable remote rather than a display.
3. The AXP2101 power backend (with the T-Beam branch), which turns the Ultra's
   battery and power-off columns on.
4. The T-Deck's GPIO4 battery divider and the AW9364 backlight protocol.
5. The T-LoRa Pager, after an ST7796 panel class and a BQ25896 driver.
6. The T-Watch S3, after its controller is verified against the Ultra's path.
