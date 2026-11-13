# PR31: enable PSRAM on the M5StickS3

## Goal

Enable the 8 MB octal PSRAM on the M5StickS3 and let the standard allocator use
it. Large heap allocations move to external RAM. Internal DMA capable RAM is
freed for BLE, FreeRTOS stacks and the LVGL display buffers. No source change is
needed. The change is confined to `sdkconfig.m5stick-s3`, so the other four
boards are untouched.

## Motivation

The `combined` integration branch boot loops on the M5StickS3. The panic is a
heap allocation failure:

```
Mem alloc fail. size 0x00003003 caps 0x00001800
```

The failure happens when the scan page redraws while a BLE scan is running.
Decoding the numbers:

- `caps 0x1800` is `MALLOC_CAP_INTERNAL | MALLOC_CAP_DEFAULT`. That is the exact
  mask `heap_caps_malloc_default()` uses when external allocations are disabled
  (`components/heap/heap_caps.c:109-110` in ESP-IDF 5.4.2). It proves the request
  came from plain `malloc()` and that the allocator had no PSRAM to fall back on.
- `size 0x3003` is 12291 bytes. The icons in `components/icons` are 64x64
  `LV_COLOR_FORMAT_RGB565A8` images with `LV_IMAGE_FLAGS_COMPRESSED`. Decompressed
  they are 64 rows of 128 byte stride for the RGB565 plane plus 64x64 bytes for
  the A8 plane. That is 8192 + 4096 = 12288 bytes, plus 3 bytes of header. The
  number identifies the allocation precisely.

So the failing allocation is the LVGL bin decoder inflating one icon.
`CONFIG_LV_BIN_DECODER_RAM_LOAD=y` and `CONFIG_LV_USE_LZ4=y` are both set, so
every compressed icon is expanded into a fresh heap buffer on first draw. LVGL
uses `CONFIG_LV_USE_CLIB_MALLOC=y`, so `lv_malloc()` is the C library `malloc()`.

12 KB is not a large request. It fails because the ESP32-S3 has only 512 KB of
internal SRAM, NimBLE holds a large share of it while scanning, and the S3 build
currently ignores the PSRAM completely. `sdkconfig.m5stick-s3:1375` says
`# CONFIG_SPIRAM is not set` even though `CONFIG_SOC_SPIRAM_SUPPORTED=y` at line
236. The StickS3 uses an ESP32-S3-PICO-1-N8R8 module. That is 8 MB flash and
8 MB octal PSRAM, both inside the module. The build has been leaving 8 MB of RAM
switched off.

With PSRAM enabled and `CONFIG_SPIRAM_USE_MALLOC=y`, the same 12 KB request is
served from external RAM and internal RAM stays available for the things that
actually need it.

## Draft issue

The M5StickS3 build does not enable the module's PSRAM. The StickS3 carries an
ESP32-S3-PICO-1-N8R8 with 8 MB of octal PSRAM, but `sdkconfig.m5stick-s3` has
`# CONFIG_SPIRAM is not set`, so the firmware runs entirely out of the 512 KB of
internal SRAM. This is tight enough to fail in normal use. On a development
branch the device boot loops with `Mem alloc fail. size 0x00003003 caps
0x00001800` when the LVGL bin decoder inflates a 64x64 compressed icon during a
redraw while a BLE scan holds internal heap. Enabling PSRAM would remove that
class of failure and give headroom for later features.

## Scope

- `sdkconfig.m5stick-s3` only.
- Octal PSRAM, boot time init, integrated into `malloc()`.
- No source change. LVGL keeps using the C library allocator.
- The LVGL display buffers stay in internal DMA memory. They are already
  allocated with explicit capability flags and must not change.
- Out of scope: moving `.bss` to PSRAM, XiP from PSRAM, 120 MHz PSRAM, raising
  the data cache to 64 KB, and any other board.

## Files to change

| File | Change |
|---|---|
| `sdkconfig.m5stick-s3` | enable `CONFIG_SPIRAM` and the octal PSRAM options |

No other file changes. `src/FurbleUI.cpp` and `include/FurbleUI.h` were checked
and need no edit. See the implementation notes.

## Configuration delta

Symbols set by hand:

| Symbol | Value | Reason |
|---|---|---|
| `CONFIG_SPIRAM` | `y` | turn on external RAM support |
| `CONFIG_SPIRAM_MODE_OCT` | `y` | the N8R8 module is octal PSRAM |
| `CONFIG_SPIRAM_SPEED_80M` | `y` | 80 MHz is the fastest non experimental speed for octal |
| `CONFIG_SPIRAM_BOOT_INIT` | `y` | initialise during startup so boot allocations can use it |
| `CONFIG_SPIRAM_USE_MALLOC` | `y` | integrate PSRAM into `malloc()` |
| `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL` | `4096` | anything above 4 KB prefers PSRAM |
| `CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL` | `32768` | ESP-IDF default, keep a reserve for DMA and stacks |

Kconfig then derives the rest. Build once and commit the file the build
produces, so the checked in `sdkconfig.m5stick-s3` stays self consistent. The
derived symbols that matter:

| Symbol | Value | Note |
|---|---|---|
| `CONFIG_SPIRAM_TYPE_AUTO` | `y` | detect the chip at boot |
| `CONFIG_SPIRAM_SPEED` | `80` | follows `SPIRAM_SPEED_80M` |
| `CONFIG_SPIRAM_MEMTEST` | `y` | default, aborts boot on a bad part |
| `CONFIG_SPIRAM_PRE_CONFIGURE_MEMORY_PROTECTION` | `y` | follows `SPIRAM_BOOT_INIT` |
| `CONFIG_STDATOMIC_S32C1I_SPIRAM_WORKAROUND` | `y` | the S32C1I atomic instruction does not work on PSRAM, so atomics fall back to a lock |
| `CONFIG_ESP_SLEEP_PSRAM_LEAKAGE_WORKAROUND` | `y` | pull up the PSRAM CS line during light sleep |
| `CONFIG_ESP32S3_SPIRAM_SUPPORT` | `y` | deprecated alias, regenerated automatically |
| `CONFIG_FATFS_ALLOC_PREFER_EXTRAM` | `y` | inert, no FATFS in this firmware |
| `CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM` | `y` | permits `xTaskCreateWithCaps()` in PSRAM, nothing here uses it |
| `CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY` | `y` | same, ordinary `xTaskCreate()` stacks stay internal |

Two symbols disappear because they become invisible under `SPIRAM`:
`CONFIG_ESP_SLEEP_POWER_DOWN_FLASH` and its alias `CONFIG_ESP_SYSTEM_PD_FLASH`.
Both were already `n`, so there is no behavior change today. See the risks.

NimBLE stays internal. `CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_INTERNAL=y` and
`CONFIG_MBEDTLS_INTERNAL_MEM_ALLOC=y` are untouched. The controller needs
internal DMA memory, so leave them alone.

## Implementation notes

### Why no LVGL change is needed

`sdkconfig.m5stick-s3:2444` sets `CONFIG_LV_USE_CLIB_MALLOC=y`. In LVGL 9.4 that
selects `LV_STDLIB_CLIB`, and `src/stdlib/clib/lv_mem_core_clib.c` implements
`lv_malloc_core()` as a direct call to `malloc()`. So every LVGL allocation
already goes through the ESP-IDF heap.

With `CONFIG_SPIRAM_USE_MALLOC=y`, `malloc()` resolves to
`heap_caps_malloc_default()`. That function compares the request against
`CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL`:

- at or below the limit it tries `MALLOC_CAP_DEFAULT | MALLOC_CAP_INTERNAL`
- above the limit it tries `MALLOC_CAP_DEFAULT | MALLOC_CAP_SPIRAM`
- either way, on failure it retries with plain `MALLOC_CAP_DEFAULT`, which can
  come from either pool

The 12291 byte icon buffer is above a 4096 byte limit, so it lands in PSRAM
directly. Small LVGL objects, styles and event descriptors stay internal, which
keeps the UI responsive. This is the whole fix. `CONFIG_LV_USE_CUSTOM_MALLOC`
with a `heap_caps_malloc()` shim was considered and rejected. It adds a source
file, an `lv_conf` override and a maintenance burden for no gain over the
threshold mechanism.

### Choosing the threshold

The ESP-IDF default for `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL` is 16384. That is
above the 12291 byte icon buffer, so the default would still route the icon to
internal memory first and only reach PSRAM through the fallback retry. That
works, but it keeps pressure on internal RAM and makes the outcome depend on
fragmentation. 4096 is deliberate. It is large enough that ordinary small
allocations are unaffected, and small enough that every buffer sized allocation
in the UI path prefers external RAM.

