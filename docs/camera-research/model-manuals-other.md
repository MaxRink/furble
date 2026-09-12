# Furble camera manual and support URL index

Retrieved 2026-09-12. This is a documentation addendum, not a protocol
specification. It indexes manufacturer-hosted manuals, support pages, and app
documentation for the non-Fujifilm models named in the current README test
inventory, followed by explicitly separate research or experimental models.

The README inventory is pinned to
[`MaxRink/furble` commit `965f299f0f43c38fe45e4680f1bfb735023533af`](https://github.com/MaxRink/furble/blob/965f299f0f43c38fe45e4680f1bfb735023533af/README.md#L73-L106). A README test claim is not a manufacturer certification. A manual or
support page establishes the camera's documented feature surface, not Furble
compatibility, BLE UUIDs, packet formats, pairing behavior, or timing.

## README-tested non-Fujifilm models

| Model | Exact official manual or support entry | Useful official companion entry | Scope and notes |
|---|---|---|---|
| Canon EOS M6 | [Canon USA EOS M6 support](https://www.usa.canon.com/support/p/eos-m6); [EOS M6 User Guide PDF](https://gdlp01.c-wss.com/gds/0/0300026140/01/eosm6-cu-en.pdf) | Support page includes Camera Connect and firmware/manual downloads | Canon support entry. Firmware and regional manual revisions can differ. |
| Canon EOS R6 Mark II | [Canon USA EOS R6 Mark II support](https://www.usa.canon.com/support/p/eos-r6-mark-ii); [Canon online Advanced User Guide](https://cam.start.canon/en/C012/manual/html/index.html); [PDF guide](https://cam.start.canon/en/C012/manual/c012.pdf) | [Canon camera manual landing page](https://cam.start.canon/en/C012/) | The current online guide says it applies to firmware 1.7.0 or later. Do not treat that as the firmware used by the Furble test. |
| Canon EOS RP | [Canon USA EOS RP support](https://www.usa.canon.com/support/p/eos-rp); [EOS RP Advanced User Guide PDF](https://gdlp01.c-wss.com/gds/8/0300033698/05/eosrp-ugsp5-en.pdf) | Support page includes Camera Connect and firmware/manual downloads | Canon support entry. Exact guide revision is encoded in the PDF URL. |
| Canon PowerShot G9 X Mark II | [Canon USA PowerShot G9 X Mark II support](https://www.usa.canon.com/support/p/powershot-g9-x-mark-ii); [Canon wireless-help PDF](https://www.cla.canon.com/en_US/app/pdf/wireless_help/powershot/psg9x-m2.pdf) | Support page includes firmware and manuals | Canon support entry. The wireless-help document is about the camera's documented wireless workflow, not a BLE reverse-engineering reference. |
| Nikon COOLPIX B600 | [Nikon Download Center: COOLPIX B600](https://downloadcenter.nikonimglib.com/en/products/510/COOLPIX_B600.html); [B600 Reference Manual PDF](https://download.nikonimglib.com/archive5/tApXH00yzxHt05yzmRA25H96G458/B600RM_%28En%2905.pdf) | The Download Center also exposes the [SnapBridge Connection Guide](https://downloadcenter.nikonimglib.com/en/products/510/COOLPIX_B600.html) and firmware 1.1 entry | Nikon page lists the English Reference Manual, connection guide, and firmware version 1.1. The official manual explicitly documents the ML-L7 remote. See the note below. |
| Nikon Z6 III | [Nikon Download Center: Z6III](https://downloadcenter.nikonimglib.com/en/products/629/Z6III.html) | The page lists the English User's Manual, Reference Guide, firmware C 2.00, and other guides. Nikon's [ML-L7 product page](https://nij.nikon.com/products/lineup/accessory/remote/ml-l7/) lists Z6III as compatible. | Nikon support/manual landing page. Use the camera's current firmware and regional manual from this page when checking behavior. |
| Sony ZV-1F | [Sony USA ZV-1F support](https://www.sony.com/electronics/support/compact-cameras-zv-series/zv-1f); [Sony ZV-1F getting-started guide](https://www.sony.com/electronics/support/articles/00288874) | [Sony Creators' App article for ZV-1F](https://www.sony.com/electronics/support/articles/00286857) documents pairing, remote shooting, transfer, and location information | Sony support page exposes the Help Guide, PDF, startup guide, and firmware. The page currently shows system software 2.01, but that is not evidence of the Furble test firmware. |
| Ricoh GR IV HDF | [Ricoh GR IV support](https://www.ricoh-imaging.co.jp/japan/support/detail/gr-4/); [Ricoh GR IV FAQ](https://www.ricoh-imaging.co.jp/english/support/qa/gr-4/); [Ricoh manual index](https://www.ricoh-imaging.co.jp/english/support/download_manual.html) | [GR IV firmware page](https://www.ricoh-imaging.co.jp/english/support/digital/gr4_s.html); [GR WORLD connection guide](https://www.ricoh-imaging.co.jp/english/products/app/gr-world/connect.html) | The official GR IV support and firmware pages cover both GR IV and GR IV HDF where stated. Select the HDF manual from the manual index rather than guessing a localized PDF filename. |

## Separate research or experimental models

These rows are not additional README-tested claims. They are starting points
for future host tests, simulators, or documentation work. No behavior should be
inferred merely because a product has Bluetooth, Wi-Fi, or a companion app.

| Model | Exact official manual or support entry | Documented app or connection entry | Research status |
|---|---|---|---|
| Ricoh GR III | [GR III Operating Manual PDF](https://www.ricoh-imaging.co.jp/english/support/man-pdf/gr-3.pdf); [GR III FAQ](https://www.ricoh-imaging.co.jp/english/support/qa/gr-3/); [firmware page](https://www.ricoh-imaging.co.jp/english/support/digital/gr3_s.html) | [Image Sync app page](https://www.ricoh-imaging.co.jp/english/products/app/image-sync2/) | Experimental only. The official FAQ and app pages describe Image Sync and location-information recording. They do not establish a Furble BLE command contract. |
| Ricoh GR IIIx | [GR III/GR IIIx Operating Manual PDF](https://www.ricoh-imaging.co.jp/english/support/pdf/gr-3_3x_EN.pdf); [GR III/GR IIIx FAQ](https://www.ricoh-imaging.co.jp/english/support/qa/gr-3/); [firmware page](https://www.ricoh-imaging.co.jp/english/support/digital/gr3x_s.html) | [Image Sync app page](https://www.ricoh-imaging.co.jp/english/products/app/image-sync2/) | Experimental only. Keep GR IIIx as its own model profile even where the manual groups it with GR III. |
| Ricoh GR II | [GR II Operating Manual PDF](https://www.ricoh-imaging.co.jp/english/support/man-pdf/gr-2.pdf); [GR II FAQ](https://www.ricoh-imaging.co.jp/english/support/qa/gr-2/); [firmware page](https://www.ricoh-imaging.co.jp/english/support/digital/gr2_s.html) | [GR II product page](https://www.ricoh-imaging.co.jp/english/products/gr_special/); [GR WORLD connection notes](https://www.ricoh-imaging.co.jp/english/products/app/gr-world/connect.html) | Experimental only. The official GR II material describes Wi-Fi and Image Sync/GR Remote-era workflows. Do not equate that with the GR III or GR IV BLE transport. |
| Ricoh GR IV (non-HDF) | [GR IV support](https://www.ricoh-imaging.co.jp/japan/support/detail/gr-4/); [GR IV FAQ](https://www.ricoh-imaging.co.jp/english/support/qa/gr-4/); [manual index](https://www.ricoh-imaging.co.jp/english/support/download_manual.html) | [GR WORLD connection guide](https://www.ricoh-imaging.co.jp/english/products/app/gr-world/connect.html); [GR IV firmware page](https://www.ricoh-imaging.co.jp/english/support/digital/gr4_s.html) | Experimental comparison target. Ricoh's firmware page explicitly names GR IV and GR IV HDF, but shared documentation does not prove identical transport behavior. |
| Panasonic LUMIX S5II (DC-S5M2) | [Panasonic S5II Operating Instructions portal](https://av.jpn.support.panasonic.com/support/dsc/oi/S5M2/index.html); [English complete-guide PDF](https://www.panasonic.com/content/dam/Panasonic/sg/en/PDF/DC-S5/Operating-Instructions-DC-S5M2K.pdf); [support page](https://panasonic.jp/dc/products/DC-S5M2/support.html) | Panasonic's manual and support material refer to LUMIX Sync and USB/LAN-related camera workflows | Experimental only. Exact remote surface and transport must be checked against the camera firmware and regional manual. |
| Panasonic LUMIX BGH1 (DC-BGH1) | [Panasonic BGH1 Operating Instructions portal](https://av.jpn.support.panasonic.com/support/dsc/oi/BGH1/index.html); [English advanced manual PDF](https://www.panasonic.com/content/dam/Panasonic/au/en/PDF/DC-BGH1GC-advanced-user-manual.pdf); [support page](https://av.jpn.support.panasonic.com/support/dsc/product/dc_bgh1.html) | [BGH1 compatibility/support page](https://av.jpn.support.panasonic.com/support/global/cs/dsc/connect/bgh1.html) | Experimental only. The manual documents a monitor-less body and LUMIX Tether, plus wired/network interfaces. It is not evidence of BLE shutter support. |
| Panasonic LUMIX G9II (DC-G9M2) | [G9II model support](https://av.jpn.support.panasonic.com/support/dsc/product/dc_g9m2.html); [English complete-guide PDF](https://help.na.panasonic.com/wp-content/uploads/2024/02/DCG9M2_DVQP3010ZA_V02.00_ENG.pdf) | [G9II compatibility page](https://av.jpn.support.panasonic.com/support/global/cs/dsc/connect/g9m2.html); [G9II firmware/update notes](https://av.jpn.support.panasonic.com/support/global/cs/dsc/download/fts/dl/g9m2.html) | Experimental only. Panasonic's update notes mention LUMIX Lab remote shooting and shutter control for later firmware, but that is an app/API feature claim, not a Furble implementation claim. |
| DJI Osmo Action 4 | [DJI Action 4 support](https://www.dji.com/support/product/osmo-action-4); [Action 4 downloads and User Manual](https://www.dji.com/osmo-action-4/downloads) | Support page documents DJI Mimo control, activation, Wi-Fi/Bluetooth, and the GPS Bluetooth Remote Controller | Experimental only. DJI documents BLE 5.0 and Wi-Fi at the product level, but no public Furble-compatible command schema is established here. |
| DJI Osmo Action 5 Pro | [DJI Action 5 Pro support](https://www.dji.com/support/product/osmo-action-5-pro); [Action 5 Pro downloads and User Manual](https://www.dji.com/osmo-action-5-pro/downloads) | Support page documents the DJI Mimo workflow and lists manuals, beginner's guide, and accessories | Experimental only. DJI's support page currently identifies BLE 5.1 and Wi-Fi 6 for this model. That does not imply Action 4 protocol compatibility. |

## Nikon COOLPIX B600 accessory clarification

The app/SnapBridge connection table is not the right source for deciding ML-L7
support. Nikon's official B600 Reference Manual has a dedicated “ML-L7 Remote
Control” section and states that the separately available ML-L7 can be paired
with the camera. It limits the camera to one paired remote at a time and lists
the remote's shooting controls. Nikon's official [ML-L7 product page](https://nij.nikon.com/products/lineup/accessory/remote/ml-l7/)
also lists COOLPIX B600 among compatible products. Therefore the README's
“Connection to remote” route has a manufacturer-backed accessory basis. This
still does not provide Nikon's BLE packet format or prove every ML-L7 operation
is implemented by Furble.

## Provenance, licensing, and use in future tests

All links above are manufacturer-hosted pages or PDFs located during the
2026-09-12 review. Canon, Nikon, Sony, Ricoh, Panasonic, and DJI manuals are
copyrighted manufacturer documentation. This file copies no substantial manual
text and redistributes no PDFs. Links are provided for source provenance only.

Treat product/support statements as manufacturer facts, README-tested labels as
repository facts, and private protocol or hardware-parity conclusions as
unverified without exact-model/firmware captures and physical outcomes. Host
tests validate code and model behavior, not independent hardware parity. Firmware versions
shown on live support pages are retrieval-time context, not a claim about the
firmware used to produce existing Furble code.

The detailed Ricoh transport evidence remains in
[the Ricoh protocol ledger](ricoh.md); this index intentionally
does not duplicate that protocol ledger. Existing project planning context is
[plan 159](../../plans/159-camera-peer-certification.md). Future
simulators should use model-specific fixtures and explicit unsupported states,
especially where an official app uses Wi-Fi, USB, or a vendor accessory rather
than a documented BLE shutter protocol.

## Deliberate URL gaps

The following are intentional omissions, not guessed URLs:

- Ricoh GR IV HDF: the live English manual index is verified, but a stable
  English HDF-specific PDF URL was not established in this review. The Japanese
  HDF PDF surfaced in search, but it is not substituted for an English manual.
- Nikon Z6 III: the official Download Center is verified and is the canonical
  entry for its regional User's Manual and Reference Guide. A direct PDF URL was
  not copied from an unverified redirect.
- Panasonic G9II and BGH1: the official support/manual portals and one English
  PDF each are verified. No additional direct PDF or HTML URL is asserted where
  the portal requires a terms-of-use selection.
- DJI Action 4 and Action 5 Pro: the official downloads pages are verified and
  list the User Manuals. No hidden CDN PDF URL is guessed.
