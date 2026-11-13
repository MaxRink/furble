# 319 - furble and Meshtastic on one node (dual boot)

Status: research and design only. Nothing here is implemented, built or
flashed. No partition table, patch or bootloader change is proposed for
merge by this document. Sources were read on 2026-11-13 against master
`e66e8807`.

Tags: [source] read in source code, a release asset or official docs;
[inferred] my conclusion, not verified in source. Source links are in the
Sources section.

## Motivation

Most boards with a crystal, a backup-powered GNSS and a big battery exist
because of Meshtastic (plan 318). An owner of such a board may want both
firmwares. This plan answers whether furble and Meshtastic can share one
node with a boot choice, on which platforms, and what each side must change.

## Prerequisite: the latent OTA rollback finding

Every furble sdkconfig sets `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`, and
nothing in furble calls `esp_ota_mark_app_valid_cancel_rollback()`. An image
selected by `esp_ota_set_boot_partition()` from a rollback-enabled app is
NEW; the bootloader turns NEW into PENDING_VERIFY on the first boot and
marks a still-pending image ABORTED on the next boot, then falls back
[source: IDF v5.5.1 `esp_ota_ops.c` 599-665, `bootloader_utility.c`
380-470]. No production path calls the OTA sink today, so this is latent.
It is recorded in plans 125 and 115. Any dual boot switch into furble
depends on mark-valid being wired first.

## Verdict per platform family

| Family | Side by side with boot choice | Verdict |
| :--- | :--- | :--- |
| ESP32 4 MB (only M5Stack Core plus LoRa is supported by both) | Fits only with no OTA on either side and a 256 KiB Meshtastic filesystem; about 5 percent headroom each. Core debug (1.74 MB) does not fit. | No. One Meshtastic release of growth breaks it. |
| ESP32-S3 8 MB (Heltec V3, T-Beam Supreme, Wireless Tracker, Cardputer Adv) | One furble slot (1.875 MiB) plus Meshtastic 2.8 (2.28 to 2.49 MB) plus its OTA loader plus a 1.5 MiB filesystem plus coredump. furble A/B only if its S3 image is at most 1.5 MiB. Meshtastic MUI builds (3.9 MB) do not fit. | Yes, with limits: furble loses self-OTA unless its image is small. |
| ESP32-S3 16 MB (T-Deck and T-Deck Plus, T-Watch S3, ThinkNode M9, CoreS3, Heltec V4, Station G2) | furble A/B at 3 MiB each, Meshtastic 5 MiB (MUI fits), loader, stock 3.4 MiB filesystem, coredump, 640 KiB spare. | Yes. This is the target. |
| nRF52840, Adafruit bootloader plus S140 (RAK4631, T-Echo, T1000-E, T114, Wio L1) | Meshtastic uses about 709 to 726 KB of the 780 KiB app region (0x27000 to 0xEA000). Code cannot usefully execute from QSPI. | No side by side. UF2 swapping only, not recommended. |
| nRF54L15 | Meshtastic there is Arduino plus S145 with its own non-UF2 bootloader (the Zephyr port was dropped), an "extra" board with no release builds. | No, not now. |

## Facts that drive the ESP32 design

Meshtastic (develop 2.8.3, latest release v2.8.2.d5f1619 of 2026-11-04,
stable v2.7.26) [source]:

- Partition tables. `default_16MB.csv`: nvs 0x9000/0x5000, otadata 0xE000,
  app0 0x10000/0x640000, app1 0x650000/0x640000, spiffs 0xC90000/0x360000,
  coredump 0xFF0000. `default_8MB.csv`: app0 0x10000/0x330000, app1
  0x340000/0x330000, spiffs 0x670000/0x180000. Meshtastic apps start at
  0x10000 and its otadata is at 0xE000. furble's app starts at 0x20000 and
  its otadata is at 0xF000. The stock layouts are incompatible, so one
  table must win.
