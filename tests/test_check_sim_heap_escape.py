"""Tests for the simulator heap-escape gate.

A bare `malloc()` in the firmware compiles and links against the host libc,
because `sim/shim/esp_heap_caps.h` includes the C library headers. The gate is
what turns that silent escape into a review failure, so both halves matter: a
clean tree passes, and every escape shape fails.
"""

from pathlib import Path
import contextlib
import io
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

import check_sim_heap_escape as gate  # noqa: E402


class GateTest(unittest.TestCase):
  def setUp(self) -> None:
    self._directory = tempfile.TemporaryDirectory()
    self.root = Path(self._directory.name)
    (self.root / "src").mkdir()

  def tearDown(self) -> None:
    self._directory.cleanup()

  def write(self, relative: str, text: str) -> Path:
    path = self.root / relative
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")
    return path

  def findings(self) -> list[str]:
    return gate.find_bare_calls(self.root, gate.source_files(self.root))

  def run_main(self) -> tuple[int, str, str]:
    stdout = io.StringIO()
    stderr = io.StringIO()
    with contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr):
      code = gate.main(["--root", str(self.root)])
    return code, stdout.getvalue(), stderr.getvalue()

  def test_clean_tree_passes(self) -> None:
    self.write(
        "src/clean.cpp",
        """
#include <esp_heap_caps.h>
void demo() {
  void *block = heap_caps_malloc(64, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  heap_caps_free(block);
}
""",
    )
    self.assertEqual(self.findings(), [])
    code, stdout, _ = self.run_main()
    self.assertEqual(code, 0)
    self.assertIn("heap escape check passed", stdout)

  def test_every_bare_family_member_is_reported(self) -> None:
    self.write(
        "src/dirty.cpp",
        """
void demo() {
  void *a = malloc(16);
  void *b = calloc(2, 8);
  void *c = realloc(a, 32);
  char *d = strdup("x");
  char *e = strndup("x", 1);
  void *f = pvPortMalloc(8);
  free(b);
  std::free(c);
  vPortFree(f);
  (void)d;
  (void)e;
}
""",
    )
    calls = sorted(finding.rsplit(": ", 1)[1] for finding in self.findings())
    self.assertEqual(
        calls,
        [
            "calloc()",
            "free()",
            "free()",
            "malloc()",
            "pvPortMalloc()",
            "realloc()",
            "strdup()",
            "strndup()",
            "vPortFree()",
        ],
    )
    code, _, stderr = self.run_main()
    self.assertEqual(code, 1)
    self.assertIn("src/dirty.cpp:3", stderr)

  def test_comments_and_literals_do_not_trip(self) -> None:
    self.write(
        "src/prose.cpp",
        """
// free(pointer) in a comment is not a call
/* malloc(8) in a block comment is not a call either */
void demo() {
  const char *text = "calloc(1, 2) inside a string";
  const char quote = 'f';
  (void)text;
  (void)quote;
}
""",
    )
    self.assertEqual(self.findings(), [])

  def test_prefixed_names_are_not_reported(self) -> None:
    self.write(
        "src/prefixed.cpp",
        """
void demo(Settings &settings, Holder &holder, void *pointer) {
  settings.free(pointer);
  holder->free(pointer);
  heap_caps_free(pointer);
  const size_t freeBytes = 0;
  (void)freeBytes;
}
""",
    )
    self.assertEqual(self.findings(), [])

  def test_non_source_files_are_ignored(self) -> None:
    self.write("src/notes.md", "malloc(16)\n")
    self.write("docs/readme.md", "free(pointer)\n")
    self.assertEqual(gate.source_files(self.root), [])

  def test_missing_firmware_tree_fails_closed(self) -> None:
    (self.root / "src").rmdir()
    code, _, stderr = self.run_main()
    self.assertEqual(code, 2)
    self.assertIn("no firmware sources", stderr)


if __name__ == "__main__":
  unittest.main()
