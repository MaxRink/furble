# 177 - MQTT 4 MB size follow-up

## Status

The fallback profile is implemented, while the default board matrix, CI
selection, release artifacts, and MQTT enablement remain unchanged.

The current 4 MB Core debug gate is still over its two-OTA slot:

| Artifact | Image | Slot | Over |
| --- | ---: | ---: | ---: |
| c66 MQTT-off Core debug | 1,753,225 | 1,740,800 | 12,425 |

The exact root-run map is
/home/a92615428/wt/c66-8mb-validation/.pio/build/m5stack-core-debug/firmware.map.
RAM was 54,632 bytes. This is a size-gate measurement, not a release claim.

Earlier experiments do not provide a solution. -Oz produced the same image as
-Os. The correctly scoped app-only LTO experiment retained 41 app objects
outside vendor code but grew the image by 3,400 bytes. An earlier broad LTO
experiment saved 10,324 bytes, but it applied LTO to vendor objects and exposed
vendor ODR/type-mismatch diagnostics, so it is invalid for shipping. The
no-MQTT baseline map retains no FurbleMQTT.cpp.o, FurbleOTAMQTT.cpp.o, or
partition-sink flash sections, and retains no TLS archive members or
certificate bundle. Removing those sources cannot be counted as a 4 MB fix.

## Optimization order

1. Measure one Kconfig change at a time on the exact Core debug baseline. Keep
   the 4 MB table, OTA slots, TLS client security, logs, WPA3, existing
   provisioning behavior, and camera behavior unchanged.
2. The first bounded candidate for MQTT-enabled larger targets is mbedTLS
   client-only mode. The production network path uses esp_crt_bundle_attach and
   the MQTT client URI; no Furble source starts an mbedTLS server. This
   candidate is not expected to change the current MQTT-off Core image because
   no TLS archive members are retained there.
3. Do not switch the full certificate bundle to the common bundle without an
   inventory of every supported TLS endpoint and certificate-root coverage.
   That is a trust-store policy change, not a free optimization.
4. Do not remove existing Wi-Fi, WPA3, provisioning scans, TLS, logging, OTA
   rollback, or camera vendors to clear this gate. SoftAP is already outside
   the supported baseline. Any future reduction must have a map delta and
   behavior evidence.

## Explicit non-OTA USB fallback

If the 4 MB OTA image cannot be reduced while retaining the product contract,
provide a named developer or recovery profile with one large app partition.
It must never replace the default 4 MB OTA profile and must not appear in the
release or web-installer matrix.

Use a project-owned table with these exact 4 MB invariants:

    # Name,   Type, SubType, Offset,    Size, Flags
    nvs,      data, nvs,     0x9000,    0x6000,
    phy_init, data, phy,     0x11000,  0x1000,
    factory,  app,  factory, 0x20000,  0x3e0000,

This ends at 0x400000 and gives one 3,968 KiB app partition. The existing
NVS offset and size are unchanged, so settings and camera bonds can survive a
layout flash when the chip is not erased. Keeping phy_init at its current
0x11000 location avoids overlapping it with the app. There is no otadata
partition; any old bytes at 0xf000 are unused by this table.

The profile needs its own sdkconfig and PlatformIO environment:

- select the custom table and clear the two-OTA Kconfig choice;
- keep the app upload offset at 0x20000;
- keep security, logging, and required network features explicit;
- if future OTA-over-MQTT composition becomes reachable, compile it out or
  fail closed for this profile because there is no inactive OTA partition.

The current production source does not wire the OTA-over-MQTT session into
the MQTT client, so removing OTA source files is not a measured saving today.
The fallback is primarily a partition-capacity escape hatch.

## USB and recovery contract

The fallback is USB-only. A first install or recovery writes the bootloader,
partition table, and app at their generated offsets, including the app at
0x20000; it does not use ota_data_initial.bin. Do not list this profile in the
normal web manifest or treat it as OTA-compatible.

There is no wireless update, inactive-slot rollback, or OTA migration path from
this profile. Returning to a normal OTA image requires USB reflashing the OTA
partition table and the app, plus ota_data_initial.bin at 0xf000. That
transition can preserve NVS only if the user does not erase the chip. A full
erase is the recovery option when storage is corrupt, but loses settings and
camera bonds.

## Required validation before enabling the profile

- Parse the custom table and assert flash bounds, app alignment, NVS
  preservation, absence of otadata, and the 3,968 KiB factory capacity.
- Verify generated flash arguments contain bootloader, partition table, and
  factory app only, with app offset 0x20000.
- Build the exact MQTT-enabled and MQTT-off profile separately and record
  image, map, RAM, and feature flags. No size result is implied by this plan.
- Exercise NVS/settings and bonded-camera persistence across a no-erase USB
  table migration, then verify a full-erase recovery.
- Verify that an attempted OTA command is rejected or unavailable in the
  fallback, while the default 4 MB and all 8 MB/16 MB OTA profiles retain
  their current behavior.

## Implementation state

The developer-only `m5stack-core-usb-debug` environment extends the normal Core
debug flags and selects `partitions_single_factory_4m.csv`. Its dedicated
sdkconfig fragment clears the two-OTA choice while retaining the Core debug
configuration. `tools/check_single_factory_profile.py` validates the table
bounds, preserved NVS and PHY locations, single factory app capacity, absent
OTA partitions, the 0x20000 upload offset, and the unchanged published OTA
manifest shape. The profile is intentionally absent from release and
web-installer matrices and is the mandatory Core debug profile in normal CI.
The legacy dual-OTA Core debug profile remains available only through an
explicit workflow-dispatch boolean and fails strictly when selected. Builds,
migration, and hardware recovery remain gates.

## Validation evidence

The serialized root validation passed the new checker and the existing OTA
checker. `tools/check_single_factory_profile.py` passed, and
`tools/check_partition_tables.py` passed all six dual-OTA board invariants.
The clean `m5stack-core-usb-debug` build used the generated custom table and
reported 1,753,225 bytes from 4,063,232 factory-app bytes, with 54,632 bytes
of RAM; the build took 246.51 seconds. Its generated `flash_args` contained
only the ESP32 bootloader at 0x1000, the partition table at 0x8000, and the
factory app at 0x20000, with no `ota_data_initial.bin` or OTA app slot.
Generated sdkconfig selected the custom CSV and retained the debug profiling,
runtime-statistics, LVGL performance, mbedTLS client, and MQTT SSL settings;
the insecure option remained unset. The serialized logs are
`~/b/c66-core-usb-debug-clean.log` and `~/b/c66-core-usb-debug-build.log`.

The no-erase migration, full-erase recovery, and hardware USB behavior remain
unverified. The default OTA profiles and release/web matrices were not
changed. Normal CI now builds the single-factory Core debug profile; the legacy
dual-OTA Core debug build remains an explicit workflow-dispatch opt-in.
