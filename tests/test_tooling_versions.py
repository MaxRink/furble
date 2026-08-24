"""Deterministic checks for the versions used by CI and the web installer."""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]


class ToolingVersionTest(unittest.TestCase):
    def test_platformio_version_is_pinned(self):
        requirements = (ROOT / "requirements.txt").read_text()

        self.assertRegex(requirements, r"(?m)^platformio==6\.1\.19$")

    def test_workflows_use_current_action_majors(self):
        workflow_text = "\n".join(
            path.read_text()
            for path in sorted((ROOT / ".github" / "workflows").glob("*.yml"))
        )

        expected = {
            "actions/checkout": "v7",
            "actions/setup-python": "v7",
            "actions/download-artifact": "v8",
        }
        for action, version in expected.items():
            versions = re.findall(
                rf"uses: {re.escape(action)}@(v\d+)(?:\s|$)", workflow_text
            )
            self.assertTrue(versions, f"No {action} references found")
            self.assertEqual(versions, [version] * len(versions))

    def test_web_installer_uses_pinned_esp_web_tools(self):
        index = (ROOT / "web-installer" / "index.html").read_text()

        self.assertIn(
            "https://unpkg.com/esp-web-tools@10.4.0/"
            "dist/web/install-button.js?module",
            index,
        )
        self.assertNotIn("esp-web-tools@10.2.1", index)


if __name__ == "__main__":
    unittest.main()
