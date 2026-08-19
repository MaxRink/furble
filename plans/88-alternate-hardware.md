# 88 - Alternate hardware for a furble remote, and Meshtastic coexistence

Status: hardware survey. No code in this document. Companion to
`plans/40-thinknode-port.md` and `plans/41-alternative-hardware.md`.

## Goal

Plans 40 and 41 asked one question: what hardware makes the best headless GPS
geotagging sidecar. This document asks a different one. What alternate hardware
makes a good furble remote control with a screen and richer inputs, and can such
a device keep running Meshtastic as well.

The framing matters. Plans 40 and 41 wanted a device with no interaction and no
screen. This survey wants the opposite: a device the user reads and drives, with
buttons, a keyboard, a trackball or a touchscreen. Almost every candidate is a
Meshtastic handheld, because that is the market funding screens on ESP32 LoRa
devices. So the second question follows naturally: if the user already carries a
Meshtastic device, can furble live on it too.

## Scope and the hard constraint

The MCU constraint from plan 41 still holds. furble is ESP32 code end to end.
`lib/furble` is built on esp-nimble-cpp, `Camera` inherits
`NimBLEClientCallbacks` and owns a `NimBLEClient` (`lib/furble/Camera.h:20,168`),
settings are ESP-IDF NVS, GPS is the ESP-IDF UART driver, and power control is
`esp_pm`. The build is `framework = espidf` (`platformio.ini:14`), not Arduino.
An ESP32-family target keeps all of that behind a board profile. An nRF52 target
is plan 40's second firmware. So nRF52 handhelds such as the ThinkNode M1 are
noted for completeness only.

furble's BLE role is central. It scans for and connects out to a camera. This is
the key fact for the coexistence section, because Meshtastic's BLE role is
peripheral: it advertises and waits for a phone to connect in.

## Candidate table

Inputs, touch, display, radios, and the sidecar peripherals for each candidate.

| Device | MCU | Inputs | Touchscreen | Display | LoRa | GNSS | IMU | SD | Battery | Flash / PSRAM |
|---|---|---|---|---|---|---|---|---|---|---|
| LilyGO T-Deck | ESP32-S3 | I2C QWERTY keyboard, trackball, one button | No | 2.8" IPS 320x240, ST7789 | SX1262 | No | No | microSD | JST Li-ion, add your own | 16 MB / 8 MB |
| LilyGO T-Deck Plus | ESP32-S3 | I2C QWERTY keyboard, trackball, one button | Yes, GT911 capacitive | 2.8" IPS 320x240, ST7789 | SX1262 | u-blox MIA-M10Q, UBX | No | microSD | 2000 mAh | 16 MB / 8 MB |
| Elecrow ThinkNode M9 | ESP32-S3R8 | 37-key QWERTY keyboard | No | 2.4" TN 240x320, color | LR1110 | multi-GNSS + compass | not stated | not stated | 2300 mAh | 16 MB / 8 MB |
| Elecrow ThinkNode M2 | ESP32-S3 | buttons | No | 1.3" OLED mono 128x64, SH1106 | SX1262 | No | No | No | 1000 mAh | 4 MB (assume) |
| Elecrow ThinkNode M5 | ESP32-S3 | knob, function button, page button, GPS switch | No | 1.54" e-paper 200x200, SSD1681 | SX1262 | Quectel L76K, $PCAS | No | No | 1200 mAh | 4 MB (N4) |
| LilyGO T-Watch S3 | ESP32-S3 | one button, touch | Yes, capacitive (IC unverified) | 1.54" 240x240 LCD (AMOLED on Plus) | SX1262 | No (add-on only) | Yes | No | ~300 to 500 mAh | 16 MB / 8 MB |
| M5Stack Cardputer | ESP32-S3 | 56-key QWERTY keyboard | No | 1.14" IPS 240x135, ST7789V | No | No | Adv only, BMI270 | No (Grove) | 1400 mAh (Adv 1750) | 8 MB |
| M5Stack Core2 / CoreS3 | ESP32 / ESP32-S3 | 3 areas, touch | Yes, capacitive (FT6336) | 2.0"/2.1" 320x240 | No | via Grove unit | Yes | microSD | 390 to 500 mAh + modules | 16 MB |
| Elecrow ThinkNode M1 | nRF52840 | rotary knob, one button | No | 1.54" e-paper, backlit | SX1262 | multi-GNSS | accel | No | 1200 mAh | nRF52, N/A here |

