#!/usr/bin/env python3
"""Check the power-management Kconfig invariants across board configs.

The ESP-IDF power symbols are not independent. ``PM_RTOS_IDLE_OPT`` and
``ESP_PHY_MAC_BB_PD`` both depend on ``FREERTOS_USE_TICKLESS_IDLE``, and a
config that enables tickless idle without the RTOS idle IRAM option leaves the
per-sleep wake cost on the table. This checker fails closed on a missing or
contradictory combination so a board config cannot drift silently.

Exit 0 when every sdkconfig is consistent, 1 otherwise.
"""

from __future__ import annotations

import argparse
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent


def symbolize(lines: list[str]) -> dict[str, str]:
    values: dict[str, str] = {}
    for line in lines:
        text = line.strip()
        if text.startswith("# ") and text.endswith(" is not set"):
            values[text[2:-len(" is not set")].strip()] = "n"
        elif text.startswith("CONFIG_") and "=" in text:
            key, value = text.split("=", 1)
            values[key] = value
    return values


def check_file(path: pathlib.Path) -> tuple[list[str], bool]:
    values = symbolize(path.read_text(encoding="utf-8", errors="ignore").splitlines())
    errors: list[str] = []
    tickless = values.get("CONFIG_FREERTOS_USE_TICKLESS_IDLE") == "y"
    pm = values.get("CONFIG_PM_ENABLE") == "y"
    idle_opt = values.get("CONFIG_PM_RTOS_IDLE_OPT") == "y"
    mac_bb_pd = values.get("CONFIG_ESP_PHY_MAC_BB_PD") == "y"
    brownout = values.get("CONFIG_ESP_BROWNOUT_DET") == "y"
    reduce_tx = values.get("CONFIG_ESP_PHY_REDUCE_TX_POWER") == "y"

    if tickless and not pm:
        errors.append("tickless idle enabled without CONFIG_PM_ENABLE")
    # CONFIG_PM_ENABLE without tickless idle is a recorded exception, not a
    # violation: the Waveshare Ethernet node keeps PM enabled for DFS and has
    # tickless off until the Ethernet/MQTT soak in plan P-026 passes. It is
    # reported for visibility but does not fail the check.
    if tickless and not idle_opt:
        errors.append("tickless idle enabled without CONFIG_PM_RTOS_IDLE_OPT")
    if mac_bb_pd and not tickless:
        errors.append("CONFIG_ESP_PHY_MAC_BB_PD enabled without FREERTOS_USE_TICKLESS_IDLE")
    if brownout and not reduce_tx:
        # A brownout reset otherwise restarts into the same TX load that caused
        # it. The PHY option drops TX power for the session and breaks the loop.
        errors.append("brownout detector enabled without CONFIG_ESP_PHY_REDUCE_TX_POWER")
    return errors, (pm and not tickless)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=pathlib.Path, default=ROOT)
    args = parser.parse_args()
    configs = sorted(args.root.glob("sdkconfig.*"))
    if not configs:
        print("no sdkconfig files found", file=sys.stderr)
        return 1

    failed = False
    for path in configs:
        errors, pm_without_tickless = check_file(path)
        if errors:
            failed = True
            for error in errors:
                print(f"{path.name}: {error}", file=sys.stderr)
        elif pm_without_tickless:
            print(f"{path.name}: PM enabled without tickless idle (recorded exception, P-026)")
        else:
            print(f"{path.name}: consistent")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
