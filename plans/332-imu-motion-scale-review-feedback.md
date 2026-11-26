# 332 review feedback (IMU motion scale audit)

Review of PR 504, `vibe/imu-motion-audit`, which merged as `0c585de9`. The
review was asked to decode the written bytes back to mg independently, to check
the two durations against the 50 Hz sample rule, to confirm the MPU-6886 4 mg
per count is labelled an assumption rather than a fact, and to confirm no
firmware register value changed. All four held. The findings are documentation
damage the change left behind, and they are fixed in
`fix/504-review-followup`.

There are five `plans/332-*.md` files on master (this plan, the docs gap
closure, the GNSS vendor audit, the simulator RTC and the vendor document
verification), so this record is named for the plan rather than the number.
`plans/332-review-feedback.md` would be ambiguous.

## Findings

| # | Severity | Finding | Resolution | Evidence |
| --- | --- | --- | --- | --- |
| 1 | NON-BLOCKING | `docs/components/mpu6886.md` cites `src/FurbleIMUMotionMPU6886.cpp` by line, and every citation was correct against the pre-#504 file: the driver's comment block grew by seven lines at the top. `:21` and `:22` for `WHO_AM_I` now point at a blank line and the header comment, `:41` and `:43` for the 4 mg claim point at `POWER_MANAGEMENT_2` and `POLL_INTERVAL_MS`, and `:109` for the `ACCEL_CONFIG` write points at a local variable. | Fixed. Every citation in the page is renumbered to the current file, and each one was checked by printing the cited line. | `git show 0c585de9^:src/FurbleIMUMotionMPU6886.cpp` puts `WHO_AM_I` at `:21`, `WOM_X_THRESHOLD` at `:29` and the `ACCEL_CONFIG` write at `:109`; the current file puts them at `:28`, `:36` and `:121`. |
| 2 | NON-BLOCKING | The same page still stated that the driver comment calls the register map "revision 1.2", that no such revision exists, and that "the citation is wrong, and this change does not edit it". #504 corrected the comment to revision 1.1, so the page asserted a defect that no longer exists. | Fixed. The paragraph now records that the comment cites revision 1.1 and that #504 corrected it, and the "facts still unverified" list no longer counts the revision as open. | Current `src/FurbleIMUMotionMPU6886.cpp:18` to `:19`: `// Register addresses and bit fields are from the InvenSense MPU-6886 datasheet` / `// DS-000193 revision 1.1.` |
| 3 | NON-BLOCKING | `docs/components/bmi270.md` still said "the file header at `:17` to `:21` also still says the any-motion and no-motion layout is not in the datasheet, which row 8 above disproves". #504 is the change that made the header cite section 5.2.40, so the page contradicted the code it was describing, and the four "still open" comment citations had moved. | Fixed. The paragraph now says the header cites section 5.2.40 since #504 and names the four comments that still carry a superseded section number, at their current lines. | Current `src/FurbleIMUMotionBMI270.cpp:18` to `:23` cites section 5.2.40 pages 92 and 93. The four stale numbers are at `:41` (5.2.36), `:47` (5.2.38), `:64` (5.2.44) and `:74` (5.2.45). |
| 4 | NON-BLOCKING | `docs/power/gps-imu.md` and `docs/power/soc.md` cite the same two drivers by range (`:17-181`, `:183-325`, `:56-70`, and so on) and both files grew, so those ranges are now off by three and seven lines. | Recorded, not changed. The ranges are still inside the files and still cover the arm and poll paths they name, so no claim is false; a range rewrite would collide with the open citation gate in PR 511, which scans this area. | `wc -l` on the two files grew by 3 and by 7 lines in `0c585de9`. |

## What was attacked and found sound

- **The bytes decode to the published sensitivity.** Independently of the header,
  `0xAA` is 170 counts and the field spans 0 to 1 g over 2048 counts, so
  `170 x 1000/2048 = 83.0078 mg`, which is the 83 mg the document states.
  `0x090` is 144 counts, `70.3125 mg`. The two engines are 12.7 mg apart, so a
  model that gives them one threshold is wrong by more than the old 1.4 mg
  arithmetic error.
- **The durations obey the 50 Hz sample rule.** One count is 20 ms, so the
  any-motion duration of 1 count is 20 ms, and the no-motion duration of 3000
  counts is 60000 ms, which is exactly the shared `STATIONARY_HOLD_MS` the other
  two backends use. `static_assert(BMI270_NO_MOTION_DURATION_MS ==
  STATIONARY_HOLD_MS)` makes the equality a compile error, not a comment. The
  documented default of 5 counts is 100 ms, which the scale test also pins.
- **The 4 mg per count is labelled an assumption, not a fact.** The one
  definition is `include/FurbleIMUMotionScale.h` as
  `MPU6886_WOM_MG_PER_COUNT_ASSUMED`, with the tag in the name; the conversion is
  `mpu6886WomThresholdMgAssumed()`; the backend comment at
  `src/FurbleIMUMotionMPU6886.cpp:48` to `:54` opens with `ASSUMPTION:`; the
  calculator, the simulator config and the encoding test all use the `Assumed`
  name. `grep -rn "4 mg\|64 mg"` over the tree finds no remaining statement of it
  as a datasheet value.
- **No firmware register value changed.** The strongest check is not the diff:
  the post-#504 encoding test, which reads the recorded feature words off the
  fake bus and asserts `0xAA`, `0x090`, `3000`, `1` and `0x10` as literals, was
  linked against the pre-#504 driver objects and passes `114 checks,
  0 failures`. The armed bytes are identical before and after, so every device
  wakes at the same acceleration.
- **The double's defect is real and is fixed.** Driving the new model with the
  old configuration reproduces the reported false pass at 75 mg of slope for
  130 s (`STATIONARY`), and with the shipped configuration it stays `MOVING`.
  60 mg still reports stationary and a 100 mg step still wakes, so the change is
  stricter in both directions without moving either boundary the firmware uses.
