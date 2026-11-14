"""Regression tests for the pull request hygiene scanner."""

from importlib.util import module_from_spec, spec_from_file_location
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
TOOL_PATH = ROOT / "tools" / "check_pr_hygiene.py"
SPEC = spec_from_file_location("check_pr_hygiene", TOOL_PATH)
assert SPEC is not None and SPEC.loader is not None
CHECKER = module_from_spec(SPEC)
sys.modules[SPEC.name] = CHECKER
SPEC.loader.exec_module(CHECKER)

# Build the sample workstation roots from pieces so this test file does not
# fail its own absolute-path check when it is added in a pull request.
USERS_ROOT = "/" + "Users/"
HOME_ROOT = "/home/"
PRIVATE_ROOT = "/private" + "/var/folders/"
TILDE_B = "~" + "/b/"


def make_diff(path, added=(), *, removed=(), status="modified",
              old_path=None, start=1):
  """Build one unified diff block with precise added line numbers."""
  source_path = old_path or path
  lines = [f"diff --git a/{source_path} b/{path}"]
  if status == "added":
    lines.extend(["new file mode 100644", "--- /dev/null", f"+++ b/{path}"])
  elif status == "deleted":
    lines.extend(["deleted file mode 100644", f"--- a/{source_path}",
                  "+++ /dev/null"])
  elif status == "renamed":
    lines.extend([f"rename from {source_path}", f"rename to {path}"])
  else:
    lines.extend([f"--- a/{source_path}", f"+++ b/{path}"])

  if added or removed:
    old_count = len(removed)
    new_count = len(added)
    if status == "added":
      old_start = 0
      new_start = start
    elif status == "deleted":
      old_start = start
      new_start = 0
    else:
      old_start = start
      new_start = start
    lines.append(
        f"@@ -{old_start},{old_count} +{new_start},{new_count} @@")
    lines.extend(f"-{text}" for text in removed)
    lines.extend(f"+{text}" for text in added)
  return "\n".join(lines) + "\n"


def join_diffs(*diffs):
  return "".join(diff if diff.endswith("\n") else diff + "\n" for diff in diffs)


def prepare_repo(root, files=None):
  (root / "plans").mkdir(parents=True, exist_ok=True)
  (root / "plans" / "README.md").write_text("# plans\n", encoding="utf-8")
  for path, text in (files or {}).items():
    target = root / path
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(text, encoding="utf-8")


def findings_for(diff_text, files=None):
  with tempfile.TemporaryDirectory() as directory:
    root = Path(directory)
    prepare_repo(root, files)
    changes = CHECKER.parse_diff(diff_text)
    return CHECKER.collect_findings(changes, root)


def rules(findings, severity=None):
  return [
      finding.rule for finding in findings
      if severity is None or finding.severity == severity
  ]


class DiffParserTest(unittest.TestCase):
  def test_added_line_numbers_are_precise(self):
    diff = make_diff("src/Camera.cpp", ["one", "two"], start=41)
    changes = CHECKER.parse_diff(diff)
    self.assertEqual(len(changes), 1)
    self.assertEqual(changes[0].path, "src/Camera.cpp")
    self.assertEqual([line.line for line in changes[0].added], [41, 42])
    self.assertEqual(changes[0].added[1].text, "two")

  def test_rename_and_delete_are_recognized(self):
    diff = join_diffs(
        make_diff("src/New.cpp", status="renamed", old_path="src/Old.cpp"),
        make_diff("src/Gone.cpp", ["removed"], status="deleted"),
    )
    changes = CHECKER.parse_diff(diff)
    self.assertEqual([change.status for change in changes],
                     ["renamed", "deleted"])
    self.assertEqual(changes[0].old_path, "src/Old.cpp")
    self.assertIsNone(changes[1].path)

  def test_new_binary_file_is_recognized(self):
    diff = (
        "diff --git a/src/blob.so b/src/blob.so\n"
        "new file mode 100644\n"
        "Binary files /dev/null and b/src/blob.so differ\n"
    )
    changes = CHECKER.parse_diff(diff)
    self.assertEqual(changes[0].status, "added")
    self.assertEqual(changes[0].path, "src/blob.so")


class CleanCaseTest(unittest.TestCase):
  def test_clean_diff_has_no_findings(self):
    diff = make_diff("src/Camera.cpp", ["  int result = camera.connect();"])
    self.assertEqual(findings_for(diff), [])


