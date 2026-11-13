# 303 - upstream sync audit and full SHA dependency pins

Status: merged on master with #385.

## Scope and method

Read-only audit of `gkoh/furble` against fork master `d182d911` on
2026-11-05. The upstream remote was added as a fetch-only remote. No issue,
comment, pull request, or push was sent upstream. The upstream issue and pull
request lists were read with `gh`. The upstream refs were fetched with
`git fetch upstream`, including the unmerged issue branches and the Lumix pull
request ref.

The fork merge base is `2b79ce8d`, which is upstream `v3.9.1`. Upstream
`master` is `0cac22aa`, one commit past `v3.9.1`.

## Upstream delta

| Upstream commit | Subject | Fork state |
| --- | --- | --- |
| `0cac22aa` | Enable watchdog on M5StickS3 (PR 310) | Superseded. The fork already arms the M5PM1 watchdog at boot with a 45 s window and a 1 s feed (`include/FurbleWatchdog.h`, `src/main.cpp:478`, `src/FurblePlatform.cpp:238`), and the host tests `pmic_recovery_test` and `sim_watchdog_test` cover it. A cherry-pick would replace the fork's feed accounting and the 45 s OTA-aware window with the upstream 255 s timeout and unconditional feed. Not carried. |
| `b4347811` | Revert some minor changes to FujifilmBasic (unmerged branch for issue 221) | Unmerged upstream experiment. It re-enables a configuration wait and moves connection parameter updates after connect. The fork has its own connection parameter and liveness work, and there is no GFX100 hardware to validate the experiment. Not carried. |
| `53c148af` | Hack up a serial test mode (unmerged branch for issue 235) | Obsolete. The fork has a headless S3 profile, a USB console, and a simulator. Not carried. |
| `9f8cbf8f` | Button configuration branch for issue 140 | Contained in upstream master and in the fork. The fork implements the feature in plan 62. No action. |
| `fdf58098` | Add support for Panasonic LUMIX cameras (PR 282) | Already ported to the fork in `604993b5` (plan 72). No newer upstream commits. No action. |
| PR 313 | Experimental Xteink X3 port | Open upstream PR for different hardware. It is not part of the fork board matrix and needs Xteink hardware. Not carried. |

The only upstream commit missing from the fork is `0cac22aa`, and the fork's
watchdog is a superset of it. No bulk merge is needed or wanted.

## Open upstream issue mapping

The following is every open `gkoh/furble` issue on 2026-11-05.

