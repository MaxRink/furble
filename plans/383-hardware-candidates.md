# 383 - Off-the-shelf port candidates, ranked

Status: research and ranking. This plan adds no board, environment, driver,
profile row or support claim. No candidate was bought, powered or measured, and
no current or timing figure here comes from a bench. Sources were read on
2026-11-26; facts about this checkout were read at master `0f3e8905`.

The document is numbered 383. It was added as 364 on 2026-11-26, when 363 was
claimed twice by open branches and 364 was free on master. It did not stay free:
`fix/ext5v-rail-ownership` (PR #493) merged as `79df13a7` an hour and a quarter
later and claimed 364 as well, so two plans and two review records shared the
number. The rail plan kept it because sixteen files outside `plans/` cite it and
this ranking was cited only from inside `plans/`, so this one moved. 383 is the
first number free above everything in use on master, on every branch and in
every open pull request. The mapping is in
[README.md](README.md#plan-renumbering).

Tags on every claim:

- `[vendor: S1]` a vendor product or documentation page, listed in Sources with
  its retrieval date.
- `[repo: L-A]` read in this repository, at the path the label resolves to. Not
  a URL.
- `[library: M]` read in the pinned M5GFX 0.2.27 dependency tree. Not a URL and
  not a tracked file: `.pio` is ignored by `.gitignore:1`, and the version comes
  from `platformio.ini:31`.
- `[inferred]` my conclusion, stated by no source.
- **UNVERIFIED** no readable page settles it; listed again in section 9.

Related: [318](318-next-board-battery-gps.md) (the board decision this document
must not overturn), [360](360-waveshare-amoled-1-8-port.md) (the AMOLED-1.8 port
that already claims part of the capability credited here),
[362](362-nrf-ble-only-port.md) (the nRF decision this document does not
reopen), [287](287-mcu-portability-and-bundle-hardware.md) (the MCU survey), and
[322](implemented/322-hardware-abstraction.md) (the HAL seams and the per-board
gap table).

## Why this exists

A port is cheap or expensive for reasons that are visible in this tree before
any hardware is bought: whether a panel controller and a PMIC already have a
driver, and whether the GPS receiver speaks the `$PCAS` dialect the current code
already sends. The earlier survey in 287 ranked devices by what they do for a
photographer. This document ranks the same field by what a port costs, so the
next board is chosen for the code it exercises rather than for its datasheet.

It is a buying guide for ports, not a support claim. Nothing below changes the
six release environments in [supported-hardware.md](../docs/supported-hardware.md).

## 1. Ranking rule

Two hard gates first, then a ratio.

**Gates.** A candidate must clear all four before it is ranked at all:

1. BLE 5 central. Every ESP32-S3 candidate clears this on the SoC. A device
   that only advertises cannot run the camera side.
2. At least 8 MB flash, for two OTA slots plus MQTT and the WebUI, which the
   4 MB profiles already drop.
3. PSRAM fitted. LVGL draw buffers and a TLS handshake share internal SRAM, and
   the 8 MB-PSRAM StickS3 is already marginal against the WebUI floor in
   [supported-hardware.md](../docs/supported-hardware.md).
4. A GPS path: a receiver onboard, or a vendor header for a module.

**Ratio.** New reusable capability classes divided by new driver or profile
units. Both halves are counted, not estimated from a feeling:

- Capability class: something furble cannot do today and would reuse on a
  second board. Counting one class twice for one board is not allowed.
- Work unit: a profile row is 1.0; each new chip driver is 1.0; configuring a
  panel whose controller M5GFX already has is 0.3; writing a panel driver M5GFX
  lacks is 1.5; a new UI tier (1-bit, round, AMOLED) is 0.5; a new input class
  (keyboard, encoder, touch callbacks) is 0.5.

**Tie-break.** When two ratios are within 0.1, the candidate with fewer parts
that have no datasheet page in `docs/components/` ranks higher, because an
unproven part is the risk that actually slips a schedule.

The ranking is a statement about port cost. It does not overrule 318, which
orders the same boards by battery floor and GNSS class.

## 2. What is already paid for

Two trees decide most of the cost. Both were read on 2026-11-26.

`lib/hal/` holds four chip drivers: `AXP2101`, `BQ27220`, `SX1262` and `XL9555`
[repo: L-A]. Parts absent from it are full-price work: `BQ25896`, `TCA8418`,
`AW9523B`, `RM67162`, `SH8601`.

The pinned M5GFX 0.2.27 panel tree holds these controllers [library: M]:

| Controller | File in `M5GFX/src/lgfx/v1/panel/` | Boards here that need it |
| --- | --- | --- |
| CO5300 AMOLED | `Panel_CO5300.hpp`, over `Panel_AMOLED.hpp` | Waveshare AMOLED 1.75-G, 2.06 |
| SH110x OLED | `Panel_SSD1306.hpp` (`Panel_SH110x`) | T-Beam Supreme |
| ST7789 | `Panel_ST7789.hpp` | T-Display S3, Cardputer Adv |
| GC9A01 | `Panel_GC9A01.hpp` | M5 Dial |
| ILI9342 | `Panel_ILI9342.hpp` | CoreS3 |
| SSD1306 | `Panel_SSD1306.hpp` | Heltec V3 and V4 |
| ST7796 | none, only a generic `Panel_LCD.hpp` | T-LoRa Pager |
| RM67162 | none | T-Display S3 AMOLED |
| SH8601 | none | T4-S3 (controller itself unconfirmed) |

`Panel_CO5300.hpp` names the T-Watch Ultra as its reference panel, so its
466x466 and 2.06-inch use is a configuration question, not a driver question
[library: M, inferred].

**No RM67162 or SH8601 driver exists in that tree** [library: M]. Both AMOLED
candidates that use them are therefore full-price panel work, which is what
demotes them below every CO5300 board.

## 3. Ranked table

Work is in the units of section 1. "Port / skip" gives the two strongest
reasons in each direction. Prices are vendor list prices where a page was read,
and none includes tax or shipping.

| # | Product | SoC | Flash / PSRAM | Display + driver | GPS + wiring | PMIC | Exposed GPIO | furble must change | Port / skip | Cap / work |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 | LilyGO T-Beam Supreme, L76K trim [S4] | ESP32-S3FN8 [S4] | 8 MB / 8 MB [S4] | 1.3 in 128x64 OLED, SH1106 (M5GFX `Panel_SH110x`) [L-A] | Onboard L76K, UART; schematic read gives RX 9, TX 8, PPS 6, WAKE 7 [L-A] | AXP2101 [L-A] | 2.54 headers, SMA, microSD [S4] | Profile row plus an AXP2101 `RegisterBus` adapter, the 1-bit UI tier, SD on SPI, `SX1262::sleep()` | Port: every driver is in-tree and the `$PCAS` GPS code is unchanged. Skip: no touch, 8 MB flash caps OTA headroom, GPS pins are schematic-only. | 5 / 2.8 = 1.8 |
| 2 | M5Stack CoreS3 [S10] | ESP32-S3 [S10] | 16 MB / 8 MB Quad [S10] | 2.0 in 320x240 ILI9342C/E, FT6336U touch; M5Unified `board_M5StackCoreS3` [S10] | None onboard; Unit GPS on a Grove port, bus unverified [S10] | AXP2101 + AW9523B [S10] | Port A/B/C plus headers [S10] | Profile row, input callbacks, battery backend through `M5.Power`, AW9523B (no HAL driver) | Port: cheapest real port, because M5Unified already detects panel, touch and power. Skip: adds almost no silicon class over Core2, and no GNSS. | 3 / 2.0 = 1.5 |
| 3 | Waveshare ESP32-S3-Touch-AMOLED-1.75-G [S14][S15] | ESP32-S3R8 [S14] | 16 MB / 8 MB [S15] | 1.75 in 466x466 CO5300 AMOLED (M5GFX `Panel_CO5300`) [S14] | Onboard LC76G plus IPEX GPS antenna [S14]; on the shared I2C bus, UART needs R15/R16 [L-B] | AXP2101 [S14] | 8-pin 2.54 header [S14] | AXP2101 adapter, CO5300 configuration, a new `$PAIR` LC76G driver, I2C arbitration with touch | Port: the only candidate adding AMOLED, PMIC and onboard GNSS at once. Skip: GPS is I2C rather than the UART the brief wants, and it shares the touch bus. | 3 / 4.3 = 0.7 |
| 4 | LilyGO T-LoRa Pager [S5] | ESP32-S3 [S5] | 16 MB / 8 MB [S5] | 2.33 in 222x480; controller not stated, ST7796U per [L-B], needs `Panel_LCD` plus an init table | Onboard u-blox MIA-M10Q, UBX [S5] | "TI power management and monitoring chipset" [S5], BQ25896 plus BQ27220 per [L-B], XL9555 | Encoder, keypad, TF card [S5] | ST7796 init table, UBX driver, TCA8418, BQ25896, XL9555 rails, encoder class | Port: the largest reusable haul (UBX, expander rails, keyboard, encoder). Skip: about six new drivers, no touch, highest unit cost. | 5 / 6.5 = 0.8 |
| 5 | Heltec WiFi LoRa 32 V4.3 (R8) [S8] | ESP32-S3R8 [S8] | 16 MB / 8 MB [S8] | 0.96 in OLED, controller unnamed, SSD1306 assumed [S8] | 1.25-8-pin GNSS header, module optional, part unnamed [S8] | None; discrete LiPo charge path [S8] | 2x18 headers; 7 ADC, 3 UART, 2 I2C, 2 SPI [S8] | Profile row, SSD1306 configuration, ADC battery backend, module-dependent GNSS driver | Port: 16 MB and 8 MB PSRAM for 21.90 to 27.50 USD, pin-compatible with V3, under 20 uA [S8]. Skip: no gauge or PMIC, no receiver fitted, and the 128x64 tier duplicates T-Beam. | 2 / 2.8 = 0.7 |
| 6 | M5Stack Dial [S11] | ESP32-S3FN8 [S11] | 8 MB / none listed [S11] | 1.28 in 240x240 GC9A01, FT3267 touch, encoder; M5Unified `board_M5Dial` [S11] | None onboard; Unit GPS through PORT.A [S11] | None; GPIO46 HOLD latch plus ADC [S11] | PORT.A, PORT.B [S11] | Profile row, encoder class, GPIO46 power-latch backend | Port: the only round display plus encoder, on a library-known board. Skip: no PSRAM, so it fails gate 3, and no PMIC. | 2 / 2.3 = 0.9 |
| 7 | M5Stack Cardputer-Adv [S12] | ESP32-S3FN8 [S12] | 8 MB / none listed [S12] | 1.14 in 240x135 ST7789V2, known `board_M5CardputerADV` [S12] | None onboard; Unit GPS through HY2.0-4P [S12] | None; battery ADC [S12] | EXT 2.54-14P; I2C G8/G9, UART G13/G15, SPI G5/G14/G39/G40 [S12] | Profile row, keyboard matrix, ADC battery backend | Port: 56-key keyboard, IR emitter, SD and 1500 mAh [S12]. Skip: no PSRAM, no touch, no GPS. | 2 / 1.8 = 1.1 |
| 8 | Heltec WiFi LoRa 32 V3 [S7] | ESP32-S3FN8 [S7] | 8 MB SiP / no external PSRAM [S7] | 0.96 in 128x64 OLED [S7] | None [S7] | None; discrete charge plus power detect [S7] | 2x18 headers; 7 ADC, 3 UART, 2 I2C, 2 SPI [S7] | Profile row, OLED configuration, ADC battery backend | Port: cheapest S3 node. Skip: Heltec's own FAQ says no PSRAM [S7], and no GPS, so it fails gates 3 and 4. | 0 / 2.3 = 0.0 |
| 9 | LilyGO T-Display S3 [S1] | ESP32-S3R8 [S1] | 16 MB / 8 MB [S1] | 1.9 in ST7789V, touch on some SKUs (`Panel_ST7789`) [S1] | None [S1] | None named [S1] | Qwiic/STEMMA plus 1.25 2-pin, headers [S1] | Profile row and ADC battery backend only | Port: 9.04 USD for 16 MB and 8 MB PSRAM [S1]. Skip: zero new capability, since ST7789 already ships, and no PMIC or GPS. | 1 / 1.8 = 0.6 |
| 10 | Waveshare ESP32-S3-Touch-AMOLED-2.06 [S15] | ESP32-S3R8 [S15] | 8 MB / 32 MB [S15] | 2.06 in AMOLED, CO5300 assumed [S15] | None [S15] | AXP2101 assumed, unverified [S15] | Not read | AXP2101 adapter plus CO5300 configuration | Port: 32 MB PSRAM headroom. Skip: no GPS path, and no dedicated page was fetched. | 1 / 2.3 = 0.4 |
| 11 | LilyGO T-Display S3 AMOLED [S2] | ESP32-S3R8 [S2] | 16 MB / 8 MB [S2] | 1.91 in RM67162 AMOLED over QSPI; no M5GFX driver [S2][library: M] | None [S2] | "built-in battery management", no part named [S2] | Headers, Qwiic [S2] | A new QSPI AMOLED panel driver | Port: 1.91 in AMOLED for 33.98 USD [S2]. Skip: a new driver for a class CO5300 already covers, and no GPS. | 1 / 3.0 = 0.3 |
| 12 | LilyGO T4-S3 [S3] | ESP32-S3 [S3] | 16 MB / 8 MB [S3] | 2.41 in 450x600 AMOLED, QSPI, controller unnamed, no M5GFX driver [S3][library: M] | None [S3] | Not stated [S3] | Boot, reset, microSD [S3] | A new QSPI panel driver plus SD | Port: the largest AMOLED. Skip: 58.99 USD, no GPS, controller unconfirmed. | 1 / 3.0 = 0.3 |

Considered and not ranked:

- **LilyGO T-Echo** [S6]: nRF52840, 850 mAh, 1.54 in e-paper, L76K. Skipped for
  the reason [362](362-nrf-ble-only-port.md) gives at length: no nRF target.
  The vendor table's "2MB flash, 2MB RAM" also conflicts with the nRF52840's
  1 MB and 256 KB [inferred from S6].
- **Heltec Wireless Tracker V2** [S9]: 8 MB flash and no PSRAM, UC6580 on
  `$CFG`, 30.90 USD. Fails gate 3 and needs a new GNSS dialect.
- **Seeed XIAO ESP32S3** [S13]: 8 MB PSRAM and 8 MB flash, 11 GPIO, charge
  management only. With the Wio-SX1262 kit it adds LoRa and still no display or
  GNSS. Useful only as a headless bench node on the existing
  `esp32-s3-headless` environment, which proves nothing new.
- **LilyGO T-Beam v1.1**: the product page returned no content, so its flash,
  PMIC and receiver are unread. Superseded by the Supreme.

## 4. Verdict

1. **LilyGO T-Beam Supreme.** It buys the first non-M5 board and the first
   AXP2101 rail model outside `M5.Power` (ALDO4 for GNSS, ALDO3 for LoRa),
   plus the 1-bit 128x64 UI tier and the first `SX1262` sleep on a board whose
   radio furble must park. Take the L76K trim for the cheapest port; take the
   M10S trim only on 318's battery-floor argument, which costs the UBX driver.
2. **M5Stack CoreS3.** It buys the board-profile-only seam: M5Unified detects
   panel, touch and AXP2101, so the new code is input callbacks and a battery
   backend, plus a first `AW9523B` power-path user. It is the cheapest actual
   port and the weakest new-silicon claim, so it is the warm-up, not the plan.
3. **Waveshare ESP32-S3-Touch-AMOLED-1.75-G.** It buys the AMOLED tier (no
   backlight, brightness as a panel command) and the first onboard GNSS sharing
   a bus with touch, which is a real constraint the code does not model today.
   Its PMIC and panel classes overlap [360](360-waveshare-amoled-1-8-port.md):
   if 360 lands first, the marginal capability here is the LC76G only, and this
   board should drop behind the Pager.

Ranks 3 and 4 are 0.70 against 0.77, inside the tie-break window. The 1.75-G
wins it because it needs one driver with a document (`LC76G`) while the Pager
needs three parts with no page in `docs/components/` today.

**Skip list, explicitly.** I would skip, now: the T-Echo (nRF, and 362 already
decided against it), the Heltec V3 and Wireless Tracker V2 (no PSRAM), the XIAO
ESP32S3 and Wio-SX1262 kit (no display, no GNSS), the T-Display S3 (no new
capability), the T-Display S3 AMOLED and T4-S3 (a QSPI panel driver M5GFX does
not have, for a class CO5300 already covers), the Waveshare 2.06 (no GPS path),
the Dial and Cardputer Adv (no PSRAM), and the T-Beam v1.1 (unread page,
superseded). The Pager is a defer, not a no: it is the right second pocket board
once the UBX and XL9555 work exists.

## 5. What this does not change

- **318.** The T-Beam Supreme M10S stays first for the battery-floor goal, and
  the L76K trim stays the no-new-driver fallback. This document ranks port cost
  and does not reorder that decision. Its only new claim is that the L76K trim
  is the cheapest *port*, which is 318's own fallback for a different reason.
- **360.** The AMOLED-1.8 board keeps the first AMOLED port. The 1.75-G is
  ranked on onboard GNSS, not on a second AMOLED bring-up.
- **362.** No nRF board is proposed, and the T-Echo entry above restates 362's
  decision rather than revisiting it.

## 6. Task table

No task is committed by this plan. If a board is picked, its tasks come from
[322](implemented/322-hardware-abstraction.md)'s per-board gap table, not from
here.

| Step | Owner plan | State |
| --- | --- | --- |
| Pick the trim and confirm GPS pins on a unit | 318 | open, bench |
| AXP2101 `RegisterBus` adapter and rail model | 322 | open |
| 1-bit 128x64 UI tier | 322 | open |
| AMOLED display policy split (no backlight) | 360 | open |
| LC76G `$PAIR` driver | none yet | not planned |

## 7. Risks

- Schematic-only facts (T-Beam GPS pins, Pager part numbers) can be wrong on a
  revision. They are marked, not assumed.
- A panel class in M5GFX is not a working panel. Every "0.3 units" in the table
  is a configuration claim that a bench check can turn into a driver job.
- The ratio is sensitive to how a class is counted. The counts are printed next
  to each score so a reviewer can disagree with a number instead of the rank.
- Prices move and stock disappears; the Pager in particular comes and goes.

## 8. Sources

All URLs read on 2026-11-26. Vendor pages are the product or documentation page
named; they settle only what the table marks as verified.

- S1 LilyGO T-Display S3: https://www.lilygo.cc/products/t-display-s3
- S2 LilyGO T-Display S3 AMOLED:
  https://www.lilygo.cc/products/t-display-s3-amoled
- S3 LilyGO T4-S3: https://www.lilygo.cc/products/t4-s3
- S4 LilyGO T-Beam Supreme: https://www.lilygo.cc/products/t-beam-supreme
- S5 LilyGO T-LoRa Pager: https://www.lilygo.cc/products/t-lora-pager
- S6 LilyGO T-Echo: https://www.lilygo.cc/products/t-echo
- S7 Heltec WiFi LoRa 32 V3: https://heltec.org/project/wifi-lora-32-v3
- S8 Heltec WiFi LoRa 32 V4: https://heltec.org/project/wifi-lora-32-v4
- S9 Heltec Wireless Tracker V2:
  https://heltec.org/project/wireless-tracker-v2
- S10 M5Stack CoreS3: https://docs.m5stack.com/en/core/CoreS3
- S11 M5Stack Dial: https://docs.m5stack.com/en/core/Dial
- S12 M5Stack Cardputer-Adv: https://docs.m5stack.com/en/core/Cardputer-Adv
- S13 Seeed XIAO ESP32S3:
  https://www.seeedstudio.com/XIAO-ESP32S3-p-5627.html
- S14 Waveshare ESP32-S3-Touch-AMOLED-1.75 wiki:
  https://www.waveshare.com/wiki/ESP32-S3-Touch-AMOLED-1.75
- S15 Waveshare ESP32-S3-Touch-AMOLED-1.75 product page, including the
  comparison table with the 2.06 row:
  https://www.waveshare.com/esp32-s3-touch-amoled-1.75.htm

Repository citations, not URLs:

- L-A [plans/implemented/322-hardware-abstraction.md](implemented/322-hardware-abstraction.md),
  the Phase 1 audit and the per-board gap table at
  `plans/implemented/322-hardware-abstraction.md:99`. This is where the
  AXP2101, BQ27220, BQ25896, XL9555, SH1106 and ST7796 facts, the T-Beam GPS
  rail and pins, and the Pager part numbers come from. They are a schematic
  read in this repository, not product-page statements.
- L-B [docs/hardware-catalog.md](../docs/hardware-catalog.md), the device tables
  at `docs/hardware-catalog.md:136`. This is where the LC76G-on-I2C note and
  the Pager display and PMIC reading come from.

Library citation, not a URL and not a tracked file:

- M pinned M5GFX 0.2.27 sources under
  `.pio/libdeps/m5stick-s3/M5GFX/src/lgfx/v1/panel/`: `Panel_CO5300.hpp`,
  `Panel_AMOLED.hpp`, `Panel_SSD1306.hpp` (which holds `Panel_SH110x`),
  `Panel_ST7789.hpp`, `Panel_GC9A01.hpp`, `Panel_ILI9342.hpp`,
  `Panel_LCD.hpp`. The absence of an RM67162 or SH8601 driver in that directory
  is the absence of a file, not a vendor statement. The version is pinned at
  `platformio.ini:31`, and `.pio` is ignored by `.gitignore:1`.

## 9. UNVERIFIED

1. T-Beam Supreme display controller, PMIC and GPS UART pins. The product page
   lists MCU, flash and PSRAM only [S4]. AXP2101, SH1106 and RX 9 / TX 8 come
   from the schematic read in [L-A].
2. Pager part numbers. The page says "TI Power Management Monitoring Chipset"
   and "Battery Gauge" only [S5]; BQ25896, BQ27220 and XL9555 are
   schematic-level [L-A].
3. Pager panel controller. Only 2.33 in and 222x480 are published [S5];
   ST7796U is from [L-B], not the vendor page.
4. Heltec V3 and V4 OLED controller: SSD1306 against SSD1315 is not named
   [S7][S8].
5. Heltec V4 GNSS module part, and whether the 8-pin header carries UART, PPS
   and an enable line [S8].
6. Waveshare 1.75-G GPS bus. LC76G is confirmed onboard [S14], but I2C against
   UART, the R15/R16 rework and the reported touch corruption are repository
   findings [L-B], not vendor statements.
7. Whether M5GFX's generic `Panel_LCD` plus an upstream ST7796 init table
   actually drives the Pager panel. Bench check.
8. Whether `Panel_CO5300`, written for the T-Watch Ultra, is configuration-only
   at 466x466 and at 2.06 in. Bench check.
9. T-Display S3 charger IC and charge current; PMIC part numbers for the
   T-Display S3 AMOLED, T4-S3 and Waveshare 2.06 [S1][S2][S3][S15].
10. T4-S3 AMOLED controller: RM67162, SH8601 or another part [S3].
11. CoreS3 Unit GPS port and bus: Grove UART against I2C [S10].
12. Exposed GPIO counts per LilyGO SKU. The pages show headers, not pin counts
    [S1] to [S5].
13. BLE 5 central behaviour and throughput per board. That is an SoC
    capability, not a per-board vendor guarantee.

## Implementation state

Research only, no implementation. Nothing here is merged as a board, profile,
driver or support claim. The document exists so the ranking is reviewable and
does not have to be reconstructed from a transcript.