class AbsolutePathTest(unittest.TestCase):
  def test_workstation_paths_fail_in_source(self):
    diff = join_diffs(
        make_diff("src/power.json", [USERS_ROOT + "alice/git/furble/b.json"]),
        make_diff("src/settings.json", [HOME_ROOT + "alice/work/settings"]),
        make_diff("src/win.ini", ["C:" + "\\Users" + "\\alice\\furble"]),
        make_diff("src/temp.log", [PRIVATE_ROOT + "ab/cd/T/report.txt"]),
        make_diff("src/evidence.txt", [TILDE_B + "scratch/baseline.json"]),
    )
    findings = findings_for(diff)
    self.assertEqual(len(findings), 5)
    self.assertEqual(rules(findings, CHECKER.FAIL), ["absolute-path"] * 5)

  def test_json_and_configuration_fail_by_default(self):
    diff = join_diffs(
        make_diff("data/power-baseline.json",
                  ['  "path": "' + USERS_ROOT + 'alice/build"']),
        make_diff("sim/scenarios/power.json",
                  ['  "root": "' + HOME_ROOT + 'alice/furble"']),
    )
    findings = findings_for(diff)
    self.assertEqual(len(findings), 2)
    self.assertEqual(rules(findings, CHECKER.FAIL), ["absolute-path"] * 2)

  def test_backticked_evidence_in_plans_and_docs_is_allowed(self):
    diff = join_diffs(
        make_diff("plans/implemented/13-auto-off-low-batt.md",
                  ["  `" + USERS_ROOT + "alice/git/furble/.pio` absent."]),
        make_diff("docs/power.md",
                  ["Evidence: `" + PRIVATE_ROOT + "ab/T/report.json`."]),
    )
    self.assertEqual(findings_for(diff), [])

  def test_unquoted_evidence_and_non_markdown_still_fail(self):
    diff = join_diffs(
        make_diff("plans/evidence.md",
                  ["Recorded at " + USERS_ROOT + "alice/git/furble."]),
        make_diff("plans/evidence.txt",
                  ["  `" + USERS_ROOT + "alice/git/furble`"]),
    )
    findings = findings_for(diff)
    self.assertEqual(len(findings), 2)
    self.assertEqual(rules(findings, CHECKER.FAIL), ["absolute-path"] * 2)

  def test_line_number_points_at_the_added_line(self):
    diff = make_diff("src/config.cpp", ["clean", USERS_ROOT + "alice/x"],
                     start=17)
    findings = findings_for(diff)
    self.assertEqual(findings[0].location, "src/config.cpp:18")


class WhitespaceTest(unittest.TestCase):
  def test_trailing_whitespace_fails(self):
    findings = findings_for(make_diff("src/a.cpp", ["int value = 1;  "]))
    self.assertEqual(len(findings), 1)
    self.assertIn("trailing whitespace", findings[0].message)
    self.assertEqual(findings[0].location, "src/a.cpp:1")

  def test_space_before_tab_fails(self):
    for line in ("  \tint value = 1;", "\t \tint value = 1;"):
      findings = findings_for(make_diff("src/a.cpp", [line]))
      self.assertEqual(len(findings), 1, line)
      self.assertIn("space before tab", findings[0].message)

  def test_crlf_line_ending_fails(self):
    findings = findings_for(make_diff("docs/note.md", ["recorded\r"]))
    self.assertEqual(len(findings), 1)
    self.assertIn("CRLF", findings[0].message)

  def test_clean_indentation_passes(self):
    findings = findings_for(make_diff("src/a.cpp", ["\tint value = 1;"]))
    self.assertEqual(findings, [])


class ArtifactTest(unittest.TestCase):
  def test_generated_and_binary_paths_are_rejected(self):
    paths = (
        "src/object.o",
        "src/libfurble.a",
        "src/libfurble.dylib",
        "src/libfurble.so",
        "src/coverage.profraw",
        "src/module.pyc",
        "src/.DS_Store",
        "src/Furble.cpp.swp",
        "src/.#Furble.cpp",
        "src/Furble.cpp~",
        "src/__pycache__/module.pyc",
    )
    for path in paths:
      self.assertIsNotNone(CHECKER.artifact_reason(path), path)

  def test_protocol_fixtures_and_docs_images_are_allowed(self):
    for path in ("tests/protocol/golden/settings/new.json",
                 "tests/protocol/golden/trigger/frame.bin",
                 "docs/img/new-page.png"):
      self.assertIsNone(CHECKER.artifact_reason(path), path)
    self.assertIsNotNone(CHECKER.artifact_reason("docs/img/payload.so"))

  def test_added_artifact_fails_but_deleted_artifact_passes(self):
    findings = findings_for(make_diff("src/generated.o", ["binary"]))
    self.assertEqual(len(findings), 1)
    self.assertEqual(findings[0].rule, "artifact")
    self.assertEqual(findings[0].location, "src/generated.o:1")
    self.assertEqual(
        findings_for(make_diff("src/old.o", ["binary"], status="deleted")), [])

  def test_allowed_fixture_diff_has_no_artifact_finding(self):
    diff = join_diffs(
        make_diff("tests/protocol/golden/settings/new.json", ["{}"]),
        make_diff("docs/img/new-page.png", ["image"]),
    )
    self.assertEqual(findings_for(diff), [])