| Upstream | Title | State in fork | Evidence |
| --- | --- | --- | --- |
| 140 | Add possibility of changing button config on settings | fixed | `Settings::BUTTON_MODE` in `include/FurbleSettings.h:67`, the settings roller in `src/FurbleUI.cpp:10214`, the host dispatch test in `tests/host/sim_action_parser_test.cpp`, the simulator scenario `sim/scenarios/e2e/button-mode-dispatch.txt`, and plan 62. |
| 159 | Fujifilm scan sometimes not reliable if initiated before camera pairing | missing | `CameraList::match` still rejects a second advertisement from the same address (`lib/furble/CameraList.cpp:465`). A camera that re-advertises with new pairing data after an early advertisement is still dropped. Plan 108 records this as actionable and it needs a Fujifilm body to reproduce. |
| 196 | Additional trigger mechanisms | missing | IR shutter and IMU tap, shake, and double-tap triggers exist (`src/FurbleIR.cpp`, `src/FurbleUI.cpp:12771`). Sound and GPIO triggers remain deferred in plans 91 and 17. The issue is an umbrella, so it is not closed by the partial coverage. |
| 201 | Any chance of getting the M5Tough made compatible | needs hardware | The M5Tough shares the Core2 image and `Platform` branches for `board_M5Tough` (`src/FurblePlatform.cpp:120`). `README.md:164` and `docs/supported-hardware.md:20` declare it untested. No M5Tough is available. |
| 221 | Frequent disconnects during shutter operation with GFX100 | needs hardware | The fork carries the reconnect, liveness, and FujifilmBasic work mapped in plans 65 and 108. The upstream fix branch is unmerged and unevidenced. No GFX100 is available to confirm the regression is gone. |
| 244 | Future enhancement to Timer operation for ASTRO/BULB | fixed | Bulb timer in `include/FurbleSettings.h:55`, 1/3-stop exposure presets in `src/FurbleUI.cpp:2804`, plans 35 and 67, and the simulator scenarios `sim/scenarios/e2e/bulb-timer.txt` and `sim/scenarios/e2e/exposure-preset.txt`. |
| 247 | Support Panasonic Lumix | fixed | `lib/furble/Lumix.cpp`, the vendor dispatch in `lib/furble/CameraList.cpp:493`, host peer coverage in `tests/host/peer_contract_test.cpp`, the simulator scenarios `sim/scenarios/e2e/peer-lumix-write.txt` and `sim/scenarios/e2e/lumix-capability-gating.txt`, and plan 72. The implementation is host-tested only. No Lumix hardware is available. The README and `docs/supported-hardware.md` vendor lists still omit Lumix, which is a documentation follow-up, not a code gap. |
| 248 | WIFI feature | fixed | WiFi, NTP, MQTT, and the authenticated HTTPS WebUI are present in the 8 MB and 16 MB profiles (`platformio.ini:48`, `src/FurbleMQTT.cpp`, `src/FurbleWebUI.cpp`). See `docs/webui.md` and `docs/settings-and-controls.md:309`. |
| 249 | Board-Only support | fixed | The developer-only `esp32-s3-headless` profile builds `FURBLE_NO_DISPLAY` with the USB console and MQTT/WebUI (`platformio.ini:58`). Host coverage is `tests/host/headless_loop_policy_test.cpp`, `tests/host/headless_ui_contract_test.cpp`, and `tests/test_headless_source_contract.py`. |
| 257 | Support for Nikon Z50II | missing | The README feature table marks Nikon Smart pairing as unsupported (`README.md:138`). Plans 60 and 65 record the Blowfish handshake and the Bluetooth Classic bond that the LE-only stack cannot complete. No code path can pair a Z50II in smart mode. |
| 306 | M5StickS3 crashing with Fuji cameras | fixed | The issue requested an automatic reset for the unrecoverable hang. The fork arms the M5PM1 watchdog at boot (`src/main.cpp:478`), uses a 45 s timeout and a 1 s feed (`include/FurbleWatchdog.h`), and covers the PMIC path in `tests/host/pmic_recovery_test.cpp`. The root crash was not reproduced and is not claimed fixed, but the lock-up now recovers without draining the battery. |
| 308 | JJC BTR-F1 complete button bitfield | not applicable | The report describes a third-party remote emulating Fujifilm button payloads. The fork is a camera-side BLE central and has no zoom, record, or lock command (`lib/furble/Fujifilm.cpp`). The issue is protocol research, not a furble defect. |
| 309 | Compile error from tinygpsplus library | fixed | The VSCode PlatformIO extension runs `git fetch --depth=1 origin <pin>` and a short SHA fails with `couldn't find remote ref`. A full SHA fetch succeeds. `platformio.ini`, `sim/platformio.ini`, and `tools/reproducible-build.lock` now use `92ac8c21b8179201c2c8c534e46af3320b76d3e7`. This branch. |
| 311 | Ricoh GR IV Monochrome compatibility with M5StickS3 | needs hardware | The Ricoh class targets the GR IV protocol (`lib/furble/Ricoh.cpp`) and the upstream maintainer states the Monochrome shares the firmware base. No Monochrome body has been tested. The README and hardware table currently name only the GR IV HDF, so the compatibility claim stays out of the docs until hardware evidence exists. |
| 312 | Support for Nikon Zf | needs hardware | The Nikon Remote class implements the ML-L7 shutter path (`lib/furble/NikonRemote.cpp`), which is the mode upstream recommends for the Zf. Smart mode is unsupported as in issue 257. No Zf is available to confirm that remote mode pairs. |
| 315 | Canon PowerShot G5 X Mark II confirmed working | needs hardware | The Canon Smart path is present (`lib/furble/CanonEOSSmart.cpp`) and the report is against upstream v3.9.1. The fork has not run this body, and the reported reconnect failure has not been reproduced. The report does not justify a new support claim in the fork docs. |

## Closed issues

No upstream issue has closed after the plan 108 snapshot. Issue 305 (ThinkNode
M4 GPS sidecar) predates the snapshot but plan 108 lists it in ACTIONABLE; it
is closed upstream and remains catalogued in plan 40 without hardware or a
build. The rest of the closed issue mapping in plan 108 is unchanged.

## Carried change

The only actionable missing item that can be fixed without hardware was issue
309, which is a build failure in our tree, not an upstream code change. The
short TinyGPSPlus pin is replaced
with the full 40-character commit SHA in both PlatformIO projects and in the
reproducible build lock. The commit is unchanged; only the pin spelling is
different.

Evidence:

- `git fetch --depth=1 origin 92ac8c2` fails with
  `fatal: couldn't find remote ref 92ac8c2`.
- `git fetch --depth=1 origin 92ac8c21b8179201c2c8c534e46af3320b76d3e7`
  succeeds and resolves to the expected commit.
- `python3 tools/reproducible_build.py --check-inputs` passes with the lock
  updated.

## Validation

- Host suite via `cmake` and `ctest`.
- Simulator build with `sim/build.sh`.
- The simulator scenarios touched by the issue mapping are unchanged, so the
  scenario manifest is unchanged. `python3 tools/check_sim_scenarios.py`
  still runs in pre-review.
- `python3 tools/pre_review.py --base origin/master`.
- No C++ source changed, so the static analysis gate is not required.

## Documentation gaps and decisions

- Lumix is implemented and host-peer tested but absent from the README vendor
  list and `docs/supported-hardware.md`. That gap predates this branch and is
  left for a focused docs change with the same untested qualifier.
- Ricoh GR IV Monochrome and Canon PowerShot G5 X Mark II have upstream user
  reports only. They stay out of the confirmed lists until the fork has
  hardware evidence.
- The GFX100 disconnect issue needs a GFX100 body before any port of the
  unmerged upstream branch is justified.
- The Xteink X3 PR is a separate hardware port and is not part of this sync.

## Constraints honored

- Read-only against `gkoh/furble`. Nothing was posted, commented, or pushed
  upstream.
- The fork's companion BLE, simulator, timezone, power, and UI work was not
  modified.
- No upstream history was merged or rebased.
