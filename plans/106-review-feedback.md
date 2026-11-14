# 106 - Review feedback for the Waveshare alignment

## Scope

This is the review-feedback record for `fix/waveshare-autodetect`: the M5GFX
autodetect pin (plan 42), plan 106 group A on `sdkconfig.waveshare-s3-eth`,
and the `sdkconfig.full-diagnostics` debug fragment. Every finding is
recorded as fixed, already fixed or a false positive, with evidence.

## Independent review

The review found the pin, the group A values and the fragment correct.

| # | Source | Finding | Resolution | Evidence |
| ---: | :--- | :--- | :--- | :--- |
| 1 | Subagent review | With `CONFIG_ESP_ERR_TO_NAME_LOOKUP` off, `esp_err_to_name()` returns the fixed string `UNKNOWN ERROR`, not a hex code, so every release board lost the code from its error logs. Plans 106 and 42 claimed the logs showed hex. | Fixed. All 33 furble call sites print `%s (0x%x)`, or `(%s, 0x%x)` where the name was already in parentheses. Plans 106 and 42 describe the real behavior. The IR boundary test expects the new timeout line; the full plain host suite and the boundary suite pass. Waveshare release flash rises 304 bytes, from 1,517,504 to 1,517,808, within budget. | `esp_err_to_name.c` in IDF 5.5.3; `75ffd217` |
| 2 | Subagent review | The AtomS3R probe listed in plan 42 and the `216db11f` message never runs on this board: it sits under `get_pkg_ver()` case 1 (LGA56), and the ESP32-S3R8 is QFN56, case 0. | Fixed. Plan 42 drops the AtomS3R bullet, says why, and adds the case 0 PaperColor probe, which drives GPIO 11 to 14 only after its I2C detect on GPIO 2 and 3 succeeds. The pinned commit message is not rewritten; plan 42 records the correction. | M5GFX 0.2.27 `M5GFX.cpp` case 0 at line 1692, case 1 at line 2750 |
| 3 | Subagent review | The 1,152-byte debug flash difference between `216db11f` and `baf31f40` was unexplained. | Fixed as a measurement artifact. The generated `sdkconfig.h` is identical, two clean builds of `baf31f40` gave 1,968,288 and 1,968,336 bytes, and a clean `216db11f` build gave 1,969,488. The two same-commit maps load the ESP-IDF component archives in a different order, which shifts `esp_eth.c.o` and changes Xtensa longcall relaxation. Plan 106 records it and drops the number. | `furble.map` from both clean builds |
| 4 | Subagent review | `waveshare-s3-eth-debug` lacked `pre:patches/reproducible.py`, unlike every other debug env. | Fixed. All seven `extra_scripts` lists now run it. The debug build passes its size and stack budgets at 1,969,824 bytes. | `platformio.ini` |
