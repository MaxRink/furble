# PR24 - SD card GPX track logging and settings backup

## Goal

Use the SD card slot on the two Core boards. Log GPS fixes to a GPX 1.1 track
file while GPS is enabled, and export or import the furble settings as a file.
The feature is hidden at runtime on boards with no card slot.

## Scope

In scope:

- New `FurbleSD` module. Mount, unmount, capability probe.
- GPX 1.1 track logger driven by the existing GPS service.
- Settings export to a file and import from a file.
- `Settings->Storage` submenu, hidden when the board has no SD slot.

Out of scope:

- Reading or writing camera pairing data. The blowfish keyed device store stays
  in NVS only. Exporting it to a removable card is a security decision, not a
  storage one.
- Firmware update from card.
- SD on the sticks. None of them have a slot.
- Photo or log file browsing on device.

## Hardware support matrix

Verified against the M5Stack product pages and the M5Unified SD pin table.

| Board | SD slot | Bus | SCLK | MOSI | MISO | CS |
|---|---|---|---|---|---|---|
| M5StickC | no | - | - | - | - | - |
| M5StickC Plus | no | - | - | - | - | - |
| M5StickC Plus2 | no | - | - | - | - | - |
| M5StickC Plus SE | no | - | - | - | - | - |
| M5StickS3 | no | - | - | - | - | - |
| M5Stack Core | yes, TF up to 16 GB | SPI, shared with display | G18 | G23 | G19 | G4 |
| M5Stack Core2 | yes, TF | SPI, shared with display | G18 | G23 | G38 | G4 |

Both product pages and the M5Unified `_pin_table_sd` agree on every pin. The
display on both boards uses the same SCLK, MOSI and MISO pins with a different
chip select, so the card shares the SPI bus with the panel.

Capability probe: `M5.getPin(m5::pin_name_t::sd_spi_cs)`. The pinned M5Unified
0.2.13 fills that table entry with 255 on boards without a slot. Note that
0.2.13 has no `hasSD()` helper. That was added later on master. Use `getPin()
>= 0` and treat 255 as absent.

## Files to change

| File | Lines | What |
|---|---|---|
| `include/FurbleSD.h` | new | `SD` singleton: `isSupported()`, `mount()`, `unmount()`, `isMounted()` |
| `src/FurbleSD.cpp` | new | SDSPI mount on the display SPI host, capability probe |
| `include/FurbleGPX.h` | new | `GPX` writer: `open()`, `addPoint()`, `close()` |
| `src/FurbleGPX.cpp` | new | GPX 1.1 serialisation and fsync policy |
| `src/CMakeLists.txt` | 1-10 | Add `FurbleSD.cpp` and `FurbleGPX.cpp` |
| `src/CMakeLists.txt` | 12-14 | Add `fatfs`, `sdmmc`, `esp_driver_sdspi` to `PRIV_REQUIRES` |
| `src/FurbleGPS.cpp` | whole file | `GPS::update()` is the fix consumer. Add the GPX hook there |
| `include/FurbleGPS.h` | 32-56 | Private members. Add the last logged fix timestamp |
| `include/FurbleSettings.h` | 16-29 | `type_t` enum. Add `SD_GPX`, `GPX_PERIOD` |
| `include/FurbleSettings.h` | 101-148 | `storage_type<>`. `bool` and `uint16_t` bindings |
| `src/FurbleSettings.cpp` | 11-24 | Setting table. Two rows |
| `src/FurbleSettings.cpp` | 169-230 | Defaults. `SD_GPX` joins the false group at 209-215 |
| `include/FurbleUI.h` | 161-191 | Add `m_StorageStr` and sub entry strings |
| `include/FurbleUI.h` | 299-346 | Add `addStorageMenu()` |
| `src/FurbleUI.cpp` | 53-76 | `m_Menu` grid map. Add the new entries |
| `src/FurbleUI.cpp` | 2062-2082 | `addSettingsMenu()`. Call `addStorageMenu(menu)` |
| `src/main.cpp` | 27-29 | Call `Furble::SD::init()` after `Settings::init()` |

## New settings

| Enum | NVS key | Namespace | Type | Default | Notes |
|---|---|---|---|---|---|
| `SD_GPX` | `sd_gpx` (6) | `FURBLE_STR` | `bool` | `false` | Off means no card is mounted and no file is opened. Reproduces current behaviour exactly |
| `GPX_PERIOD` | `gpx_period` (10) | `FURBLE_STR` | `uint16_t` | `5` | Minimum seconds between track points. 1 to 60 |

Name strings: `"GPX Logging"` and `"GPX Interval"`.

