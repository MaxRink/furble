# 342 - Review feedback

An independent review of `91d2d04b` returned PASS with seven non-blocking
findings on `fix/wifi-lost-ip` (#485). Two were cheap enough to close in the
same PR: the doc paragraph that the diff joined to the lease-loss text, and the
untested deliberate-stop gate. The other five are recorded here and in
[342-wifi-lost-ip.md](342-wifi-lost-ip.md), with the follow-up named for each.
Every row is either fixed with evidence or a recorded decision; none is left
open without a named next step.

| # | Source | Finding | Resolution | Evidence |
| ---: | :--- | :--- | :--- | :--- |
| 1 | Independent review | `docs/webui.md:196` carried "An out-of-memory start keeps the stored" at the end of the new plan 342 paragraph, so the sentence about the identity and a rejected certificate/key pair at `:197` read as part of the lease-loss paragraph, and that line was unwrapped at 114 characters. | Fixed. A new paragraph starts before the identity sentence, and its four lines wrap at 65 to 79 characters like the prose around them. | `docs/webui.md:198-201` |
| 2 | Independent review | The deliberate-stop gate at `src/FurbleWiFi.cpp:997` had no test: replacing `if (g_ConnectRequested)` passed both host tests, so the clause that keeps a `wifi disable` quiet was unguarded. | Fixed. `tests/host/wifi/stubs/esp_log.h` counts `ESP_LOGW` calls in `esp_log_warn_count`, and `real-wifi-lifecycle` snapshots the count, runs `disconnect(); loseIp();` and requires it flat. Mutation: with the gate replaced by `if (true)`, the new assertion fails `FAIL: a deliberate stop still logged a lost IP` and the test exits 53; the same mutation with the pre-change test still passes, which reproduces the finding. | `tests/host/wifi/stubs/esp_log.h:4-11`, `tests/host/wifi/real_wifi_test.cpp:488-500` |
| 3 | Independent review | The wired half of the gate is still a cache: `src/FurbleWebUI.cpp:495-497` reads `Ethernet::isConnected()` and `getIP()`, which only the `IP_EVENT_ETH_LOST_IP` handler updates, and that event comes from the same 120 s ip-lost timer. On waveshare-s3-eth a wired lease loss therefore keeps the listener and `connected:true` for up to two minutes. | Recorded, out of scope. Same defect class as the radio half, which this PR fixes only for the station netif. Follow-up: a live wired readiness answer that asks esp-netif for the Ethernet netif, with the same `hasAssignedAddress()` shape. Named in plan 342. | `src/FurbleEthernet.cpp:368-379`, `:464-472`, `src/FurbleWebUI.cpp:495-497`, `components/esp_netif/lwip/esp_netif_lwip.c:1483-1520` |
| 4 | Independent review | `/api/status` mixes a live boolean with a cached ip (`src/FurbleWebUI.cpp:1150-1152`), so between the address appearing and `GOT_IP` caching it the page can emit `connected:true` with an empty `ip`, which the embedded page renders as " -42 dBm" at `src/FurbleWebUI.cpp:157`. | Recorded. Transient, one event-handler hop wide, and gone as soon as `GOT_IP` fills the cache. Follow-up: a live string sibling of `hasAssignedAddress()`, so the `ip` field comes from the same query answer as the boolean. Named in plan 342. | `src/FurbleWebUI.cpp:1150-1152`, `:157`, `src/FurbleWiFi.cpp:1266-1281` |
| 5 | Independent review | Half the readiness query is untested: `esp_netif_is_netif_up` returns `true` unconditionally in the double (`tests/host/wifi/real_wifi_test.cpp:56-60`), so deleting that check from `hasAssignedAddress()` fails nothing. | Recorded. Only the non-zero-address half is covered. Follow-up: a mutable up flag in the double and a case where the netif is down while the address is still set. Named in plan 342. | `tests/host/wifi/stubs/esp_netif.h:12-13`, `tests/host/wifi/real_wifi_test.cpp:56-60`, `src/FurbleWiFi.cpp:1275-1280` |
| 6 | Independent review | A station that never held an address logs "WiFi lost its IP address" on every DHCP retry, because esp-netif arms the ip-lost timer at each DHCP start (`components/esp_netif/lwip/esp_netif_lwip.c:1627`). | Recorded. Decision for this PR: the warning line stays, because the handler's log is already through review and gating it changes what a support session reads. Follow-up, named in plan 342: capture the previous `g_GotIp` under `g_StateMutex` before clearing it and log only a real transition, leaving the cache clear and the worker wake unchanged, with a never-held-address test. | `components/esp_netif/lwip/esp_netif_lwip.c:1627`, `:1529-1532`, `:1498-1516`, `src/FurbleWiFi.cpp:992-1008` |
| 7 | Independent review | "every committed sdkconfig" overstated the period citation: seven board configs carry `CONFIG_ESP_NETIF_IP_LOST_TIMER_INTERVAL=120` and the six fragments do not. The plan also cited `esp_netif_lwip.c:1503-1516` for the reset block and the post. | Fixed. The plan now names the seven board files and says the six fragments do not set the symbol, and the evidence row splits the citation into `:1506-1508` for the `esp_netif_reset_ip_info()` block and `:1510-1516` for the post. The published commit message still says "every committed sdkconfig"; this file and the plan carry the corrected wording, because a pushed commit is immutable. | `plans/342-wifi-lost-ip.md:43-47`, `:137-138`, python scan of `sdkconfig.*` (7 board files set the symbol, 6 fragments do not), `components/esp_netif/lwip/esp_netif_lwip.c:1506-1508`, `:1510-1516` |

## Validation

`cmake --build /tmp/furble-host-build`, then
`ctest --test-dir /tmp/furble-host-build`, passes 196 of 196 host tests,
`sh tools/run_sanitized_host_tests.sh` passes 194 of 194 under AddressSanitizer
and UndefinedBehaviorSanitizer (the two ThreadSanitizer probes excluded), and
`clang-format 21 --dry-run --Werror` is clean on the changed C and C++ files.
`python3 tools/pre_review.py --base origin/master` passes its 10 gates and
`sh sim/scripts/check-doc-tokens.sh` passes, both after the rebase onto
`dc5be70f` that kept master's plan 328 index row.
`python3 tools/check_review_threads.py --repo MaxRink/furble --pr 485` reports
no unresolved review thread and every check green on the new head.
