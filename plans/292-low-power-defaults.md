# 292 - low-power defaults on fresh devices

## Motivation

The two largest modelled idle loads were still on their legacy defaults:
`SLEEP_CONN` was off, so a connected StickS3 held the no-light-sleep lock with
the screen off, and `INACTIVITY`/`DISPLAY_OFF` shipped as Never/Dim, so a
forgotten device kept the panel and backlight awake. Plans 98 and the power
matrix rank the connected floor (P-040) and the idle display policy (P-073)
as the top two addressable opportunities.

## Implementation state

Fresh NVS now seeds:

- `INACTIVITY` = 2 (60 secs) instead of 0 (Never).
- `DISPLAY_OFF` = 1 (Off) instead of 0 (Dim), except on the M5Stack Core Basic,
  which keeps Dim because its IP5306 cuts the board below 45 mA after 32 s.
- `SLEEP_CONN` = true on `FURBLE_M5STICKS3` only. Other boards keep false
  because they have no modem-sleep controller or GPS burst lock.

Only an unset key is seeded. An existing device keeps its stored value, so
this is not a migration and does not rewrite user choices.

## Validation

- `settings_nvs_roundtrip_test`: PASS, including the StickS3 defaults.
- New `settings-default-policy`, `settings-default-policy-stickc`, and
  `settings-default-policy-core` targets exercise both sleep-default branches
  and the Core Basic display exception.
- `settings_nvs_roundtrip_test` also seeds legacy Never/Dim/Sleep-off values,
  reruns init, and proves they are preserved.
- Full host CTest: 128/128.
- Certified power-gate scenarios: PASS after refreshing the
  `blind-remote-shutter` baseline. The intended default change moves that
  scenario from 43.65 mA to 4.45 mA because the connected screen-off state now
  light sleeps. The refresh also wires the existing connected-radio floor into
  the model, so the new baseline does not hide that term.
- Representative simulator runs pass: `screen-off-30s`, `menu-idle-30s`,
  `e2e/connect-flow`, `bughunt/power-off-state-matrix`, `e2e/restart-persist`,
  and `e2e/gps-motion-setting`.

## Remaining gates

The power numbers are modelled or inferred, not bench-measured. A per-board
30-minute soak, first-shutter latency after idle, and wake behaviour remain
the release gate. `SLEEP_CONN` is still read at connect time, so a device that
is already connected keeps the setting it started with.
