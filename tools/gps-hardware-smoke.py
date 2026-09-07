#!/usr/bin/env python3
"""Run a bounded, non-interactive GPS console smoke test.

Usage: python3 tools/gps-hardware-smoke.py /dev/cu.usbmodemXXXX

The script only uses the USB console. It does not flash, reset, or open a
serial monitor, and it leaves the GPS satellite capture setting disabled.
"""

from __future__ import annotations

import os
import select
import sys
import termios
import time


def configure(fd: int) -> None:
    attrs = termios.tcgetattr(fd)
    attrs[0] = 0
    attrs[1] = 0
    attrs[2] = termios.CS8 | termios.CLOCAL | termios.CREAD
    attrs[3] = 0
    attrs[4] = termios.B115200
    attrs[5] = termios.B115200
    termios.tcsetattr(fd, termios.TCSANOW, attrs)


def read_available(fd: int, seconds: float) -> bytes:
    end = time.monotonic() + seconds
    result = bytearray()
    while True:
        remaining = end - time.monotonic()
        if remaining <= 0:
            return bytes(result)
        ready, _, _ = select.select([fd], [], [], remaining)
        if not ready:
            return bytes(result)
        try:
            chunk = os.read(fd, 4096)
        except BlockingIOError:
            continue
        if not chunk:
            return bytes(result)
        result.extend(chunk)


def run_command(fd: int, command: str, seconds: float) -> bytes:
    print(f">>> {command}", flush=True)
    os.write(fd, (command + "\r\n").encode("ascii"))
    output = read_available(fd, seconds)
    if output:
        sys.stdout.buffer.write(output)
        sys.stdout.buffer.flush()
    return output


def main() -> int:
    if len(sys.argv) != 2:
        print(f"usage: {sys.argv[0]} SERIAL_PORT", file=sys.stderr)
        return 2

    port = sys.argv[1]
    try:
        fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    except OSError as exc:
        print(f"cannot open {port}: {exc}", file=sys.stderr)
        return 2

    try:
        configure(fd)
        # Drain boot logs and any stale console input before issuing commands.
        read_available(fd, 0.5)
        run_command(fd, "gps", 1.0)
        run_command(fd, "gps sats on", 0.5)
        run_command(fd, "gps sats", 2.0)
        run_command(fd, "gps monhw", 2.0)
        run_command(fd, "gps sats off", 0.5)
        run_command(fd, "gps", 1.0)
    finally:
        os.close(fd)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
