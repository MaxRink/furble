# 108 - upstream issue mapping

## Goal

Cross-reference every upstream gkoh/furble GitHub issue, open and closed,
against this fork so we know what we already cover, what is in flight, and what
is still worth picking up. This lets each fork PR cite the upstream issues it
addresses. This is a mapping and analysis document. No code changes. Nothing is
posted to gkoh/furble. All references stay in fork PR bodies on MaxRink/furble.

Source data: `gh issue list --repo gkoh/furble --state all` (108 issues, 10
open, 98 closed) and `gh pr list --repo MaxRink/furble --state all` captured
2026-08-22. Base fork/master 9a2f7be.

## Status legend

- ALREADY-FIXED: the fork carries a distinct PR, plan, or code change that
  addresses the issue. The mapped PR is named.
- IN-FLIGHT: an open fork PR or active branch addresses it. The PR is named.
- ACTIONABLE: not addressed, but we could. Scope and hardware flag given.
- N/A: not applicable to our fork. Upstream infra, docs, wontfix, duplicate, or
  fixed upstream with no distinct fork work. Many closed upstream bugs land here
  because we inherit the upstream fix. Related fork work is noted where useful.

## Summary counts

| Status | Count |
|---|---|
| ALREADY-FIXED | 30 |
| IN-FLIGHT | 8 |
| ACTIONABLE | 3 |
| N/A | 67 |
| Total | 108 |

## ALREADY-FIXED (30)

| upstream | state | title | mapped PR / plan | note |
|---|---|---|---|---|
| #8 | closed | Battery saving with display (colorscheme/brightness) | #26 display-off (plan 12), #5 battery (plan 02), #35 auto-off (plan 13) | display sleep + battery-aware brightness |
| #22 | closed | Modify and enforce code style | #22 fix/clang-format | clang-format 21 CI enforced |
| #32 | closed | Add transmit power control | #25 adaptive-tx-power (plan 11) | adaptive TX power |
| #43 | closed | Add GPS location and time sync | #6 GPS-while-connected (plan 03) + GPS suite | baseline upstream, fork extends heavily |
| #63 | closed | Quickly pressing buttons in shooting mode gets stuck | #77 ui-bug-batch, #115 interval-state-bugs, #129 button-indicator-highlight | UI focus/mode-latch fixes |
| #69 | closed | Add web based installer | #46 web-installer, #86/#89/#102/#103 | fork GitHub Pages flasher |
| #71 | closed | Handle Bluetooth connection dropping | #109 disconnect-liveness, #114 connui, #128 disconnect-freeze, #19 backoff (plans 82/89) | core liveness suite |
| #75 | closed | M5StickC Plus range lower | #25 adaptive-tx-power (plan 11) | TX power / range |
| #102 | closed | Update GPS geotag whilst connected | #6 GPS-while-connected (plan 03) | GPS shown/updated while connected |
| #116 | closed | Further extend communication range | #25 adaptive-tx-power (plan 11) | TX power control |
| #140 | OPEN | Changing button config in settings | #58 button-mode (plan 62) | configurable one-button mode; open upstream |
| #151 | closed | Improve battery during Infinite-ReConnect | #19 reconnect-backoff (plan 09), #10 ble-sleep, #24 adaptive-conn-params | backoff + sleep during reconnect |
| #167 | closed | Portrait button icons no longer align | #117 layout-overflow, #137 layout-audit (plan 107) | narrow-panel layout fits |
| #175 | closed | Unable to cancel connection when disconnected in shutter mode | #19 reconnect-backoff, #114 connui | interruptible reconnect + single Cancel |
| #179 | closed | Plus2 failed to boot after infinite intervalometer | #115 interval-state-bugs | reset interval run state |
| #192 | closed | Fujifilm unable to connect after successful pairing | #121 reconnect-stuck, #126 fuji-guard, #93 false-connected-gate | fuji connect reliability |
| #195 | closed | Auto connect after power on | #62 reconnect-after-restart (plan 68) | graceful restart reconnect |
| #216 | closed | Infinite-ReConnect (FujifilmSecure) does not work | #121 reconnect-stuck-impl | fast-reconnect stuck on Fuji Secure |
| #217 | closed | M5Core scanning not behaving as expected | #11 scan-tuning (plan 08) | scan duty presets + timeout |
| #222 | closed | Shutter window weirdness during disconnect | #128 disconnect-freeze, #114 connui | non-blocking disconnect |
| #244 | OPEN | Timer operation for ASTRO/BULB | #33 exposure-presets (plan 35), #61 bulb-ux (plan 67) | plan 35 closes #244; open upstream |
| #245 | closed | Support the new M5StickS3 | #2 s3-psram (plan 31), #20 pm1-watchdog (plan 26) | S3 is the primary fork target |
| #252 | closed | X-E5 timeout/disconnection not recognized | #109 disconnect-liveness, #114 connui, #93 false-connected-gate | prompt disconnect detection |
| #267 | closed | Add Ricoh GR Bluetooth support | #78 pentax-k (plan 73) | Ricoh Imaging BLE family; GR-IV hw pending |
| #293 | closed | Scanning never stops, radio at full duty | #11 scan-tuning (plan 08) | exactly plan 08 |
| #295 | closed | No way to see device state in the field | #8 diagnostics (plan 05), #1 usb-console, #130 console-debug | diagnostics + console |
| #298 | closed | StickS3 can lock up with no reset | #20 pm1-watchdog (plan 26), #132 s3-flash-watchdog-reset | hardware watchdog |
| #299 | closed | No way to drive furble from host for testing | #1 usb-console (plan 27), #130 console-debug, sim harnesses | automation surface |
| #300 | closed | StickS3 burns tens of mA idle while connected | #10 ble-sleep (plan 07), #9 power-module, plan 98 power audit | modem sleep + DFS |
| #302 | closed | GPS receiver cannot be configured | #12 gps-pcas (plan 14), #64 gps-advanced (plan 32) | PCAS + CASIC config |

