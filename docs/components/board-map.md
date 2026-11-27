# Board map

Every pin number, bus, address and timing constant that the firmware hard codes
for a supported board, with the line that proves it. Vendor documents do not
supply these numbers; they come from the board layout as it is written in
`src/FurbleBoard.cpp` and the drivers.

## Runtime board profile

`Board::profileFor()` picks a row from the detected board, not from a build
macro, because one image serves several boards. The field list and the meaning
of each column are in `include/FurbleBoard.h`. The values below are the table in
`src/FurbleBoard.cpp`, and the evidence for each one is the per-board
verification table in
[board-verification.md](board-verification.md).

| Field | StickC | StickC+ | StickC+2 | StickS3 | Core | Core2 | Tough | Waveshare | Unknown |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| `battery.level` | yes | yes | yes | yes | yes | yes | yes | no | no |
| `battery.voltage` | yes | yes | yes | yes | no | yes | yes | no | no |
| `battery.current` | yes | yes | no | no | no | yes | yes | no | no |
| `battery.charging` | yes | yes | no | yes | yes | yes | yes | no | no |
| `batteryCapacityMah` | 95 | 120 | 200 | 250 | 110 | 500 | 390 | 0 | 0 |
| `rtcBatteryBacked` | yes | yes | yes | no | no | yes | yes | no | no |
| `rtcOnBus` | yes | yes | yes | no | no | yes | yes | no | no |
| `irPin` | 9 | 9 | 19 | 46 | -1 | -1 | -1 | -1 | -1 |
| `motionWake` | DIRECT | DIRECT | NONE | PMIC_GPIO | NONE | NONE | NONE | NONE | NONE |
| `motionWakePin` | 35 | 35 | -1 | 13 | -1 | -1 | -1 | -1 | -1 |
| `timedWake` | NONE | RTC_ALARM | RTC_ALARM | PMIC_TIMER | NONE | NONE | RTC_ALARM | NONE | NONE |
| `powerHoldPin` | -1 | -1 | 4 | -1 | -1 | -1 | -1 | -1 | -1 |
| `pmicClickStreak` | yes | yes | no | no | no | no | yes | no | no |
| `sdSlot` | no | no | no | no | yes | yes | yes | no | no |
| `canPowerOff` | yes | yes | yes | yes | no | yes | yes | no | yes |
| `powerOffPath` | yes | yes | yes | yes | yes | yes | yes | no | yes |
| `modemSleep` | no | no | no | yes | no | no | no | no | no |
| `minBrightness` | 48 | 48 | 32 | 32 | 32 | 32 | 32 | 32 | 32 |
| `feedbackSound` | no | yes | yes | yes | yes | yes | yes | no | no |
| `feedbackLight` | yes | yes | yes | no | no | no | no | no | no |
| `feedbackVibration` | no | no | no | no | no | yes | no | no | no |
| `ledPin` | 10 | 10 | 19 | -1 | -1 | -1 | -1 | -1 | -1 |
| `gpsRail` | yes | yes | no | yes | no | yes | yes | no | no |
| `ledActiveLow` | yes | yes | no | no | no | no | no | no | yes |
| `irMarkLow` | yes | yes | no | no | no | no | no | no | no |
| `gpsPinSource` | BOARD | BOARD | BOARD | BOARD | NONE | BOARD | BOARD | NONE | BOARD |
| `gpsRxPin` / `gpsTxPin` | -1 / -1 | -1 / -1 | -1 / -1 | -1 / -1 | -1 / -1 | -1 / -1 | -1 / -1 | -1 / -1 | -1 / -1 |

BOARD is `GpsPinSource::BOARD_SUPPORT`. The unknown row is the fallback for a
board furble cannot identify, and the Waveshare ESP32-S3-ETH is the row for
`board_M5StampS3`, which is the identity M5GFX reports for that image because
`platformio.ini` pins it.

