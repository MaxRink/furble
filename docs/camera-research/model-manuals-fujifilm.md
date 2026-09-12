# Fujifilm model manual and firmware index

Research/retrieval date: 2026-09-12 (Europe/Berlin).

Scope is the 18 Fujifilm models listed by Furble's pinned master README at
[`965f299f0f43c38fe45e4680f1bfb735023533af`](https://github.com/MaxRink/furble/blob/965f299f0f43c38fe45e4680f1bfb735023533af/README.md#L73-L94).
The README is the Furble model-list provenance; Fujifilm's [official manual
selector](https://fujifilm-dsc.com/en-int/manual/) was used to open and verify
each manual URL, and each firmware URL below was opened on the official global
support site, on the retrieval date. All 18 manuals were found. No not-found
URL is being inferred or substituted.

Firmware versions and dates are transcribed as displayed by Fujifilm's pages;
they are a retrieval snapshot, not a promise that they remain current.

| Furble-listed model | Official manual | Manual status | Official firmware/support | Page version (last updated) |
| --- | --- | --- | --- | --- |
| GFX100 II | [Manual](https://fujifilm-dsc.com/en/manual/gfx100ii/) | Found | [Firmware](https://www.fujifilm-x.com/global/support/download/firmware/cameras/gfx100-ii/) | 2.50 (03.26.2026) |
| GFX100RF | [Manual](https://fujifilm-dsc.com/en/manual/gfx100rf/) | Found | [Firmware](https://www.fujifilm-x.com/global/support/download/firmware/cameras/gfx100rf/) | 1.13 (06.30.2026) |
| GFX100S | [Manual](https://fujifilm-dsc.com/en/manual/gfx100s/) | Found | [Firmware](https://www.fujifilm-x.com/global/support/download/firmware/cameras/gfx100s/) | 2.13 (02.27.2025) |
| GFX100S II | [Manual](https://fujifilm-dsc.com/en/manual/gfx100s-ii/) | Found | [Firmware](https://www.fujifilm-x.com/global/support/download/firmware/cameras/gfx100s-ii/) | 1.20 (09.02.2025) |
| GFX50S II | [Manual](https://fujifilm-dsc.com/en/manual/gfx50s-ii/) | Found | [Firmware](https://www.fujifilm-x.com/global/support/download/firmware/cameras/gfx50s-ii/) | 2.12 (07.09.2024) |
| X-E4 | [Manual](https://fujifilm-dsc.com/en/manual/x-e4/) | Found | [Firmware](https://www.fujifilm-x.com/global/support/download/firmware/cameras/x-e4/) | 2.01 (06.15.2023) |
| X-E5 | [Manual](https://fujifilm-dsc.com/en/manual/x-e5/) | Found | [Firmware](https://www.fujifilm-x.com/global/support/download/firmware/cameras/x-e5/) | 1.12 (06.04.2026) |
| X-H1 | [Manual](https://fujifilm-dsc.com/en/manual/x-h1/) | Found | [Firmware](https://www.fujifilm-x.com/global/support/download/firmware/cameras/x-h1/) | 2.15 (06.23.2026) |
| X-H2S | [Manual](https://fujifilm-dsc.com/en/manual/x-h2s/) | Found | [Firmware](https://www.fujifilm-x.com/global/support/download/firmware/cameras/x-h2s/) | 7.30 (12.02.2025) |
| X-S10 | [Manual](https://fujifilm-dsc.com/en/manual/x-s10/) | Found | [Firmware](https://www.fujifilm-x.com/global/support/download/firmware/cameras/x-s10/) | 3.11 (03.28.2024) |
| X-S20 | [Manual](https://fujifilm-dsc.com/en/manual/x-s20/) | Found | [Firmware](https://www.fujifilm-x.com/global/support/download/firmware/cameras/x-s20/) | 3.30 (07.22.2025) |
| X-T200 | [Manual](https://fujifilm-dsc.com/en/manual/x-t200/) | Found | [Firmware](https://www.fujifilm-x.com/global/support/download/firmware/cameras/x-t200/) | 1.16 (07.27.2022) |
| X-T3 | [Manual](https://fujifilm-dsc.com/en/manual/x-t3/) | Found | [Firmware](https://www.fujifilm-x.com/global/support/download/firmware/cameras/x-t3/) | 5.11 (03.28.2024) |
| X-T30 | [Manual](https://fujifilm-dsc.com/en/manual/x-t30/) | Found | [Firmware](https://www.fujifilm-x.com/global/support/download/firmware/cameras/x-t30/) | 2.01 (07.04.2023) |
| X-T4 | [Manual](https://fujifilm-dsc.com/en/manual/x-t4/) | Found | [Firmware](https://www.fujifilm-x.com/global/support/download/firmware/cameras/x-t4/) | 2.12 (07.09.2024) |
| X-T5 | [Manual](https://fujifilm-dsc.com/en/manual/x-t5/) | Found | [Firmware](https://www.fujifilm-x.com/global/support/download/firmware/cameras/x-t5/) | 4.31 (07.26.2025) |
| X100V | [Manual](https://fujifilm-dsc.com/en/manual/x100v/) | Found | [Firmware](https://www.fujifilm-x.com/global/support/download/firmware/cameras/x100v/) | 3.01 (07.04.2023) |
| X100VI | [Manual](https://fujifilm-dsc.com/en/manual/x100vi/) | Found | [Firmware](https://www.fujifilm-x.com/global/support/download/firmware/cameras/x100vi/) | 1.31 (07.26.2025) |

## Confirmed wireless-security notes

Only the following model-specific security notes are included because the
official firmware pages explicitly state them:

- **X-E5:** Firmware 1.10 changed wireless security and the smartphone pairing
  procedure. Fujifilm says the iOS Camera Remote app cannot connect after that
  update, and Android devices/tablets using Bluetooth 4.1 or earlier cannot
  pair over Bluetooth; the documented minimum is Bluetooth 4.2. Use XApp as
  directed by Fujifilm. The current page reports 1.12.
- **X100VI:** Firmware 1.30 changed wireless security and smartphone pairing;
  the page repeats the Camera Remote/XApp and Android Bluetooth 4.2 minimum
  cautions. The current page reports 1.31. This is smartphone/app guidance,
  not evidence of Furble GATT compatibility or a universal camera protocol.

These pages document Fujifilm's app-facing behavior only. They do not publish
the private Furble/XApp GATT UUIDs or shutter bytes, and they do not turn a
model-level Furble README entry into firmware-wide certification.
