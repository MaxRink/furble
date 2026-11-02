# Wi-Fi and radio coexistence power

Lane report for `docs/power/`. Scope: the marginal energy of the Wi-Fi station
and of everything that rides on it (NTP, MQTT, WebUI, OTA), on a device whose
primary radio workload is a BLE camera link.

Base for every code read: `7383561a`. The branch has gained docs-only commits
since (this lane and the display lane); nothing under `src/`, `include/`, `lib/`
or the sdkconfigs has changed. Every source claim below was read in this
worktree except the WebUI, which is not on `master` and was read from
`origin/feat/33d-webui` at `777b4251`.

No new measurements were made for this report. Rows are labelled
`measured-local`, `measured-vendor`, `modeled`, `inferred`, or `unknown`, and the
only `measured-local` values are ones quoted from earlier plans with their
provenance intact. Nothing here is a bench result for Wi-Fi.

---

## 1. What this lane can and cannot see

Three limits shape every estimate below.

1. The energy model has no Wi-Fi term. `sim/FurbleWiFi.cpp` is a stub that
   reports `STATE_DISABLED`, and `tools/power-model/board-currents.yaml` has
   radio entries only for BLE. No committed scenario contains a station, an
   MQTT session, or an HTTP request, so no Wi-Fi change can be CI-gated today.
2. The simulator cannot exercise the paths at all. `sim/FurbleWiFi.cpp` returns
   `false` from `connect()` and `getStatus()` always reports disabled.
3. The StickS3 has no battery-current channel at all. `Platform::readBattery()`
   reads only voltage and charge status from the M5PM1, and `battery_caps.current`
   is false, so `power log` is state evidence on that board (voltage, charge
   status, modeled state), not a current trace, with or without USB attached.
   Section 8 works around this with external instruments and states the bias of
   each.

Consequence: this report can rank mechanisms and bound their direction. It
cannot rank their size. Every "estimated saving" that is not a quoted vendor
figure is an arithmetic bound with its assumptions printed next to it.

## 2. Evidence and document register

Evidence labels used in every row:

| Label | Meaning |
| --- | --- |
| `measured-local` | Measured on this project's hardware in an earlier plan. Quoted, not repeated. |
| `measured-vendor` | Published by Espressif with a stated test condition. Not reproduced here. |
| `modeled` | Produced by the simulator or the energy model. No Wi-Fi row exists. |
| `inferred` | Read from code, Kconfig help, or arithmetic with stated assumptions. |
| `unknown` | Plausible mechanism, no evidence of size. |

`measured-local` and `measured-vendor` are the two subdivisions of the `measured`
label used in `docs/power/README.md`. No row here is `measured-local` on new
evidence.

Documents cited by the rows, with the limit that matters:

| ID | Part and document | Revision and limit |
| --- | --- | --- |
| D1 | ESP32-S3 SoC; *ESP32-S3-WROOM-1 & WROOM-1U Datasheet* v1.0, Table 9, whose note states the table is excerpted from the ESP32-S3 Series Datasheet power-consumption section. The repo cites the same SoC table at `tools/power-model/board-currents.yaml:164-193`. | Peak current, 3.3 V, 25 C, 100 percent TX duty. Wi-Fi TX 802.11b 1 Mbps at 20.5 dBm 355 mA; 802.11g 54 Mbps at 18 dBm 297 mA; HT20 MCS7 at 17.5 dBm 286 mA; RX HT20 95 mA, HT40 97 mA. |
| D2 | ESP-IDF 5.5.3 Kconfig, `components/esp_wifi/Kconfig` and `components/esp_phy/Kconfig` | Help text read from the locally installed framework (3.50402.0, IDF 5.4.2) while the build pins 3.50503.0 (IDF 5.5.3). Re-check the help text at the pinned version before relying on a number. |
| D3 | ESP-IDF v5.5, *Power Management* API guide (fetched 2026-11-01) | Wi-Fi holds `ESP_PM_APB_FREQ_MAX` between `esp_wifi_start` and `esp_wifi_stop`, released while the radio is off under modem sleep. Bluetooth holds `ESP_PM_NO_LIGHT_SLEEP` for the whole time the controller is enabled, releasing only APB. |
| D4 | ESP-IDF v5.5, *RF Coexistence*, ESP32-S3 page (fetched 2026-11-01) | One 2.4 GHz RF module, time-division multiplexed. Wi-Fi CONNECTED plus BLE CONNECTED gets 50/50 slices in a period anchored to TBTT and longer than 100 ms. SCAN and CONNECTING get longer Wi-Fi slices. S3 SoftAP rows against BLE are marked unstable (C1). |
| D5 | ESP-IDF 5.4.2, `components/mqtt/esp-mqtt/include/mqtt_client.h` | `keepalive` default 120 s, client pings at half the configured interval; `reconnect_timeout_ms` default 10 s; `timeout_ms` default 10 s. |
| D6 | ESP-IDF 5.4.2, `components/esp_https_server/include/esp_https_server.h` | `HTTPD_SSL_CONFIG_DEFAULT()` sets `keep_alive_enable = false` and `max_open_sockets = 4`; `keep_alive_enable` enables TCP liveness probes (`SO_KEEPALIVE`), not HTTP connection reuse, and the header states one socket uses about 40 KB of RAM with SSL. |
| D7 | ESP-IDF 5.4.2, `components/esp_http_server/include/esp_http_server.h` | Plain HTTPD defaults: `max_open_sockets = 7`, `lru_purge_enable = false`, `keep_alive_enable = false`. The last field enables TCP liveness probes (`SO_KEEPALIVE`), not HTTP connection reuse. |
| D8 | NimBLE `ble_gap.h` | `BLE_GAP_INITIAL_CONN_ITVL_MIN` 30 ms, `_MAX` 50 ms, `BLE_GAP_INITIAL_SUPERVISION_TIMEOUT` 0x0100 units of 10 ms. |
| D9 | ESP-IDF 5.4.2, `esp_wifi_types_generic.h` | `WIFI_PS_MIN_MODEM` wakes at every DTIM; `WIFI_PS_MAX_MODEM` uses `listen_interval` (default 3 when 0). SoftAP `beacon_interval` default 100 TU, `dtim_period` default 1. |
| D10 | ESP-IDF 5.4.2, `esp_wifi.h` | `esp_wifi_set_max_tx_power` takes 0.25 dBm units: {[44,51] maps to 44}, that is 11 dBm; {[52,55] maps to 52}, that is 13 dBm. |
| D11 | Espressif low-power-mode Wi-Fi figures, quoted in `plans/33-wifi-hub.md:630-634` | Station with modem sleep, shielded box: DTIM1 40.1 mA, DTIM3 38.7 mA, DTIM10 38.2 mA. Not reproduced here. |
| D12 | `plans/00-hardware-experiments.md`, BLE floor | 3.3 mA connected light sleep on the StickS3, main XTAL. Measured locally in that plan. |

Links for the fetched documents: D3
`https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32s3/api-reference/system/power_management.html`,
D4 `https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32s3/api-guides/coexist.html`.
D1 is the Espressif ESP32-S3 datasheet PDF; the module table cited above is
`ESP32-S3-WROOM-1 & WROOM-1U Datasheet` v1.0, Table 9. D5 to D10 are the headers
inside the locally installed `framework-espidf` 3.50402.0 tree.

The relevant sdkconfig facts, all `inferred` from the committed files:

| Setting | Value | Where |
| --- | --- | --- |
| `CONFIG_ESP_COEX_SW_COEXIST_ENABLE` | `y` | every committed config |
| `CONFIG_ESP_WIFI_SLP_IRAM_OPT` | not set | every committed config |
| `CONFIG_ESP_WIFI_SLP_DEFAULT_MIN_ACTIVE_TIME` | 50 ms | `sdkconfig.m5stick-s3:2358` |
| `CONFIG_ESP_WIFI_SLP_DEFAULT_MAX_ACTIVE_TIME` | 10 s | `sdkconfig.m5stick-s3:2361` |
| `CONFIG_ESP_WIFI_SLP_DEFAULT_WAIT_BROADCAST_DATA_TIME` | 15 ms | `sdkconfig.m5stick-s3:2362` |
| `CONFIG_ESP_WIFI_STA_DISCONNECTED_PM_ENABLE` | `y` | `sdkconfig.m5stick-s3:2364` |
| `CONFIG_ESP_PHY_MAX_WIFI_TX_POWER` | 20 dBm | `sdkconfig.m5stick-s3:2083` |
| `CONFIG_ESP_PHY_MAC_BB_PD` | `y` on the two S3 hub configs, unset on `waveshare-s3-eth` | see W-14 |
| `CONFIG_ESP_PHY_REDUCE_TX_POWER` | not set | every committed config |
| `CONFIG_ESP_PHY_ENABLE_USB` | `y` on the S3 boards | `sdkconfig.m5stick-s3:2088` |
| `CONFIG_LWIP_SNTP_UPDATE_DELAY` | 3600000 | `sdkconfig.m5stick-s3:2771` |
| `CONFIG_MQTT_TRANSPORT_SSL` | `y` in the six board configs, not set in `waveshare-s3-eth` | note below |
| `CONFIG_ESP_TLS_CLIENT_SESSION_TICKETS` | not set in `sdkconfig.m5stick-s3` | `sdkconfig.m5stick-s3:1634` |

