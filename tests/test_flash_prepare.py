"""Tests for the conservative PMIC flash preflight handshake."""

import importlib.util
import io
from pathlib import Path
import sys
import types
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "furble_flash_prepare", ROOT / "tools" / "flash_prepare.py"
)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
sys.modules[SPEC.name] = MODULE
SPEC.loader.exec_module(MODULE)


class FakeSerial:
    def __init__(self, lines):
        self.lines = iter(lines)
        self.closed = False

    def __enter__(self):
        return self

    def __exit__(self, *_args):
        return False

    def reset_input_buffer(self):
        pass

    def write(self, command):
        self.command = command

    def flush(self):
        pass

    def close(self):
        self.closed = True

    def readline(self):
        return next(self.lines, b"")


class HandshakeFailingSerial(FakeSerial):
    def write(self, _command):
        raise self.error


class RestoreSerial(FakeSerial):
    """Fake console used to verify the watchdog restoration command."""


class CloseFailingSerial(FakeSerial):
    def close(self):
        raise RuntimeError("close failed")


class FlashPrepareTest(unittest.TestCase):
    def test_accepts_all_three_acknowledgements(self):
        serial = FakeSerial(
            iter(
                line.encode()
                for line in (
                    "flash.ready: true\n",
                    "flash.watchdog: disabled\n",
                    "flash.download_recovery: unlocked\n",
                )
            )
        )
        serial_module = types.SimpleNamespace(Serial=mock.Mock(return_value=serial))
        with mock.patch.dict(sys.modules, {"serial": serial_module}):
            self.assertTrue(MODULE.prepare("/dev/test", 115200, 0.01))
        self.assertEqual(serial.command, b"flash prepare\n")
        options = serial_module.Serial.call_args.kwargs
        self.assertEqual(options["baudrate"], 115200)
        self.assertEqual(options["timeout"], 0.2)
        self.assertEqual(options["write_timeout"], 1)
        if MODULE.os.name == "posix":
            self.assertTrue(options["exclusive"])

    def test_success_has_explicit_result(self):
        serial = FakeSerial(
            iter(
                line.encode()
                for line in (
                    "flash.ready: true\n",
                    "flash.watchdog: disabled\n",
                    "flash.download_recovery: unlocked\n",
                )
            )
        )
        serial_module = types.SimpleNamespace(Serial=mock.Mock(return_value=serial))
        with mock.patch.dict(sys.modules, {"serial": serial_module}):
            self.assertIs(
                MODULE.prepare_result("/dev/test", 115200, 0.01),
                MODULE.PreflightResult.PASSED,
            )

    def test_close_exception_does_not_mask_success(self):
        serial = CloseFailingSerial(
            iter(
                line.encode()
                for line in (
                    "flash.ready: true\n",
                    "flash.watchdog: disabled\n",
                    "flash.download_recovery: unlocked\n",
                )
            )
        )
        serial_module = types.SimpleNamespace(Serial=mock.Mock(return_value=serial))
        with mock.patch.dict(sys.modules, {"serial": serial_module}):
            self.assertIs(
                MODULE.prepare_result("/dev/test", 115200, 0.01),
                MODULE.PreflightResult.PASSED,
            )

    def test_rejects_missing_acknowledgement(self):
        serial = FakeSerial(iter((b"flash.ready: true\n",)))
        serial_module = types.SimpleNamespace(Serial=mock.Mock(return_value=serial))
        with mock.patch.dict(sys.modules, {"serial": serial_module}):
            self.assertFalse(MODULE.prepare("/dev/test", 115200, 0.01))

    def test_missing_acknowledgement_is_not_lock_recovery(self):
        serial = FakeSerial(iter((b"flash.ready: true\n",)))
        serial_module = types.SimpleNamespace(Serial=mock.Mock(return_value=serial))
        with mock.patch.dict(sys.modules, {"serial": serial_module}):
            self.assertIs(
                MODULE.prepare_result("/dev/test", 115200, 0.01),
                MODULE.PreflightResult.HANDSHAKE_FAILED,
            )
        self.assertNotIn("DL_LOCK", MODULE.handshake_message())

    def test_port_failure_is_not_lock_recovery(self):
        serial_module = types.SimpleNamespace(
            Serial=mock.Mock(side_effect=OSError("denied"))
        )
        with mock.patch.dict(sys.modules, {"serial": serial_module}):
            self.assertIs(
                MODULE.prepare_result("/dev/test", 115200, 0.01),
                MODULE.PreflightResult.CONTACT_FAILED,
            )
        self.assertNotIn("DL_LOCK", MODULE.contact_message())

    def test_mid_handshake_oserror_is_not_reported_as_port_failure(self):
        serial = HandshakeFailingSerial(())
        serial.error = OSError("disconnected")
        serial_module = types.SimpleNamespace(Serial=mock.Mock(return_value=serial))
        with mock.patch.dict(sys.modules, {"serial": serial_module}):
            self.assertIs(
                MODULE.prepare_result("/dev/test", 115200, 0.01),
                MODULE.PreflightResult.HANDSHAKE_IO_FAILED,
            )
        self.assertIn("serial port opened", MODULE.handshake_io_message())
        self.assertNotIn("could not open", MODULE.handshake_io_message())

    def test_missing_dependency_does_not_contact_device_or_claim_lock(self):
        with mock.patch.object(MODULE, "_load_serial", return_value=None):
            self.assertIs(
                MODULE.prepare_result("/dev/test", 115200, 0.01),
                MODULE.PreflightResult.DEPENDENCY_MISSING,
            )
        message = MODULE.dependency_message()
        self.assertIn("pip install -r", message)
        self.assertIn("No serial port was opened", message)
        self.assertNotIn("DL_LOCK", message)

    def test_preflight_only_does_not_say_that_upload_started(self):
        serial = FakeSerial(
            iter(
                line.encode()
                for line in (
                    "flash.ready: true\n",
                    "flash.watchdog: disabled\n",
                    "flash.download_recovery: unlocked\n",
                )
            )
        )
        serial_module = types.SimpleNamespace(Serial=mock.Mock(return_value=serial))
        with (
            mock.patch.dict(sys.modules, {"serial": serial_module}),
            mock.patch.object(
                sys,
                "argv",
                ["flash_prepare.py", "--port", "/dev/test", "--preflight-only"],
            ),
            mock.patch.object(MODULE.subprocess, "run") as run,
            mock.patch("sys.stdout", new_callable=io.StringIO) as stdout,
        ):
            self.assertEqual(MODULE.main(), 0)
        run.assert_not_called()
        self.assertIn("upload not started", stdout.getvalue())

    def test_dry_run_remains_legacy_alias(self):
        serial = FakeSerial(
            iter(
                line.encode()
                for line in (
                    "flash.ready: true\n",
                    "flash.watchdog: disabled\n",
                    "flash.download_recovery: unlocked\n",
                )
            )
        )
        serial_module = types.SimpleNamespace(Serial=mock.Mock(return_value=serial))
        with (
            mock.patch.dict(sys.modules, {"serial": serial_module}),
            mock.patch.object(
                sys, "argv", ["flash_prepare.py", "--port", "/dev/test", "--dry-run"]
            ),
            mock.patch.object(MODULE.subprocess, "run") as run,
        ):
            self.assertEqual(MODULE.main(), 0)
        run.assert_not_called()

    def test_missing_platformio_restores_watchdog(self):
        prepared = FakeSerial(
            iter(
                line.encode()
                for line in (
                    "flash.ready: true\n",
                    "flash.watchdog: disabled\n",
                    "flash.download_recovery: unlocked\n",
                )
            )
        )
        restored = RestoreSerial(
            iter((b"flash.ready: false\n", b"flash.watchdog: armed\n"))
        )
        serial_module = types.SimpleNamespace(
            Serial=mock.Mock(side_effect=[prepared, restored])
        )
        with (
            mock.patch.dict(sys.modules, {"serial": serial_module}),
            mock.patch.object(
                sys, "argv", ["flash_prepare.py", "--port", "/dev/test"]
            ),
            mock.patch.object(
                MODULE.subprocess, "run", side_effect=FileNotFoundError("pio")
            ),
            mock.patch("sys.stderr", new_callable=io.StringIO) as stderr,
        ):
            self.assertEqual(MODULE.main(), 2)
        self.assertEqual(serial_module.Serial.call_count, 2)
        self.assertEqual(prepared.command, b"flash prepare\n")
        self.assertEqual(restored.command, b"flash cancel\n")
        self.assertIn("restoration succeeded", stderr.getvalue())

    def test_unexecutable_platformio_reports_failed_restoration(self):
        prepared = FakeSerial(
            iter(
                line.encode()
                for line in (
                    "flash.ready: true\n",
                    "flash.watchdog: disabled\n",
                    "flash.download_recovery: unlocked\n",
                )
            )
        )
        restored = HandshakeFailingSerial(())
        restored.error = PermissionError("cancel denied")
        serial_module = types.SimpleNamespace(
            Serial=mock.Mock(side_effect=[prepared, restored])
        )
        with (
            mock.patch.dict(sys.modules, {"serial": serial_module}),
            mock.patch.object(
                sys, "argv", ["flash_prepare.py", "--port", "/dev/test"]
            ),
            mock.patch.object(
                MODULE.subprocess, "run", side_effect=PermissionError("pio")
            ),
            mock.patch("sys.stderr", new_callable=io.StringIO) as stderr,
        ):
            self.assertEqual(MODULE.main(), 2)
        self.assertIn("automatic PMIC watchdog restoration failed", stderr.getvalue())
        self.assertIn("run 'flash cancel'", stderr.getvalue())

    def test_failed_upload_restores_watchdog(self):
        prepared = FakeSerial(
            iter(
                line.encode()
                for line in (
                    "flash.ready: true\n",
                    "flash.watchdog: disabled\n",
                    "flash.download_recovery: unlocked\n",
                )
            )
        )
        restored = RestoreSerial(
            iter((b"flash.ready: false\n", b"flash.watchdog: armed\n"))
        )
        serial_module = types.SimpleNamespace(
            Serial=mock.Mock(side_effect=[prepared, restored])
        )
        completed = types.SimpleNamespace(returncode=1)
        with (
            mock.patch.dict(sys.modules, {"serial": serial_module}),
            mock.patch.object(
                sys, "argv", ["flash_prepare.py", "--port", "/dev/test"]
            ),
            mock.patch.object(MODULE.subprocess, "run", return_value=completed),
            mock.patch("sys.stderr", new_callable=io.StringIO) as stderr,
        ):
            self.assertEqual(MODULE.main(), 1)
        self.assertEqual(serial_module.Serial.call_count, 2)
        self.assertEqual(restored.command, b"flash cancel\n")
        self.assertIn("restoration succeeded", stderr.getvalue())

    def test_failed_upload_reports_manual_recovery_when_restore_fails(self):
        prepared = FakeSerial(
            iter(
                line.encode()
                for line in (
                    "flash.ready: true\n",
                    "flash.watchdog: disabled\n",
                    "flash.download_recovery: unlocked\n",
                )
            )
        )
        restored = HandshakeFailingSerial(())
        restored.error = OSError("disconnected")
        serial_module = types.SimpleNamespace(
            Serial=mock.Mock(side_effect=[prepared, restored])
        )
        completed = types.SimpleNamespace(returncode=1)
        with (
            mock.patch.dict(sys.modules, {"serial": serial_module}),
            mock.patch.object(
                sys, "argv", ["flash_prepare.py", "--port", "/dev/test"]
            ),
            mock.patch.object(MODULE.subprocess, "run", return_value=completed),
            mock.patch("sys.stderr", new_callable=io.StringIO) as stderr,
        ):
            self.assertEqual(MODULE.main(), 1)
        self.assertIn("automatic PMIC watchdog restoration failed", stderr.getvalue())
        self.assertIn("run 'flash cancel'", stderr.getvalue())

    def test_subprocess_exception_restores_watchdog(self):
        prepared = FakeSerial(
            iter(
                line.encode()
                for line in (
                    "flash.ready: true\n",
                    "flash.watchdog: disabled\n",
                    "flash.download_recovery: unlocked\n",
                )
            )
        )
        restored = RestoreSerial(
            iter((b"flash.ready: false\n", b"flash.watchdog: armed\n"))
        )
        serial_module = types.SimpleNamespace(
            Serial=mock.Mock(side_effect=[prepared, restored])
        )
        with (
            mock.patch.dict(sys.modules, {"serial": serial_module}),
            mock.patch.object(
                sys, "argv", ["flash_prepare.py", "--port", "/dev/test"]
            ),
            mock.patch.object(
                MODULE.subprocess, "run", side_effect=ValueError("bad arguments")
            ),
            mock.patch("sys.stderr", new_callable=io.StringIO) as stderr,
        ):
            self.assertEqual(MODULE.main(), 2)
        self.assertEqual(restored.command, b"flash cancel\n")
        self.assertIn("restoration succeeded", stderr.getvalue())

    def test_dependency_failure_main_has_no_recovery_claim(self):
        with (
            mock.patch.object(MODULE, "_load_serial", return_value=None),
            mock.patch.object(sys, "argv", ["flash_prepare.py", "--port", "/dev/test"]),
            mock.patch("sys.stderr", new_callable=io.StringIO) as stderr,
        ):
            self.assertEqual(MODULE.main(), 2)
        self.assertIn("pyserial is unavailable", stderr.getvalue())
        self.assertNotIn("DL_LOCK", stderr.getvalue())

    def test_upload_uses_normal_build_and_required_identity_environment(self):
        serial = FakeSerial(
            iter(
                line.encode()
                for line in (
                    "flash.ready: true\n",
                    "flash.watchdog: disabled\n",
                    "flash.download_recovery: unlocked\n",
                )
            )
        )
        serial_module = types.SimpleNamespace(Serial=mock.Mock(return_value=serial))
        completed = types.SimpleNamespace(returncode=0)
        with (
            mock.patch.dict(sys.modules, {"serial": serial_module}),
            mock.patch.object(
                sys, "argv", ["flash_prepare.py", "--port", "/dev/test"]
            ),
            mock.patch.object(
                MODULE.subprocess, "run", return_value=completed
            ) as run,
        ):
            self.assertEqual(MODULE.main(), 0)

        command = run.call_args.args[0]
        self.assertIn("upload", command)
        self.assertNotIn("nobuild", command)
        environment = run.call_args.kwargs["env"]
        self.assertEqual(environment["FURBLE_VERSION"], "dev")
        self.assertEqual(environment["FURBLE_TEST"], "0")


if __name__ == "__main__":
    unittest.main()
