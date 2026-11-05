#!/usr/bin/env python3
"""Read the read-only BLE status exposed by the `m5stick-s3-debug-ble` image.

The service is intentionally unencrypted so it still answers when companion
authentication, the host GATT cache, or the USB CDC endpoint is broken. It is
not part of the production companion contract.

Install the one dependency:

    python3 -m pip install bleak

Examples:

    tools/ble_debug.py --device furble-38a70 --status
    tools/ble_debug.py --device furble-38a70 --status --interval 5
"""

from __future__ import annotations

import argparse
import asyncio
import sys
import time

try:
    from bleak import BleakClient, BleakScanner
except ImportError:  # pragma: no cover - dependency hint
    print("bleak is not installed; run: python3 -m pip install bleak", file=sys.stderr)
    raise SystemExit(2)

SERVICE_UUID = "b57f4f70-087b-4740-b71d-8262cf26ebbc"
STATUS_UUID = "b57f4f72-087b-4740-b71d-8262cf26ebbc"


def matches(device, advertisement, target: str | None) -> bool:
    if not target:
        return True
    needle = target.lower()
    # macOS keeps the first name it saw for a CoreBluetooth identifier even
    # after a firmware rename or address rotation, so also check the local name
    # carried in the current advertisement.
    return (
        needle in (device.name or "").lower()
        or needle in (advertisement.local_name or "").lower()
        or needle in (device.address or "").lower()
    )


async def find_device(target: str | None, timeout: float):
    devices = await BleakScanner.discover(timeout=timeout, return_adv=True)
    matched = [
        (device, advertisement)
        for device, advertisement in devices.values()
        if matches(device, advertisement, target)
    ]
    if not matched:
        names = ", ".join(
            sorted(
                {
                    advertisement.local_name or device.name or device.address
                    for device, advertisement in devices.values()
                }
            )
        ) or "none"
        print(f"no BLE device matched {target!r}; saw: {names}", file=sys.stderr)
        return None
    matched.sort(
        key=lambda item: (item[1].local_name or item[0].name or "").lower().find("furble") < 0
    )
    return matched[0][0]


async def run(args) -> int:
    device = await find_device(args.device, args.scan_timeout)
    if device is None:
        return 4
    print(f"connecting to {device.name or device.address} ({device.address})", file=sys.stderr)
    try:
        async with BleakClient(device, timeout=args.timeout) as client:
            if not client.is_connected:
                print("connection failed", file=sys.stderr)
                return 1
            deadline = time.monotonic() + args.duration if args.duration > 0 else None
            while True:
                try:
                    value = await client.read_gatt_char(STATUS_UUID)
                    print(value.decode("utf-8", "replace").rstrip())
                except Exception as error:  # noqa: BLE001 - surface CoreBluetooth text
                    print(f"status read failed: {error}", file=sys.stderr)
                    return 1
                if args.interval <= 0:
                    return 0
                if deadline is not None and time.monotonic() + args.interval > deadline:
                    return 0
                await asyncio.sleep(args.interval)
    except KeyboardInterrupt:
        return 0
    except Exception as error:  # noqa: BLE001
        print(f"BLE debug failed: {error}", file=sys.stderr)
        return 1


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--device", help="advertised name substring or CoreBluetooth UUID")
    parser.add_argument("--scan-timeout", type=float, default=6.0)
    parser.add_argument("--timeout", type=float, default=20.0)
    parser.add_argument("--status", action="store_true", help="read the status snapshot")
    parser.add_argument(
        "--interval", type=float, default=0.0, help="repeat every N seconds; 0 reads once"
    )
    parser.add_argument("--duration", type=float, default=0.0, help="repeat duration; 0 is forever")
    args = parser.parse_args()
    args.status = True
    return asyncio.run(run(args))


if __name__ == "__main__":
    raise SystemExit(main())