`CONFIG_LV_DRAW_LAYER_SIMPLE_BUF_SIZE` is 24576, so LVGL layer buffers also move
to PSRAM under this threshold. Those are read and written by the software
renderer, not by DMA, so PSRAM is correct for them. Watch the frame rate during
verification.

### Display buffers stay internal

`src/FurbleUI.cpp:125-127` allocates both display buffers with
`heap_caps_aligned_alloc(64, BUFFER_SIZE, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL)`.
`MALLOC_CAP_DMA` already forces internal memory on the S3, and
`MALLOC_CAP_INTERNAL` states it a second time. `src/FurbleUI.cpp:376` hands those
buffers to `M5.Display.pushImageDMA()`. Nothing here changes and nothing here may
change. `CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL=32768` exists to keep this kind of
allocation working when the general heap is busy.

`BUFFER_SIZE` is `MAX_WIDTH * (MAX_HEIGHT / 15) * BYTES_PER_PIXEL`
(`include/FurbleUI.h:196`), so the two buffers are small. They are not the
problem and they are not the fix.

### Module pins

Octal PSRAM on the ESP32-S3 uses GPIO 33 to 37 for the extra data lines and the
DQS signal. On the PICO-1-N8R8 those pins are bonded inside the module and are
not brought out to the StickS3 headers, so there is no pin conflict to resolve.
Flash stays QIO at 40 MHz. Only `CONFIG_SPIRAM_MODE_OCT` changes, not
`CONFIG_ESPTOOLPY_OCT_FLASH`.

## Risks

- Wrong PSRAM mode. Quad mode settings on an octal part fail at boot, usually as
  a PSRAM ID mismatch or a memory test failure in `esp_psram_init()`. The module
  is documented as octal, and `CONFIG_SPIRAM_MEMTEST` stays on so a mismatch
  aborts loudly at startup instead of corrupting data later. Verify against the
  boot log line that reports the detected PSRAM size.
- Cache and DMA constraints. PSRAM is reached through the data cache. DMA
  descriptors cannot live in PSRAM, and DMA to and from PSRAM is slower and more
  restricted than to internal RAM. The display path is unaffected because its
  buffers are explicitly `MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL`. Any future code
  that DMAs from a plain `malloc()` buffer would now be at risk, so new DMA
  buffers must use explicit capability flags.
- Cache thrash. External RAM shares the cache region with flash, and working sets
  larger than the 32 KB data cache degrade to external access speed and can evict
  cached flash. `CONFIG_ESP32S3_DATA_CACHE_32KB` stays as it is in this change.
  If the UI feels slower, raising the data cache to 64 KB is the first thing to
  try, in a separate change.
- Rendering slowdown. Layer buffers and decompressed icons now live in slower
  memory. At 135x240 with a partial render mode this should not be visible.
  Measure it rather than assume it.
- PSRAM costs current. The octal PSRAM is powered whenever the chip is out of
  deep sleep. This works against the power goals in PR06 and PR07. Measure idle
  drain before and after.
- Light sleep interaction. Cache and PSRAM are affected by light sleep. The
  StickS3 already disables light sleep while the GPS UART is active
  (`src/FurbleGPS.cpp:120-122`). Re-run the light sleep paths from PR07 on this
  branch before combining them.
- Boot time. `CONFIG_SPIRAM_MEMTEST` adds a memory test over 8 MB at every boot.
  If startup becomes noticeably slower, turning the test off is an option, but
  keep it on until the hardware is proven.
- Flash power down during light sleep is no longer selectable.
  `CONFIG_ESP_SLEEP_POWER_DOWN_FLASH` depends on `!SPIRAM`, because flash and
  PSRAM share the MSPI power domain. It was already off, so nothing regresses
  now, but PR07 loses that option on the S3 unless PSRAM is turned back off.
- Atomics get slower. `CONFIG_STDATOMIC_S32C1I_SPIRAM_WORKAROUND` is switched on
  automatically because the S3 `S32C1I` compare and swap instruction does not
  work against PSRAM. Atomic operations become a lock and unlock pair. This is
  chip wide, not just for PSRAM addresses.