## IN-FLIGHT (8)

| upstream | state | title | mapped open PR / plan | note |
|---|---|---|---|---|
| #128 | closed | Add simultaneous multiple camera connection | #38 multiconnect-ui (plan 25) | multi-connect landed upstream; fork adds selection UI |
| #196 | OPEN | Additional trigger mechanisms | #29 IR-trigger (merged), #45 imu-gestures, plan 91 mic-trigger | several trigger paths, some merged |
| #221 | OPEN | Frequent disconnects during shutter with GFX100 | #109/#114/#128/#121 liveness suite | fixes merged; needs GFX100 hardware to confirm |
| #247 | OPEN | Support Panasonic Lumix | #79 lumix (plan 72) | Lumix BLE vendor, untested |
| #248 | OPEN | WIFI feature | #31 wifi-hub, #53 provisioning, #66 mqtt, #90 webui (plans 33/33b/33c/33d) | full WiFi hub track |
| #249 | OPEN | Board-Only support | #31 wifi-hub displayless (plan 33), #66 mqtt | headless board + HA via MQTT |
| #257 | OPEN | Support for Nikon Z50II | #40 nikon-analysis (plan 60) | analysis done; blocked on LE-only stack (plan 65) |
| #297 | closed | Multi-Connect no visibility into connected cameras | #38 multiconnect-ui (plan 25) | camera status page |

## ACTIONABLE (3)

| upstream | state | title | scope | hardware |
|---|---|---|---|---|
| #159 | OPEN | Fujifilm scan unreliable if initiated before pairing | Investigate scan-before-pairing timing; may extend #93 false-connected-gate to also cover scan initiation ordering. 1-2 day scope. | needs Fuji |
| #201 | OPEN | M5Tough compatibility | Add M5Tough (touch Core variant) board env; reuse Core2 UI path. Catalogued in plan 88. | needs M5Tough hw |
| #305 | OPEN | ThinkNode M4 GPS sidecar target | Port furble to ThinkNode M4 as a GPS sidecar; documented in plan 40, not built. | needs ThinkNode hw |

## N/A (67)

Upstream infra, documentation, wontfix, board notes, or bugs fixed upstream that
the fork inherits. Related fork work noted where it exists.

