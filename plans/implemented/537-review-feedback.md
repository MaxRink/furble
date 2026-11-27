# 537 - Review feedback

An independent, adversarial review of the WebSerial provisioning step: the
`web-installer/provision.js` encoder, WebSerial session and response parser, the
provisioning panel in `web-installer/index.html`, the `MAX_COMMAND` line guard in
`src/FurbleConsole.cpp`, `src/FurbleProvision.cpp`, `src/FurbleSettings.cpp`, and
the new Node and host tests.

Status: every finding is fixed on `feat/114b-flasher-provisioning`, in the
review-fix commits that add this file. #542 landed first and this branch was
rebased onto it, which is the order the review required.

Reviewed at head `9f68b283`, base `79df13a7`. The review treated the firmware as
the authority and checked the browser against it rather than against the PR
description: the wire ids and types were read out of the settings table and
`runtimeType()`, the TX power labels were traced through
`load<esp_power_level_t>()` to the dBm the radio actually gets, and the NVS
question was answered from the storage model. Every encoder limit was then
exercised with realistic and maximal field values in Node. No blocking defect
was found in this PR's own diff; the blocking item is a cross-PR interaction
with #542.

| # | Severity | Finding | Resolution | Evidence |
| ---: | :--- | :--- | :--- | :--- |
| 1 | Blocking (cross-PR) | Merged with #542 in either order, `tests.test_web_installer` fails. #542's `write_site()` fixture builds a deploy tree of manifests and firmware assets only, and #537's new `page_script_errors()` now requires `index.html` plus every local script it loads. Each PR is green alone, so nothing catches this until the second one merges. | Fixed after #542 landed. This branch is rebased onto the merge and all three textual conflicts keep both sides. The stale side was #542's fixture: `write_site()` now writes `index.html` and every local `<script src="./x.js">` target, so `page_script_errors()` resolves the page it checks (`tests/test_web_installer.py:80-90`, `tools/verify_web_installer.py:122-135`). Evidence: `Ran 36 tests ... OK`, `validated 11 manifests and 44 firmware assets`. | Both scratch merges of `origin/master + pr-542 + pr-537` and `origin/master + pr-537 + pr-542`: same three textual conflicts, then `python3 -m unittest tests.test_web_installer` -> `FAIL: test_pages_validation_accepts_the_generated_site`, `SystemExit: web installer validation failed: index.html is missing from the site`, `Ran 60 tests ... FAILED (errors=1)` (order A) and `Ran 31 tests ... FAILED (errors=1)` (order B). Alone: #542 `Ran 18 tests OK`, #537 `Ran 19 tests OK`. With the fixture fix applied to the order-A tree: `Ran 31 tests ... OK`, `validated 11 manifests and 44 firmware assets`. |
| 2 | Non-blocking | Neither the page nor the firmware enforces the WPA2 minimum passphrase length, so an 8-byte-short PSK is accepted, stored and reported as applied on a device that can never associate. `web-installer/provision.js:170` checks `0 .. LIMITS.pskBytes` (63) and `src/FurbleNetworkSettings.cpp:13-14` checks only `value.size() <= WIFI_PSK_MAX_LENGTH`. That is the half-provisioned outcome the design says it avoids, and it reports success while doing it. | Fixed on both sides. `Settings::validNetworkString()` accepts an empty value (open network) or `WIFI_PSK_MIN_LENGTH` 8 to `WIFI_PSK_MAX_LENGTH` 63, the WPA2-PSK and WPA3-SAE range. `web-installer/provision.js` refuses 1 to 7 bytes before the port opens, the console message names the range, and `ProvisioningContractTest` pins the browser bounds to `include/FurbleSettings.h`. Tests: `settings_nvs_roundtrip_test.cpp` `testNetworkIngressValidation` (empty, 7, 8, 63, 64), `provision_apply_test.cpp` `testWifiCredentialValidation` (open, an 8-byte SAE passphrase, a 63-byte WPA2 passphrase, and 1, 7 and 64 rejected with nothing written), the console receive path, and the Node test `passphrase length follows the WPA2 and WPA3-SAE range`. | `checkLength("Passphrase", psk, 0, LIMITS.pskBytes)` at `provision.js:170`; `validNetworkString` at `src/FurbleNetworkSettings.cpp:6-20` has no minimum for `WIFI_PSK`. `src/FurbleProvision.cpp:432-439` therefore passes a 3-byte passphrase to `Settings::save`. |
| 3 | Non-blocking | The success summary counts fields whose write failure cannot be observed. The SSID and the passphrase are written last through `Settings::save`, which is `void`, and `report.fieldsApplied` is incremented regardless, so an NVS failure on either still reports `N field(s) applied` and the browser shows success. The companion password and the settings records do check, via `saveSetting()` and `savePassword()`. | Fixed. The SSID and the passphrase go through `saveNetworkString()`, which reads the value back because `Settings::save()` is `void`. A mismatch logs at error level on device (the host build skips the `ESP_LOGE`) and returns `STORAGE_FAILURE` with the field's wire id and `wifi_ssid`/`wifi_psk persistence failed`, so the command prints an `error:` line, then `provision: <n> field(s) applied, <m> field(s) deferred, 1 field(s) failed`, returns 1, and never announces the field as applied. Tests: `testWifiCredentialStorageFailure` injects `nvs_test_fail_set_on(1)` for each field and asserts the error, the wire id, the message, `fieldsApplied == 0` and that the previous value survives. | `src/FurbleProvision.cpp:525-537` (`Settings::save<std::string>` then `report.fieldsApplied++`), against `src/FurbleProvision.cpp:493-498` and `:510-517` which do test a return value; `Settings::save` is `void` at `src/FurbleSettings.cpp:501-513`. |
| 4 | Non-blocking | The theme is not validated on the wire path even though a validator exists. `validTheme()` allows exactly `Dark`, `Default`, `Mono Furble` and is called only by the SD importer, so an arbitrary string up to 63 bytes is stored through provisioning and through `settings set theme`. The page offers the right three values, so the shipped path is correct; the contract test pins the wire id and type only, not the accepted values. | Fixed. `validTheme()` moved to `Settings::validTheme()` and is called from `validateSetting()`'s STRING branch, from `settings set theme`, and so from the WebUI settings write that routes through the same validator. The closed set now has one definition and no wider write path remains. The page's three option values and the encoder's `THEMES` list are pinned to it. Tests: `provision_apply_test.cpp` case `{3, STRING, "dark"}`, `console_commands_test.cpp` `settings set theme dark`, the Node test `the theme is the closed set the firmware accepts`, and `test_theme_set_matches_the_firmware_and_the_page`. | `src/FurbleSD.cpp:154-155` defines it, `:563` is the only caller. `src/FurbleProvision.cpp:292-321` has no theme check. Page options at `web-installer/index.html:153-158` equal `src/FurbleUI.cpp:12050` `{"Dark", "Default", "Mono Furble"}`. Contract test at `tests/test_web_installer.py:217-225`. |
| 5 | Non-blocking | TX power accepts the full 0..255 byte with no bound, and every value outside 0/1/2 silently becomes P3. The page's own select cannot produce such a value, so this is consistent with the console's `settings set` and not a regression; it is only worth a bound if the wire path is meant to be tighter than the console. | Fixed. `validateSetting()` refuses a value above `Settings::TX_POWER_MAX` (2) with `TX power must be 0 (P3), 1 (P6) or 2 (P9)`, and the page encoder refuses the same before it opens the port, so nothing is stored and silently clamped. The console's `settings set tx_power` keeps its 0 to 255 parsing, so the wire path is deliberately the tighter one. Tests: `provision_apply_test.cpp` cases for 3 and 9, the console receive path, the Node test `TX power is one of the three radio levels`, and `test_tx_power_bound_matches_the_settings_header`. | `provision.js:212-217` allows `0..255`; `src/FurbleProvision.cpp:188-291` has no `TX_POWER` case; `src/FurbleSettings.cpp:455-473` maps anything else to `ESP_PWR_LVL_P3`. `src/FurbleConsole.cpp:575-601` likewise only rejects `> UINT8_MAX`. |
| 6 | Note | The write path is unauthenticated by design: the console is the transport, it is USB Serial/JTAG only, and it exists only in `FURBLE_CONSOLE` images. Anyone with physical USB access to a debug image can therefore set the WiFi PSK, the MQTT password, and the companion password, which is an authentication gate for the companion service. That is a defensible model for a bench image, but the PR text states it as "requires the debug build" rather than as a threat model. | Fixed. `docs/console-commands.md` and `docs/wiki/Console-Commands.md` now state that physical USB access to a debug image is equivalent to control of every stored credential it can write, including the companion password that gates the companion service. | `src/FurbleConsole.cpp:3718-3744` installs the USB Serial/JTAG driver; `FURBLE_CONSOLE=1` appears only in the `-debug` environments and `esp32-s3-headless` in `platformio.ini`; `docs/settings-and-controls.md:130-132` describes the companion password as a gate. |