The per-board facts behind these columns, with a document or a source file for
each one, are in [board-verification.md](board-verification.md). The columns that
changed in plan 386 are `rtcOnBus`, which was false on four boards whose vendor
pinmap lists a BM8563 at 0x51, and `ledActiveLow`, which was true on four rows
that drive no LED pad at all.

Reading notes from the comment block above the table:

- Battery caps still follow the PMIC: AXP192 and AXP2101 boards report all four
  values, the M5PM1 has no battery current register, the Plus2 reads an ADC
  divider on G38, and the Core's IP5306 gives a coarse four-level level only.
- The IR pin is only free on the StickC and StickC Plus because those images run
  the flash in DIO mode. QIO would claim GPIO 9 and 10 as SD2 and SD3. GPIO 19
  on the Plus2 is also the LED, and the StickS3 emitter is on G46 with its
  receiver on G42.
- `gpsRail` is true only where `M5.Power.setExtOutput()` actually switches the
  external 5 V output. The same output feeds the Grove port, the HAT 5 V pin and
  the IR circuit on the StickS3, and one BUS_5V net for Port A and the M-Bus on
  the Core2 and Tough.
- `powerOffPath` is false only on the Waveshare row, which has no PMIC, no cell
  and no rail, so `M5.Power.powerOff()` would deep sleep with no wake source
  armed.

## LilyGO boards

The two LilyGO images have no `m5::board_t` value and no `profileFor()` arm:
M5Unified detects neither board, so `Board::compiledProfile()` answers from the
build macro (`src/FurbleBoard.cpp`, `include/FurbleBoard.h`). Their wiring is a
separate facts table, `include/FurbleLilyGo.h` and `src/FurbleLilyGo.cpp`, which
is the only place a LilyGO pin number appears.

| Fact | T-Deck / T-Deck Plus | T-Watch Ultra |
| :--- | :--- | :--- |
| SPI host | `SPI2_HOST` (1) | `SPI2_HOST` (1) |
| SPI SCLK / MISO / MOSI | 40 / 38 / 41, shared by panel, microSD and LoRa | 35 / 33 / 34, shared by microSD, LoRa and NFC |
| Panel controller | `lgfx::Panel_ST7789`, SPI | `lgfx::Panel_CO5300`, quad SPI |
| Panel CS / DC / RST / backlight | 12 / 11 / none / 42 | 41 / none / 37 / none (brightness is command 0x51) |
| Panel geometry, native | 240x320 | 410x502 |
| Panel geometry, driven | 320x240 at rotation 1 | 502x410 at rotation 1 |
| Panel offset | 0, 0 | `offset_x` 22, `offset_y` 0, swapped to `offset_y` by rotation 1 |
| Panel invert | yes (IPS) | no |
| I2C SDA / SCL | 18 / 8 | 3 / 2 |
| I2C devices | keyboard 0x55 (INT 46) | AXP2101 0x34, XL9555 0x20 |
| SD card select | 39 | 21 |
| Speaker I2S BCLK / WS / DOUT | 7 / 5 / 6 | 9 / 10 / 11 |
| Input | trackball right 2, up 3, left 1, down 15, click 0 | BOOT on 0 |
| Peripheral gate | GPIO10, driven high before any peripheral | none; the AXP2101 rails are switched |
| Battery sense | GPIO4, divider, not driven by this port | AXP2101, not read by this port |
| Panel quad-SPI SCLK / D0-D3 | none | 40 / 38, 39, 42, 45 |
| Keyboard interrupt | 46, active low, used to gate the I2C read | none |

The display path is `include/FurbleDisplay.h`: on an M5 board `screen()` returns
`M5.Display` from src/FurbleUI.cpp, and on a LilyGO board it builds an
`lgfx::LGFX_Device` from the facts row. `src/FurbleDisplay.cpp` is the only file
that names a panel class for these boards, and `Furble::LilyGo::powerOn()` is the
only file that programs the AXP2101 rails: ALDO2 display, ALDO1 SD, BLDO2
speaker, BLDO1 GNSS, then XL9555 pin 7 high for the panel supply.