- Firmware grows. The PSRAM driver, the memory test and the MSPI timing tuning
  code add about 27 KB of flash. There is headroom, but note it.

## Verification

Build:

```
FURBLE_VERSION=dev FURBLE_TEST=0 pio run -e m5stick-s3
```

Measured, `master` against this branch, `pio run -e m5stick-s3`:

| | master | this branch | delta |
|---|---|---|---|
| static DRAM | 33924 | 35524 | +1600 |
| flash | 1028996 | 1056200 | +27204 |

Both are small. The static DRAM cost buys 8 MB of heap.

Confirm the other four environments still build and that `git status` shows only
`sdkconfig.m5stick-s3` changed:

```
FURBLE_VERSION=dev FURBLE_TEST=0 pio run -e m5stick-c -e m5stick-c-plus -e m5stack-core -e m5stack-core2
```

On device, pending hardware:

1. Flash and open the serial monitor. Confirm the boot log reports the PSRAM
   size, and that it is 8 MB and octal.
2. Log `heap_caps_get_free_size(MALLOC_CAP_INTERNAL)` and
   `heap_caps_get_free_size(MALLOC_CAP_SPIRAM)` at the end of `setup()`.
   Compare against the same numbers on master.
3. Reproduce the original failure path. Open the scan page with a BLE scan
   running and let the icons decode. On master this is where the panic happens.
   It must not panic here.
4. Repeat with the `combined` branch merged on top. That is the branch where the
   boot loop was first seen, so it is the real acceptance test.
5. Watch for UI slowdown. Compare page transitions and the scan list scroll
   against master by eye, then by the LVGL refresh period if anything looks off.
6. Confirm the GPS UART still works, since it is the other S3 specific path.
7. Measure idle battery drain over 30 minutes and compare with master, to size
   the PSRAM power cost.

## Dependencies

- Independent. It touches one file that no other planned PR touches.
- Should land early. Several later PRs add UI pages and buffers, and all of them
  are easier with 8 MB of headroom.
- Interacts with PR06 and PR07 on power. Re-measure drain once those land.

## References

- [ESP-IDF 5.4.2 support for external RAM, esp32s3](https://docs.espressif.com/projects/esp-idf/en/v5.4.2/esp32s3/api-guides/external-ram.html)
  for the three access methods, `CONFIG_SPIRAM_USE_MALLOC`, the
  `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL` threshold semantics,
  `CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL`, the rule that DMA descriptors cannot
  live in PSRAM, and the note that working sets above 32 KB fall back to external
  access speed.
- ESP-IDF 5.4.2 `components/esp_psram/esp32s3/Kconfig.spiram` and
  `components/esp_psram/Kconfig.spiram.common` for the exact symbol names,
  the `SPIRAM_MODE_QUAD` default that has to be overridden, the
  `SPIRAM_SPEED_40M` default, and the defaults of 16384 and 32768 for the two
  malloc tuning options.
- ESP-IDF 5.4.2 `components/heap/heap_caps.c:107-134` for
  `heap_caps_malloc_default()`, which is where the `caps 0x1800` in the panic
  message comes from and where the threshold is applied.
- [M5Stack StickS3 documentation](https://docs.m5stack.com/en/core/StickS3)
  confirming ESP32-S3-PICO-1-N8R8, 8 MB flash, 8 MB PSRAM in octal mode, and the
  135x240 LCD.
- [LVGL 9.4.0 lv_mem_core_clib.c](https://raw.githubusercontent.com/lvgl/lvgl/v9.4.0/src/stdlib/clib/lv_mem_core_clib.c)
  showing `lv_malloc_core()` calling `malloc()` directly when
  `LV_USE_STDLIB_MALLOC` is `LV_STDLIB_CLIB`.
- [LVGL 9.4.0 Kconfig](https://raw.githubusercontent.com/lvgl/lvgl/v9.4.0/Kconfig)
  for the `LV_USE_BUILTIN_MALLOC` and `LV_USE_CLIB_MALLOC` choice, and for
  `LV_BIN_DECODER_RAM_LOAD` and `LV_USE_LZ4`.
- [LVGL 9.4.0 bundled LZ4](https://raw.githubusercontent.com/lvgl/lvgl/v9.4.0/src/libs/lz4/lz4.h)
  for the decompressor behind `LV_IMAGE_FLAGS_COMPRESSED`.
