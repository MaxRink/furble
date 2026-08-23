# 113 - Per-board OTA slot sizing for boards with spare flash

Status: design only. Extends `plans/34-ota-partitions.md`, which already landed
the uniform two-slot layout. Foundational for the flasher/OTA/app/sim track:
sequence this FIRST.

**Codex-implementable.** Pure build-system and sdkconfig work with a host-side
partition-decode check. No network, no hardware needed to self-verify the claim
this doc makes. One residual hardware step (a real non-destructive reflash)
stays with Claude.

## Motivation

`plans/34-ota-partitions.md` moved all five environments to the stock
`partitions_two_ota_large.csv`, a single uniform layout with two 1700K app
slots. That was the right first move: one layout, one ceiling, one migration,
and it is already in `platformio.ini:11` on fork master today. Section "Per
flash size variants: not now" of plan 34 deliberately deferred larger slots
until a measured image approached the 1700K ceiling.

Two things now push against that ceiling:

- `plans/33-wifi-hub.md` adds `esp_wifi`, `esp_netif`, lwIP, esp-mqtt and
  mbedTLS with a certificate bundle. Plan 33's own budget section measured the
  S3 release image at 1,034,256 bytes and called WiFi plus TLS "plausibly most
  of" the 686 KB of headroom. The S3 MQTT build is the tight one: the roadmap
  notes it at roughly 99.7% of a slot once WiFi, MQTT and TLS are in.
- `plans/42-waveshare-eth-node.md` is a 16 MB board that wants TLS, MQTT and
  BLE resident at once.

The uniform 1700K ceiling is set by the smallest boards (three 4 MB
environments). The 8 MB S3 and the 16 MB Core2 have flash to spare that the
uniform layout leaves unused. This doc gives the boards with spare flash a
larger OTA app slot, per board, without moving `nvs` or `otadata`, so the WiFi
and MQTT track fits with real headroom.

## Verified current state (fork master)

- `platformio.ini:11`, shared `[env]`:
  `board_build.partitions = partitions_two_ota_large.csv`. All envs share it.
- Per-board flash (from `plans/34-ota-partitions.md`, cross-checked against the
  board defs): `m5stick-c` 4 MB, `m5stick-c-plus` 4 MB (this env ships the same
  binary to Plus, Plus2 and Plus SE, so 4 MB is binding), `m5stack-core` 4 MB,
  `m5stack-core2` 16 MB, `m5stick-s3` 8 MB. The task brief's "S3 = 16 MB" is
  wrong: the S3 devkit is 8 MB. Design against the verified sizes.
- Stock `partitions_two_ota_large.csv` on 4 MB resolves to
  `nvs @0x9000/24K`, `otadata @0xf000/8K`, `phy_init @0x11000/4K`,
  `ota_0 @0x20000/1700K`, `ota_1 @0x1d0000/1700K`.

## Scope

In scope:

- Two committed per-board partition CSVs at the repo root:
  - `partitions_two_ota_8m.csv` for the 8 MB S3: two 3 MB (3072K) slots.
  - `partitions_two_ota_16m.csv` for the 16 MB Core2: two 6 MB (6144K) slots.
- Per-env `board_build.partitions` overrides in `platformio.ini` for
  `m5stick-s3` and `m5stack-core2` (and their `-debug` variants and the
  `esp32-s3-headless` env, which is 8 MB S3 devkit).
- Per-env sdkconfig changes for the two boards:
  `CONFIG_PARTITION_TABLE_CUSTOM=y`,
  `CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions_two_ota_Nm.csv"`,
  `CONFIG_PARTITION_TABLE_TWO_OTA_LARGE` cleared.
- Keep the three 4 MB envs (`m5stick-c`, `m5stick-c-plus`, `m5stack-core`) on
  the stock uniform 1700K layout unchanged.

Out of scope:

- Any change to `nvs` or `otadata` offset or size on any board. That is the
  non-negotiable migration invariant, see Risks.
- The OTA delivery code. That is `plans/115-ota-update-mqtt.md`.
- A per-flash-size app feature split. The larger slot is headroom for the same
  feature set, not a licence to grow the S3 image past what the 4 MB boards
  carry unless a deliberate later decision says so.

## New CSVs

`partitions_two_ota_8m.csv` (generated form on 8 MB, `nvs`/`otadata` byte
identical to stock):

```
# Name,   Type, SubType, Offset,   Size
nvs,      data, nvs,     0x9000,   0x6000
otadata,  data, ota,     0xf000,   0x2000
phy_init, data, phy,     0x11000,  0x1000
ota_0,    app,  ota_0,   0x20000,  0x300000
ota_1,    app,  ota_1,   0x320000, 0x300000
```

`ota_1` ends at 0x620000, leaving 1920K free tail on 8 MB.

`partitions_two_ota_16m.csv` (16 MB Core2):