The backlight is a limitation, not a driver: the T-Deck's AW9364 takes a pulse
train on GPIO42, so `LightDigital` drives the pin on or off and the brightness
slider switches rather than dims.

## Motion wake wiring

- StickC family: the MPU6886 INT pin is GPIO 35, shared with the BM8563 on the
  SYS_INT net, and M5Unified configures that pin open drain, so it needs an
  external pull-up (`src/FurbleBoard.cpp:21-27`,
  `src/FurbleIMUMotionMPU6886.cpp:54-57`).
- StickS3: the BMI270 INT1 goes to M5PM1 GPIO 4, and the PMIC drives its own IRQ
  into SoC GPIO 13 (`src/FurbleBoard.cpp:25-27`). The firmware arms GPIO 4 as a
  wake source before light sleep (`src/FurblePlatform.cpp:677`) and clears it
  afterwards (`src/FurblePlatform.cpp:752`).

## I2C devices

| Part | 7-bit address | Bus owner | Citation |
| :--- | :--- | :--- | :--- |
| FT6336U touch, Core2 | 0x38 | M5GFX `Touch_FT5x06`, INT on G39 | M5Stack Core2 page, PinMap `CAP.TOUCH` |
| CHSC6540 touch, Tough | 0x2E | M5GFX `Touch_CHSC6540`, INT on G39 | M5Stack Tough page, PinMap `Cap.Touch` |
| BMI270 IMU | 0x68 | M5Unified internal bus | `src/FurbleIMUMotionBMI270.cpp:24` |
| MPU6886 IMU | 0x68 | M5Unified internal bus | `src/FurbleIMUMotionMPU6886.cpp:18` |
| BM8563 RTC | 0x51 | M5Unified internal bus, 400 kHz | `src/FurblePlatform.cpp:42-46` |
| M5PM1 PMIC | vendor library | M5Unified internal bus | `src/FurblePlatform.cpp:136` |
| AXP2101 PMIC | 0x34 | `lib/hal` `RegisterBus`, future boards only | `lib/hal/AXP2101.h:30` |
| BQ27220 gauge | 0x55 | `lib/hal` `RegisterBus`, future boards only | `lib/hal/BQ27220.h:30` |
| XL9555 expander | 0x20 to 0x27 | `lib/hal` `RegisterBus`, future boards only | `lib/hal/XL9555.h:27` |

The AXP192 (0x34) and the IP5306 (0x75 on the Core, its I2C variant) are driven
entirely by M5Unified, so their addresses appear in this repository only as the
register targets `src/FurblePlatform.cpp` writes and as the vendor pinmap rows
that name them. The two touch controllers are M5GFX's: FT5x06 at 0x38 on the
Core2 and CHSC6540 at 0x2E on the Tough, both with their interrupt on G39, which
is also the wake pin M5Unified records for those boards.

## GPS UART

| Item | Value | Citation |
| :--- | :--- | :--- |
| Port | Grove Port A. The comment records RX 33 and TX 32 for the unsupported Module GPS v2.1 | `src/FurbleGPS.cpp:167` |
| TX and RX pins | `M5.getPin(m5::port_a_pin2)` and `port_a_pin1` | `src/FurbleGPS.cpp:168-169` |
| Frame | 8N1, no flow control | `src/FurbleGPS.cpp:173-178` |
| Clock source | `UART_SCLK_XTAL` on S3 images, `UART_SCLK_REF_TICK` on ESP32 images, so DFS cannot shift the baud | `src/FurbleGPS.cpp:179-189` |
| Line detection | one `\n`, 9 bit times, pattern interrupt | `src/FurbleGPS.cpp:193` |
| Baud ladder | 115200, 9600, 38400, 57600, 19200, 4800 at 1200 ms per step, two checksum-passing sentences to lock | `lib/furble/protocol/GpsCasic.h:48-56` |

The Module GPS v2.1 comment at `src/FurbleGPS.cpp:166` records RX 33 and TX 32
for a module the firmware does not enable.