Config inconsistency worth recording: `sdkconfig.waveshare-s3-eth` builds with
`-DFURBLE_MQTT=1` (`platformio.ini:69-70`) but has both
`CONFIG_MQTT_PROTOCOL_311` and `CONFIG_MQTT_TRANSPORT_SSL` unset
(`sdkconfig.waveshare-s3-eth:2973-2975`). That is a correctness and
all-environment-consistency defect, not a power item, but it decides whether the
TLS rows apply to that node. Plan 96 item C7 describes the same class of drift in
the opposite direction.

## 3. What the code does today, in power terms

### 3.1 Boot and ownership

`esp_netif_init` and the default event loop are owned by `app_main`
(`src/main.cpp:334-335`). `WiFi::init()` runs unconditionally on every release
environment (`src/main.cpp:376`). It creates the default station netif, registers
a handler for every `WIFI_EVENT` plus `IP_EVENT_STA_GOT_IP`, loads the remembered
BSSID and channel from NVS, and creates a 4 KB task (`src/FurbleWiFi.cpp:504-530`).
The radio itself does not start unless the `WIFI` setting is true; the setting
defaults to false because a missing NVS key returns `T{}`
(`src/FurbleSettings.cpp:331-338`). So the always-on boot cost is RAM and one
blocked task, not current.

### 3.2 Station lifecycle

`startDriver()` applies `WIFI_INIT_CONFIG_DEFAULT`, `WIFI_STORAGE_RAM` (the
driver never writes its own NVS copy), `WIFI_MODE_STA`,
`esp_wifi_set_ps(WIFI_PS_MIN_MODEM)`, then `esp_wifi_start()`
(`src/FurbleWiFi.cpp:263-289`). `disconnect()` goes all the way to
`esp_wifi_deinit()` (`:291-300`, `:549-562`), which is the right shape: leaving
the driver initialized keeps the buffers and the PHY glue resident.

The worker retries with 1 s doubling to a 60 s cap and up to 25 percent jitter
(`:42-43`, `:307-326`), never gives up, and refuses to scan while
`Control::getState()` is `STATE_ACTIVE` (`:328-332`). After two direct failures
it runs one blocking active scan with `show_hidden = true` to refresh BSSID and
channel, then resumes direct connects (`:411-422`, `:328-371`).

### 3.3 Sleep posture

Nothing in `FurbleWiFi.cpp` or `FurbleMQTT.cpp` takes a power lock. The locks
that matter are taken by drivers:

- Wi-Fi takes `ESP_PM_APB_FREQ_MAX` between start and stop and releases it while
  the radio is off under modem sleep (D3). On this build the APB lock also pins
  the CPU at 80 MHz, because the configured minimum is 40 MHz
  (`include/FurblePower.h:61`) and the ESP32-S3 lock table maps APB max to
  CPU 80 MHz (D3).
- Bluetooth holds `ESP_PM_NO_LIGHT_SLEEP` for as long as the controller is
  enabled, and releases only APB under modem sleep (D3). That is why a connected
  camera already prevents light sleep unless sleep while connected is enabled,
  which the battery-saver profile only forces on the StickS3
  (`src/FurbleSettings.cpp:786-796`).
- `Control` adds its own `NO_LIGHT_SLEEP` while `STATE_ACTIVE` when sleep while
  connected is off (`src/FurbleControl.cpp:1101-1130`).

So a station does not add a lock of its own. It adds wake events: every DTIM
receive window re-acquires APB, and every application exchange extends the time
before the next tickless idle window. That is the mechanism to attack. A station
can coexist with `SLEEP_CONN` without breaking it, but it is expected to dominate
the 3.3 mA floor, which is exactly what test M6 must establish rather than
assume.

### 3.4 Coexistence posture

Coexistence is enabled everywhere. The S3 matrix puts STA plus BLE in the stable
quadrant for scan, connect and connected (D4). Connected plus connected is a
50/50 split anchored to the beacon (D4), so the beacon cadence, not just the
application traffic rate, shapes the arbitration windows that BLE shares.

BLE link parameters are the fast profile at 30 to 50 ms with latency 1 and a
5.12 s supervision timeout (`lib/furble/Camera.h:469-472`, D8), and the optional
connection saver moves to 250 to 300 ms with latency 0 when idle
(`lib/furble/Camera.h:479-502`). Twenty to thirty-three connection events per
second in the fast profile is the workload the station arbitrates against.

### 3.5 Consumers and their cadence

MQTT is compiled only into `m5stack-core2`, `m5stick-s3`, `esp32-s3-headless`
and `waveshare-s3-eth` (`platformio.ini:49`, `:55`, `:62`, `:70`). `m5stick-c`
and `m5stack-core` have the station but no MQTT, so on those two boards the only
network consumer is NTP; the console and the companion can enable the station but
do not use it. That makes W-03 most valuable exactly there.

| Consumer | Cadence in the current code | Evidence |
| --- | --- | --- |
| NTP | Start on GOT_IP, re-sync every 3600 s, stop on disconnect (`src/FurbleWiFi.cpp:170-218`, `:495-497`; `sdkconfig.m5stick-s3:2771`) | inferred |
| MQTT owner task | Wakes at least every 250 ms while the client exists (`include/FurbleMQTT.h:72`) | inferred |
| MQTT keepalive | 60 s configured, so a PINGREQ about every 30 s (`src/FurbleMQTT.cpp:599`, D5) | inferred |
| MQTT broker reconnect | Fixed 10 s, forever, handled inside esp-mqtt (`src/FurbleMQTT.cpp:602`) | inferred |
| MQTT camera state | Sampled at 1 Hz, published on change (`include/FurbleMQTT.h:73`, `src/FurbleMQTT.cpp:1350-1388`) | inferred |
| MQTT battery | Sampled at 1 Hz, published every 60 s or on charging change (`include/FurbleMQTT.h:74`, `src/FurbleMQTT.cpp:1390-1408`) | inferred |
| MQTT GPS | Published every 10 s while a fix is held (`include/FurbleMQTT.h:75`, `src/FurbleMQTT.cpp:1410-1435`) | inferred |
| MQTT discovery | Republished on every connect (`src/FurbleMQTT.cpp:770-782`, `:1658-1825`) | inferred |
| WebUI, branch only | Supervisor at 500 ms; page polls `/api/status` every 3000 ms; three requests at page load; new TLS session per request | inferred |
| OTA over MQTT | Protocol library only; no caller in `src/`, so no present energy cost (`src/FurbleOTAMQTT.cpp` is linked but unreferenced) | inferred |

There is no image or thumbnail path anywhere in the tree, over BLE or Wi-Fi.
Bulk transfer today means firmware, and the OTA-over-MQTT engine is not wired.

## 4. Opportunity catalogue

The coordinator assigns `P-xxx` identifiers when these rows are merged into
`opportunity-matrix.md`. IDs below are stable within this lane.

### A. Mode and radio on/off policy

#### W-01 Station only, never SoftAP

- **Change:** none. Keep the station posture and add a build-time guard that
  fails if `WIFI_MODE_AP` or SoftAP support appears in a release config.
- **Current behavior:** `esp_wifi_set_mode(WIFI_MODE_STA)`
  (`src/FurbleWiFi.cpp:272`); SoftAP support is compiled out on the S3
  (`# CONFIG_ESP_WIFI_SOFTAP_SUPPORT is not set`, `sdkconfig.m5stick-s3:2367`);
  plan 33 excludes SoftAP from provisioning.
- **Evidence:** `inferred` (code plus D4).
- **Estimated saving:** none to gain. An AP would add a beacon every 100 TU
  (about 102.4 ms) at DTIM 1 by default (D9) and would move the S3 out of the
  stable coexistence quadrant (D4).
- **Risk:** high if reversed. SoftAP plus BLE on the S3 is documented unstable,
  and the 4 MB image work already removed SoftAP to recover flash.
- **Effort:** none to keep; one source-contract test to enforce.
- **Required test:** a check in `tests/test_headless_source_contract.py` style
  that greps `WIFI_MODE_AP` and `ESP_WIFI_SOFTAP_SUPPORT` across every
  `sdkconfig.*`, plus a host test asserting the `set_mode` argument.
- **Citation:** `src/FurbleWiFi.cpp:272`; `sdkconfig.m5stick-s3:2367`;
  `plans/33-wifi-hub.md:551-566`;
  `esp_wifi_types_generic.h:533-535` (beacon and DTIM defaults); D4.

