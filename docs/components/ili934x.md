# ILITEK ILI934x

**Part and package.** ILITEK ILI9342C and ILI9341, TFT controller and driver
chips with frame memory, COG bumped die. The panel module and its glass are board
specific and were not read, so they are UNVERIFIED.

**Role in furble.** The panel on the Core2 and on the Core. The pinned board
support library drives it. furble owns the same sleep dwell as for the ST7789,
because the trap covers both families (`CLAUDE.md`, hardware traps).

**Documents.** [ILI9342C-DS], [ILI9341-DS]

## Documents used

| Key | Title | Revision or date | URL | Retrieved | Opened in this change |
| :--- | :--- | :--- | :--- | :--- | :--- |
| ILI9342C-DS | ILI9342C a-Si TFT LCD Single Chip Driver 320RGBx240 Resolution and 262K color Datasheet, document ILI9342C_DTS_V101_20111214 | V101 | <https://m5stack.oss-cn-shenzhen.aliyuncs.com/resource/docs/datasheet/core/ILI9342C-ILITEK.pdf> | 2026-11-25 | Version string and the two sleep command entries |
| ILI9341-DS | ILI9341 datasheet, as cited in `docs/power/datasheet-register.md` | No revision string in the cited copy | <https://cdn-shop.adafruit.com/datasheets/ILI9341.pdf> | not retrieved | Nothing |

## Facts furble depends on

| # | Fact | Document and section | Code | State |
| :--- | :--- | :--- | :--- | :--- |
| 1 | Enter Sleep Mode is command 10h | ILI9342C-DS section 8.2.11, page 100 | Reached through `M5.Display.sleep()` at `src/FurbleUI.cpp:1863` | VERIFIED |
| 2 | Sleep Out is command 11h | ILI9342C-DS section 8.2.12, page 101 | Reached through `M5.Display.wakeup()` at `src/FurbleUI.cpp:1897` | VERIFIED |
| 3 | Sleep Out requires 120 ms before the next Sleep In, and 5 ms after Sleep Out before any other command. The rule is one way, from Sleep Out to the next Sleep In | ST7796S-DS sections 9.2.12 and 9.2.13, which the pass reports applies to this family too | `src/FurbleUI.cpp:1888` to `:1897` enforces a wait in the other direction | REPORTED |
| 4 | Sleep In draws 15 uA typical and 30 uA maximum | ILI9341-DS standby ceiling, used as the ILI9342C high bound in `docs/power/datasheet-register.md` | `tools/power-model/board-currents.yaml` (pairing with the ILI9342C is a proxy) | CITED |
| 5 | The ILI9342C has no supply current table, so the power model uses an ST7789V analogy for operating current | `docs/power/datasheet-register.md` section 2 | `tools/power-model/board-currents.yaml` | CITED |

## Where the code implements it

- `src/FurbleUI.cpp:1860` to `:1863`: waits for the DMA queue, then Sleep In.
- `src/FurbleUI.cpp:1888` to `:1897`: dwell, APB lock, Sleep Out.
- `include/FurbleUI.h:1136`: the dwell constant.

## Citation gaps

| Constant or fact | Driver location | Why no section | Page row |
| :--- | :--- | :--- | :--- |
| none | - | No `lib/hal` driver. The panel commands are issued by the board support library. | - |

## Facts still unverified

- The sleep timing sections of ILI9342C V101. The two command entries were read;
  the timing requirement was not. The one-way timing rule on
  [st7789.md](st7789.md#the-sleep-timing-rule) is REPORTED and applies here by the
  pass's statement, not by a section of the ILI9342C document.
- The ILI9341 standby current, and whether a 9342C may be assumed to match it.
- Which of the two parts each board carries, and the panel resolution that M5GFX
  selects.
- The full ILI9341 revision and its command list. That copy was not opened.

## Redistribution

The ILI9342C copy carries an ILITEK copyright notice with no redistribution
grant. The ILI9341 URL is a distributor mirror, and a mirror copy is never
committed. Both are cited and not stored, per the rule in
[README.md](README.md#committed-documents).

[ILI9342C-DS]: https://m5stack.oss-cn-shenzhen.aliyuncs.com/resource/docs/datasheet/core/ILI9342C-ILITEK.pdf
[ILI9341-DS]: https://cdn-shop.adafruit.com/datasheets/ILI9341.pdf
