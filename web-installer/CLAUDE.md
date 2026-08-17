# web-installer/

ESP Web Tools flashing page. Manifests are generated at release time, not
committed.

- `generate-manifest.py` renders `manifest.tmpl` from the `PLATFORM` and
  `VERSION` env vars, one manifest per board env.
- Flash offsets differ by chip: bootloader at 0x1000 on ESP32, 0x0 on
  ESP32-S3. Shared offsets: partition table at 0x8000, otadata at 0xf000,
  app at 0x20000. Keep both `builds` entries in sync when changing
  partitions.
- Firmware binary names follow `furble[-part]-$PLATFORM-$VERSION.bin`. Any
  rename must match the release workflow in `.github/workflows/`.