#### W-02 Prefer Ethernet and never keep a station on a wired node

- **Change:** on `FURBLE_ETHERNET` builds, do not auto-start the station while
  the Ethernet link holds an address, and only fall back to the station when the
  wired link drops.
- **Current behavior:** `MQTT::networkReady()` accepts any interface that is up
  with an IPv4 address (`src/FurbleMQTT.cpp:528-549`), and Wi-Fi and Ethernet are
  independent. Enabling both keeps both up and MQTT binds to whichever the stack
  prefers.
- **Evidence:** `inferred`.
- **Estimated saving:** on a Waveshare node, removes the station entirely. The
  only population figure available is the published station average (D11), which
  is not a delta and must not be quoted as one. The node is mains powered, so the
  value is coexistence and simplicity, not runtime.
- **Risk:** low. Needs a failover policy so an unplug does not silently stop the
  hub.
- **Effort:** small.
- **Required test:** boot with Ethernet up and `WIFI` true, confirm the station
  never starts; pull the cable and confirm the station joins; replug and confirm
  it stops.
- **Citation:** `src/FurbleMQTT.cpp:528-549`; `src/main.cpp:356-360`;
  `src/FurbleEthernet.cpp`.

#### W-03 Stop the station when its last consumer goes away

- **Change:** track consumers (MQTT enabled, WebUI enabled, NTP refresh pending)
  and call `WiFi::disconnect()` when the count reaches zero. Restart on demand.
- **Current behavior:** `WIFI` is a single boolean. Once true, the station runs
  until the user disables it, even if the only reason was one NTP sync. A synced
  station keeps re-syncing hourly (`CONFIG_LWIP_SNTP_UPDATE_DELAY=3600000`,
  `sdkconfig.m5stick-s3:2771`).
- **Evidence:** `inferred`.
- **Estimated saving:** the largest single lever because it removes the station
  instead of tuning it. The only population figure is D11's 40.1 mA, which is a
  whole-chip station average and not a delta; the real marginal delta is unknown
  and is what M7 measures. Do not quote the 40.1 mA as a delta.
- **Risk:** medium. `wifi status` and any LAN client lose the address; NTP
  freshness depends on the refresh policy; a wake-up path has to exist.
- **Effort:** medium (refcount, policy, settings surface, console message).
- **Required test:** host test for the count transitions; M7 runtime test;
  on-device: enable Wi-Fi with MQTT and WebUI off, confirm the driver stops after
  the sync, confirm `wifi connect` restarts it, confirm NTP re-syncs on the next
  window.
- **Citation:** `src/FurbleWiFi.cpp:527-529`, `:549-562`, `:170-218`;
  `sdkconfig.m5stick-s3:2771`.

#### W-04 Battery auto-disable policy

- **Change:** add policy hooks on top of W-03: off after N idle minutes, off
  while `Control` is active when no hub consumer exists, off below a battery
  threshold. Reuse the existing battery-saver settings pattern.
- **Current behavior:** no auto-off. Plan 98 records that no auto-off or low
  battery policy exists in the tree (`plans/98-power-optimization-audit.md:260-265`).
- **Evidence:** `inferred` (code absence) plus `plans/98`.
- **Estimated saving:** same class as W-03 for the "enabled once, forgotten"
  case. Not separately additive.
- **Risk:** medium. A surprise disconnect during a shoot is worse than the
  energy it saves, so the default has to be conservative and visible.
- **Effort:** medium.
- **Required test:** policy unit tests (the `auto_off_policy_test.cpp` pattern);
  on-device soak that the radio is off after the threshold and that a console
  command re-enables it.
- **Citation:** `plans/98-power-optimization-audit.md:260-265`;
  `include/FurbleBatterySaver.h`; `src/FurbleWiFi.cpp:613-645`.

#### W-05 Defer `WiFi::init()` to first enable

- **Change:** create the netif, handlers, task and remembered-AP load on the
  first `wifi enable`, not at boot.
- **Current behavior:** unconditional at `src/main.cpp:376`. The default netif,
  the handler registrations, a 4 KB stack task and one NVS read exist even when
  the setting is false and no MQTT or WebUI build exists.
- **Evidence:** `inferred`.
- **Estimated saving:** none in current. Frees the task stack and netif RAM
  (unmeasured exact size), which is headroom against the heap risk in
  `plans/96-wifi-bt-ble-hardening.md:332-339`.
- **Risk:** low. `wifi status` and console commands must work before init, and
  `connect()` has to initialize on demand.
- **Effort:** small.
- **Required test:** boot with `WIFI` false, run `wifi status` and `debug heap`,
  then `wifi enable` and confirm the station joins.
- **Citation:** `src/main.cpp:376`; `src/FurbleWiFi.cpp:504-530`;
  `plans/96-wifi-bt-ble-hardening.md:332-339`.

### B. Station sleep and 802.11 timers

#### W-06 `WIFI_PS_MAX_MODEM` with a bounded listen interval

- **Change:** switch the station to `WIFI_PS_MAX_MODEM` with `listen_interval`
  3 (about 307 ms of downlink latency) and make it a setting, not a default.
- **Current behavior:** `WIFI_PS_MIN_MODEM` (`src/FurbleWiFi.cpp:275`), which is
  also the driver default (D9). The station wakes at every DTIM.
- **Evidence:** `inferred` for the mechanism; the only size evidence is
  `measured-vendor` at the AP-side DTIM (D11).
- **Estimated saving:** D11 gives 40.1 mA at DTIM1 against 38.7 mA at DTIM3, a
  1.4 mA (3.5 percent) analogue. A station listen interval is not the same knob
  as an AP DTIM period, so treat this as an order-of-magnitude hint only.
- **Risk:** medium to high for this product. Every downlink waits for the next
  listen window, so MQTT commands and WebUI GETs gain up to one interval. Some
  APs age out a station that skips DTIMs.
- **Effort:** small.
- **Required test:** latency A/B at listen interval 1 and 3: p50/p95/p99 for a
  WebUI GET and for an MQTT `shutter hold`; 24 h soak for disconnects; M6 for
  energy. Reject if p99 shutter latency grows by more than 100 ms.
- **Citation:** `src/FurbleWiFi.cpp:275`; D9 (`wifi_ps_type_t`, `listen_interval`);
  D11; `plans/33-wifi-hub.md:620-641`.

#### W-07 Enable `CONFIG_ESP_WIFI_SLP_IRAM_OPT`

- **Change:** set `CONFIG_ESP_WIFI_SLP_IRAM_OPT=y` in every config that runs a
  station.
- **Current behavior:** unset in every committed config. It defaults to `y` only for
  Wi-Fi 6 capable SoCs, so the S3 and ESP32 keep it off (D2).
- **Evidence:** `inferred` from `measured-vendor` Kconfig text: "Wi-Fi power-save
  mode average current would be reduced if this option is enabled". Cost with
  `ESP_WIFI_RX_IRAM_OPT` already on is 1.3 KB of IRAM (D2).
- **Estimated saving:** unquantified by the vendor. This is the cleanest
  sdkconfig lever and the first thing to A/B.
- **Risk:** low but not zero. The option also selects
  `PM_SLP_DEFAULT_PARAMS_OPT`, `PM_SLEEP_FUNC_IN_IRAM` and
  `ESP_PERIPH_CTRL_FUNC_IN_IRAM`, which changes the light-sleep flash-cache path.
  Re-verify GPS NMEA integrity, display behaviour and the watchdog.
- **Effort:** small (one line per board config).
- **Required test:** boot plus 24 h soak on the StickS3 with `SLEEP_CONN` on and a
  camera connected; M6 A/B energy; `power stats` for lock residency.
- **Citation:** `sdkconfig.m5stick-s3:2357`; D2, `esp_wifi/Kconfig` lines for
  `ESP_WIFI_SLP_IRAM_OPT`.

#### W-08 Shorten the post-wake active window

- **Change:** lower `CONFIG_ESP_WIFI_SLP_DEFAULT_MIN_ACTIVE_TIME` from 50 ms to
  20 to 30 ms and measure.
- **Current behavior:** 50 ms minimum active time after each wake, refreshed by
  any traffic in the window (D2, `sdkconfig.m5stick-s3:2358`).
- **Evidence:** `inferred`.
- **Estimated saving:** time only, no current figure. At a DTIM1 rate of about
  9.8 wakes per second, a 30 ms reduction removes up to 0.29 s per second of
  active-window time. The current drawn inside that window is not characterised
  here, so no mA number is offered.
- **Risk:** medium. A shorter window can miss downlink frames and trigger
  retries, which cost more than the window saved.
- **Effort:** small.
- **Required test:** MQTT throughput and retry count with the window at 50 and
  at 20 ms; M6 energy; 24 h soak.
- **Citation:** `sdkconfig.m5stick-s3:2358`; D2.