- Toolchain. develop uses pioarduino 55.03.312-1, Arduino-ESP32 3.3.12 on
  ESP-IDF 5.5.5. v2.7.26 used PlatformIO espressif32 6.13.0 Arduino, which is
  Arduino 2.0.x on IDF 4.4 [inferred]. 2.8 is the first line on furble's IDF
  5.5 family.
- OTA. Since 2.7 the second app is `mt-esp32s3-ota.bin` (636,544 bytes) from
  `meshtastic/esp32-unified-ota` v1.0.1 (bare IDF 5.5.0). AdminModule finds
  the first `ota_1`, refuses unless its `project_name` starts with
  `MeshtasticOTA`, sets it as the boot partition and stores the SSID, PSK
  and hash in NVS namespace `MeshtasticOTA`. The loader always writes
  `ESP_PARTITION_SUBTYPE_APP_OTA_0` (`ota_processor.cpp:170`).
- Config. LittleFS under `/prefs/*.proto` on label `spiffs`;
  `FSCom.begin(true)` formats on failure. The default `nvs` holds
  `nimble_bond`, `MeshtasticOTA` and PHY calibration (PHY is [inferred]).
  Factory reset with bond erase calls `nvs_flash_erase()` on the whole
  default partition (`NodeDB.cpp` around 1090). `purgeIncompatibleBleBonds()`
  wipes `nimble_bond` on a record-size mismatch.
- Rollback. Meshtastic never calls the API, but Arduino's `initArduino()`
  auto-marks a PENDING_VERIFY image valid (`esp32-hal-misc.c` around line
  319). Meshtastic confirms itself when another app selects it.
- BLE. `esp32-common.ini` compiles NimBLE with `ROLE_CENTRAL=n`,
  `ROLE_OBSERVER=n`, `MAX_CONNECTIONS=1`, `MAX_BONDS=6`.
- Sizes (v2.8.2 assets). S3 apps 2.28 to 2.49 MB (T-Deck 2,403,408 bytes;
  Cardputer Adv 2,491,952 bytes; tbeam-s3-core 2,374,864 bytes), MUI 3.88 to
  3.92 MB; ESP32 apps about 2.06 MB; littlefs images 0x360000 (16 MB),
  0x180000 (8 MB), 1 MiB (4 MB).

ESP-IDF mechanics [source: v5.5.1]:

- `esp_ota_get_next_update_partition()` walks ota_0 to ota_15 by subtype and
  wraps to the first slot.
- CSV rows must ascend by offset, but subtype order is free, so `ota_2` can
  sit at 0x20000 ahead of `ota_0`. That keeps furble's 0x20000 rule and its
  whole low layout byte-identical.
- The bootloader picks the active otadata entry. With both entries invalid
  and no factory app it boots ota_0.
- `CONFIG_BOOTLOADER_APP_TEST` boots the `test` subtype only while a GPIO is
  held (5 s default, no latch). `CONFIG_BOOTLOADER_FACTORY_RESET` erases
  data partitions. Custom bootloader examples (`bootloader_hooks`,
  `bootloader_override`, `bootloader_multiboot`) exist.
- The bootloader boots apps from newer IDF versions, not older.
  `otatool.py switch_ota_partition` rewrites only the otadata sequence and
  CRC (state UNDEFINED, no rollback).
- The NimBLE bond store calls `nvs_open("nimble_bond")` on the default
  partition. `NVS_DEFAULT_PART_NAME "nvs"` is an unconditional define in
  `nvs_flash.h:58`.

furble [source: this repo]:

- Low layout nvs 0x9000/0x6000, otadata 0xF000/0x2000, phy_init
  0x11000/0x1000, first app 0x20000 (`partitions_two_ota_16m.csv`).
  0x12000 to 0x20000 (56 KiB) is free in every table.
- IDF 5.5.3 (`espressif32@6.13.0`, `framework-espidf@3.50503.0`,
  `platformio.ini:12-13`). The phy_init partition is unused.
