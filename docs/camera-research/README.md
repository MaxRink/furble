# Camera protocol research

Research snapshot: 2026-09-12. Firmware and official-app verification added
2026-11-01 for Fujifilm X100VI 1.31, X-S20 3.30, Ricoh GR III 2.20, and GR IV /
GR IV HDF 1.20. This library preserves GitHub implementations, official
manuals, source conflicts, firmware-derived UUID tables, and future capture
targets for every real camera family registered in Furble. It contains no
imported implementation or hardware fixture.

| Research notes | Scope | Important boundary |
| --- | --- | --- |
| [Ricoh GR](ricoh.md) | GR III/IIIx BLE, GR II Wi-Fi, GR IV/HDF, GPS and WLAN split | GR III and GR IV share UUIDs but differ in power, WLAN, and pairing; broad PENTAX matching is not validated support |
| [Fujifilm, Canon, Nikon](fuji-canon-nikon.md) | Basic/Secure firmware profiles, Smart/Remote roles, Nikon Classic continuation | Keep model, firmware, and application/accessory roles separate |
| [Fujifilm Secure GATT](fujifilm-secure-gatt.md) | X100VI 1.31 and X-S20 3.30 70-entry UUID table and 13-service layout | Handles and properties are a public template match, not a runtime capture |
| [Sony, Panasonic LUMIX, DJI Osmo](sony-lumix-dji.md) | Remote/GPS protocols, generation-specific login, framing and status | Experimental source paths and official accessory support do not establish Furble compatibility |

FauxNY is documented in the third note as a software-only lifecycle peer. It
cannot establish any physical camera outcome. The deprecated MOBILE_DEVICE
enum is not another supported family.

The exact-model official reference index covers all 26 README-listed models:
[18 Fujifilm models](model-manuals-fujifilm.md) and
[eight other models](model-manuals-other.md). The latter also lists separate
research and experimental targets. Direct-PDF gaps are recorded explicitly;
verified manufacturer landing pages are used instead of guessed URLs.

## How to use this research

[Plan 159](../../plans/159-camera-peer-certification.md) governs implementation
and certification. Official manuals establish user-visible semantics. Public
implementations supply protocol hypotheses and negative-test ideas. Notes
label the inspected Furble behavior as current-source evidence, not independent
confirmation. Only an exact hardware corpus can support a feature-scoped
certified result; missing or conflicting evidence remains `UNCERTIFIED`.

Each note records immutable GitHub revisions, retrieval dates, model/firmware
scope, licensing limits, protocol fields, and unresolved questions. Official
web pages can change; archive the exact manual revision and digest with a
future capture. Verify all source pins again before importing any material.
No-license, copyleft, or mixed-EULA references remain citation-only under this
research plan, not permission to copy code or fixtures into Furble.

## Next evidence work

1. Capture GR III/IIIx dynamic pairing and Wi-Fi-wake behavior separately.
   The Ricoh location-enable UUID and final GPS byte are resolved from firmware
   and the official apps; runtime acceptance still needs a capture.
2. Capture Fujifilm registration, RPA/bonds, the `F9150137` time-request
   semantics, the time packets, Canon role transitions, and Nikon
   BLE-to-Classic continuation. The Secure GATT UUID table is resolved for
   X100VI 1.31 and X-S20 3.30.
3. Resolve Sony button order and contradictory GPS length/flag descriptions;
   capture Lumix readiness/identity and DJI security/MTU/status behavior.
4. Add strict GATT properties, security, asynchronous events, and negative
   cases below the production camera stack, following plan 159. Do not replace
   camera behavior with a permissive universal peer.
5. Bind each fixture to exact camera firmware, Furble/dependency revisions,
   raw trace digests, settings, timing samples, and physical capture/EXIF
   outcomes. A successful ATT write is not proof that a photo was taken.

This research cannot provide 100% hardware certainty. It makes the unknowns
explicit so simulation results do not promise more than their evidence.
