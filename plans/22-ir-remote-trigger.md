# PR22 - Infrared shutter trigger

## Goal

Fire a camera shutter over infrared. This is a second trigger path next to BLE.
It works with no camera paired and no BLE connection. Cameras that only accept
IR become usable. Cameras that accept both can be triggered by either path.

## Scope

In scope:

- New `FurbleIR` module built on the ESP-IDF RMT transmit driver.
- Three protocols: Nikon ML-L3, Sony 20 bit SIRC, Canon RC-1.
- IR page on the main menu with a Fire button, plus an IR entry on the
  Connected menu.
- `Settings->Infrared` submenu with an enable toggle and a protocol roller.
- Runtime hiding on boards with no IR emitter.

Out of scope:

- IR receive and decode. The StickS3 has an IR receiver on G42 but no other
  supported board does. A learning mode is a separate PR.
- Pentax, Olympus and Panasonic protocols. The encoder table is written so they
  can be added later without touching the driver.
- Chaining IR into the intervalometer. Doable later once the trigger is proven.

## Implementation status

Implemented:

- Added the `FurbleIR` RMT transmit module with lazy channel allocation, a
  queued worker task, carrier control, and runtime board pin selection for the
  documented M5Stick boards.
- Added Nikon ML-L3, Sony 20 bit SIRC, and Canon RC-1/Canon RC-6 handset
  encoders using the timings in this plan.
- Added persisted `IR` and `IR_PROTO` settings, with infrared disabled by
  default.

Rebase notes:

- `IR` is assigned wire_id 31 and `IR_PROTO` wire_id 32, continuing after
  `PRESET_PICKER` (30) from PR 33.
- Console settingType, printValue and setValue cover `IR` as bool and
  `IR_PROTO` as generic uint8 (`IR::fire()` clamps out-of-range protocol
  values). Both are marked appliesImmediately because `IR::fire()` loads
  both settings on every trigger.
- `src/FurbleCompanion.cpp` settingType and settingValue cover `IR` as
  SETTING_BOOL and `IR_PROTO` as SETTING_U8.
- Added the standalone main-menu IR Fire page, the Connected-menu IR entry,
  and the Settings->Infrared submenu. Entries are hidden at runtime when IR is
  disabled or when the detected board has no emitter pin.
- Deviation from the Firing section as originally written: the Connected-menu
  IR entry navigates to the shared IR page instead of firing IR alongside BLE.
  A link is simpler and avoids surprise double-triggering, so `IR::fire()` has
  exactly two call sites: the Fire button and the console `ir fire` command.
- Added a debug console `ir fire [protocol]` command as the scripted hardware
  verification path. The protocol argument is optional and defaults to the
  `IR_PROTO` setting.
- Corrected the Nikon ML-L3 marks and spaces to the published reference values
  (390/1580, 410/3580, 400 stop) after review caught rounded stand-ins.
- Console `settings set ir on|off` queues a `UI::Request::IR_RELOAD` so the IR
  menu entries appear or disappear without a reboot, mirroring `GPS_RELOAD`.

Deferred:

- IR receive, decoding, and learning mode remain out of scope.
- Pentax, Olympus, and Panasonic protocols remain unimplemented.
- Intervalometer integration and camera-specific feedback remain deferred.

Open questions:

- Hardware verification is pending. Waveform correctness needs checking via a
  phone camera against the IR LED output.
- The author only has a Fujifilm camera body with no IR receiver, so end-to-end
  protocol validation against real camera hardware needs community help.
- The M5StickC Plus SE emitter pin is still unverified and needs an on-device
  probe before it can be treated as supported.

## Hardware support matrix

Verified against the M5Stack product pages listed in References.

| Board | IR emitter | GPIO | Notes |
|---|---|---|---|
| M5StickC | yes | G9 | Red LED is a separate pin (G10) |
| M5StickC Plus | yes | G9 | Red LED is a separate pin (G10) |
| M5StickC Plus2 | yes | G19 | **Shared with the red LED.** Driving IR blinks the LED |
| M5StickC Plus SE | assume Plus family | verify | No M5Stack doc page exists for this model. Probe on device |
| M5StickS3 | yes | G46 TX, G42 RX | Docs state IR receive must use RMT |
| M5Stack Core | no | - | Not listed in the product page pinmap |
| M5Stack Core2 | no | - | Not listed in the product page pinmap |

M5Unified has no `pin_name_t` entry for an IR pin, so furble needs its own
per-board table keyed on `M5.getBoard()`.

The `m5stick-c-plus` PlatformIO env builds one binary for StickC Plus, Plus2 and
Plus SE, so the pin must be selected at runtime, not by `#ifdef`.

## Files to change