Port effort scale, same as plan 41. Board profile means a new platform layer and
a new env with `lib/furble` untouched. Rewrite means plan 40's second firmware.
Every ESP32-S3 row above is a board profile. The M1 is a rewrite. The Core2 and
CoreS3 are already furble-adjacent M5Unified boards and are the lowest effort of
all.

## Exhaustive input inventory

This is the canonical input catalog for every device studied across plans 40, 41
and 88. It enumerates every input method on each board, not a summary. Plans 40
and 41 point here rather than duplicating it.

Two facts are true of every device below, so they are stated once instead of
filling a column. First, all ESP32 and nRF52 candidates expose a USB serial
console, so scripted input over USB is universal. On furble that surface is the
plan 27 USB console. Second, none of these devices ship a physical D-pad,
thumb-joystick, or a bank of dedicated capacitive touch keys. The only capacitive
touch keys in the set are the three programmable touch zones on the M5Stack Core2
and CoreS3, and those are regions of the main touch panel, not separate keys.

Legend. MT means multitouch. "IMU input" lists the onboard motion sensor and
whether it can serve as a tap, shake or tilt input, which depends on firmware
enabling it. "None stated" means the vendor does not document the part.

| Device | MCU | Push-buttons (count, placement) | QWERTY keyboard | Trackball | Rotary / encoder | Touchscreen (IC, type, MT) | IMU input | Microphone | Source |
|---|---|---|---|---|---|---|---|---|---|
| Elecrow ThinkNode M1 | nRF52840 | 1 function/power button, front | No | No | Yes, rotary dial (brightness + navigation) | No (e-paper) | Accelerometer, tap/tilt capable | No | espboards, Elecrow M1 review |
| Elecrow ThinkNode M2 | ESP32-S3 | Function button + reset/boot; exact count unverified | No | No | No | No (1.3" OLED) | None stated | No | Elecrow M2 page |
| Elecrow ThinkNode M5 | ESP32-S3 | 4: function, page-turn, GPS switch, reset | No | No | Yes, knob switch | No (1.54" e-paper SSD1681) | None stated | No | plan 41, Elecrow M5 |
| Elecrow ThinkNode M9 | ESP32-S3R8 | Power/reset + 37-key keyboard block | Yes, 37 keys | No | No | No (2.4" TN, non-touch) | Magnetometer/compass; no 6-axis stated | No (buzzer only) | CNX, Elecrow M9 |
| LilyGO T-Deck | ESP32-S3 | 1 trackball centre-click + reset | Yes, ~35-key I2C (ESP32-C3 controller) | Yes, 5-way with click | No | No (trackball nav) | None | Yes, mic | LilyGO wiki, espboards |
| LilyGO T-Deck Plus | ESP32-S3 | 1 trackball centre-click + reset | Yes, ~35-key I2C (ESP32-C3 controller) | Yes, 5-way with click | No | Yes, GT911 capacitive, MT | None | Yes, mic array | LilyGO wiki, Meshtastic disc 5606 |
| LilyGO T-Watch S3 | ESP32-S3 | 1 side button | No | No | No | Yes, capacitive (CST/FT-class, verify), single/MT | BMA423 accel, activity/tap engine | Yes, PDM mic | OpenELAB, igeekphone |
| LilyGO T-Beam Supreme | ESP32-S3 | 3: Power (left), Reset (right), Boot/IO0 | No | No | No | No (1.3" SH1106 OLED) | QMI8658 6-axis + QMC6310 mag, tap/tilt capable | No | Meshtastic T-Beam buttons, espboards |
| M5Stack Cardputer | ESP32-S3 | 56-key keyboard + StampS3 G0 button | Yes, 56 keys | No | No | No (1.14" ST7789V) | Adv only: BMI270, tap capable | Yes, PDM mic (IR out too) | CNX, Make: |
| M5Stack Core2 | ESP32 | Physical power + RST; 3 programmable touch zones | No | No | No | Yes, FT6336U capacitive, MT | MPU6886 (BMI270 on v1.3), tap/tilt | Yes, SPM1423 PDM | M5 docs Core2 |
| M5Stack CoreS3 | ESP32-S3 | Power via AXP2101; programmable touch zones; no physical nav buttons | No | No | No | Yes, FT6336U capacitive, MT | BMI270 + BMM150 | Yes, dual mic (ES7210); GC0308 camera + LTR-553 proximity | M5 docs CoreS3 |
| Heltec Wireless Tracker (V1.1/V2) | ESP32-S3 | 2: PRG (user) + RST | No | No | No | No (0.96" TFT, output only) | None onboard | No | Heltec, espboards |
| Heltec WiFi LoRa 32 V3 | ESP32-S3 | 2: PRG (user) + RST | No | No | No | No (0.96" OLED, output only) | None onboard | No | Heltec, espboards |
| RAK WisBlock (RAK19007 + RAK4631) | nRF52840 | 1 reset; nav button not built in, add on AIN1 (pin 31) | No | No | No | No (optional OLED module, output only) | Optional WisBlock sensor module | Optional module | RAK datasheet, Meshtastic peripherals, Rokland |

Notes on the harder-to-verify rows:

- T-Deck keyboard key count. LilyGO does not publish an exact number. The
  physical layout is roughly 35 keys on a secondary ESP32-C3 exposed over I2C.
  Treat the count as approximate until a device is in hand.
- T-Watch S3 touch controller. The vendor pages confirm capacitive touch but do
  not consistently name the controller. The classic T-Watch used an FT6336, and
  the S3 generation is reported as a CST816-class part. Verify on hardware before
  a board profile.
- ThinkNode M2 buttons. The Elecrow listing shows a function button and the usual
  reset and boot, but does not give a definitive nav-button count. Confirm on
  hardware.
- RAK and the M1 are nRF52840, not ESP32. They are plan 40 rewrite targets and
  are in this table only so the input catalog is complete.

## Per-device feedback and reviews

### LilyGO T-Deck and T-Deck Plus

The T-Deck is the reference handheld for this shape: a BlackBerry-style ESP32-S3
with a keyboard, a trackball and a 2.8 inch screen. A secondary ESP32-C3 runs the
keyboard and presents it over I2C, and the trackball doubles as the DFU trigger.
The standard T-Deck has no touchscreen and navigates by trackball. The T-Deck
Plus adds a GT911 capacitive touch panel, a u-blox MIA-M10Q GPS, and a 2000 mAh
cell. Both use the ST7789 at 320x240.
(https://wiki.lilygo.cc/products/t-deck-series/t-deck-plus/,
https://meshtastic.org/docs/hardware/devices/lilygo/tdeck/,
https://www.espboards.dev/esp32/lilygo-t-deck/)

Feedback is positive with real caveats. A DEF CON 33 field writeup and an Open
Source Society Malta review both call it a genuinely standalone Meshtastic
terminal once running, with better keyboard build quality than earlier LilyGO
boards and an ALT+B keyboard backlight toggle. Both also flag boot loops after a
flat battery or after flashing with the wrong tool, no IP rating, an inaccurate
battery percentage because there is no fuel gauge, and firmware that still feels
like a beta. The Malta reviewer's verdict for long-term field use was to prefer a
simpler board paired to a phone.
(https://blog.shellntel.com/p/def-con-33-and-meshtastic-on-the-lilygo-t-deck-plus,
https://ossmalta.eu/lilygo-t-deck-plus-review-a-meshtastic-handheld-with-great-potential-and-quirky-flaws/)

The most cited hardware complaint is the touchscreen. A Meshtastic firmware
discussion documents the T-Deck Plus panel being unresponsive or laggy out of the
box, fixed by flashing a touch-calibration binary before the application
firmware, with the caveat that erasing the flash removes the fix and it must be
redone.
(https://github.com/meshtastic/firmware/discussions/5606)

### Elecrow ThinkNode M9

The M9 is the newest and best-shaped keyboard handheld here, launched August
2026. It is an ESP32-S3R8 with 16 MB flash and 8 MB PSRAM, an LR1110 radio, a
2.4 inch 240x320 color TN LCD, a 37-key physical QWERTY keyboard, multi-GNSS with
a magnetic compass, an RTC and buzzer, and a 2300 mAh battery, in a phone-like
126 x 67 x 10 mm case for about $74.90.
(https://www.cnx-software.com/2026/08/10/elecrow-thinknode-m9-a-standalone-esp32-s3-meshcore-communication-terminal-with-color-lcd-qwerty-keyboard/,
https://liliputing.com/elecrow-thinknode-m9-is-a-phone-like-mesh-communicator-with-a-2-4-inch-sccreen-qwerty-keyboard-and-lora/,
https://www.elecrow.com/thinknode-m9-meshcore-communication-terminal-with-full-keyboard-2-4inch-lcd-esp32-s3-lr1110-gps-2300mah.html)

Two cautions. It ships with MeshCore, not Meshtastic, so the Meshtastic variant
file that plan 41 relied on for published pinouts may not exist yet. And the
display is a TN panel with 200 cd/m2 peak brightness, which is dim and has narrow
viewing angles compared to the IPS panels furble targets today. It is very new,
so there is little independent long-term feedback.

### Elecrow ThinkNode M2

The M2 is the plain ESP32-S3 Meshtastic node: an SX1262, a 1.3 inch mono OLED, a
1000 mAh cell, buttons only, no GNSS, no touch. It ships with Meshtastic and can
also run MeshCore. It is a good bring-up target because it is cheap and simple,
but its mono OLED and lack of GNSS make it a weak furble remote and no kind of
sidecar.
(https://www.elecrow.com/thinknode-m2-meshtastic-lora-signal-transceiver-powered-by-esp32-s3-with-1-3-oled-display.html,
https://www.espboards.dev/blog/thinknode-m1-m2-meshtastic-review/)

### Elecrow ThinkNode M5

Covered in full in plan 41 as the recommended sidecar. Here it earns a second
mention as a remote, but its e-paper panel rules out live UI. Refresh rates on
the SSD1681 make the current LVGL menu animations impossible, so on the M5 the
screen is a slow status display and the phone does the driving. See plan 41 for
the full specification and the L76K GNSS reuse argument.

### LilyGO T-Watch S3

The T-Watch S3 is a wrist-worn ESP32-S3 with a 1.54 inch 240x240 capacitive touch
LCD, an SX1262 LoRa radio, BLE, an IMU and an RTC. The Plus variant uses an
ESP32-S3-WROOM-1U with 16 MB flash and 8 MB PSRAM and an AMOLED panel. It is
open and hackable, which reviewers praise, but the recurring complaint across the
LilyGO S3 family is thin documentation and fragile library support, where getting
the vendor demo to build requires exact library versions.
(https://openelab.io/products/lilygo-t-watch-s3,
https://www.igeekphone.com/lilygo-t-watch-s3-plus-smartwatch-review-at-73-79-a-developers-dream-with-esp32-s3-power/,
https://www.andibond.com/lilygo-t-watch-s3-plus-esp32-s3-review/)

For furble the watch form factor is intriguing as a wrist shutter, but the tiny
battery makes it the shortest-runtime option here, and its only inputs are one
button and touch, which is the sparsest input set of any screened candidate.

### M5Stack Cardputer

The Cardputer is a $30 card-sized ESP32-S3 with a 56-key QWERTY keyboard and a
1.14 inch 240x135 ST7789V TFT, built on a removable StampS3 module, with a mic,
speaker, a 1400 mAh cell and a Grove port. It has no GNSS and no LoRa. The newer
Cardputer-Adv adds a BMI270 IMU, a 1750 mAh cell and better audio, and it reduced
the key actuation force from 260 gf to 160 gf.
(https://www.cnx-software.com/2023/10/14/m5stack-cardputer-a-30-card-sized-esp32-s3-computer-with-display-and-keyboard/,
https://www.cnx-software.com/2025/10/23/m5stack-cardputer-adv-esp32-s3-computer-gains-improved-antenna-larger-1750-mah-battery-es8311-audio-codec/,
https://makezine.com/article/technology/microcontrollers/review-m5stack-cardputer-adv-version-esp32-s3/)

Reviews split on the original keyboard. Raspberry Pi's magazine called the screen
small and the buttons hard to press; other reviewers found the keys pleasant. The
Adv revision addresses the actuation-force complaint. For furble the Cardputer is
attractive because it is an M5Stack device, so M5Unified is likely to know it,
which pushes it toward the lower end of the board-profile effort. It is not a
Meshtastic device, so it does not raise the coexistence question at all.
(https://magazine.raspberrypi.com/articles/m5stack-card-computer-review)

### M5Stack Core2 and CoreS3, the near-zero option

These are already M5Unified boards at furble's 320x240 touch resolution. The
Core2 is already close to a furble env. They have no LoRa and no built-in GNSS,
so they are not Meshtastic devices, but as a touch remote they are the least work
and the safest hardware. If the point is a bigger, nicer furble remote and not a
mesh device, a CoreS3 is the obvious pick.

### Elecrow ThinkNode M1, non-ESP32, for completeness

The M1 is an nRF52840 with an SX1262, a backlit 1.54 inch e-paper display with a
rotary brightness dial, multi-GNSS, and a 1200 mAh cell, with a very low sleep
current near 5.6 uA. Reviewers like it as a low-power, sunlight-readable field
node. It is not ESP32, so it is plan 40's rewrite, not a board profile. Note it
here, do not plan around it.
(https://www.elecrow.com/sharepj/elecrow-m1-meshtastic-review-e-paper-display-awesomeness-914.html,
https://www.espboards.dev/blog/thinknode-m1-m2-meshtastic-review/,
https://openelab.io/blogs/learn/thinknode-m1-vs-m5-which-elecrow-meshtastic-device-makes-more-sense)

## Meshtastic coexistence

The question: can one device run both furble and Meshtastic. The honest answer
has three layers, and only the first is easy.

### Layer 1: RF coexistence is a non-issue

LoRa and BLE do not compete. LoRa lives on a separate transceiver chip, the
SX1262 or LR1110, on SPI, in the sub-GHz band around 868 or 915 MHz. BLE uses the
ESP32's own 2.4 GHz radio. They are different silicon on different bands, so both
can be active at once. This is exactly how every Meshtastic node already works: a
phone connects over BLE while the node relays over LoRa. The one real ESP32
coexistence contention is BLE plus WiFi, which share the single 2.4 GHz radio.
LoRa plus BLE avoids it.
(https://heltec.org/project/wifi-lora-32-v3/,
https://en.wikipedia.org/wiki/Meshtastic,
https://www.seeedstudio.com/blog/2026/05/26/esp32-lora-guide/)

### Layer 2: two full firmwares, not one running image

Meshtastic is a complete firmware. furble is a complete firmware. There is no
mechanism to run two independent firmware images at the same time on one ESP32.
Meshtastic's own extension model is its compile-time module system, where a
feature is a module compiled into the single mesh binary and run as a FreeRTOS
task. So "furble as a Meshtastic module" would mean merging furble's sources into
the Meshtastic tree as a module.
(https://deepwiki.com/meshtastic/firmware,
https://deepwiki.com/meshtastic/firmware/11.1-esp32-platform-and-variants)

Three hard obstacles make that combined single build a large project, not a tidy
one:

- Framework mismatch. furble builds on bare ESP-IDF (`platformio.ini:14`).
  Meshtastic's ESP32 support is built on the Arduino-ESP32 core layered over
  ESP-IDF. The two do not share a build. Merging means porting furble's platform
  and UI onto Arduino-ESP32, or porting a Meshtastic module out of it, either of
  which is substantial.
- BLE role conflict. furble is a BLE central that connects out to a camera
  (`lib/furble/Camera.h:20`). Meshtastic is a BLE peripheral that advertises to a
  phone. NimBLE on ESP32 can hold both roles at once, but two codebases each
  assuming they own the NimBLE stack, its callbacks and its bonding store is the
  kind of integration that breaks in subtle ways. furble's camera pairing needs
  bonding, and so does Meshtastic's phone link.
- UI ownership. Both firmwares own the whole screen, the input devices and the
  main loop. Only one can. A combined build has to pick one UI host and make the
  other a guest, which the module model does not naturally provide for a UI as
  large as furble's.

### Layer 3: the practical answer is two apps, switched, on a big-flash device

The realistic way to "keep Meshtastic too" is not one binary. It is two separate
apps flashed into two partitions and switched, the same shape as an A/B OTA
scheme. Meshtastic already ships a small unified OTA bootloader for exactly this
kind of partition juggling.
(https://github.com/meshtastic/esp32-unified-ota,
https://meshtastic.org/docs/getting-started/flashing-firmware/esp32/)

The gate is flash. A dual-slot OTA layout gives roughly 1.25 to 1.9 MB per app
slot on a 4 MB part, and the Meshtastic ESP32 app is already near the 4 MB
ceiling, with sister projects reporting they had to drop 4 MB support as firmware
grew. furble's own binaries are about 1.0 MB (plan 41). Two full apps plus the
bootloader, NVS and a filesystem do not fit in 4 MB. They fit comfortably in
16 MB.
(https://github.com/hoylabs/OpenDTU-OnBattery/issues/1025,
https://www.esp32.com/viewtopic.php?t=20647)

So coexistence is a device-selection decision:

- 16 MB flash, 8 MB PSRAM devices can hold both: the T-Deck and T-Deck Plus, the
  ThinkNode M9, the T-Watch S3 Plus. On these, "keep Meshtastic" means flash
  Meshtastic into one slot and a furble board-profile build into another, and
  switch with a button gesture at boot. Each app runs alone when selected. This
  is the only shape worth pursuing.
- 4 MB devices cannot hold both: the ThinkNode M2 and M5. On these it is furble
  or Meshtastic, reflashed over USB to change.

### Coexistence verdict

Keep the two firmwares separate and switch between them on a big-flash device.
Do not attempt a combined single binary. RF is not the obstacle and never was.
The obstacles are the espidf-versus-Arduino framework split, the central-versus-
peripheral BLE role split, and single ownership of the UI and main loop. A
partition-switch on a 16 MB ESP32-S3 sidesteps all three and delivers the actual
user goal, which is one device in the bag that can be either a mesh node or a
camera remote. The furble side of that is just the board profile this document
otherwise argues for. The only furble-specific work coexistence adds is a
partition table and a boot-time selector, which is adjacent to plan 34
(OTA partitions).

## Implications for furble

### UI resolution matrix

furble today supports three geometries: 80x160 on the M5StickC, 135x240 on the
StickC Plus, Plus2 and StickS3, and 320x240 on the Core, Core2 and CoreS3. The
screened candidates add new points to that matrix:

- 320x240 is already supported. The T-Deck, T-Deck Plus and Core2 or CoreS3 land
  on the existing 320x240 layout with no new geometry work. This is a real
  advantage: the T-Deck reuses furble's largest existing layout as-is.
- 240x320 portrait is new. The ThinkNode M9 is 240x320, the same pixel count
  rotated. furble's layouts assume landscape. A portrait target needs the layout
  code audited for hardcoded width and height assumptions, which the narrow-panel
  work in fork PR #117 (80x160 fit) has already started exercising.
- 240x240 square is new. The T-Watch S3 is 240x240. Square is a geometry furble
  has never drawn for and would need layout review.
- 240x135 is a rotation of the existing 135x240. The Cardputer is 240x135, the
  StickC Plus geometry in landscape. Close to supported, needs a rotation pass.
- 200x200 e-paper and mono OLED are out of scope for live UI. The M5, M2 and M1
  displays cannot run the animated LVGL menus and belong to the headless plus
  companion-app path from plans 40, 41 and 50.

### Input abstraction is the real work

Today furble's input model is the M5 button set plus optional touch, gated
throughout `src/FurbleUI.cpp` by `M5.Touch.isEnabled()`, which selects between a
touch pointer device and an LVGL encoder or button navigation group. The touch
branch registers an `lv_indev` pointer (`src/FurbleUI.cpp:504`); the non-touch
branch drives an encoder group. Every candidate here brings an input type furble
does not model:

- Full QWERTY keyboard (T-Deck, T-Deck Plus, M9, Cardputer). LVGL has a keypad
  indev type, but furble has no keyboard navigation model. Mapping a keyboard
  onto menu navigation, or using it for text entry such as naming a saved camera,
  is new UI work.
- Trackball (T-Deck, T-Deck Plus). A trackball reads naturally as an LVGL encoder
  or as relative pointer motion. The encoder path already exists for non-touch
  boards, so a trackball is the cheapest new input to adopt.
- GT911 and other capacitive touch (T-Deck Plus, T-Watch S3). furble already has
  a touch path, but it assumes the M5Unified touch API. A board profile must feed
  the panel's own touch controller, GT911 on the T-Deck Plus, into the same
  `touchRead` callback (`src/FurbleUI.cpp:443`).
- Rotary knob (ThinkNode M5, M1). A rotary encoder is the classic LVGL encoder
  input and maps directly onto the existing non-touch navigation group.

The clean shape is to widen furble's input layer to an explicit set of input
kinds, buttons, encoder or trackball, keypad, and touch, decoupled from
`M5.Touch.isEnabled()`. That is the same seam the sidecar and companion-app plans
also want, and it is worth doing once.

### Tie-in with the sim per-model button work

Fork PR #118, "Model per-model physical buttons in the SDL sim"
(https://github.com/MaxRink/furble/pull/118), is directly relevant. The sim today
always attaches a mouse-driven touch device, so `M5.Touch.isEnabled()` is true
regardless of the modeled board, which makes the non-touch button branches
unreachable in the sim by default. PR #118 starts modeling each board's real
physical button set. Extending that same per-model input description to cover the
new input kinds above, a trackball, a keyboard, a rotary knob, and non-touch
versus touch panels, is exactly how a T-Deck or M9 profile would be developed and
regression-tested without owning the hardware. The input abstraction and the sim
input model should be designed together: the sim's per-model input description is
the natural place to declare what inputs a board profile exposes.

### Power profiles

Every candidate is a battery device and needs the plan 06 to 15 power stack.
Screens dominate draw here far more than on the sidecar. A 2.8 inch backlit IPS
on a T-Deck, or an AMOLED on a T-Watch S3, is a much larger power line than the
StickS3's panel, so the plan 12 display-off and blind-remote work matters more,
not less. The e-paper M5 is the opposite: its panel costs nothing to hold, which
is why plan 41 recommends it for the sidecar and this document does not recommend
it as a live remote.

### Recommendation

If the goal is a nicer furble remote and nothing else, buy a CoreS3 or a Core2.
They are M5Unified boards at furble's existing 320x240 touch geometry, the least
work and the most reliable, with no coexistence question to answer.

If the goal is one device that is both a Meshtastic handheld and a furble remote,
the LilyGO T-Deck is the pick, in either trim. It has 16 MB flash for the
two-app partition switch, it lands on furble's existing 320x240 layout, its
trackball maps onto the encoder navigation furble already has, and it is the most
reviewed and best-understood device in this class, with published Meshtastic
pinouts. Take the caveats seriously: no IP rating, an out-of-box touch quirk on
the Plus, and boot loops after a flat battery. Prefer the plain T-Deck if the
touch panel is not wanted, since its trackball is the lower-risk input and it
still runs Meshtastic. The ThinkNode M9 is the more elegant object, with a real
keyboard and a phone-like case, but it is brand new, ships MeshCore rather than
Meshtastic, has a dim TN panel, and would land furble on an unfamiliar 240x320
portrait geometry. Revisit it once it has a Meshtastic variant file and some
field history.

Do not build a combined furble-plus-Meshtastic binary. The partition-switch on a
16 MB device gives the same user outcome for a fraction of the risk.

## References

Every link below was fetched or searched while writing this document, in August
2026.

Elecrow ThinkNode M1, M2, M9

- ThinkNode series, Meshtastic hardware overview:
  https://meshtastic.org/docs/hardware/devices/elecrow/thinknode/
- ThinkNode M1 vs M2 review, espboards:
  https://www.espboards.dev/blog/thinknode-m1-m2-meshtastic-review/
- ThinkNode M1 e-paper review, Elecrow:
  https://www.elecrow.com/sharepj/elecrow-m1-meshtastic-review-e-paper-display-awesomeness-914.html
- ThinkNode M1 vs M5, MCU comparison, OpenELAB:
  https://openelab.io/blogs/learn/thinknode-m1-vs-m5-which-elecrow-meshtastic-device-makes-more-sense
- ThinkNode M2 product page, ESP32-S3, 1.3" OLED:
  https://www.elecrow.com/thinknode-m2-meshtastic-lora-signal-transceiver-powered-by-esp32-s3-with-1-3-oled-display.html
- ThinkNode M9, ESP32-S3, QWERTY, GNSS, CNX Software:
  https://www.cnx-software.com/2026/08/10/elecrow-thinknode-m9-a-standalone-esp32-s3-meshcore-communication-terminal-with-color-lcd-qwerty-keyboard/
- ThinkNode M9, phone-like mesh communicator, Liliputing:
  https://liliputing.com/elecrow-thinknode-m9-is-a-phone-like-mesh-communicator-with-a-2-4-inch-sccreen-qwerty-keyboard-and-lora/
- ThinkNode M9 product page, LR1110, 2.4" LCD, 2300 mAh:
  https://www.elecrow.com/thinknode-m9-meshcore-communication-terminal-with-full-keyboard-2-4inch-lcd-esp32-s3-lr1110-gps-2300mah.html

LilyGO T-Deck, T-Deck Plus, T-Watch S3

- T-Deck Plus documentation, ST7789, GT911, keyboard, MIA-M10Q GPS:
  https://wiki.lilygo.cc/products/t-deck-series/t-deck-plus/
- T-Deck Meshtastic device page:
  https://meshtastic.org/docs/hardware/devices/lilygo/tdeck/
- T-Deck pinout and specs, espboards:
  https://www.espboards.dev/esp32/lilygo-t-deck/
- T-Deck Plus DEF CON 33 field writeup:
  https://blog.shellntel.com/p/def-con-33-and-meshtastic-on-the-lilygo-t-deck-plus
- T-Deck Plus review, quirks and reliability, OSS Malta:
  https://ossmalta.eu/lilygo-t-deck-plus-review-a-meshtastic-handheld-with-great-potential-and-quirky-flaws/
- T-Deck Plus touchscreen fix discussion, Meshtastic firmware:
  https://github.com/meshtastic/firmware/discussions/5606
- T-Watch S3 product and specs, OpenELAB:
  https://openelab.io/products/lilygo-t-watch-s3
- T-Watch S3 Plus review, ESP32-S3, 16 MB / 8 MB, AMOLED:
  https://www.igeekphone.com/lilygo-t-watch-s3-plus-smartwatch-review-at-73-79-a-developers-dream-with-esp32-s3-power/
- T-Watch S3 Plus review, documentation and library caveats:
  https://www.andibond.com/lilygo-t-watch-s3-plus-esp32-s3-review/

M5Stack Cardputer

- Cardputer, $30 ESP32-S3 with keyboard and display, CNX Software:
  https://www.cnx-software.com/2023/10/14/m5stack-cardputer-a-30-card-sized-esp32-s3-computer-with-display-and-keyboard/
- Cardputer-Adv, improved keys, IMU, larger battery, CNX Software:
  https://www.cnx-software.com/2025/10/23/m5stack-cardputer-adv-esp32-s3-computer-gains-improved-antenna-larger-1750-mah-battery-es8311-audio-codec/
- Cardputer Adv review, Make:
  https://makezine.com/article/technology/microcontrollers/review-m5stack-cardputer-adv-version-esp32-s3/
- Cardputer review, screen and keyboard criticism, Raspberry Pi magazine:
  https://magazine.raspberrypi.com/articles/m5stack-card-computer-review

Input inventory sources

- LilyGO T-Beam Supreme buttons, Meshtastic:
  https://meshtastic.org/docs/hardware/devices/lilygo/tbeam/buttons/
- T-Beam Supreme pinout, buttons, QMI8658 IMU, espboards:
  https://www.espboards.dev/esp32/lilygo-t-beam-supreme/
- Heltec WiFi LoRa 32 V3, PRG and RST buttons, no touchscreen:
  https://www.espboards.dev/esp32/heltec-wifi-lora-32-v3/
- Heltec Wireless Tracker, UC6580, TFT, buttons:
  https://heltec.org/project/wireless-tracker/
- RAK19007 base board, reset button, no built-in nav button:
  https://docs.rakwireless.com/product-categories/wisblock/rak19007/datasheet/
- RAK WisBlock user button on AIN1 pin 31, Meshtastic peripherals:
  https://meshtastic.org/docs/hardware/devices/rak-wireless/wisblock/peripherals/
- RAK WisBlock adding a user button, Rokland:
  https://store.rokland.com/pages/adding-a-user-button-rak19007
- M5Stack Core2, FT6336U touch, MPU6886/BMI270 IMU, SPM1423 mic:
  https://docs.m5stack.com/en/core/core2
- M5Stack CoreS3, FT6336U multitouch, BMI270 + BMM150, dual mic:
  https://docs.m5stack.com/en/core/CoreS3

Meshtastic coexistence and flash

- Meshtastic firmware architecture, module system, DeepWiki:
  https://deepwiki.com/meshtastic/firmware
- Meshtastic ESP32 platform and variants, DeepWiki:
  https://deepwiki.com/meshtastic/firmware/11.1-esp32-platform-and-variants
- Meshtastic esp32-unified-ota bootloader:
  https://github.com/meshtastic/esp32-unified-ota
- Meshtastic ESP32 flashing, OTA partition offsets:
  https://meshtastic.org/docs/getting-started/flashing-firmware/esp32/
- 4 MB flash OTA squeeze, OpenDTU-OnBattery issue 1025:
  https://github.com/hoylabs/OpenDTU-OnBattery/issues/1025
- ESP32 max app size with OTA, esp32.com forum:
  https://www.esp32.com/viewtopic.php?t=20647
- LoRa plus BLE concurrency, Heltec WiFi LoRa 32 V3:
  https://heltec.org/project/wifi-lora-32-v3/
- Meshtastic overview, LoRa plus BLE, Wikipedia:
  https://en.wikipedia.org/wiki/Meshtastic
- ESP32-S3 LoRa and Meshtastic guide, Seeed Studio:
  https://www.seeedstudio.com/blog/2026/05/26/esp32-lora-guide/

furble source and plan anchors

- `lib/furble/Camera.h:20,168` BLE central role, NimBLEClient ownership
- `platformio.ini:14` framework espidf, not Arduino
- `src/FurbleUI.cpp:443,504` touch read callback and pointer indev
- `plans/40-thinknode-port.md` nRF52 rewrite verdict
- `plans/41-alternative-hardware.md` sidecar hardware survey, ThinkNode M5 pick
- `plans/34-ota-partitions.md` OTA partition scheme, adjacent to the two-app switch
- Fork PR #118, per-model physical buttons in the SDL sim:
  https://github.com/MaxRink/furble/pull/118