#### W-09 Raise the null-data keepalive interval

- **Change:** raise `CONFIG_ESP_WIFI_SLP_DEFAULT_MAX_ACTIVE_TIME` from 10 s to
  30 s.
- **Current behavior:** the station sends a null data frame whenever nothing has
  been sent for 10 s, to keep the AP's entry alive (D2,
  `sdkconfig.m5stick-s3:2361`).
- **Evidence:** `inferred`.
- **Estimated saving:** two fewer small frames per 30 s. At D1's TX peak for
  under a millisecond each, this is far below the resolution of any instrument
  in this project. Group it with W-06 or W-07 rather than doing it alone.
- **Risk:** low. AP inactivity timers are commonly 300 s, and 30 s stays well
  inside that. Confirm on the target AP.
- **Effort:** small.
- **Required test:** 24 h soak, count station disconnects and reason codes.
- **Citation:** `sdkconfig.m5stick-s3:2361`; D2.

#### W-10 There is no station-side DTIM or beacon-interval control

- **Change:** none. Record it so nobody tries.
- **Current behavior:** DTIM period and beacon interval are AP properties. The
  station only chooses to wake at every DTIM (`MIN_MODEM`) or every
  `listen_interval` beacons (`MAX_MODEM`) (D9). A beacon interval is settable
  only in `wifi_ap_config_t`, which requires SoftAP (D9).
- **Evidence:** `inferred` from D9.
- **Estimated saving:** none; there is no knob.
- **Risk:** attempting it wastes effort or pushes the firmware into SoftAP.
- **Effort:** none.
- **Required test:** none.
- **Citation:** `esp_wifi_types_generic.h:374-378`, `:533-535`, `:558`.

### C. Transmit power

#### W-11 Cap Wi-Fi TX power with a weak-signal restore

- **Change:** call `esp_wifi_set_max_tx_power(44)` (11 dBm) or `52` (13 dBm) when
  the station RSSI is comfortably above a threshold, and return to the 20 dBm
  ceiling when it is not. Make the cap a setting.
- **Current behavior:** `CONFIG_ESP_PHY_MAX_WIFI_TX_POWER=20` on every config
  (`sdkconfig.m5stick-s3:2083`). No code calls `esp_wifi_set_max_tx_power`
  anywhere in `src/`, `include/` or `lib/`.
- **Evidence:** `inferred` from the datasheet peak table (D1) and the mapping
  table (D10).
- **Estimated saving:** small and unmeasured. D1 gives 355 mA at 20.5 dBm for
  100 percent duty, but a modem-sleep station transmits for a few percent of the
  time, so the average saving is a fraction of a milliamp. The credible wins are
  peak current, brownout margin and thermal headroom, not runtime.
- **Risk:** medium to high. Lower TX power can lower the selected rate and
  increase retries, which can cost more energy than the PA saves. A fixed cap
  without an RSSI restore is the dangerous version.
- **Effort:** small to medium (setting plus policy plus restore path).
- **Required test:** 24 h soak at the far edge of the AP's range with RSSI,
  retry count and MQTT publish success recorded; M3 energy; brownout soak with a
  weak supply. Reject if retries rise more than 20 percent.
- **Citation:** `sdkconfig.m5stick-s3:2083`; `esp_wifi.h:1127-1137`; D1; D10.

#### W-12 Enable `CONFIG_ESP_PHY_REDUCE_TX_POWER` on battery boards

- **Change:** `CONFIG_ESP_PHY_REDUCE_TX_POWER=y` where a brownout detector exists.
- **Current behavior:** unset in every committed config.
- **Evidence:** `inferred` from D2.
- **Estimated saving:** none directly. It reduces PHY TX power after a brownout
  reset so the device keeps running instead of resetting in a loop. A reset loop
  costs far more energy than the power it saves.
- **Risk:** low. It only acts after a brownout event.
- **Effort:** trivial.
- **Required test:** induced brownout on a weak supply, then confirm the device
  stays up and the station reconnects.
- **Citation:** D2, `esp_phy/Kconfig` (`ESP_PHY_REDUCE_TX_POWER`).

#### W-13 Disable the USB PHY during Wi-Fi init on battery hub builds

- **Change:** `CONFIG_ESP_PHY_ENABLE_USB=n` for a battery hub profile.
- **Current behavior:** `y` on the S3 boards (`sdkconfig.m5stick-s3:2088`). The
  Kconfig text states that keeping the USB PHY enabled "can interfere with WiFi
  thus lowering WiFi performance", and that disabling it means the USB PHY
  cannot be used while Wi-Fi is enabled (D2).
- **Evidence:** `inferred`.
- **Estimated saving:** unquantified. It removes the USB PHY from the Wi-Fi-on
  state and removes the documented Wi-Fi performance penalty, which should show
  up as fewer retries.
- **Risk:** high for development. Losing the USB console while Wi-Fi is up also
  removes `power log`, which is the instrument for every other row. Only usable
  in a release battery profile.
- **Effort:** small.
- **Required test:** build plus boot with Wi-Fi on a battery profile; confirm the
  station joins and that the station is reachable over the network; M3 energy and
  a retry comparison against the current build.
- **Citation:** `sdkconfig.m5stick-s3:2088`; D2, `esp_phy/Kconfig`
  (`ESP_PHY_ENABLE_USB`).

#### W-14 Set `CONFIG_ESP_PHY_MAC_BB_PD` on the Waveshare config

- **Change:** align `sdkconfig.waveshare-s3-eth` with the two other S3 configs.
- **Current behavior:** set on `sdkconfig.m5stick-s3:2086` and on
  `sdkconfig.esp32-s3-headless`; unset on `sdkconfig.waveshare-s3-eth`.
- **Evidence:** `inferred`.
- **Estimated saving:** applies only when the PHY is fully disabled, meaning both
  radios idle, so it is worth little on a mains Ethernet node. It is primarily a
  consistency fix, and it costs about 5.3 KB of RAM when Wi-Fi and Bluetooth are
  both enabled (D2).
- **Risk:** low.
- **Effort:** trivial.
- **Required test:** build, boot, Ethernet link, 24 h soak.
- **Citation:** D2, `esp_phy/Kconfig` (`ESP_PHY_MAC_BB_PD`); the three configs.

### D. Coexistence with BLE

#### W-15 Broaden the scan refusal to every non-idle Control state

- **Change:** refuse the fallback scan for `STATE_CONNECT`, `STATE_CONNECTING`,
  `STATE_DISCONNECTING` and an in-flight BLE discovery scan, not only
  `STATE_ACTIVE`. If a scan can ever overlap a link, set
  `wifi_scan_config_t.coex_background_scan = true`.
- **Current behavior:** only `STATE_ACTIVE` is refused
  (`src/FurbleWiFi.cpp:328-332`). A BLE connect attempt can hold a scan for up to
  60 s in the FujifilmSecure rescan path (`lib/furble/FujifilmSecure.cpp:165-174`),
  and a Wi-Fi active scan can run inside that window.
- **Evidence:** `inferred` from code plus D4 (SCAN and CONNECTING take extended
  Wi-Fi slices).
- **Estimated saving:** none in steady state. It avoids the worst arbitration
  window, where BLE loses connection events and a retry costs radio time.
- **Risk:** low, provided W-16 lands with it.
- **Effort:** small.
- **Required test:** unit test the guard predicate against every state; on-device,
  start a camera connect and a `wifi connect` together and confirm the refusal
  log with no BLE drop.
- **Citation:** `src/FurbleWiFi.cpp:328-332`; `include/FurbleControl.h:33-43`;
  `lib/furble/FujifilmSecure.cpp:165-174`;
  `esp_wifi_types_generic.h:249`; D4.

#### W-16 Preserve a refused join scan until it can run

- **Change:** set `g_ScanAttempted` only after `scanForNetwork()` actually runs.
  A refusal from the Control-state gate must leave the fallback scan available
  so a later idle retry can use it.
- **Current behavior:** the worker sets `g_ScanAttempted = true` before it calls
  `scanForNetwork()` (`src/FurbleWiFi.cpp:512-523`). A refused scan therefore
  consumes the only fallback scan for that failure streak. The worker is not
  parked: it still calls `connectAttempt()` and receives the same disconnect
  notifications (`src/FurbleWiFi.cpp:525-533`). The defect is the lost scan, not
  a missing notification.
- **Evidence:** `inferred` (code).
- **Estimated saving:** none directly. It avoids a failure streak with no
  fallback scan after one coexistence refusal.
- **Risk:** low.
- **Effort:** small.
- **Required test:** host test with `tests/host/wifi/real_wifi_test.cpp`; force a
  refusal, return Control to idle, and confirm one scan runs. On-device: block a
  scan with a camera connected, disconnect the camera, and confirm the station
  joins without a manual command.