## W5500 Ethernet

| Signal | GPIO | Citation |
| :--- | :--- | :--- |
| SPI SCLK | 13 | `src/FurbleEthernet.cpp:131` |
| SPI MOSI | 11 | `src/FurbleEthernet.cpp:132` |
| SPI MISO | 12 | `src/FurbleEthernet.cpp:133` |
| SPI CS | 14 | `src/FurbleEthernet.cpp:134` |
| Interrupt | 10 | `src/FurbleEthernet.cpp:135` |
| Reset | 9 | `src/FurbleEthernet.cpp:136` |
| SPI host | `SPI2_HOST`, DMA channel auto | `src/FurbleEthernet.cpp:165` |
| SPI clock | 20 MHz | `src/FurbleEthernet.cpp:137,176` |

Those six pins are the reason `board_M5StampS3` is pinned for the Waveshare
image: it has no M5GFX probe, so no probe can drive GPIO 9 to 14
(`platformio.ini:88`, `CLAUDE.md` under single-board images).

## microSD bus and writer task

The slot shares the display SPI bus and is driven by the SDSPI host at 10 MHz
(`src/FurbleSD.cpp:46,959-969`). It is mounted at
`FURBLE_SD_MOUNT_POINT` with settings under `/furble`
(`src/FurbleSD.cpp:76-79`). The writer task is 6144 bytes, priority 1, with an
8 entry queue, a 100 ms enqueue timeout, a 3000 ms power-off drain and a
three-failure limit (`src/FurbleSD.cpp:47-56`).

## Display

The panel driver comes from M5GFX. The firmware only chooses a board pin:
`M5GFX_BOARD` is set for the three single-board images
(`platformio.ini:43,51,63`) and to `board_M5StampS3` for the display-less
Waveshare image (`platformio.ini:88`), where `post:patches/m5gfx_board.py` pins
`M5GFX::autodetect`. Panel geometry per simulated board is 80x160 StickC,
135x240 StickS3 and 320x240 Core (`docs/sim.md:17-21`).

The panel parts are ST7735S on the StickC, an ST7789 variant on the StickC Plus,
the Plus2 and the StickS3, and ILI9342C on the Core, the Core2 and the Tough,
each with its own page: [st7735.md](st7735.md), [st7789.md](st7789.md) and
[ili934x.md](ili934x.md). The Waveshare image has no panel. M5GFX maps the four
ILI9342C boards to `Panel_ILI9342` and the StickC to `Panel_ST7735S`, so the
both-direction dwell rule applies to four of the seven display boards, not
three.

### The panel sleep dwell

The panel datasheets require 120 ms between Sleep Out and the next Sleep In, and
M5GFX does not enforce it, so the firmware does (`include/FurbleUI.h:1136`,
`src/FurbleUI.cpp:1841-1844,1887-1894`).

| Rule | Behaviour | Citation |
| :--- | :--- | :--- |
| Sleep too soon after wake | refused, and the inactivity timer retries | `src/FurbleUI.cpp:1841-1845` |
| Wake too soon after sleep | the UI task delays the remainder, then wakes | `src/FurbleUI.cpp:1887-1894` |
| Frame in flight | `M5.Display.waitDMA()` before `sleep()` so the panel is never commanded to sleep mid transfer | `src/FurbleUI.cpp:1860-1863` |
| Backlight | `ESP_PM_APB_FREQ_MAX` is released only after the panel is asleep and acquired before `wakeup()`, because M5GFX restores the backlight during wakeup | `src/FurbleUI.cpp:1881-1882,1897-1898` |
| Dim then off | `DIM_TO_OFF_MS` is 30 s, measured from the moment the display entered DIM, so activity that restores the panel also restarts the dim dwell | `include/FurbleDisplayPolicy.h:11-23` |

### LVGL buffers

Both draw buffers are `MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL` because they feed
the SPI panel and cannot live in PSRAM (`src/FurbleUI.cpp:571-583`).

