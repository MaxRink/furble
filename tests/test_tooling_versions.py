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
            # Workflows pin actions to immutable SHAs and retain the major
            # version in a trailing comment. Accept the readable vN form too
            # so this check verifies policy rather than one spelling.
            matches = re.finditer(
                rf"uses: {re.escape(action)}@([^\s]+)(?:\s+#\s*(v\d+))?",
                workflow_text,
            )
            versions = [
                match.group(1)
                if match.group(1).startswith("v")
                else match.group(2)
                for match in matches
            ]
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

    def test_web_installer_runs_sticks3_pmic_preflight(self):
        index = (ROOT / "web-installer" / "index.html").read_text()
        preflight = (ROOT / "web-installer" / "pmic-preflight.js").read_text()
        workflow = (ROOT / ".github" / "workflows" / "pages.yml").read_text()

        self.assertIn('src="./pmic-preflight.js"', index)
        self.assertIn('id="installButton"', index)
        self.assertIn("flash prepare\\n", preflight)
        self.assertIn("flash.watchdog: disabled", preflight)
        self.assertIn("flash.download_recovery: unlocked", preflight)
        self.assertIn("port.close", preflight)
        self.assertIn("cp web-installer/pmic-preflight.js site/pmic-preflight.js", workflow)


if __name__ == "__main__":
    unittest.main()