```
# Name,   Type, SubType, Offset,    Size
nvs,      data, nvs,     0x9000,    0x6000
otadata,  data, ota,     0xf000,    0x2000
phy_init, data, phy,     0x11000,   0x1000
ota_0,    app,  ota_0,   0x20000,   0x600000
ota_1,    app,  ota_1,   0x620000,  0x600000
```

`ota_1` ends at 0xC20000, leaving 3968K free tail on 16 MB.

Both keep `nvs @0x9000/0x6000` and `otadata @0xf000/0x2000` exactly where the
stock uniform table put them, so a device already on the uniform layout keeps
its settings and camera bonds across the reflash to the larger layout.

## Settings and defaults

No runtime settings. This is a build-time layout change. Defaults preserve
current behavior on the three 4 MB boards (bit-for-bit same partition table)
and enlarge the slot only on the two boards that have the flash.

## Dependencies

- `plans/34-ota-partitions.md` (PR34a-1): landed. This doc edits the layout it
  established.
- Feeds `plans/33-wifi-hub.md` PR33c and `plans/42-waveshare-eth-node.md`: both
  gain real S3/16 MB headroom. Land 113 before measuring the WiFi+MQTT+TLS
  image against a slot.
- No dependency on WiFi, MQTT, companion or the sim. **Startable NOW.**

## Risks

- **`nvs`/`otadata` must not move.** If a generated offset drifts, existing
  devices lose settings and bonds on the reflash. The self-verify decode below
  asserts the offsets byte for byte against the current uniform layout. This is
  the single highest-value check.
- **The larger slot invites divergence.** The S3 could grow past 1700K while the
  4 MB boards cannot. State in the PR body that the larger slot is coexistence
  and TLS headroom, not a second feature set. If a feature genuinely needs the
  extra space on S3 only, that is a separate, argued decision.
- **Changing slot size is another full reflash.** Users already reflashed once
  for plan 34. A second reflash to the larger layout is a second cable. Frame it
  in release notes exactly as plan 34 did: settings and bonds survive because
  `nvs` does not move; decline the erase prompt.
- **`ota_data_initial.bin` size is unchanged (8K).** The web installer manifest
  offset for it (0xf000) does not change. Confirm the release workflow still
  emits it for the two custom-table boards.
- Nothing here is vendor specific. No camera coverage risk.

## Codex self-verification (headless, no hardware)

All five envs must build and every app must fit its slot, and the decoded table
must keep `nvs`/`otadata` fixed.

```
export FURBLE_VERSION=dev FURBLE_TEST=0
# 1. All five release envs plus the headless env build.
pio run -e m5stick-c -e m5stick-c-plus -e m5stack-core \
        -e m5stack-core2 -e m5stick-s3 -e esp32-s3-headless

# 2. Size fits the per-board slot. 4 MB boards: < 1740800. S3/headless: <
#    3145728. Core2: < 6291456. -t size prints the app partition usage.
for e in m5stick-c m5stick-c-plus m5stack-core; do \
  pio run -e $e -t size; done   # assert app < 1740800
pio run -e m5stick-s3 -t size            # assert app < 3145728
pio run -e esp32-s3-headless -t size     # assert app < 3145728
pio run -e m5stack-core2 -t size         # assert app < 6291456

# 3. Decode the built tables. nvs and otadata MUST be unchanged everywhere;
#    ota_0/ota_1 offsets and sizes must match this doc per board.
GEN=~/.platformio/packages/framework-espidf/components/partition_table/gen_esp32part.py
for e in m5stick-c m5stick-c-plus m5stack-core m5stack-core2 m5stick-s3; do \
  echo "== $e =="; python3 "$GEN" .pio/build/$e/partitions.bin; done
```

Pass criteria, all machine-checkable from the decode output:

- Every env: `nvs` at offset 0x9000 size 0x6000; `otadata` at 0x0f000 size
  0x2000.
- `m5stick-c`, `m5stick-c-plus`, `m5stack-core`: `ota_0 @0x20000` size 0x1a9000,
  `ota_1 @0x1d0000`.
- `m5stick-s3`: `ota_0 @0x20000` size 0x300000, `ota_1 @0x320000`.
- `m5stack-core2`: `ota_0 @0x20000` size 0x600000, `ota_1 @0x620000`.
- Each `firmware.bin` is smaller than its board's `ota_0` size.

A Codex agent proves the whole design headless with the three commands above. No
broker, no radio, no phone.

## Residual (Claude / hardware) verification

- On a real S3 already running the uniform-layout release with saved cameras and
  non-default settings, `pio run -e m5stick-s3 -t upload` (which does not erase),
  confirm it boots, the cameras are still listed, settings survived, and it
  connects to the Fujifilm without re-pairing. This is the non-destructive
  migration claim and only hardware proves it.
- Repeat on a real Core2 (16 MB) if available.
- Confirm BLE still works after the reflash (RF calibration in `phy_init` is
  unchanged in offset here, so this should be a formality, but check RSSI at a
  fixed distance against a pre-reflash reading).
