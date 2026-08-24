# 34 - OTA updates and the partition scheme change that enables them

Upstream issue: [#248 WIFI feature](https://github.com/gkoh/furble/issues/248),
which lists OTA updates under optional quality of life features.

Three staged pieces. 34a is the partition change plus HTTPS OTA and is the only
one proposed for near term work. 34b defers to
`plans/50-companion-app-design.md`. 34c documents that the existing web
installer keeps working.

All line anchors and all measurements below were taken at commit `2b79ce8` on
`master`.

## Goal

Give furble two application slots so it can update itself, land the partition
change once and never again, and make sure a failed update cannot brick a
device that is not physically reachable.

## Motivation

Updating furble today requires a USB cable and a computer. That is fine for a
remote in a pocket and it is wrong for the device #249 describes, bolted into a
studio rack driving a Home Assistant dashboard. It is also friction for every
ordinary user: furble ships releases regularly and each one is a cable, a
browser and a flashing session.

`plans/50-companion-app-design.md` reached the same conclusion from the phone
app side and stopped at the same wall. Its section 3.7 sketches BLE OTA using
`esp_ota_begin()`, `esp_ota_write()`, `esp_ota_end()` and
`esp_ota_set_boot_partition()`, then defers the whole thing because there is
exactly one app slot. It reserved two GATT characteristic UUIDs and moved on.
Its own staging says OTA comes "only after the partition table question has an
answer".

This document is that answer. The partition table is the blocker for both
delivery mechanisms, it is a one time cost, and it gets cheaper the sooner it
happens. Every release that ships on the current table is another device in the
field that will need one more cable.

## Draft issue

Open this before any code. Motivation and the migration cost, no implementation
detail.

> **Updating furble needs a cable and a computer, and the partition table
> blocks any alternative**
>
> Every furble release today has to be flashed over USB, which is friction for
> ordinary updates and a real problem for the board-only, headless setups
> discussed in #249. The blocker is the flash layout: all five environments
> build with `partitions_singleapp_large.csv` and
> `CONFIG_PARTITION_TABLE_SINGLE_APP_LARGE`, so there is a single `factory` app
> slot and no `otadata`, which means neither the OTA-over-WiFi bullet in #248
> nor the BLE OTA sketch in the companion app design can be built at all. I
> would like to propose moving all five environments to the stock ESP-IDF
> `partitions_two_ota_large.csv`, which gives two 1700K app slots against a
> current image of roughly 1.01 MB, and which on the 4 MB boards actually
> increases the per-slot space while reclaiming 2.4 MB of flash that is unused
> today. The change costs users one full USB reflash, and because the layout
> keeps `nvs` at exactly the same offset and size, settings and camera pairings
> can be preserved through it if the release ships an initial `otadata` image
> and the web installer manifest is updated. Would this be welcome, and if so
> should it land on its own ahead of any OTA delivery code?

## Current state, verified

Every environment uses one app slot and has no OTA data partition.

`platformio.ini:11`, inside the shared `[env]` block, applies to all five
environments:

```
board_build.partitions = partitions_singleapp_large.csv
```

The committed sdkconfigs agree:

| File | Lines | Content |
|---|---|---|
| `sdkconfig.m5stick-c` | 399, 404 | `CONFIG_PARTITION_TABLE_SINGLE_APP_LARGE=y`, `CONFIG_PARTITION_TABLE_FILENAME="partitions_singleapp_large.csv"` |
| `sdkconfig.m5stick-c-plus` | 399, 404 | same |
| `sdkconfig.m5stack-core` | 399, 404 | same |
| `sdkconfig.m5stack-core2` | 399, 404 | same |
| `sdkconfig.m5stick-s3` | 552, 557 | same |

`CONFIG_PARTITION_TABLE_OFFSET=0x8000` and `CONFIG_PARTITION_TABLE_MD5=y` on
all five.

Rollback is off everywhere. `sdkconfig.m5stick-s3:445` reads
`# CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE is not set`.

The actual table as built, decoded from
`.pio/build/m5stick-s3/partitions.bin` and
`.pio/build/m5stick-c-plus/partitions.bin` with `gen_esp32part.py`. Both are
identical:

```
# Name, Type, SubType, Offset, Size, Flags
nvs,      data, nvs,   0x9000,  24K,
phy_init, data, phy,   0xf000,   4K,
factory,  app,  factory, 0x10000, 1500K,
```

One `factory` partition, no `otadata`, no `ota_0`, no `ota_1`. The ESP-IDF OTA
API requires at least two OTA app slot partitions and an OTA data partition.
None of the three exist. OTA is not merely unimplemented, it is impossible.

## Per board flash size

Verified from the PlatformIO board definitions under
`~/.platformio/platforms/espressif32/boards/`, cross checked against the
committed sdkconfigs.

| Environment | Board def | `upload.maximum_size` | sdkconfig `ESPTOOLPY_FLASHSIZE` | MCU |
|---|---|---|---|---|
| `m5stick-c` | `m5stick-c` | 4,194,304 | `"4MB"` | esp32 |
| `m5stick-c-plus` | `m5stick-c` | 4,194,304 | `"4MB"` | esp32 |
| `m5stack-core` | `m5stack-core-esp32` | 4,194,304 | `"4MB"` | esp32 |
| `m5stack-core2` | `m5stack-core2` | 16,777,216 | `"16MB"` | esp32 |
| `m5stick-s3` | `esp32-s3-devkitc-1` | 8,388,608 | `"8MB"` | esp32s3 |

Two things here are easy to get wrong and both matter.

**The M5Stack Core builds as a 4 MB board.** The task brief assumed 16 MB. The
repo does not. `m5stack-core-esp32.json` gives 4,194,304 and
`sdkconfig.m5stack-core:378` gives `CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y`. Three of
the five environments are 4 MB, not two.

**The `m5stick-c-plus` environment serves three different products.** The README
lists M5StickC Plus, M5StickC Plus2 and M5StickC Plus SE as supported
controllers, and the wiki install instructions point Plus and Plus2 at the same
`m5stick-c-plus` environment. `platformio.ini:27-29` sets `board = m5stick-c`
for it, which is a 4 MB definition. The Plus2 hardware has 8 MB of flash, but
the binary that ships to it is built for 4 MB because the same binary has to run
on the original 4 MB Plus. The 4 MB constraint therefore covers three of the
five environments and the majority of the installed base.

Conclusion: **4 MB is the binding constraint and there is no point designing for
anything else first.** If the layout works at 4 MB it works everywhere.

## Current application size

Measured from `.pio/build/*/firmware.bin` at `2b79ce8`, and cross checked
against the published v3.9.1 release assets.

| Environment | Local build | v3.9.1 release asset |
|---|---|---|
| `m5stick-c` | 1,001,584 | 997,328 |
| `m5stick-c-plus` | 1,002,240 | 997,328 |
| `m5stack-core` | 1,037,616 | 1,032,688 |
| `m5stack-core2` | 1,037,728 | 1,032,784 |
| `m5stick-s3` | 1,034,256 | 1,032,784 (comparable) |

Roughly 1.01 MB and growing. The S3 section breakdown from
`xtensa-esp32s3-elf-size -A firmware.elf`:

```
.flash.text     691,826
.flash.rodata   221,916
.iram0.text     103,811
.dram0.data      15,188
```

Growth pressure is real. `plans/33-wifi-hub.md` adds `esp_wifi`, `esp_netif`,
lwIP and esp-mqtt, and possibly mbedTLS with a certificate bundle. That is the
single largest additive change on the roadmap and it lands in the same image
that has to fit a slot.

---

# The partition layout

## The 4 MB verdict

**4 MB boards fit, with room to spare, using a stock ESP-IDF table and no new
file. No reduced feature build is needed.**

The stock `partitions_two_ota_large.csv` shipped with ESP-IDF 5.4 is:

```
nvs,      data, nvs,     ,  0x6000,
otadata,  data, ota,     ,  0x2000,
phy_init, data, phy,     ,  0x1000,
ota_0,    app,  ota_0,   ,  1700K,
ota_1,    app,  ota_1,   ,  1700K,
```

Generated for a 4 MB flash with the IDF tool
(`gen_esp32part.py --flash-size 4MB`), it resolves to:

```
# Name, Type, SubType, Offset,   Size, Flags
nvs,      data, nvs,   0x9000,    24K,
otadata,  data, ota,   0xf000,     8K,
phy_init, data, phy,   0x11000,    4K,
ota_0,    app,  ota_0, 0x20000,  1700K,
ota_1,    app,  ota_1, 0x1d0000, 1700K,
```

Computed occupancy:

| Item | Offset | Size |
|---|---|---|
| bootloader | 0x1000 on ESP32, 0x0 on ESP32-S3 | up to 0x7000 |
| partition table | 0x8000 | 0x1000 |
| `nvs` | 0x9000 | 0x6000, 24K |
| `otadata` | 0xf000 | 0x2000, 8K |
| `phy_init` | 0x11000 | 0x1000, 4K |
| alignment gap | 0x12000 | 0xE000, app offsets must be 64K aligned |
| `ota_0` | 0x20000 | 0x1A9000, 1,740,800 bytes |
| alignment gap | 0x1C9000 | 0x7000 |
| `ota_1` | 0x1d0000 | 0x1A9000, 1,740,800 bytes |
| end of table | 0x379000 | 3,641,344 bytes used |
| unused tail | 0x379000 | 552,960 bytes, 540K free |

Headroom against the current binaries:

| Environment | Image | Slot use | Headroom |
|---|---|---|---|
| `m5stick-c` | 1,001,584 | 57.5% | 721 KB |
| `m5stick-c-plus` | 1,002,240 | 57.6% | 721 KB |
| `m5stack-core` | 1,037,616 | 59.6% | 686 KB |
| `m5stack-core2` | 1,037,728 | 59.6% | 686 KB |
| `m5stick-s3` | 1,034,256 | 59.4% | 689 KB |

The counterintuitive result is worth stating plainly. **The 4 MB boards gain app
space by moving to two slots.** Today's single `factory` slot is 1500K. Each of
the two OTA slots is 1700K, which is 200K more per slot than the single slot the
project ships with now. This is possible because the current layout wastes an
enormous amount of flash: `factory` ends at 0x196000, leaving 2,531,328 bytes,
2.41 MB, of a 4 MB part completely unused. The change reclaims most of it.

1700K is 1.66 MiB, above the 1.6 MB headroom target, and 686 KB above the
largest current image.

## Recommendation: use the stock table, do not write a custom one

Use `partitions_two_ota_large.csv` on all five environments. Reasons, in order:

1. **`nvs` does not move and does not change size.** Both the current
   `partitions_singleapp_large.csv` and `partitions_two_ota_large.csv` declare
   `nvs` as `0x6000` with an auto-assigned offset that resolves to `0x9000` in
   both cases. Decoded from the actual built binary, current is
   `nvs,data,nvs,0x9000,24K`. Stock two-OTA is `nvs,data,nvs,0x9000,24K`. Byte
   for byte the same region. This is what makes a non-destructive migration
   possible and it is the single most valuable property of this choice. See the
   migration section.
2. **No new file to maintain.** It is an IDF-maintained table selected by
   `CONFIG_PARTITION_TABLE_TWO_OTA_LARGE`, a kconfig option that already exists
   in every committed sdkconfig as `# ... is not set`
   (`sdkconfig.m5stick-s3:554`). Flipping a documented choice is a smaller
   review surface than inventing a layout.
3. **One layout for all five environments.** The same offsets everywhere means
   one manifest shape, one OTA URL convention, one set of instructions, and no
   per board special case in the release workflow.
4. **PlatformIO resolves it by name.** The espressif32 builder looks up
   `board_build.partitions` in the framework's `components/partition_table`
   directory before treating it as a path, so
   `board_build.partitions = partitions_two_ota_large.csv` works with no file
   added to the repo.

## Per flash size variants: adopted for the larger-flash boards

An 8 MB or 16 MB board could carry larger slots. For reference, a 3 MB slot
layout for the S3 that still preserves `nvs` at 0x9000/24K generates cleanly:

```
nvs,      data, nvs,   0x9000,    24K,
otadata,  data, ota,   0xf000,     8K,
phy_init, data, phy,   0x11000,    4K,
ota_0,    app,  ota_0, 0x20000,     3M,
ota_1,    app,  ota_1, 0x320000,    3M,
```

That ends at 0x620000 and leaves 1920K free on 8 MB.

The implementation now deliberately ships per-flash-size layouts. The 8 MB S3
boards use two 3 MB slots and the 16 MB Core2 uses two 6 MB slots. The 4 MB
boards retain the stock 1700K slots. This supersedes the original single-layout
recommendation: selecting the final layout before the one-time USB migration
avoids requiring a second partition-table reflash merely to use flash already
present on those boards.

This does not authorize an accidental feature split. Every release environment
continues to build in CI, and features intended for all boards must continue to
fit the 1700K ceiling of the 4 MB boards. The larger slots reserve update and
diagnostic headroom; they are not permission to stop building the smaller
targets. `tools/check_partition_tables.py` verifies the exact offsets, equal OTA
slots, preserved NVS and OTA-data regions, flash bounds, and current image
headroom for all six board environments. The PlatformIO workflow runs that
checker before its firmware matrix completes.

---

# Migration

## The cost, stated honestly

Changing the partition table changes the flash layout. The new table has to be
written by something that can write outside the app partition, which the running
app cannot do safely. So the transition itself cannot be an OTA. **Every
existing device needs one full USB reflash to get onto the OTA layout.** After
that, no cable.

That is the price and there is no way around it. Framing matters: it is one
cable, once, in exchange for never needing one again.

## The good news: NVS survives

This is the finding that changes the shape of the migration.

Current layout, decoded from the shipped `partitions.bin`:

```
nvs, data, nvs, 0x9000, 24K
```

New layout, generated from `partitions_two_ota_large.csv`:

```
nvs, data, nvs, 0x9000, 24K
```

Identical offset, identical size. The NVS region is untouched by the layout
change. Everything in it survives a reflash that does not erase the whole chip:

- every entry in `Settings::m_Setting` (`src/FurbleSettings.cpp:11-24`),
  brightness, theme, TX power, GPS, intervalometer, multiconnect, reconnect,
  autoconnect, touch calibration
- the saved camera list from `lib/furble/CameraList.h:28-62`
- the NimBLE bonding keys, which live in NVS in namespaces furble does not own

So the migration does not have to lose settings and it does not have to lose
camera pairings, provided three things are true:

1. The bootloader, the new partition table and the new app are written, and the
   flash is not fully erased.
2. `otadata` at 0xf000 is written with a valid initial image. That region
   currently holds `phy_init` data. Leaving stale bytes there is the one real
   hazard. An all-0xFF `otadata` is the correct initial state and the bootloader
   treats it as "no OTA slot selected", falling back to `ota_0`, which is where
   the new app is written.
3. `phy_init` moves from 0xf000 to 0x11000 and gets regenerated. It is
   calibration data, not user data, and losing it costs nothing.

PlatformIO already generates the file needed for point 2. The espressif32 ESP-IDF
builder detects an `ota` data partition in the CSV, generates
`$BUILD_DIR/ota_data_initial.bin` of the partition's size filled with 0xFF, and
appends it to `FLASH_EXTRA_IMAGES` at the correct offset. So
`pio run -t upload` handles a developer's device with no extra steps. What needs
work is the release path, see 34c.

## Mitigation plan

In order of preference:

1. **Preserve NVS.** Ship `ota_data_initial.bin` as a release artifact, add it to
   the web installer manifest at offset 0xf000, and document that the user
   should decline the erase prompt to keep their cameras. This is the primary
   path and it should be tested first, because if it works the other two are
   only fallbacks.
2. **Settings export via the console.** `plans/24-sd-gpx-logging.md:212-230`
   already designs settings export and import, and `plans/27-usb-console.md`
   gives `settings list` and `settings set`. A user with the console can dump
   settings to the host and replay them. Note that this does not cover camera
   pairings, because BLE bonding keys are not in the `Settings` table and PR27
   explicitly forbids a raw NVS dump for exactly that reason. Export is a
   partial mitigation and should be described as one.
3. **Accept and document.** If the user does erase, they lose settings and
   pairings. Re-pairing is the existing Scan flow and it is not difficult, it is
   just annoying. Say so in the release notes rather than discovering it in an
   issue.

Whichever path a given user takes, the release that carries the partition change
needs release notes that say, in the first line, that this update requires a USB
flash and cannot be applied over the air. Users who have been running the web
installer for a year will otherwise assume the usual process.

---

# Rollback safety

An OTA that bricks a device that is inside a studio wall is worse than no OTA.

## Configuration

Set `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y` in all five sdkconfigs, in the
same PR as the partition change.

With it on, the bootloader tracks image state. A newly written image is marked
`ESP_OTA_IMG_NEW`. On its first boot the bootloader moves it to
`ESP_OTA_IMG_PENDING_VERIFY`. From there the application must call exactly one
of two functions. `esp_ota_mark_app_valid_cancel_rollback()` promotes it to
`ESP_OTA_IMG_VALID`. `esp_ota_mark_app_invalid_rollback_and_reboot()` marks it
`ESP_OTA_IMG_INVALID` and reboots into the previous slot. If the device reboots
while still in `PENDING_VERIFY` without either call, the state becomes
`ESP_OTA_IMG_ABORTED` and the bootloader will not select it again, so the
previous image comes back automatically.

That last property is the entire safety net. A new image that panics on boot
gets exactly one chance and then the device returns to the version that worked,
with no user action.

Do not enable `CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK`. It burns a security version
into eFuse and permanently prevents downgrades. That is correct for a product
with a security programme and wrong for a hobbyist camera remote where a user
may legitimately want to go back a release. It is also irreversible, which is
the strongest argument against it.

## The health check

`esp_ota_mark_app_valid_cancel_rollback()` must be called only after the device
has proved it works. Calling it unconditionally at the top of `app_main` gives
the appearance of rollback safety with none of the substance.

Define the health check as all of the following, evaluated within 30 seconds of
boot:

1. **NVS opened.** `Settings::init()` (`src/main.cpp:28`) returned and a known
   key reads back. If NVS is unreadable the device has no cameras and no
   settings, which is a failure even if it does not crash.
2. **Platform initialised.** `Platform::init()` (`src/main.cpp:27`) returned and
   `Platform::getInstance().m_Init` is true, meaning `M5.begin()` completed at
   `src/FurblePlatform.cpp:22`. On the StickS3 this also covers the M5PM1 PMIC
   bring-up at `src/FurblePlatform.cpp:34-38`.
3. **BLE host up.** `Device::init()` (`src/main.cpp:29`) returned and the NimBLE
   stack reports initialised. This is the one that catches a broken NimBLE
   component update.
4. **Control task alive.** The task created at `src/main.cpp:32` is running and
   its queue accepts a no-op. `Control::sendCommand`
   (`src/FurbleControl.cpp:183-185`) is a zero-timeout `xQueueSend`, so a full
   queue means the control task is not draining, which means the shutter will
   not work.
5. **UI loop ticking**, in a GUI build. `UI::task`
   (`src/FurbleUI.cpp:2123-2134`) has completed at least 100 iterations, which
   at the 5 ms delay on line 2133 is about half a second of a live LVGL handler.
   In a `FURBLE_NO_DISPLAY` build from `plans/33-wifi-hub.md`, substitute the
   headless loop.
6. **Heap floor.** `esp_get_free_heap_size()` above a per board threshold. Pick
   the threshold from a measured master build minus a margin, do not invent a
   number. This catches an image that boots but has no room to connect a camera.
7. **No panic reset.** `esp_reset_reason()` is not `ESP_RST_PANIC`,
   `ESP_RST_TASK_WDT`, `ESP_RST_INT_WDT` or `ESP_RST_WDT`.

What is deliberately **not** in the check:

- Connecting to a camera. A device may be updated with no camera in range or
  with every camera switched off. Requiring a connection would roll back a
  perfectly good image because the photographer went to lunch.
- Connecting to WiFi. Same reasoning. A device may be updated and then moved.
- Any user interaction. A headless board has nobody to press a button.

Where it lives: a small `Furble::Health` check called from the end of `app_main`
after the control task is created, arming a 30 second `esp_timer` that evaluates
conditions 1 to 4, 6 and 7 immediately and waits for condition 5. On success,
`esp_ota_mark_app_valid_cancel_rollback()` and log it. On failure,
`esp_ota_mark_app_invalid_rollback_and_reboot()` and log why first, so the log
survives to the serial console.

Compile the whole thing out when `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` is off,
so a developer build behaves exactly as it does today.

Interaction with `plans/26-pm1-watchdog.md`: the M5PM1 hardware watchdog on the
StickS3 and the rollback timer both exist to catch a wedged device, at different
layers. They do not conflict but the watchdog timeout must be longer than the 30
second health window, or a slow-but-healthy boot gets reset before it can
validate. Check this if PR26 has landed.

---

# PR34a: partition table change and HTTPS OTA over WiFi

## Goal

Move all five environments to a two slot OTA layout, enable rollback, and pull
firmware from a signed HTTPS URL over WiFi.

## Scope

Split into two PRs if review prefers, and it probably should be:

**34a-1, the layout.** Partition table, rollback config, release workflow, web
installer manifest. No OTA code and no health-check code at all; the health
check ships with the delivery code in 34a-2. This is independently
valuable because it unblocks both delivery mechanisms and it is the piece that
costs users a reflash. Landing it alone means the reflash happens once, early,
before the delivery mechanism is even chosen.

**34a-2, the delivery.** `esp_https_ota`, the console commands, the version
check.

Out of scope for both:

- Signed images and secure boot. `CONFIG_SECURE_BOOT` is not set anywhere today
  and turning it on is irreversible per device. Not here.
- Automatic background updates. Every update is explicitly requested. A camera
  remote must not decide to reboot itself.
- Delta updates. The image is 1 MB and the link is WiFi.

## Files to change

- `platformio.ini:11`. `partitions_singleapp_large.csv` becomes
  `partitions_two_ota_large.csv`. One line, all five environments, because it is
  in the shared `[env]` block.
- All five sdkconfigs, the partition table block. `m5stick-c`,
  `m5stick-c-plus`, `m5stack-core` and `m5stack-core2` at lines 399 to 404;
  `m5stick-s3` at lines 552 to 557. `CONFIG_PARTITION_TABLE_SINGLE_APP_LARGE=y`
  becomes `# ... is not set`, `CONFIG_PARTITION_TABLE_TWO_OTA_LARGE=y` is set,
  and `CONFIG_PARTITION_TABLE_FILENAME` becomes
  `"partitions_two_ota_large.csv"`.
- All five sdkconfigs, the bootloader block.
  `# CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE is not set`
  (`sdkconfig.m5stick-s3:445`) becomes `=y`.
- `web-installer/manifest.tmpl`. Both the `ESP32` and `ESP32-S3` build entries
  list three parts. The app offset `65536` must become `131072`, matching
  `ota_0` at 0x20000, and a fourth part for `ota_data_initial.bin` at offset
  `61440`, which is 0xf000, must be added. The bootloader offsets, 4096 for
  ESP32 and 0 for ESP32-S3, and the partition table offset 32768 are unchanged.
- `.github/workflows/release.yml:52-56`, the rename step. It moves
  `firmware.bin`, `bootloader.bin` and `partitions.bin`. Add
  `ota_data_initial.bin`, which PlatformIO now generates in the same directory.
  Add it to the artifact upload list at lines 66-75 and to the release file
  globs at lines 96-99.
- `.github/workflows/release.yml:88-91`, the sha256 step. It already hashes
  `furble-*.bin` and publishes `sha256sum.txt`. That file is the version and
  integrity manifest an OTA client wants and it is already being produced.
- New `src/FurbleOTA.cpp` and `include/FurbleOTA.h`. Add to `furble_sources` at
  `src/CMakeLists.txt:1-10`.
- `src/CMakeLists.txt:12-14`. `PRIV_REQUIRES` gains `esp_https_ota`,
  `esp_http_client`, `app_update` and `esp-tls`.
- New `src/FurbleHealth.cpp` and `include/FurbleHealth.h` for the boot health
  check, or fold it into `FurbleOTA.cpp` if it stays under 100 lines.
- `src/main.cpp:21-40`, `app_main`. The health check arms after the control task
  is created at line 32.
- The `plans/27-usb-console.md` command table. A new `ota` command group.
- `README.md:89-92`, the Easy Install section, and the linked wiki page. Both
  need a note about the one time reflash.

## New settings

| Setting | Type | Default | Effect |
|---|---|---|---|
| `OTA_URL` | string | the project firmware base URL | where to fetch from |
| `OTA_CHANNEL` | enum `stable`, `prerelease` | `stable` | which release track |

No `OTA` enable flag. There is no background activity to gate. An update happens
only when the user runs `ota update`, so the setting would be a switch that
controls nothing.

`OTA_URL` has a sensible default so an ordinary user never has to type one. It
exists so a developer can point at their own build server, and so the project is
not permanently locked to one host.

## Menu placement

An `Update` entry under Settings would be reasonable eventually and is not part
of this PR. Console only first. `plans/33-wifi-hub.md` PR33b already establishes
that network configuration lives on the console, and an OTA the user cannot
configure a network for is not useful.

## Implementation notes

### Where the firmware comes from

Two candidate sources exist and only one of them is pleasant. Both were checked
live.

**GitHub releases.** `https://github.com/gkoh/furble/releases/download/<tag>/<asset>`
returns a 302 to `release-assets.githubusercontent.com` with a signed URL
carrying a JWT and roughly 800 characters of query string. The two hosts have
different certificate chains: `github.com` is issued by Sectigo Public Server
Authentication CA DV E36, `release-assets.githubusercontent.com` by Let's
Encrypt. Asset names also contain a `+` from
`FURBLE_VERSION=${{ github.ref_name }}+${{ github.run_attempt }}`
(`.github/workflows/release.yml:7`), which appears percent-encoded as `%2B` in
the API's `browser_download_url`.

**The project's own Cloudflare Pages deployment.** The release workflow already
publishes every binary and a per platform JSON manifest to
`furble-web-installer.pages.dev` (`.github/workflows/release.yml:101-139`).
`https://furble-web-installer.pages.dev/firmware/furble-m5stick-s3-v3.9.1+1.bin`
returns 200 `application/octet-stream` directly, no redirect, single host,
issued by Google Trust Services WE1. `https://furble-web-installer.pages.dev/manifest_m5stick-s3.json`
returns 200 `application/json` and already contains a `version` field.

Use the Pages deployment. One host, one certificate chain, no redirect, no
signed URL, no expiry, and a version endpoint that already exists and is already
kept current by the release workflow. It costs the project nothing new. Fetch
`manifest_<platform>.json`, compare its `version` to `FURBLE_VERSION`, and if it
differs fetch the `path` from the matching `builds[].parts` entry whose offset is
the app offset.

That last part is the reason the manifest change in 34a-1 matters beyond the web
installer: after the offset change, the app part is identified by offset 131072,
and an OTA client can locate it without hardcoding a filename pattern.

Keep GitHub releases working as a manually specified `OTA_URL`. Because the
chains differ, this is only reliable with the certificate bundle, see below. Do
not make it the default.

### TLS

`esp_https_ota_config_t.http_config` takes either `cert_pem`, a single root, or
`crt_bundle_attach`, the ESP x509 certificate bundle. The ESP-IDF documentation
is explicit that a server endpoint's **root** certificate should be used, not an
intermediate, and that when redirects cross CAs all the relevant certificates
have to be appended to `cert_pem`.

Use `crt_bundle_attach = esp_crt_bundle_attach`. Pinning a single root is
tempting and it is wrong here: the Pages certificate is a Google Trust Services
issued leaf that will rotate, GitHub's is Sectigo, and the asset host is Let's
Encrypt. A pin means the update path breaks the day any one of them rotates a
chain, and it breaks on devices in the field that cannot be updated any other
way. That is the definition of a footgun.

The bundle is already enabled: `CONFIG_MBEDTLS_CERTIFICATE_BUNDLE=y` and
`CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_FULL=y` with
`CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_MAX_CERTS=200`
(`sdkconfig.m5stick-s3:1998-2004`). The full bundle source
`cacrt_all.pem` is 235 KB on disk. If the image does not fit,
switching to the common bundle is the first lever, and it must be checked
against all three hosts above before pulling it.

Never set `CONFIG_ESP_HTTPS_OTA_ALLOW_HTTP`. An unauthenticated plaintext
firmware download is a remote code execution path onto the user's device.

### Memory during the download

`CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN=16384` (`sdkconfig.m5stick-c-plus:1770`)
means a 16 KB inbound record buffer for the TLS session. Combined with WiFi
buffers and NimBLE, this is the tightest moment in the firmware's life.

Set `partial_http_download = true` with a `max_http_request_size` of 16 KB or
less. The ESP-IDF documentation names this as the way to reduce mbedTLS buffer
requirements, and it also means a dropped connection resumes from a range rather
than restarting a 1 MB transfer.

Use the advanced API, `esp_https_ota_begin()`, a loop of
`esp_https_ota_perform()`, then `esp_https_ota_finish()`, rather than the
one-shot `esp_https_ota()`. The loop is where progress reporting lives and where
an abort check lives. A one minute blocking call with no feedback on a headless
device is not acceptable.

### Preconditions, enforced not advised

Refuse to start an update if any of these hold:

- Any camera is connected. `Control::getState()` is `STATE_ACTIVE`. This is the
  coexistence rule from `plans/33-wifi-hub.md`. A firmware download saturates a
  radio that is shared with the camera link, and an update mid-shoot is a bad
  idea regardless.
- Battery below 40 percent and not charging, on boards where
  `plans/02-battery-display.md` provides the reading. A device that browns out
  mid-write leaves a half written slot. Rollback covers that case, but not
  needing rollback is better.
- The intervalometer is running.

Print the reason. Do not offer a force flag in the first version.

### Progress reporting

Register an `esp_event` handler for `esp_https_ota_event_t`.
`ESP_HTTPS_OTA_START`, `ESP_HTTPS_OTA_CONNECTED`, `ESP_HTTPS_OTA_GET_IMG_DESC`,
`ESP_HTTPS_OTA_WRITE_FLASH`, `ESP_HTTPS_OTA_FINISH` and `ESP_HTTPS_OTA_ABORT`
map directly onto console output lines, one fact per line as PR27 requires.

If `plans/33-wifi-hub.md` PR33c has landed, publish progress to
`BASE/ID/state/ota` as well. A studio user watching a dashboard should not have
to guess.

### Console commands

```
ota status        running slot, running version, other slot state, last result
ota check         fetch the manifest, print available version, no download
ota update        check, then download and apply, then reboot
ota rollback      esp_ota_mark_app_invalid_rollback_and_reboot
ota confirm       force esp_ota_mark_app_valid_cancel_rollback, developer escape
```

`ota status` reads `esp_ota_get_running_partition()` and
`esp_ota_get_state_partition()`. It is the first thing anyone will type after a
failed update and it should say something useful.

`ota rollback` is the manual escape when an image boots and passes the health
check but is subtly broken, for example a regression that breaks one camera
vendor. The automatic mechanism cannot catch that. A human can.

## Risks

- **The reflash is a one way door for the user's patience.** Get the manifest
  right the first time. A partition change that ships with a wrong offset in
  `manifest.tmpl` means users flash a device that does not boot, over a web
  installer, with no console. Test the manifest against the real ESP Web Tools
  page before release, not after.
- **`ota_data_initial.bin` at 0xf000 overwrites the old `phy_init`.** That is
  intended. Confirm on a real device that RF still works after the migration and
  that the calibration data regenerates. This is the highest value single test
  in the whole PR.
- **Stale `otadata` if a user flashes only app and table.** The initial image
  must be in the manifest, not just in the developer's build directory.
- **Every environment's build now enforces a 1700K app ceiling.** A future PR
  that pushes past it will fail the build rather than silently overflow, which
  is correct, but it will fail for someone who did not expect it. Say so in the
  PR body and in `plans/33-wifi-hub.md`'s budget section.
- **Rollback masks a genuinely broken release.** A device that quietly rolls
  back looks to the user like an update that did nothing. The console must log
  the rollback loudly and `ota status` must report it.
- **The health check itself can be wrong.** A check that is too strict rolls
  back good images. A check that is too loose is decoration. Test both
  directions deliberately, see Verification.
- **Cloudflare Pages is a dependency the project did not previously have at
  runtime.** It is already a build-time dependency of the web installer, so this
  is a small increase, but the default `OTA_URL` means an outage there stops
  updates. `OTA_URL` being configurable is the mitigation.
- **Nothing here is vendor specific**, so camera coverage is not a risk. Say so
  in the PR body.

## Verification

Attached StickS3, plus a StickC Plus for the 4 MB path, which is the one that
matters most.

Build matrix first:

```
export FURBLE_VERSION=dev FURBLE_TEST=0
pio run -e m5stick-c -e m5stick-c-plus -e m5stack-core -e m5stack-core2 -e m5stick-s3
```

Confirm all five build, confirm `pio run -t size` reports each app under
1,740,800 bytes, and record the five numbers in the PR body.

Layout, 34a-1:

1. Decode `.pio/build/*/partitions.bin` with `gen_esp32part.py` for all five.
   Confirm `nvs` is at 0x9000 with 24K on every one, and confirm `ota_0` at
   0x20000 and `ota_1` at 0x1d0000.
2. Confirm `ota_data_initial.bin` exists in each build directory and is 8192
   bytes of 0xFF.
3. On a StickS3 already running a release build with saved cameras and non
   default settings, flash the new build with `pio run -t upload`, which does
   not erase. Confirm it boots, confirm the saved cameras are still listed,
   confirm the settings survived, and confirm it still connects to the Fujifilm
   without re-pairing. **This is the migration claim and it is the test that
   proves or kills it.**
4. Repeat step 3 on a StickC Plus.
5. Confirm BLE still works after the `phy_init` move. Scan, connect, check RSSI
   against a pre-migration reading at the same distance.
6. Build the release workflow locally or in a fork, generate a manifest, and run
   the real ESP Web Tools installer against it on a device that was fully
   erased. Confirm a clean install works end to end.

Rollback, 34a-1:

7. Confirm `esp_ota_get_state_partition()` reports `ESP_OTA_IMG_VALID` after a
   normal boot and health check pass.
8. Introduce a deliberate `abort()` early in `app_main`, flash it as an OTA
   image, confirm the device boots the bad image once and comes back on the old
   one with no user action.
9. Introduce a deliberate health check failure that does not crash, for example
   forcing the heap floor check to fail. Confirm
   `esp_ota_mark_app_invalid_rollback_and_reboot()` fires and the log explains
   why.
10. Boot a good image with no camera in range, no WiFi configured and no user
    interaction. Confirm it validates. This is the false-positive test and it is
    as important as step 8.

Delivery, 34a-2:

11. `ota status` on a fresh device. Confirm it names the running slot and
    version.
12. `ota check` with WiFi up. Confirm it reads the manifest and reports the
    available version without downloading.
13. `ota update` to a genuinely newer build. Confirm progress output, confirm
    reboot, confirm `ota status` shows the new version in the other slot.
14. `ota update` again. Confirm it alternates back to the first slot.
15. Pull the WiFi access point mid-download. Confirm a clean abort, confirm the
    running image is untouched, confirm a retry works.
16. Power cycle mid-download. Confirm the running image is untouched.
17. `ota update` with a camera connected. Confirm the refusal and the stated
    reason.
18. Point `OTA_URL` at a GitHub release URL. Confirm the redirect and the CA
    change are handled by the certificate bundle. If this fails, document it and
    keep the Pages default.
19. Point `OTA_URL` at a plain HTTP URL. Confirm it is refused.
20. Serve a truncated or corrupted image. Confirm `esp_ota_end()` rejects it and
    the boot partition is not changed.
21. Measure peak download time and minimum free heap during the transfer on the
    S3, and again on the StickC Plus if it fits at all.
22. `ota rollback` from a working image. Confirm it returns to the other slot.

---

# PR34b: BLE OTA

Not detailed here. `plans/50-companion-app-design.md` section 3.7 already
sketches it and this document exists to remove its blocker.

What changes for it once 34a-1 lands:

- The partition question in `plans/50` section 3.7 is answered, and answered
  better than that section feared. It says two OTA slots "roughly halve the
  space available to the app" and that "the 4 MB boards are much tighter". Both
  are wrong, because they assume the current layout uses the flash it has. It
  does not. Each of the two new slots is 1700K against today's single 1500K
  slot, on every board including the 4 MB ones. The measured binary size per
  board that section 3.7 asks for is in this document.
- The two reserved UUIDs, OTA control `00000010-6675-7262-6c65-e0d1c2b3a495` and
  OTA data `00000011-6675-7262-6c65-e0d1c2b3a495`, become implementable rather
  than reserved.
- The rollback machinery, the health check and the preconditions from 34a apply
  unchanged. Only the transport differs. Build them once, in 34a, in a way that
  does not assume HTTP.

Why it stays behind 34a: BLE OTA moves roughly 1 MB over a GATT connection with
a 30 to 50 ms connection interval (`lib/furble/Camera.h:180-181`). Even with a
large MTU and write-without-response that is minutes, not seconds, and it needs
its own flow control, its own chunk acknowledgement and its own resume story.
HTTPS OTA needs none of that because TCP already solved it. Ship the easy
transport first and let it prove the partition change, the rollback path and the
health check in the field. Then the hard transport only has to get the transport
right.

Its advantage is real and it is why it should still be built: it works with no
WiFi, no access point and no credentials, on a device the user already has
paired. For a photographer in a field that is the only update path that exists.

Design decision to make once and honour in both: define the OTA state machine in
`FurbleOTA` with a transport-agnostic interface, `begin(size)`,
`write(chunk)`, `end()`, `abort()`, wrapping `esp_ota_begin`, `esp_ota_write`,
`esp_ota_end` and `esp_ota_set_boot_partition`. 34a implements one caller, 34b
implements a second. If 34a hardcodes `esp_https_ota`'s one-shot API instead,
34b starts from nothing.

---

# PR34c: the existing web installer

No PR. This section documents the interplay so nobody breaks it.

The web installer is not replaced by OTA and must not be. It remains the way a
new device gets its first furble, the way a bricked device is recovered, and the
way a device on the old partition table migrates. `README.md:87-92` points at it
as the simplest way to get started and that stays true.

What 34a changes for it:

- `web-installer/manifest.tmpl` gains a fourth part and changes the app offset
  from 65536 to 131072. This is not optional. A manifest with the old offset
  writes the app into the alignment gap before `ota_0` and the device does not
  boot.
- `.github/workflows/release.yml` publishes `ota_data_initial.bin` alongside the
  three existing binaries.
- `"new_install_prompt_erase": true` in `manifest.tmpl` stays. The prompt is
  what gives the user the choice between a clean install and a settings
  preserving one. The wiki page should explain which to pick: decline the erase
  to keep cameras and settings, accept it to start clean.

What stays the same: bootloader offsets 4096 on ESP32 and 0 on ESP32-S3,
partition table offset 32768, the per platform manifest naming, the Cloudflare
Pages deployment, and `sha256sum.txt`.

One nice consequence. After 34a, a device flashed by the web installer lands in
`ota_0` with an all-0xFF `otadata`, which is exactly the state an OTA client
expects. USB flashing and OTA flashing converge on the same layout instead of
being two worlds. A user can move between them freely and in either direction,
which is the property that makes the whole thing worth the one time reflash.

## Implementation state

The fork-specific web installer is implemented on branch
`feat/web-installer-fork`.

- Manifest generation supports both MaxRink/furble release assets and a local
  Pages asset directory. The release workflow passes the tag separately from
  the versioned asset names.
- The new Pages workflow builds all five release environments on a `v*` tag,
  generates one manifest per board, and publishes the installer page with the
  binaries.
- Fork deviation: GitHub Pages is added as the fork-owned installer path. It
  avoids depending on the upstream Cloudflare project credentials.
- The installer page links to MaxRink/furble. The README documents browser
  flashing and the erase choice.
- Manifest and workflow validation passed. The requested m5stick-s3 build was
  blocked before compilation by the sandbox. The global PlatformIO store is
  protected, and the task-local retry reached an HTTPClientError while fetching
  dependencies. No TinyGPSPlus first-install failure occurred.
- No firmware source, partition table, sdkconfig, or setting changed.
- No hardware flashing was performed. Browser flashing remains hardware
  untested in this fork.

---

# Considered and rejected

**A custom partition CSV in the repo.** A hand written table with 32K `nvs`,
64K aligned slot sizes of 0x1A0000 and a 640K tail generates cleanly and is
slightly tidier, with no alignment gaps. Rejected because growing `nvs` from 24K
to 32K moves its end boundary over what is currently `phy_init`, which forfeits
the byte-identical `nvs` region that makes the non-destructive migration work.
Tidiness is worth less than not erasing every user's camera list.

**Keeping a `factory` partition alongside two OTA slots.** Three app slots do
not fit in 4 MB. `partitions_two_ota.csv` manages it with 1 MB slots, which is
under the current 1500K and barely above the current 1.01 MB image. That would
mean shipping a smaller app slot than the project has today in order to gain
OTA, and it would break within a release or two. Rejected. A factory image as a
permanent recovery slot is attractive and 4 MB simply does not have room for it.

**Per flash size partition tables in 34a.** Rejected, see the layout section.
One layout, one ceiling, one decision. Revisit only when a measured image
approaches 1700K.

**Anti-rollback via eFuse.** `CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK` burns a
security version into eFuse permanently. Rejected. Irreversible, and downgrading
is a legitimate user action for this project.

**Secure boot and signed images.** Rejected for now. `CONFIG_SECURE_BOOT` is not
set anywhere and enabling it is a per device one way operation. It is the right
answer for a commercial product and the wrong answer for a device users are
encouraged to build themselves. The mitigation for image integrity here is TLS
plus the existing `sha256sum.txt`.

**Automatic background update checks.** Rejected. A device that decides to
reboot is a device that misses the shot. Every update is explicit.

**Pinning a single root certificate instead of the bundle.** Rejected, see the
TLS section. Three hosts, three chains, all of which rotate.

**Fetching from the GitHub releases API by default.** Rejected. Redirects, a
signed URL with an expiry, a JWT in the query string, cross-CA chains and rate
limits, when the project already publishes the same binaries on a single-host
CDN with a JSON manifest.

**Delta or compressed updates.** Rejected. A 1 MB image over WiFi takes seconds.
The complexity buys nothing until BLE OTA is the primary transport, and even
then a resume mechanism is worth more than a diff.

---

# Dependencies

```
34a-1 (layout)  -> 34a-2, 34b, and is a hard prerequisite for both
plans/33 PR33b  -> 34a-2 (esp_https_ota needs a network)
plans/33 PR33b  -> budget coupled with 34a-1, see below
plans/50        -> 34b, which is plans/50 section 3.7 unblocked
plans/27        -> the ota console commands
plans/02        -> the battery precondition
plans/26        -> watchdog timeout must exceed the 30 s health window
plans/24        -> settings export as a migration mitigation
```

Ordering claim, stated plainly: **34a-1 should land before
`plans/33-wifi-hub.md` PR33b and PR33c.** Two reasons. The partition change
costs users a reflash and it is cheaper the fewer releases have shipped without
it. And the 1700K slot ceiling is the budget that the WiFi work has to fit
inside, so fixing it first turns a guess into a build-enforced constraint. The
sequence is 34a-1, then 33a, 33b, 33c, then 34a-2 once there is a network to
fetch over.

---

# Implementation status

## 34a-1 complete

Stage 34a-1 is implemented on `feat/34-ota-partitions`.

- All five release environments select the stock `partitions_two_ota_large.csv`
  layout. The resolved table is `nvs` at `0x9000` with size `24K`, `otadata`
  at `0xf000` with size `8K`, `phy_init` at `0x11000` with size `4K`, `ota_0`
  at `0x20000` with size `1700K`, and `ota_1` at `0x1d0000` with size `1700K`.
  This applies to the 4 MB `m5stick-c`, `m5stick-c-plus`, and `m5stack-core`
  boards, the 16 MB `m5stack-core2`, and the 8 MB `m5stick-s3`.
- The NVS offset and size remain `0x9000` and `24K`, preserving the existing
  address range used for settings and BLE pairings during the migration.
- PlatformIO generates `ota_data_initial.bin`. The release workflow and web
  installer include the image at `0xf000` (`61440`), and the built image was
  verified as an `8192` byte all-`0xff` image that selects `ota_0` on first
  boot.
- `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y` is enabled in all five release
  sdkconfigs as specified for 34a-1. `CONFIG_APP_ROLLBACK_ENABLE=y` also
  appears in all five regenerated files: it is an auto-generated deprecated
  mirror of `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`, not an independent
  setting. No OTA delivery or health-check source code was added.
- The raw app image offset moved from `0x10000` to `0x20000`. Anyone flashing
  with a scripted `esptool` invocation must update the app offset, and the
  release notes should carry a line saying so.
- The five release firmware binaries were checked against the `1700K` app
  slot and all fit. The required release builds and the `m5stick-s3-debug`
  build pass. Hardware verification on the M5StickC Plus S3 remains pending.

## Per-board slot sizing complete

- `m5stick-s3` and `esp32-s3-headless` select a repository-owned table with two
  3 MB OTA slots on their 8 MB flash.
- `m5stack-core2` selects a repository-owned table with two 6 MB OTA slots on
  its 16 MB flash.
- The three 4 MB environments retain the stock two-OTA-large table and its two
  1700K slots, preserving the cross-board feature budget.
- The invariant checker covers all six environments and is executed by the
  PlatformIO CI workflow. The firmware-size PR report uses each environment's
  real slot size instead of reporting every board against 1700K.
- This is partition configuration only. Runtime hardware behavior is unchanged;
  the applicable verification is table validation and complete release/debug
  firmware builds. The first on-device OTA transition remains part of stage
  34a-2.

# References

All fetched and verified.

- ESP-IDF v5.4, over the air updates. The requirement for at least two OTA app
  slot partitions plus an OTA data partition, the 0x2000 two-sector `otadata`
  size, `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`,
  `esp_ota_mark_app_valid_cancel_rollback()`,
  `esp_ota_mark_app_invalid_rollback_and_reboot()`, the `ESP_OTA_IMG_NEW`,
  `PENDING_VERIFY`, `VALID`, `INVALID` and `ABORTED` states, the
  `esp_ota_begin` / `esp_ota_write` / `esp_ota_end` /
  `esp_ota_set_boot_partition` sequence, and
  `CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK`:
  https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/api-reference/system/ota.html
- ESP-IDF v5.4, partition tables. The 0x8000 default table offset, the table
  occupying one 0x1000 sector, the rule that app partitions must be placed at
  offsets aligned to 0x10000, automatic alignment of blank offsets, the built in
  table choices, the 0x2000 `otadata` size and `CONFIG_PARTITION_TABLE_MD5`:
  https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/api-guides/partition-tables.html
- ESP-IDF v5.4, ESP HTTPS OTA. `esp_https_ota()`, `esp_https_ota_begin()`,
  `esp_https_ota_perform()`, `esp_https_ota_finish()`,
  `esp_https_ota_config_t.http_config` with `cert_pem` versus
  `crt_bundle_attach`, the instruction to use the server endpoint **root**
  certificate and to append all certificates when redirects cross CAs,
  `partial_http_download` with `max_http_request_size` and its default 16 KB
  buffer, `CONFIG_ESP_HTTPS_OTA_ALLOW_HTTP`, and the `esp_https_ota_event_t`
  values:
  https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/api-reference/system/esp_https_ota.html
- ESP-IDF v5.4, ESP x509 certificate bundle. `esp_crt_bundle_attach` and the
  bundle configuration options:
  https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/api-reference/protocols/esp_crt_bundle.html
- ESP-IDF v5.4, ESP HTTP client. Redirect handling and the configuration the
  HTTPS OTA client passes through:
  https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/api-reference/protocols/esp_http_client.html
- ESP-IDF v5.4, NVS flash. Partition sizing and the behavior of a partition that
  keeps its offset and size across a table change:
  https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/api-reference/storage/nvs_flash.html
- ESP-IDF v5.4, RF coexistence. The single shared 2.4 GHz radio, which is why an
  OTA download must not run while a camera is connected:
  https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/api-guides/coexist.html
- ESP Web Tools. The manifest format, `parts` with `path` and `offset`,
  `chipFamily`, and `new_install_prompt_erase`, which is what
  `web-installer/manifest.tmpl` implements:
  https://esphome.github.io/esp-web-tools/
- PlatformIO, Espressif 32 platform. `board_build.partitions` resolution against
  the framework `components/partition_table` directory, and
  `upload.maximum_size` in the board definitions:
  https://docs.platformio.org/en/latest/platforms/espressif32.html
- gkoh/furble issue 248, WIFI feature. The OTA updates bullet under optional
  quality of life features:
  https://github.com/gkoh/furble/issues/248
- gkoh/furble issue 249, Board-Only support. The headless studio device that
  makes cable-free updates matter:
  https://github.com/gkoh/furble/issues/249
- gkoh/furble wiki, Easy Web Install. The user facing flashing instructions that
  need a migration note:
  https://github.com/gkoh/furble/wiki/Easy-Web-Install
- Verified live, 2026-08-16: `https://furble-web-installer.pages.dev/manifest_m5stick-s3.json`
  returns 200 `application/json` with a `version` field, and
  `https://furble-web-installer.pages.dev/firmware/furble-m5stick-s3-v3.9.1+1.bin`
  returns 200 `application/octet-stream` with no redirect. TLS issuers observed:
  `furble-web-installer.pages.dev` Google Trust Services WE1, `github.com`
  Sectigo Public Server Authentication CA DV E36,
  `release-assets.githubusercontent.com` Let's Encrypt.

## Hardware verification, 2026-08-17

Migration verified on the attached M5StickS3. Baseline captured on the running
pre-OTA image over the USB console: 19 settings and 2 saved Fujifilm cameras
(X-E5 and X100VI). Flashed the new partition table (app at 0x20000, NVS
unchanged at 0x9000, 6 entries). After boot every baseline setting read back
identical, both camera bonds loaded and reconnected, and the new settings
appeared with their defaults. Verdict: PASS. The bootloader log confirmed
`Loaded app from partition at offset 0x20000`.
