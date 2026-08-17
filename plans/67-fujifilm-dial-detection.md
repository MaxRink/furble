# 67: Fujifilm drive dial detection

## Motivation

Detecting the drive dial position (single, continuous, bracketing, movie)
would let furble adapt shutter behavior per mode. Before designing anything,
capture what the camera actually exposes over BLE.

## Experiment, 2026-08-17

A throwaway patch on the combined image (scratch branch, not pushed) dumped
the full GATT table of the X100VI at connect: every service, characteristic,
property set and the value of every readable characteristic, then subscribed
to every notify or indicate capable characteristic through the hexdumping
notify handler.

Results:

- 15 services. Standard 0x1800/0x1801/0x180a plus 12 vendor services.
- Device name `X100VI`, manufacturer `FUJIFILM`, model `FF230003`, camera
  serial and three firmware version strings (31.30, 01.31, 01.80) are plain
  readable GATT device-information characteristics.
- A cluster of RWN characteristics (handles 0x4503 to 0x4516, service
  `4e941240-d01d-46b9-a5ea-67636806830b` area) carries small state values
  (0100, 0a00, 00) that look like live camera state and are the prime
  candidates for dial state.
- The full dump takes about 40 s per connect because reads run at the idle
  connection interval.
- Stability: the camera dropped the link during some dump runs and one run
  ended in a device reboot with the panic text lost over USB-Serial JTAG.
  A production probe must read fewer characteristics per connect.

## Open

- Map notifications to dial positions. Needs a session with the camera awake
  while turning the dial, notifications now arrive hexdumped on the console.
- Identify which of the 0x45xx characteristics is the drive mode.

## References

- Raw capture: session scratchpad `dial-crash.log`, `dial-run2.log`.
