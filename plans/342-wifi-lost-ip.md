# 342 - WebUI network state after a DHCP lease loss

Status: implemented on `fix/wifi-lost-ip`, host coverage and docs included. It
touches `src/FurbleWiFi.cpp`, `src/FurbleWebUI.cpp`, `include/FurbleWiFi.h`,
`include/FurbleWebUI.h`, the WiFi and WebUI host doubles and tests,
`sim/FurbleWiFi.cpp` and the matching docs. No hardware run: the defect and the
fix are event and interface bookkeeping, and both host doubles drive the
production handler.

## Defect

After a DHCP lease loss the WebUI kept reporting a connected network on an
address that no longer carried traffic, while MQTT recovered on its own.

`FurbleWiFi::ensureStarted()` registered `WIFI_EVENT` and
`IP_EVENT_STA_GOT_IP` only. Nothing cleared the cached address when the station
lost its lease, so `WiFi::status_t.connected` and `status_t.ip` kept the values
the last `GOT_IP` wrote.

Two WebUI surfaces read that cache:

- `WebUI::networkReady()` decided whether the HTTPS server may run, from
  `status.connected && !status.ip.empty()`.
- `/api/status` reported `network.connected` and `network.ip` from the same
  snapshot, which is the "Network" row the browser renders.

`MQTT::networkReady()` already asked the opposite question of the live stack:
any `esp_netif` that is up with a non-zero IPv4 address. After a lease loss the
station kept listening for a browser on a dead address, or served a page that
claimed a connection the station no longer had, while MQTT reconnected as soon
as an address came back. The two surfaces disagreed because they read different
sources, not because the station behaved differently.

## Why the cache alone cannot be the fix

ESP-IDF does not deliver `IP_EVENT_STA_LOST_IP` synchronously with the loss. The
default station netif configures `.lost_ip_event = IP_EVENT_STA_LOST_IP`
(`components/esp_netif/include/esp_netif_defaults.h:51`), and the ip-lost timer
posts it only after it observes that the netif address went to zero
(`components/esp_netif/lwip/esp_netif_lwip.c:1483-1516`, timer period
`CONFIG_ESP_NETIF_IP_LOST_TIMER_INTERVAL`, 120 s in every committed
`sdkconfig.*`). A cache kept in step by that event can therefore be stale for up
to two minutes, which is longer than the WebUI supervisor period (500 ms) and
the MQTT reconnect retry.

So the work is split: the event keeps the cache honest for the console, NTP and
`WiFi::status_t`, and the readiness answer is read from the interface so it is
correct before any event arrives. The second half is what makes the two
surfaces agree at every instant, not just after the event.

## Event flow

Before:

```text
lease lost -> esp-netif resets ip_info, arms the ip-lost timer
           -> (up to 120 s) IP_EVENT_STA_LOST_IP  [no handler]
           -> WiFi cache keeps connected=true and the old ip
           -> WebUI gate stays open, /api/status says connected on a dead ip
           -> MQTT live netif query says down and retries
```

After:

```text
lease lost -> esp-netif resets ip_info (address 0)
           -> WebUI gate and /api/status read the live netif: false at once
           -> IP_EVENT_STA_LOST_IP: handler clears g_GotIp and g_Ip under
              g_StateMutex and notifies the Wi-Fi worker
           -> worker runs serviceEventStorage(); a queued NTP start declines
              because startNtp() re-reads g_GotIp
           -> DHCP recovers: esp-netif assigns the address, GOT_IP restores the
              cache, the gate reopens and MQTT connects
```

The handler stays on the default event task and follows the existing rules: it
takes a mutex, calls `std::string::clear()` (no allocation and no free), logs
one line and posts a task notification. It opens no NVS handle, reads no
setting, starts or stops no SNTP client and touches no driver.

The event is ignored when `g_ConnectRequested` is false, because a deliberate
`wifi disconnect` or `wifi disable` already cleared the same fields and its own
driver stop also drops the address. That keeps the log quiet during an
intentional stop.

## Decisions

- `WiFi::status_t.connected` stays true after a lost lease. The station is
  still associated, and clearing the flag would drop the worker out of its
  connected branch into the reconnect loop, where `esp_wifi_connect()` on an
  associated station fails and feeds the failure counters until the station
  stops. The lost address is visible as an empty `status_t.ip`, and
  `hasAssignedAddress()` is the readiness answer.