| upstream | state | title | why N/A | related fork work |
|---|---|---|---|---|
| #5 | closed | M5StickC EOL, replaced with Plus | board note | Plus supported |
| #7 | closed | Works with Fujifilm X-S10 | info confirmation | camera-compat catalog (plan 61) |
| #9 | closed | User-friendly instruction | docs | |
| #10 | closed | Focus button on m5Stick | fixed upstream (Fuji AF) | |
| #11 | closed | Donation button | non-code | |
| #14 | closed | Rename project | non-code | |
| #16 | closed | Gfx100s owner | support query | |
| #19 | closed | Add releases and versioning | upstream infra | fork has own release plan |
| #21 | closed | Support Canon EOS RP | fixed upstream | camera-compat (plan 61) |
| #25 | closed | Refactor Canon EOS support | upstream refactor | |
| #28 | closed | Shutter releases immediately (Canon) | fixed upstream | |
| #29 | closed | Focus support for Canon EOS | fixed upstream | |
| #34 | closed | Installation info | docs | |
| #35 | closed | Handle pairing confirmation for Canon EOS | fixed upstream | relates #63 pairing-code |
| #37 | closed | Refactor pairing and connecting | upstream refactor | |
| #39 | closed | X-T30 fw 1.50 cannot pair | fixed upstream | |
| #41 | closed | furble identifies as smartphone (new Fuji fw) | fixed upstream | |
| #46 | closed | Remote will not work with new Fuji updates | fixed upstream | |
| #48 | closed | Update docs for working cameras | docs | camera-compat (plan 61) |
| #52 | closed | Migrate to M5Unified | upstream infra | |
| #55 | closed | Support M5Core, M5Core2 | fixed upstream | Core/Core2 build envs present |
| #57 | closed | Update docs for v2 | docs | |
| #59 | closed | Support M5StickC PLUS2 | fixed upstream | board supported |
| #60 | closed | Add basic interval shutter release | fixed upstream | #59 interval deep sleep extends |
| #65 | closed | Add shutter lock mode | fixed upstream | |
| #67 | closed | Add advanced interval shutter release | fixed upstream | plan 90 scheduled shooting |
| #80 | closed | Better UI for Core2 | fixed upstream (LVGL rewrite) | fork UI polish #104/#105 |
| #83 | closed | Per-remote identifier support | fixed upstream | |
| #85 | closed | Disable Connect/Delete if no connections | fixed upstream | |
| #86 | closed | Backlight doesn't turn off on Plus2 | fixed upstream | #26 display-off |
| #90 | closed | Long press activation for shutter lock | fixed upstream | |
| #92 | closed | Mobile device (smartphone) support | fixed upstream | fork companion app #18 |
| #95 | closed | GFX100II GPS not working | fixed upstream | relates GPS suite |
| #97 | closed | Improve web installer content | docs | fork flasher |
| #100 | closed | Human readable peer name for smartphone | fixed upstream | fork companion #55 |
| #101 | closed | Migrate deployment to wrangler | upstream CI | fork dropped Cloudflare (#103) |
| #105 | closed | Use table for What Works README | docs | camera-compat uses tables |
| #109 | closed | Update M5Unified/M5GFX/NimBLE deps | upstream infra | |
| #111 | closed | Backlight timeout brightness too high | fixed upstream | #26 display-off |
| #112 | closed | Backlight menu slider tweaking | fixed upstream | |
| #120 | closed | Screen size | query | |
| #123 | closed | Unify M5StickC firmware images | upstream infra | |
| #129 | closed | Connection without timeout | fixed upstream | #19 reconnect suite |
| #134 | closed | Fujifilm X-S20 doesn't work | fixed upstream | #126 fuji-guard; X-S20 hw pending |
| #135 | closed | Log RSSI from scan results | fixed upstream | #130 console-debug logs scan |
| #136 | closed | Log the firmware version | fixed upstream | #8 diagnostics |
| #141 | closed | Mini GPS/BDS unit EOL | hw note | fork GPS supports AT6668 |
| #149 | closed | M5Stack Core Power Off doesn't | fixed upstream | #9 power-module |
| #150 | closed | UI issues in v3.0.0-rc2 | fixed upstream | fork UI batch |
| #152 | closed | Screen lock for touch screens | fixed upstream | |
| #154 | closed | Improve touch screen experience | fixed upstream | |
| #165 | closed | Enabling GPS hangs in v3.0.0 | fixed upstream | #27 gps-power |
| #183 | closed | Multi-Connect lag issues | fixed upstream | relates #38 |
| #184 | closed | Add X-E4 as confirmed working | docs | |
| #189 | closed | Support Canon EOS GPS | fixed upstream | camera-compat (plan 61) |
| #193 | closed | Switchable delay after single trigger | fixed upstream | |
| #200 | closed | Fujifilm X-M5 shutter works but no GPS | fixed upstream | relates GPS suite |
| #206 | closed | Support more camera models (Nikon) | fixed upstream | camera-compat (plans 60/61) |
| #208 | closed | Fuji fw July 2025 breaks connectivity | fixed upstream | #126 fuji-guard |
| #209 | closed | Nikon Z6III support | fixed upstream | camera-compat (plan 60) |
| #231 | closed | Core2 touch stops responding if idle | fixed upstream | #26 display-off |
| #235 | closed | Nikon Z7II test without screen | fixed upstream | #31 displayless relates |
| #251 | closed | Touch screen limitation/frustration | fixed upstream | |
| #265 | closed | Support Fujifilm H2 | fixed upstream (Fuji) | |
| #271 | closed | Publish firmware to M5Burner | distribution | fork flasher |
| #278 | closed | Support M5StickC Plus SE | fixed upstream | board supported |

## PR reference lines