- **Citation:** `src/FurbleWiFi.cpp:503-533`;
  `plans/96-wifi-bt-ble-hardening.md:286-291`.

#### W-17 Wi-Fi-aware BLE connection interval floor

- **Change:** while the station is connected, select a BLE fast profile with a
  51.25 to 60 ms minimum interval, avoiding multiples of the 102.4 ms beacon,
  and restore 30 to 50 ms when Wi-Fi drops.
- **Current behavior:** 30 to 50 ms with latency 1 and 5.12 s supervision
  (`lib/furble/Camera.h:469-472`, D8), which sits below the controller's
  always-high-priority threshold described in `plans/65-bt-coexistence.md:322-332`.
- **Evidence:** `inferred` from D4 plus the published issue guidance quoted in
  plan 65.
- **Estimated saving:** connection events per second drop from 20 to 33 down to
  about 16 to 19 while the fast profile is active, an approximately 17 to 42
  percent cut in BLE event rate for that window. The fast profile is not the idle
  state, so the daily average effect is smaller and unmeasured.
- **Risk:** medium. Adds up to one interval of shutter latency and needs the A/B
  that plan 65 already specifies.
- **Effort:** small to medium (plumb the Wi-Fi state into profile selection).
- **Required test:** plan 65 test 3 (shutter latency p50/p95/p99 in three arms)
  and missed-event counts; soak at 3 cameras.
- **Citation:** `lib/furble/Camera.h:469-502`;
  `plans/65-bt-coexistence.md:416-422`, `:450-472`; D4; D8.

#### W-18 Do not step BLE TX down while Wi-Fi is up

- **Change:** floor the adaptive TX range one step higher, or suspend step-down,
  while the station is connected.
- **Current behavior:** the adaptive policy steps through P3, P6 and P9 on RSSI
  hysteresis (`src/FurbleControl.cpp:30-33`, `:1136-1306`). Camera-side RSSI is
  measured on the camera's transmissions, so it does not see Wi-Fi bursts on the
  shared front end.
- **Evidence:** `inferred`, matching `plans/96-wifi-bt-ble-hardening.md:360-364`
  (item D2).
- **Estimated saving:** negative. This deliberately spends BLE TX power to avoid
  a retry or a supervision timeout, whose recovery cost is not quantified.
- **Risk:** low.
- **Effort:** small (a two-line condition in `sampleAdaptivePower`).
- **Required test:** 24 h soak with Wi-Fi plus MQTT traffic at the far edge of
  range, counting supervision timeouts with and without the floor.
- **Citation:** `src/FurbleControl.cpp:1136-1306`;
  `plans/96-wifi-bt-ble-hardening.md:360-364`.

#### W-19 Cap hub-mode camera count

- **Change:** document 3 cameras as supported and 5 as tested in hub mode, and
  optionally refuse a sixth connect while the station is up.
- **Current behavior:** `CONFIG_BT_NIMBLE_MAX_CONNECTIONS=9`
  (`sdkconfig.m5stick-s3:1159`) with no hub-mode cap in code or docs.
- **Evidence:** `inferred` from the ESPHome field data recorded in
  `plans/65-bt-coexistence.md:345-362`.
- **Estimated saving:** none per camera. It prevents the retry-heavy regime where
  nine links share the radio with a station, where retries and arbitration losses
  can make energy per delivered byte worse as the camera count rises.
- **Risk:** product restriction. Users who run five cameras will notice.
- **Effort:** small for the doc, medium for enforcement.
- **Required test:** soak with 1, 3 and 5 cameras plus MQTT and compare
  disconnects and p99 shutter latency.
- **Citation:** `sdkconfig.m5stick-s3:1159`;
  `plans/65-bt-coexistence.md:345-362`, `:423-426`.

### E. Scan cost

#### W-20 Stop requesting hidden SSIDs in the fallback scan (implemented)

- **Change:** set `wifi_scan_config_t.show_hidden = false` for the fallback scan.
  Implemented as P-029.
- **Current behavior:** `show_hidden = false` (`src/FurbleWiFi.cpp:336`).
- **Evidence:** `inferred` from `esp_wifi_types_generic.h:242`.
- **Estimated saving:** none established. `show_hidden` controls whether the
  scan result list includes hidden SSIDs; it does not select active probing or
  the per-channel dwell, so setting it false does not establish a probe-traffic
  saving. The earlier claim is withdrawn. Probe cost needs an airtime capture
  and belongs to W-21.
- **Risk:** low. A user with a genuinely hidden SSID would lose the fallback; the
  direct BSSID and channel connect still works after the first join, and the
  first join can be served by the driver's own scan when the BSSID is not stored.
- **Effort:** trivial.
- **Required test:** join a normal AP and a hidden AP from a clean NVS; confirm
  the first join still succeeds.
- **Citation:** `src/FurbleWiFi.cpp:335-337`;
  `esp_wifi_types_generic.h:238-250`.

#### W-21 Passive or bounded-dwell fallback scan

- **Change:** make the fallback scan passive with a bounded dwell, or active with
  an explicit `min`/`max` of 60 to 120 ms per channel instead of driver defaults.
- **Current behavior:** active scan with no `scan_time` set, so driver defaults
  apply; the struct is zero-initialised at `src/FurbleWiFi.cpp:335`.
- **Evidence:** `inferred` from D9's scan structure and D4's scan slice rule.
- **Estimated saving:** removes probe TX on 13 channels and bounds the total scan
  window. Unmeasured; like W-20 this is a one-time cost per join cycle, not a
  steady-state saving.
- **Risk:** medium. Passive scan cannot discover a hidden SSID and takes longer
  per channel; a longer scan holds the extended Wi-Fi coexistence slice longer.
- **Effort:** small.
- **Required test:** time to join on three APs including one hidden; M10 scan
  energy; confirm no BLE event loss during the scan.
- **Citation:** `src/FurbleWiFi.cpp:334-341`;
  `esp_wifi_types_generic.h:212-250`; D4.

#### W-22 Prefer the driver's scan-and-join over an explicit scan

- **Change:** after two direct failures, clear the remembered BSSID so
  `esp_wifi_connect()` performs its own scan and join, instead of running an
  explicit scan and then a direct connect.
- **Current behavior:** the explicit scan refreshes BSSID and channel, then
  `connectAttempt()` re-applies the config and calls `esp_wifi_connect()`
  (`src/FurbleWiFi.cpp:411-427`, `:373-395`). When the scan fails to find the
  SSID, the stale BSSID is kept and the connect is a direct attempt to the old
  channel.
- **Evidence:** `inferred`.
- **Estimated saving:** removes one scan pass in the recovery path. Unmeasured;
  the goal is fewer channel sweeps per recovery, not a steady-state saving.
- **Risk:** low. Behaviour is still "find the AP and join"; the difference is who
  runs the scan.
- **Effort:** small.
- **Required test:** move an AP to a new channel, then confirm the recovery time
  and the number of channel sweeps from logs; confirm no regression on a normal
  join.
- **Citation:** `src/FurbleWiFi.cpp:373-435`; D9.

### F. Retry policy

#### W-23 Classify disconnect reasons and bound retries (implemented)

- **Change:** pass the `wifi_event_sta_disconnected_t` reason into the handler.
  Stop on a confirmed authentication error, allow six attempts for no-AP
  reasons, and allow ten attempts for other transient failures. `wifi connect`
  clears the stop. Implemented as P-028.
- **Current behavior:** `classifyDisconnect()` maps `WIFI_REASON_AUTH_FAIL` to
  `AUTH`, the no-AP reasons to `NO_AP`, and everything else, including handshake
  timeouts, to `TRANSIENT` (`src/FurbleWiFi.cpp:105-125`). The handler stops
  after one `AUTH`, after `NO_AP_MAX_ATTEMPTS` (6), or after
  `TRANSIENT_MAX_ATTEMPTS` (10), with capped backoff for the retries
  (`src/FurbleWiFi.cpp:552-600`). `NO_AP_FOUND` is therefore not treated as a
  permanent authentication failure, so an ordinary AP outage recovers without a
  manual command.
- **Evidence:** `measured-local` behavior test; the energy figure remains
  `inferred`.
- **Estimated saving:** ends an unbounded retry loop in the failure case.
  Estimate with stated assumptions: after backoff reaches its cap, one direct
  connect attempt per 60 s lasting a second or two, mostly receive at D1's
  95 mA plus a TX burst, is roughly 2 to 5 percent duty, so order 1 to 5 mA
  average while the failure persists. That is an arithmetic bound, not a
  measurement.
- **Risk:** low. Bounded retries cover an AP restart or a transient handshake
  timeout; only a repeatedly confirmed credential error stops the station.
- **Effort:** small.
- **Required test:** host test for the reason mapping; on-device with a wrong
  passphrase, confirm the station stops after the bound and `wifi status`
  explains why; then fix the passphrase and confirm `wifi connect` succeeds.
