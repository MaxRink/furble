#!/usr/bin/env python3
"""Disarm the StickS3 PMIC watchdog before a deliberate serial upload.

The command is intentionally conservative. It only starts PlatformIO after a
developer-console firmware has acknowledged that the PMIC watchdog is disabled
and long-press download recovery is unlocked. If a dependency, port, or
handshake fails, the script refuses to guess and reports only what was observed.
"""

from __future__ import annotations

import argparse
from enum import Enum
import importlib
import os
from shutil import which
import subprocess
import sys
import time
from pathlib import Path


class PreflightResult(Enum):
    """Outcome categories used to keep diagnostic failures fail-closed."""

    PASSED = "passed"
    DEPENDENCY_MISSING = "dependency-missing"
    CONTACT_FAILED = "contact-failed"
    HANDSHAKE_FAILED = "handshake-failed"


def _platformio_site_packages() -> list[Path]:
    """Return site-packages directories belonging to the PlatformIO penv.

    PlatformIO installs pyserial in its own virtual environment. Developers
    often invoke this script with the system Python instead of that
    environment. We may import a package from the penv, but never install or
    execute anything implicitly.
    """

    candidates: list[Path] = []
    pio = which("pio")
    if pio:
        pio_path = Path(pio).resolve()
        if pio_path.parent.name in {"bin", "Scripts"}:
            candidates.append(pio_path.parent.parent)

    core_dir = os.environ.get("PLATFORMIO_CORE_DIR")
    if core_dir:
        candidates.append(Path(core_dir).expanduser())
    candidates.append(Path.home() / ".platformio")

    site_packages: list[Path] = []
    seen: set[Path] = set()
    for penv in candidates:
        if penv in seen:
            continue
        seen.add(penv)
        site_packages.extend((penv / "lib").glob("python*/site-packages"))
        site_packages.append(penv / "Lib" / "site-packages")
    return [path for path in site_packages if path.is_dir()]


def _load_serial():
    """Load pyserial from the active interpreter or PlatformIO's penv."""

    try:
        return importlib.import_module("serial")
    except ImportError:
        pass

    for package_dir in _platformio_site_packages():
        package_text = str(package_dir)
        if package_text not in sys.path:
            sys.path.insert(0, package_text)
        try:
            importlib.invalidate_caches()
            return importlib.import_module("serial")
        except ImportError:
            continue
    return None


def dependency_message() -> str:
    """Explain how to provide pyserial without contacting the device."""

    project = Path(__file__).resolve().parent.parent
    lines = [
        "PMIC preflight was not attempted because pyserial is unavailable.",
        f"Install the repository tools with: {sys.executable} -m pip install -r {project / 'requirements.txt'}",
    ]
    if which("pio"):
        lines.append(
            "Alternatively, invoke this script with the Python interpreter "
            "from the PlatformIO penv so its bundled pyserial is used."
        )
    lines.append("No serial port was opened and no PMIC state was inferred.")
    return "\n".join(lines)


def contact_message() -> str:
    return (
        "The PMIC preflight could not open the requested serial port. No upload "
        "was started and no PMIC lock state was inferred. Check the port, USB "
        "cable, and that no other monitor owns the device, then retry."
    )


def handshake_message() -> str:
    return (
        "The device responded, but the PMIC preflight did not provide all three "
        "safety acknowledgements. No upload was started and the PMIC state is "
        "unknown. If the application is responsive, run 'flash cancel' or reboot "
        "before retrying."
    )


def prepare_result(port: str, baud: int, timeout: float) -> PreflightResult:
    serial = _load_serial()
    if serial is None:
        return PreflightResult.DEPENDENCY_MISSING

    try:
        serial_options = {
            "baudrate": baud,
            "timeout": 0.2,
            "write_timeout": 1,
        }
        # Two readers can split the three acknowledgements and turn a valid
        # preflight into a timeout. pyserial exposes the POSIX TIOCEXCL
        # guard; leave the option out on platforms where it is unsupported.
        if os.name == "posix":
            serial_options["exclusive"] = True
        with serial.Serial(port, **serial_options) as device:
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
                # Collect each explicit acknowledgement. Matching only the
                # ready line would make the success check impossible because
                # the watchdog and download-recovery acknowledgements would
                # never enter `seen`.
                if line in {
                    "flash.ready: true",
                    "flash.watchdog: disabled",
                    "flash.download_recovery: unlocked",
                }:
                    seen.add(line)

            required = {
                "flash.ready: true",
                "flash.watchdog: disabled",
                "flash.download_recovery: unlocked",
            }
            if required.issubset(seen):
                return PreflightResult.PASSED
            return PreflightResult.HANDSHAKE_FAILED
    except (OSError, ValueError) as error:
        print(f"error: cannot contact {port}: {error}", file=sys.stderr)
        return PreflightResult.CONTACT_FAILED


def prepare(port: str, baud: int, timeout: float) -> bool:
    """Compatibility wrapper returning the historical boolean result."""

    return prepare_result(port, baud, timeout) is PreflightResult.PASSED


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

    result = prepare_result(args.port, args.baud, args.timeout)
    if result is not PreflightResult.PASSED:
        message = {
            PreflightResult.DEPENDENCY_MISSING: dependency_message,
            PreflightResult.CONTACT_FAILED: contact_message,
            PreflightResult.HANDSHAKE_FAILED: handshake_message,
        }[result]()
        print(message, file=sys.stderr)
        return 2

    if args.dry_run:
        print("PMIC preflight passed: dry run complete; upload not started")
        return 0

    print("PMIC preflight passed: starting upload")

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
    # Keep the normal PlatformIO build dependency. A `nobuild` upload can
    # silently flash an image left by another checkout or revision.
    build_environment = os.environ.copy()
    build_environment.setdefault("FURBLE_VERSION", "dev")
    build_environment.setdefault("FURBLE_TEST", "0")
    result = subprocess.run(command, check=False, env=build_environment)
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