- `Settings::init` erases all NVS on `NO_FREE_PAGES` or `NEW_VERSION_FOUND`
  (`src/FurbleSettings.cpp:794`). `lib/preferences/Preferences.cpp:48`
  already supports `nvs_flash_init_partition(label)`.
- The OTA sinks select `esp_ota_get_next_update_partition(nullptr)`
  (`src/FurbleOTAPartitionSinkESP.cpp:23`).
- License MIT. Meshtastic is GPL-3.0.

## ESP32-S3 design

### Slot ownership rules

1. Meshtastic must be `ota_0`, because its loader always writes ota_0.
2. Its loader, if kept, must be `ota_1`, because AdminModule looks up the
   first ota_1 and checks the project name.
3. furble goes in `ota_2`, and `ota_3` for A/B, starting at 0x20000.
4. Named-slot OTA: furble's OTA sink must stop using
   `esp_ota_get_next_update_partition()`, which would wrap to ota_0 and
   overwrite Meshtastic. Pin the target by label (`furble_a` and
   `furble_b`), and refuse any slot whose subtype is ota_0 or ota_1 or whose
   `project_name` is not `furble`.
5. If furble occupies ota_1 instead (no loader), Meshtastic's OTA refuses on
   the project-name check. Meshtastic loses BLE and WiFi OTA, and nothing is
   clobbered.

### Proposed 16 MB table

T-Deck, CoreS3, T-Watch S3, ThinkNode M9. furble keeps 0x20000 and its exact
low layout:

```
# Name,      Type, SubType,  Offset,   Size
nvs,         data, nvs,      0x9000,   0x6000    # Meshtastic and its loader
otadata,     data, ota,      0xF000,   0x2000
phy_init,    data, phy,      0x11000,  0x1000
furble_nvs,  data, nvs,      0x12000,  0xE000    # furble private
furble_a,    app,  ota_2,    0x20000,  0x300000
furble_b,    app,  ota_3,    0x320000, 0x300000
app0,        app,  ota_0,    0x620000, 0x500000  # Meshtastic, MUI fits
app1,        app,  ota_1,    0xB20000, 0xA0000   # mt-esp32s3-ota.bin
spiffs,      data, spiffs,   0xBC0000, 0x360000  # same size as the release littlefs image
coredump,    data, coredump, 0xF20000, 0x40000
# 0xF60000-0x1000000 (640 KiB) spare
```

8 MB (Heltec V3, T-Beam Supreme, Cardputer Adv), single furble slot:
`furble_nvs 0x12000/0xE000`, `furble_a ota_2 0x20000/0x1E0000`, `app0 ota_0
0x200000/0x2B0000`, `app1 ota_1 0x4B0000/0xA0000`, `spiffs
0x550000/0x180000`, `coredump 0x6D0000/0x40000`. For furble A/B, use two
0x180000 slots and drop coredump, only if a measured S3 image is at most
1.5 MiB.

### Private furble NVS

furble must move, not Meshtastic. Meshtastic's NimBLE store, PHY and WiFi
use the default label inside Arduino-prebuilt libraries, while furble builds
IDF from source and already has a `patches/` mechanism. Patch
`NVS_DEFAULT_PART_NAME` to `"furble_nvs"`, which redirects settings, NimBLE
bonds, PHY calibration and WiFi, and put `furble_nvs` in the free 0x12000 to
0x20000 gap. Leave `nvs` at 0x9000 to Meshtastic.

Hazards this removes:

- A Meshtastic factory reset (`nvs_flash_erase`) wiping furble settings and
  camera bonds.
- Meshtastic "clear bonds" and `purgeIncompatibleBleBonds()` deleting camera
  bonds.
- NimBLE round-robin eviction with Meshtastic's `MAX_BONDS=6` [inferred].
- furble's own erase on `NO_FREE_PAGES` wiping Meshtastic.

