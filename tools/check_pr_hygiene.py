#!/usr/bin/env python3
"""Block review hygiene mistakes in a pull request diff before review.

The scanner reads a git revision range or a unified diff and inspects added
lines only. It exists because review rounds were spent on avoidable defects:
an absolute workstation path leaked into a committed power baseline, whitespace
errors failed CI after review, generated artifacts landed in the source tree,
debug leftovers reached production code, behavior changes shipped without their
docs, and the plan index linked to a deleted document. Each check function
names the mistake it prevents.

Usage:
  python3 tools/check_pr_hygiene.py
  python3 tools/check_pr_hygiene.py --repo . origin/master...HEAD
  python3 tools/check_pr_hygiene.py --diff-file pull-request.diff

Exit status is 0 when only warnings are present and 1 when any failure is
present. A malformed invocation or unreadable diff exits 2.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass, field
import re
from pathlib import Path
import shlex
import subprocess
import sys

DEFAULT_REVISION = "origin/master...HEAD"

FAIL = "FAIL"
WARN = "WARN"


@dataclass
class DiffLine:
  text: str
  line: int


@dataclass
class ChangedFile:
  path: str | None
  old_path: str | None
  status: str
  added: list[DiffLine] = field(default_factory=list)
  removed: list[DiffLine] = field(default_factory=list)


@dataclass
class Finding:
  severity: str
  rule: str
  location: str
  message: str


@dataclass
class _FileBuilder:
  """Accumulate one `diff --git` block before it is finalized."""

  old_path: str | None = None
  new_path: str | None = None
  old_seen: bool = False
  new_seen: bool = False
  rename_from: str | None = None
  rename_to: str | None = None
  is_new: bool = False
  is_deleted: bool = False
  added: list[DiffLine] = field(default_factory=list)
  removed: list[DiffLine] = field(default_factory=list)

  def finish(self) -> ChangedFile | None:
    old = self.rename_from or (self.old_path if self.old_seen else None)
    new = self.rename_to or (self.new_path if self.new_seen else None)
    if self.is_new:
      old = None
    if self.is_deleted:
      new = None
    if self.rename_from and self.rename_to:
      status = "renamed"
    elif old is None and new is not None:
      status = "added"
    elif new is None and old is not None:
      status = "deleted"
    else:
      status = "modified"
    if new is None and old is None:
      return None
    return ChangedFile(path=new, old_path=old, status=status,
                       added=self.added, removed=self.removed)


# Absolute workstation paths are not portable and leaked into a committed
# power baseline during review. The tilde-b form also records a local build
# path. These are only allowed in Markdown evidence when the whole path is
# inside a code span, because that is an explicitly recorded observation, not
# executable configuration.
TILDE_B_ROOT = "~" + "/b/"
ABSOLUTE_PATH_PATTERNS = (
    ("macOS user path", re.compile(r"(?<![\w.])/Users/[A-Za-z0-9._-]+")),
    ("macOS temporary path",
     re.compile(r"(?<![\w.])/private/var/folders/[A-Za-z0-9._-]+")),
    ("Windows user path",
     re.compile(r"(?i)(?<![\w])[A-Z]:\\{1,2}Users\\{1,2}[A-Za-z0-9._-]+")),
    ("Linux home path", re.compile(r"(?<![\w.])/home/[A-Za-z0-9._-]+")),
    ("tilde-b evidence path", re.compile(re.escape(TILDE_B_ROOT))),
)

# `git diff --check` rejects these. A late CI failure costs another review
# round, so catch them before review starts.
TRAILING_WHITESPACE_RE = re.compile(r"[ \t]+$")
SPACE_BEFORE_TAB_RE = re.compile(r"^[ \t]* \t")

# Generated and binary artifacts do not belong in source or docs. Protocol
# fixtures and documentation images are the two intentional exceptions.
ARTIFACT_SUFFIXES = {
    ".o", ".a", ".dylib", ".so", ".profraw", ".pyc", ".pyo", ".class",
    ".swp", ".swo", ".swx", ".orig", ".rej",
}
ARTIFACT_NAMES = {".DS_Store", "Thumbs.db"}
ARTIFACT_NAME_PREFIXES = (".#",)
ARTIFACT_ALLOWED_FIXTURE_PREFIX = "tests/protocol/golden/"
ARTIFACT_ALLOWED_IMAGE_PREFIX = "docs/img/"
ARTIFACT_IMAGE_SUFFIXES = {
    ".png", ".jpg", ".jpeg", ".gif", ".webp", ".svg", ".bmp", ".ico",
}

# Debug leftovers in production code are review blockers. They are ignored in
# tests and test doubles, where markers and logging are legitimate.
DEBUG_MARKER_RE = re.compile(r"\b(?:TODO|FIXME|XXX|HACK)\b")
DEBUG_PRINT_RE = re.compile(r'printf\s*\(\s*"DEBUG')
DEBUG_CERR_RE = re.compile(r'std::cerr\s*<<\s*"DEBUG')
BARE_CONSOLE_LOG_RE = re.compile(r"(?<![\w.$])console\.log\s*\(")
DEBUG_PATTERNS = (
    (DEBUG_MARKER_RE, "debug or follow-up marker"),
    (DEBUG_PRINT_RE, 'printf("DEBUG diagnostic'),
    (DEBUG_CERR_RE, 'std::cerr << "DEBUG diagnostic'),
    (BARE_CONSOLE_LOG_RE, "bare console.log"),
)
PRODUCTION_SOURCE_PREFIXES = ("src/", "include/", "lib/")
TEST_PATH_PARTS = {
    "test", "tests", "testing", "testdata", "mock", "mocks", "fake", "fakes",
}
TEST_NAME_HINTS = ("test", "mock", "fake")

# Settings defaults, console commands, and UI pages are user-facing contracts.
# A behavior change without its docs is incomplete and should not merge.
SETTINGS_FILE = "src/FurbleSettings.cpp"
SETTINGS_DEFAULT_RE = re.compile(r"save\s*<[^<>]+>\s*\(\s*setting\.type\s*,")
SETTINGS_REQUIRED_DOCS = (
    "docs/settings-and-controls.md",
    "docs/wiki/Settings-Reference.md",
)
CONSOLE_FILE = "src/FurbleConsole.cpp"
CONSOLE_REQUIRED_DOCS = (
    "docs/console-commands.md",
    "docs/wiki/Console-Commands.md",
)
UI_FILE = "src/FurbleUI.cpp"
UI_DOC = "docs/ui-walkthrough.md"
UI_SCENARIO_PREFIX = "sim/scenarios/"
STRING_LITERAL_RE = re.compile(r'"(?:[^"\\]|\\.)*"')
UI_CHANGE_RE = re.compile(r"\b(?:[Pp]age|[Qq]uery)\w*")

# The plan index is the entry point for open work. A link to a missing plan
# sends the next reviewer to a dead end.
MARKDOWN_LINK_RE = re.compile(r"\[[^\]]*\]\(([^)\s]+)\)")
EXTERNAL_LINK_PREFIXES = ("http://", "https://", "mailto:", "#")

HUNK_RE = re.compile(r"^@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@")


def parse_diff(text: str) -> list[ChangedFile]:
  """Parse a unified diff and keep added and removed lines with numbers."""
  files: list[ChangedFile] = []
  builder: _FileBuilder | None = None
  in_hunk = False
  old_remaining = 0
  new_remaining = 0
  old_number = 0
  new_number = 0

  def flush() -> None:
    nonlocal builder
    if builder is not None:
      change = builder.finish()
      if change is not None:
        files.append(change)
      builder = None

  for raw in text.split("\n"):
    if in_hunk and builder is not None:
      if old_remaining <= 0 and new_remaining <= 0:
        in_hunk = False
      elif raw.startswith("\\ No newline at end of file"):
        continue
      else:
        marker = raw[:1]
        if marker == "+":
          builder.added.append(DiffLine(raw[1:], new_number))
          new_number += 1
          new_remaining -= 1
          continue
        if marker == "-":
          builder.removed.append(DiffLine(raw[1:], old_number))
          old_number += 1
          old_remaining -= 1
          continue
        if marker == " ":
          old_number += 1
          new_number += 1
          old_remaining -= 1
          new_remaining -= 1
          continue
        in_hunk = False

    if raw.startswith("diff --git "):
      flush()
      builder = _FileBuilder()
      _seed_paths_from_git_header(builder, raw)
      continue
    if builder is None:
      continue
    if raw.startswith("@@"):
      match = HUNK_RE.match(raw)
      if match is None:
        continue
      old_start = int(match.group(1))
      old_remaining = int(match.group(2) or "1")
      new_start = int(match.group(3))
      new_remaining = int(match.group(4) or "1")
      old_number = old_start
      new_number = new_start
      # A zero count means the side has no lines. Keep the cursor stable.
      if old_remaining == 0:
        old_number = old_start
      if new_remaining == 0:
        new_number = new_start
      in_hunk = True
      continue
    if raw.startswith("--- "):
      builder.old_seen = True
      builder.old_path = _diff_path(raw[4:], "a/")
      continue
    if raw.startswith("+++ "):
      builder.new_seen = True
      builder.new_path = _diff_path(raw[4:], "b/")
      continue
    if raw.startswith("new file mode"):
      builder.is_new = True
      continue
    if raw.startswith("deleted file mode"):
      builder.is_deleted = True
      continue
    if raw.startswith("rename from "):
      builder.rename_from = _unquote_git_path(raw[len("rename from "):])
      continue
    if raw.startswith("rename to "):
      builder.rename_to = _unquote_git_path(raw[len("rename to "):])
      continue
    if raw.startswith("Binary files "):
      _seed_paths_from_binary_header(builder, raw)
      continue

  flush()
  return files


def _seed_paths_from_git_header(builder: _FileBuilder, raw: str) -> None:
  try:
    tokens = shlex.split(raw[len("diff --git "):])
  except ValueError:
    tokens = []
  if len(tokens) == 2:
    builder.old_path = _strip_prefix(tokens[0], "a/")
    builder.new_path = _strip_prefix(tokens[1], "b/")
    builder.old_seen = True
    builder.new_seen = True


def _seed_paths_from_binary_header(builder: _FileBuilder, raw: str) -> None:
  body = raw[len("Binary files "):]
  if body.endswith(" differ"):
    body = body[:-len(" differ")]
  old, separator, new = body.partition(" and ")
  if not separator:
    return
  if old == "/dev/null":
    builder.is_new = True
    builder.old_seen = True
  else:
    builder.old_seen = True
    builder.old_path = _strip_prefix(_unquote_git_path(old), "a/")
  if new == "/dev/null":
    builder.is_deleted = True
    builder.new_seen = True
  else:
    builder.new_seen = True
    builder.new_path = _strip_prefix(_unquote_git_path(new), "b/")


def _diff_path(raw: str, prefix: str) -> str | None:
  token = raw.split("\t", 1)[0].strip()
  if token == "/dev/null":
    return None
  return _strip_prefix(_unquote_git_path(token), prefix)


def _strip_prefix(path: str, prefix: str) -> str:
  return path[len(prefix):] if path.startswith(prefix) else path


def _unquote_git_path(token: str) -> str:
  if len(token) < 2 or not token.startswith('"') or not token.endswith('"'):
    return token
  body = token[1:-1]
  escapes = {
      "\\\\": "\\", '\\"': '"', "\\a": "\a", "\\b": "\b", "\\f": "\f",
      "\\n": "\n", "\\r": "\r", "\\t": "\t", "\\v": "\v",
  }
  output: list[str] = []
  index = 0
  while index < len(body):
    if body[index] == "\\" and index + 1 < len(body):
      octal = body[index + 1:index + 4]
      if len(octal) == 3 and all(char in "01234567" for char in octal):
        output.append(chr(int(octal, 8)))
        index += 4
        continue
      pair = body[index:index + 2]
      if pair in escapes:
        output.append(escapes[pair])
        index += 2
        continue
    output.append(body[index])
    index += 1
  return "".join(output)


def check_absolute_paths(files: list[ChangedFile]) -> list[Finding]:
  """Reject workstation paths in added lines.

  Prevents the committed power baseline mistake, where a local checkout path
  made the evidence non-portable. Markdown under plans/ and docs/ may record a
  path when the full path is inside a code span.
  """
  findings: list[Finding] = []
  for change in files:
    path = change.path or change.old_path or "<unknown>"
    for line in change.added:
      for label, pattern in ABSOLUTE_PATH_PATTERNS:
        for match in pattern.finditer(line.text):
          if _is_recorded_evidence(path, line.text, match.group(0)):
            continue
          findings.append(Finding(
              FAIL, "absolute-path", f"{path}:{line.line}",
              f"{label} is not portable: {match.group(0)}. Remove it, or "
              "record it as backticked evidence under plans/ or docs/."))
  return findings


def _is_recorded_evidence(path: str, text: str, matched: str) -> bool:
  if not (path.startswith("plans/") or path.startswith("docs/")):
    return False
  if not path.lower().endswith((".md", ".markdown")):
    return False
  return any(matched in span for span in re.findall(r"`([^`\n]+)`", text))


def check_whitespace(files: list[ChangedFile]) -> list[Finding]:
  """Reject added lines that `git diff --check` would reject."""
  findings: list[Finding] = []
  for change in files:
    path = change.path or change.old_path or "<unknown>"
    for line in change.added:
      text = line.text
      location = f"{path}:{line.line}"
      if text.endswith("\r"):
        findings.append(Finding(
            FAIL, "whitespace", location,
            "CRLF line ending in an added line. Commit LF line endings."))
        text = text[:-1]
      if TRAILING_WHITESPACE_RE.search(text):
        findings.append(Finding(
            FAIL, "whitespace", location,
            "trailing whitespace. `git diff --check` rejects this."))
      if SPACE_BEFORE_TAB_RE.search(text):
        findings.append(Finding(
            FAIL, "whitespace", location,
            "space before tab in indent. `git diff --check` rejects this."))
  return findings


def artifact_reason(path: str) -> str | None:
  """Return why a path is a generated or binary artifact, or None."""
  if path.startswith(ARTIFACT_ALLOWED_FIXTURE_PREFIX):
    return None
  if (path.startswith(ARTIFACT_ALLOWED_IMAGE_PREFIX)
      and Path(path).suffix.lower() in ARTIFACT_IMAGE_SUFFIXES):
    return None
  parts = Path(path).parts
  if not parts:
    return None
  name = parts[-1]
  lowered = name.lower()
  if name in ARTIFACT_NAMES:
    return "OS metadata file"
  if name.startswith(ARTIFACT_NAME_PREFIXES):
    return "editor swap file"
  if name.startswith("#") and name.endswith("#"):
    return "editor swap file"
  if name.endswith("~"):
    return "editor backup file"
  if "__pycache__" in parts:
    return "Python bytecode cache"
  if Path(lowered).suffix in ARTIFACT_SUFFIXES:
    return "generated or binary artifact"
  return None


def check_artifacts(files: list[ChangedFile]) -> list[Finding]:
  """Reject generated and binary artifacts outside the allowed fixture dirs."""
  findings: list[Finding] = []
  for change in files:
    if change.status == "deleted" or not change.path:
      continue
    reason = artifact_reason(change.path)
    if reason is None:
      continue
    findings.append(Finding(
        FAIL, "artifact", f"{change.path}:1",
        f"{reason} does not belong in the repository. Remove it from the "
        "diff or keep protocol fixtures in tests/protocol/golden/ and images "
        "in docs/img/."))
  return findings


def _is_production_source(path: str) -> bool:
  if not path.startswith(PRODUCTION_SOURCE_PREFIXES):
    return False
  parts = [part.lower() for part in Path(path).parts]
  if any(part in TEST_PATH_PARTS for part in parts):
    return False
  name = parts[-1]
  return not any(hint in name for hint in TEST_NAME_HINTS)


def check_debug_leftovers(files: list[ChangedFile]) -> list[Finding]:
  """Reject debug markers and diagnostics that would ship in production."""
  findings: list[Finding] = []
  for change in files:
    if not change.path or not _is_production_source(change.path):
      continue
    for line in change.added:
      for pattern, label in DEBUG_PATTERNS:
        match = pattern.search(line.text)
        if match is not None:
          detail = f" {match.group(0)}" if pattern is DEBUG_MARKER_RE else ""
          findings.append(Finding(
              FAIL, "debug-leftover", f"{change.path}:{line.line}",
              f"{label}{detail} in production code. Resolve it before review "
              "or move the note to a plan document."))
  return findings


def _all_changed_paths(files: list[ChangedFile]) -> set[str]:
  """Return paths touched by the diff. A deletion does not satisfy a doc."""
  paths: set[str] = set()
  for change in files:
    if change.status == "deleted":
      continue
    if change.path:
      paths.add(change.path)
    if change.status == "renamed" and change.old_path:
      paths.add(change.old_path)
  return paths


def _is_comment_or_blank(path: str, text: str) -> bool:
  stripped = text.strip()
  if not stripped:
    return True
  if path.endswith((".c", ".cc", ".cpp", ".h", ".hh", ".hpp")):
    return stripped.startswith(("//", "/*", "*", "*/"))
  if path.endswith((".py", ".sh", ".cmake", ".yml", ".yaml")):
    return stripped.startswith("#")
  return False


def _change_signal(change: ChangedFile) -> str:
  """Classify a change as code, whitespace, or comments for conservative rules."""
  path = change.path or change.old_path or ""
  added = [line for line in change.added
           if not _is_comment_or_blank(path, line.text)]
  removed = [line for line in change.removed
             if not _is_comment_or_blank(path, line.text)]
  if not added and not removed:
    return "comments"
  if added and removed:
    normalized_added = sorted(" ".join(line.text.split()) for line in added)
    normalized_removed = sorted(" ".join(line.text.split()) for line in removed)
    if normalized_added == normalized_removed:
      return "whitespace"
  return "code"


def _find_change(files: list[ChangedFile], path: str) -> ChangedFile | None:
  for change in files:
    if change.path == path or change.old_path == path:
      return change
  return None


def _line_location(path: str, line: DiffLine) -> str:
  return f"{path}:{line.line}"


def _missing_requirements(files: list[ChangedFile], required: tuple[str, ...],
                          extra_required: bool) -> list[str]:
  changed = _all_changed_paths(files)
  missing = [path for path in required if path not in changed]
  if extra_required and not any(path.startswith("tests/") for path in changed):
    missing.append("a test under tests/")
  return missing


def _coupling_findings(change: ChangedFile, trigger_lines: list[DiffLine],
                       missing: list[str], severity: str,
                       rule_detail: str) -> list[Finding]:
  findings: list[Finding] = []
  anchor = trigger_lines[0] if trigger_lines else None
  for item in missing:
    location = (_line_location(change.path or "", anchor)
                if anchor is not None else f"{change.path}:1")
    findings.append(Finding(
        severity, "doc-coupling", location,
        f"{rule_detail}; the same pull request must update {item}."))
  return findings


def check_doc_coupling(files: list[ChangedFile]) -> list[Finding]:
  """Require docs and tests for settings, console, and UI behavior changes.

  Prevents the incomplete-behavior-change mistake. Deletes, renames, comment
  edits, and whitespace-only edits are uncertain mappings, so they warn rather
  than fail.
  """
  findings: list[Finding] = []
  paths = _all_changed_paths(files)

  settings = _find_change(files, SETTINGS_FILE)
  if settings is not None:
    if settings.status in ("deleted", "renamed"):
      findings.append(Finding(
          WARN, "doc-coupling", f"{SETTINGS_FILE}:1",
          "settings source was deleted or renamed. Confirm the settings docs "
          "and tests by hand; the mapping is not automatic."))
    else:
      triggers_added = [
          line for line in settings.added
          if SETTINGS_DEFAULT_RE.search(line.text)
          and not _is_comment_or_blank(settings.path or "", line.text)
      ]
      triggers_removed = [
          line for line in settings.removed
          if SETTINGS_DEFAULT_RE.search(line.text)
          and not _is_comment_or_blank(settings.path or "", line.text)
      ]
      if not triggers_added and not triggers_removed:
        if _change_signal(settings) in ("comments", "whitespace"):
          findings.append(Finding(
              WARN, "doc-coupling", f"{SETTINGS_FILE}:1",
              "settings source changed only in comments or whitespace. "
              "Confirm no default changed, then ignore this warning."))
      else:
        severity = (
            FAIL if triggers_added and _change_signal(settings) != "whitespace"
            else WARN)
        missing = _missing_requirements(
            files, SETTINGS_REQUIRED_DOCS, extra_required=True)
        findings.extend(_coupling_findings(
            settings, triggers_added or triggers_removed, missing, severity,
            "a settings default changed"))

  console = _find_change(files, CONSOLE_FILE)
  if console is not None:
    if console.status in ("deleted", "renamed"):
      findings.append(Finding(
          WARN, "doc-coupling", f"{CONSOLE_FILE}:1",
          "console source was deleted or renamed. Confirm the console docs by "
          "hand; the mapping is not automatic."))
    else:
      triggers_added = [
          line for line in console.added
          if STRING_LITERAL_RE.search(line.text)
          and not line.text.lstrip().startswith("#include")
          and not _is_comment_or_blank(console.path or "", line.text)
      ]
      triggers_removed = [
          line for line in console.removed
          if STRING_LITERAL_RE.search(line.text)
          and not line.text.lstrip().startswith("#include")
          and not _is_comment_or_blank(console.path or "", line.text)
      ]
      # The same literals added and removed is code rewritten around an
      # unchanged string, e.g. strcmp(x, "trace") becoming an explicit
      # comparison. Nothing a user sees changed, so it only warns.
      def literals(lines):
        return sorted(m for line in lines for m in STRING_LITERAL_RE.findall(line.text))

      same_strings = literals(triggers_added) == literals(triggers_removed)
      if triggers_added or triggers_removed:
        severity = (
            FAIL if triggers_added and _change_signal(console) != "whitespace"
            and not same_strings
            else WARN)
        missing = _missing_requirements(
            files, CONSOLE_REQUIRED_DOCS, extra_required=False)
        findings.extend(_coupling_findings(
            console, triggers_added or triggers_removed, missing, severity,
            "a console command or output string changed"))
      elif _change_signal(console) in ("comments", "whitespace"):
        findings.append(Finding(
            WARN, "doc-coupling", f"{CONSOLE_FILE}:1",
            "console source changed only in comments or whitespace. Confirm no "
            "command or output string changed, then ignore this warning."))
      else:
        findings.append(Finding(
            WARN, "doc-coupling", f"{CONSOLE_FILE}:1",
            "console source changed without a detected command or output "
            "string. Check docs/console-commands.md by hand."))

  ui = _find_change(files, UI_FILE)
  if ui is not None:
    if ui.status in ("deleted", "renamed"):
      findings.append(Finding(
          WARN, "doc-coupling", f"{UI_FILE}:1",
          "UI source was deleted or renamed. Confirm the UI walkthrough and "
          "simulator scenarios by hand; the mapping is not automatic."))
    else:
      triggers_added = [
          line for line in ui.added
          if UI_CHANGE_RE.search(line.text)
          and not _is_comment_or_blank(ui.path or "", line.text)
      ]
      triggers_removed = [
          line for line in ui.removed
          if UI_CHANGE_RE.search(line.text)
          and not _is_comment_or_blank(ui.path or "", line.text)
      ]
      if triggers_added or triggers_removed:
        has_doc = UI_DOC in paths
        has_scenario = any(path.startswith(UI_SCENARIO_PREFIX) for path in paths)
        if not has_doc and not has_scenario:
          severity = (
              FAIL if triggers_added and _change_signal(ui) != "whitespace"
              else WARN)
          anchor = (triggers_added or triggers_removed)[0]
          findings.append(Finding(
              severity, "doc-coupling", _line_location(ui.path or "", anchor),
              "a UI page or query changed. Regenerate docs/ui-walkthrough.md "
              "or update a simulator scenario in the same pull request."))
      elif _change_signal(ui) in ("comments", "whitespace"):
        findings.append(Finding(
            WARN, "doc-coupling", f"{UI_FILE}:1",
            "UI source changed only in comments or whitespace. Confirm no page "
            "or query changed, then ignore this warning."))
      else:
        findings.append(Finding(
            WARN, "doc-coupling", f"{UI_FILE}:1",
            "UI source changed without a detected page or query token. Check "
            "docs/ui-walkthrough.md and simulator scenarios by hand."))

  return findings


def check_plan_links(repo: Path) -> list[Finding]:
  """Reject local Markdown links in plans/README.md that point nowhere."""
  readme = repo / "plans" / "README.md"
  if not readme.is_file():
    return [Finding(
        WARN, "plan-links", "plans/README.md:1",
        "plan index is missing, so its links cannot be checked.")]
  findings: list[Finding] = []
  try:
    text = readme.read_text(encoding="utf-8")
  except OSError as error:
    return [Finding(
        FAIL, "plan-links", "plans/README.md:1",
        f"plan index cannot be read: {error}.")]
  for number, line in enumerate(text.splitlines(), 1):
    for raw_target in MARKDOWN_LINK_RE.findall(line):
      target = raw_target.strip("<>")
      if target.startswith(EXTERNAL_LINK_PREFIXES):
        continue
      target = target.split("#", 1)[0].split("?", 1)[0]
      if not target or not target.lower().endswith(".md"):
        continue
      if target.startswith("plans/"):
        resolved = repo / target
      else:
        resolved = readme.parent / target
      if not resolved.exists():
        findings.append(Finding(
            FAIL, "plan-links", f"plans/README.md:{number}",
            f"broken local link: {raw_target} does not exist."))
  return findings


def collect_findings(files: list[ChangedFile], repo: Path) -> list[Finding]:
  """Run every check in priority order."""
  findings: list[Finding] = []
  findings.extend(check_absolute_paths(files))
  findings.extend(check_whitespace(files))
  findings.extend(check_artifacts(files))
  findings.extend(check_debug_leftovers(files))
  findings.extend(check_doc_coupling(files))
  findings.extend(check_plan_links(repo))
  return findings


def load_diff(repo: Path, revision: str, diff_file: str | None) -> str:
  """Read a unified diff from a file, stdin, or a git revision range."""
  if diff_file is not None:
    if diff_file == "-":
      return sys.stdin.read()
    path = Path(diff_file)
    if not path.exists():
      raise RuntimeError(f"diff file does not exist: {diff_file}")
    try:
      return path.read_text(encoding="utf-8", errors="replace")
    except OSError as error:
      raise RuntimeError(f"diff file cannot be read: {error}") from error
  try:
    arguments = shlex.split(revision)
  except ValueError as error:
    raise RuntimeError(f"invalid revision expression: {error}") from error
  command = [
      "git", "-C", str(repo), "diff", "--no-color", "--no-ext-diff",
      "--find-renames", "--unified=0", *arguments, "--",
  ]
  try:
    completed = subprocess.run(
        command, capture_output=True, text=True, check=False)
  except OSError as error:
    raise RuntimeError(f"cannot run git: {error}") from error
  if completed.returncode != 0:
    detail = completed.stderr.strip() or "unknown git error"
    raise RuntimeError(f"git diff failed for {revision}: {detail}")
  return completed.stdout


def report(findings: list[Finding]) -> int:
  """Print FAIL and WARN sections, then return the process exit status."""
  failures = [finding for finding in findings if finding.severity == FAIL]
  warnings = [finding for finding in findings if finding.severity == WARN]
  if failures:
    print("FAIL")
    for finding in failures:
      print(f"  [{finding.rule}] {finding.location}: {finding.message}")
  if warnings:
    print("WARN")
    for finding in warnings:
      print(f"  [{finding.rule}] {finding.location}: {finding.message}")
  if not failures:
    print("PASS: no blocking findings")
  print(f"summary: {len(failures)} failure(s), {len(warnings)} warning(s)")
  return 1 if failures else 0


def main(argv: list[str] | None = None) -> int:
  parser = argparse.ArgumentParser(
      description="Catch review-blocking mistakes in a pull request diff.")
  parser.add_argument(
      "revision", nargs="*", default=None,
      help=f"git revision range (default: {DEFAULT_REVISION})")
  parser.add_argument(
      "--diff-file",
      help="read a unified diff from this file instead of git (use - for stdin)")
  parser.add_argument(
      "--repo", default=".", help="checkout root used for git and plan links")
  arguments = parser.parse_args(argv)

  if arguments.diff_file is not None and arguments.revision:
    parser.error("--diff-file cannot be combined with a revision range")

  repo = Path(arguments.repo).resolve()
  if not repo.is_dir():
    print(f"FAIL [setup] {arguments.repo}: repository root does not exist.")
    return 2

  revision = " ".join(arguments.revision) if arguments.revision else DEFAULT_REVISION
  try:
    diff_text = load_diff(repo, revision, arguments.diff_file)
  except RuntimeError as error:
    print(f"FAIL [setup] {error}")
    return 2

  files = parse_diff(diff_text)
  return report(collect_findings(files, repo))


if __name__ == "__main__":
  sys.exit(main())
