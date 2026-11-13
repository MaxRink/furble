# 318 - Next board: battery life and GPS

Status: research and decision. This plan adds no board, firmware target,
driver or support claim. No hardware was bought or measured for it. Sources
were read on 2026-11-13 against master `e66e8807`.

Tags on every external claim: [vendor] a vendor page, data sheet or vendor
schematic statement; [schematic] a net or part read from the vendor
schematic PDF; [measured: who] a third-party measurement; [anecdote: where]
a user report; [firmware] read in another project's source; [inferred] my
conclusion, not stated by any source. Source IDs resolve in the Sources
section.

## Motivation

The two pain points of the current remote are battery life and a GPS that
only locks outdoors. Plan 287 showed that neither is fixed by a different
MCU. They are fixed by the board around it: a 32.768 kHz crystal for the BLE
sleep clock, a GNSS backup supply, a better antenna and a better receiver
class. This plan picks the next board with those two goals first.

The owner accepts a new GPS driver when it brings better receivers or
features. That changes the ranking in the research report, which avoided a
new driver at almost any cost.

## Finding 1: the 32.768 kHz crystal sets the battery floor

Espressif's NimBLE `power_save` example gives the BLE-connected light sleep
floor with the main XTAL versus a 32.768 kHz crystal as the sleep clock
[vendor: E1]:

| SoC | Main XTAL | 32.768 kHz crystal |
| :--- | ---: | ---: |
| ESP32-S3 | 3.3 mA | 230 uA |
| ESP32-C6 | 3.3 mA | 34 uA |
| ESP32-C3 | 2.3 mA | 140 uA |

The original ESP32 cannot light-sleep BLE on the main XTAL at all [vendor:
E1]. The internal RC cannot be used for connections [vendor: E2].

The StickS3 has no crystal, so it sits on the 3.3 mA floor
([00-hardware-experiments.md](00-hardware-experiments.md), experiment A).
Schematic reads found a crystal on the ESP32 XTAL_32K pins only on these
boards [schematic]: LilyGO T-Beam Supreme [L13], T-Watch Ultra and T-LoRa
Pager [L17]; Heltec Wireless Tracker V1.1 and V2 [H14, H19], Vision Master
E213 and T190, and Wireless Paper [H25, H30, H32].

No M5Stack board and no Waveshare board that was checked has one. On M5
boards the only 32 kHz crystal belongs to the RTC chip [schematic: M2, M4,
M6, M10, M14]. The T-Display S3, T-Deck, T-Deck Plus, T-Deck Pro, T-Watch S3
and S3 Plus, and every XIAO have none either [schematic: L1, L6, L10, L17;
vendor: H40].

No measurement of BLE-connected light sleep on a crystal board was found. The
230 uA figure is a vendor example figure, not a furble measurement. It is the
first bench item below.

## Finding 2: GPS is backup supply, antenna and receiver class

- Backup supply. The current GPS/BDS Unit v1.1 (AT6668) has no backup rail,
  so a rail cut costs a cold start of about 108 s
  ([00-hardware-experiments.md](00-hardware-experiments.md), experiment B).
  A receiver with V_BCKP held up keeps ephemeris and time and hot-starts in
  about 1 s [vendor: U1].
- Antenna. An SMA or U.FL port lets an active antenna sit away from the
  battery and the 2.4 GHz antenna. The T-Beam Supreme has SMA [vendor: L14].
  The Unit GPS SMA U190 gives the StickS3 the same option with no port
  [vendor: M44].
- Receiver class. u-blox M10 tracks at -167 dBm against -162 dBm for the
  AT6668 and L76K, with the same -148 dBm cold start [vendor: U1;
  `docs/hardware-catalog.md` receiver table]. Better tracking holds a fix
  under trees and at a window. It does not create an indoor cold fix
  [inferred, as plan 287 already concluded].

Board GNSS facts that matter here:

| Board | Receiver, class | Backup | Antenna | Tag |
| :--- | :--- | :--- | :--- | :--- |
| T-Beam Supreme L76K | L76K, `$PCAS` | V_BCKP from AXP2101 VRTC; survives a rail cut while the 18650 is in | SMA | [schematic: L13] |
| T-Beam Supreme M10S | MAX-M10S, UBX | same board and VRTC net as the L76K trim | SMA | [vendor: L14, L26]; same net [inferred] |
| T-Watch Ultra | MIA-M10Q, UBX | V_BCKP from AXP2101 VRTC | internal | [schematic: L17] |
| T-LoRa Pager | MIA-M10Q, UBX, 38400 baud, GPS_EN via XL9555 | VRTC backup | internal | [schematic: L17] |
| T-Deck Plus | L76K or MIA-M10Q | V_BCKP on a VRTC net, source untraced | internal | [schematic: L6; vendor: L26] |
| Cardputer Adv + Cap LoRa-1262 | AT6668, `$PCAS` | not found | Cap | [vendor: M46] |

## Scoring

Each need scored 0 to 3: BLE central, GPS (class reuse, backup, antenna),
display, input, battery and telemetry, sleep floor (3 when a 32 kHz crystal
or a Nordic-class floor exists), IR possible (Grove or free GPIO; onboard IR
not verified), pocketability. From the research report [inferred scoring]:

| Board | BLE | GPS | Disp | Input | Batt | Sleep | IR | Pocket | Total /24 | Effort |
| :--- | --: | --: | --: | --: | --: | --: | --: | --: | --: | :--- |
| T-LoRa Pager | 3 | 3 | 2 | 3 | 3 | 3 | 1 | 2 | 20 | L |
| T-Watch Ultra | 3 | 3 | 3 | 2 | 2 | 3 | 1 | 2 | 19 | L |
| Cardputer Adv + Cap AT6668 | 3 | 2 | 2 | 3 | 1 | 1 | 2 | 3 | 17 | S/M |
| Nesso N1 | 3 | 1 | 2 | 2 | 3 | 1 | 2 | 3 | 17 | M |
| T-Beam Supreme L76K | 3 | 3 | 1 | 0 | 3 | 3 | 2 | 1 | 16 | M |
| T-Deck Plus L76K | 3 | 2 | 3 | 3 | 1 | 1 | 2 | 1 | 16 | M |
| StopWatch | 3 | 1 | 3 | 2 | 2 | 1 | 1 | 3 | 16 | M |
| Wio Tracker L1 Pro (Nordic) | 3 | 2 | 1 | 2 | 1 | 3 | 1 | 2 | 15 | M-L |
| CoreS3 + Unit GPS | 3 | 2 | 2 | 2 | 2 | 1 | 2 | 1 | 15 | S |
| Makerfabs Nomad | 3 | 3 | 3 | 1 | 3 | 1 | 1 | 0 | 15 | M |
| Waveshare 1.75-G | 3 | 2 | 3 | 1 | 2 | 1 | 1 | 2 | 15 | L |
| XIAO nRF52840 + L76K (Nordic) | 3 | 2 | 0 | 0 | 1 | 3 | 2 | 3 | 14 | M |
| Core2 v1.3 + Unit GPS SMA | 2 | 2 | 2 | 2 | 2 | 0 | 2 | 1 | 13 | S |
| Heltec Tracker V2 | 3 | 1 | 1 | 0 | 1 | 3 | 2 | 2 | 13 | L |
| T-Echo Plus (Nordic) | 3 | 2 | 1 | 1 | 1 | 2 | 0 | 2 | 12 | M-L |
| ThinkNode M5 | 3 | 1 | 2 | 2 | 1 | 1 | 0 | 2 | 12 | L |
| XIAO S3 + L76K (headless) | 3 | 2 | 0 | 0 | 0 | 1 | 2 | 3 | 11 | S |

Effort: S is an M5Unified board plus `$PCAS` GPS; M is a LovyanGFX panel plus
a new board, power or input profile; L is a new GNSS driver, an e-paper UI
tier or P4 hosted BLE. The T-Beam Supreme M10S scores like the L76K trim with
a GPS score of 3 and effort L. Raw score is not the ranking. The crystal, the
GPS backup and the effort weigh more than a perfect form factor.

## Decision