Existing furble users need a one-time migration. Do not erase anything to do
it. phy_init is unused by both. Keep only furble's coredump; Meshtastic
disables coredump. Never touch `spiffs`.

### Bootloader IDF version

Use furble's bootloader, but first align furble to IDF 5.5.5 or newer,
because the bootloader must not be older than the apps it boots. Meshtastic
2.8 is 5.5.5. The unified loader is 5.5.0, older but the same minor
[inferred]. Meshtastic 2.7.x (IDF 4.4, NimBLE-Arduino 1.4.3 bond format) is
out of scope. Chip and eFuse revision ranges are compatible, the MMU page is
64 KB on the S3, and each app retunes MSPI from its own config [inferred].
Secure boot and flash encryption are off on both sides. Enabling either on
one side breaks the other.

### Switching

| Option | For | Against |
| :--- | :--- | :--- |
| (a) Menu or console entry in furble: find ota_0, check `esp_ota_get_partition_description()`, call `esp_ota_set_boot_partition()`, then `esp_restart()` | Stock bootloader, latches, a few lines | Meshtastic has no "boot furble" control. Needs USB `otatool`, a Meshtastic patch, or (c). |
| (b) Tiny factory selector app | Works from both sides | Needs a display driver per board, takes 0x20000 (breaks the furble rule), shown only when otadata is invalid |
| (c) Custom second-stage bootloader (`bootloader_components` override like `bootloader_multiboot`) that reads a non-strapping GPIO at reset and returns ota_2 or ota_0, optionally latching through otadata | Works whatever app runs, no Meshtastic patch | Not OTA-updatable. The GPIO must not be a strapping pin (the T-Deck trackball click is GPIO0 [inferred]). Any Meshtastic `.factory.bin` or web-flasher full flash replaces it. |
| (d) Stock `CONFIG_BOOTLOADER_APP_TEST` | No custom code | A 5 s hold on every boot, no latch. From the test slot `next_update_partition` returns ota_0, so furble self-OTA clobbers Meshtastic. |

Recommendation: (a) for furble to Meshtastic, (c) for Meshtastic to furble,
with `otatool.py switch_ota_partition --name furble_a` over USB as the
zero-code fallback.

Rollback interplay:

| Event | Result |
| :--- | :--- |
| furble selects Meshtastic | NEW, then PENDING_VERIFY, then Arduino marks it valid. Sticks. |
| Meshtastic or a patch selects furble while furble never marks valid (today) | Runs once, ABORTED on the next reset, falls back to Meshtastic. Not destructive, but furble mode does not survive a power cycle. Fix the prerequisite first. |
| `otatool switch_ota_partition` | UNDEFINED. Sticks. |
| Meshtastic BLE or WiFi OTA | The loader writes ota_0. furble is untouched. Safe. |
| Meshtastic web flasher, `device-update.sh` (`UPDATE_OFFSET=0x10000`) or any `.factory.bin` | Writes at 0x10000 over otadata, phy_init, `furble_nvs` and furble. A factory image also replaces the bootloader and the partition table. Destructive, USB recovery only. |

## Risks

- USB-recoverable bricking from Meshtastic's official USB tools or a custom
  bootloader bug.
- Losing Meshtastic config if `spiffs` is resized or touched.
- Losing camera bonds without the `furble_nvs` move.
- OTA clobbering if furble keeps `next_update_partition`.
- User complexity: one multi-offset esptool command plus `otatool`, and every
  later Meshtastic update must avoid Meshtastic's own USB tools. An
  esptool-js page in the furble web installer could hide most of that.

## Proof of concept path

1. Hardware: an ESP32-S3 N16R8 devkit with the `esp32-s3-headless` env (no
   PMIC, no LoRa risk) and Meshtastic v2.8.2 `esp32-s3-pico`
   (`default_16MB`). Move to a T-Deck or CoreS3 once a furble board profile
   exists.