- **Citation:** `src/FurbleWiFi.cpp:105-125`, `:552-600`;
  `plans/96-wifi-bt-ble-hardening.md:340-343`.

#### W-24 Slow retries while a camera is active

- **Change:** while `Control` is active, raise the retry cap from 60 s to 300 s,
  or pause attempts until the camera session ends.
- **Current behavior:** 1 s doubling to a 60 s cap, forever, with jitter
  (`src/FurbleWiFi.cpp:42-43`, `:307-326`). A studio shoot sees a join attempt
  about every minute.
- **Evidence:** `inferred` from D4's CONNECTING slice rule.
- **Estimated saving:** fewer extended Wi-Fi slices during a session. Unmeasured;
  the value is BLE latency stability more than milliamp-hours.
- **Risk:** low. Reconnect after a router reboot gets slower during a shoot,
  which is the intended trade.
- **Effort:** small.
- **Required test:** M5 latency run with a router reboot; confirm the camera link
  does not drop and that the station returns within the new cap.
- **Citation:** `src/FurbleWiFi.cpp:42-43`, `:307-326`; D4.

#### W-25 Stop and reinitialize the driver after a long failure streak

- **Change:** after N minutes of failures, `esp_wifi_stop()` and retry by
  restarting the driver on the backoff schedule instead of holding it started.
- **Current behavior:** the driver stays initialized and started while
  disconnected. `CONFIG_ESP_WIFI_STA_DISCONNECTED_PM_ENABLE=y`
  (`sdkconfig.m5stick-s3:2364`) already lets it modem-sleep between attempts, so
  the idle cost is not the full station average.
- **Evidence:** `inferred`.
- **Estimated saving:** unquantified, and smaller than it looks because the
  disconnected power save is already on. The saving is the periodic connect
  attempt and the retained buffers.
- **Risk:** medium. `esp_wifi_deinit` cycling has a known suspicion about the
  default netif glue not being detached (`plans/96-wifi-bt-ble-hardening.md:344`),
  and re-init costs heap churn.
- **Effort:** medium.
- **Required test:** 30 min with no AP in range, compare M10 energy for the two
  policies; repeat 20 enable/disable cycles and confirm heap does not leak.
- **Citation:** `src/FurbleWiFi.cpp:291-305`, `:397-435`;
  `plans/96-wifi-bt-ble-hardening.md:340-351`.

### G. MQTT

#### W-26 Adaptive MQTT owner-task period

- **Change:** replace the fixed 250 ms wait with a wait bounded by the next due
  deadline (camera sample 1 s, GPS 10 s, battery publish 60 s, hold or interval
  deadline, offline ack deadline). Incoming events already notify the task, so
  command latency is unaffected.
- **Current behavior:** `TASK_PERIOD_MS = 250` (`include/FurbleMQTT.h:72`). The
  task wakes at least four times a second while enabled, and each wake at
  `src/FurbleMQTT.cpp:305-309` runs the settings lookups in W-27.
- **Evidence:** `inferred`.
- **Estimated saving:** estimate only: three fewer wakes per second, each with a
  sleep exit and entry. At a nominal 1 ms exit at D1-class currents that is order
  0.1 to 0.2 mA, but it is below the resolution of the built-in gauge and must be
  measured with an external analyzer (M3).
- **Risk:** low if the deadline maths is right. Camera state changes could be up
  to one period late.
- **Effort:** small.
- **Required test:** extend `tests/host/mqtt_test.cpp` to count owner
  notifications and publishes over simulated time and assert the cadence is
  unchanged; M3 energy with the analyser.
- **Citation:** `include/FurbleMQTT.h:72-75`; `src/FurbleMQTT.cpp:305-334`.

#### W-27 Cache settings in the MQTT loop

- **Change:** read `MQTT`, `MQTT_URI`, `MQTT_USER`, `MQTT_PASS`, `MQTT_BASE` and
  `GPS` once per `reloadSetting()`, not on every task iteration.
- **Current behavior:** every `taskStep()` calls `Settings::load<MQTT>()` and
  then `isConfigured()`, which loads five more settings
  (`src/FurbleMQTT.cpp:395-424`, `:293-301`). Each `Settings::load` opens
  Preferences and copies from the NVS cache (`src/FurbleSettings.cpp:331-338`).
  At 250 ms intervals that is about 24 lookups per second on an idle hub.
- **Evidence:** `inferred`.
- **Estimated saving:** CPU time inside the wake window, unmeasured. It makes
  W-26 more effective.
- **Risk:** low. Every MQTT setting write already calls `reloadSetting()`
  (`src/FurbleCompanionService.cpp:1107-1114`), and so does the console.
- **Effort:** small.
- **Required test:** existing `tests/host/mqtt_test.cpp` reload tests must still
  pass; add a case that changes a setting without reload and asserts the old
  client is kept, documenting the intended contract.
- **Citation:** `src/FurbleMQTT.cpp:293-301`, `:395-424`;
  `src/FurbleSettings.cpp:331-338`.

#### W-28 Exponential backoff for broker reconnects

- **Change:** disable esp-mqtt auto-reconnect and drive reconnects from the owner
  task with 10 s doubling to a 300 s cap, reset on success. This is also the
  shape plan 96 item C5 asks for.
- **Current behavior:** `reconnect_timeout_ms = 10000` fixed, forever
  (`src/FurbleMQTT.cpp:602`). Every attempt is a fresh TCP connect and a full TLS
  handshake against the CA bundle.
- **Evidence:** `inferred` from D5 and the client configuration.
- **Estimated saving:** during a broker outage, attempts drop from six per minute
  to about one every five minutes. Each attempt costs a TLS handshake and several
  kilobytes of radio traffic, so the reduction is large in relative terms; the
  absolute figure depends on how long the broker is down and is unmeasured.
- **Risk:** low. LWT-based availability already reports the outage; a longer
  backoff only delays recovery after the broker returns.
- **Effort:** small to medium.
- **Required test:** M8 with the broker down for 30 min, then up; confirm
  reconnect within the cap and measure energy; confirm HA sees the device return.
- **Citation:** `src/FurbleMQTT.cpp:588-640`; D5;
  `plans/96-wifi-bt-ble-hardening.md:304-308`.

#### W-29 Relax the GPS publish cadence

- **Change:** publish GPS every 30 s instead of 10 s when no WebUI client is
  attached, or make it a setting.
- **Current behavior:** 10 s gate, non-retained QoS 0
  (`include/FurbleMQTT.h:75`, `src/FurbleMQTT.cpp:1410-1435`).
- **Evidence:** `inferred`.
- **Estimated saving:** three times fewer small publishes, so three times fewer
  forced awake windows for that traffic. Unmeasured; small next to the DTIM rate.
- **Risk:** low, but it changes dashboard freshness and is a product decision.
- **Effort:** trivial.
- **Required test:** M3 with 10 s and 30 s cadence; confirm HA history still
  looks acceptable to the maintainer.
- **Citation:** `include/FurbleMQTT.h:75`; `src/FurbleMQTT.cpp:1410-1435`.

### H. WebUI (branch `feat/33d-webui`, not on master)

Every row here describes `origin/feat/33d-webui` at `777b4251`. None of it
exists on `master`, so none of it is a present power cost.

#### W-30 Establish WebUI connection reuse before changing HTTPD keepalive

- **Change:** do not use `config.httpd.keep_alive_enable` as an HTTP
  connection-reuse switch. That field enables TCP liveness probes
  (`SO_KEEPALIVE`), not HTTP keep-alive. Capture whether the HTTPS server already
  reuses connections, then review `max_open_sockets` separately.
- **Current behavior:** `HTTPD_SSL_CONFIG_DEFAULT()` leaves `keep_alive_enable`
  false and sets `max_open_sockets` to 4 (`src/FurbleWebUI.cpp:452-463`, D6). The
  page polls every 3 s (`src/FurbleWebUI.cpp:160`). Whether each poll opens a new
  TCP connection and runs a full TLS handshake is not established.
- **Evidence:** `inferred` from the header and code; connection reuse is
  `unknown`.
- **Estimated saving:** unknown. The earlier 20-reconnects-to-one and 2 to 3 mA
  figures assumed the flag controls HTTP reuse and are withdrawn. Measure new
  TCP connections, TLS handshakes, and sockets per poll in M4 before claiming a
  saving.
- **Risk:** low for the measurement; changing socket counts without a capture can
  starve requests. The SSL default is four sockets (D6) and a browser opens up to
  six connections. `lru_purge_enable` is already true in the SSL default.
- **Effort:** small for the capture, medium to act on it.
- **Required test:** capture a visible tab for 30 min: count new TCP connections,
  TLS handshakes, and concurrent sockets. Then A/B `max_open_sockets` and any
  reuse change, confirming no failed requests and no heap exhaustion.
- **Citation:** `origin/feat/33d-webui:src/FurbleWebUI.cpp:160`, `:452-463`;
  D6; D7.

