"""Regression tests for the stacked-PR workflow trigger checker."""

from importlib.util import module_from_spec, spec_from_file_location
from pathlib import Path
import re
import shutil
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
  def test_release_upload_job_has_scoped_write_permission(self):
    document = CHECKER._load_workflow(ROOT / ".github" / "workflows" / "release.yml")
    self.assertEqual(document["jobs"]["build"]["permissions"], {"contents": "read"})
    self.assertEqual(
        document["jobs"]["release"]["permissions"], {"contents": "write"}
    )

  def test_core_usb_debug_is_not_published_as_ota_manifest(self):
    for workflow_name in ("release.yml", "pages.yml"):
      document = CHECKER._load_workflow(ROOT / ".github" / "workflows" / workflow_name)
      matrix = document["jobs"]["build"]["strategy"]["matrix"]
      self.assertIn(
          {"platform": "m5stack-core", "variant": "-debug"},
          matrix["exclude"],
          workflow_name,
      )
  def test_installer_debug_selector_never_requests_core_manifest(self):
    self.assertIsNotNone(shutil.which("node"), "Node.js is required by this regression")
    installer = (ROOT / "web-installer" / "index.html").read_text(encoding="utf-8")
    scripts = re.findall(r"<script>(.*?)</script>", installer, re.DOTALL)
    script = next(script for script in scripts if "function updateManifest" in script)
    harness = r'''
      const vm = require("vm");
      const listeners = {};
      const state = {
        selected: {value: "m5stick-s3"},
        debug: {
          checked: true,
          disabled: false,
          addEventListener(event, callback) { listeners.debug = callback; }
        },
        button: {manifest: "", classList: {remove() {}}}
      };
      const document = {
        querySelector(selector) {
          return selector === 'input[name="type"]:checked' ? state.selected : state.button;
        },
        querySelectorAll() {
          return {forEach(callback) {
            callback({addEventListener(event, handler) { listeners.board = handler; }});
          }};
        },
        getElementById(id) { return id === "debug" ? state.debug : state.button; }
      };
      const context = {document};
      vm.runInNewContext(SCRIPT, context);
      listeners.board();
      if (state.button.manifest !== "./manifest_m5stick-s3-debug.json" || state.debug.disabled) process.exit(1);
      state.selected = {value: "m5stack-core"};
      listeners.board();
      if (state.button.manifest !== "./manifest_m5stack-core.json" || !state.debug.disabled || state.debug.checked) process.exit(2);
      state.selected = {value: "m5stick-s3"};
      state.debug.checked = true;
      listeners.board();
      if (state.button.manifest !== "./manifest_m5stick-s3-debug.json" || state.debug.disabled) process.exit(3);
      state.debug.checked = false;
      listeners.debug();
      if (state.button.manifest !== "./manifest_m5stick-s3.json") process.exit(4);
      state.selected = null;
      const previous = state.button.manifest;
      listeners.board();
      if (state.button.manifest !== previous) process.exit(5);
    '''
    result = subprocess.run(
        ["node", "-e", "const SCRIPT = process.argv[1];" + harness, script],
        check=False,
        capture_output=True,
        text=True,
    )
    self.assertEqual(result.returncode, 0, result.stderr)

  def lint(self, text: str) -> list[str]:
    with tempfile.NamedTemporaryFile(
        mode="w", suffix=".yml", encoding="utf-8"
    ) as workflow:
      workflow.write(text)
      workflow.flush()
      return CHECKER.lint_workflow(Path(workflow.name))

  def test_current_validation_workflows_pass(self):
    workflow_root = ROOT / ".github" / "workflows"
    workflow_paths = CHECKER._workflow_paths(workflow_root)
    checked = 0
    for path in workflow_paths:
      if "pull_request" in CHECKER.event_blocks(
          path.read_text(encoding="utf-8").splitlines()
      ):
        checked += 1
        self.assertEqual(CHECKER.lint_workflow(path), [], path.name)
    self.assertGreater(checked, 0)

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

  def test_platformio_profile_contract_is_structural(self):
    errors = CHECKER._lint_platformio_ci_contract(
        CHECKER._load_text(
            """name: PlatformIO CI
on:
  workflow_dispatch:
    inputs:
      core_ota_debug:
        type: boolean
        required: false
        default: false
jobs:
  discover:
    env:
      MANDATORY_CORE_DEBUG: m5stack-core-usb-debug
      OPTIONAL_CORE_DEBUG: m5stack-core-debug
  build: {}
"""
        )
    )
    self.assertEqual(errors, [])

  def test_platformio_profile_contract_rejects_unsafe_opt_in(self):
    errors = CHECKER._lint_platformio_ci_contract(
        CHECKER._load_text(
            """name: PlatformIO CI
on:
  workflow_dispatch:
    inputs:
      core_ota_debug:
        type: string
        required: true
        default: true
jobs:
  discover:
    env:
      MANDATORY_CORE_DEBUG: m5stack-core-debug
      OPTIONAL_CORE_DEBUG: m5stack-core-usb-debug
  build:
    continue-on-error: true
"""
        )
    )
    self.assertIn("PlatformIO CI core_ota_debug input must be boolean", errors)
    self.assertIn("PlatformIO CI core_ota_debug input must default to false", errors)
    self.assertIn("PlatformIO CI core_ota_debug input must be optional", errors)
    self.assertIn("PlatformIO CI mandatory Core debug profile is incorrect", errors)
    self.assertIn("PlatformIO CI optional Core debug profile is incorrect", errors)
    self.assertIn("PlatformIO CI firmware build must fail strictly", errors)


if __name__ == "__main__":
  unittest.main()
