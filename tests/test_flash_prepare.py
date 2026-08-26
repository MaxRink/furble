"""Tests for the conservative PMIC flash preflight handshake."""

import importlib.util
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

    def readline(self):
        return next(self.lines, b"")


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

    def test_rejects_missing_acknowledgement(self):
        serial = FakeSerial(iter((b"flash.ready: true\n",)))
        serial_module = types.SimpleNamespace(Serial=mock.Mock(return_value=serial))
        with mock.patch.dict(sys.modules, {"serial": serial_module}):
            self.assertFalse(MODULE.prepare("/dev/test", 115200, 0.01))


if __name__ == "__main__":
    unittest.main()
