# 41 - Alternative sidecar hardware

Status: hardware survey. No code in this document. Companion to
`plans/40-thinknode-port.md`.

## Goal

Find hardware that does the job plan 40 wants from the ThinkNode M4, without
the rewrite plan 40 priced. The job is a dedicated BLE geotagging sidecar for
Fujifilm cameras. It carries its own GNSS receiver, runs for days on a charge,
fits a pocket or a camera bag, charges over USB, and needs no interaction. A
display is optional because plan 50 gives it a companion app.

The hard constraint is the MCU. furble is ESP32 code end to end. `lib/furble`
is built on esp-nimble-cpp, settings are ESP-IDF NVS, GPS is the ESP-IDF UART
driver, and power control is `esp_pm`. An ESP32-family target keeps all of it.
Anything else repeats plan 40's verdict, which is a second firmware.

LoRa is irrelevant to this use case. It appears on almost every candidate
because the Meshtastic market is what funds these devices. Meshtastic support
is still worth having, because it means published pinouts, a board definition,
and a community that has already measured the power.

## Summary verdict

The Elecrow ThinkNode M5 is the recommendation. It is the device plan 40 said
did not exist: an ESP32-S3 with a built-in GNSS receiver, a 1200 mAh cell, a
sealed case, USB-C, buttons, and an e-paper screen, for 64,90 EUR at BerryBase.
Its GNSS chip is a Quectel L76K, which speaks the same `$PCAS` command set as
the AT6558 family, so the entire plans/14 and plans/32 investment carries over
untouched. The Meshtastic variant file publishes every pin including a GPS
power switch and a GPS standby line, which is exactly what plans/15 needs. The
port is a board profile, not a rewrite. The runner-up is the LilyGO T-Beam
Supreme in its L76K trim at 66,55 EUR, which trades the case and the pocket
form factor for a swappable 18650 and an AXP2101 PMIC, and is the right choice
if runtime beats tidiness.

## Comparison table

| Device | MCU | GNSS chip | Battery | Display | EU price and source | Port effort |
|---|---|---|---|---|---|---|
| Elecrow ThinkNode M5 | ESP32-S3-WROOM-1 | Quectel L76K, `$PCAS` | 1200 mAh internal, USB-C | 1.54" e-paper, SSD1681 | 64,90 EUR BerryBase | board profile |
| LilyGO T-Beam Supreme, L76K trim | ESP32-S3FN8, 8 MB PSRAM | Quectel L76K, `$PCAS` | 18650 holder, AXP2101 | 1.3" OLED, SH1106 | 66,55 EUR hamparts.shop | board profile |
| LilyGO T-Beam Supreme, u-blox trim | ESP32-S3FN8, 8 MB PSRAM | u-blox MAX-M10S, UBX | 18650 holder, AXP2101 | 1.3" OLED, SH1106 | 84,90 EUR BerryBase | board profile plus GNSS driver |
| Heltec Wireless Tracker V1.1 | ESP32-S3FN8 | Unicore UC6580, `$CFG` | none, JST 1.25 for your own cell | 0.96" TFT, ST7735 | about 25 to 35 EUR, OpenELAB DE, hexaspot | board profile plus GNSS driver |
| LilyGO T-Deck Plus | ESP32-S3 | u-blox class, chip not verified | 2000 mAh internal | 2.8" LCD plus keyboard | 94,90 EUR BerryBase | board profile |
| LilyGO T-LoRa Pager | ESP32-S3, 16 MB flash | u-blox MIA-M10Q, UBX | internal Li-ion, capacity not published | 2.33" IPS plus keyboard | 119,90 EUR BerryBase | board profile |
| Seeed XIAO ESP32S3 plus L76K GNSS module | ESP32-S3 | Quectel L76K, `$PCAS` | your own LiPo | none | about 10,90 USD module plus board | board profile, DIY, no case |
| M5Stack Core2 v1.1 plus GPS unit plus Battery Module 13.2 | ESP32 | AT6668, `$PCAS` | 1500 mAh module, stackable | 2.0" touch LCD | 49,90 plus 12,90 EUR BerryBase | none |
| M5StickC Plus2 or StickS3 plus GPS unit plus USB power bank | ESP32 or ESP32-S3 | AT6668, `$PCAS` | any USB bank | yes | already owned | none |
| Elecrow ThinkNode M4, plan 40 baseline | nRF52840 | multi-constellation, unnamed | 7000 mAh 18650 pack | none | 69,90 EUR BerryBase | rewrite |
| LilyGO T-Echo Plus | nRF52840 | Quectel L76K | 2400 mAh | 1.54" e-paper | 79,90 EUR BerryBase | rewrite |

