# Boot timing

`boot timing` prints the retained boot timeline from the developer console. The
timeline is a fixed array of `esp_timer_get_time()` marks, recorded once per
mark with no heap allocation. It is compiled into every build but only the
debug console exposes it.

```sh
pio device monitor -e m5stick-s3-debug
furble> boot timing
```

The same timeline is printed once as a single `I (...) boot: timeline ...` line
after the first UI frame or the first headless service pass.

## Reading the output

```
boot.origin: app_main
boot.reset_to_app_ms: 2310
boot.reset_note: cycle counter, includes ROM bootloader and PSRAM init
mark.app_main.at_ms: 0
mark.app_main.delta_ms: 0
mark.psram_ready.at_ms: 0
mark.psram_ready.delta_ms: 0
mark.settings_done.at_ms: 12
...
boot.not_recorded: ...
```

- `mark.<name>.at_ms` is milliseconds from the first `app_main` statement.
- `mark.<name>.delta_ms` is the gap from the previous recorded mark in time
  order.
- `boot.reset_to_app_ms` is the CPU cycle-counter timestamp at `app_main`. It
  includes the ROM bootloader and the PSRAM boot init, which happen before the
  microsecond timeline starts. Add it to `at_ms` to estimate the time since
  reset.
- `psram_ready` has the same timestamp as `app_main` because PSRAM is already
  initialised before `app_main` runs.
- `boot.not_recorded` lists marks that never occurred, for example `wifi_ip`
  when WiFi is disabled.

The reset estimate uses the same 32-bit cycle counter as the early ESP-IDF log
timestamp. It is accurate for the first boot tens of seconds and wraps after
about 53 seconds at 80 MHz, so it is only sampled once at `app_main`.

## Baseline and expected change

The only hardware measured for this change is an M5StickS3 debug build with
MQTT and WiFi enabled. The "before" rows are the serial log timestamps supplied
with the boot report. The "after" rows are expected values, not measurements.
Run `boot timing` on the device to replace them. No hardware was flashed by the
agent that produced this change.

| Event | Before, ms since reset | After, expected | Basis |
| :--- | ---: | ---: | :--- |
| ROM/bootloader to `app_main` | 1300 | 1300 | Unchanged, outside the firmware timeline |
| PSRAM ready | 2000 | 1300 | Release builds drop the full PSRAM memory test; debug keeps it. Inferred from the 700 ms gap and the IDF test comment of about 1 s per 4 MiB |
| M5GFX board detect | 2300 | 1600 | Follows the PSRAM saving |
| BLE controller init | 2900 | 2200 | Follows the PSRAM saving |
| Console ready | 3200 to 3600 | 2500 to 2900 | Follows the PSRAM saving |
| WiFi associated | 4600 | 3900 to 4300 | Follows the PSRAM saving and overlaps companion/MQTT init |
| DHCP/IP | 5700 | 5000 to 5400 | Follows association |
| MQTT connected | 6800 | 6100 to 6500 | Follows IP |
| UI settled | 12800 | 12100 to 12500 | Follows the PSRAM saving. The UI construction cost itself is not changed here |

The PSRAM row applies only to release S3 builds. Every S3 debug build keeps
`CONFIG_SPIRAM_MEMTEST=y` through `sdkconfig.spiram-memtest`, so a debug boot
still pays the full test. Merge that fragment into another environment when a
full PSRAM check is needed:

```ini
board_build.esp-idf.sdkconfig_fragment = sdkconfig.debug,sdkconfig.spiram-memtest
```

Expected saving summary:

- PSRAM memory test: about 700 ms on a release S3 boot, inferred from the
  reported app-start to PSRAM-ready gap. The debug profile keeps the test, so a
  StickS3 debug boot still pays it.
- Settings seed: one NVS commit per namespace plus one marker commit on a fresh
  store, and one marker read per later boot. Exact counts are pinned by
  `settings_boot_test`; the time saving has not been measured on hardware.
