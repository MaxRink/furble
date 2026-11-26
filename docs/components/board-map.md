# Board map

Every pin number, bus, address and timing constant that the firmware hard codes
for a supported board, with the line that proves it. Vendor documents do not
supply these numbers; they come from the board layout as it is written in
`src/FurbleBoard.cpp` and the drivers.

## Runtime board profile

`Board::profileFor()` picks a row from the detected board, not from a build
macro, because one image serves several boards (`src/FurbleBoard.cpp:61-80`).
The row layout is documented above the table in `src/FurbleBoard.cpp:25-28`, and
the field list is `include/FurbleBoard.h:36-65`.

| Field | StickC | StickC+ | StickC+2 | StickS3 | Core | Core2 | Tough | Unknown |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| `batteryCapacityMah` | 95 | 120 | 200 | 250 | 0 | 390 | 390 | 0 |
| `rtcBatteryBacked` | true | true | false | false | false | true | false | false |
| `irPin` | 9 | 9 | 19 | 46 | -1 | -1 | -1 | -1 |
| `motionWake` | DIRECT | DIRECT | NONE | PMIC_GPIO | NONE | NONE | NONE | NONE |
| `motionWakePin` | 35 | 35 | -1 | 13 | -1 | -1 | -1 | -1 |
| `timedWake` | NONE | NONE | RTC_ALARM | PMIC_TIMER | NONE | NONE | NONE | NONE |
| `powerHoldPin` | -1 | -1 | 4 | -1 | -1 | -1 | -1 | -1 |
| `pmicClickStreak` | true | true | false | false | false | false | true | false |
| `sdSlot` | false | false | false | false | true | true | false | false |
| `canPowerOff` | true | true | true | true | false | true | true | true |
| `modemSleep` | false | false | false | true | false | false | false | false |
| `minBrightness` | 48 | 48 | 32 | 32 | 32 | 32 | 32 | 32 |
| `feedbackSound` | false | true | true | true | true | true | false | false |
| `feedbackLight` | true | true | true | false | false | false | false | false |
| `feedbackVibration` | false | false | false | false | false | true | false | false |
| `ledPin` | 10 | 10 | 19 | -1 | -1 | -1 | -1 | -1 |
| `gpsRail` | true | true | false | true | false | true | true | false |

Rows: StickC `src/FurbleBoard.cpp:31`, StickC+ `:34`, StickC+2 `:37`, StickS3
`:40`, Core `:43`, Core2 `:46`, Tough `:49`, unknown `:55`.

Reading notes from the same block (`src/FurbleBoard.cpp:7-23`):

- The IR pin is only free on StickC and StickC Plus because those images run the
  flash in DIO mode. QIO would claim GPIO 9 and 10 as SD2 and SD3.
  `src/FurbleBoard.cpp:12-14`.
- GPIO 19 on the Plus2 is also the LED, so the IR emitter and the LED share it.
  `src/FurbleBoard.cpp:13-14`.
- Battery flow follows the PMIC. AXP192 boards report all four values, the
  M5PM1 has no battery current register, the Plus2 reads an ADC divider and the
  Core's IP5306 gives a coarse level only. `src/FurbleBoard.cpp:7-10`.
- `gpsRail` is true only where `M5.Power.setExtOutput()` actually switches Port A
  5 V. It does nothing on the Plus2, the Core or any other supported board.
  `src/FurbleBoard.cpp:20-23`.

## Motion wake wiring

- StickC family: the MPU6886 INT pin is GPIO 35, shared with the BM8563 on the
  SYS_INT net, and M5Unified configures that pin open drain, so it needs an
  external pull-up (`src/FurbleBoard.cpp:15-17`,
  `src/FurbleIMUMotionMPU6886.cpp:54-57`).
- StickS3: the BMI270 INT1 goes to M5PM1 GPIO 4, and the PMIC drives its own IRQ
  into SoC GPIO 13 (`src/FurbleBoard.cpp:16-18`). The firmware arms GPIO 4 as a
  wake source before light sleep (`src/FurblePlatform.cpp:526-536`) and clears
  it afterwards (`src/FurblePlatform.cpp:573-597`).

## I2C devices

| Part | 7-bit address | Bus owner | Citation |
| :--- | :--- | :--- | :--- |
| BMI270 IMU | 0x68 | M5Unified internal bus | `src/FurbleIMUMotionBMI270.cpp:24` |
| MPU6886 IMU | 0x68 | M5Unified internal bus | `src/FurbleIMUMotionMPU6886.cpp:18` |
| BM8563 RTC | 0x51 | M5Unified internal bus, 400 kHz | `src/FurblePlatform.cpp:42-46` |
| M5PM1 PMIC | vendor library | M5Unified internal bus | `src/FurblePlatform.cpp:125` |
| AXP2101 PMIC | 0x34 | `lib/hal` `RegisterBus`, future boards only | `lib/hal/AXP2101.h:30` |
| BQ27220 gauge | 0x55 | `lib/hal` `RegisterBus`, future boards only | `lib/hal/BQ27220.h:30` |
| XL9555 expander | 0x20 to 0x27 | `lib/hal` `RegisterBus`, future boards only | `lib/hal/XL9555.h:27` |