Port effort scale. None means an existing PlatformIO env already covers it.
Small means an M5Unified board that M5Unified already knows. Board profile
means a new platform layer and a new env, with `lib/furble` untouched. Rewrite
means plan 40's second firmware.

## Per-device notes

### Elecrow ThinkNode M5

Plan 40 concluded that no ThinkNode device is both ESP32 and GNSS-equipped.
That was true of the M1 to M4 range. The M5 changes it. The Meshtastic hardware
page now lists seven models, and the M5 is ESP32-S3 with a built-in GNSS
receiver.

Verified specification. ESP32-S3, Xtensa LX7 dual core at up to 240 MHz, 4 MB
flash, 512 KB SRAM, 8 MB PSRAM per Elecrow. SX1262 LoRa. 1.54 inch e-paper,
200 x 200, SSD1681. 1200 mAh lithium cell. USB-C at 5 V 1 A. Controls are a
knob switch, a function button, a page turn button, a GPS switch and a reset
button. 82 x 51.6 x 26.3 mm, 81 g, closed ABS case. Elecrow quote about 34 uA
in low power mode. Price 53,90 USD direct, 64,90 EUR at BerryBase, where stock
shows as limited availability.

GNSS. The Elecrow page only says GPS, GLONASS, BeiDou and QZSS. The Meshtastic
variant names the part: `#define GPS_L76K`. The constellation list matches the
L76K exactly. The variant also gives the wiring that matters for power work:
`GPS_SWITH 10` for the module rail, `PIN_GPS_STANDBY 11`, `PIN_GPS_REINIT 13`,
and UART on `GPS_TX_PIN 20` and `GPS_RX_PIN 19`. Default baud for an L76K is
9600.

Why the L76K matters. Meshtastic probes the L76K with `$PCAS06,0*1B` and
configures it with `$PCAS04,7`, `$PCAS03,...` and `$PCAS11,3`. The source
comments that `$PCAS03` is "valid for L76K, ATGM336H (and likely other AT6558
devices)". The L76K is therefore a `$PCAS` receiver from furble's point of
view. Everything in plans/14 and plans/32 applies with no protocol work.

Power. Seeed publish 41 mA working and 360 uA standby for the L76K. That is a
third of what a dual-band receiver costs and it has a real standby mode, which
is what plans/15 duty cycling needs.

Flash. Meshtastic builds the M5 against `ESP32-S3-WROOM-1-N4`, so treat 4 MB as
the working assumption until a device is in hand. furble's current binaries are
about 1.0 MB, and the m5stick-c env already targets a 4 MB part, so 4 MB is not
a constraint. The 8 MB PSRAM claim from Elecrow conflicts with an N4 module and
should be checked on hardware before plans/31 PSRAM work is assumed to apply.

Display, better news than expected. Meshtastic drives the panel through GxEPD2
with `EINK_DISPLAY_MODEL=GxEPD2_154_D67`. That is the GDEW0154D67, and
LovyanGFX, which M5GFX is built on, ships `Panel_GDEW0154D67`. So the panel
furble's own graphics stack would need is already implemented upstream. It is
still e-paper, so refresh rates rule out the current LVGL menu animations. The
safe first build is headless plus plan 50, with a slow status screen later.