| File | Lines | What |
|---|---|---|
| `include/FurbleIR.h` | new | `IR` singleton, `protocol_t` enum, `bool isSupported()`, `void fire()` |
| `src/FurbleIR.cpp` | new | RMT channel setup, encoder table, per-board pin table |
| `src/CMakeLists.txt` | 1-10 | Add `FurbleIR.cpp` to `furble_sources` |
| `src/CMakeLists.txt` | 12-14 | Add `esp_driver_rmt` to `PRIV_REQUIRES` |
| `include/FurbleSettings.h` | 16-29 | `type_t` enum. Add `IR`, `IR_PROTO` |
| `include/FurbleSettings.h` | 101-148 | `storage_type<>`. Add `bool` and `uint8_t` bindings |
| `src/FurbleSettings.cpp` | 11-24 | Setting table. Add two rows |
| `src/FurbleSettings.cpp` | 169-230 | Defaults. `IR` joins the false group at 209-215, `IR_PROTO` defaults to 0 |
| `include/FurbleUI.h` | 161-191 | Menu name strings. Add `m_IRStr`, `m_IRProtoStr` |
| `include/FurbleUI.h` | 299-346 | Add `addIRMenu()` and `addIRSettingsMenu()` |
| `src/FurbleUI.cpp` | 53-76 | `m_Menu` grid map. Add the new entries |
| `src/FurbleUI.cpp` | 705-731 | `addSettingItem()` bool helper, reused for the `IR` toggle |
| `src/FurbleUI.cpp` | 1278-1428 | Connected menu. Add the IR entry |
| `src/FurbleUI.cpp` | 2062-2082 | `addSettingsMenu()`. Call `addIRSettingsMenu(menu)` |
| `src/main.cpp` | 27-29 | Call `Furble::IR::init()` after `Settings::init()` |

## New settings

| Enum | NVS key | Namespace | Type | Default | Notes |
|---|---|---|---|---|---|
| `IR` | `ir` (2) | `FURBLE_STR` | `bool` | `false` | Off reproduces current behaviour. No RMT channel is allocated when false |
| `IR_PROTO` | `ir_proto` (8) | `FURBLE_STR` | `uint8_t` | `0` | 0 Nikon, 1 Sony, 2 Canon immediate, 3 Canon 2 s delay |

Name strings in the setting table: `"Infrared"` and `"IR Protocol"`.

## Menu placement

```
Main
└─ IR                (hidden when IR off or board has no emitter)
   └─ Fire button
Settings
└─ Infrared          (hidden when the board has no emitter)
   ├─ Infrared       (switch)
   └─ IR Protocol    (roller: Nikon / Sony / Canon / Canon 2s)
Connected
└─ IR                (hidden when IR off or board has no emitter)
```

The main menu grid at `src/FurbleUI.cpp:53-76` has Connect `{0,0}`, Scan `{1,0}`,
Delete `{2,0}`, Settings `{3,0}` and Off `{3,1}`. Put IR at `{0,1}`.

The Settings grid is contested by PR01 (Power), PR05 (Diagnostics), PR08
(Bluetooth) and PR16 (Sensors). Take the next free cell. Whichever of those PRs
lands last settles the final grid.

## Implementation notes

### Why RMT

The ESP32 RMT peripheral generates the pulse train and the carrier in hardware.
Bit banging a 38 kHz carrier from a task competes with LVGL, NimBLE and the GPS
UART, and furble runs with automatic light sleep enabled. RMT is also what
M5Stack recommends: the StickS3 page states that IR decoding "must use the ESP32
RMT peripheral".

Driver shape, following the ESP-IDF `ir_nec_transceiver` example:

```
rmt_tx_channel_config_t tx = {
  .gpio_num = <board pin>,
  .clk_src = RMT_CLK_SRC_DEFAULT,
  .resolution_hz = 1000000,   // 1 tick = 1 us
  .mem_block_symbols = SOC_RMT_MEM_WORDS_PER_CHANNEL,  // 64 on ESP32, 48 on S3
  .trans_queue_depth = 4,
};
rmt_new_tx_channel(&tx, &chan);
rmt_carrier_config_t carrier = { .frequency_hz = f, .duty_cycle = 0.33 };
rmt_apply_carrier(chan, &carrier);
```

Each protocol is a table of `rmt_symbol_word_t` pairs in microseconds, sent with
`rmt_new_copy_encoder()` and `rmt_transmit()`, then `rmt_tx_wait_all_done()`.
No custom encoder is needed because every frame fits in a static symbol array.

Carrier frequency differs per protocol, so call `rmt_apply_carrier()` before each
transmit rather than once at init.

### Protocol tables

**Nikon ML-L3.** 38 kHz carrier. One frame is:

```
mark 2000, space 27830, mark 390, space 1580,
mark 410, space 3580, mark 400
```

Then a 63 ms gap and the same frame again. The sources disagree on whether the
published 63.2 ms is the gap between frames or the frame repeat period, so the
implementation treats it as the gap and waveform verification on hardware will
settle it. The real remote sends the frame three times. Timing tolerance is
loose. Both reverse engineering write-ups report that a few percent of drift
causes no problems.

**Sony.** 40 kHz carrier, 20 bit SIRC. Header is a 2400 us mark and a 600 us
space. A zero bit is a 600 us mark and a 600 us space. A one bit is a 1200 us
mark and a 600 us space. Bits go out least significant first: 7 command bits
then 13 address bits. Address is `0x1E3A`. Command `0x2D` is release and `0x37`
is the two second delayed release. Repeat the frame three times at 45 ms
intervals.

**Canon RC-1.** 32.7 kHz carrier, no data. Two bursts of 16 carrier cycles. The
gap between burst starts selects the mode: 7.33 ms fires immediately, 5.36 ms
fires after two seconds. Reported tolerances are 7.0 to 7.7 ms and 5.1 to 5.7 ms.

Note on naming. The Canon RC-6 handset uses this same two burst scheme. It is not
the Philips RC-6 protocol, which is Manchester coded on a 36 kHz carrier. Do not
mix up the two when reading references.

### Lifecycle and power

Allocate the RMT channel lazily on first fire and keep it. `rmt_enable()` and
`rmt_disable()` bracket each transmission. The IDF RMT driver takes a power
management lock inside `rmt_enable()` when `CONFIG_PM_ENABLE` is set, so light
sleep is blocked only while a frame is going out. Total on time is under 200 ms
per shot for the worst protocol.

Energy cost is negligible. The IR LED is on only during marks, which is a few
milliseconds of the frame, and only when the user fires. There is no idle cost
because nothing polls.

### Plus2 LED conflict

On StickC Plus2 the IR emitter and the red LED share G19. Any LED feedback added
by PR23 must not drive G19 while an IR frame is in flight, and the IR fire will
visibly blink the red LED. Document this in both PRs. The IR module owns G19 on
that board and PR23 must ask it before using the LED.

### Firing

`IR::fire()` builds the symbol list for the current `IR_PROTO`, applies the
carrier, transmits, waits, and disables the channel. It runs from the LVGL
button callback. Worst case is the Nikon frame at about 190 ms across three
repeats, which is long enough to stall the UI, so run the transmit on a short
lived task or split it across `lv_timer` ticks. Prefer a dedicated task with a
queue of one so a double press cannot overlap frames.

`IR::isSupported()` returns whether `M5.getBoard()` maps to a pin. Every menu
entry checks it and stays hidden when false. No `#ifdef` is used, matching the
runtime board switch style at `src/FurbleUI.cpp:95-109`.

BLE is untouched. The Connected menu IR entry is a navigation link to the
shared IR page, not a trigger. Firing IR is always an explicit press of the
Fire button, which is the only UI call site of `IR::fire()`. The original idea
of an entry that fires IR in addition to BLE was dropped: a link is simpler and
a connected shutter press can never double-trigger by surprise. The two paths
do not interact.

## Dependencies

- None hard. It can land any time.
- PR03 (settings while connected) makes the Connected menu IR entry more useful,
  but is not required because IR is also on the main menu.
- PR06 (power module) is useful if the RMT power management lock turns out to be
  insufficient, but the IDF driver handles it.
- Interacts with PR23 on StickC Plus2 through the shared G19 pin.

## Risks

- Protocol timing accuracy. All three protocols come from reverse engineering,
  not vendor documentation. Sony and Canon values are corroborated by two
  independent sources each. Verify the emitted waveform before trusting the
  numbers.
- Range and aim. The on board IR LEDs on the sticks are small and unaimed.
  Expect a few metres at best, and only with line of sight to the camera IR
  window. State this in the PR body so nobody reports it as a bug.
- No feedback channel. IR is one way. There is no way to confirm the camera
  fired. The UI can only say the frame was sent.
- Wrong protocol selected silently does nothing. Make the roller label explicit
  about which camera families each entry targets.
- StickC Plus SE pin is unverified. If the probe shows no IR, hide the feature on
  that board and say so.
- RMT channel exhaustion. The classic ESP32 has 8 RMT channels and the S3 has 4
  TX channels. Nothing else in furble uses RMT today. If a later PR adds an
  addressable LED driver it will also want RMT. Allocate one channel and hold it.

## Verification

Build matrix:

```
pio run -e m5stick-c -e m5stick-c-plus -e m5stack-core -e m5stack-core2 -e m5stick-s3
```