| Rule | Value | Citation |
| :--- | :--- | :--- |
| Largest panel | 320x240, the bound used for clamping | `include/FurbleUIMemory.h:12-13` |
| Partial rows divisor | 15 | `include/FurbleUIMemory.h:20` |
| Narrow panel width | at or below 160 px | `include/FurbleUIMemory.h:23` |
| Single buffer | narrow panels use one buffer and wait for DMA before reporting the flush complete | `include/FurbleUIMemory.h:36-38`, `src/FurbleUI.cpp:580-604` |
| Bytes per buffer | width times partial lines, halved again on a narrow panel | `include/FurbleUIMemory.h:56-64` |
| Second buffer missing | falls back to single buffering with a warning instead of aborting | `src/FurbleUI.cpp:594-604` |
| First buffer missing | logs and aborts, because every sdkconfig aborts on a failed allocation | `src/FurbleUI.cpp:584-589` |

Why it matters: the old fixed 320x240 pair wasted 11.8 KiB on the StickS3 and
17.3 KiB on the StickC, all of it internal DMA-capable RAM that the optional
HTTPS server needs (`src/FurbleUI.cpp:571-577`).

### Brightness floor

The minimum brightness per board is a multiple of the slider step, so the slider
cannot select a level the panel renders as off (`include/FurbleBoard.h:53`).
The values are 48 on StickC and StickC Plus and 32 everywhere else
(`src/FurbleBoard.cpp:132-168`, and the `minBrightness` row above).

## Console transport

The console reads the USB-Serial/JTAG peripheral on boards that make it primary,
and UART0 on the ESP32 boards behind the USB-UART bridge
(`src/FurbleConsole.cpp:3681-3702`). The ESP32 path takes an
`ESP_PM_APB_FREQ_MAX` lock for the console's lifetime, which is why no power
measurement may be taken from a build that contains it
(`src/FurbleConsole.cpp:3688-3699`).

## Board peripherals

The parts that are not a chip on an I2C bus: the card slot, the IR emitter, the
feedback outputs and the USB port. Each row is a board constant, and the chip
behind it is documented on its own page.

## microSD

| Item | Value | Citation |
| :--- | :--- | :--- |
| Boards with a slot | Core, Core2 and Tough. The StickC family has none, and the stack is compiled out there | `include/FurbleBoardCaps.h`, `src/FurbleBoard.cpp` |
| Bus | SDSPI on the display SPI bus, 10 MHz | `src/FurbleSD.cpp:959-969` |
| Mount point | `/sd` by default | `src/FurbleSD.cpp:39-43` |
| Settings file | `/sd/furble/settings.txt` | `src/FurbleSD.cpp:44-45` |
| Writer task | 6144-byte stack, priority 1, queue depth 8 | `src/FurbleSD.cpp:47-51` |
| Enqueue timeout | 100 ms | `src/FurbleSD.cpp:54` |
| Power-off drain | 3000 ms | `src/FurbleSD.cpp:55` |
| Failure limit | 3 | `src/FurbleSD.cpp:56` |

| Card detect | none of the three boards wires one. M5Unified's SD pin table carries `255` in the detect columns for the Core, Core2 and Tough | vendor pinmap, `SD` row: CS, SCK, MISO, MOSI only |
| Waveshare slot | the board has a TF slot but no card-select pin that the SDSPI stack could use, so the profile reports no slot while the image is built with `FURBLE_HAS_SD` | [board-verification.md](board-verification.md), Waveshare row |

A card that is absent, mounted or in error is a modelled peripheral state in the
simulator (`sim/coverage/hardware_quirks.json`, category `peripheral`, feature
`SD absent/mounted/error`).

## IR emitter