Verified, no change needed:

| Check | Result | Evidence |
| :--- | :--- | :--- |
| The settings written match the settings table's wire ids | Holds. The page writes `THEME` as id 3 and `TX_POWER` as id 4, which is what the table declares. | `web-installer/provision.js:48` `WIRE_ID = { THEME: 3, TX_POWER: 4 }`; `src/FurbleSettings.cpp:34` `{THEME, 3, "Theme", ...}`, `:36` `{TX_POWER, 4, "TX Power", ...}`. Pinned by `tests/test_web_installer.py:217-225`. |
| The types match what the firmware demands | Holds, and the firmware checks it twice, so a wrong type byte cannot slip through. | `provision.js:387` sends `TYPE.STRING` for the theme and `:394` `TYPE.U8` for TX power; `runtimeType()` returns `U8` for `TX_POWER` at `src/FurbleProvision.cpp:96` and `STRING` for `THEME` at `:130`; `validateSetting` rejects any mismatch against both the schema and the runtime type at `:164-171`. |
| The TX power labels the page shows are the levels the radio gets | Holds. This is the one place the review expected a defect and did not find one. | `index.html:162-166` labels 0/1/2 as `Low (P3)`, `Medium (P6)`, `High (P9)`; `src/FurbleSettings.cpp:455-473` `load<esp_power_level_t>` maps `0 -> ESP_PWR_LVL_P3`, `1 -> ESP_PWR_LVL_P6`, `2 -> ESP_PWR_LVL_P9`, default `P3`. `src/FurbleControl.cpp:1665-1673` converts that to dBm and applies it. |
| It cannot corrupt an existing settings blob | Holds, because there is no blob. Every setting is its own `Preferences` key in its own namespace, so writing `wifi_ssid`, `wifi_psk`, `companion_pw`, `theme` and `tx_power` cannot disturb any other key or the OTA data record. The two blob-shaped settings (`TOUCH_CALIBRATION`, `INTERVAL`) are unreachable from this page, which sends only ids 3 and 4 plus the four dedicated tags. | `src/FurbleSettings.cpp:342-343`, `:372-383` show per-key `prefs.putString`/`prefs.put`; `src/FurbleProvision.cpp:322-338` only accepts a blob for `INTERVAL`; `provision.js:163-223` never emits `TYPE.BLOB`. |
| A bad record cannot half-apply an earlier one | Holds, and this is the strongest part of the design: the whole batch is validated before the first NVS write. | `src/FurbleProvision.cpp:466-480` preflights every record through `getByWireId` and `validateSetting`, then `:482-508` writes. The comment at `:466-468` states the intent. |
| The console refuses an over-long line instead of clipping it | Holds, and the browser refuses the same length before opening the port. | `src/FurbleConsole.cpp:79-83` `MAX_COMMAND = MAX_LINE - 1`, `:3784-3810` sets `overflow` and prints `error: line longer than %u bytes, command not run`; `provision.js:54` `MAX_COMMAND_CHARS = 255` and `:254-260` throws before any write. Pinned to `MAX_LINE - 1` by `tests/test_web_installer.py:203-215`. |
| A rejected bundle writes nothing | Holds for the cases the tests drive. | `tests/host/console_commands_test.cpp:2321` `a truncated frame changes nothing`, `:2336` `a repeated frame changes nothing`, plus the over-long SSID and passphrase cases. |
| Secrets never reach the page | Holds. The console echo is dropped by prefix, and the renderer prints only the applied, deferred and error lists, never `unexpected`. There is no `innerHTML` on the result path. | `provision.js:294-301` drops the echoed `provision base64:`/`provision hex:` line; `index.html:355-366` renders `applied`, `deferred`, `errors` only; `:346-353` uses `textContent`. |
| Stored secrets are not readable back over the console | Holds. Passphrase, MQTT password and companion password all print `set`/`unset` only; the SSID is printed in full, and it is not a secret. | `src/FurbleConsole.cpp:503-505` `WIFI_PSK` set/unset, `:499-501` `MQTT_PASS` set/unset, `:506-511` companion password set/unset, `:993-995` `settings get` refuses the companion password outright. |
| Credentials only go to a device that presents the furble prompt | Holds. The command is written only after the prompt is confirmed, so a wrong port receives carriage returns and nothing else. | `provision.js:548-568`: `openPort`, then `session.waitForPrompt(promptTimeoutMs)` at `:554`, and only then `session.send(plan.command, ...)` at `:568`. |
| The port is always handed back | Holds. | `provision.js:573-608` `finally` cancels the reader, awaits the read loop, releases both locks and closes the port. |
| The parser fails closed | Holds on all the shapes the PR claims. | `provision.js:343-389`: an `error:`/`failed:` line, a missing summary, a missing decoded-byte count, a byte-count mismatch, any deferred field, or zero applied fields each return `ok: false` with a reason. The decoded-byte comparison at `:365-373` is what makes a clipped line detectable. |
| The 178-byte command budget is workable | Holds for realistic input. Maximal SSID+PSK+companion input still fits; only a maximal SSID and PSK combined with MQTT fields would be refused, and the refusal message is honest about it. | Node harness against `buildCommand`: typical MQTT build (SSID 9, PSK 14, companion 8, `mqtt://192.168.1.10`, base `furble`) -> 71 bytes, 113 chars; the same plus MQTT user and password -> 82 bytes, 129 chars; max SSID 32 + PSK 63 + companion 63 -> 167 bytes, 241 chars, under the 255 limit. |
| The board MQTT map matches the compiled profiles | Holds, and it is pinned rather than restated. | `provision.js:63-67`; `tests/test_web_installer.py:227-249` derives the MQTT environments from `platformio.ini` and compares the intersection with the page's radios. |
| The settings contract checker passes | Holds, with seven pre-existing warnings unrelated to this change. | `python3 tools/check_settings_contracts.py` -> `settings contract check ... summary: 0 FAIL, 7 WARN`, `settings contracts: PASS`, 76 table rows, 71 source wire ids, 76 ledger wire ids. The two warnings that touch this PR are the dynamic `FURBLE_PROVISION_FIXTURE` definition at `tests/host/CMakeLists.txt:1101`, which is a known not-evaluated shape. |
| The browser test suite passes | Holds. | `node tests/web-installer/provision.test.cjs` -> `113 checks, 0 failure(s)`, including the fixture bytes, field limits, command limit, response parser, fake-port session and the panel driven through a fake DOM. |
| The deploy copies the new script and the gate checks it | Holds. | `.github/workflows/pages.yml:144` adds `cp web-installer/provision.js site/provision.js`; `tools/verify_web_installer.py:84-97` `page_script_errors` fails the deploy if `index.html` or any `src="./..."` target is missing. `python3 tools/check_ci_workflows.py` -> `CI workflow trigger check passed`. |
| The partition-table gate still passes on this branch | Holds. | `python3 -m unittest tests.test_check_partition_tables` on `pr-537` -> `PASS: 7 boards`. |