#### W-31 Poll less often, only when visible, and in one request

- **Change:** replace `setInterval(refresh, 3000)` with a 10 to 15 s poll gated
  on `document.visibilityState`, and fold the initial `refresh()`, `cameras()`
  and `settings()` calls into one combined `/api/state` request.
- **Current behavior:** three TLS requests at page load and a status request
  every 3 s for as long as the tab exists
  (`origin/feat/33d-webui:src/FurbleWebUI.cpp:152-160`). Background tabs are
  throttled by the browser, but the code does not rely on it.
- **Evidence:** `inferred`.
- **Estimated saving:** three to five times fewer requests while visible, and
  one instead of three at load. Combines with W-30; not additive beyond it.
- **Risk:** low. Stale status for up to 15 s.
- **Effort:** trivial.
- **Required test:** browser test that hidden tabs stop polling, visible tabs
  refresh, and the combined endpoint returns the same fields.
- **Citation:** `origin/feat/33d-webui:src/FurbleWebUI.cpp:152-160`.

#### W-32 Stop the server when idle and slow the supervisor

- **Change:** stop the HTTPS server after N minutes without a request, restart it
  on the next accepted connection attempt or on an explicit enable, and make the
  500 ms supervisor period adaptive.
- **Current behavior:** the supervisor wakes every 500 ms, loads `WEB_UI` and
  walks the netif list on every tick, and the server runs for as long as the
  setting is on and the network is up (`include/FurbleWebUI.h:42`,
  `origin/feat/33d-webui:src/FurbleWebUI.cpp:183-215`).
- **Evidence:** `inferred`.
- **Estimated saving:** removes two wakes per second and the per-tick settings
  and netif work; stopping the server does not itself stop the station. Small.
- **Risk:** medium for usability. A stopped server cannot be woken by a browser
  unless something keeps listening, so this only makes sense with an idle timer
  that is longer than a dashboard refresh interval.
- **Effort:** medium.
- **Required test:** M4 idle versus active; confirm the server restarts on the
  next request path chosen.
- **Citation:** `origin/feat/33d-webui:src/FurbleWebUI.cpp:183-215`;
  `origin/feat/33d-webui:include/FurbleWebUI.h:42`.

### I. Bulk transfer

#### W-33 Images: no path today, use a Wi-Fi window if one is ever added

- **Change:** none now. If image transfer is ever added, make it an explicit,
  user-initiated, camera-connected-only window over Wi-Fi, with BLE used only for
  the trigger and the metadata.
- **Current behavior:** no image, thumbnail or camera preview transfer code
  exists in `src/`, `include/` or `lib/`. MQTT is documented as a control channel
  only (`plans/33-wifi-hub.md:813-814`).
- **Evidence:** `inferred` from code absence.
- **Estimated saving:** not applicable today. Arithmetic for a future design: a
  5 MB JPEG at 802.11n HT20 MCS7 (D1 286 mA TX) is seconds of radio; the same
  file over BLE at 30 to 50 ms intervals and a few hundred bytes per event is
  minutes of radio at the 176 mA BLE TX peak recorded in
  `tools/power-model/board-currents.yaml:164-193`. The per-byte radio energy
  strongly favors Wi-Fi for anything larger than a thumbnail, but the fixed cost
  of bringing the station up dominates small files. No measurement exists for
  either path.
- **Risk:** high if added carelessly. A transfer saturates the radio and is the
  worst possible neighbor for a camera link, which is why plan 33 forbids OTA
  while connected.
- **Effort:** large (new feature).
- **Required test:** if implemented, a latency A/B for the camera link during a
  transfer, plus energy per delivered byte for both transports.
- **Citation:** `plans/33-wifi-hub.md:813-814`; D1; D8.

#### W-34 Keep OTA off the camera session before the engine is wired

- **Change:** when the OTA-over-MQTT engine is wired, gate it on
  `Control::getState() == STATE_IDLE` and on a minimum battery level, and make it
  a hard refusal rather than a warning.
- **Current behavior:** `FurbleOTAMQTT.cpp` is linked but has no caller in
  `src/`, and it has no Control-state check. The "no OTA while connected" rule is
  documentation only (`plans/33-wifi-hub.md:1366-1368`).
- **Evidence:** `inferred`.
- **Estimated saving:** none today. Prevents the single largest avoidable radio
  burst from overlapping a camera session.
- **Risk:** low to add; high to omit.
- **Effort:** small (a guard at the call site).
- **Required test:** host test that an OTA manifest is rejected while a fake
  Control reports `ACTIVE`; on-device, attempt an OTA with a camera connected and
  confirm the refusal.
- **Citation:** `src/FurbleOTAMQTT.cpp`; `plans/33-wifi-hub.md:1366-1368`;
  `plans/65-bt-coexistence.md:427-429`.

## 5. Ranked safe now

These have a certain mechanism, a bounded or absent energy claim, and no
measurement is needed to accept them. Order is by value per unit of risk.

1. **W-15 with W-16, refuse scans in every non-idle state and un-park the
   worker.** Correctness fix with a coexistence benefit. Ship as one change
   because W-15 alone can strand the station.
2. **W-23, stop retrying on authentication failures.** Ends an unbounded retry
   loop and makes `wifi status` honest. The arithmetic bound is a few milliamps
   forever in the failure case.
3. **W-20, stop scanning for hidden SSIDs.** One line, removes probe traffic.
4. **W-27 + W-26, cache settings and lengthen the MQTT idle wait.** Behaviour
   preserving once the deadlines are bounded, and it shortens every wake.
5. **W-31, poll less and combine the WebUI requests (branch).** Trivial, large
   relative reduction in WebUI traffic.
6. **W-02, prefer Ethernet on wired nodes.** Removes the station on the
   Waveshare entirely.
7. **W-24, slow retries while a camera is active.** Protects BLE latency during
   a shoot.
8. **W-05, defer Wi-Fi init.** Heap headroom, no radio change.
9. **W-14, align `MAC_BB_PD` on the Waveshare config.**
10. **W-12, enable brownout TX reduction.** Safety, not savings.

## 6. Ranked needs measurement

Order is expected value, not confidence. None of these should change a default
before the named test.

1. **W-03, stop the station when its last consumer goes.** Largest possible
   lever; needs a runtime test to justify the policy and to prove the delta is
   not just the published whole-chip figure.
2. **W-06, `WIFI_PS_MAX_MODEM` with listen interval 3.** The only station-side
   sleep knob with a published analogue; blocked on a downlink latency test.
3. **W-07, `ESP_WIFI_SLP_IRAM_OPT`.** Cleanest sdkconfig win, vendor states a
   reduction, cost is 1.3 KB IRAM.
4. **W-30, WebUI connection reuse (branch).** Needs a connection capture before
   any handshake saving; then a socket-count and heap check.
5. **W-11, TX power cap with restore.** Real peak-current and brownout value,
   small average value, and a genuine retry risk.
6. **W-17, Wi-Fi-aware BLE interval.** Cuts BLE event rate under coexistence and
   raises the always-high-priority chance; needs the latency A/B.
7. **W-28, broker reconnect backoff.** Large relative reduction during an
   outage; depends on the outage duration for the absolute number.
8. **W-08, shorter post-wake active window.** Plausible per-wake saving, real
   miss risk.
9. **W-21 and W-22, scan shape and scan ownership.** Fewer channel sweeps per
   recovery.
10. **W-25, stop and reinit the driver after a long failure streak.** Smaller
    than it looks because disconnected power save is already on.
11. **W-32, idle-stop the WebUI server (branch).** Small energy, usability risk.
12. **W-29, GPS cadence.** Product decision first, measurement second.
13. **W-13, USB PHY off on battery hub builds.** Real penalty removed, but it
    removes the instrument.
14. **W-18 and W-19, BLE TX floor and camera cap.** Spend energy to protect
    latency and stability; measure disconnects, not milliamp-hours.
15. **W-09, null-data keepalive.** Group with W-06 and do not chase it alone.
16. **W-33 and W-34, bulk transfer.** Design rules today, measurements only if
    the features are built.

## 7. Rejected

Ordered roughly by how close each item is to being worth revisiting. None of
these should be retried without new evidence.

