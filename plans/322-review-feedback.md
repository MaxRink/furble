# 322 - Review feedback for hardware abstraction

## Scope

This is the review-feedback record for plan 322
([322-hardware-abstraction.md](322-hardware-abstraction.md)), branches
`refactor/hal-1-drivers` and `refactor/hal-2-interfaces`. Every finding is
recorded as fixed, already fixed or a false positive, with evidence.

The review confirmed 27 register, bit and command values against the
datasheets, every `Board::Profile` row against the removed board switches,
and unchanged behavior on the existing boards. The findings below are what it
asked to change.

| # | Source | Finding | Resolution | Evidence |
| ---: | :--- | :--- | :--- | :--- |
| 1 | Subagent review | The m5stick-s3 image was 1,296 bytes smaller with no explanation; suspected 64 KB DROM/IROM padding. | Explained, no code change. Fresh base and hal-2 builds give -32 bytes. `image_info` shows the IROM segment at file offset 0x60018 in every image, so padding did not change. The difference is the IROM segment length, spread over 49 mostly non-furble functions by 4 to 60 bytes, which points to layout-dependent Xtensa call relaxation and alignment. DRAM and IRAM totals are unchanged. | plan 322 "StickS3 -1,296 bytes"; `image_info` of both images |
| 2 | Subagent review | The non-virtual power contract had no compile-time check. | Fixed. `meetsPowerBackend<P>()` compares `decltype(&P::member)` with the documented signature for every member and rejects a polymorphic type; `FurblePlatform.h` asserts it after the class. `board-profile` pins the trait with one passing and two failing types. | `include/FurblePowerBackend.h`, `include/FurblePlatform.h`, `tests/host/board_profile_test.cpp` |
| 3 | Subagent review | `AXP2101(RegisterBus &, uint16_t protectedRails = 0)` let a board forget its protected rails. | Fixed. No default; every board states its mask. | `lib/hal/AXP2101.h` |
| 4a | Subagent review | The SX1262 wait bound reused the driver's own constants. | Fixed. Bounded by the literal `10'200` us from section 8.1 and Table 8-2. | `tests/host/hal_drivers_test.cpp` |
| 4b | Subagent review | The AXP2101 rail table skipped DCDC2, DCDC4, ALDO2 and ALDO3. | Fixed. All four added with their REG 80 and REG 90 bits. | `tests/host/hal_drivers_test.cpp` |
| 5 | Subagent review | `Rail::DISPLAY`, `Rail::PERIPHERAL` and `getBatterySource()` had no caller. | Fixed. Removed, including from the contract check, which now covers nine members. They return with their first caller. This deviates from the brief, which listed display and peripheral rails and a battery source. | `include/FurblePowerBackend.h`, `src/FurblePlatform.cpp`, `sim/FurblePlatformSim.cpp` |
| 6 | Subagent review | The VRTC note cited section 6.5.4.5 (Reset); the off-state rule is in 6.5.4. | Fixed. Cites 6.5.4: in the off state every output except RTCLDO is turned off. | `lib/hal/AXP2101.h` |

Validation after these changes: `hal-drivers` and `board-profile` pass, as do
the host targets that include the real `FurblePlatform.h` (settings, boundary,
headless service, companion GATT). The m5stick-s3 firmware builds with the
assertion; size and stack budgets pass at 1,848,928 bytes. The StickS3
simulator rebuilt with the assertion and passes the 22 hardware-profile
scenarios. `python3 tools/pre_review.py --base origin/master`: 9 of 9 gates.
`sh sim/scripts/check-doc-tokens.sh`: tokens present. The rebase onto
`refactor/port-4-camera-types` is pending that branch's final review fixes.
