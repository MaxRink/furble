# Provisioning schema and wire-ID ledger follow-up

## State

Implemented on master-derived work. Wire ID 43 (`AUTO_OFF_CHARGING`) now has
the BOOL, one-byte `ProvisionTLV` schema it was missing. The existing all-keyed
settings schema walk now has no exemptions, and the provisioning apply test
covers both valid boolean values and rejection of value 2.

The reservation documentation now reflects the current master, including its
conditional display, MQTT, and S3 watchdog settings, and five open
PR heads: #59 uses 75/76, #90 uses 62, and #273 uses 65. IDs 42 and 45 retain
historical claims and are not allocated or reused without a compatibility
audit.

## Deviations and remaining gates

The generated golden corpus is unchanged after regeneration, and protocol
conformance passed. Both `provision_apply_test` and
`provision_apply_mqtt_test` passed through CTest. Full host, firmware, and
broader CI validation remain separate gates. The duplicate check covers the
checked-in master settings table; cross-PR collision review remains a rebase-time
operation because open-head state is external to the repository. Issues #288 and
#280 remain open until their complete review gates are satisfied. The executable
reservation mirror rejects duplicate reserved IDs and any reserved ID already
present in the master settings table, but it does not replace the live open-head
audit.
