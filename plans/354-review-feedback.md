# 354 - Review feedback for the Core family hardware and power fixes

## Scope

This is the review-feedback record for #497 (`fix/core-family`), which corrects
the Core family capabilities, capacities, power-off paths, IP5306 boost policy
and GPS rail scope. Every finding is recorded as fixed, already fixed or a false
positive, with evidence. Line references are positions in the tree this PR
produces on master.

## PR #497 (564024b1)

Source: CI on the first pushed head, `Reproducible firmware builds`,
`build (m5stack-core-debug)`.

| # | Finding | Resolution | Evidence |
| ---: | :--- | :--- | :--- |
| 1 | The `m5stack-core-debug` profile no longer fits its app slot: `Error: The program size (1741009 bytes) is greater than maximum allowed (1740800 bytes)`, 209 bytes over. The profile is the dual-OTA Core debug image, so the slot cannot grow without a partition change, and CI builds it as a mandatory lane. | Fixed. The new refusal and boot-log strings were shortened, the second IP5306 defence string was dropped in favour of a silent `return false`, `UI::serviceRequests()` reports `unsupported` without its own sentence, and the refusal text is one literal shared between `Platform::powerOff()` and `UI::doPowerOff()`. No safety gate was removed: the menu hide, the callback guard, the console refusal, the `Request::POWER_OFF` gate, `doPowerOff()` and `canTurnDisplayOff()` all remain. | `pio run -e m5stack-core-debug` now reports `Flash: used 1740629 bytes from 1740800`, 171 bytes under the slot, and the build succeeds. `tools/size_budget.json` keeps its 1,740,800 `flash_slot` entry. |

The following were checked and are not findings:

- `M5.Power.Ip5306` exists only in the classic-ESP32 branch of
  `Power_Class.hpp` (M5Unified 0.2.20 `src/utility/Power_Class.hpp:239`), which
  is exactly the `#if defined(CONFIG_IDF_TARGET_ESP32)` guard the new code uses.
- `IP5306_Class` declares no `readRegister8` of its own; it inherits the public
  `const std::uint8_t readRegister8(std::uint8_t)` from `I2C_Device`
  (`src/utility/I2C_Class.hpp:173`), so `holdBoostOn()` is well formed.
- No em-dashes were added; `CONTRIBUTING.md` forbids them.