- WiFi overlap: 100 to 400 ms earlier IP, inferred from the init that now runs
  under the join. It does not move WiFi ahead of BLE or the TLS identity.
- Boot splash: 0 ms on the measured boot, up to 700 ms on a boot that would
  otherwise hold the splash past the minimum.
- UI construction: no change in this PR.

The settings seed and WiFi changes are not visible in the table because no
hardware measurement of them exists yet. The settings change is counted by the
host test:

- A fresh store uses one NVS commit for the `M5ez` defaults, one for the
  `FURBLE_STR` defaults, and one for the version marker. The old path used one
  commit per missing key.
- A boot with the current marker opens only the marker and performs no writes.
  The old path opened every setting namespace and probed every key.
- An upgrade performs one full scan and never overwrites a stored value. A
  failed default commit leaves the marker unwritten, so the next boot rescans
  and retries the seed.

The WiFi change moves association ahead of companion and MQTT setup, so their
init overlaps the join. It does not start WiFi before BLE or the WebUI TLS
identity. That ordering is intentional: the identity generator needs the
unfragmented internal heap, and BLE controller init should not race the first
scan.

## Expected numbers per board

Only the M5StickS3 rows are measured. Treat every other board as a prediction
until `boot timing` is captured on it.

| Board | PSRAM test removed | Settings seed | WiFi overlap | UI marks |
| :--- | :--- | :--- | :--- | :--- |
| M5StickS3 | Yes, release only | Yes | Yes | Yes |
| M5Stack Core2 | No PSRAM | Yes | Yes | Yes |
| M5StickC / Plus / Plus2 | No PSRAM | Yes | If enabled | Yes, Plus2 differs by panel |
| M5Stack Core | No PSRAM | Yes | If enabled | Yes |
| esp32-s3-headless | Yes | Yes | Yes | Headless service marks only |
| Waveshare ESP32-S3-ETH | Yes | Yes | After Ethernet init | Headless service marks only |

## What still needs hardware measurement

- Confirm the release PSRAM saving, because the memtest runs before the
  firmware timeline starts. Compare the serial line before `app_main` on a
  release build with and without `sdkconfig.spiram-memtest`.
- Capture `mark.ui_ctor_start` through `mark.ui_first_frame` on the StickS3. The
  reported 12.8 s UI settle leaves about 6 s after MQTT connects. This change
  instruments that span but does not change it.
- Capture `settings_done` on a device that is upgrading from a build without
  the marker. The first boot performs the scan; later boots should show a
  near-zero delta.
- Capture `wifi_started`, `wifi_assoc`, and `wifi_ip` on Core2 and the headless
  S3 build. Their radio timing is not measured.

## Tradeoffs

- The release S3 images no longer abort at boot on a bad PSRAM part or a wrong
  PSRAM mode. The debug profiles keep the test, and the fragment above restores
  it for any custom environment. A bad part still shows up as an allocation
  failure or a crash instead of a named early boot failure.
- The settings version marker is a boot-time cache. A missing or corrupt marker
  causes one full scan. A single key lost while the marker survives is not
  reseeded at boot. This is the same kind of partial NVS damage that already
  makes an individual load return its zero value. Bump
  `SettingsBootstrap::DEFAULTS_VERSION` whenever a default changes or a setting
  is added.
- The direct WiFi join uses the remembered BSSID and channel. A failed first
  join now clears that memory and runs one scan, so a moved access point
  recovers after one retry instead of being retried through the backoff. A
  transient failure during the first join also costs one scan.
- NTP still starts only after an IP address, and MQTT still waits for the
  network. Neither blocks the UI or the console. The WebUI TLS identity remains
  before WiFi because generating it after the radio and display allocations
  fragments the internal heap.

## Related references

- [Console commands](console-commands.md)
- [Settings and controls reference](settings-and-controls.md)
- [Supported hardware](supported-hardware.md)
