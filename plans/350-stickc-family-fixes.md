# 350 - StickC family fixes

Status: implemented on `fix/stickc-family`, not merged. Host tests and
`tools/pre_review.py` pass; no hardware run in this work, so the electrical
claims that need a scope or a schematic stay UNVERIFIED as marked below.

Related: [320](320-rtc-use.md) (timed wake through the BM8563 and the M5PM1),
[322](implemented/322-hardware-abstraction.md) (the `Board::Profile` row this work extends),
[313](313-sim-only-status.md) (hardware-deferred gates), [326](implemented/326-docs-accuracy.md)
(the doc audit that left the `setExtOutput` behaviour documented, not fixed).

## Why

The StickC family is three boards from one or two images: the StickC (G9 IR,
G10 LED), the StickC Plus (same pins, AXP192, buzzer), and the Plus2 (G19 shared
by the red LED and the IR emitter, no PMIC, HOLD pin, BM8563). Two independent
audit passes over that family found eight defects, each verified against the
pinned M5Unified 0.2.20 and M5GFX 0.2.27 sources in `.pio/libdeps`, against the
IDF 5.5.3 RMT and GPIO drivers, or against a StickC Plus schematic page supplied
by the second pass. Six are shared-pin or polarity faults that make the LED or
the emitter behave differently from the code's model, one is a wake-net
attribution error, and two are missing board capabilities.

No new setting, no new default, and no behaviour change on the Core, Core2,
Tough, StickS3 or Waveshare rows.

## Defects and fixes

### 1. The Plus2 LED and IR emitter had no pin owner

`Feedback::setLight()` wrote GPIO19 with `gpio_set_level()` while
`IR::createChannel()` bound the same pin to an RMT TX channel. Neither side knew
about the other, so:

- a blink during a frame could not be attributed and is lost on the device,
  because `rmt_new_tx_channel()` muxes the pad to the RMT signal;
- after `rmt_disable()` the pad stays muxed to that signal, so the LED is left at
  the RMT level and later GPIO writes are inert;
- a blink interleaves with the carrier when the pin returns to the GPIO path.

Fix: `include/FurblePinShare.h` (pure `Arbiter` plus `ledSharesIr()`,
`ledLevel()`) and `src/FurblePinShare.cpp` (one short mutex, the pad writes and
the hand-over). Feedback records every level through `PinShare::requestLed()`;
IR brackets a frame with `PinShare::beginIr()` / `PinShare::endIr()`. The frame
end restores the recorded level and returns the pad to the GPIO output path with
`gpio_set_direction(pin, GPIO_MODE_OUTPUT)`, which reaches `gpio_output_enable()`
and re-selects the default output signal
(`components/esp_driver_gpio/src/gpio.c:215-222`). A later frame re-selects the RMT
signal with `rmt_tx_switch_gpio()`. Nothing is allocated, the lock is one short
mutex per call, and it is never held across `rmt_tx_wait_all_done()`. A board
whose LED and emitter pins differ never enters the path, and an LED that was
never driven is parked at the profile's inactive level (`ledActiveLow`) instead
of being driven to a level invented from the frame.

### 2. The Plus2 reported an unbacked RTC and skipped the BM8563 voltage-low flag

`STICKC_PLUS2` had `rtcBatteryBacked = false`, so `TimeKeeper` ignored the
BM8563 voltage-low flag (`0x02` bit 7, read by M5Unified
`PCF8563_Class::getVoltLow()` as `0x02 & 0x80`) and charged a ten-minute
uncertainty to every RTC read. The board keeps the BM8563 alive across a HOLD
power-off exactly so its alarm can wake the device, so its calendar is retained
for the same purpose as the cell-backed rows.

Fix: the row is `true`. `TimeKeeperPolicy` gained `RTC_BACKED_UNCERTAINTY_MS`
(2 s), `RTC_UNBACKED_UNCERTAINTY_MS` (10 min), `rtcUncertaintyMs()` and
`rtcValueTrusted()`, and `readRtc()` uses them, so the flag rejects a suspect
calendar on a backed RTC only. The timed-wake path
(`canTimedWake()`, `powerOffUntil()`, `consumeTimedWake()`) does not read the
backing flag, so the Plus2 rejection behaviour, including
`verifyPlus2Timer()`'s register readback and `TIMED_WAKE_MAX_EARLY_S`, is
unchanged.

