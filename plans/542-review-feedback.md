# 542 - Review feedback

An independent, adversarial review of the one-chip-family-per-manifest fix: the
new `tools/web_installer_layout.py`, the rewritten `web-installer/manifest.tmpl`
and `web-installer/generate-manifest.py`, the `tools/verify_web_installer.py`
gate, the `web-installer/index.html` fetch-and-refuse path, and
`tests/test_web_installer.py`.

Reviewed at head `83962013`, base `18bbcb5a`. The review re-derived the defect
from unmodified master sources rather than from the PR description, re-resolved
`PLATFORM` to a family for all fifteen `platformio.ini` environments with an
independent parser that follows `extends` and `sdkconfig_path`, and executed the
page's own JavaScript in Node against a fake DOM to test the four claimed
fail-closed paths plus three it does not claim. The defect is real and the fix is
correct; one finding blocks the merge because it turns an existing gate red.

| # | Severity | Finding | Resolution | Evidence |
| ---: | :--- | :--- | :--- | :--- |
| 1 | Blocking | The change breaks `tests.test_check_partition_tables`, so CI "Python tooling tests" is red. `tools/check_partition_tables.py:330-341` reads `web-installer/manifest.tmpl` and requires a literal decimal app offset beside `furble-$PLATFORM-$VERSION.bin`: `r'"path":\s*"[^\"]+/furble-\$PLATFORM-\$VERSION\.bin",\s*"offset":\s*(\d+)'`. The new template writes `$APP_OFFSET` instead of `131072`, the regex matches nothing, `app_offsets` is empty and `require(...)` fails. The check's intent (no-build upload offset equals `ota_0`) is still valid; only its parsing is stale. | Open. Resolve the placeholder instead of a literal: import `APP_OFFSET` from `tools/web_installer_layout.py` and compare that, which preserves the one-definition property the PR is built on. Also note `tools/pre_review.py:84-95` runs a fixed eight-module subset that excludes `tests.test_check_partition_tables`, which is why the claimed "PASS: 13 gates" is consistent with a red CI; the Gates section should quote `python -m unittest discover -s tests -p 'test_*.py'`, which is what `.github/workflows/main.yml:120` runs. | `python3 -m unittest tests.test_check_partition_tables` on `pr-542` -> `FAIL: test_the_repository_tables_pass`, `upload layout: FAIL: web-installer/manifest.tmpl: app offset differs from ota_0`, `Ran 4 tests ... FAILED (failures=1)`. The same command on `origin/master` and on `pr-537` -> `PASS: 7 boards`. CI agrees: job 140237144676 `Python tooling tests` conclusion `failure`, log line `upload layout: FAIL: web-installer/manifest.tmpl: app offset differs from ota_0`, `Ran 648 tests in 14.398s / FAILED (failures=1)`. `gh pr view 542` reports `mergeStateStatus=UNSTABLE`. |
| 2 | Blocking | A radio without `data-chip-family` silently disables the whole check instead of refusing. `web-installer/index.html:154-158` returns early on `if (!family \|\| typeof fetch !== 'function')` and sets `activate.disabled = false`, so a board whose attribute is missing or empty gets Connect enabled without the manifest ever being fetched. Today all six radios carry the attribute and `tests/test_web_installer.py:230-236` enforces it, so nothing is broken on this head; but the page's defence disappears exactly when a new board is added without the attribute, which is the failure mode the attribute exists to prevent. `\|\| ''` also means `data-chip-family=""` takes the same path. | Open. Keep Connect disabled when `family` is empty and only fail open for the missing-`fetch` case, which is the one the comment actually argues for. | The real page script extracted from `index.html` and driven in Node: with a radio carrying no attribute and a manifest declaring `ESP32-S3`, `verifyFamily` returns with `installActivate.disabled === false`, `flashStatus.textContent === ''`, and `fetch` never called. With `data-chip-family=""` the same. The six published radios alone are covered by `tests/test_web_installer.py:230` via `radio_families()` at `:101-110`. |
| 3 | Non-blocking | A stale verification response can re-enable Connect after the current selection failed. `verifyFamily` (`index.html:151-183`) has no request generation, and `updateManifest` (`:202`) fires it on every radio change without cancelling the previous call, so the last response to arrive wins rather than the last request sent. | Open. Capture a counter in `updateManifest`, pass it to `verifyFamily` and drop any response whose counter is not current. Alternatively serialise: disable and await before starting the next check. | Executed against the page JS: request for `m5stick-c` (deliberately slow, success) is followed by a request for `m5stick-s3` that fails with a 404. After the failure `disabled=true`, `status="Refusing to flash: manifest 404."`. When the stale success lands, `disabled` becomes `false` and `status="ESP32 image ready for m5stick-c."` while the selected board is `m5stick-s3`. |
| 4 | Non-blocking | "An environment with no sdkconfig ... exits non-zero" holds only when no known base prefixes the name. `tools/web_installer_layout.py:88` accepts any environment whose name starts with a base plus `-` (`platform == base or platform.startswith(base + "-")`), so a name that has no sdkconfig at all still resolves through a neighbour. | Open. Require the resolved base to be the environment itself or the environment minus a recognised suffix, or cross-check the candidate against the `[env:...]` sections of `platformio.ini`. A cheap version is to fail when `platform != base` and no `sdkconfig.<platform>` exists. | `PLATFORM=m5stack-core-usb-debug-x VERSION=9.9.9 python3 generate-manifest.py` exits 0 and emits a full manifest with `"chipFamily": "ESP32"` from `sdkconfig.m5stack-core`, although `sdkconfig.m5stack-core-usb-debug-x` does not exist. Same for `m5stick-s3-nonsense` -> `sdkconfig.m5stick-s3` and `m5stick-c-typo` -> `sdkconfig.m5stick-c`. `PLATFORM=no-such-board` does fail closed, exit 1, as claimed. |
| 5 | Non-blocking | `tools/check_partition_tables.py` is now the second place that hard-codes the app offset against the template, and after finding 1's fix there will be two independent readers of `manifest.tmpl`. The template's four offsets are no longer visible as literals anywhere, so a future change to the flash map has to be made in the layout table and both checkers. | Note only. Finding 1's fix should make the checker derive from `flash_offsets()` so there is one definition, not two. | `tools/verify_web_installer.py:34-36` already derives `EXPECTED_FLASH_OFFSETS` from `flash_offsets()`; `tools/check_partition_tables.py:330-341` does not. |

