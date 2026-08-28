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
    HANDSHAKE_IO_FAILED = "handshake-io-failed"


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
    except Exception:
        pass

    for package_dir in _platformio_site_packages():
        package_text = str(package_dir)
        if package_text not in sys.path:
            sys.path.insert(0, package_text)
        try:
            importlib.invalidate_caches()
            return importlib.import_module("serial")
        except Exception:
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


def handshake_io_message() -> str:
    return (
        "The serial port opened, but communication failed during the PMIC "
        "preflight handshake. No upload was started and the PMIC state is "
        "unknown. Keep the device powered and retry after checking the USB "
        "connection."
    )


def restore_message(restored: bool) -> str:
    if restored:
        return (
            "The upload tool could not be started. PMIC watchdog restoration "
            "succeeded; no upload was started."
        )
    return (
        "The upload tool could not be started and automatic PMIC watchdog "
        "restoration failed. Keep the device powered, reconnect to the console, "
        "run 'flash cancel', and do not unplug USB while the watchdog is disabled."
    )


def upload_failure_message(restored: bool) -> str:
    if restored:
        return (
            "upload failed. PMIC watchdog restoration succeeded; no upload is "
            "still running."
        )
    return (
        "upload failed and automatic PMIC watchdog restoration failed. Keep the "
        "device powered, reconnect to the console, run 'flash cancel', and do "
        "not unplug USB while the watchdog is disabled."
    )


def preflight_only_message(restored: bool) -> str:
    if restored:
        return (
            "PMIC preflight passed and was cancelled cleanly: watchdog armed and "
            "download recovery locked; upload not started."
        )
    return (
        "PMIC preflight passed, but automatic watchdog restoration failed. No "
        "upload was started. Keep the device powered, reconnect to the console, "
        "run 'flash cancel', and do not unplug USB while the watchdog is disabled."
    )


def try_restore_flash_preparation(port: str, baud: int, timeout: float) -> bool:
    """Attempt cleanup without masking the original upload failure."""

    try:
        return restore_flash_preparation(port, baud, timeout)
    except Exception as error:
        print(f"error: PMIC watchdog restoration raised: {error}", file=sys.stderr)
        return False


def prepare_result(port: str, baud: int, timeout: float) -> PreflightResult:
    serial = _load_serial()
    if serial is None:
        return PreflightResult.DEPENDENCY_MISSING

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

    try:
        # Serial() opens the port. Keep this separate from the protocol below
        # so an open failure cannot be confused with a mid-handshake failure.
        device = serial.Serial(port, **serial_options)
    except (OSError, ValueError) as error:
        print(f"error: cannot open {port}: {error}", file=sys.stderr)
        return PreflightResult.CONTACT_FAILED

    try:
        try:
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
            print(f"error: PMIC handshake failed on {port}: {error}", file=sys.stderr)
            return PreflightResult.HANDSHAKE_IO_FAILED
    finally:
        close = getattr(device, "close", None)
        if callable(close):
            try:
                close()
            except Exception:
                pass


def restore_flash_preparation(port: str, baud: int, timeout: float) -> bool:
    """Re-arm the PMIC watchdog after a preflight-only upload failure."""

    serial = _load_serial()
    if serial is None:
        return False

    serial_options = {
        "baudrate": baud,
        "timeout": 0.2,
        "write_timeout": 1,
    }
    if os.name == "posix":
        serial_options["exclusive"] = True

    try:
        device = serial.Serial(port, **serial_options)
    except (OSError, ValueError) as error:
        print(f"error: cannot open {port} to restore PMIC watchdog: {error}", file=sys.stderr)
        return False

    try:
        try:
            device.reset_input_buffer()
            device.write(b"flash cancel\n")
            device.flush()
            deadline = time.monotonic() + timeout
            seen: set[str] = set()
            while time.monotonic() < deadline:
                raw = device.readline()
                if not raw:
                    continue
                line = raw.decode("utf-8", errors="replace").strip()
                print(line)
                if line in {"flash.ready: false", "flash.watchdog: armed"}:
                    seen.add(line)
            return {"flash.ready: false", "flash.watchdog: armed"}.issubset(seen)
        except (OSError, ValueError) as error:
            print(f"error: PMIC watchdog restoration failed on {port}: {error}", file=sys.stderr)
            return False
    finally:
        close = getattr(device, "close", None)
        if callable(close):
            try:
                close()
            except Exception:
                pass


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
    parser.add_argument(
        "--preflight-only",
        "--dry-run",
        dest="preflight_only",
        action="store_true",
        help="run the PMIC preflight but do not start PlatformIO (legacy alias: --dry-run)",
    )
    args = parser.parse_args()

    result = prepare_result(args.port, args.baud, args.timeout)
    if result is not PreflightResult.PASSED:
        message = {
            PreflightResult.DEPENDENCY_MISSING: dependency_message,
            PreflightResult.CONTACT_FAILED: contact_message,
            PreflightResult.HANDSHAKE_FAILED: handshake_message,
            PreflightResult.HANDSHAKE_IO_FAILED: handshake_io_message,
        }[result]()
        print(message, file=sys.stderr)
        return 2

    if args.preflight_only:
        restored = try_restore_flash_preparation(args.port, args.baud, args.timeout)
        print(
            preflight_only_message(restored),
            file=sys.stdout if restored else sys.stderr,
        )
        return 0 if restored else 2

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
    try:
        result = subprocess.run(command, check=False, env=build_environment)
    except (
        FileNotFoundError,
        PermissionError,
        OSError,
        ValueError,
        subprocess.SubprocessError,
    ) as error:
        print(f"error: could not start PlatformIO upload: {error}", file=sys.stderr)
        print(
            restore_message(
                try_restore_flash_preparation(args.port, args.baud, args.timeout)
            ),
            file=sys.stderr,
        )
        return 2
    if result.returncode != 0:
        print(
            upload_failure_message(
                try_restore_flash_preparation(args.port, args.baud, args.timeout)
            ),
            file=sys.stderr,
        )
    return result.returncode


if __name__ == "__main__":
    raise SystemExit(main())