1. **LilyGO T-Beam Supreme M10S first (50.04 USD plus an 18650)** [vendor:
   L26]. It has every hardware lever at once: the 32 kHz crystal on XTAL_32K
   [schematic: L13], an 18650 holder, the AXP2101 with battery percentage,
   the MAX-M10S with V_BCKP backup, and SMA for an active antenna. It is a
   bench board: one-button OLED input and no case. It is the board that
   proves the 230 uA floor and the M10 driver before either goes into a
   pocket device.
2. **T-Watch Ultra (90.34 USD) or T-LoRa Pager (87.35 USD) as the pocket
   form factor** [vendor: L26]. Both have the crystal and an M10 receiver with
   VRTC backup [schematic: L17]. The Ultra has a 2.06 inch CO5300 AMOLED and
   touch. The Pager has a keyboard, a rotary encoder and a BQ27220 fuel
   gauge [schematic: L17]. Both reuse the UBX driver from step 1. Open risks: the Pager cell
   size is unsettled (1200 or 1500 mAh; the research report saw both
   figures in listings it did not cite) [inferred], a reviewer reports BLE
   drops when its screen sleeps [anecdote: L20], and one Ultra user reports a
   USB reconnect loop after third-party firmware [anecdote: L28].
3. **T-Beam Supreme L76K (37.12 USD) as the no-new-driver fallback**
   [vendor: L26]. Same board, `$PCAS` receiver, so plans 14, 15, 19 and 32
   apply unchanged. Choose it only if the UBX driver stalls.

Known T-Beam risks: one unit measured 0.1 mA off and another 13.2 mA, with a
leak suspected [measured: user, L15]. The Meshtastic web flasher once picked
a wrong build so the GPS was not seen [anecdote: L16].

Rejected for this goal: the Cardputer Adv with Cap LoRa-1262 is the cheapest
upgrade (S/M effort, AT6668 `$PCAS`) but has no crystal, so it keeps the 3.3
mA floor [schematic: M14]. The zero-port GPS fix for existing StickS3 users
remains the Unit GPS SMA U190 with an active antenna [vendor: M44].

## UBX driver design

Today `src/FurbleGPS.cpp` mixes NMEA handling, the duty-cycle state machine
and CASIC configuration (`$PCAS` text and CASIC binary through
`sendBinary()`). The CASIC wire format already lives in the portable core as
`lib/furble/protocol/GpsCasic` and `GpsNavx.h`, covered by the `gps-casic`
host test.

Target shape:

- One receiver-agnostic GPS interface in `src/`: probe, configure rate,
  sentences, constellations and dynamic model, enter and leave standby,
  send aiding (time and position), poll health. The duty cycle, motion,
  aid cache and UI use only this interface.
- Shared NMEA. TinyGPSPlus and the GSV/GSA satellite parser stay common to
  every receiver. UBX receivers keep NMEA output, so fix handling does not
  change.
- One configuration backend per vendor:
  - CASIC (`$PCAS` and CASIC binary): today's AT6668, L76K, ATGM336H.
  - UBX: u-blox M10 next (MAX-M10S, MIA-M10Q), using the CFG-VALSET key
    interface of SPG 5.10 [vendor: U2].
  - Airoha `$PAIR` (AG3352, AG3335, LC76G) and Unicore `$CFG` (UC6580) later,
    only when a target board needs them.
- Each backend's wire format goes in `lib/furble/protocol/` as pure code
  (framing, checksum, key encoding, ACK/NAK decode), listed in
  `tools/portable_core_manifest.txt`. It is host-tested like `GpsCasic`.
  The UBX frame parser also gets a fuzz target; `GpsCasic` itself has none
  yet, so the CASIC parser should join it in the same change.
- Probe order: try the configured backend, then the others. A receiver that
  answers no backend stays NMEA-only, which is today's degraded mode.

M10 features to use, all from the MAX-M10S data sheet [vendor: U1] and the
SPG 5.10 interface description [vendor: U2]:

| Feature | What it gives | Source |
| :--- | :--- | :--- |
| Power save mode (`CFG-PM`, on/off or cyclic tracking) | Tracking current at VCC drops from 9.5 mA continuous to 5 mA cyclic (3.0 V, default constellations). BeiDou B1C is not supported in this mode. | [vendor: U1 table 15, U2 section 4.9.15] |
| AssistNow Autonomous (`CFG-ANA`) | Receiver-predicted orbits with no network. TTFF 4 s with the default constellations, 3 s with GPS and Galileo. | [vendor: U1, U2 section 4.9.1] |
| Four concurrent constellations | GPS, GLONASS, Galileo and BeiDou. The default is GPS, Galileo and BeiDou B1I. All four needs BeiDou B1C, because B1I cannot run with GLONASS. | [vendor: U1 section 1.3, footnote 13] |
| Dynamic model (`CFG-NAVSPG-DYNMODEL`) | PORT (portable), PED (pedestrian) and WRIST (wrist-worn) match a camera remote better than the default. | [vendor: U2] |
| BBR configuration save | The BBR layer has the highest priority and is retained by the backup supply, so a rail cut does not lose the configuration while V_BCKP is up. | [vendor: U2 configuration layers]; retained by V_BCKP [inferred from U1 backup modes] |
| Warm start via backup | Hardware backup mode costs 28 uA at V_BCKP = 3.3 V and keeps the receiver able to hot-start in 1 s. | [vendor: U1 table 17 and TTFF table] |

The u-blox "25 mW in continuous tracking mode" is a vendor claim [vendor:
U1]. The same data sheet's current tables give about 21 mW (GPS only, 1.8 V)
to about 35 mW (default constellations, 3.0 V, VCC plus V_IO) [inferred from
U1 tables 15 and 16]. Use the bench figure, not the headline.

Effort: 2 to 4 weeks for the interface split, the UBX backend, host tests
and the fuzz target [inferred, from the research report].

## Bench test plan

Run each on the T-Beam Supreme M10S unless stated. Record board, firmware
commit and meter in the result.

1. BLE-connected floor with the crystal. Build with
   `CONFIG_BT_CTRL_LPCLK_SEL_EXT_32K_XTAL`, confirm the init log names the
   32 kHz crystal (as in experiment A), connect a Fujifilm body, screen off,
   GPS off. Measure the battery current. Pass target: well under the 3.3 mA
   StickS3 floor; the vendor figure is 230 uA [vendor: E1].
2. M10 current, continuous versus power save. Same link, GPS on, 1 Hz. Measure
   continuous tracking, then cyclic tracking via `CFG-PM`. Compare to 9.5 mA
   and 5 mA at VCC [vendor: U1].
3. Warm start after a rail cut. Get a fix, cut the GPS main rail with the
   18650 in place so V_BCKP stays up, restore it, time the fix. Repeat with
   the cell removed. Compare to the 108 s StickS3 cold start.
4. The T-Beam off-current outlier. Measure the board in the AXP2101 off state
   on our unit and compare to the 0.1 mA and 13.2 mA reports [measured:
   user, L15].
5. LoRa radio held asleep. furble does not use the SX1262. Measure the board
   with the radio left as reset leaves it, then with it put to sleep at boot,
   and keep the lower state [inferred: an unconfigured radio may idle above
   its sleep current].

## Nordic track

A separate product, not the next remote. The proof board is a Seeed XIAO
nRF52840 with the L76K XIAO module (10.90 USD [vendor: N22]), about 21 USD
in total [inferred]. h2zero's n-able Arduino
core runs NimBLE host and controller on nRF52840 with no SoftDevice under
NimBLE-Arduino, which has the same C++ API as furble's `esp-nimble-cpp`
[firmware: N53]. It could show whether `lib/furble` builds there with no
facade. The pocket product would be the Seeed Wio Tracker L1 Pro (47.90 USD,
L76K, 2000 mAh, joystick and OLED) [vendor: N27]. The XIAO nRF54LM20A is the
forward silicon (512 KB RAM, nPM1300 gauge) but needs the full Zephyr facade
of plan 127 [vendor: N44]. See plans 127 and 287.

## Open items

- Bench items 1 to 5 above.
- Trace what feeds VRTC on the T-Deck Plus.
- Confirm the Pager cell size.
- Confirm on our T-Beam M10S unit that V_BCKP comes from the AXP2101 VRTC as
  on the L76K trim.