- A running SNTP client is not stopped by the event. Stopping it means
  `esp_netif_sntp_deinit()` under `g_NtpMutex` on the event task, which is the
  blocking work the handler must not do. The client keeps its configured server
  until the next disconnect, and it recovers with the address.
- `WiFi::hasAssignedAddress()` is a `WiFi` member rather than a new shared
  header so the station netif stays private and no new ESP-IDF type appears in a
  public header (the coupling ratchet counts those). `MQTT::networkReady()`
  keeps its own generic loop on purpose: it must not bind to Wi-Fi, because a
  wired netif satisfies it just as well.
- The WebUI keeps its `FURBLE_ETHERNET` branch. The station query answers for
  the radio, the Ethernet branch answers for the wire, as before.
- No new task, no new start gate and no new setting. The poll cadence, the heap
  floors and the credential gate are unchanged.

## Evidence

ESP-IDF 5.5, the pinned framework package:

| Fact | Location |
| :--- | :--- |
| `IP_EVENT_STA_LOST_IP` means "station lost IP and the IP is reset to 0" | `components/esp_netif/include/esp_netif_types.h:100` |
| the default station netif posts that event | `components/esp_netif/include/esp_netif_defaults.h:51` |
| the ip-lost timer resets `ip_info` for a DHCP client and posts the event | `components/esp_netif/lwip/esp_netif_lwip.c:1503-1516` |
| the timer period is 120 s on every board file in this tree | `CONFIG_ESP_NETIF_IP_LOST_TIMER_INTERVAL=120` |

Host coverage, `real-wifi-lifecycle` (`tests/host/wifi/real_wifi_test.cpp`),
which compiles the production `src/FurbleWiFi.cpp` against the existing WiFi
double:

- the lazy start now registers three handlers, pinned by the existing
  `event_handler_registrations` assertion;
- `GOT_IP` caches the address, and `hasAssignedAddress()` is true;
- `IP_EVENT_STA_LOST_IP` empties the cached address, and
  `hasAssignedAddress()` is false;
- the deferred NTP start that `GOT_IP` queued does not start SNTP after the
  loss;
- a live address with no event yet already answers true, which is what the
  cached answer could not do;
- a later `GOT_IP` restores the cache and the readiness answer.

`tests/host/wifi/stubs/esp_netif.h` gained `esp_netif_is_netif_up()`, and the
test's fake address is now mutable, because esp-netif resets the address to zero
before it posts the loss. The station netif stays up across the loss, as it does
on the device.

Host coverage, `webui-handler` (`tests/host/webui_handler_test.cpp`), which runs
the production `src/FurbleWebUI.cpp`:

- the readiness gate is open while the double holds an address, closes when the
  address is lost, and reopens when it returns;
- `/api/status` reports `"connected":false` with an empty `ip` after the loss and
  the cached address again afterwards.

Mutation checks, all caught:

| Reverted behaviour | Result |
| :--- | :--- |
| handler clears `g_GotIp` but not the address | `FAIL: a lost IP kept the cached address` |
| readiness reads the cache instead of the netif | `FAIL: the WebUI readiness answer did not follow the live netif` |
| WebUI gate and status page read `WiFi::getStatus()` | three failures in `webui_handler_test` |

## Remaining disagreement between the surfaces

Recorded so the next reader does not rediscover them:

- `wifi status` prints the connected state with `ip: none` between the loss and
  the next lease, because `status_t.connected` tracks association and is
  deliberately not cleared. The WebUI answers `connected:false` for the same
  moment. The console exposes the distinction, the page answers one question.
- `MQTT::networkReady()` accepts any netif; the WebUI accepts the station netif
  or Ethernet. A third interface type would satisfy MQTT and not the WebUI. No
  supported profile creates one.
- Neither surface reacts to a lease change that keeps a valid address (a DHCP
  renewal): the address is still live, so no event is due and both stay ready.

## Docs

- `src/CLAUDE.md`: the `FurbleWiFi` and `FurbleWebUI` entries.
- `include/CLAUDE.md`: the `FurbleWiFi.h` entry for `hasAssignedAddress()`.
- `docs/webui.md`: what "WiFi has no address" means and why it is live.

Plan number 342 follows the branch sequence; plans merged in this checkout stop
at 325 and 326-338 are in flight on other branches.