Open risks. A PCA9557 GPIO expander gates peripheral power and the LEDs, so the
board profile has an I2C dependency before anything else comes up. The e-paper
backlight enable also sits on that expander.

### LilyGO T-Beam Supreme

Verified specification. ESP32-S3FN8, 8 MB flash, 8 MB PSRAM. SX1262. 1.3 inch
SH1106 OLED, 128 x 64. 18650 holder with an AXP2101 PMU. QMI8658 IMU, QMC6310
magnetometer, BME280, PCF8563 RTC, microSD, Qwiic. Native USB, so no serial
bridge driver. Meshtastic officially supports it as `tbeam-s3-core`, with GNSS
UART on pins 8 and 9 and a GPS wakeup line on pin 7.

Two GNSS trims. The CORE module ships either with a u-blox MAX-M10S-00B or with
a Quectel L76K, and the module is swappable. BerryBase stock the u-blox trim at
84,90 EUR. hamparts.shop stock the L76K trim at 66,55 EUR including tax, in
stock with 2 to 3 day shipping. TinyTronics list the L76K trim in the 868 MHz
band as well but block automated access, so treat their price as unverified.

Buy the L76K trim. The u-blox part is more accurate and a little cheaper on
power, but it speaks UBX, which means a second GNSS driver in furble and no
reuse of plans/14 or plans/32. The L76K trim is a `$PCAS` receiver and reuses
all of it.

Strengths. The 18650 is the real argument. A 3400 mAh Panasonic NCR18650GA is
5,90 EUR at BerryBase, and a spare cell in the bag beats any fixed battery.
The AXP2101 gives proper rail control for the GNSS and the radio.

Weaknesses. It is a bare development board. No case, an SMA antenna sticking
out, roughly 100 x 33 mm of PCB plus the cell. It is a bag device, not a pocket
device, and it is the least tidy thing on this list to clip to a camera strap.

### Heltec Wireless Tracker

Verified specification. ESP32-S3FN8, SX1262, Unicore UC6580 GNSS, 0.96 inch
160 x 80 ST7735 TFT on V1.1, USB-C, SH1.25-2 battery connector with charge
management, 65.48 x 28.06 x 13.52 mm, 22,90 USD. V2 exists with 28 dBm TX, LDS
antennas and a smaller board. Heltec are also beta testing an nRF52840 version
of V2, which tells you where they think the power problem is.

GNSS. The UC6580 is dual band, L1 plus L2 and L5, and it is configured with
Unicore's own `$CFGSYS` and `$CFGMSG` sentences at 115200 baud, not `$PCAS`.
That is a separate driver in furble, and none of plans/14 or plans/32 carries
over. Community measurements put dual band acquisition draw well above a single
band L76K, and vendor figures are absent.

Power data. Heltec's own Meshtastic low power guide claims 13 uA in low power
mode and about 1 percent of a 1000 mAh cell per hour once GPS is acquired, so
roughly four days. That is the best published duty cycled figure for any ESP32
device here, and it is worth reading before designing furble's own sidecar duty
cycle.

Practical problems. No battery, no case, and no reliable German source. The
board is not stocked at BerryBase. OpenELAB DE listed it with zero stock,
hexaspot list V2. Order friction is the main reason this is not the runner-up.
As a pure DIY option it is the cheapest way to an ESP32-S3 with GNSS and a cell
of any size you like.

### LilyGO T-Deck Plus and T-LoRa Pager

Both are ESP32-S3 handhelds with GNSS, a real battery, a keyboard and a colour
LCD, sold at BerryBase for 94,90 EUR and 119,90 EUR. The T-LoRa Pager uses a
u-blox MIA-M10Q at 38400 baud and has a BQ25896 charger with a BQ27220 fuel
gauge, which is the nicest power subsystem in this survey. The T-Deck Plus has
a 2000 mAh cell.

They are the wrong shape for this job. A sidecar that runs for days should not
carry a backlit LCD, a keyboard and a speaker. Both are twice the price of the
M5 for hardware whose main features this use case switches off. If furble ever
wants a full handheld remote with a map, revisit the T-Deck Plus. For a
geotagging sidecar, skip both.

