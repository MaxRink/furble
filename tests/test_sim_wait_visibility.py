#!/usr/bin/env python3
"""Keep production condition variables visible to the simulator scheduler.

Issue 289. Scan::stop() waited on a plain std::condition_variable for a
callback running on the radio task. In the simulator that wait was invisible:
the control task stayed runnable and kept the scheduler turn, so the radio task
could not finish the callback until the 2 s host-time stall breaker parked the
control task. A parked task runs outside the turn, so the UI advanced the
virtual clock without waiting for it, and on a loaded host a cancel burned its
whole 30 s interactive cap. Every cancel in the Fujifilm Secure and reconnect
sweeps went through that breaker.

A production condition variable therefore has to be one of:

1. sim-only code, inside a `#if defined(FURBLE_SIM...)` branch, or
2. a type alias `using NAME = std::condition_variable;` whose simulator branch
   in the same file is `using NAME = Sim::SchedulerConditionVariable;`, the
   pattern `connect_mutex_t` uses for mutexes.

Plain std::mutex is deliberately not covered. Most production mutexes guard a
few loads and stores and never cross a scheduler boundary, which the scheduler
handles. A condition variable exists to block until another task acts, which
is exactly the wait the scheduler must see.

Known residual hole, written down rather than claimed closed: a plain mutex
held across a scheduler boundary is the same class of defect and cannot be
recognised by declaration. The `FURBLE_SIM_SCHED_TRACE` trace in
sim/freertos.cpp is the tool for finding one.
"""

import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parent.parent
PRODUCTION_DIRS = ("src", "include", "lib/furble")
SUFFIXES = {".c", ".cc", ".cpp", ".h", ".hpp"}

CONDITION = re.compile(r"\bstd::condition_variable(?:_any)?\b")
ALIAS = re.compile(r"\busing\s+(\w+)\s*=\s*std::condition_variable(?:_any)?\s*;")
SIM_ALIAS = r"\busing\s+{name}\s*=\s*(?:Furble::)?Sim::SchedulerConditionVariable\s*;"
POSITIVE = re.compile(r"^(?:ifdef\s+FURBLE_SIM\w*|if\s+defined\s*\(?\s*FURBLE_SIM\w*)")
NEGATIVE = re.compile(r"^(?:ifndef\s+FURBLE_SIM\w*\s*$|if\s+!\s*defined\s*\(?\s*FURBLE_SIM\w*\s*\)?\s*$)")


def strip_comments(text: str) -> str:
  """Blank comments and string literals, keeping line structure."""
  out = []
  i = 0
  n = len(text)
  while i < n:
    if text.startswith("//", i):
      while i < n and text[i] != "\n":
        i += 1
    elif text.startswith("/*", i):
      end = text.find("*/", i + 2)
      end = n if end < 0 else end + 2
      out.append("".join("\n" if c == "\n" else " " for c in text[i:end]))
      i = end
    elif text[i] == '"':
      j = i + 1
      while j < n and text[j] != '"' and text[j] != "\n":
        j += 2 if text[j] == "\\" else 1
      out.append(" " * (j + 1 - i))
      i = j + 1
    else:
      out.append(text[i])
      i += 1
  return "".join(out)


def findings(text: str) -> list[int]:
  """Return the 1-based lines of condition variables the scheduler cannot see."""
  code = strip_comments(text)
  # Each frame is [polarity, in_else]: polarity is +1 for a FURBLE_SIM test,
  # -1 for a negated one and 0 for anything else.
  frames: list[list[int]] = []
  bad = []
  for number, line in enumerate(code.splitlines(), start=1):
    stripped = line.strip()
    if stripped.startswith("#"):
      directive = stripped[1:].strip()
      if directive.startswith("if"):
        polarity = 1 if POSITIVE.match(directive) else -1 if NEGATIVE.match(directive) else 0
        frames.append([polarity, 0])
      elif directive.startswith("elif") and frames:
        frames[-1] = [0, 0]
      elif directive.startswith("else") and frames:
        frames[-1][1] = 1
      elif directive.startswith("endif") and frames:
        frames.pop()
      continue
    if not CONDITION.search(line):
      continue
    sim_only = any((polarity == 1 and not in_else) or (polarity == -1 and in_else)
                   for polarity, in_else in frames)
    if sim_only:
      continue
    alias = ALIAS.search(line)
    if alias and re.search(SIM_ALIAS.format(name=re.escape(alias.group(1))), code):
      continue
    bad.append(number)
  return bad


class ScannerTest(unittest.TestCase):

  def test_plain_member_is_flagged(self):
    self.assertEqual(findings("struct A {\n  std::condition_variable cv;\n};\n"), [2])

  def test_any_variant_is_flagged(self):
    self.assertEqual(findings("std::condition_variable_any cv;\n"), [1])

  def test_sim_only_branch_is_skipped(self):
    text = "#if defined(FURBLE_SIM_MQTT)\nstd::condition_variable cv;\n#endif\n"
    self.assertEqual(findings(text), [])

  def test_else_of_sim_branch_is_production(self):
    text = "#if defined(FURBLE_SIM)\nint x;\n#else\nstd::condition_variable cv;\n#endif\n"
    self.assertEqual(findings(text), [4])

  def test_aliased_type_with_sim_twin_passes(self):
    text = ("#if defined(FURBLE_SIM)\nusing c_t = Sim::SchedulerConditionVariable;\n"
            "#else\nusing c_t = std::condition_variable;\n#endif\n")
    self.assertEqual(findings(text), [])

  def test_alias_without_sim_twin_is_flagged(self):
    self.assertEqual(findings("using c_t = std::condition_variable;\n"), [1])

  def test_comment_is_not_code(self):
    self.assertEqual(findings("// a std::condition_variable here\nint x;\n"), [])


class ProductionTest(unittest.TestCase):

  def test_production_condition_variables_are_scheduler_visible(self):
    problems = []
    for directory in PRODUCTION_DIRS:
      for path in sorted((ROOT / directory).rglob("*")):
        if path.suffix not in SUFFIXES or not path.is_file():
          continue
        for line in findings(path.read_text(encoding="utf-8", errors="replace")):
          problems.append(f"{path.relative_to(ROOT)}:{line}")
    self.assertEqual(problems, [],
                     "plain std::condition_variable in production code; alias it to "
                     "Sim::SchedulerConditionVariable under FURBLE_SIM (issue 289)")

  def test_scan_idle_wait_uses_the_alias(self):
    header = (ROOT / "lib/furble/Scan.h").read_text(encoding="utf-8")
    self.assertRegex(header, r"\bscan_condition_t\s+m_CallbackIdle\s*;")


if __name__ == "__main__":
  unittest.main()