| Item | Value | Citation |
| :--- | :--- | :--- |
| Pin | 9 on StickC and StickC Plus, 19 on Plus2, 46 on StickS3, none elsewhere | `src/FurbleBoard.cpp:132-160` |
| Peripheral | one RMT TX channel at 1 MHz resolution | `src/FurbleIR.cpp:21,229-236` |
| Transmit timeout | 1000 ms, then the channel is disabled regardless | `src/FurbleIR.cpp:24,289-305` |
| Task | 4096-byte stack, priority 3, stack allowed in PSRAM because the task never writes NVS | `src/FurbleIR.cpp:27,174-186` |
| Carriers | 38 kHz Nikon, 40 kHz Sony, 32.7 kHz Canon | `src/FurbleIR.cpp:31,47,55` |
| GPIO 9 caveat | only free because the StickC images run the flash in DIO mode | `src/FurbleBoard.cpp:17-19` |

The protocol timings are in [the IR remote page](../protocols/ir-remote.md).

## Speaker, LED and vibration

| Output | Behaviour | Citation |
| :--- | :--- | :--- |
| Sound | M5Unified I2S. The IDF I2S driver holds `ESP_PM_APB_FREQ_MAX` while the channel is enabled, so tones are DFS-safe with no extra lock | `src/FurbleFeedback.cpp:15-19` |
| LED | plain GPIO, DFS-immune, and the pads hold their level through light sleep because `PM_SLP_DISABLE_GPIO` is unset | `src/FurbleFeedback.cpp:20-22` |
| Vibration | an I2C write to the PMIC, and the I2C driver recomputes its clock divider per transaction | `src/FurbleFeedback.cpp:23-24` |
| Low battery threshold | 10 percent over 6 samples | `src/FurbleFeedback.cpp:29-30` |
| Vibration level | 255 | `src/FurbleFeedback.cpp:31` |
| Pattern tick | 10 ms | `src/FurbleFeedback.cpp:32` |

Which outputs a board has is the `feedbackSound`, `feedbackLight` and
`feedbackVibration` fields of the board profile
(`include/FurbleBoard.h:79-81`, `src/FurbleBoard.cpp:139-177`). A request for an
output the board lacks is skipped rather than logged as a failure
(`src/FurbleFeedback.cpp:85-87`).

## USB

| Item | Value | Citation |
| :--- | :--- | :--- |
| S3 native USB | USB-Serial/JTAG, and the console reads it through its own driver | `src/FurbleConsole.cpp:3680-3686` |
| ESP32 boards | UART0 behind a USB-UART bridge, read through the same driver the IDF REPL installs | `src/FurbleConsole.cpp:3687-3700` |
| Console capability | `FURBLE_HAS_USB_SERIAL_JTAG_CONSOLE` | `include/FurbleBoardCaps.h:40,42` |
| VBUS sense | the Waveshare board has none, so it never infers external power | `include/FurbleBoardCaps.h:54,56` |
| Reset on port open | the S3 resets on each host port open, which the flash helper uses as the safe state transition | `CLAUDE.md`, hardware traps |

## Status LED on the Waveshare board

The Waveshare ESP32-S3-ETH has a WS2812 on GPIO 21
(`docs/power/board-power.md:115`). furble keeps it off, and it is one of the
three idle loads on that board together with the W5500 and the unused microSD
slot (`docs/power/board-power.md:283`).

## Open rows

| ID | Missing | Document and section needed | State |
| :--- | :--- | :--- | :--- |
| C-31a | The microSD electrical limits and the SDSPI timing table | SD Physical Layer Simplified Specification, bus timing chapter | open, the firmware fixes 10 MHz with no cited limit |
| C-31b | The speaker amplifier part and its gain on the Core | The Basic pinmap puts the speaker on G25 as `DAC/SPK`, and M5Unified drives G25 with `use_dac = true`, so there is no I2S amplifier on that board. Which part buffers the DAC output is still not in the tree | open, narrowed by plan 386 |
| C-31c | The WS2812 timing | WS2812B datasheet, timing table | open, M5Unified drives it |
| C-31d | The USB CDC and JTAG electrical detail | ESP32-S3 Technical Reference Manual v1.8 section 33.4, page 1253 gives the PLL plus APB_CLK dependency and the 40 MHz APB floor, so the clock half of this row is closed | open for the electrical detail only |