### Seeed XIAO ESP32S3 plus L76K GNSS module

The XIAO ESP32S3 plus Seeed's L76K GNSS module for XIAO, 10,90 USD, gives the
same `$PCAS` receiver as the M5 on a 21 x 18 mm stack with a LiPo pad on the
board. It is the cheapest route and the most flexible on battery size.

It is also a naked board with no case, no button worth pressing, an unshielded
antenna choice, and no published pinout beyond Seeed's own examples. Meshtastic
do have a `seeed_xiao_s3` variant. Treat this as the fallback if the M5 turns
out to have a defect that cannot be worked around, not as a first choice. The
whole point of the exercise is a finished object.

### Already supported M5 hardware, the zero port option

This deserves an honest hearing because the effort is zero and the user already
owns the parts.

M5Stack Core2 v1.1 is 49,90 EUR at BerryBase and is already a furble env. The
Battery Module 13.2 is 1500 mAh at 3.7 V, stacks under the Core, and multiple
modules can be stacked, at 12,90 EUR each. The GPS/BDS Unit v1.1 with its
AT6668 is the reference receiver for the whole GPS plan line. That is a working
sidecar today with 1500 mAh, or 3000 mAh with two modules, and every plan in
the roadmap applies to it by definition.

The cheaper variant of the same idea is a StickC Plus2 or StickS3 with the GPS
unit hanging off Port A and a USB-C power bank in the same pocket. A 10000 mAh
bank makes the runtime question disappear entirely.

Why it is still not the answer. A Core2 with a module stack and a Grove cable
to a separate GPS puck is three objects and a cable. It is not pocketable, the
cable is the weak point on a camera strap, and the M5Stack Module GPS v2.1 is
explicitly unsupported in the code today (`src/FurbleGPS.cpp:23`). This is the
option that works this weekend, not the option that ends the project.

### Non-ESP32 devices, for completeness

The LilyGO T-Echo Plus is an nRF52840 with an L76K, a 2400 mAh cell and a
1.54 inch e-paper, at 79,90 EUR from BerryBase. If the plan 40 rewrite ever
happens it is a better shaped device than the M4 for this use case, with three
times the M4's per-cell efficiency advantage already spent on a smaller case.
It does not change plan 40's verdict. It is still a rewrite.

Heltec's nRF52840 Wireless Tracker V2 and the Heltec T114 are in the same
bucket. Note them, do not plan around them.

## How each fits the roadmap

### Plans that apply unchanged on any ESP32-S3 candidate

- `07-ble-sleep.md`. Modem sleep and light sleep while connected are `esp_pm`
  and NimBLE behaviour. Nothing in it is M5 specific. On a board without a
  32.768 kHz crystal the same caveat from `00-hardware-experiments.md`
  experiment A applies, so that experiment must be repeated per board.
- `27-usb-console.md`. The console is the only sane way to work a headless
  device. It becomes more important, not less. On the ThinkNode M5 the USB
  path is the same web-flasher serial path Meshtastic uses.
- `50-companion-app-design.md`. On a headless or e-paper device this stops
  being a nice extra and becomes the setup and pairing UI.
- `06-power-module.md`, `08` to `11`, `13`. All portable, all ESP-IDF and
  NimBLE level.

### Plans that apply only if the GNSS is a `$PCAS` receiver

- `14-gps-pcas.md` and `32-gps-advanced.md`. Both are `$PCAS` command work
  against the AT6558 family. The L76K answers the same commands, including the
  `$PCAS06` probe that plan 32a uses for auto detection. So the ThinkNode M5,
  the T-Beam Supreme L76K trim and the XIAO plus L76K inherit both plans with
  at most a device string added to the detection table.
