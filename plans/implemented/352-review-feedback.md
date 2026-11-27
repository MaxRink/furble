# 352 - Review feedback for the M5GFX board pinning

## Scope

This is the retrospective, adversarial review of PR 499 (`e7cbea4d`,
`fix/m5gfx-board-pin`), which merged on green CI plus author verification
without an independent review. Re-read at `c644f39e`.

Verdict: no production defect found. The pinning gate goes red when the
no-probe patch is reverted, the two patch files are needed for the in-place
libdeps upgrade, and `FURBLE_HAS_INTERNAL_I2C` cannot disable an RTC or IMU that
a board has. Three gate blind spots and one wording problem are recorded as
nits; none changes the shipped image.

## What was attacked

The real M5GFX 0.2.27 source from `.pio/libdeps/m5stick-s3/M5GFX` was
reverse-patched back to the parent revision, then used as a fresh libdeps tree
for each scenario below. The gate was
`python3 tools/check_m5gfx_pinning.py --root <tree> --env waveshare-s3-eth
--require-source`.

| # | Attack | Result |
| ---: | :--- | :--- |
| 1 | Apply only `patches/m5gfx_board.patch` (the production fix reverted) | Gate exit 1: `board_M5StampS3 matches no arm in the esp32s3 branch ... so it falls through to board_unknown and every probe runs, five times over the retry loop` |
| 2 | Apply both patch files to a fresh tree | Gate exit 0 |
| 3 | Use a tree an older revision already patched (first marker present, no no-probe guard), then run the current post script | First patch `present`, second `applied`, gate exit 0: the in-place upgrade works |
| 4 | Combine both hunks into one patch carrying the old marker, apply it to the old tree | `apply_patch` returns `present`, gate exit 1: the defect the split avoids is real |
| 5 | Combine both hunks into one patch carrying a new marker, apply it to the old tree | `apply_patch` raises `1 out of 2 hunks failed`; a fresh marker fails loudly instead of silently keeping the defect |
| 6 | Keep the case labels, replace `goto init_clear;` with `break;` | Gate exit 0: the board is still named, so the no-probe rule passes although the probes would run |
| 7 | Wrap the no-probe guard in `#if 0` while leaving the case labels | Gate exit 0 |
| 8 | Add `case board_t::board_M5StickS3:` to the no-probe list | Gate exit 0: a panel-bearing pin would lose its display and no gate notices |

## Finding 1: the two-file mechanism is justified, but the wording is one step too strong

Status: nit.

Attack 4 confirms the exact reason the plan gives for the split: a combined
patch with the old marker is reported `present` and leaves the defect. Attack 5
shows the other half: a combined patch with a new marker is rejected and fails
the build, so the split is what makes the in-place upgrade work without deleting
`.pio/libdeps`. The plan says a combined patch "would have left a libdeps tree
patched by an older revision at 'present', silently keeping the defect". That is
true only while the marker stays the same; a fresh marker fails loudly. The
mechanism is sound, the sentence should say "the split is what lets an old tree
upgrade in place" rather than implying there is no other design.

The second patch is also applied to `m5stick-c`, `m5stack-core` and
`m5stick-s3`, where its `#elif defined (CONFIG_IDF_TARGET_ESP32S3)` body is
outside the compiled branch or constant-folds to `default: break;`. The plan
records the byte-identical StickS3 result; the ESP32 images do not compile the
hunk. No problem.

## Finding 2: `FURBLE_HAS_INTERNAL_I2C` cannot disable a real RTC or IMU

Status: no defect.

- The macro is 0 only under `FURBLE_WAVESHARE_S3_ETH`
  (`include/FurbleBoardCaps.h:60-67`). `esp32-s3-headless` does not define that
  macro and keeps 1; the Waveshare release and debug envs inherit it from the
  release flags.
- The Waveshare ESP32-S3-ETH vendor resource list has the ESP32-S3R8, 16 MB
  flash, W5500, transformer, regulator, USB-C, Ethernet, PoE header, camera
  interface, antenna, TF slot, buttons and LEDs. It lists no RTC and no IMU.
  `docs/power/board-power.md:119` also records "no display or IMU".
- M5Unified's `_pin_table_i2c_ex_in` row for `board_M5StampS3` is 255/255 for the
  internal bus, so `In_I2C` gets the sentinel port. `M5Unified::_begin` guards
  `Rtc.begin` and `Imu.begin` on `cfg.internal_rtc`/`cfg.internal_imu`, both set
  false before `M5.begin(cfg)` in `Platform::getInstance`.
- The boards with an RTC or IMU keep `FURBLE_HAS_INTERNAL_I2C 1`: the profile
  table in `src/FurbleBoard.cpp` has the BM8563 on the StickC family and
  Core2/Tough, and the MPU6886/BMI270 rows on the StickC family and StickS3.
  The Waveshare maps to the UNKNOWN row, whose `TimedWake` is `NONE` and whose
  battery/RTC fields are false, so disabling its RTC and IMU removes no
  capability the profile claims.
- `FURBLE_HAS_INTERNAL_I2C 0` is compile-only for the Waveshare; the plan says
  no device confirms the drivers stay down. That is correctly listed as
  unverified.

## Finding 3: the gate has three blind spots, all outside the shipped patch

Status: nits.

Attacks 6 and 7 show that the source check keys on a `case` or `board ==`
mention, not on a return, and does not evaluate the enclosing `#if`. Attack 8
shows that `NO_PROBE_BOARDS` in the checker is a separate hardcoded tuple, so a
new board in the patch's case list is named and passes without the checker
knowing it is display-less. `patches/test_m5gfx_board.py` does assert that the
checked-in patch contains `goto init_clear;` and the three StampS3 cases, so the
current patch is covered by the patch test; the gate alone is not. A future
patch edit that keeps the case labels and drops the return would pass the gate
and the patch test would only catch it if the assertion list stayed in step.

No fix here: the production revert (attack 1) is the failure the gate exists
for, and it goes red. The blind spots are recorded so the next change to the
patch or the checker knows to tighten both together.

## Finding 4: docs and plans

Status: nit.

`plans/implemented/42-waveshare-eth-node.md` appends a "Superseded" note and a
follow-up. The note says the claim that the pin "folds every probe away and
`autodetect()` returns `board_unknown` at once on each retry" is superseded.
The folding part was true: M5GFX's own probe code did drop out; what was false
was the consequence, because returning `board_unknown` made M5Unified probe.
`docs/supported-hardware.md:23` states the corrected consequence ("no M5 probe
touches the W5500 pins"), and the follow-up at the end of 42 records the
correct story. The "Superseded" paragraph should distinguish the two clauses.

No board row contradicts another. The Waveshare still maps to the UNKNOWN
profile row, which already says no RTC, IMU, battery, GPS rail or timed wake.

## Checks run

- `python3 -m unittest tests.test_check_m5gfx_pinning`: 16 tests pass.
- `python3 patches/test_m5gfx_board.py`: 5 tests pass.
- `python3 tools/check_m5gfx_pinning.py --root .`: pass.
- The revert and combined-patch attacks in the table above, against the real
  M5GFX source.

No code change is made by this review.