Settings export and import are menu actions, not settings. They add no NVS keys.

## Menu placement

```
Settings
└─ Storage             (hidden when the board has no SD slot)
   ├─ GPX Logging      (switch)
   ├─ GPX Interval     (roller: 1 / 2 / 5 / 10 / 30 / 60 s)
   ├─ Export Settings  (button, confirm dialog)
   ├─ Import Settings  (button, confirm dialog, restart after)
   └─ Card Info        (label: mounted, capacity, free space)
```

`Storage` is a new submenu created by this PR. Take the next free `{col,row}` on
the Settings grid at `src/FurbleUI.cpp:53-76`. PR01, PR05, PR08, PR16, PR22 and
PR23 also add cells. Whichever lands last settles the grid.

The whole `Storage` entry is hidden when `SD::isSupported()` is false, so nothing
changes on the five stick and non SD builds.

## Implementation notes

### Mounting on a bus the display already owns

This is the part that needs care. M5GFX initialises the SPI bus itself. In the
ESP-IDF build path `lgfx::spi::init()` calls `spi_bus_initialize()` and then
`spi_bus_add_device()` for the panel. Calling `spi_bus_initialize()` a second
time for the card returns `ESP_ERR_INVALID_STATE`.

The fix is to not initialise the bus. ESP-IDF states the requirement the other
way round: `esp_vfs_fat_sdspi_mount()` needs the bus to already be initialised by
the caller. M5GFX has done that. So:

```
sdmmc_host_t host = SDSPI_HOST_DEFAULT();
host.slot = <display spi host>;
sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
slot.gpio_cs = (gpio_num_t)M5.getPin(m5::pin_name_t::sd_spi_cs);
slot.host_id = host.slot;
esp_vfs_fat_mount_config_t mount = {
  .format_if_mount_failed = false,
  .max_files = 4,
  .allocation_unit_size = 16 * 1024,
};
esp_vfs_fat_sdspi_mount("/sd", &host, &slot, &mount, &card);
```

`format_if_mount_failed` must stay false. Never reformat a user card.

The display host on both Core boards is the ESP32 VSPI host, which M5GFX
selects as the default at `lgfx/v1/platforms/esp32/common.cpp`. Read it back
from M5GFX rather than hardcoding it, so a future M5GFX change does not break
the mount silently.

M5GFX already expects this sharing. It sets the SPI user register with the
comment "need SD card access (full duplex setting)" and runs a CMD0 and CMD58
sequence during panel autodetect to put the card into SPI mode. So the
combination is intended by the library author, not a hack.

Both the panel and the card go through `spi_bus_add_device()`, so the ESP-IDF
SPI master driver arbitrates between them. Writes are short and infrequent. Do
not hold the bus across a whole GPX flush.

### GPX file format

GPX 1.1 is an XML schema published by TopoGrafix. A minimal valid track file is:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<gpx version="1.1" creator="furble"
     xmlns="http://www.topografix.com/GPX/1/1"
     xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance"
     xsi:schemaLocation="http://www.topografix.com/GPX/1/1
                         http://www.topografix.com/GPX/1/1/gpx.xsd">
  <trk><trkseg>
    <trkpt lat="-34.928500" lon="138.600700">
      <ele>50.0</ele>
      <time>2026-08-16T01:23:45Z</time>
      <sat>9</sat>
    </trkpt>
  </trkseg></trk>