## Remediation

The blocking finding was a cross-PR interaction, so it needed #542 first. After
that merge this branch was rebased onto master and the three textual conflicts
(`tools/verify_web_installer.py`, `tests/test_web_installer.py` and
`docs/wiki/Getting-Started.md`) were resolved by keeping both sides, as the
review predicted. The stale side was #542's `write_site()` fixture: it now
writes `index.html` and every local `<script src="./x.js">` target, so
`page_script_errors()` resolves the page it is asked to check.

The two domain gaps and the invisible write are fixed in both halves, and each
one carries a test on the firmware side and on the browser side. The contract
test pins the page constants to the firmware constants rather than restating
them, so none of the three sets can drift again.

Verification on the rebased branch:

```
python3 -m unittest tests.test_web_installer
  Ran 36 tests ... OK
  validated 11 manifests and 44 firmware assets
node tests/web-installer/provision.test.cjs
  140 checks, 0 failure(s)
python3 -m unittest discover -s tests -p 'test_*.py'
  Ran 676 tests ... OK
python3 tools/verify_web_installer.py <generated site>
  validated 11 manifests and 44 firmware assets
ctest --test-dir <host build>
  100% tests passed out of 223
python3 tools/pre_review.py --base origin/master
  PASS: 13 gates
sh sim/scripts/check-doc-tokens.sh
  sim documentation tokens are present
python3 tools/check_doc_citations.py --root .
  0 errors of 3298 citations in 187 files
```

The parts of this change that needed a device are the parts the PR already
declines to claim, and that is the right call: the prompt timing after the
install dialog releases the port, the port reopen, association on the next boot
and the MQTT discovery round trip are all hardware items, and the page's retry
model (`openPort` with bounded attempts, then fail closed) is the correct shape
to test them with. Findings 2 to 5 are small and none of them is a reason to
hold the branch; finding 1 is.