- `15-gps-power.md`. This gets easier, not harder, on the ThinkNode M5. The
  M5Stack unit has no backup supply and no standby pin, which is why plan 32
  worries about 23 second cold starts. The M5's L76K has a dedicated standby
  line at GPIO 11 and a rail switch at GPIO 10, both published in the variant
  file, and 360 uA standby keeps the ephemeris warm.
- A u-blox or UC6580 board voids both. Plan 14, plan 32 and plan 15's `$PCAS12`
  work would all need a second driver written from the vendor's own protocol.
  That is the single biggest reason to prefer L76K trims.

### What a board profile pull request contains

The M5 dependency in furble is narrower than it looks. Only five files touch
M5 at all: `src/FurblePlatform.cpp`, `include/FurblePlatform.h`,
`src/FurbleUI.cpp`, `src/FurbleGPS.cpp` and `src/FurbleCalibrate.cpp`. All of
`lib/furble` is clean, which is the opposite of plan 40's finding for nRF,
where `lib/furble` was the problem.

`src/FurblePlatform.cpp` is 102 lines and is the template. It already isolates
exactly the things a new board needs to supply: `M5.begin` and its config, the
board identity switch, the PMIC button handling, `setSleep` over `esp_pm`, and
`powerOff`. A board profile provides a second implementation of that same
`Furble::Platform` interface behind a build flag, with no M5Unified and no
M5PM1. Roughly:

1. `platformio.ini`. New env, `board = esp32-s3-devkitc-1` or the vendor board,
   a `-DFURBLE_THINKNODE_M5` style flag, and the same espidf framework and
   partition table. The existing envs are the pattern.
2. `src/FurblePlatform.cpp`. A non-M5 branch. Init the I2C expander, power the
   peripheral rail, read the battery ADC on GPIO 8 with the 2.0 multiplier the
   variant publishes, map the two buttons on GPIO 21 and 14, and keep
   `setSleep` and `tick` exactly as they are.
3. `src/FurbleGPS.cpp`. The only change is where pins come from. Line 23 to 25
   currently calls `M5.getPin(m5::port_a_pin1)`. A board profile supplies
   constants instead, 19 and 20 on the ThinkNode M5. Everything below that,
   the ESP-IDF UART driver, the pattern detect interrupt and TinyGPS++, is
   unchanged.
4. Battery and buttons for the UI. `src/FurbleUI.cpp` uses a short list of M5
   calls: `M5.Display` brightness, width and height, `M5.Touch`, the four
   button objects and `M5.Power.getBatteryLevel` and `getBatteryCurrent`. On a
   headless build most of them compile out. On a screen build they need a
   thin shim.
5. Display. Headless first. LVGL keeps running with no display driver
   registered, or the UI task is not started at all and plan 50 does the
   talking. An e-paper flush callback is a separate later pull request.

Scale estimate. A headless ThinkNode M5 profile is a few hundred lines of new
code plus a build env. Compare plan 40's estimate for the M4: 3000 to 4000 new
lines, a BLE abstraction refactor across every file in `lib/furble`, and two to
three months to something trustworthy. That is the whole argument of this
document in one comparison.

## Recommendation

Buy an Elecrow ThinkNode M5 from BerryBase at 64,90 EUR and write a headless
board profile.

Reasoning, weighed against the alternatives.

Against the ThinkNode M4 from plan 40. The M4 has 5.8 times the energy and an
MCU whose sleep floor is measured in microamps. It also costs a second
firmware, a BLE abstraction refactor, and a GNSS chip Elecrow refuse to name.
The M5 costs a board profile and keeps every line of protocol code, every
setting, the whole UI stack if wanted, and both GPS plans. Runtime is the M5's
weak point and it is a real one, so be honest about the arithmetic. An L76K
tracking continuously draws 41 mA. On its own that empties 1200 mAh in about 29
hours. Add an ESP32-S3 holding a BLE connection and continuous operation is
roughly a day. The device only becomes a days-long sidecar through plans/07 and
plans/15, GNSS standby between fixes, and a long connection interval when
nothing is being shot. The hardware is built for exactly that: 360 uA GNSS
standby, a rail switch, 34 uA claimed device standby, and a PMU. Heltec's
measured 1 percent per hour on a 1000 mAh cell says four days is achievable on
this class of hardware with those techniques. If the measurements come in worse
than that, the fallback is not a rewrite, it is a bigger cell, which is the
runner-up.

