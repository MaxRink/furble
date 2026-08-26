#!/usr/bin/env python3
"""Disarm the StickS3 PMIC watchdog before a deliberate serial upload.

The command is intentionally conservative. It only starts PlatformIO after a
developer-console firmware has acknowledged that the PMIC watchdog is disabled
and long-press download recovery is unlocked. If the application is wedged, the
script refuses to guess and prints the physical recovery procedure instead.
"""

from __future__ import annotations

import argparse
import subprocess
import sys
import time
from pathlib import Path


def recovery_message() -> str:
    return (
        "PMIC preflight did not complete. USB unplugging and reset are insufficient "
        "for a retained DL_LOCK; remove battery power (disconnect, depletion, or "
        "service), restore it, then hold the StickS3 side button until the green "
        "LED flashes and retry."
    )


def prepare(port: str, baud: int, timeout: float) -> bool:
    try:
        import serial  # type: ignore
    except ImportError:
        print("error: pyserial is required for PMIC flash preflight", file=sys.stderr)
        return False

    try:
        with serial.Serial(port, baudrate=baud, timeout=0.2, write_timeout=1) as device:
            device.reset_input_buffer()
            device.write(b"flash prepare\n")
            device.flush()

            deadline = time.monotonic() + timeout
            seen: set[str] = set()
            while time.monotonic() < deadline:
                raw = device.readline()
                if not raw:
                    continue
                line = raw.decode("utf-8", errors="replace").strip()
                # Echoing the complete line is useful for an audit trail and
                # contains no credentials.
                print(line)
                if line.startswith(("flash.ready:", "flash.watchdog:", "flash.download_recovery:")):
                    seen.add(line)

            return {
                "flash.ready: true",
                "flash.watchdog: disabled",
                "flash.download_recovery: unlocked",
            }.issubset(seen)
    except (OSError, ValueError) as error:
        print(f"error: cannot contact {port}: {error}", file=sys.stderr)
        return False


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="StickS3 USB serial device")
    parser.add_argument(
        "--env", default="m5stick-s3-debug", help="PlatformIO environment to upload"
    )
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=5.0)
    parser.add_argument("--dry-run", action="store_true", help="preflight only")
    args = parser.parse_args()

    if not prepare(args.port, args.baud, args.timeout):
        print(recovery_message(), file=sys.stderr)
        return 2

    print("PMIC preflight passed: starting upload")
    if args.dry_run:
        return 0

    project = Path(__file__).resolve().parent.parent
    command = [
        "pio",
        "run",
        "-d",
        str(project),
        "-e",
        args.env,
        "-t",
        "upload",
        "--upload-port",
        args.port,
    ]
    result = subprocess.run(command, check=False)
    if result.returncode != 0:
        print(
            "upload failed. The device may still be in ROM download mode. "
            "Power-cycle it before retrying; if the application boots, run "
            "'flash cancel' to restore watchdog protection.",
            file=sys.stderr,
        )
    return result.returncode


if __name__ == "__main__":
    raise SystemExit(main())