| Item | Why rejected |
| --- | --- |
| SoftAP or hotspot mode for provisioning or control (W-01) | A beacon every 100 TU at DTIM 1 by default (D9), no modem sleep for the AP role, and the S3 SoftAP rows against BLE are documented unstable (D4). Provisioning is already console and companion based. |
| Setting the DTIM period or beacon interval from firmware (W-10) | Neither is a station property. `dtim_period` and `beacon_interval` live in `wifi_ap_config_t` only (D9). |
| Raising MQTT keepalive from 60 s to 300 s | The client pings at half the interval (D5), so this removes one two-packet exchange per 30 s, which is far below measurement resolution, while pushing LWT detection from about 90 s to about 450 s. Keep 60 s, as `plans/65-bt-coexistence.md:401-410` already argues. |
| Removing TLS from the WebUI to save energy | Violates the lane rule that a change weakening security is rejected. The device already ships a per-device P-256 certificate and Basic auth over TLS. |
| TLS session tickets as a WebUI power fix | `httpd_ssl_config_t` does not expose a session-ticket context in the installed IDF (D6), so there is no supported path through `httpd_ssl_start`. Without keep-alive the resumption would still be per new connection. Revisit only if the API grows and W-30 lands first. |
| `WIFI_PS_NONE` for reliability | Holds the APB lock continuously, pins DFS at 80 MHz, and removes every sleep window (D3). It converts a latency problem into a several-fold energy problem. |
| Pinning BLE channels away from the AP channel | Not exposed by NimBLE, and internal coexistence is a time-division problem on one radio (D4), not a channel-separation problem. |
| Disabling the DHCP ARP check for power | Saves about one ARP round trip per join (`CONFIG_LWIP_DHCP_DOES_ARP_CHECK=y`, `sdkconfig.m5stick-s3:2666`), while removing duplicate-address detection. Not worth the correctness loss. |
| Lowering the CPU minimum below 40 MHz for Wi-Fi builds | The APB lock forces CPU to 80 MHz whenever it is held (D3), so the station would mask any gain while it is up. Also out of scope per plan 01. |
| Disabling Wi-Fi entirely whenever a camera is connected | Defeats hub mode, which is the only reason the station exists on a hub build. The exception is W-03, where there is no hub consumer. |
| Using 5 GHz to escape the shared band | The ESP32-S3 radio is 2.4 GHz only (D4 and the product family). |
| Background or automatic image sync over BLE | No image path exists, and per-byte radio energy over BLE is far worse than over Wi-Fi (W-33). A background sync would also defeat `CONN_SAVER`. |

## 8. Measurement plan

This lane adds no measurements. The plan below is what the coordinator needs to
promote a row from `unknown` or `inferred` to `measured-local`.

### 8.1 Instruments and their bias

| Instrument | What it gives | Known bias |
| --- | --- | --- |
| Inline USB power analyzer on the USB-C input, battery present | Absolute input power at 1 sample per second or better | Includes charger and USB PHY; measures VBUS, not battery. Valid only as an A/B between two firmware arms measured the same way. |
| `power log <seconds>` over the USB console | PMIC voltage, level, current, EWMA, runtime estimate, CPU frequency per sample | Invalid while charging, and USB is the only console, so it cannot measure a battery-only run. Use for state and timing correlation, not for absolute battery current. |
| `power stats` and `esp_pm_dump_locks` | Lock residency, including the Wi-Fi APB lock and the Bluetooth no-light-sleep lock | Attribution only, no energy. |
| Battery runtime to shutdown at a fixed start voltage | The only absolute number that needs no console | Hours per run; resolve only differences larger than about 10 percent. Three runs per arm, same cell, same temperature. |
| Plan 65 latency procedure, still to be written as a host script | p50/p95/p99 and missed-event counts | Required companion metric for every arm that touches coexistence. Plan 65 specifies the method; no implementation exists in the tree yet. |

### 8.2 Scenario matrix

Each row is a 30 minute steady-state window unless stated. Every arm runs with
the same screen state, the same camera, the same AP and the same channel.

| ID | Scenario | Purpose |
| --- | --- | --- |
| M0 | Wi-Fi off, camera connected, screen off, `SLEEP_CONN` per its current default | Baseline for every delta. |
| M1 | M0 plus station up, no MQTT, NTP already synced | Isolates the station itself. |
| M2 | M1 plus MQTT connected, no traffic | Isolates keepalive and the owner loop. |
| M3 | M2 plus one MQTT publish per 10 s and a shutter every 2 s for 10 min | Loaded hub. |
| M4 | M3 plus a visible WebUI tab polling at 3 s (branch build) | WebUI cost. |
| M5 | M3 plus 3 and 5 cameras | Coexistence scaling. |
| M6 | M1 with `SLEEP_CONN` on | The key question: what the station does to the 3.3 mA floor. |
| M7 | Wi-Fi enabled with MQTT and WebUI off, 30 min | W-03 stop-on-last-consumer. |
| M8 | M2 with the broker down for 30 min, then up | W-28 backoff. |
| M9 | Wrong passphrase, 30 min | W-23 retry loop. |
| M10 | 20 `wifi connect` cycles with the AP off and a camera connected | Scan and retry cost, W-20 to W-23. |
| M11 | Not a run: software coexistence has no runtime switch, it is `CONFIG_ESP_COEX_SW_COEXIST_ENABLE` in every config | Record that coexistence can only be observed with the latency procedure, not toggled. |

### 8.3 Procedure

1. Fresh NVS per arm. `wifi forget`, then provision the same SSID and
   passphrase, so `bssid_set` starts false in every arm.
2. Charge to the same voltage, then run the arm on the analyzer. Record the
   meter minimum, mean and integrated mAh, plus `power stats` before and after.
3. Repeat each arm three times. Report mean and standard deviation. Treat any
   delta smaller than three standard deviations as unmeasured.
4. For any arm that changes a radio setting, run the plan 65 shutter latency test
   in the same session and record p50/p95/p99 and missed events.
5. For any arm that changes a sleep or light-sleep setting, also record heap
   minimum and `power stats` lock residency, because the two failure modes here
   are heap exhaustion and a leaked lock, not a wrong number.
6. Reject an arm if the energy delta is inside noise, if p99 shutter latency
   grows by more than 10 percent, or if the 24 h soak shows more BLE disconnects
   than the baseline.

### 8.4 What each row needs before it can be promoted

- W-03 and W-04 need M7 plus a policy decision. Without M7 there is no delta.
- W-06 needs M1 with listen interval 1 and 3 plus the latency test. The published
  DTIM analogue (D11) is not sufficient.
- W-07 needs M6 A/B on the same binary except for the Kconfig symbol.
- W-08 needs M1 with the window at 50 and 20 ms, plus a retry count.
- W-11 needs M3 at the far edge of range, with retry and RSSI logging.
- W-13 needs a build that cannot use the console, so it needs the analyzer, not
  `power log`.
- W-17, W-18 and W-19 need M5 and the latency harness.
- W-26 and W-27 need M2 and M3, and the effect may be inside noise. Accept them
  as simplifications even if the energy is unmeasurable, and say so.
- W-28 needs M8 and a broker outage of at least 30 min.
- W-30 to W-32 need the branch build, M4 and a browser session.

## 9. Topic coverage

| Requested topic | Rows |
| --- | --- |
| AP versus station mode | W-01, W-02 |
| Wi-Fi sleep | W-06, W-07, W-08, W-09, W-10 |
| Modem sleep | W-06, W-07; current state in 3.3 |
| DTIM | W-06, W-10 |
| Beacon interval | W-10; BLE interval interaction in W-17 |
| TX power | W-11, W-12, W-13, W-14 |
| Coexistence with BLE | W-15 to W-19, plus 3.4 |
| Scan cost | W-15, W-20, W-21, W-22 |
| Connection retries | W-23, W-24, W-25 |
| MQTT keepalive | No row: keepalive is not a power lever. The rejected keepalive change in section 7 explains why. |
| WebUI polling | W-31, W-32 |
| TLS session cost | W-30, and the rejected session-ticket row in section 7 |
| Image transfer over Wi-Fi versus BLE | W-33 |
| When Wi-Fi should be disabled | W-02, W-03, W-04, section 7 |

## 10. Follow-ups and documentation obligations

If any of these land, the documentation mandate applies:

- A new or changed Wi-Fi setting (`WIFI_PS` mode, TX cap, idle timeout, hub
  consumer policy) needs `docs/settings-and-controls.md`,
  `docs/wiki/Settings-Reference.md` and the README settings overview.
- A new console behavior (stop on auth failure, stop on idle) needs
  `docs/console-commands.md` and the README console block.
- The WebUI rows are branch-only and need their own docs in the branch.
- Rows that change coexistence policy (W-17, W-19) should update
  `plans/65-bt-coexistence.md` and `plans/96-wifi-bt-ble-hardening.md` item C8
  and D2 in the same PR.

Open questions this report cannot answer:

1. What is the marginal current of a modem-sleep station on a StickS3 that is
   otherwise in `SLEEP_CONN` light sleep? That single number decides whether W-03
   and W-06 are worth their risk.
2. Is the 40.1 mA published figure reproducible on this board, and at what CPU
   frequency? D11 does not state the clock.
3. Does `ESP_WIFI_SLP_IRAM_OPT` interact with the GPS UART or display APB pattern
   under light sleep? Only a soak answers this.
4. What does the target AP do with a station that uses a listen interval of 3?
   The answer is per-AP and must be tested on the actual studio network.
