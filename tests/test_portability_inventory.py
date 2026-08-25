"""Failure-mode tests for the Nordic portability boundary guard."""

from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
CHECKER = ROOT / "tools" / "check_portability_inventory.py"


class PortabilityInventoryTest(unittest.TestCase):
    def run_checker(self, root: Path) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [sys.executable, str(CHECKER), "--check", "--root", str(root)],
            check=False,
            capture_output=True,
            text=True,
        )

    def make_minimal_root(self) -> Path:
        temp = Path(tempfile.mkdtemp(prefix="furble-portability-"))
        protocol = temp / "lib/furble/protocol"
        protocol.mkdir(parents=True)
        (protocol / "codec.cpp").write_text("int codec() { return 0; }\n", encoding="utf-8")
        tools = temp / "tools"
        tools.mkdir()
        (tools / "portable_core_manifest.txt").write_text(
            "lib/furble/protocol/codec.cpp\n", encoding="utf-8"
        )
        return temp

    def test_missing_portable_root_fails_closed(self):
        with tempfile.TemporaryDirectory(prefix="furble-portability-") as path:
            result = self.run_checker(Path(path))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("missing portable contract root", result.stderr)

    def test_empty_portable_root_fails_closed(self):
        root = self.make_minimal_root()
        (root / "lib/furble/protocol/codec.cpp").unlink()
        result = self.run_checker(root)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("portable contract root is empty", result.stderr)

    def test_renamed_source_fails_manifest_check(self):
        root = self.make_minimal_root()
        (root / "lib/furble/protocol/codec.cpp").rename(
            root / "lib/furble/protocol/renamed.cpp"
        )
        result = self.run_checker(root)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("shared source manifest", result.stderr)

    def test_nordic_protocol_copy_fails_closed(self):
        root = self.make_minimal_root()
        copied = root / "ports/nordic/lib/furble"
        copied.mkdir(parents=True)
        (copied / "camera_bytes.cpp").write_text(
            "int   codec() {\n  return 0; }\n", encoding="utf-8"
        )
        result = self.run_checker(root)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("duplicate portable source content", result.stderr)

    def test_nordic_port_adapter_without_copy_is_allowed(self):
        root = self.make_minimal_root()
        adapter = root / "ports/nrf52840"
        adapter.mkdir(parents=True)
        (adapter / "ble_adapter.cpp").write_text(
            "int nordic_adapter() { return 1; }\n", encoding="utf-8"
        )
        result = self.run_checker(root)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_current_tree_passes(self):
        result = self.run_checker(ROOT)
        self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main()
