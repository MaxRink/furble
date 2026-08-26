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

  def test_scalar_pull_request_target_is_rejected(self):
    errors = self.lint(
        """name: unsafe
on: pull_request_target
"""
    )
    self.assertIn("uses pull_request_target", errors)

  def test_scalar_pull_request_requires_paths_and_dispatch(self):
    errors = self.lint(
        """name: incomplete
on: pull_request
"""
    )
    self.assertIn("pull_request has no path filter", errors)
    self.assertIn("has no workflow_dispatch trigger", errors)

  def test_mixed_sequence_trigger_events_are_structural(self):
    errors = self.lint(
        """name: unsafe
on: [pull_request_target, workflow_dispatch]
"""
    )
    self.assertIn("uses pull_request_target", errors)

  def test_sequence_pull_request_requires_paths_but_has_dispatch(self):
    errors = self.lint(
        """name: incomplete
on: [pull_request, workflow_dispatch]
"""
    )
    self.assertIn("pull_request has no path filter", errors)
    self.assertNotIn("has no workflow_dispatch trigger", errors)

  def test_sequence_alias_target_is_rejected(self):
    errors = self.lint(
        """name: unsafe
events: &events [pull_request_target, workflow_dispatch]
on: *events
"""
    )
    self.assertIn("uses pull_request_target", errors)

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

  def test_multiline_flow_mapping_keeps_branch_filter_structural(self):
    errors = self.lint(
        """name: test
on: {
  pull_request: {branches: [master], paths: [src/**]},
  workflow_dispatch: {}
}
"""
    )
    self.assertIn("pull_request has a base-branch filter", errors)

  def test_top_level_trigger_alias_is_resolved(self):
    errors = self.lint(
        """name: test
triggers: &events
  pull_request:
    paths: [src/**]
  workflow_dispatch:
on: *events
"""
    )
    self.assertEqual(errors, [])
    self.assertEqual(
        CHECKER.event_blocks(
            [
                "triggers: &events",
                "  pull_request:",
                "    paths: [src/**]",
                "  workflow_dispatch:",
                "on: *events",
            ]
        )["pull_request"].keys,
        frozenset({"paths"}),
    )

  def test_anchored_pull_request_configuration_is_resolved(self):
    errors = self.lint(
        """name: test
on:
  pull_request: &pull_request
    branches-ignore: [master]
    paths: [src/**]
  workflow_dispatch:
"""
    )
    self.assertIn("pull_request has a base-branch filter", errors)

  def test_literal_run_text_does_not_grant_pull_request_write(self):
    errors = self.lint(
        """name: test
on:
  pull_request:
    paths: [src/**]
  workflow_dispatch:
jobs:
  test:
    steps:
      - run: |
          echo "pull_request:"
          echo "pull-requests: write"
"""
    )
    self.assertEqual(errors, [])

  def test_folded_run_text_does_not_grant_pull_request_write(self):
    errors = self.lint(
        """name: test
on:
  pull_request:
    paths: [src/**]
  workflow_dispatch:
jobs:
  test:
    steps:
      - run: >-
          echo "pull_request:"
          echo "pull-requests: write"
"""
    )
    self.assertEqual(errors, [])

  def test_yaml_parse_failure_is_fatal(self):
    with tempfile.TemporaryDirectory(prefix="furble-ci-workflow-") as root:
      workflow_root = Path(root) / ".github" / "workflows"
      workflow_root.mkdir(parents=True)
      (workflow_root / "broken.yml").write_text(
          "name: broken\non: [\n", encoding="utf-8"
      )
      result = subprocess.run(
          [sys.executable, str(CHECKER_PATH), "--root", root],
          check=False,
          capture_output=True,
          text=True,
      )
      self.assertNotEqual(result.returncode, 0)
      self.assertIn("broken.yml: YAML parse failed", result.stderr)

  def test_pull_request_target_is_rejected_even_without_pull_request(self):
    errors = self.lint(
        """name: unsafe
on:
  pull_request_target:
  workflow_dispatch:
"""
    )
    self.assertIn("uses pull_request_target", errors)

  def test_duplicate_yaml_keys_are_fatal(self):
    with tempfile.NamedTemporaryFile(
        mode="w", suffix=".yml", encoding="utf-8"
    ) as workflow:
      workflow.write("name: test\non: {}\non: {}\n")
      workflow.flush()
      with self.assertRaises(CHECKER.WorkflowParseError):
        CHECKER.lint_workflow(Path(workflow.name))

  def test_block_event_with_flow_configuration_is_structural(self):
    errors = self.lint(
        """name: test
on:
  pull_request: {branches-ignore: [master], paths: [src/**]}
  workflow_dispatch: {}
"""
    )
    self.assertIn("pull_request has a base-branch filter", errors)
    self.assertNotIn("pull_request has no path filter", errors)

  def test_firmware_filter_requires_full_manual_dispatch(self):
    errors = self.lint(
        """name: test
on:
  pull_request:
    paths: [src/**]
  workflow_dispatch:
jobs:
  changes:
    steps:
      - id: filter
        run: |
          firmware_changed=false
          changed_paths="$(git diff --name-only "$PR_BASE_SHA" "$CURRENT_SHA")"
"""
    )
    self.assertIn(
        "firmware change filter does not fully build manual dispatches", errors
    )

  def test_firmware_filter_rejects_multiple_root_fallback(self):
    errors = self.lint(
        """name: test
on:
  pull_request:
    paths: [src/**]
  workflow_dispatch:
jobs:
  changes:
    steps:
      - id: filter
        run: |
          if [[ "$EVENT_NAME" == "workflow_dispatch" ]]; then
            firmware_changed=true
          fi
          base_sha="$(git rev-list --max-parents=0 "$CURRENT_SHA")"
"""
    )
    self.assertIn(
        "firmware change filter can produce multiple base SHAs", errors
    )


if __name__ == "__main__":
  unittest.main()
