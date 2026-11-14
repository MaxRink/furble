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

# Qualified or not: `using namespace std;` makes a bare name the same type.
CONDITION = re.compile(r"\bcondition_variable(?:_any)?\b")
ALIAS = re.compile(r"\busing\s+(\w+)\s*=\s*(?:std::)?condition_variable(?:_any)?\s*;")
SIM_ALIAS = re.compile(r"\busing\s+(\w+)\s*=\s*(?:::)?(?:Furble::)?Sim::SchedulerConditionVariable\s*;")
_DEFINED = r"defined\s*(?:\(\s*{name}\s*\)|\s{name})"
_SIM = _DEFINED.format(name=r"FURBLE_SIM\w*")
_ANY = _DEFINED.format(name=r"\w+")
# The whole directive must be a FURBLE_SIM test, optionally narrowed by `&&`
# terms. Any `||` or other operator makes it a non-sim frame.
POSITIVE = re.compile(r"^(?:ifdef\s+FURBLE_SIM\w*|if\s+" + _SIM
                      + r"(?:\s*&&\s*!?\s*" + _ANY + r")*)\s*$")
NEGATIVE = re.compile(r"^(?:ifndef\s+FURBLE_SIM\w*|if\s+!\s*" + _SIM + r")\s*$")
DEAD = re.compile(r"^if\s+0\s*$")


def _skip_literal(text: str, i: int, quote: str) -> int:
  """Return the index just past a quoted literal starting at i."""
  j = i + 1
  n = len(text)
  while j < n and text[j] != quote and text[j] != "\n":
    j += 2 if text[j] == "\\" else 1
  return min(j + 1, n)


def strip_comments(text: str) -> str:
  """Blank comments, string and character literals, keeping line structure."""
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
    elif text[i] == '"' or (text[i] == "'" and not (i > 0 and text[i - 1].isdigit())):
      # A quote after a digit is a C++14 digit separator, not a literal.
      end = _skip_literal(text, i, text[i])
      out.append(" " * (end - i))
      i = end
    else:
      out.append(text[i])
      i += 1
  return "".join(out)


def _classify(code: str):
  """Yield (line number, line, sim_only) for every live non-directive line."""
  # Each frame is [kind, in_else]; kind is sim, nosim, dead or other.
  frames: list[list] = []
  for number, line in enumerate(code.splitlines(), start=1):
    stripped = line.strip()
    if stripped.startswith("#"):
      directive = re.sub(r"\s+", " ", stripped[1:].strip())
      if directive.startswith("if"):
        kind = ("sim" if POSITIVE.match(directive) else "nosim" if NEGATIVE.match(directive)
                else "dead" if DEAD.match(directive) else "other")
        frames.append([kind, False])
      elif directive.startswith("elif") and frames:
        frames[-1] = ["other", False]
      elif directive.startswith("else") and frames:
        frames[-1][1] = True
      elif directive.startswith("endif") and frames:
        frames.pop()
      continue
    if any(kind == "dead" and not in_else for kind, in_else in frames):
      continue
    sim_only = any((kind == "sim" and not in_else) or (kind == "nosim" and in_else)
                   for kind, in_else in frames)
    yield number, line, sim_only


def findings(text: str) -> list[int]:
  """Return the 1-based lines of condition variables the scheduler cannot see."""
  code = strip_comments(text)
  lines = list(_classify(code))
  # Only a simulator alias that is live and sim-only counts as the twin.
  sim_aliases = {match.group(1) for _, line, sim_only in lines if sim_only
                 for match in SIM_ALIAS.finditer(line)}
  bad = []
  for number, line, sim_only in lines:
    if sim_only or not CONDITION.search(line):
      continue
    alias = ALIAS.search(line)
    if alias and alias.group(1) in sim_aliases:
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
    text = ("#if defined(FURBLE_SIM) && !defined(FURBLE_SIM_NO_SCHED_MUTEX)\n"
            "using c_t = Sim::SchedulerConditionVariable;\n"
            "#else\nusing c_t = std::condition_variable;\n#endif\n")
    self.assertEqual(findings(text), [])

  def test_alias_without_sim_twin_is_flagged(self):
    self.assertEqual(findings("using c_t = std::condition_variable;\n"), [1])

  def test_comment_is_not_code(self):
    self.assertEqual(findings("// a std::condition_variable here\nint x;\n"), [])

  def test_or_condition_is_not_sim_only(self):
    text = "#if defined(FURBLE_SIM) || defined(X)\nstd::condition_variable cv;\n#endif\n"
    self.assertEqual(findings(text), [2])

  def test_unqualified_name_is_flagged(self):
    self.assertEqual(findings("using namespace std;\ncondition_variable cv;\n"), [2])

  def test_sim_twin_in_dead_code_does_not_count(self):
    text = ("#if 0\nusing c_t = Sim::SchedulerConditionVariable;\n#endif\n"
            "using c_t = std::condition_variable;\n")
    self.assertEqual(findings(text), [4])

  def test_unguarded_sim_twin_does_not_count(self):
    text = ("using c_t = Sim::SchedulerConditionVariable;\n"
            "using c_t = std::condition_variable;\n")
    self.assertEqual(findings(text), [2])

  def test_char_literal_quote_does_not_hide_code(self):
    self.assertEqual(findings("char q = '\"'; std::condition_variable cv;\n"), [1])

  def test_escaped_char_literal(self):
    self.assertEqual(findings("char q = '\\''; std::condition_variable cv;\n"), [1])

  def test_digit_separator_is_not_a_literal(self):
    self.assertEqual(findings("int n = 1'000; std::condition_variable cv;\n"), [1])

  def test_dead_code_is_skipped(self):
    self.assertEqual(findings("#if 0\nstd::condition_variable cv;\n#endif\n"), [])

  def test_include_line_is_not_a_use(self):
    self.assertEqual(findings("#include <condition_variable>\n"), [])


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
