# 144 - Keep PlatformIO uploads aligned with OTA partitions

## Status

Implemented on the development branch. This is a build-tooling correction for
the per-board OTA layouts from plan 113 and PR 167. The web installer already
uses the same application offset.

## Failure

ESP-IDF's normal PlatformIO build discovers `ota_0` at `0x20000` and writes
that value into `app-flash_args` and `flash_args`. PlatformIO's combined
`-t nobuild -t upload` path can skip that framework discovery and use the
generic board default of `0x10000` instead. Uploading an existing
`firmware.bin` through that path therefore writes the application at the
wrong address. It can corrupt the data area before `ota_0`, leave `otadata`
selecting the old image, and make a successful upload appear ineffective.

## Fix

`platformio.ini` sets the inherited `board_upload.offset_address` to `0x20000`.
This is harmless for normal builds because the ESP-IDF builder computes the
same value from the partition table. It also makes no-build uploads explicit.
The setting does not touch `nvs`, `otadata`, or camera bonds.

`tools/check_partition_tables.py` now checks that the PlatformIO setting,
every explicit environment override, the OTA CSVs, and both web-installer
manifest entries agree on `0x20000`. CI runs this check on the partition-table
validation job.

## Safe recovery

For an already built S3 artifact, use the complete generated map:

```sh
python "$HOME/.platformio/packages/tool-esptoolpy/esptool.py" \
  --chip esp32s3 --port /dev/cu.usbmodem1101 --baud 460800 \
  write_flash -z \
  0x0 .pio/build/m5stick-s3-debug/bootloader.bin \
  0x8000 .pio/build/m5stick-s3-debug/partitions.bin \
  0xf000 .pio/build/m5stick-s3-debug/ota_data_initial.bin \
  0x20000 .pio/build/m5stick-s3-debug/firmware.bin
```

This does not erase NVS. Verify the artifact's `flash_args` before using the
command if the build environment or partition table differs. On the S3, run
the PMIC flash preparation handshake first and restore the watchdog after the
new image boots.
