# 335 Display and touch datasheet promotions

Status: implemented as a documentation change. Every date below is 2026-11-26,
the local date of the reading pass.

## Motivation

The display and touch rows in `docs/power/datasheet-register.md`,
`docs/power/display.md`, `docs/power/board-power.md` and
`plans/322-hardware-abstraction.md` were written from a mixture of the repo
model, second-hand part names and one mislabelled mirror. Four problems were
carried in the corpus:

1. The StickS3 panel document was listed as unknown, and the ST7789 family sleep
   current was copied to every variant. The ST7789P3 sleep current is not the
   ST7789V value.
2. The ILI9342C row said the cited copy had no revision string. The copy is
   complete and carries a version and a document number.
3. ST7796 was attributed to Sino Wealth and described as if the U added MIPI
   DSI over the S. Sitronix makes ST parts, Sino Wealth makes SH parts, and both
   the U and the S document carry MIPI DSI. The confusion started with a host
   file named `ST7796S_spec.pdf` that contains the ST7796U document.
4. Two panels had no document at all in the corpus, the CO5300 AMOLED driver and
   the CST9217 touch controller, and the SH1106 file was treated as if a vendor
   copy could still be found.

This plan records the promotion of each document, the exact page each number
came from, and the items that could not be verified and why.

## Method

For each document: download the file, compute `shasum -a 256`, read the page
count with `pdfinfo`, extract text with `pdftotext -layout` page by page, then
open the page that the claim cites and read it. No number below was copied from
another document, a search snippet, or the repo model. No vendor PDF was written
into this repository, and none may be.

## Promotions

| Part | Document and version | Pages | sha256 | Pages read | Written into |
| --- | --- | --- | --- | --- | --- |
| Sino Wealth SH1106 | V2.3, June 2013, Pololu mirror, `https://www.pololu.com/file/0J1813/SH1106.pdf` | 49 | ab5ce5f3 | 35, 49 | register section 2, `display.md` reference list, plan 322 chip table |
| Sino Wealth SH1106 | V2.3 OCR copy, Crystalfontz, `https://www.crystalfontz.com/controllers/uploaded/SH1106-Revision_2.3.pdf` | 49 | 3baf1812 | metadata and text comparison | register section 2 |
| Hynitron CST9217 | V1.0, 2022-08-11, `docs/CST9217.pdf` at `ScripTerasu/cst92xx-touch-driver` commit `0e4cede3` | 10 | 74cf16e6 | 1, 5, 8, 9, 10 | register section 2, `display.md` D-013 and reference list, plan 322 chip table and per-board gaps |
| Sitronix ST7796U | V1.0, 2020/02, `https://www.smartdisplaytech.com/wp-content/uploads/2023/03/ST7796S_spec.pdf` | 287 | c907c64e | 1, 3, 6, 163, 164 | register section 2, plan 322 chip table |
| Sitronix ST7796S | V1.0, 2014/11, `https://dl.espressif.com/dl/schematics/ST7796S_SPEC_V1.0.pdf` | 279 | 212f7a8e | 1, 3, 159, 160 | register section 2 |
| Sitronix ST7789P3 | v0.1, 2022/03, `https://files.waveshare.com/wiki/ESP32-S3-GEEK/ST7789P3.pdf` | 276 | 26e62a24 | 34, 41, 143, 145 | register section 2 and 13.1, `display.md` board table and reference list, `board-power.md` section 7, plan 322 chip table |
| Sitronix ST7789V | v1.3, 2014/03, `https://newhavendisplay.com/content/datasheets/ST7789V.pdf` | 316 | 8ecf0e43 | 40, 48, 182, 184 | register section 2, `display.md` reference list, `board-power.md` section 7, plan 322 display backend |
| Sitronix ST7789V2 | v1.0, 2016/11, `https://files.waveshare.com/upload/c/c9/ST7789V2.pdf` | 319 | 89376784 | 40, 48, 184, 186 | register section 2, `display.md` reference list, plan 322 display backend |
| Sitronix ST7735S | v1.1, 2011-11-21, `https://cdn.sparkfun.com/assets/9/0/2/5/8/ST7735S_v1.1.pdf` | 201 | 0322a527 | 30 | register section 2 and 13.1, `display.md` reference list, `board-power.md` section 7 |
| ILITEK ILI9342C | V101, document `ILI9342C_DTS_V101_20111214`, `https://m5stack.oss-cn-shenzhen.aliyuncs.com/resource/docs/datasheet/core/ILI9342C-ILITEK.pdf` | 235, complete | 604a35c0 | 100, 101, 227, 235 | register section 2 and 13.1, `display.md` board table, dwell bullet and reference list, `board-power.md` section 7, plan 322 display backend |
| ILITEK ILI9341 | V1.11, document `ILI9341_DS_V1.11.pdf`, `https://cdn-shop.adafruit.com/datasheets/ILI9341.pdf` | 245 | a9bbfdf6 | 1, 236 | register section 2 |
| Chipone CO5300 | V0.01, 2023/07/24, `https://files.waveshare.com/wiki/common/Co5300_Datasheet.pdf` | 226 | 2ad0461a | 1, 2, 103, 117, 226 | register section 2, `display.md` reference list, `board-power.md` section 7, plan 322 chip table |
| Chipone CO5300 | V0.00, 2023/03/28, `https://files.waveshare.com/wiki/common/CO5300_Datasheet_V0.00.pdf` | 226 | 20161057 | 2, 103 | register section 2 |