class DebugLeftoverTest(unittest.TestCase):
  def test_production_markers_and_diagnostics_fail(self):
    diff = join_diffs(
        make_diff("src/Camera.cpp", ["  // TODO: retry the connection"]),
        make_diff("include/Camera.h", ["  std::cerr << \"DEBUG connect\";"]),
        make_diff("lib/furble/FurbleGPS.cpp", ['  printf("DEBUG gps\\n");']),
        make_diff("lib/furble/FurbleUI.js", ["console.log(state);"]),
        make_diff("src/FurbleUI.cpp", ["  // FIXME: page lifetime"]),
        make_diff("include/FurbleGPS.h", ["  // HACK: shared buffer"]),
    )
    findings = findings_for(diff)
    failures = [finding for finding in findings if finding.severity == CHECKER.FAIL]
    self.assertEqual(len(failures), 6)
    self.assertEqual(rules(failures), ["debug-leftover"] * 6)
    self.assertTrue(any("TODO" in finding.message for finding in failures))

  def test_tests_and_test_doubles_are_exempt(self):
    diff = join_diffs(
        make_diff("tests/unit/FurbleCameraTest.cpp", ["  // TODO: add case"]),
        make_diff("lib/testing/nimble/MockNimBLE.cpp", ["  // FIXME: mock"]),
        make_diff("lib/furble/FurbleTestSync.h", ["  // HACK: test hook"]),
        make_diff("sim/scenarios/test-double.cpp", ["console.log('debug');"]),
    )
    self.assertEqual(findings_for(diff), [])


