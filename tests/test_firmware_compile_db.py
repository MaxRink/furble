"""tools/firmware_compile_db.py rewrites Xtensa GCC entries for clang-tidy."""

import importlib.util
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "firmware_compile_db", ROOT / "tools" / "firmware_compile_db.py")
TOOL = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(TOOL)

GXX = "xtensa-esp32s3-elf-g++"
GCC = "xtensa-esp32s3-elf-gcc"
INCLUDES = {GXX: ["/tc/include/c++/14.2.0", "/tc/include"], GCC: ["/tc/include/c++/14.2.0",
                                                                    "/tc/include"]}


def entry(compiler, path):
    return {"directory": str(ROOT), "file": path,
            "command": f"{compiler} -o x.o -c -Os -mlongcalls -Werror=all -fcallgraph-info=su "
                       f"-fstrict-volatile-bitfields -DFOO=1 -Iinclude {path}"}


class ConvertTest(unittest.TestCase):
    def test_cpp_entry_is_rewritten_for_clang(self):
        [out] = TOOL.convert([entry(GXX, "src/main.cpp")], dict(INCLUDES))
        args = out["arguments"]
        self.assertEqual(args[:2], ["clang++", TOOL.TARGET])
        self.assertIn("-D__XTENSA__=1", args)
        self.assertIn("-nostdinc++", args)
        self.assertIn("/tc/include/c++/14.2.0", args)
        for dropped in ("-mlongcalls", "-Werror=all", "-fcallgraph-info=su",
                        "-fstrict-volatile-bitfields"):
            self.assertNotIn(dropped, args)
        for kept in ("-DFOO=1", "-Iinclude", "-Os"):
            self.assertIn(kept, args)
        self.assertEqual(out["file"], str(ROOT / "src" / "main.cpp"))

    def test_c_entry_gets_no_cpp_headers(self):
        [out] = TOOL.convert([entry(GCC, "lib/furble/x.c")], dict(INCLUDES))
        self.assertEqual(out["arguments"][0], "clang")
        self.assertNotIn("/tc/include/c++/14.2.0", out["arguments"])
        self.assertIn("/tc/include", out["arguments"])

    def test_only_furble_sources_are_kept(self):
        entries = [entry(GXX, "components/esp-nimble-cpp/src/NimBLEClient.cpp"),
                   entry(GXX, "/elsewhere/framework/src/x.c"),
                   entry(GXX, "src/main.cpp")]
        out = TOOL.convert(entries, dict(INCLUDES))
        self.assertEqual([e["file"] for e in out], [str(ROOT / "src" / "main.cpp")])


if __name__ == "__main__":
    unittest.main()
