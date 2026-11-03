# 65: Broader Bluetooth: Classic BT feasibility and WiFi+BLE coexistence

Status: research, no implementation. All cited links were fetched and
verified during this survey (August 2026). Unverified items are flagged
inline. Repo facts were read on fork master at `f455b0b`.

## Motivation

Three pressures converge on the same question: what can furble's radio
actually do beyond a single-purpose NimBLE central?

1. Nikon smart device support (upstream issue 257) is blocked on a
   Bluetooth Classic bond. `plans/60-issue-257.md` established that the
   first-time pairing swaps from BLE to a Classic secure simple pairing
   bond, that NimBLE is LE only, and that the hurui200320/nsg project
   proves the full flow works on a plain ESP32. Whether furble should
   ever follow costs a stack decision, not a feature patch.
2. The WiFi hub mode (`plans/33-wifi-hub.md`, PRs 33b/33c) will put WiFi
   STA plus MQTT on the same 2.4 GHz radio as up to nine BLE camera
   links. Plan 33 has a coexistence risk section. This document deepens
   it with the Espressif issue record and field data, and turns it into
   a concrete config and test list.
3. The upstream history has scars worth learning from before touching
   the stack: a Bluedroid proof of concept that failed multi-vendor, a
   string of esp-nimble-cpp regressions that each broke one vendor, and
   an advertising workaround that took two PRs to understand and remove.

This document is the record of all three investigations. It proposes no
code. Its verdicts feed plans 33 and 60 and any future stack work.

## Track 1: upstream history

### WiFi plus Bluetooth