2. furble code: `partitions_coexist_16m.csv`, an `NVS_DEFAULT_PART_NAME`
   patch in `patches/`, the OTA sink pinned by label, mark-valid wired after
   boot health, and a console or menu "Boot Meshtastic" entry. Optionally a
   `bootloader_components` GPIO override.
3. Flash once over USB, never erasing a device that already holds bonds:

   ```
   esptool.py --chip esp32s3 -p PORT erase_flash   # fresh device only
   esptool.py --chip esp32s3 -p PORT write_flash \
     0x0 bootloader.bin 0x8000 partitions.bin 0x20000 firmware.bin \
     0x620000 firmware-<mt-board>-2.8.2.d5f1619.bin \
     0xB20000 mt-esp32s3-ota.bin \
     0xBC0000 littlefs-<mt-board>-2.8.2.d5f1619.bin
   python $IDF_PATH/components/app_update/otatool.py -p PORT switch_ota_partition --name furble_a
   ```

4. Tests: switch both ways with a power cycle each time. Pair a camera, pair
   a phone in Meshtastic, run Meshtastic clear-bonds and factory reset, and
   confirm the camera bond survives. Run Meshtastic BLE OTA and confirm the
   furble slot hashes are unchanged. Run furble self-OTA from `furble_b` and
   confirm ota_0 is unchanged.
5. Afterwards Meshtastic updates only through in-app OTA or `esptool
   write_flash 0x620000 <app.bin>`, never the web flasher,
   `device-update.sh` or a `.factory.bin`.

## nRF52840 and nRF54L

- nRF52840 flash map [source]: MBR 0x0 to 0x1000; S140 v6 or v7 to 0x26000
  or 0x27000; Meshtastic app to 0xEA000; warm-node store 0xEA000 to 0xED000;
  InternalFS LittleFS 0xED000 to 0xF4000; Adafruit bootloader 0xF4000 to
  0xFE000; MBR params 0xFE000; bootloader settings 0xFF000. The Adafruit
  bootloader does UF2 DFU of the application only; dual bank is off by
  default.
- Two apps cannot coexist in internal flash. A swap-from-QSPI scheme means
  replacing the Adafruit bootloader with MCUboot or a custom one, losing
  Meshtastic's UF2 and BLE DFU flows and rewriting internal flash per switch
  [inferred].
- UF2 swapping works only for an app linked at the right SoftDevice offset
  (0x27000 on S140 v7) that leaves 0xEA000 to 0xF4000 alone. Zephyr's stock
  UF2 layouts put `storage_partition` at 0xEC000 to 0xF4000, which overlaps
  Meshtastic's InternalFS and warm store, so it must move. Each switch is a
  1.4 MB UF2 drag plus a double-tap reset [inferred].
- nRF54L15: `meshtastic/platform-nordicnrf54` is an Adafruit-style Arduino
  core on S145 (bootloader 0x0 to 0x7C00, app 0x8000 to 0x147000), DFU over
  serial or GPREGRET, no UF2. A GPREGRET-selected second slot needs a forked
  bootloader. Not a release target.

## Alternative: furble camera BLE as a Meshtastic module

Feasible on an ESP32-S3 with PSRAM, but a large, long-lived fork.

- BLE: Meshtastic compiles out the central and observer roles and allows
  one connection. Re-enabling them is a `custom_sdkconfig` change (central
  and observer on, `MAX_CONNECTIONS>=2`, `BLE_MAX_ACT>=4`). RAM is the risk;
  Meshtastic already trimmed NimBLE for heap starvation [inferred].
- Code: `lib/furble` is written against esp-nimble-cpp `NimBLEClient`, while
  Meshtastic uses Arduino's BLE library and ignores NimBLE-Arduino. Either
  port the camera drivers or add esp-nimble-cpp alongside, which means two
  wrappers fighting over GAP and store callbacks [inferred]. Camera bonds
  would share Meshtastic's 6-bond budget with phones.
