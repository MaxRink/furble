# Camera protocol research

Research snapshot: 2026-09-12. This library preserves GitHub implementations,
official manuals, source conflicts, and future capture targets for every real
camera family registered in Furble at
`965f299f0f43c38fe45e4680f1bfb735023533af`. It changes no supported-camera claim
and contains no imported implementation or hardware fixture.

| Research notes | Scope | Important boundary |
| --- | --- | --- |
| [Ricoh GR](ricoh.md) | GR III/IIIx BLE, GR II Wi-Fi, GR IV/HDF, GPS and WLAN conflicts | GR III research is not GR IV evidence; broad PENTAX matching is not validated support |
| [Fujifilm, Canon, Nikon](fuji-canon-nikon.md) | Basic/Secure firmware profiles, Smart/Remote roles, Nikon Classic continuation | Keep model, firmware, and application/accessory roles separate |
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

1. Resolve Ricoh location-enable UUID and datum-versus-centisecond conflicts;
   capture GR III/IIIx dynamic pairing and Wi-Fi-wake behavior separately.
2. Capture exact Fujifilm firmware/GATT identities, registration, RPA/bonds,
   Canon role transitions, and Nikon BLE-to-Classic continuation.
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