All five must build clean with `-Wall -Wextra`. The Core and Core2 builds must
compile the module but resolve to no pin.

Defaults regression:

1. Erase NVS, flash master, note the main menu layout.
2. Flash this branch on fresh NVS. The main menu must look identical. IR is off,
   so the IR entry is hidden. Only `Settings->Infrared` is new.

Waveform verification, M5StickS3 over USB, no camera needed:

1. `pio run -e m5stick-s3 -t upload`, then `pio device monitor`.
2. Use the on board IR receiver on G42 as a loopback. Configure an RMT RX channel
   in a debug build, point the device at a wall or a mirror at close range, fire
   each protocol and dump the captured symbol list to the console.
3. Compare the captured marks and spaces against the tables above. Every value
   must be within 5 percent.
4. If reflection is too weak, aim a second StickS3 at the first, or read G46
   directly with a jumper to G42 through a resistor.

Waveform verification, boards with no IR receiver (StickC, Plus, Plus2):

1. Point the emitter at a phone camera. Most phone sensors see 940 nm. Fire
   repeatedly and confirm the LED flashes on the phone preview.
2. This proves the LED and the pin, not the timing. Trust the S3 loopback for
   timing and confirm the same code path runs on the AXP192 boards.

Camera verification:

1. Only Fujifilm bodies are available. Most Fujifilm X bodies have no IR
   receiver, so an end to end IR test is likely impossible here. Check the
   specific body first. If it has an IR window, add a Fujifilm protocol entry in
   a follow up. If it does not, say plainly in the PR body that no camera was
   triggered and that Nikon, Sony and Canon are waveform verified only.
2. Confirm IR does not disturb BLE. Connect to a Fujifilm body, then fire IR 20
   times from the Connected menu. No disconnects, no shutter latency change.

Power check:

1. Unplug USB. Log battery voltage every 30 s for 30 minutes with IR enabled and
   idle. Compare against the same run with IR off. The two slopes must match,
   because nothing runs when idle.

## References

All links fetched and checked.

- M5StickS3 product page, IR TX on G46 and IR RX on G42, and the statement that
  IR decoding must use the RMT peripheral:
  https://docs.m5stack.com/en/core/StickS3
- M5StickC product page, IR transmitter on G9, red LED on G10:
  https://docs.m5stack.com/en/core/m5stickc
- M5StickC Plus product page, IR transmitter on G9, red LED on G10:
  https://docs.m5stack.com/en/core/m5stickc_plus
- M5StickC Plus2 product page, IR emitter and red LED both on G19:
  https://docs.m5stack.com/en/core/M5StickC%20PLUS2
- M5Stack Core Basic product page, no IR in the pinmap:
  https://docs.m5stack.com/en/core/basic
- M5Stack Core2 product page, no IR in the pinmap:
  https://docs.m5stack.com/en/core/core2
- ESP-IDF v5.4.2 RMT driver, transmit channel config, carrier config, encoders
  and the power management note:
  https://docs.espressif.com/projects/esp-idf/en/v5.4.2/esp32/api-reference/peripherals/rmt.html
- ESP-IDF v5.4.2 `ir_nec_transceiver` example, the reference structure for an IR
  encoder on RMT:
  https://github.com/espressif/esp-idf/tree/v5.4.2/examples/peripherals/rmt/ir_nec_transceiver
- Nikon ML-L3 protocol, SB-Projects, 38 kHz carrier and the 63.2 ms frame repeat:
  https://www.sbprojects.net/projects/nikon/index.php
- Nikon ML-L3 teardown and oscilloscope capture, full mark and space sequence:
  https://goughlui.com/2013/12/06/teardown-and-project-clone-nikon-ml-l3-ir-remote-and-emulation/
- Sony SIRC specification, header and bit timings, 12/15/20 bit variants:
  https://www.edcheung.com/automa/sircs.htm
- Sony IR code layout, command and address bit order:
  https://www.righto.com/2010/03/understanding-sony-ir-remote-codes-lirc.html
- Canon RC-1 reverse engineering, 32700 Hz carrier, 16 pulse bursts, 7.33 ms and
  5.36 ms gaps with tolerances:
  http://www.cfd.tu-berlin.de/~panek/foto/rc-1/canon_remote_control.html
- Philips RC-6, cited only to show it is a different protocol from the Canon RC-6
  handset: https://www.sbprojects.net/knowledge/ir/rc6.php

## Hardware verification, 2026-08-17

Console path verified on the combined image. `ir fire 0..3` queues for all four
protocols and correctly errors with `ir is disabled` when the setting is off.
Actual IR emission is fired blind, filming the emitter with a phone camera is
on the user checklist.
