# UI evidence for merged PRs

Selected-page captures from the M5StickS3 simulator (135x240, default theme):

- `master/main.png`
- `master/settings.png`
- `master/gps-data.png`
- `master/companion-pairing.png`
- `fixed/companion-pairing.png` (compact dialog)

The last ten merged PRs (#345, #348, #349, #350, #351, #352, #354, #356,
#357, #358) change no `src/FurbleUI.cpp`, icon, simulator input, or e2e scenario
files. Their before/after UI capture is therefore identical; this directory
holds the common capture instead of ten duplicate sets.