Against the T-Beam Supreme. The Supreme wins on energy and loses on
everything else. A 3400 mAh 18650 with a spare in the bag is unarguable, and
the AXP2101 is a better PMIC than anything else here. But it is a bare board
with an SMA antenna, it needs a case built for it, and in the u-blox trim
BerryBase sells it costs 84,90 EUR and voids two GPS plans. Buy the L76K trim
from hamparts.shop at 66,55 EUR if runtime turns out to matter more than form.
Both devices share the same board profile work, and the same L76K driver path,
so this is a decision that can be deferred until after the profile exists.

Against the already supported M5Stack hardware. A Core2 with a 1500 mAh battery
module and the Grove GPS unit works today at zero engineering cost, and it
should be the thing that is actually used while the profile is written. It is
not the end state because it is three objects, a cable and no case.

Against everything else. The Heltec Wireless Tracker is the cheapest path to an
ESP32-S3 with GNSS, but its UC6580 costs a second GNSS driver and there is no
reliable German source. The T-Deck Plus and the T-LoRa Pager are handhelds
priced at 95 and 120 EUR whose defining features this application disables. The
XIAO stack is a breadboard, not a device.

Suggested order of work. First, buy the M5 and confirm the two facts that are
not verifiable from a desk: that the GNSS really is an L76K answering `$PCAS06`
on GPIO 19 and 20, and what the module and the flash actually are, since
Meshtastic's `ESP32-S3-WROOM-1-N4` and Elecrow's 8 MB PSRAM claim cannot both
be right. Second, land the headless board profile with `27-usb-console.md` as
the only interface. Third, measure. Then decide whether the 1200 mAh cell is
enough, or whether the same profile should be pointed at a T-Beam Supreme with
an 18650 in it.

## References

Every link below was fetched and read while writing this document.

Elecrow ThinkNode M5

- Elecrow product page, specifications and price:
  https://www.elecrow.com/thinknode-m5-meshtastic-lora-signal-transceiver-esp32-s3-1-54-screen-gps-function.html
- Elecrow wiki:
  https://www.elecrow.com/wiki/ThinkNode_M5_Meshtastic_LoRa_Signal_Transceiver_ESP32-S3.html
- Meshtastic ThinkNode series overview, model by model MCU and GNSS:
  https://meshtastic.org/docs/hardware/devices/elecrow/thinknode/
- Meshtastic variant, pinout including `GPS_L76K`, `GPS_SWITH`,
  `PIN_GPS_STANDBY`:
  https://raw.githubusercontent.com/meshtastic/firmware/master/variants/esp32s3/ELECROW-ThinkNode-M5/variant.h
- Meshtastic build env, board type and e-paper model:
  https://raw.githubusercontent.com/meshtastic/firmware/master/variants/esp32s3/ELECROW-ThinkNode-M5/platformio.ini
- BerryBase, 64,90 EUR, limited availability:
  https://www.berrybase.de/elecrow-thinknode-m5-meshtastic-lora-kommunikationsgeraet-esp32-s3-sx1262-gps-1-54-e-paper-5v

GNSS protocol evidence

- Meshtastic GPS driver, `$PCAS` use for L76K and ATGM336H, `$PCAS06` probe,
  `$CFGSYS` and `$CFGMSG` for UC6580:
  https://raw.githubusercontent.com/meshtastic/firmware/master/src/gps/GPS.cpp
- Seeed L76K wiki, 41 mA working, 360 uA standby, 9600 default baud:
  https://wiki.seeedstudio.com/get_start_l76k_gnss/
- Seeed L76K module product page, 10,90 USD:
  https://www.seeedstudio.com/L76K-GNSS-Module-for-Seeed-Studio-XIAO-p-5864.html

