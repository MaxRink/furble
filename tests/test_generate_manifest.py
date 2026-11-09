"""web-installer/generate-manifest.py keeps or drops the OTA data part."""

import importlib.util
import json
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "generate_manifest", ROOT / "web-installer" / "generate-manifest.py")
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("cannot load web-installer/generate-manifest.py")
TOOL = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(TOOL)
TEMPLATE = str(ROOT / "web-installer" / "manifest.tmpl")


class RenderTest(unittest.TestCase):
    def parts(self, ota_data):
        text = TOOL.render(TEMPLATE, "m5stick-c-debug", "v1", "https://x", ota_data)
        return [[(p["path"].rsplit("/", 1)[1], p["offset"]) for p in b["parts"]]
                for b in json.loads(text)["builds"]]

    def test_two_ota_image_flashes_ota_data(self):
        for build in self.parts(True):
            self.assertIn(("furble-ota_data_initial-m5stick-c-debug-v1.bin", 61440), build)
            self.assertEqual(len(build), 4)

    def test_single_factory_image_omits_only_ota_data(self):
        for build in self.parts(False):
            names = [name for name, _ in build]
            self.assertEqual(len(build), 3)
            self.assertFalse(any("ota_data_initial" in name for name in names))
            self.assertIn(("furble-m5stick-c-debug-v1.bin", 131072), build)


if __name__ == "__main__":
    unittest.main()
