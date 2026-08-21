# include/

Public headers for the app layer in src/, one header per module
(`Furble<Module>.h`). Everything lives in the `Furble` namespace.

- Add a header here only for src/ modules. lib/furble keeps its headers next
  to its sources.
- These headers may include lib/furble headers; the reverse is forbidden
  (see lib/furble/CLAUDE.md).
- Doxygen-style comments on public members, matching the existing files.
- `FurbleControl::setPower` is the user-selected maximum for Bluetooth transmit
  power. Adaptive runtime changes must stay at or below that cap.
- `FurbleGPS` exposes the CASIC binary test path and AID-INI injection as
  documented public methods. Keep their wire format comments next to the API.