LilyGO

- Meshtastic T-Beam device page, variants and GNSS options:
  https://meshtastic.org/docs/hardware/devices/lilygo/tbeam/
- Meshtastic `tbeam-s3-core` variant, GPS pins:
  https://raw.githubusercontent.com/meshtastic/firmware/master/variants/esp32s3/tbeam-s3-core/variant.h
- Meshtastic `t-beam-1w` variant, L76K pins, for comparison:
  https://raw.githubusercontent.com/meshtastic/firmware/master/variants/esp32s3/t-beam-1w/variant.h
- Meshtastic `tlora-pager` variant, MIA-M10Q at 38400, BQ25896 and BQ27220:
  https://raw.githubusercontent.com/meshtastic/firmware/master/variants/esp32s3/tlora-pager/variant.h
- BerryBase T-Beam Supreme, u-blox MAX-M10S trim, 84,90 EUR:
  https://www.berrybase.de/lilygo-ublox-t-beam-supreme-esp32-s3-lora-868mhz-meshtastic-gnss-oled-500ma-5v
- hamparts.shop T-Beam Supreme, L76K trim, 66,55 EUR incl. tax, in stock:
  https://hamparts.shop/t-beam-supreme-lilygo.html
- BerryBase T-LoRa Pager, 119,90 EUR:
  https://www.berrybase.de/lilygo-t-lora-pager-meshtastic-esp32-s3-gnss-nfc-sensor-audio-lora-868-mhz-space-gray
- BerryBase T-Echo Plus, nRF52840 with L76K and 2400 mAh, 79,90 EUR:
  https://www.berrybase.de/lilygo-t-echo-plus-lora-868mhz-nrf52840-l76k-gnss-bewegungssensor-e-paper-1-54-zoll-2400mah
- espboards.dev T-Beam Supreme pinout and peripheral list:
  https://www.espboards.dev/esp32/lilygo-t-beam-supreme/

Graphics stack

- LovyanGFX panel directory, showing `Panel_GDEW0154D67` for the ThinkNode M5's
  e-paper:
  https://github.com/lovyan03/LovyanGFX/tree/master/src/lgfx/v1/panel

Heltec

- Wireless Tracker V1.1 product page, UC6580, ST7735, 22,90 USD:
  https://heltec.org/project/wireless-tracker/
- Wireless Tracker V2 product page, 30,90 USD:
  https://heltec.org/project/wireless-tracker-v2/
- Heltec Meshtastic low power guide, 13 uA sleep, 1 percent per hour:
  https://docs.heltec.org/en/node/esp32/wireless_tracker/meshtastic_tracker.html
- Meshtastic `heltec_wireless_tracker` variant, `GPS_UC6580`, 115200 baud,
  `VEXT_ENABLE`:
  https://raw.githubusercontent.com/meshtastic/firmware/master/variants/esp32s3/heltec_wireless_tracker/variant.h

M5Stack, the zero port path

- Battery Module 13.2, 1500 mAh at 3.7 V, stackable:
  https://docs.m5stack.com/en/module/battery13.2
- BerryBase M5Stack listings checked for price and stock: Core2 v1.1 49,90 EUR,
  Battery Module 1500 mAh 12,90 EUR, M5GO Battery Bottom2 for Core2 16,60 EUR,
  GPS Module v2.1 with AT6668 17,90 EUR, 18650 cells from 4,90 EUR. Search
  entry point: https://www.berrybase.de/search?search=m5stack+gps

Plan 40 baseline

- BerryBase ThinkNode M4, 69,90 EUR:
  https://www.berrybase.de/elecrow-thinknode-m4-powerbank-fuer-meshtastic-nrf52840-lora-868-915-mhz-gps-bluetooth-7000-mah

Sources consulted but not usable. TinyTronics list the T-Beam Supreme L76K in
868 MHz but return HTTP 403 to automated access, so no price is quoted from
them. OpenELAB DE list the Heltec Wireless Tracker but showed zero stock.
