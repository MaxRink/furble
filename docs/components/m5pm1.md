# M5Stack M5PM1

**Part and package.** M5Stack M5PM1, a power management chip with a hardware
watchdog, power button gestures, GPIO wake, a status LED, a 5 V boost and a
3.3 V rail. Package and pinout were not read for this page and are UNVERIFIED.

**Role in furble.** The power-management chip on the StickS3. furble drives it
through the pinned M5PM1 library, not through `lib/hal`, so the facts below are
about the library calls and the register semantics the firmware relies on.

**Documents.** [M5PM1-UM]

## Documents used

| Key | Title | Revision or date | URL | Retrieved | Opened in this change |
| :--- | :--- | :--- | :--- | :--- | :--- |
| M5PM1-UM | M5PM1 Chip User Manual, Shenzhen Mingzhan Information Technology Co., Ltd. | V 1.9 | <https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1207/M5PM1_Datasheet_EN.pdf> | 2026-11-25 | Title page, revision string, and the register names in the register list |

The manual numbers its registers in a single list, with the register name and
the address in adjacent cells rather than as numbered subsections. That is why
this page cites register names and page-level sections, not section numbers,
for the register facts.

## Facts furble depends on

| # | Fact | Document and section | Code | State |
| :--- | :--- | :--- | :--- | :--- |
| 1 | `PWR_CFG` is register 0x06 and its default 0x17 means LED enable, 5 V boost off, 3.3 V LDO on, 3.3 V DCDC on and charge on | M5PM1-UM register list, `PWR_CFG` 0x06 | No direct write; the library's `setLedEnLevel` path (register name read, bit meanings CITED) | CITED |
| 2 | `I2C_CFG` is register 0x09 and its `SLP_TO` field is bits 3:0, where 0 disables idle sleep | M5PM1-UM register list, `I2C_CFG` 0x09 | `src/FurblePlatform.cpp:159` and the `m5pm1Access` retry path | CITED |
| 3 | The first I2C transaction after idle sleep fails and only wakes the chip, so every access is retried once | Firmware observation recorded in `CLAUDE.md`, hardware traps | `src/FurblePlatform.cpp:171`, `:177`, `:201`, `:221`, `:241` (firmware observation, no datasheet section) | CITED |
| 4 | `WDT_CNT` is 0x0A in seconds with 0 disabling the watchdog, and `WDT_KEY` at 0x0B is fed 0xA5 | M5PM1-UM register list, `WDT_CNT` 0x0A and `WDT_KEY` 0x0B | `src/FurblePlatform.cpp:196`, `:241` (0x0B and 0xA5 verified, the rest CITED) | CITED |
| 5 | `SYS_CMD` is 0x0C and 0xA1 means power off | M5PM1-UM register list, `SYS_CMD` 0x0C | `src/FurblePlatform.cpp` power-off path (register verified, 0xA1 CITED) | CITED |
| 6 | `HOLD_CFG` at 0x07 retains the 5 V boost, LDO and GPIO state | M5PM1-UM register list, `HOLD_CFG` 0x07 | Library call, no direct use in furble | CITED |
| 7 | The status LED stays lit at display off unless it is cleared, which saves `PWR_CFG` bit 4 | `CLAUDE.md`, hardware traps | `src/FurbleFeedback.cpp:306` to `:312` | CITED |
| 8 | The BMI270 interrupt reaches the chip on GPIO4 on the StickS3 | M5Stack StickS3 low-power page | `src/FurbleBoard.cpp:17` | CITED |

## Where the code implements it

- `src/FurblePlatform.cpp:125` to `:127`: `begin()`, then single-click reset and
  double-click power off are disabled so a button gesture cannot reset the
  board under the UI.
- `src/FurblePlatform.cpp:171` to `:241`: download recovery, watchdog readback
  and the watchdog set path, all through the retrying access helper.
- `src/FurbleFeedback.cpp:306` to `:312`: wakes the chip before the amplifier
  access that follows an idle sleep.
- `include/FurblePlatform.h:104` to `:115`: the watchdog feed, the wake helper
  and the retry contract.
- `src/FurbleConsole.cpp:2484`: reports the retry count so a bench session can
  see the idle-sleep behaviour.

## Citation gaps

| Constant or fact | Driver location | Why no section | Page row |
| :--- | :--- | :--- | :--- |
| none | - | The M5PM1 is not a `lib/hal` driver, so the gate does not scan it. Its gaps are the rows below. | - |

## Facts still unverified

- Package and pinout.
- Section numbers and page numbers for the register list. The register names and
  addresses were read, the numbering scheme was not.
- The `PWR_CFG` default value 0x17 and the meaning of bit 4 for the LED.
- The `SLP_TO` field position and the idle-sleep timeout in seconds.
- The 0xA1 power-off command value.
- The watchdog timeout units and the maximum value.
- The "first transaction fails after sleep" behaviour is a firmware
  observation. No datasheet statement confirms it.

## Redistribution

The manual carries no licence statement in its front matter. A silent licence is
a refusal under the rule in [README.md](README.md#committed-documents), so the
PDF is cited and not stored.

[M5PM1-UM]: https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1207/M5PM1_Datasheet_EN.pdf