The same flag also suppresses the NVS shutdown checkpoint on that row
(`TimeKeeper`'s `needsNvsCheckpoint = !g_RtcBatteryBacked`), so the BM8563 is
now the only retention mechanism across a Plus2 power-off. That is the point of
the change, but it is a consequence worth stating: if the BM8563 does not
survive a HOLD power-off, which is a hardware gate this plan records, the next
boot sees the voltage-low flag, rejects the calendar and falls back to the last
NVS record, which is at most six hours old. The failure mode is a stale clock,
never a fabricated one.

### 3. The AXP192 ADC mask was justified by a Core2/Tough-only behaviour

`include/FurblePlatformPolicy.h` claimed M5Unified's `setExtOutput()` reads VBUS
current on AXP192 boards, so the channel had to stay enabled. In the pinned
0.2.20 source only the `board_M5StackCore2`/`board_M5Tough` case reads
`Axp192.getVBUSCurrent()` (`Power_Class.cpp:951-981`); the StickC and Plus case
only calls `Axp192.setEXTEN()`, and the Plus2 falls through to the empty default.

Fix: the comment now names the Core2/Tough path, says that one mask is written on
every AXP192 board so bit 2 stays set to keep that path unchanged, and records
that trimming the channel only on the StickC family is a follow-up, deferred
because the AXP192 register details could not be verified in this pass. The mask
write is unchanged, so Core2 and Tough behaviour is identical, and
`pm1_poll_policy_test` now names the real reason.

### 4. IR marks were inverted on the StickC and StickC Plus

`encodeNikon()`, `encodeSony()` and `encodeCanon()` emit marks at level 1 and the
carrier gates the high level. On the StickC and Plus the emitter anode is on
3.3 V and the cathode on GPIO9 (StickC Plus schematic, LED1 `LED_IR`), so light
is emitted on a low pin: the whole optical frame is inverted and the 2 ms contact
header becomes a 27.8 ms burst. The Plus2 drives an NPN stage, which inverts the
same image back, so it was correct there.

Fix: `Profile::irMarkLow` (true for the StickC and Plus, false for the Plus2). IR
sets `rmt_tx_channel_config_t.flags.invert_out` from it and passes the same
inversion to `rmt_tx_switch_gpio()`. The encoder stays mark-high, and the carrier
duty is flipped to 2/3 on those rows so the optical duty stays a third. The
inversion also fixes the idle level: `init_level` 0 becomes the pad-high,
emitter-off state on the active-low rows, so a board that leaves the pad on the
RMT signal no longer holds its emitter conducting between frames. The StickS3
emitter wiring was not available in this pass, so its row keeps the previous
non-inverted image and is UNVERIFIED.

### 5. The LED polarity was hardcoded active low

`Feedback::initialize()` passed `true` for `led_active_low` and `setLight()`
derived the level from it, so on the Plus2, where GPIO19 drives an NPN base, the
LED is active high and `setLight(false)` drove the pin high. That keys the IR
emitter through the 22 ohm resistor for as long as the LED is idle.

Fix: `Profile::ledActiveLow` (true for the StickC and Plus, false for the Plus2)
feeds `m_Capabilities.led_active_low`, and `PinShare::ledLevel()` maps the logical
state to the level. The shared-pin arbitration from defect 1 still applies, so a
boot with light feedback enabled now leaves the Plus2 pin driven low rather than
high.

### 6. GPIO35 was described as two sources, not three

The wake-net comments named the MPU6886 INT and the BM8563 INT only. The same
open-drain net also carries the AXP192 IRQ output, and M5Unified's AXP192 init
leaves the power-key IRQ enabled (IRQ enable 3, `0x42 = 0x03`), so every power
button press pulls the line low and the firmware classifies it as motion.

Fix: the comments in `src/FurblePlatform.cpp` and `src/FurbleBoard.cpp` name all
three sources, and the arming and sampling sites say why any low level is treated
as "wake and count activity": masking the AXP192 source would also remove the
`0x46` power-key status that `AXP192_Class::getPekPress()` provides and that
`M5.BtnPWR` feeds the PMIC click streak from, so the power key would go dead while
motion wake is armed. The net cannot be attributed without a schematic, and a
false motion classification only wakes the display, which a button press does
anyway. `docs/power/board-power.md` carries the same correction.

### 7. The StickC Plus denied a timed wake it has

`STICKC_PLUS` was `TimedWake::NONE`. Its BM8563 INT is coupled through R401 and
C401 into the AXP192 power key, and the vendor documents sleep and wake from
v1.1, so an AXP192 shutdown plus a countdown alarm is a timed wake with the same
mechanism as the Plus2.

Fix: the row is `TimedWake::RTC_ALARM`, which reuses the existing Plus2 code path
unchanged: `setAlarmIRQ()`, the `verifyPlus2Timer()` readback, the early-wake
rejection, `consumeTimedWake()`'s `getIRQstatus()` check and the intervalometer
resume window. The one branch that had to change is the sleep itself:
`powerOffUntil()` drives the hold pin low when the row has one (the Plus2) and
calls `powerOff()` for a full AXP192 shutdown when it does not (the Plus). The
shared log strings now say "StickC family RTC" instead of naming the Plus2.

### 8. The StickC and Plus GPS rail is already on before the GPS driver runs

Two writes during `M5.begin()` reach AXP192 EXTEN on the StickC and Plus:

- M5Unified 0.2.20 `M5Unified.cpp` calls `Power.setExtOutput(cfg.output_power)`
  with the default `output_power = true` (`M5Unified.hpp:133`), and
  `Power_Class.cpp` sends that board pair to `Axp192.setEXTEN(enable)`.
- M5GFX 0.2.27 `Light_M5StickC::init()` writes `0x12 = 0x4D`, whose bit 6 is
  EXTEN (M5Unified 0.2.20 `AXP192_Class::setEXTEN()`).

So the Grove 5 V rail is live before furble's GPS driver exists. `GPS::enable()`'s
"power on" comment assumed an off-to-on edge, and a GPS unit on Port A is powered
from boot even when GPS is disabled in settings.

Fix: the rail comment in `src/FurbleBoard.cpp`, the `enable()` comment and the
`beginWindow()` `RAIL_OFF` branch now distinguish "ensure the rail is on" from
"this driver cut and re-raised the rail". Nothing in the acquisition path depends
on the receiver having restarted; the acquisition deadline is anchored to the
`enable()` call. The EXTEN-to-Grove-5V routing stays UNVERIFIED, and cutting the
rail at boot when GPS is disabled is left as an explicit non-change: it would
also remove power from any other device on Port A.

## Evidence

| Fact | Source |
| :--- | :--- |
| `rmt_disable()` does not detach the pad; `rmt_new_tx_channel()` muxes it | ESP-IDF 5.5.3 `components/esp_driver_rmt/src/rmt_tx.c` (`rmt_tx_disable`, `tx_channel->base.gpio_num` set in `rmt_new_tx_channel`) |
| `gpio_output_enable()` re-selects the default output signal | ESP-IDF 5.5.3 `components/esp_driver_gpio/src/gpio.c:215-222` (`gpio_hal_matrix_out_default`, `gpio_hal_func_sel`) |
| `rmt_tx_switch_gpio()` re-selects a channel's TX signal and needs the channel in the init state | ESP-IDF 5.5.3 `rmt_tx.c:1135-1178`, `components/esp_driver_rmt/include/driver/rmt_tx.h:197` |
| The carrier gates the high level, so an inverted output keeps a mark-high encoder correct | ESP-IDF RMT carrier enable (`rmt_ll_tx_enable_carrier_modulation`) plus `rmt_new_tx_channel`'s `esp_rom_gpio_connect_out_signal(..., invert_out, ...)` |
| BM8563/PCF8563 voltage-low is `0x02` bit 7 | M5Unified 0.2.20 `src/utility/rtc/PCF8563_Class.cpp:34-37` |
| AXP192 power-key IRQ is enabled and its status is `0x46` | M5Unified 0.2.20 `Power_Class.cpp` AXP192 register block (`0x42, 0x03`), `AXP192_Class.cpp:275-280` |
| `setExtOutput()` reads VBUS current on Core2/Tough only | M5Unified 0.2.20 `Power_Class.cpp:951-981` |
| AXP192 ADC mask registers and the all-on default | M5Unified 0.2.20 `Power_Class.cpp` (`0x82, 0xFF`, `0x83, 0x80`, `0x84, 0x32`) |
| EXTEN is register `0x12` bit 6, and both M5Unified and M5GFX set it at boot | M5Unified 0.2.20 `AXP192_Class.cpp:228-240`; `M5Unified.cpp` `Power.setExtOutput(cfg.output_power)` with `output_power = true` (`M5Unified.hpp:133`) and `Power_Class.cpp`'s StickC/Plus `Axp192.setEXTEN(enable)`; M5GFX 0.2.27 `M5GFX.cpp:465` (`Light_M5StickC::init`, `0x12 = 0x4D`) |
| The status LED polarity per board | M5Unified 0.2.20 `Power_Class.cpp:1216-1246`: `Light_PWM` config uses `invert = true` on GPIO10 for the StickC and Plus and `invert = false` on GPIO19 for the Plus2, and `Power_Class.cpp:577-585` puts GPIO19 on the output path for the Plus2 at `Power.begin()` |
| StickC and Plus emitter cathode on GPIO9, anode on 3.3 V; Plus2 NPN stage on GPIO19 | StickC Plus schematic page supplied by the second audit pass (`LED1`, `LED_IR`), and the Plus2 NPN stage from the same pass. Not confirmed against a vendor PDF in this tree, and the StickC row follows the Plus page because both boards put the emitter on G9 |

## What stays UNVERIFIED

- The AXP192 register details beyond what M5Unified and M5GFX write. The fetched
  datasheet copy has no revision string, and it was not re-opened in this pass.
  `docs/power/datasheet-register.md` records the gap.
- The StickC and Plus schematics. They are not in the tree, so the
  EXTEN-to-Grove-5V routing stays UNVERIFIED even though `0x12 = 0x4D` and
  EXTEN on bit 6 are library facts. `docs/power/board-power.md` says so at the
  rail table and in PM-04, LK-02 and LK-03.
- A StickC-specific schematic. The IR emitter wiring for that row is copied from
  the StickC Plus page, because both boards put the emitter on G9 and M5Unified
  drives the status LED the same way on both. `ledActiveLow` on all three StickC
  family rows is independently confirmed by M5Unified's own LED `invert` flags.
  Only the emitter side of the StickC row rests on the Plus page.
- The StickS3 emitter polarity. No source in this pass, so its row keeps the
  previous non-inverted image.
- The Plus BM8563-to-AXP192 power-key coupling through R401 and C401, which is
  vendor-documented but not scoped here, and the Plus2 BM8563 retention across
  HOLD, which plan 320 also leaves as a hardware gate.
- Everything electrical: this work has no analyzer, scope or board run. The
  arbitration, polarity and rail changes are code-and-datasheet evidence only.

## Test evidence

| Command | Result |
| :--- | :--- |
| `pin_share_policy_test` | PASS. Ownership, suppression during a frame, restore of the mid-frame level, refused nested begin and hand-over, refused hand-over without a frame, the untouched pin, `ledSharesIr()` and `ledLevel()`. |
| `boundary_shared_pin_test shared` | PASS. Plus2: boot leaves G19 low (active high), a blink during a blocked frame cannot write the pin, the pad returns from the RMT signal, the mid-frame level is restored, and a second frame re-selects the RMT signal once. |
| `boundary_shared_pin_test shared-off` | PASS. Plus2 with light feedback off: a frame parks G19 at the profile's inactive level (low) on the GPIO output path, so the pad is never left floating and a later level write still reaches it. |
| `boundary_shared_pin_test separate` | PASS. StickC: no hand-over, the LED keeps its own pin, and the channel is inverted. |
| `boundary_ir_polarity_{stickc,stickc-plus,stickc-plus2}` | PASS. `invert_out` 1/1/0, emitter mark level 0/0/1, optical duty 333/333/667 permille. |
| `board_profile_test` | PASS. New `ledActiveLow` and `irMarkLow` columns, the Plus2 RTC backing row, the Plus timed-wake row, and the pin-share assertion. |
| `time_keeper_policy_test` | PASS. `rtcUncertaintyMs()` and `rtcValueTrusted()` on both sides. |
| `time_keeper_persistence_test` | PASS. A Plus2 boot with a clear flag is an RTC sample with the 2 s window; a set flag fails closed. |
| `pm1_poll_policy_test` | PASS. The corrected AXP192 mask reason. |
| `boundary_ir_{protocols,psram-fallback,queue-timeout,unsupported}`, `boundary_feedback_{sound,light,overcurrent,low-battery,vibration}` | PASS, unchanged. |
| `python3 tools/pre_review.py --base origin/master` | PASS (see the commit for the exact run). |

## Deviations

- The AXP192 ADC trim is not made per board. The requirement allowed either
  correcting the comment or applying the trim only where the reason holds; this
  work keeps the single mask and states the real reason, because trimming the
  StickC family alone would change that family's ADC enable map on an unverified
  register description. Recorded as a follow-up in PM-04.
- The AXP192 IRQ is not masked while motion wake is armed, for the reason in
  defect 6. The requirement allowed documenting the alternative, and the
  documentation is in the code, `src/CLAUDE.md` and `docs/power/board-power.md`.
- The GPS rail is not cut at boot when GPS is disabled. It is a real saving but
  it changes Port A for every user of the port, so it stays a documented
  consequence rather than a silent behaviour change.
