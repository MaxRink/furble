# Provisioning schema and wire-ID ledger follow-up

## State

Implemented on master-derived work. Wire ID 43 (`AUTO_OFF_CHARGING`) now has
the BOOL, one-byte `ProvisionTLV` schema it was missing. The existing all-keyed
settings schema walk now has no exemptions, and the provisioning apply test
covers both valid boolean values and rejection of value 2.

The reservation documentation now reflects the current master and five open
PR heads: #59 uses 75/76, #90 uses 62, and #273 uses 65. IDs 42 and 45 remain
reserved historical assignments and are not declared reusable.

## Deviations and remaining gates

Golden fixtures were intentionally not regenerated in this change. The root
agent must run the protocol fixture generator and review the resulting corpus
delta. Builds and tests are also pending. The duplicate check covers the
checked-in master settings table; cross-PR collision review remains a rebase-time
operation because open-head state is external to the repository.