Verified, no change needed:

| Check | Result | Evidence |
| :--- | :--- | :--- |
| The claimed defect is real on unmodified master | Holds, and on every platform, not just the two named. | Master generator, `PLATFORM=m5stick-s3`: `builds: 2`, `ESP32 bootloader=0x1000 offsets=['0x1000','0x8000','0xf000','0x20000']` and `ESP32-S3 bootloader=0x0 offsets=['0x0','0x8000','0xf000','0x20000']`, both pointing at `furble-bootloader-m5stick-s3-9.9.9.bin`. `PLATFORM=m5stick-c`: the same two entries over the `m5stick-c` binaries. |
| The fix produces exactly one correct family and offset | Holds. | PR head, same inputs: `m5stick-s3` -> `builds: 1`, `chipFamily=ESP32-S3`, `offsets=['0x0','0x8000','0xf000','0x20000']`; `m5stick-c` -> `builds: 1`, `chipFamily=ESP32`, `offsets=['0x1000','0x8000','0xf000','0x20000']`. |
| `PLATFORM` resolves to the right sdkconfig for every environment | Holds for all fifteen. An independent resolver that parses `platformio.ini`, follows `extends` and reads `board_build.esp-idf.sdkconfig_path` agrees with the tool on the sdkconfig and the family in every row. | `m5stick-c` esp32/ESP32; `m5stick-c-plus` esp32/ESP32; `m5stack-core` esp32/ESP32; `m5stack-core2` esp32/ESP32; `m5stick-s3` esp32s3/ESP32-S3; `esp32-s3-headless` esp32s3/ESP32-S3; `waveshare-s3-eth` esp32s3/ESP32-S3; `m5stick-c-debug` ESP32; `m5stick-c-plus-debug` ESP32; `m5stack-core-debug` ESP32; `m5stack-core-usb-debug` ESP32; `m5stack-core2-debug` ESP32; `m5stick-s3-debug` ESP32-S3; `m5stick-s3-debug-ble` ESP32-S3; `waveshare-s3-eth-debug` ESP32-S3. 15 envs, 0 mismatch. |
| The longest-base prefix rule is not fooled by the longer debug names | Holds. `m5stick-s3-debug-ble` picks `m5stick-s3`, not `m5stick-c`; `m5stack-core-usb-debug` picks `m5stack-core` because the only longer candidate `sdkconfig.m5stack-core-usb-debug` declares no target. `m5stack-core2-debug` picks `m5stack-core2`, not `m5stack-core`, because the guard requires `base + "-"`. | `sdkconfig.*` target census: `sdkconfig.m5stack-core`, `.m5stack-core2`, `.m5stick-c`, `.m5stick-c-plus`, `.m5stick-s3`, `.waveshare-s3-eth`, `.esp32-s3-headless` declare `CONFIG_IDF_TARGET`; `.debug`, `.stack-protector`, `.spiram-memtest`, `.coredump-flash`, `.full-diagnostics`, `.m5stack-core-usb-debug` do not. |
| An environment that merges two sdkconfigs resolves to the base, not the fragment | Holds. Every `-debug` env that merges fragments resolves to its release base, which is where the target lives. | `platformio.ini:115-116`, `:132-133`, `:149-152`, `:173`, `:178-181`, `:199-202`, `:223-224`. Fragment files carry no `CONFIG_IDF_TARGET`, which is the property `web_installer_layout.py:85-86` relies on. |
| An unknown target fails closed | Holds. | `chip_family_for_platform('board', <root with CONFIG_IDF_TARGET="esp32c6">)` raises `ValueError: board: ESP-IDF target 'esp32c6' has no ESP Web Tools chip family.` |
| The gate really did require the defect, and now rejects it | Holds, both directions. On a complete pre-fix deploy tree built with the master generator, the old verifier returns 0; the new verifier rejects all eleven manifests. | `python3 /tmp/revmaster/tools/verify_web_installer.py <pre-fix tree>` -> `validated 11 manifests and 44 firmware assets`, exit 0. `python3 /tmp/rev542/tools/verify_web_installer.py <same tree>` -> exit 1 with `expected exactly one build for one supported chip family ['ESP32', 'ESP32-S3'], got [...]` and `expected chipFamily 'ESP32' for this board, got 'ESP32-S3'` for each of the eleven. The new verifier passes the PR-generated tree: `validated 11 manifests and 44 firmware assets`, exit 0. |
| No published URL or asset file name changed | Holds. | The eleven `manifest_<board>[-debug].json` names are identical between a master-generated tree and a PR-generated tree, and the set of 44 `parts[].path` strings is identical (set difference empty in both directions). `web-installer/manifest.tmpl` keeps the four `furble-bootloader-`, `furble-partitions-`, `furble-ota_data_initial-` and `furble-` names; only the offset values and `chipFamily` moved to substitution. |
| Making the template path absolute changes nothing in CI | Holds. The deploy already ran the generator from the template's own directory. | `.github/workflows/pages.yml:103` sets `working-directory: web-installer`, and `:108` invokes `python generate-manifest.py`; `generate-manifest.py:24` now names the template absolutely, so the step is unaffected and a manual run from the repo root also works. `git diff --stat origin/master pr-542 -- .github/` is empty, matching the "no workflow change" claim. |
| Three of the four claimed fail-closed paths, executed | Holds at settlement, not just synchronously. | Page JS run in Node: unreachable manifest (`fetch` rejects) -> `disabled=true`, `Refusing to flash: network down.`; HTTP 404 -> `disabled=true`, `Refusing to flash: the manifest request returned 404.`; zero builds -> `disabled=true`, `Refusing to flash: this manifest declares no chip family...`; two builds -> `disabled=true`, `...declares ESP32 and ESP32-S3...`; single build with the wrong family -> `disabled=true`, `...declares ESP32-S3, but m5stick-c is ESP32...`. A correct single-family manifest enables Connect: `disabled=false`, `ESP32-S3 image ready for m5stick-s3.` The fourth claimed path, family mismatch, is the one finding 3 attacks. |
| No credential or manifest text is trusted from the DOM | Holds. All status and result text is written with `textContent`, never `innerHTML`. | `index.html:337-353`. |