class SettingsCouplingTest(unittest.TestCase):
  SAVE_LINE = "          save<uint8_t>(setting.type, 128);"

  def test_settings_default_without_docs_and_test_fails(self):
    findings = findings_for(
        make_diff("src/FurbleSettings.cpp", [self.SAVE_LINE], start=590))
    failures = [finding for finding in findings if finding.severity == CHECKER.FAIL]
    self.assertEqual(len(failures), 3)
    self.assertEqual(rules(failures), ["doc-coupling"] * 3)
    self.assertEqual(failures[0].location, "src/FurbleSettings.cpp:590")
    messages = " ".join(finding.message for finding in failures)
    self.assertIn("docs/settings-and-controls.md", messages)
    self.assertIn("docs/wiki/Settings-Reference.md", messages)
    self.assertIn("tests/", messages)

  def test_settings_default_with_docs_and_test_passes(self):
    diff = join_diffs(
        make_diff("src/FurbleSettings.cpp", [self.SAVE_LINE]),
        make_diff("docs/settings-and-controls.md", ["Updated default."]),
        make_diff("docs/wiki/Settings-Reference.md", ["Updated default."]),
        make_diff("tests/test_settings.py", ["  pass"]),
    )
    self.assertEqual(findings_for(diff), [])

  def test_one_missing_required_doc_fails(self):
    diff = join_diffs(
        make_diff("src/FurbleSettings.cpp", [self.SAVE_LINE]),
        make_diff("docs/settings-and-controls.md", ["Updated default."]),
        make_diff("tests/test_settings.py", ["  pass"]),
    )
    findings = findings_for(diff)
    self.assertEqual(len(findings), 1)
    self.assertEqual(findings[0].severity, CHECKER.FAIL)
    self.assertIn("docs/wiki/Settings-Reference.md", findings[0].message)

  def test_removed_default_warns_instead_of_failing(self):
    findings = findings_for(
        make_diff("src/FurbleSettings.cpp", removed=[self.SAVE_LINE]))
    self.assertEqual(rules(findings, CHECKER.FAIL), [])
    self.assertEqual(len(findings), 3)
    self.assertEqual(rules(findings, CHECKER.WARN), ["doc-coupling"] * 3)

  def test_deleted_or_renamed_settings_file_warns(self):
    deleted = findings_for(
        make_diff("src/FurbleSettings.cpp", ["x"], status="deleted"))
    renamed = findings_for(
        make_diff("src/Settings.cpp", status="renamed",
                  old_path="src/FurbleSettings.cpp"))
    self.assertEqual(rules(deleted, CHECKER.FAIL), [])
    self.assertEqual(rules(renamed, CHECKER.FAIL), [])
    self.assertEqual(rules(deleted, CHECKER.WARN), ["doc-coupling"])
    self.assertEqual(rules(renamed, CHECKER.WARN), ["doc-coupling"])

  def test_comment_only_settings_change_warns(self):
    findings = findings_for(make_diff(
        "src/FurbleSettings.cpp", ["  // The default stays 128."]))
    self.assertEqual(rules(findings, CHECKER.FAIL), [])
    self.assertEqual(rules(findings, CHECKER.WARN), ["doc-coupling"])

  def test_whitespace_only_settings_change_warns(self):
    findings = findings_for(make_diff(
        "src/FurbleSettings.cpp",
        ["    " + self.SAVE_LINE.lstrip()],
        removed=[self.SAVE_LINE],
    ))
    self.assertEqual(rules(findings, CHECKER.FAIL), [])
    self.assertEqual(rules(findings, CHECKER.WARN), ["doc-coupling"] * 3)

  def test_deleted_required_doc_does_not_satisfy_the_rule(self):
    diff = join_diffs(
        make_diff("src/FurbleSettings.cpp", [self.SAVE_LINE]),
        make_diff("docs/settings-and-controls.md", ["old"], status="deleted"),
        make_diff("docs/wiki/Settings-Reference.md", ["Updated default."]),
        make_diff("tests/test_settings.py", ["  pass"]),
    )
    findings = findings_for(diff)
    self.assertEqual(len(findings), 1)
    self.assertEqual(findings[0].severity, CHECKER.FAIL)
    self.assertIn("docs/settings-and-controls.md", findings[0].message)


class ConsoleCouplingTest(unittest.TestCase):
  CONSOLE_LINE = '    console.printf("shutter %d\\n", count);'

  def test_console_string_without_docs_fails(self):
    findings = findings_for(make_diff("src/FurbleConsole.cpp", [self.CONSOLE_LINE]))
    failures = [finding for finding in findings if finding.severity == CHECKER.FAIL]
    self.assertEqual(len(failures), 2)
    messages = " ".join(finding.message for finding in failures)
    self.assertIn("docs/console-commands.md", messages)
    self.assertIn("docs/wiki/Console-Commands.md", messages)

  def test_console_string_with_docs_passes(self):
    diff = join_diffs(
        make_diff("src/FurbleConsole.cpp", [self.CONSOLE_LINE]),
        make_diff("docs/console-commands.md", ["shutter"]),
        make_diff("docs/wiki/Console-Commands.md", ["shutter"]),
    )
    self.assertEqual(findings_for(diff), [])

  def test_code_around_an_unchanged_console_string_warns(self):
    findings = findings_for(make_diff(
        "src/FurbleConsole.cpp",
        ['  if ((argc < 3) || (strcmp(argv[1], "trace") != 0)) {'],
        removed=['  if ((argc < 3) || strcmp(argv[1], "trace")) {']))
    self.assertEqual(rules(findings, CHECKER.FAIL), [])
    self.assertEqual(rules(findings, CHECKER.WARN), ["doc-coupling"] * 2)

  def test_changed_console_string_with_code_still_fails(self):
    findings = findings_for(make_diff(
        "src/FurbleConsole.cpp",
        ['  if ((argc < 3) || (strcmp(argv[1], "traces") != 0)) {'],
        removed=['  if ((argc < 3) || strcmp(argv[1], "trace")) {']))
    self.assertEqual(rules(findings, CHECKER.FAIL), ["doc-coupling"] * 2)

  def test_swapped_console_strings_still_fail(self):
    # same literal set, but each command now prints the other's text
    findings = findings_for(make_diff(
        "src/FurbleConsole.cpp",
        ['    puts("on");', '    puts("off");'],
        removed=['    puts("off");', '    puts("on");']))
    self.assertEqual(rules(findings, CHECKER.FAIL), ["doc-coupling"] * 2)

  def test_console_string_moved_elsewhere_still_fails(self):
    # the same line removed near the top and added further down
    diff = "\n".join([
        "diff --git a/src/FurbleConsole.cpp b/src/FurbleConsole.cpp",
        "--- a/src/FurbleConsole.cpp", "+++ b/src/FurbleConsole.cpp",
        "@@ -10,1 +10,0 @@", '-    puts("ok");',
        "@@ -200,0 +199,1 @@", '+    puts("ok");']) + "\n"
    findings = findings_for(diff)
    self.assertEqual(rules(findings, CHECKER.FAIL), ["doc-coupling"] * 2)

  def test_removed_console_string_warns(self):
    findings = findings_for(
        make_diff("src/FurbleConsole.cpp", removed=[self.CONSOLE_LINE]))
    self.assertEqual(rules(findings, CHECKER.FAIL), [])
    self.assertEqual(rules(findings, CHECKER.WARN), ["doc-coupling"] * 2)

  def test_comment_only_or_unmapped_console_change_warns(self):
    comment = findings_for(
        make_diff("src/FurbleConsole.cpp", ["  // Clarify the parser."]))
    include = findings_for(
        make_diff("src/FurbleConsole.cpp", ['#include "FurbleConsole.h"']))
    reindented = findings_for(make_diff(
        "src/FurbleConsole.cpp",
        ["  " + self.CONSOLE_LINE.lstrip()],
        removed=[self.CONSOLE_LINE],
    ))
    self.assertEqual(rules(comment, CHECKER.FAIL), [])
    self.assertEqual(rules(comment, CHECKER.WARN), ["doc-coupling"])
    self.assertEqual(rules(include, CHECKER.FAIL), [])
    self.assertEqual(rules(include, CHECKER.WARN), ["doc-coupling"])
    self.assertEqual(rules(reindented, CHECKER.FAIL), [])
    self.assertEqual(rules(reindented, CHECKER.WARN), ["doc-coupling"] * 2)

  def test_deleted_console_file_warns(self):
    findings = findings_for(
        make_diff("src/FurbleConsole.cpp", ["x"], status="deleted"))
    self.assertEqual(rules(findings, CHECKER.FAIL), [])
    self.assertEqual(rules(findings, CHECKER.WARN), ["doc-coupling"])


