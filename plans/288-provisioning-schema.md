# 288 - provisioning schema and wire-ID ledger follow-up

## State

Merged on master with PR #273 (merge commit `2e3491de`). Wire ID 43
(`AUTO_OFF_CHARGING`) now has the BOOL, one-byte `ProvisionTLV` schema it was
missing. Wire ID 46 (`IMU`) already had its row by that point. `SETTING_SCHEMAS`
now covers every source-exposed nonzero wire id, and the provisioning apply test
walks every `Settings::Key` with a wire id and fails on a missing row. The test
also covers valid boolean values and rejection of value 2.

The current master ledger (2026-11-02, `ea86482a`) is:

| Owner | Wire ids |
| --- | --- |
| Master | 1-22, 24-35, 37-41, 43-44, 46-47, 51-55, 62, 65-69, 72-76 |
| Master, conditional | 23 (`WATCHDOG` on `FURBLE_M5STICKS3`), 36 (`DISPLAY_MODE` without `FURBLE_NO_DISPLAY`), 56-61 (`FURBLE_MQTT`) |
| Historical | 42, 45: keep reserved; a compatibility audit is required before reuse |
| Free | 48-50, 63-64, 70-71 |
| Open PRs #317, #318, #319 | none |

The matching reservation table and rebase rule live in
[include/CLAUDE.md](../include/CLAUDE.md).

## Deviations and remaining gates

The generated golden corpus is unchanged after regeneration, and protocol
conformance passed. `provision_apply_test`, `provision_apply_mqtt_test`, and
`settings_table_test` pass on master. Full firmware and broader CI validation
remain separate gates. The duplicate check covers the checked-in master settings
table; cross-PR collision review remains a rebase-time operation because
open-head state is external to the repository. The executable reservation check
parses the authoritative ledger instead of duplicating its constants, rejects
duplicate owners, and requires Master rows to match the source table, but it does
not replace the live open-head audit.

## Issue disposition

- **#288** is satisfied by the 43/46 schema rows and the all-keyed walk. It can
  close after this plan update.
- **#280** is satisfied by the refreshed ledger and the absence of any wire-id
  claim on the three open PR heads. The only residual action is the documented
  rebase-time re-audit; it is not an open implementation task. It can close
  after this plan update.