Which of the four fail-closed paths is untested: all four, at the automated
level. No test executes the page's JavaScript. `tests/test_web_installer.py`
only asserts on the page source: `test_page_labels_every_published_board_with_its_family`
at `:230-236` parses the radios, and `test_page_keeps_the_published_manifest_url_contract`
at `:238-240` is an `assertIn` on the template-literal string, which would still
pass if the line were commented out. `grep -rn 'verifyFamily\|flashStatus\|installActivate\|fetch('`
over `tests/` returns only unrelated `run_fetch` helpers in
`test_check_review_threads.py`. PR 537 adds the repository's only harness that
executes page JavaScript, and it covers the provisioning panel, not
`verifyFamily`: `grep` for those four symbols in
`tests/web-installer/provision.test.cjs` returns nothing. Of the four, the
family-mismatch branch is the one that can only be reached by executing the
function, so it is the one worth a test first.

The defect statement, the template change, the derived offset table and the
verifier rewrite are all correct and well argued, and the independent
reproduction matches the PR's own numbers. Finding 1 is a stale consumer of the
template that nobody re-ran, not a flaw in the design; findings 2 and 3 are two
gaps in an otherwise sound page check.

## Resolution

Findings 1, 2, 3 and 5 and both gaps (the gate list and the untested page paths)
are fixed on this branch, in the follow-up commit on top of `83962013`. Finding 4
is left open with the reason below: its cheap fix would reject every `-debug`
environment, and the correct fix is a separate change to the resolver. The
review's own reproduction is unchanged, so the defect and the fix it verified
still stand.