class UICouplingTest(unittest.TestCase):
  PAGE_LINE = "  lv_obj_t *page = lv_obj_create(parent);"

  def test_page_change_without_docs_or_scenario_fails(self):
    findings = findings_for(make_diff("src/FurbleUI.cpp", [self.PAGE_LINE]))
    self.assertEqual(len(findings), 1)
    self.assertEqual(findings[0].severity, CHECKER.FAIL)
    self.assertEqual(findings[0].rule, "doc-coupling")
    self.assertIn("docs/ui-walkthrough.md", findings[0].message)

  def test_page_change_with_walkthrough_passes(self):
    diff = join_diffs(
        make_diff("src/FurbleUI.cpp", [self.PAGE_LINE]),
        make_diff("docs/ui-walkthrough.md", ["Updated screenshot."]),
    )
    self.assertEqual(findings_for(diff), [])

  def test_page_change_with_simulator_scenario_passes(self):
    diff = join_diffs(
        make_diff("src/FurbleUI.cpp", ["  query = Query::Battery;"]),
        make_diff("sim/scenarios/manifest.json", ['{"scenarios": []}']),
    )
    self.assertEqual(findings_for(diff), [])

  def test_layout_only_or_removed_page_change_warns(self):
    layout = findings_for(
        make_diff("src/FurbleUI.cpp", ["  lv_obj_set_width(row, 100);"]))
    removed = findings_for(
        make_diff("src/FurbleUI.cpp", removed=[self.PAGE_LINE]))
    reindented = findings_for(make_diff(
        "src/FurbleUI.cpp",
        ["  " + self.PAGE_LINE.lstrip()],
        removed=[self.PAGE_LINE],
    ))
    self.assertEqual(rules(layout, CHECKER.FAIL), [])
    self.assertEqual(rules(layout, CHECKER.WARN), ["doc-coupling"])
    self.assertEqual(rules(removed, CHECKER.FAIL), [])
    self.assertEqual(rules(removed, CHECKER.WARN), ["doc-coupling"])
    self.assertEqual(rules(reindented, CHECKER.FAIL), [])
    self.assertEqual(rules(reindented, CHECKER.WARN), ["doc-coupling"])

  def test_renamed_ui_file_warns(self):
    findings = findings_for(make_diff(
        "src/UI.cpp", status="renamed", old_path="src/FurbleUI.cpp"))
    self.assertEqual(rules(findings, CHECKER.FAIL), [])
    self.assertEqual(rules(findings, CHECKER.WARN), ["doc-coupling"])