Upstream has never run WiFi and Bluetooth together. Searches for "coex"
and "coexistence" across gkoh/furble issues, PRs and commits return
nothing. The WiFi feature requests are
[#248](https://github.com/gkoh/furble/issues/248) (hub mode: MQTT,
WebUI, NTP, OTA) and
[#249](https://github.com/gkoh/furble/issues/249) (displayless board
build). gkoh's staging in #249 puts the console first, network last,
and names MQTT as the first network target in #248. The only place WiFi
and BT intersect upstream is
[#247](https://github.com/gkoh/furble/issues/247) (Lumix): the user
observed Lumix Sync briefly activating the camera's WiFi during first
BT pairing, and gkoh noted the ESP32's WiFi "is not used, but it could
be". So there are no upstream coexistence scars yet. The scars are all
on the BLE stack itself.

### Why NimBLE

NimBLE was the founding choice, not a migration. The initial 2020
commits vendor NimBLE-Arduino as a subtree (`0507573`, `2b2909a`).
No rationale is recorded anywhere in the repo, issues or early README.
There was never a Bluedroid era. The framework later moved Arduino to
ESP-IDF (`db551aa` PR 148, `0880431` PR 224) and NimBLE-Arduino to
esp-nimble-cpp (`8325195`). The only comparative judgment on record is
recent, from [#257](https://github.com/gkoh/furble/issues/257):
"Bluedroid is an utter wasteland of weird caveats."

### Classic BT attempts

- [#206](https://github.com/gkoh/furble/issues/206): the discovery.
  "In this mode, the B600 changes from BLE to Bluetooth Classic halfway
  through handshake to do the bonding. While the ESP32 does support
  Bluetooth Classic, the library (NimBLE-Arduino) I used is BLE only
  and rewriting it (to use Bluedroid) is a large effort."
- [#209](https://github.com/gkoh/furble/issues/209): Z6III bring-up.
  gkoh: "I am very keen to get Nikon smart mode working because it gets
  us both focus and GPS on the newer cameras. To achieve that I will
  need to change the Bluetooth library used."
- [#257](https://github.com/gkoh/furble/issues/257): the decisive
  thread. Contributor h3ifri proved full Z50II pairing on a plain ESP32
  with Bluedroid, but bond persistence required patching Bluedroid
  itself ("auth_req in the IO capability reply is sent as
  BTM_AUTH_AP_YES = 0x03", a `btm_sec.c` patch). gkoh then built his
  own minimal Bluedroid port and concluded: "I cannot support Fujifilm,
  Sony and Nikon (smart) in a single build due to crazy limitations on
  the Bluetooth LE privacy handling. ATM I am considering the effort
  and future maintenance for a Nikon specific version of furble that
  will only support Nikon smart/remote mode." His stated hardware
  guidance: full Nikon needs an M5StickC or equivalent plain ESP32.

gkoh never names the exact privacy limitation. The likely mechanism is
identified in Track 2 below.

### Multi-connect and disconnect history

The instability record traces to specific library bugs, not to the
radio or to the multi-connect design:

- [#183](https://github.com/gkoh/furble/issues/183) multi-connect lag:
  measured BT jitter was 30 to 60 ms; the real culprit was the camera's
  SD write throughput. The radio was fine.
- [#216](https://github.com/gkoh/furble/issues/216) infinite reconnect
  broken: root cause
  [esp-nimble-cpp #360](https://github.com/h2zero/esp-nimble-cpp/issues/360),
  disconnect events failed to match the client when the peer uses a
  rotating RPA, so `onDisconnect()` never fired. gkoh submitted the
  fix, merged upstream December 2025.
- [#221](https://github.com/gkoh/furble/issues/221) GFX100 disconnects
  during shutter: regression isolated to a NimBLE-Arduino update, never
  root-caused, still open.
- [#252](https://github.com/gkoh/furble/issues/252) X-E5 disconnect not
  recognized: root cause
  [esp-nimble-cpp #395](https://github.com/h2zero/esp-nimble-cpp/issues/395),
  a 2.3.4 descriptor discovery regression, fixed upstream March 2026,
  shipped in furble v3.8.0.
- [esp-nimble-cpp #356](https://github.com/h2zero/esp-nimble-cpp/issues/356)
  is still open: a public-address central times out connecting to an
  RPA peripheral, the Z6III symptom from #209.
- [#159](https://github.com/gkoh/furble/issues/159) scan duplicate
  filtering hides re-paired cameras, tracked upstream as
  [NimBLE-Arduino #968](https://github.com/h2zero/NimBLE-Arduino/issues/968).
- PR [#194](https://github.com/gkoh/furble/pull/194) reverted a
  NimBLE-Arduino update over X-T5 reconnect failures, and PR
  [#255](https://github.com/gkoh/furble/pull/255) notes esp-nimble-cpp
  2.3.4 "breaks Nikon BLE attribute discovery".

No systemic multi-central instability exists in esp-nimble-cpp 2.5.0 as
far as either tracker shows. The pattern is instead: every stack update
breaks one vendor, and gkoh personally debugs the library. Any stack
change multiplies that surface. That is the real historical lesson.

### Advertising removal

Commit `5564b73` is PR
[#281](https://github.com/gkoh/furble/pull/281). Advertising had been
kept as a Sony workaround; PR
[#255](https://github.com/gkoh/furble/pull/255) restored it with "for
some reason the advertising service is required for Sony cameras to
connect. There is some interaction here that I do not understand."
#281 closed the loop: Sony only reads the GATT device name during
connect, so a running server suffices and advertising is unnecessary.
Not a coexistence story, but relevant to plan 50 and to any Insta360
peripheral-role idea: furble is now a pure central with a passive
server, which is the cheapest possible BLE posture for coexistence.

Fork note: upstream declined this fork's scan duty-cycle proposal
([#293](https://github.com/gkoh/furble/issues/293), PR 294 closed
unmerged). Scan behavior changes need stronger evidence upstream.

## Track 2: Bluetooth Classic (BR/EDR) feasibility

Scope: the four plain ESP32 boards only (m5stick-c, m5stick-c-plus,
m5stack-core, m5stack-core2). The ESP32-S3 has no Classic radio
([product page](https://www.espressif.com/en/products/socs/esp32-s3)
lists Bluetooth 5 LE only) and is permanently out, per plans/60.

### (a) Bluedroid dual-mode exists and is the only in-tree path

ESP-IDF v5.4 supports dual-mode Bluedroid on ESP32. The
[v5.4 BLE overview](https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32/api-guides/ble/overview.html)
states "ESP-Bluedroid for ESP32 supports Classic Bluetooth and
Bluetooth LE" while "ESP-NimBLE supports Bluetooth LE only. Classic
Bluetooth is not supported." The
[v5.4 Bluetooth API index](https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32/api-reference/bluetooth/index.html)
says plainly: "For usecases involving classic Bluetooth as well as
Bluetooth Low Energy, Bluedroid should be used."

Costs versus NimBLE. Espressif stays qualitative ("ESP-NimBLE requires
less heap and flash size"). Concrete third-party numbers:

- [Moddable SDK measurements](https://moddable.com/blog/moddable-sdk-improvements-for-esp32-projects/):
  switching Bluedroid to NimBLE made firmware "about 254 KB smaller"
  and left "33 KB more system memory free" after a connection, and that
  is BLE-only Bluedroid. Dual-mode with Classic profiles costs more.
- [esp-nimble-cpp README](https://github.com/h2zero/esp-nimble-cpp):
  "nearly 50% reduction in flash use and approx. 100kB less ram
  consumed" versus the old Bluedroid-based Arduino BLE library.
- An esp32.com thread reporting ~167 KB flash and ~8.5 KB heap savings
  is behind a bot challenge. Unverified.

For boards already running LVGL plus a 9-connection NimBLE central in
320 KB of DRAM, budget roughly 100 KB or more of extra RAM pressure and
250 KB of flash for a Bluedroid build. Nobody has measured Bluedroid
dual-mode next to an LVGL UI of furble's size. Unverified risk.

### (b) One host stack at a time is a hard build-time constraint

The ESP-IDF host selection is a Kconfig `choice` in
[components/bt/Kconfig at v5.4](https://github.com/espressif/esp-idf/blob/v5.4/components/bt/Kconfig):
`BT_BLUEDROID_ENABLED` ("Bluedroid - Dual-mode"), `BT_NIMBLE_ENABLED`
("NimBLE - BLE only"), or controller-only. Exactly one host links. Both
hosts attach to the same VHCI controller interface, so NimBLE for LE
plus Bluedroid for BR/EDR concurrently is impossible. The nsg source
states the consequence directly: "Bluedroid owns the HCI interface to
the BT controller. Disabling Bluedroid to use VHCI directly would kill
BLE" (`esp32/src/pairing/ClassicBT.cpp` in
[hurui200320/nsg](https://github.com/hurui200320/nsg)). A minimal
Classic-bonding-only shim next to NimBLE does not exist and cannot.

furble's sdkconfigs confirm the current posture: all five boards set
`CONFIG_BT_NIMBLE_ENABLED=y`, and the ESP32 boards additionally pin the
controller to `CONFIG_BTDM_CTRL_MODE_BLE_ONLY=y`
(`sdkconfig.m5stick-c:636`), so BR/EDR is compiled out of the
controller today.

### (c) What the Nikon flow needs, per nsg

[hurui200320/nsg](https://github.com/hurui200320/nsg) (AGPL-3.0, so
reference only, no code reuse in MIT furble) runs the complete flow on
a plain ESP32 WROOM-32E under Bluedroid dual-mode (Arduino-ESP32 3.3.9
on ESP-IDF 5.5.4):

1. Scan, then the 4-stage BLE handshake, then disconnect BLE.
2. Classic discovery. The camera exposes a different Classic address
   than its BLE address and only becomes discoverable after the BLE
   handshake (`doc/nikon-z-gps.md`).
3. Dedicated bonding with numeric comparison. The camera exposes no
   Classic profile, so `esp_spp_connect` fails before security runs.
   ESP-IDF has no public API for profile-less GAP bonding. nsg links
   the internal Bluedroid symbol `BTA_DmBond` directly and runs an SPP
   slave (`serialBT.begin`) because pairing fails without it
   (`esp32/src/pairing/ClassicBT.cpp`).
4. Save the camera, reboot, reconnect over BLE. Geo writes now work.

nsg carries no ESP-IDF source patch. h3ifri's alternative SPP-connect
approach in #257 needed the `btm_sec.c` auth_req patch for the bond to
survive a camera reboot. `BTA_DmBond` does dedicated bonding, which
plausibly avoids that, but nsg does not explicitly confirm
reboot-survival. Unverified. Either way the choice is an internal
symbol that can vanish at link time, or a carried IDF patch. Both are
maintenance liabilities.

nsg's own resource fight was IRAM, not heap: it disables A2DP, HFP,
BluFi and WiFi and moves FreeRTOS functions to flash to claw back IRAM
(`esp32/platformio.ini`). nsg has no display, so it says nothing about
LVGL coexistence.

### The privacy wall

The likely mechanism behind gkoh's multi-vendor failure is the ESP32
`BT_BLE_RPA_SUPPORTED` trap in
[Bluedroid's Kconfig at v5.4](https://github.com/espressif/esp-idf/blob/v5.4/components/bt/host/bluedroid/Kconfig.in):
the original ESP32 "only support network privacy mode. If this option
is enabled, ESP32 will only accept advertising packets from peer
devices that contain private address ... If this option is disabled,
address resolution will be performed in the host, so the functions that
require controller to resolve address in the white list cannot be
used." That is a global either/or. Fujifilm secure rotates its MAC
(upstream [#217](https://github.com/gkoh/furble/issues/217)), Sony
bonds with RPAs, and furble itself uses an RPA identity
(`lib/furble/Device.cpp:48`). No single Bluedroid privacy configuration
covers all of it on the original ESP32. This mapping is inference from
the Kconfig text and #217; gkoh never named the exact API. Flagged.

### (d) Migration surface

[esp-nimble-cpp](https://github.com/h2zero/esp-nimble-cpp) is
NimBLE-only. No Bluedroid backend, none planned. No maintained C++
wrapper over the Bluedroid GATT client exists: nkolban's cpp_utils is
dead and targets IDF 3.x/4.x, the Arduino BLE library is welded to the
Arduino core furble just removed (PR 224), and nothing else is
comparable. A Bluedroid variant means rewriting lib/furble's central
layer against the raw `esp_gattc`/`esp_gap_ble` callback API (see the
[Espressif GATT client walkthrough](https://github.com/espressif/esp-idf/blob/master/examples/bluetooth/bluedroid/ble/gatt_client/tutorial/Gatt_Client_Example_Walkthrough.md))
while the S3 keeps NimBLE. Two stacks, two behavior sets, double the
vendor-regression surface that Track 1 shows gkoh already struggles to
service with one stack.

[BTstack](https://github.com/bluekitchen/btstack) is a third option,
proven dual-mode on plain ESP32 by
[bluepad32](https://github.com/ricardoquesada/bluepad32) (which uses
BTstack, not Bluedroid). License is free for open source, commercial
otherwise. It replaces the stack for all vendors, has no C++ GATT
client wrapper either, and is a less-traveled ESP-IDF port. Strictly
more work than Bluedroid for no guarantee on the privacy wall.

### (e) Verdict

| Route | Effort | Assessment |
|---|---|---|
| Full Bluedroid migration (ESP32 boards) | XL | Blocked by the privacy wall gkoh already hit. Rewrite with no wrapper. Dual-stack forever. Do not attempt. |
| Nikon-pairing-only Bluedroid build variant | L | gkoh's own stated direction. Sidesteps the privacy wall. Still a new build matrix dimension, a raw-C BLE path, and a bonding hack (internal symbol or carried patch). |
| BTstack | XL | Proven dual-mode, but replaces everything for all vendors. No. |

Which boards benefit: only the four plain ESP32 boards, and of those
only users with Nikon bodies that lack ML-L7 remote mode or who need
GPS. The fork's reference hardware is an S3 and its cameras are
Fujifilm. plans/61 ranks Lumix (upstream PR 282 exists), Pentax K
(possibly free) and DJI (documented protocol) all ahead of Nikon smart
on effort-to-value. Verdict for this fork: not worth it for Nikon
alone. Do not start a Classic effort. If gkoh ships his Nikon-specific
Bluedroid build upstream, adopt it as a build variant rather than
porting anything here. plans/60's recommendation stands unchanged.

## Track 3: WiFi + BLE coexistence for hub mode

This extends the coexistence section of `plans/33-wifi-hub.md` with the
Espressif issue record and field data. Plan 33's mitigations all
survive contact with the evidence. New findings are marked.

### The coexistence policy, from the v5.4 guide

[ESP32 guide](https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32/api-guides/coexist.html)
and
[ESP32-S3 guide](https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/api-guides/coexist.html).
One shared 2.4 GHz RF module per chip, time-division multiplexed by a
priority arbiter. With WiFi CONNECTED and BLE CONNECTED "the time
slices of Wi-Fi and BLE in a coexistence period each account for 50%",
and the period is anchored to the WiFi beacon (more than 100 ms). WiFi
SCAN and CONNECTING take extended slices, confirming plan 33's "never
scan while connected" rule. The support matrix marks every WiFi STA row
against every BLE state stable on both chips; SoftAP rows are unstable
on the S3. Hub mode must stay STA-only.

The guide's headline performance advice is core splitting: WiFi tasks
on one CPU, BT controller and host on the other. Both furble builds run
`CONFIG_FREERTOS_UNICORE=y`, so this lever is currently unavailable.
See the recommendation section.

Negative findings worth recording: the v5.4 guide contains no "increase
BLE connection interval" advice (that lives in issue responses), there
is no coex preference Kconfig in 5.x, and `esp_coex_preference_set` is
deprecated in the v5.4 header with a replacement whose BLE status bits
are MESH-only. Plan 33's rule 6 (do not touch that API) is confirmed
from both directions.

### The issue record, 2023 to 2026

- [espressif/esp-idf#15833](https://github.com/espressif/esp-idf/issues/15833)
  is furble's exact topology on the S3 controller family: BLE central
  plus WiFi STA plus MQTT, and the central intermittently dropped 4
  consecutive connection events, disconnecting setups with tight
  supervision timeouts. Espressif guidance from the thread: timeout at
  least 10x the interval, and "when the BLE connection interval is
  large enough (typically, larger than 50 ms), BLE will always use high
  priority to request RF resource". Also avoid intervals near multiples
  of the 102.4 ms beacon interval. A controller patch raising priority
  near disconnect fixed the reporter's case; whether it was merged into
  a released IDF is unverified.
- [espressif/esp-idf#18931](https://github.com/espressif/esp-idf/issues/18931)
  (open): BLE scanning under coex misses most advertisements after a
  v5.5.5 controller change. Espressif workaround: set scan interval and
  window equal when scanning under coex.
- [espressif/esp-idf#18049](https://github.com/espressif/esp-idf/issues/18049):
  WiFi+BLE+MQTT instability on ESP32 correlated with continuous BLE
  passive scanning, not with connected-idle BLE. Scanning is the
  aggressor; connections are cheap.
- [espressif/esp-idf#4719](https://github.com/espressif/esp-idf/issues/4719)
  (historical): ESP32 controller lockups combining coex and BTDM modem
  sleep. Old, but it puts the ESP32 boards in the soak-test matrix.

### Field data: ESPHome bluetooth_proxy

The largest deployed WiFi+BLE-central fleet.
[bluetooth_proxy docs](https://esphome.io/components/bluetooth_proxy/),
[esp32_ble_tracker docs](https://esphome.io/components/esp32_ble_tracker/):

- Default 3 active GATT connections on WiFi-based proxies, recommended
  ceiling 5, hard max 9. Ethernet boards "can generally handle 4"
  because they do not share the radio with WiFi. furble's 9-connection
  config plus WiFi is 3x the field-reliable default. This is the
  strongest single datapoint for capping hub-mode camera counts.
- Scan defaults that survive coex at fleet scale: interval 320 ms,
  window 30 ms, about 9.4% duty. Aggressive scan settings "can cause
  WiFi instability".
- Recurring issue patterns: connection slot leaks needing cleanup logic
  ([esphome/issues#6701](https://github.com/esphome/issues/issues/6701))
  and BLE activity causing brief WiFi stalls, not the reverse
  ([esphome/issues#7224](https://github.com/esphome/issues/issues/7224)).

### Modem sleep, DFS, and the fork's power setup

From the v5.4
[power management docs](https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32/api-reference/system/power_management.html):
the WiFi driver holds `ESP_PM_APB_FREQ_MAX` between start and stop, and
with modem sleep releases it while the radio is off. It takes neither
`ESP_PM_CPU_FREQ_MAX` nor `ESP_PM_NO_LIGHT_SLEEP`. The BT controller
already holds `ESP_PM_NO_LIGHT_SLEEP` permanently.

Consequence for the fork's DFS setup: WiFi STA with
`WIFI_PS_MIN_MODEM` changes the duty cycle of APB frequency ramps (a
wake per DTIM beacon, roughly every 102.4 ms, plus MQTT exchanges), not
the failure class. The known hardware traps (GPS UART on
`UART_SCLK_XTAL`, display holding `ESP_PM_APB_FREQ_MAX` while the
backlight is on) already make the peripherals ramp-proof. With
`WIFI_PS_NONE` the APB lock is held continuously and DFS is effectively
pinned while the hub is up. The existing BT sleep configs are correct
as-is on both chips: ESP32 ORIG mode with the main XTAL low-power
clock explicitly supports DFS, and the S3 equivalent
(`CONFIG_BT_CTRL_MODEM_SLEEP_MODE_1`) does too, per the v5.4 Kconfig
help texts.

### Expected connection-event loss

The worst observed consecutive-loss burst in the field record is 4
connection events (#15833). furble's parameters (interval 30 to 50 ms,
latency 1, supervision timeout 5120 ms, `lib/furble/Camera.h:180-185`)
tolerate roughly 100 consecutive missed events. Margin is about 25x the
observed worst case. Steady-state supervision timeouts from coex alone
are very unlikely. The plan 09 reconnect backoff covers the residual.
The realistic cost is latency jitter, exactly as plan 33 predicted, and
the sub-50 ms interval sitting below the controller's always-high
priority threshold. Quantitative throughput/latency numbers floating
around (12.4 to 8.2 Mbps, 15 to 32 ms) come from a single blog with no
methodology and cite an API that does not exist in v5.4. Unverified,
do not cite further.

### MQTT keepalive

[esp-mqtt v5.4 docs](https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32/api-reference/protocols/mqtt.html):
default keepalive 120 s, ping at half the interval, network timeout
10 s. Plan 33c's 60 s keepalive means one ping per ~30 s, negligible
next to per-beacon wakes. Keepalive tuning is irrelevant to coex
airtime; it only sets LWT detection latency (broker fires the will at
1.5x keepalive, so ~90 s). Keep 60 s, retained LWT availability topic,
publish-on-change, defaults elsewhere. The real MQTT-adjacent risk on
the no-PSRAM ESP32 boards is memory (the #18049 pattern), not timing.

## Recommendations

### Do now (fold into 33b/33c when they are implemented)

1. `esp_wifi_set_ps(WIFI_PS_MIN_MODEM)`, as plan 33 already says. It
   is also what keeps DFS alive.
2. While WiFi is up, raise the BLE minimum connection interval to just
   above 50 ms (51.25 to 60 ms, never near a multiple of 102.4 ms), so
   BLE holds always-high coex priority per #15833. Application change,
   ties into plan 10's adaptive connection parameters. A/B test it, do
   not hardcode it.
3. Cap the advertised hub-mode camera count. Document 3 concurrent
   cameras as the supported hub configuration and 5 as the tested
   ceiling, per the ESPHome field data. Nine plus WiFi is untested
   territory and must not be promised.
4. Keep every plan 33 mitigation: no scanning while connected, no OTA
   while connected, existing connection parameters when WiFi is off,
   publish on change, no deprecated coex APIs.
5. If BLE scanning under coex is ever unavoidable, set scan window
   equal to scan interval (#18931 guidance).
6. sdkconfig: nothing to change. Coex, full-scan support, and both
   modem sleep configs are already correct. Leave
   `CONFIG_ESP_COEX_POWER_MANAGEMENT` unset (undocumented). Keep the
   WiFi/lwIP buffer knobs from the coexist guide in reserve for the
   ESP32 boards if heap gets tight.

### Defer

- Any Bluetooth Classic work. Track 2 verdict: the only viable shape is
  a Nikon-only Bluedroid build variant, it is gkoh's stated direction,
  and the right fork move is to wait and adopt, not to lead. Revisit if
  upstream #257 produces a build.
- Dual-core hub builds. If soak tests show BLE starvation under WiFi,
  re-enabling the second core and pinning WiFi to core 1 is Espressif's
  primary documented lever. It changes power behavior and all five
  sdkconfigs, so it is its own experiment, not a default.
- BTstack. Recorded here so nobody re-researches it.

### Hardware test list (for 33b/33c verification)

Ordered by value, on the StickS3 plus one plain ESP32 board:

1. Soak: one Fujifilm camera connected, WiFi STA plus idle MQTT, 24 h.
   Count BLE disconnects and WiFi disconnect reasons via the console.
   Repeat with FauxNY as extra connections at 3 and 5 cameras.
2. Shutter latency distribution (p50/p95/p99/max), three arms: WiFi
   off, WiFi plus idle MQTT, MQTT burst plus a large TCP download.
   This is plan 33's six-state test compressed to the decisive arms.
3. Connection-interval A/B: 30 to 50 ms versus 51.25 to 60 ms minimum
   under arm 3, measuring missed-event bursts. Validates
   recommendation 2 on real controllers.
4. WiFi join churn: 100 direct BSSID+channel connect/disconnect cycles
   with a camera connected. Zero BLE drops expected through the
   CONNECTING extended slices.
5. DFS peripheral audit with WiFi active: GPS sentence integrity,
   backlight flicker, M5PM1 retry rate, under `WIFI_PS_MIN_MODEM` and
   `WIFI_PS_NONE`, plus current draw per mode.
6. Memory headroom: minimum free heap with max cameras plus WiFi plus
   MQTT, ESP32 board especially, watching WiFi `fail_oom` counters.
7. ESP32-only: repeat test 1 watching for controller lockups, not just
   disconnects, per the #4719 history.

## Verification plan for this document

- Docs only, no code. CI must stay green.
- Every link above was fetched and checked for the claimed content.
  Items that could not be verified are flagged inline: the esp32.com
  memory figures, nsg bond reboot-survival, the exact Bluedroid privacy
  limitation gkoh hit, Bluedroid dual-mode RAM next to LVGL, the
  #15833 patch merge status, and the third-party latency numbers.

## Dependencies

- Extends the coexistence section of `plans/33-wifi-hub.md`; its
  recommendations bind 33b and 33c.
- Confirms and depends on the analysis in `plans/60-issue-257.md`.
- Recommendation 2 interacts with `plans/10-adaptive-conn-params.md`.
- The camera-count cap feeds `plans/25-multiconnect-ui.md` hub-mode
  documentation.