## Sources (accessed 2026-11-13)

- E1 https://github.com/espressif/esp-idf/blob/master/examples/bluetooth/nimble/power_save/README.md
- E2 https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/low-power-mode/low-power-mode-ble.html
- U1 u-blox MAX-M10S data sheet: https://content.u-blox.com/sites/default/files/MAX-M10S_DataSheet_UBX-20035208.pdf
- U2 u-blox M10 SPG 5.10 interface description: https://content.u-blox.com/sites/default/files/u-blox-M10-SPG-5.10_InterfaceDescription_UBX-21035062.pdf
- M2 https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1207/K150_Stick_S3_PRJ_V0.6_20251111_2025_11_17_16_10_24.pdf
- M4 https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/512/Sch_M5StickC_Plus2_v0.5.pdf
- M6 https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/486/Sch_Core2_v1.1_2023-07-20.pdf
- M10 https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/490/Sch_M5_CoreS3_v1.0.pdf
- M14 https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1178/Sch_M5CardputerAdv_v1.0_2025_06_20_17_19_58.pdf
- M44 https://docs.m5stack.com/en/unit/Unit-GPS%20SMA
- M46 https://docs.m5stack.com/en/cap/Cap_LoRa-1262
- L1 https://github.com/Xinyuan-LilyGO/T-Display-S3/blob/HEAD/schematic/T_Display_S3.pdf
- L6 https://github.com/Xinyuan-LilyGO/T-Deck/tree/HEAD/schematic
- L10 https://github.com/Xinyuan-LilyGO/T-Deck-Pro
- L13 https://github.com/Xinyuan-LilyGO/LilyGo-LoRa-Series/tree/HEAD/schematic/T-Beam-S3-Supreme
- L14 https://wiki.lilygo.cc/products/t-beam-series/t-beam-supreme/
- L15 https://github.com/Xinyuan-LilyGO/LilyGo-LoRa-Series/issues/301
- L16 https://github.com/meshtastic/firmware/issues/10487
- L17 https://github.com/Xinyuan-LilyGO/LilyGoLib/tree/master/schematic
- L20 https://adrelien.com/lilygo-t-lora-pager-review-packed-with-features-you-cant-use-yet/
- L26 LilyGO product variant prices: https://lilygo.cc/products/t-beam-supreme.json ,
  https://lilygo.cc/products/t-watch-ultra.json ,
  https://lilygo.cc/products/t-lora-pager.json and
  https://lilygo.cc/products/t-deck-plus-1.json (re-read 2026-11-13)
- L28 https://www.cnx-software.com/2026/04/20/lilygo-t-watch-ultra-an-ip65-rated-esp32-s3-smartwatch-with-2-01-inch-amoled-lora-and-gnss/
- H14 https://resource.heltec.cn/download/Wireless_Tracker/Wireless_Tacker1.1/HTIT-Tracker_V0.5.pdf
- H19 https://resource.heltec.cn/download/Wireless_Tracker_V2/Schematic/HTIT-Tracker-FEM_V2.3.pdf
- H25 https://resource.heltec.cn/download/HT-VME213/HT-VME213_Schematic_Diagram_V1.0.pdf
- H30 https://resource.heltec.cn/download/HT-VMT190/HT-VMT90%20Schematic%20Diagram.pdf
- H32 https://resource.heltec.cn/download/Wireless_Paper/Wireless_Paper_V0.4_Schematic_Diagram.pdf
- H40 https://wiki.seeedstudio.com/xiao_esp32s3_getting_started/
- N22 https://www.seeedstudio.com/L76K-GNSS-Module-for-Seeed-Studio-XIAO-p-5864.html
- N27 https://www.seeedstudio.com/Wio-Tracker-L1-Pro-p-6454.html
- N44 https://www.cnx-software.com/2026/06/30/xiao-nrf54lm20a-sense-board-enables-ultra-low-power-bluetooth-6-0-matter-thread-zigbee-or-2-4-ghz-proprietary-applications/
- N53 https://github.com/h2zero/n-able-Arduino and https://github.com/h2zero/NimBLE-Arduino

## Implementation state

Research and decision only. No code, board profile or purchase yet.