class PlanLinkTest(unittest.TestCase):
  def test_missing_plan_target_fails_with_line_number(self):
    with tempfile.TemporaryDirectory() as directory:
      root = Path(directory)
      (root / "plans").mkdir()
      (root / "plans" / "ok.md").write_text("# ok\n", encoding="utf-8")
      (root / "plans" / "README.md").write_text(
          "# plans\n"
          "- [ok](ok.md)\n"
          "- [missing](missing.md)\n"
          "- [anchor](ok.md#state)\n"
          "- [external](https://example.com/missing.md)\n"
          "- [root relative](plans/ok.md)\n",
          encoding="utf-8",
      )
      findings = CHECKER.check_plan_links(root)
      self.assertEqual(len(findings), 1)
      self.assertEqual(findings[0].severity, CHECKER.FAIL)
      self.assertEqual(findings[0].location, "plans/README.md:3")
      self.assertIn("missing.md", findings[0].message)

  def test_missing_plan_index_warns(self):
    with tempfile.TemporaryDirectory() as directory:
      findings = CHECKER.check_plan_links(Path(directory))
      self.assertEqual(len(findings), 1)
      self.assertEqual(findings[0].severity, CHECKER.WARN)


class CommandLineTest(unittest.TestCase):
  def run_tool(self, *arguments):
    return subprocess.run(
        [sys.executable, str(TOOL_PATH), *arguments],
        capture_output=True,
        text=True,
        check=False,
    )

  def test_diff_file_failure_returns_one_and_names_line(self):
    with tempfile.TemporaryDirectory() as directory:
      root = Path(directory)
      prepare_repo(root)
      diff_path = root / "change.diff"
      diff_path.write_text(
          make_diff("src/config.cpp", [USERS_ROOT + "alice/furble"]),
          encoding="utf-8",
      )
      result = self.run_tool(
          "--repo", str(root), "--diff-file", str(diff_path))
      self.assertEqual(result.returncode, 1)
      self.assertIn("FAIL", result.stdout)
      self.assertIn("src/config.cpp:1", result.stdout)
      self.assertIn("absolute-path", result.stdout)

  def test_clean_diff_file_returns_zero(self):
    with tempfile.TemporaryDirectory() as directory:
      root = Path(directory)
      prepare_repo(root)
      diff_path = root / "clean.diff"
      diff_path.write_text(
          make_diff("src/Camera.cpp", ["  int value = 1;"]),
          encoding="utf-8",
      )
      result = self.run_tool(
          "--repo", str(root), "--diff-file", str(diff_path))
      self.assertEqual(result.returncode, 0)
      self.assertIn("PASS", result.stdout)

  def test_revision_range_is_used(self):
    with tempfile.TemporaryDirectory() as directory:
      root = Path(directory)
      prepare_repo(root, {"src/Camera.cpp": "int value = 0;\n"})
      commands = (
          ["git", "init", "-q"],
          ["git", "add", "."],
          ["git", "-c", "user.name=Test", "-c", "user.email=test@example.com",
           "commit", "-q", "-m", "base"],
      )
      for command in commands:
        subprocess.run(command, cwd=root, check=True, capture_output=True)
      (root / "src" / "Camera.cpp").write_text(
          USERS_ROOT + "alice/git/furble\n", encoding="utf-8")
      result = self.run_tool("--repo", str(root), "HEAD")
      self.assertEqual(result.returncode, 1)
      self.assertIn("absolute-path", result.stdout)
      self.assertIn("src/Camera.cpp", result.stdout)

  def test_bad_inputs_return_two(self):
    with tempfile.TemporaryDirectory() as directory:
      root = Path(directory)
      prepare_repo(root)
      missing = self.run_tool(
          "--repo", str(root), "--diff-file", str(root / "absent.diff"))
      no_repo = self.run_tool("--repo", str(root / "absent"))
      bad_revision = self.run_tool("--repo", str(root), "origin/master...HEAD")
      self.assertEqual(missing.returncode, 2)
      self.assertEqual(no_repo.returncode, 2)
      self.assertEqual(bad_revision.returncode, 2)


if __name__ == "__main__":
  unittest.main()
