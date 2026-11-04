"""Timezone table refresh sanity.

The C++ host test compiles the table and checks the same invariants. This test
keeps the maintenance script itself runnable so a table edit fails before the
firmware build, including when only the script changes.
"""

from __future__ import annotations

from pathlib import Path
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]


class TimezoneTableTest(unittest.TestCase):
  def test_curated_table_passes_check(self) -> None:
    result = subprocess.run(
        [sys.executable, str(ROOT / "tools" / "timezone_table.py"), "--check"],
        cwd=ROOT,
        text=True,
        capture_output=True,
        check=False,
    )
    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
    self.assertIn("timezone table: PASS", result.stdout)


if __name__ == "__main__":
  unittest.main()
