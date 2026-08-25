"""Regression tests for the stacked-PR workflow trigger checker."""

from importlib.util import module_from_spec, spec_from_file_location
from pathlib import Path
import sys
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
CHECKER_PATH = ROOT / "tools" / "check_ci_workflows.py"
SPEC = spec_from_file_location("check_ci_workflows", CHECKER_PATH)
assert SPEC is not None and SPEC.loader is not None
CHECKER = module_from_spec(SPEC)
sys.modules[SPEC.name] = CHECKER
SPEC.loader.exec_module(CHECKER)


class CheckCIWorkflowsTest(unittest.TestCase):
  def lint(self, text: str) -> list[str]:
    with tempfile.NamedTemporaryFile(
        mode="w", suffix=".yml", encoding="utf-8"
    ) as workflow:
      workflow.write(text)
      workflow.flush()
      return CHECKER.lint_workflow(Path(workflow.name))

  def test_current_validation_workflows_pass(self):
    workflow_root = ROOT / ".github" / "workflows"
    checked = 0
    for path in CHECKER._workflow_paths(workflow_root):
      if "pull_request" in CHECKER.event_blocks(
          path.read_text(encoding="utf-8").splitlines()
      ):
        checked += 1
        self.assertEqual(CHECKER.lint_workflow(path), [], path.name)
    self.assertEqual(checked, 8)

  def test_branches_ignore_is_a_base_branch_filter(self):
    errors = self.lint(
        """name: test
on:
  pull_request:
    branches-ignore: [master]
    paths: [src/**]
  workflow_dispatch:
"""
    )
    self.assertIn("pull_request has a base-branch filter", errors)

  def test_quoted_keys_and_nonstandard_indentation_are_structural(self):
    errors = self.lint(
        """name: test
'on':
      'pull_request':
            'branches-ignore': [master]
            'paths': [src/**]
      'workflow_dispatch': {}
"""
    )
    self.assertIn("pull_request has a base-branch filter", errors)
    self.assertNotIn("pull_request has no path filter", errors)
    self.assertNotIn("has no workflow_dispatch trigger", errors)

  def test_flow_mapping_preserves_on_and_nested_event_keys(self):
    errors = self.lint(
        """name: test
on: {"pull_request": {"branches": [master], "paths": [src/**]}, "workflow_dispatch": {}}
"""
    )
    self.assertIn("pull_request has a base-branch filter", errors)
    self.assertEqual(
        CHECKER.event_blocks(
            [
                'on: {"pull_request": {"paths": [src/**]}, '
                '"workflow_dispatch": {}}'
            ]
        )["pull_request"].keys,
        frozenset({"paths"}),
    )

  def test_write_pull_request_permission_is_structural(self):
    errors = self.lint(
        """name: test
on:
  pull_request:
    paths: [src/**]
  workflow_dispatch:
permissions: {"pull-requests": write}
"""
    )
    self.assertIn("requests pull-requests: write for pull_request runs", errors)

  def test_new_pull_request_workflow_is_not_silently_omitted(self):
    with tempfile.TemporaryDirectory(prefix="furble-ci-workflow-") as root:
      workflow_root = Path(root) / ".github" / "workflows"
      workflow_root.mkdir(parents=True)
      (workflow_root / "new.yml").write_text(
          """name: new
on:
  pull_request:
    paths: [src/**]
""",
          encoding="utf-8",
      )
      result = subprocess.run(
          [sys.executable, str(CHECKER_PATH), "--root", root],
          check=False,
          capture_output=True,
          text=True,
      )
      self.assertNotEqual(result.returncode, 0)
      self.assertIn("new.yml: has no workflow_dispatch trigger", result.stderr)


if __name__ == "__main__":
  unittest.main()