| # | State | Fix |
| ---: | :--- | :--- |
| 1 | fixed | `tools/check_partition_tables.py` resolves the template offset through `tools/web_installer_layout.py` and takes `APP_FLASH_OFFSET` from it. A literal in the template is rejected by name; the application part must resolve to the table's `APP_OFFSET`; a moved table fails against `platformio.ini` and the board CSVs. |
| 2 | fixed | `verifyFamily` keeps Connect disabled for a radio with a missing or empty `data-chip-family` and refuses before fetching. All six published radios still carry the attribute. |
| 3 | fixed | `updateManifest` increments a request generation and `verifyFamily` drops any response whose generation is not current, in both the success and the failure path. |
| 4 | open, not in this pass | The prefix rule at `tools/web_installer_layout.py:88` is unchanged. The reviewer's cheap version (`platform != base` requires `sdkconfig.<platform>`) rejects every `-debug` environment: `sdkconfig.m5stick-c-debug` and its siblings do not exist at all, only the fragments do. The robust version is to require the name to be an `[env:...]` section of `platformio.ini`, which is a change to the resolver rather than a review fix, so it is left for its own PR. |
| 5 | fixed | There is one reader now: `check_manifest_offsets` in `tools/check_partition_tables.py` is called by `check_upload_offset` and by `tools/check_single_factory_profile.py`, and the constant they compare against comes from the layout table. |
| gap: gate list | fixed | `tools/pre_review.py` discovers `tests/test_check_*.py` instead of listing a subset, so 14 scanner modules run where 8 did. `--unit-only` lets `.github/workflows/pre-review.yml` use that list instead of its own stale copy. |
| gap: page tests | fixed | `tests/web-installer/manifest.test.cjs` runs the page's script in Node against a fake DOM and a fake fetch. `tests/test_web_installer.py` runs it with the tooling suite, so the Python job executes the page. |

### A third stale reader the review did not list

`tools/check_single_factory_profile.py` matched the same decimal app offset and
failed with `FAIL: web-installer/manifest.tmpl: published app offset changed` on
`83962013`. It also required two `ota_data_initial` parts, which was the
two-family template; a one-build template publishes one. Both are fixed: it
calls the same `check_manifest_offsets`/`manifest_parts` helpers and expects the
one part. It is not in any gate (only `CONTRIBUTING.md` names it), which is why
CI did not see it.

### What each proof shows