Supporting sources that are not datasheets, all read on 2026-11-26:

- Sitronix AIoT device DDI table, `https://www.sitronix.com.tw/en/products/aiot-device-ddi/`,
  lists ST7796U, ST7796UI, ST7796HU and ST7796U2 and does not list ST7796S.
- LilyGo T-Lora Pager hardware document, `docs/hardware/lilygo-t-lora-pager.md`
  in `Xinyuan-LilyGO/LilyGoLib`, gives the driver IC as ST7796U over SPI with
  480 x 222, 2.33 inch and 262K colours. The wiki summary page says "ST7796"
  without the suffix, so the suffixed name comes from the hardware document.
- Zephyr `drivers/input` carries only `input_cst8xx.c` and `Kconfig.cst8xx`,
  supporting CST816S, CST816T, CST816D and CST820 by chip ID. There is no
  CST92xx driver.
- The MIT `cst92xx` Rust crate, `Cargo.toml` `license = "MIT"`, `src/registers.rs`
  and `src/protocol.rs` at commit `0e4cede3`, is the only driver source used for
  the touch register list. Its README states it was ported from the MIT
  SensorLib C++ driver.

## What the re-read changed

- ILI9342C is no longer "no revision string in the cited copy". The copy is
  V101, document `ILI9342C_DTS_V101_20111214`, 235 pages and complete at 235 of
  235, with a 2011/12/26 item in the revision history on page 235. The absent
  supply current table is real: section 18.2 page 227 gives only plus or minus
  0.1 uA logic input leakage.
- The ILI9342C dwell is 120 ms in both directions, from sections 8.2.11 and
  8.2.12 on pages 100 and 101. The ST7789 and ST7796 documents need 120 ms from
  SLPOUT to the next SLPIN only, plus 5 ms after SLPIN and after SLPOUT. The
  corpus had both a one-way claim and a claim that applied the two-way rule to
  every panel. Neither is restored here. `UI::sleepDisplay()` and
  `UI::wakeDisplay()` enforce both waits through one 120 ms constant, which is
  correct for every panel.
- ST7789P3 sleep current is 20 uA typical and 50 uA maximum (Table 3 page 34),
  not the 15 uA and 30 uA of ST7789V and ST7789V2 (their Table 3 page 40).
  Working current is the same 6.0 mA typical and 7.5 mA maximum on all three.
- The ST7789P3 reset timing is Table 8 on page 41. ST7789V and ST7789V2 carry
  the same rows as Table 9 on page 48. TRW is 10 us minimum, TRT is 120 ms
  maximum in both.
- ST7796U and ST7796S both carry MIPI DSI in section 8.5. The U document is the
  16.7M-colour, 287-page one at V1.0 2020/02; the S document is the
  262K-colour, 279-page one at V1.0 2014/11. DSI does not separate them.
- ST7796 is a Sitronix part. Sitronix uses ST, Sino Wealth uses SH. The
  "Sino Wealth ST7796S" attribution is gone.
- SH1106 sleep current is 5 mA maximum on the VDD1 and VDD2 row and 5 mA
  maximum on the external VPP row, both at TA 25 C, page 35. It must never be
  rendered as microamps.