The AXP192, IP5306 and the Core2 touch controller are driven entirely by
M5Unified and M5GFX, so their addresses are not in this repository. That is an
open row in [the gap closure plan](../../plans/332-docs-gap-closure.md), not a
value this page may invent.

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
(`src/FurbleSD.cpp:43-46`). The writer task is 6144 bytes, priority 1, with an
8 entry queue, a 100 ms enqueue timeout, a 3000 ms power-off drain and a
three-failure limit (`src/FurbleSD.cpp:47-56`).

## Display

The panel driver comes from M5GFX. The firmware only chooses a board pin:
`M5GFX_BOARD` is set for the three single-board images
(`platformio.ini:43,51,63`) and to `board_M5StampS3` for the display-less
Waveshare image (`platformio.ini:88`), where `post:patches/m5gfx_board.py` pins
`M5GFX::autodetect`. Panel geometry per simulated board is 80x160 StickC,
135x240 StickS3 and 320x240 Core (`docs/sim.md:17-21`).

The panel parts are ST7735S on the StickC, ST7789V on the StickC Plus, the Plus2
and the StickS3, and ILI9342C on the Core and Core2, each with its own page:
[st7735.md](st7735.md), [st7789.md](st7789.md) and [ili934x.md](ili934x.md).

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
(`src/FurbleBoard.cpp:31-56`, and the `minBrightness` row above).

## Console transport

The console reads the USB-Serial/JTAG peripheral on boards that make it primary,
and UART0 on the ESP32 boards behind the USB-UART bridge
(`src/FurbleConsole.cpp:3679-3700`). The ESP32 path takes an
`ESP_PM_APB_FREQ_MAX` lock for the console's lifetime, which is why no power
measurement may be taken from a build that contains it
(`src/FurbleConsole.cpp:3686-3697`).

## Board peripherals

The parts that are not a chip on an I2C bus: the card slot, the IR emitter, the
feedback outputs and the USB port. Each row is a board constant, and the chip
behind it is documented on its own page.

## microSD

| Item | Value | Citation |
| :--- | :--- | :--- |
| Boards with a slot | Core, Core2 and Tough. The StickC family has none, and the stack is compiled out there | `include/FurbleBoardCaps.h:47,49`, `src/FurbleBoard.cpp:43-52` |
| Bus | SDSPI on the display SPI bus, 10 MHz | `src/FurbleSD.cpp:959-969` |
| Mount point | `/sd` by default | `src/FurbleSD.cpp:39-43` |
| Settings file | `/sd/furble/settings.txt` | `src/FurbleSD.cpp:44-45` |
| Writer task | 6144-byte stack, priority 1, queue depth 8 | `src/FurbleSD.cpp:47-51` |
| Enqueue timeout | 100 ms | `src/FurbleSD.cpp:54` |
| Power-off drain | 3000 ms | `src/FurbleSD.cpp:55` |
| Failure limit | 3 | `src/FurbleSD.cpp:56` |

The Waveshare image carries a microSD slot in 1-line mode that furble does not
use (`docs/power/board-power.md:185`). A card that is absent, mounted or in error
is a modelled peripheral state in the simulator
(`sim/coverage/hardware_quirks.json`, category `peripheral`, feature
`SD absent/mounted/error`).

## IR emitter

| Item | Value | Citation |
| :--- | :--- | :--- |
| Pin | 9 on StickC and StickC Plus, 19 on Plus2, 46 on StickS3, none elsewhere | `src/FurbleBoard.cpp:31-52` |
| Peripheral | one RMT TX channel at 1 MHz resolution | `src/FurbleIR.cpp:21,229-236` |
| Transmit timeout | 1000 ms, then the channel is disabled regardless | `src/FurbleIR.cpp:24,289-305` |
| Task | 4096-byte stack, priority 3, stack allowed in PSRAM because the task never writes NVS | `src/FurbleIR.cpp:27,174-186` |
| Carriers | 38 kHz Nikon, 40 kHz Sony, 32.7 kHz Canon | `src/FurbleIR.cpp:31,47,55` |
| GPIO 9 caveat | only free because the StickC images run the flash in DIO mode | `src/FurbleBoard.cpp:12-14` |

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
(`include/FurbleBoard.h:54-56`, `src/FurbleBoard.cpp:65-92`). A request for an
output the board lacks is skipped rather than logged as a failure
(`src/FurbleFeedback.cpp:85-87`).

## USB

| Item | Value | Citation |
| :--- | :--- | :--- |
| S3 native USB | USB-Serial/JTAG, and the console reads it through its own driver | `src/FurbleConsole.cpp:3678-3684` |
| ESP32 boards | UART0 behind a USB-UART bridge, read through the same driver the IDF REPL installs | `src/FurbleConsole.cpp:3685-3698` |
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
| C-31b | The speaker amplifier part and its gain | M5Stack Core schematic and the amplifier datasheet, which is not in the tree | open |
| C-31c | The WS2812 timing | WS2812B datasheet, timing table | open, M5Unified drives it |
| C-31d | The USB CDC and JTAG electrical detail | ESP32-S3 Technical Reference Manual v1.8 section 33.4, page 1253 gives the PLL plus APB_CLK dependency and the 40 MHz APB floor, so the clock half of this row is closed | open for the electrical detail only |