</gpx>
```

Element order inside `trkpt` is fixed by the schema: `ele` then `time` then
`sat`. Getting it wrong produces a file that some readers accept and others
reject. Follow the schema order.

Write the header on open. Append `trkpt` elements. Write the three closing tags
on close. If power is lost before close, the file is missing three closing tags.
Most readers, including GPSBabel and gpxpy, reject that. Mitigate by keeping the
closing tags on disk at all times: after each point, write the closers, then
`fseek` back over them before the next point. That costs 40 bytes of rewrite per
point and makes every intermediate state a valid file.

Coordinates come from TinyGPSPlus, which furble already uses. `gps.location.lat()`
and `lng()` are doubles. Print with six decimal places, which is about 0.11 m.
Altitude from `gps.altitude.meters()`. Time from `gps.date` and `gps.time`, which
are UTC, matching the GPX requirement.

### When to log

Hook `GPS::update()` in `src/FurbleGPS.cpp`. That already runs on the GPS service
timer and already knows whether there is a fix. Add a point when all of these
hold:

- `SD_GPX` is true and the card is mounted.
- GPS is enabled and `gps.location.isValid()`.
- `gps.location.age()` is under the existing `MAX_AGE_MS` of 30 s.
- At least `GPX_PERIOD` seconds have passed since the last logged point.

One file per session. Name it from the first valid fix time,
`/sd/furble/YYYYMMDD-HHMMSS.gpx`. If no fix is available yet, defer the open
until the first one. That avoids an empty file per boot.

### fsync cadence

The card can be pulled or the battery can die at any time. FatFs buffers, so an
unflushed write is lost. `CONFIG_FATFS_IMMEDIATE_FSYNC` flushes after every write
but costs a full FAT update per point.

Policy: call `fsync()` after every point when `GPX_PERIOD` is 10 s or more, and
every fifth point when it is under 10 s. That bounds worst case loss to under a
minute of track while keeping the flush rate low. Make the cadence a constant in
`FurbleGPX.cpp` so it is easy to tune after measuring.

Close the file properly on the power off path so the normal case is clean.
`Platform::powerOff()` at `src/FurblePlatform.cpp:74-80` is the single exit point
and is where the unmount belongs.

### Settings export and import

Export walks the `Settings::m_Setting` table at `src/FurbleSettings.cpp:11-24`,
reads each key from NVS and writes a plain text key and value file to
`/sd/furble/settings.txt`. Text, not a binary NVS image. A binary dump is tied to
the NVS layout and would break across firmware versions. Text survives.

Import parses the same file, validates each key against the table, and saves only
keys it recognises. Unknown keys are logged and skipped. Values out of range are
rejected. Then restart, using the same pattern as the Theme restart button at
`src/FurbleUI.cpp:1984-1992`.

`TOUCH_CALIBRATION` and `INTERVAL` are structs. Serialise them as hex of the raw
bytes with a length prefix, and refuse to import when the length does not match
the current build. Do not silently accept a struct from a different firmware
version.

Both actions need a confirm dialog. Import overwrites everything.

## Dependencies

- None hard. It can land any time.
- PR14 and PR15 touch `FurbleGPS.cpp` and will conflict textually. Land this
  after them, or expect a small rebase.
- PR05 (diagnostics) is a natural home for the Card Info page. Without it, keep
  Card Info under Storage.
- Useful to PR19. Deep sleep between intervalometer shots loses the file handle,
  so a later PR can reopen and append after wake using the rewound closer trick.

## Risks

- SPI bus sharing with the display. This is the main risk. If the panel and the
  card interleave badly, expect visible display corruption or a mount failure.
  Both go through `spi_bus_add_device()` so the driver should arbitrate, and
  M5GFX explicitly supports the combination, but this must be proven on device
  before merge, not argued from source.
- SPI clock. The panel runs at 40 MHz write. The card default is 20 MHz. The
  driver reconfigures per device, but a card that is marginal at 20 MHz will fail
  intermittently. Start at 10 MHz and raise it after it works.
- Card write wear. One point every 5 s with an fsync is roughly 17 000 FAT
  updates per day of continuous logging. That is well inside the endurance of any
  modern card, but a 1 s period with per point fsync is five times worse. Cap the
  minimum period at 1 s and default to 5 s.
- Power loss mid write. The rewound closer trick makes the file valid at rest,
  but a power cut during the write itself can still leave a torn line. Readers
  will reject it. Accept this and say so.
- No card inserted, or a card pulled at runtime. Every write must check the
  return and disable logging on repeated failure rather than logging an error
  every second forever.
- Mount cost at boot. SDSPI mount adds tens of milliseconds and the card draws
  current continuously once mounted. Only mount when `SD_GPX` is true or the user
  opens the Storage page.
- Settings import is a foot gun. A malformed file could brick the UI through a
  bad brightness or theme value. Validate every value against the same ranges the
  UI enforces.
- Format is never offered. Make sure `format_if_mount_failed` stays false in
  every code path.

## Verification

Build matrix:

```
pio run -e m5stick-c -e m5stick-c-plus -e m5stack-core -e m5stack-core2 -e m5stick-s3
```

All five clean with `-Wall -Wextra`. The three stick builds must compile the
module and resolve `isSupported()` to false.

Defaults regression:

1. Erase NVS, flash master on a Core2, note the menu.
2. Flash this branch on fresh NVS. `SD_GPX` is false, so no card is mounted. The
   only change is the new `Settings->Storage` entry. Boot time must not move.
3. Flash the same branch on a StickS3. `Storage` must be absent.

On device, M5Stack Core2 over USB, with a GPS unit and a FAT32 card:

1. `pio run -e m5stack-core2 -t upload`, then `pio device monitor`.
2. Storage page with no card inserted. Card Info must say not mounted. No crash.
3. Insert a card. Enable GPX Logging. Confirm the mount log line and the capacity
   readout.
4. Enable GPS. Wait for a fix. Confirm a file appears under `/sd/furble/`.
5. Watch the display while logging. Look for tearing, flicker or corruption. Move
   through menus during writes. This is the SPI sharing check and it must be
   clean for at least 10 minutes.
6. Pull the card without unmounting. Confirm the log reports write failures and
   disables logging instead of spinning.
7. Reinsert, power cycle, confirm a fresh file and a clean mount.

GPX validity:

1. Log at least 20 points, then power off cleanly through the Off menu.
2. Read the card on a workstation. Validate against the GPX 1.1 schema with
   `xmllint --schema gpx.xsd`. It must pass.
3. Repeat, but pull the battery mid session instead of powering off cleanly.
   Validate again. With the rewound closer trick it must still pass.
4. Open both files in a GPX reader and confirm the track looks like the route
   taken.

Settings export and import:

1. Change brightness, theme, TX power and interval. Export.
2. Read the file. Every setting from the table must appear.
3. Erase NVS. Boot. Import. Restart. Confirm all four values came back.
4. Corrupt the file by hand: unknown key, out of range brightness, truncated
   struct hex. Import each. The device must reject the bad entries, log them, and
   stay usable.

On device, M5Stack Core:

1. Repeat steps 3 to 7 above. The Core uses MISO G19 instead of G38 and has no
   touch, so the display timing profile differs. The SPI sharing check must be
   repeated here, not assumed.

Camera check, Fujifilm only:

1. Connect to a Fujifilm body with GPX logging and GPS on. Fire 20 frames.
   Confirm the GEOTAG path still works and no shutter is missed while a flush is
   in progress.
2. Run 30 minutes connected with logging on. No disconnects.

Battery impact, on board instrumentation only:

1. Unplug USB. Log battery voltage every 30 s.
2. 30 minutes with GPS on and GPX off, then 30 minutes with GPS on and GPX on at
   a 5 s period. Record both slopes in the PR body. A mounted card is expected to
   cost a measurable amount, which is why the default is off.

## References

All links fetched and checked.

- M5Stack Core Basic product page, TF slot pins MOSI G23, MISO G19, SCK G18,
  CS G4: https://docs.m5stack.com/en/core/basic
- M5Stack Core2 product page, TF slot pins CS G4, MOSI G23, MISO G38, SCK G18,
  and the display on the same SCLK, MOSI and MISO:
  https://docs.m5stack.com/en/core/core2
- M5Unified SD pin table and `getPin(pin_name_t::sd_spi_cs)`, the runtime
  capability probe: https://github.com/m5stack/M5Unified/blob/master/src/M5Unified.cpp
- M5GFX ESP32 SPI init, showing `spi_bus_initialize` and `spi_bus_add_device` for
  the panel and the "need SD card access (full duplex setting)" comment:
  https://github.com/m5stack/M5GFX/blob/master/src/lgfx/v1/platforms/esp32/common.cpp
- ESP-IDF v5.4.2 FATFS, `esp_vfs_fat_sdspi_mount` signature, the requirement that
  the caller initialises the SPI bus first, mount config fields, and
  `CONFIG_FATFS_IMMEDIATE_FSYNC`:
  https://docs.espressif.com/projects/esp-idf/en/v5.4.2/esp32/api-reference/storage/fatfs.html
- ESP-IDF v5.4.2 SD SPI host driver:
  https://docs.espressif.com/projects/esp-idf/en/v5.4.2/esp32/api-reference/peripherals/sdspi_host.html
- ESP-IDF v5.4.2 SD pull up requirements, relevant when a card fails to enumerate:
  https://docs.espressif.com/projects/esp-idf/en/v5.4.2/esp32/api-reference/peripherals/sd_pullup_requirements.html
- ESP-IDF v5.4.2 virtual filesystem, for the `/sd` mount point semantics:
  https://docs.espressif.com/projects/esp-idf/en/v5.4.2/esp32/api-reference/storage/vfs.html
- GPX 1.1 schema documentation, TopoGrafix:
  https://www.topografix.com/GPX/1/1/
- GPX 1.1 XSD, used for `xmllint` validation in the verification steps:
  https://www.topografix.com/GPX/1/1/gpx.xsd
- GPX developer manual, element ordering inside `trkpt`:
  https://www.topografix.com/gpx_manual.asp
- ESP-IDF v5.4.2 NVS, background for the settings export format decision:
  https://docs.espressif.com/projects/esp-idf/en/v5.4.2/esp32/api-reference/storage/nvs_flash.html