- UI: furble's LVGL UI is not reusable as is. It becomes a BaseUI frame, a
  MUI page or an InkHUD applet.
- License: furble code can go into Meshtastic (the combined binary is
  GPLv3). Meshtastic code cannot come back into MIT furble.
- Unique upside: a mesh-triggered remote shutter over LoRa, which dual boot
  cannot do.
- Cost: a monthly release train (2.8.0 Sep, 2.8.1 Oct, 2.8.2 Nov 2026),
  uncertain upstream acceptance, two BLE-role codebases in one host.

## Sources (accessed 2026-11-13)

- Meshtastic firmware: https://github.com/meshtastic/firmware (develop:
  partition tables, `version.properties`, `variants/esp32/esp32-common.ini`,
  `src/platform/esp32/MeshtasticOTA.cpp`, `src/modules/AdminModule.cpp`
  399-441, `src/nimble/NimbleBluetooth.cpp`, `src/mesh/NodeDB.cpp`,
  `bin/device-update.sh`, `src/platform/nrf52/nrf52840_s140_v6.ld` and
  `_v7.ld`, `variants/nrf54l15/*`)
- Meshtastic releases: https://github.com/meshtastic/firmware/releases
- Meshtastic unified OTA loader: https://github.com/meshtastic/esp32-unified-ota
- Meshtastic nRF54 platform: https://github.com/meshtastic/platform-nordicnrf54
  and https://github.com/meshtastic/nRF54_Bootloader
- pioarduino 55.03.312-1: https://github.com/pioarduino/platform-espressif32/releases/tag/55.03.312-1
- PlatformIO espressif32 6.13.0: https://github.com/platformio/platform-espressif32/releases/tag/v6.13.0
- Arduino-ESP32 `initArduino()`: https://github.com/espressif/arduino-esp32/blob/master/cores/esp32/esp32-hal-misc.c
- ESP-IDF v5.5.1 OTA ops: https://github.com/espressif/esp-idf/blob/v5.5.1/components/app_update/esp_ota_ops.c
- ESP-IDF v5.5.1 bootloader utility: https://github.com/espressif/esp-idf/blob/v5.5.1/components/bootloader_support/src/bootloader_utility.c
- ESP-IDF v5.5.1 partition generator: https://github.com/espressif/esp-idf/blob/v5.5.1/components/partition_table/gen_esp32part.py
- ESP-IDF v5.5.1 NVS header: https://github.com/espressif/esp-idf/blob/v5.5.1/components/nvs_flash/include/nvs_flash.h
- ESP-IDF v5.5.1 otatool: https://github.com/espressif/esp-idf/blob/v5.5.1/components/app_update/otatool.py
- ESP-IDF custom bootloader examples: https://github.com/espressif/esp-idf/tree/v5.5.1/examples/custom_bootloader
- ESP-IDF bootloader guide: https://docs.espressif.com/projects/esp-idf/en/v5.5.1/esp32s3/api-guides/bootloader.html
- NimBLE NVS bond store: https://github.com/espressif/esp-nimble/blob/nimble-1.5.0-idf/nimble/host/store/config/src/ble_store_nvs.c
- Adafruit nRF52 bootloader: https://github.com/adafruit/Adafruit_nRF52_Bootloader
- Zephyr UF2 layout: https://github.com/zephyrproject-rtos/zephyr/blob/main/dts/vendor/nordic/nrf52840_partition_uf2_sdv6.dtsi

Inferred, not verified in source: NimBLE eviction with different
`MAX_BONDS`; IDF 5.5.3 versus 5.5.5 bond-struct equality; Meshtastic PHY and
WiFi keys in NVS; MSPI retune; T-Deck GPIO strapping; QSPI population on the
RAK4631, T114 and Wio L1; UF2 swap timing; module RAM impact.

## Implementation state

Research and design only. Nothing is implemented. The prerequisite
mark-valid wiring belongs to plans 125 and 115.