- The offset check is not tautological. It reads the placeholder the generator
  substitutes, rejects the literal that used to be there, rejects the
  application part naming another constant, and fails when the layout table
  itself moves. All four cases are unit tests in
  `tests/test_check_partition_tables.py` and were also run through the CLI on a
  scratch copy of the tree.
- The gate list can no longer fall behind. The old nine-module list passes on
  `83962013` while the omitted module fails there, which is the reported "PASS:
  13 gates" next to a red job. The discovered list fails on the same tree.
- The page tests fail on the pre-fix page. Reverting `index.html` to `83962013`
  turns the harness red on nine assertions; removing only the request
  generation turns it red on the four stale-response assertions.

### Evidence

`tests.test_check_partition_tables`, previously the red one, on this branch:

```
$ python3 -m unittest tests.test_check_partition_tables
upload layout: PASS app=0x20000 (PlatformIO and web installer)
PASS: 7 boards
Ran 13 tests in 0.054s
OK
```

The check still fails when an offset is wrong. A scratch copy of the tree, with
`tools/`, `platformio.ini`, the CSVs and the template, run through
`python3 tools/check_partition_tables.py`:

```
# the old literal restored in the template, exit 1
upload layout: FAIL: web-installer/manifest.tmpl: name the layout constants instead of the literal offsets 131072

# the application part names another layout constant, exit 1
upload layout: FAIL: web-installer/manifest.tmpl: app offset differs from ota_0: the template resolves $OTA_DATA_OFFSET to 0xf000, the partition tables use 0x20000

# the layout table itself moves (APP_OFFSET = 0x10000), exit 1
upload layout: FAIL: platformio.ini: upload app offset must match ota_0 at 0x20000

# the branch as pushed, exit 0
upload layout: PASS app=0x20000 (PlatformIO and web installer)
PASS: 7 boards
```

The gate gap, on the unmodified PR head at `83962013`:

```
$ python3 -m unittest tests.test_check_pr_hygiene tests.test_check_settings_contracts \
    tests.test_check_cpp_safety tests.test_check_coupling_ratchet \
    tests.test_check_datasheet_citations tests.test_check_format \
    tests.test_check_m5gfx_pinning tests.test_check_doc_citations \
    tests.test_plot_companion_power
Ran 215 tests in 30.233s
OK (skipped=4)

$ python3 -m unittest tests.test_check_partition_tables
FAIL: test_the_repository_tables_pass (tests.test_check_partition_tables.CoredumpTest.test_the_repository_tables_pass)
upload layout: FAIL: web-installer/manifest.tmpl: app offset differs from ota_0
Ran 4 tests in 0.063s
FAILED (failures=1)

$ python3 -m unittest discover -s tests -p 'test_check_*.py'   # the new gate
FAIL: test_the_repository_tables_pass (test_check_partition_tables.CoredumpTest.test_the_repository_tables_pass)
Ran 319 tests in 22.424s
FAILED (failures=1, skipped=4)
```

The page, executed:

```
$ node tests/web-installer/manifest.test.cjs
42 checks, 0 failure(s)

# the same harness against the page at 83962013
42 checks, 9 failure(s)
# a stale success re-enables Connect, a radio with no attribute is not refused

# against this page with only the request generation removed
42 checks, 4 failure(s)
# the two stale-response cases fail in both directions
```

The single-factory profile, which CI never ran:

```
$ python3 tools/check_single_factory_profile.py
PASS: m5stack-core-usb-debug single-factory table and USB upload contract
```

The gate and the suite:

```
$ python3 tools/pre_review.py --base origin/master
PASS: scanner unit tests
PASS: 13 gates

$ python3 -m unittest discover -s tests -p 'test_*.py'   # what main.yml:120 runs
Ran 659 tests in 101.206s
OK (skipped=4)

$ python3 -m unittest tests.test_web_installer -v        # the page harness runs
test_the_page_script_fails_closed (tests.test_web_installer.WebInstallerPageTest.test_the_page_script_fails_closed) ... ok
```

`node` is skipped, not failed, when it is absent, following the harness
`tests/web-installer/provision.test.cjs` pattern. The GitHub runner and the
developer machines have it, so the job executes it.