Exact lines to paste into fork PR bodies. Only confident matches. Use
`Addresses` where the PR resolves the issue for the fork, `Relates to` where it
partially covers or is closely related.

### Open fork PRs

- #31 wifi-hub: Addresses gkoh/furble#249 - Relates to gkoh/furble#248
- #38 multiconnect-ui: Addresses gkoh/furble#297 - Relates to gkoh/furble#128, gkoh/furble#181, gkoh/furble#183
- #45 imu-gestures: Relates to gkoh/furble#196
- #53 wifi-provisioning: Relates to gkoh/furble#248
- #59 interval-deep-sleep: Relates to gkoh/furble#67, gkoh/furble#151
- #66 mqtt: Addresses gkoh/furble#248 - Addresses gkoh/furble#249
- #79 lumix: Addresses gkoh/furble#247
- #90 webui: Addresses gkoh/furble#248 - Relates to gkoh/furble#249
- #93 false-connected-gate: Addresses gkoh/furble#252 - Relates to gkoh/furble#159, gkoh/furble#192, gkoh/furble#221
- #137 ui-layout-audit: Relates to gkoh/furble#167, gkoh/furble#150

### Recently merged fork PRs (this session)

- #114 connui: Addresses gkoh/furble#71, gkoh/furble#175, gkoh/furble#222, gkoh/furble#252 - Relates to gkoh/furble#221
- #126 fuji-guard: Relates to gkoh/furble#192, gkoh/furble#208, gkoh/furble#134, gkoh/furble#221
- #128 disconnect-freeze: Addresses gkoh/furble#222 - Relates to gkoh/furble#71, gkoh/furble#221, gkoh/furble#252
- #133 menu-focus-outline-dedup: Relates to gkoh/furble#150, gkoh/furble#167
- #109 disconnect-liveness: Addresses gkoh/furble#252, gkoh/furble#71 - Relates to gkoh/furble#221
- #121 reconnect-stuck-impl: Addresses gkoh/furble#216 - Relates to gkoh/furble#192
- #120 nimble-client-leak: Relates to gkoh/furble#71, gkoh/furble#216
- #115 interval-state-bugs: Addresses gkoh/furble#179 - Relates to gkoh/furble#63

### Earlier merged fork PRs (for completeness)

- #58 button-mode: Addresses gkoh/furble#140
- #62 reconnect-after-restart: Addresses gkoh/furble#195
- #11 scan-tuning: Addresses gkoh/furble#293, gkoh/furble#217
- #19 reconnect-backoff: Addresses gkoh/furble#151 - Relates to gkoh/furble#175, gkoh/furble#129
- #25 adaptive-tx-power: Addresses gkoh/furble#32, gkoh/furble#116 - Relates to gkoh/furble#75
- #12 gps-pcas: Addresses gkoh/furble#302
- #64 gps-advanced: Relates to gkoh/furble#302
- #6 gps-while-connected: Addresses gkoh/furble#102 - Relates to gkoh/furble#43
- #33 exposure-presets: Addresses gkoh/furble#244
- #61 bulb-ux: Relates to gkoh/furble#244
- #20 pm1-watchdog: Addresses gkoh/furble#298
- #10 ble-sleep: Addresses gkoh/furble#300
- #46 web-installer: Addresses gkoh/furble#69
- #1 usb-console: Addresses gkoh/furble#299, gkoh/furble#295
- #130 console-debug: Addresses gkoh/furble#295 - Relates to gkoh/furble#299, gkoh/furble#135
- #8 diagnostics: Addresses gkoh/furble#295 - Relates to gkoh/furble#136
- #26 display-off: Addresses gkoh/furble#8 - Relates to gkoh/furble#86, gkoh/furble#111, gkoh/furble#231
- #78 pentax-k: Addresses gkoh/furble#267
- #40 nikon-analysis: Relates to gkoh/furble#257, gkoh/furble#206, gkoh/furble#209
- #77 ui-bug-batch: Relates to gkoh/furble#63, gkoh/furble#150
- #117 layout-overflow: Relates to gkoh/furble#167

## Top actionable pickups, ranked

1. #159 Fujifilm scan unreliable before pairing (needs Fuji hardware). Closest
   to landing, extends the false-connected work already in #93.
2. #201 M5Tough board support (needs M5Tough hardware). Mechanical board add on
   top of the existing Core2 UI path.
3. #305 ThinkNode M4 GPS sidecar (needs ThinkNode hardware). Larger port,
   already scoped in plan 40.

## Constraints honored

- Read-only against gkoh/furble. Nothing posted, commented, or edited upstream.
- No code changed. This is a mapping document only.
- All issue references live in fork PR bodies on MaxRink/furble.