- CO5300 V0.01 adds the deep stand-by row that V0.00 lacks: IDSTB 2 uA VDDI plus
  1 uA VCI on page 103. The V0.01 revision history entry is "Update Deep
  Stand-by mode current", 2023/07/24.
- CST9217 reset timing is TPOR 100 ms, TPR 1 ms, TRON 100 ms and TRST 0.1 ms on
  page 9. The 1 ms belongs to TPR, the RST pin delay, not to TPOR.

## Licence constraints, recorded so the next pass does not break them

- Sitronix and ILITEK print the same sentence on their pages: the document
  "shall not be distributed, reproduced, or disclosed in whole or in part
  without prior written permission" of the vendor.
- Chipone's Important Notice on CO5300 page 226 and Hynitron's declaration on
  CST9217 page 10 grant no redistribution right.
- Therefore this corpus stores the URL, the version, the page count and the
  hash. It never stores a copy, and no vendor PDF is committed.
- Linux `drivers/input/touchscreen/hynitron_cstxxx.c` is `GPL-2.0-only`. The
  ESPHome CST92xx component is C++ under the ESPHome licence, whose C++ half is
  GPLv3. Neither is compatible with this repository's MIT licence, so neither
  may be copied from or consulted for a register value. The MIT `cst92xx` Rust
  crate is the permitted source and is attributed by name, commit and file.
- Where a register can only be learned from a driver, the provenance is written
  next to it. The CST9217 datasheet carries no register map at all, so every
  register address in the corpus is code-derived and labelled that way.

## Items that stay unverified

| Item | Reason |
| --- | --- |
| Any vendor copy of the SH1106 datasheet | `sino-wealth.com` returns NXDOMAIN and its Wayback captures are HTML only. The live `sinowealth.com` is an MCU vendor site: 106 archived PDFs, none an SH1106. |
| `buydisplay.com/download/ic/SH1106.pdf` as a second SH1106 copy | The host answers 403 to an automated fetch, so the file was never read. |
| The ST7796 controller datasheet page on orientdisplay.com | 403 to an automated fetch. |
| ILITEK's own site as the primary ILITEK source | `ilitek.com` answers with no address record and `www.ilitek.com` is NXDOMAIN, so the document is cited through the M5Stack mirror only. |
| Hynitron's own copy of the CST9217 datasheet | Hynitron's product page for the part, `http://www.hynitron.com/product/wearable/204`, lists the chip and links no datasheet PDF. The only copy found is the one committed in the MIT `cst92xx` crate, which itself carries a Shenzhen distributor's watermark on every page. |
| An FCC ID for any CST9217 board | `fcc.gov/oet/ea/fccid` and `fccid.io` both answer 403, so no exhibit list could be read. |
| The FT6336U touch datasheet | Not part of this pass. It stays a gap in register section 11 and in `display.md` D-013. |
| A datasheet supply current for the ILI9342C | The document has no such table. Section 18.2 page 227 has only the 0.1 uA logic leakage, so the model keeps an ST7789V analogy tagged `estimated`. The 15 to 100 uA sleep-in band therefore stays an analogy, and the ILI9341 standby ceiling on page 236 stays the only hard upper bound in the corpus. |
| A full-finger-count register separate from the report | The datasheet has no register map, and the MIT crate decodes the count from byte 5 of the 15-byte read from 0xD000, which is address 0xD005. That is the strongest available statement and is what the docs say. |
| A CST9217 current figure at 3.6 V or at 25 C outside the datasheet condition | The datasheet gives typicals only, at VDDA 2.8 V and 25 C. There is no maximum column for the three operating modes. |
| A measured display or touch current on this project's hardware | No hardware measurement exists for the display lane. Everything in `display.md` is a model estimate, a datasheet line or an inference, and plan 158 still gates any absolute claim. |

## Follow-up

- A hardware pass can promote the CST9217 monitor-mode item from datasheet to
  measured once a Watch Ultra or Pager is on the bench.
- If Sitronix ever publishes ST7796S on its own AIoT table, recheck the vendor
  list before changing the attribution sentence.
- Keep the per-panel dwell rule in one place. A new panel class must add its own
  row to the dwell paragraph in `plans/322-hardware-abstraction.md` and to the
  bullet in `docs/power/display.md`.
