"""Tests for the Markdown citation gate.

Both halves matter: a citation that points where it says must pass, and one that
does not must fail with the reason that makes the failure actionable.
"""

from pathlib import Path
import contextlib
import io
import json
import shutil
import subprocess
import sys
import tempfile
import unittest
import unittest.mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

import check_doc_citations as citations  # noqa: E402


# Thirteen filler lines, then a blank, so the definition of legendSelectable
# starts on line 15 and a four line drift above it is easy to reproduce.
SOURCE = """\
// 1
// 2
// 3
// 4
// 5
// 6
// 7
// 8
// 9
// 10
// 11
// 12
// 13

bool UI::legendSelectable(void) {
  return true;
}
"""

# The same symbol with a doc comment attached above it, so a citation of the
# comment is a citation of the declaration.
COMMENTED = """\
constexpr uint32_t OTHER = 1;

/** 120 ms each way, Sleep Out to Sleep In. */
static constexpr uint32_t DISPLAY_SLEEP_DWELL_MS = 120;
"""


def git(root: Path, *arguments: str) -> str:
    result = subprocess.run(["git", "-C", str(root), *arguments],
                            capture_output=True, text=True, check=True)
    return result.stdout.strip()


def line_of(text: str, needle: str) -> int:
    for number, line in enumerate(text.splitlines(), start=1):
        if needle in line:
            return number
    raise AssertionError(f"{needle} is not in the fixture")


class CorpusTest(unittest.TestCase):
    """A corpus built from a dict of files, with no repository to enumerate."""

    def corpus(self, files: dict[str, str],
               externals: list[dict[str, str]] | None = None,
               allowed: list[dict[str, str]] | None = None) -> citations.Corpus:
        root = Path(tempfile.mkdtemp(prefix="furble-citations-"))
        self.addCleanup(shutil.rmtree, root)
        for name, text in files.items():
            target = root / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(text, encoding="utf-8")
        return citations.Corpus(root, files=sorted(files), externals=externals or [],
                                allowed=allowed or [])

    def check(self, files: dict[str, str], **kwargs) -> tuple[list[str], dict[str, int]]:
        externals = kwargs.pop("externals", None)
        allowed = kwargs.pop("allowed", None)
        return citations.check_corpus(self.corpus(files, externals, allowed), **kwargs)


class ParsingTest(CorpusTest):
    def test_single_line_range_and_en_dash(self):
        text = ("`a/b.cpp:12`, `a/b.cpp:12-34`, `a/b.cpp:12\u201334`,"
                " `a/b.cpp:12 - 34`\n")
        found = citations.citations_in("docs/x.md", text)
        self.assertEqual([(c.start, c.end) for c in found],
                         [(12, None), (12, 34), (12, 34), (12, 34)])

    def test_github_anchor_is_not_a_citation(self):
        found = citations.citations_in(
            "docs/x.md", "https://example.invalid/tree/master/src/a/b.cpp#L120\n")
        self.assertEqual(found, [])

    def test_a_plain_word_is_not_a_symbol(self):
        self.assertFalse(citations.symbol_like("settings"))
        self.assertFalse(citations.symbol_like("src/FurbleUI.cpp"))
        self.assertTrue(citations.symbol_like("UI::legendSelectable()"))
        self.assertTrue(citations.symbol_like("STATE_SHUTTER_OPEN"))
        self.assertTrue(citations.symbol_like("getState"))


class LineGateTest(CorpusTest):
    def test_in_range_citation_passes(self):
        errors, totals = self.check({"docs/x.md": "See `src/FurbleUI.cpp:2`.\n",
                                     "src/FurbleUI.cpp": SOURCE})
        self.assertEqual(errors, [])
        self.assertEqual(totals["checked"], 1)

    def test_missing_path_fails(self):
        errors, _ = self.check({"docs/x.md": "See `src/Gone.cpp:2`.\n",
                                "src/FurbleUI.cpp": SOURCE})
        self.assertEqual(len(errors), 1)
        self.assertIn("unresolved path", errors[0])
        self.assertIn("src/Gone.cpp:2", errors[0])

    def test_bare_filename_is_counted_but_not_resolved(self):
        """A basename may name a stub or an upstream copy, so it is not guessed."""
        errors, totals = self.check({"docs/x.md": "See `FurbleUI.cpp:2`.\n",
                                     "src/FurbleUI.cpp": SOURCE})
        self.assertEqual(errors, [])
        self.assertEqual(totals["unresolved"], 1)
        self.assertEqual(totals["checked"], 0)

    def test_declared_external_prefix_is_skipped(self):
        errors, totals = self.check(
            {"docs/x.md": "See `components/heap/heap_caps.c:109`.\n"},
            externals=[{"prefix": "components/",
                        "reason": "ESP-IDF is not vendored in this checkout."}])
        self.assertEqual(errors, [])
        self.assertEqual(totals["external"], 1)

    def test_a_tracked_file_beats_an_external_prefix(self):
        """`components/` is declared external, but a tracked path still resolves."""
        errors, totals = self.check(
            {"docs/x.md": "See `components/icons/icon.h:1`.\n",
             "components/icons/icon.h": "// icon\n"},
            externals=[{"prefix": "components/",
                        "reason": "ESP-IDF is not vendored in this checkout."}])
        self.assertEqual(errors, [])
        self.assertEqual(totals["external"], 0)
        self.assertEqual(totals["checked"], 1)

    def test_start_past_the_end_fails(self):
        total = len(SOURCE.splitlines())
        errors, _ = self.check({"docs/x.md": f"See `src/FurbleUI.cpp:{total + 1}`.\n",
                                "src/FurbleUI.cpp": SOURCE})
        self.assertEqual(len(errors), 1)
        self.assertIn("line out of range", errors[0])
        self.assertIn(f"has {total} lines", errors[0])

    def test_range_end_past_the_end_fails(self):
        total = len(SOURCE.splitlines())
        errors, _ = self.check({"docs/x.md": f"See `src/FurbleUI.cpp:1-{total + 1}`.\n",
                                "src/FurbleUI.cpp": SOURCE})
        self.assertEqual(len(errors), 1)
        self.assertIn("line out of range", errors[0])

    def test_inverted_range_fails(self):
        errors, _ = self.check({"docs/x.md": "See `src/FurbleUI.cpp:9-4`.\n",
                                "src/FurbleUI.cpp": SOURCE})
        self.assertEqual(len(errors), 1)
        self.assertIn("line out of range", errors[0])


class SymbolGateTest(CorpusTest):
    def setUp(self):
        self.start = line_of(SOURCE, "legendSelectable")

    def cite(self, line: str, symbol: str = "legendSelectable") -> dict[str, str]:
        return {"docs/x.md": f"`{symbol}()` is true on the Stick boards\n({line}).\n",
                "src/FurbleUI.cpp": SOURCE}

    def test_symbol_on_the_cited_line_passes(self):
        errors, _ = self.check(self.cite(f"`src/FurbleUI.cpp:{self.start}`"))
        self.assertEqual(errors, [])

    def test_the_four_line_drift_fails(self):
        """The failure mode this gate exists for: lines moved, the number did not."""
        start = self.start - 4
        errors, _ = self.check(
            self.cite(f"`src/FurbleUI.cpp:{start}-{self.start + 2}`"))
        self.assertEqual(len(errors), 1)
        self.assertIn("symbol missing", errors[0])
        self.assertIn(f"`legendSelectable` is not on line {start}", errors[0])
        self.assertIn(f"it defines `legendSelectable` at {self.start}-", errors[0])

    def test_a_line_inside_the_definition_passes(self):
        """Citing a line inside a function is legitimate, not drift."""
        errors, _ = self.check(
            self.cite(f"`src/FurbleUI.cpp:{self.start + 1}-{self.start + 2}`"))
        self.assertEqual(errors, [])

    def test_a_comment_above_a_declaration_passes(self):
        declared = line_of(COMMENTED, "DISPLAY_SLEEP_DWELL_MS")
        errors, _ = self.check(
            {"docs/x.md": f"`DISPLAY_SLEEP_DWELL_MS` is 120\n"
                          f"(`src/ui.h:{declared - 1}-{declared}`).\n",
             "src/ui.h": COMMENTED})
        self.assertEqual(errors, [])

    def test_a_symbol_the_file_only_uses_passes_anywhere_in_the_range(self):
        errors, _ = self.check(
            {"docs/x.md": "`TRANSIENT_MAX_ATTEMPTS` caps the retries\n"
                          "(`src/net.cpp:1-4`).\n",
             "src/net.cpp": "// pad\nconst int TRANSIENT_MAX_ATTEMPTS = 10;\n"
                            "// pad\n// pad\n"})
        self.assertEqual(errors, [])

    def test_two_named_symbols_are_not_checked(self):
        """A sentence naming two symbols does not claim either one is there."""
        start = self.start - 4
        errors, _ = self.check(
            {"docs/x.md": "`legendSelectable` to `someOtherSymbol`\n"
                          f"(`src/FurbleUI.cpp:{start}-{start + 2}`).\n",
             "src/FurbleUI.cpp": SOURCE})
        self.assertEqual(errors, [])

    def test_a_plain_word_in_a_code_span_is_not_a_symbol(self):
        errors, _ = self.check(
            {"docs/x.md": "- Evidence: `code`\n  (`src/FurbleUI.cpp:2`).\n",
             "src/FurbleUI.cpp": SOURCE})
        self.assertEqual(errors, [])

    def test_a_citation_with_no_named_symbol_is_not_symbol_checked(self):
        """This is the documented blind spot, pinned so the docs stay honest."""
        errors, _ = self.check(
            {"docs/x.md": "the settings roller\n"
                          f"(`src/FurbleUI.cpp:{self.start - 4}`).\n",
             "src/FurbleUI.cpp": SOURCE})
        self.assertEqual(errors, [])

    def test_a_symbol_only_in_a_comment_on_the_line_satisfies_the_check(self):
        """The symbol check reads the raw line, so a comment mention passes.

        This is a documented limit, not a desired result: the prose may be
        citing a log line that names the symbol in a different function.
        """
        errors, totals = self.check(
            {"docs/x.md": "The retry calls `Control::setPower` (`src/a.cpp:2`).\n",
             "src/a.cpp": "// line 1\n// TODO: Control::setPower is called here.\n"
                          "void other(void) {\n}\n"})
        self.assertEqual(errors, [])
        self.assertEqual(totals["symbols"], 1)

    def test_a_comment_inserted_above_a_cited_definition_is_absorbed(self):
        """The span covers the attached comment block, so the shift is hidden.

        The old definition line now holds a comment line, and the citation still
        passes because that comment is part of the definition's span. Pinned as a
        documented blind spot so a future change to it is a contract change.
        """
        cited = line_of(COMMENTED, "DISPLAY_SLEEP_DWELL_MS")
        lines = COMMENTED.splitlines(keepends=True)
        lines.insert(2, "// new doc line\n// new doc line\n"
                        "// new doc line\n// new doc line\n")
        moved_source = "".join(lines)
        self.assertNotEqual(cited, line_of(moved_source, "DISPLAY_SLEEP_DWELL_MS"))
        errors, _ = self.check(
            {"docs/x.md": "`DISPLAY_SLEEP_DWELL_MS` is 120\n"
                          f"(`src/ui.h:{cited}`).\n",
             "src/ui.h": moved_source})
        self.assertEqual(errors, [])


class AllowlistTest(CorpusTest):
    def files(self) -> dict[str, str]:
        return {"docs/x.md": "`legendSelectable()` (`src/FurbleUI.cpp:1`).\n",
                "src/FurbleUI.cpp": SOURCE}

    def test_an_allowed_citation_passes(self):
        errors, _ = self.check(self.files(), allowed=[
            {"doc": "docs/x.md", "citation": "src/FurbleUI.cpp:1",
             "reason": "the citation deliberately spans the caller above"}])
        self.assertEqual(errors, [])

    def test_a_stale_allow_entry_fails(self):
        errors, _ = self.check(self.files(), allowed=[
            {"doc": "docs/x.md", "citation": "src/FurbleUI.cpp:99",
             "reason": "no longer suppresses anything at all"}])
        self.assertTrue(any("stale allow entry" in error for error in errors), errors)

    def test_an_allow_entry_without_a_reason_is_rejected(self):
        root = Path(tempfile.mkdtemp(prefix="furble-citations-"))
        self.addCleanup(shutil.rmtree, root)
        (root / "tools").mkdir()
        (root / "tools" / "doc_citations.json").write_text(json.dumps({
            "externals": [],
            "allow": [{"doc": "docs/x.md", "citation": "src/x.cpp:1"}]}),
            encoding="utf-8")
        with self.assertRaises(citations.InvocationError) as raised:
            citations.load_config(root)
        self.assertIn("reason", str(raised.exception))

    def test_an_external_entry_without_a_reason_is_rejected(self):
        root = Path(tempfile.mkdtemp(prefix="furble-citations-"))
        self.addCleanup(shutil.rmtree, root)
        (root / "tools").mkdir()
        (root / "tools" / "doc_citations.json").write_text(
            json.dumps({"externals": [{"prefix": "components/"}]}), encoding="utf-8")
        with self.assertRaises(citations.InvocationError) as raised:
            citations.load_config(root)
        self.assertIn("needs a prefix and a reason", str(raised.exception))


class BaseModeTest(CorpusTest):
    """`--base` must see drift caused by a source edit, not only doc edits."""

    def scope(self, touched: set[str], **kwargs) -> tuple[list[str], dict[str, int]]:
        # Both documents cite a line the symbol is not on, so only the scoping
        # rule decides which of them the range is responsible for.
        corpus = self.corpus({
            "docs/changed.md": "`legendSelectable()` (`src/FurbleUI.cpp:1`).\n",
            "docs/untouched.md": "`legendSelectable()` (`src/FurbleUI.cpp:2`).\n",
            "src/FurbleUI.cpp": SOURCE,
        }, kwargs.pop("externals", None), kwargs.pop("allowed", None))
        # The revision range itself is covered by ChangedPathsTest. Here the
        # scoping rule is what matters, so the touched set is injected.
        with unittest.mock.patch.object(citations, "changed_paths",
                                        return_value=touched):
            return citations.check_corpus(corpus, base="BASE")

    def test_a_changed_target_is_checked_even_when_its_doc_is_untouched(self):
        errors, _ = self.scope({"src/FurbleUI.cpp"})
        self.assertTrue(any("docs/untouched.md" in error for error in errors), errors)
        self.assertTrue(any("docs/changed.md" in error for error in errors), errors)

    def test_an_untouched_doc_and_target_is_skipped(self):
        errors, totals = self.scope({"src/unrelated.cpp"})
        self.assertEqual(errors, [])
        self.assertEqual(totals["citations"], 0)

    def test_a_changed_doc_is_checked(self):
        errors, _ = self.scope({"docs/changed.md"})
        self.assertEqual(len(errors), 1)
        self.assertIn("docs/changed.md", errors[0])
        self.assertNotIn("docs/untouched.md", errors[0])

    def test_a_stale_allow_entry_is_not_reported_in_base_mode(self):
        """Base mode sees a subset, so it cannot judge that an entry is stale."""
        errors, _ = self.scope({"src/unrelated.cpp"}, allowed=[
            {"doc": "docs/absent.md", "citation": "src/none.cpp:1",
             "reason": "not exercised by this revision range"}])
        self.assertEqual(errors, [])


class ChangedPathsTest(unittest.TestCase):
    """One real repository, so the git plumbing itself is pinned."""

    def test_a_source_edit_that_moves_a_symbol_fails_an_untouched_doc(self):
        root = Path(tempfile.mkdtemp(prefix="furble-citations-"))
        self.addCleanup(shutil.rmtree, root)
        git(root, "init", "-q", "-b", "master")
        (root / "docs").mkdir()
        (root / "src").mkdir()
        (root / "docs" / "x.md").write_text(
            "`legendSelectable()` (`src/FurbleUI.cpp:15`).\n", encoding="utf-8")
        (root / "src" / "FurbleUI.cpp").write_text(SOURCE, encoding="utf-8")
        git(root, "add", ".")
        git(root, "-c", "user.email=t@example.invalid", "-c", "user.name=Test",
            "commit", "-q", "-m", "base")
        base = git(root, "rev-parse", "HEAD")
        # Four lines above the definition, which is what moved the plan citations.
        (root / "src" / "FurbleUI.cpp").write_text(
            "// 1\n// 2\n// 3\n// 4\n" + SOURCE, encoding="utf-8")
        git(root, "add", ".")
        git(root, "-c", "user.email=t@example.invalid", "-c", "user.name=Test",
            "commit", "-q", "-m", "move it")
        self.assertEqual(citations.changed_paths(root, base, "HEAD"),
                         {"src/FurbleUI.cpp"})
        errors, _ = citations.check(root, base=base)
        self.assertEqual(len(errors), 1)
        self.assertIn("docs/x.md", errors[0])
        self.assertIn("symbol missing", errors[0])
        # The same revision on both sides touches nothing, so nothing is checked.
        errors, totals = citations.check(root, base=base, head=base)
        self.assertEqual(errors, [])
        self.assertEqual(totals["citations"], 0)


class RepositoryTest(unittest.TestCase):
    """The gate is registered and configured where the repository expects it."""

    def test_the_config_declares_a_reason_for_every_entry(self):
        config = json.loads(
            (ROOT / "tools" / "doc_citations.json").read_text(encoding="utf-8"))
        self.assertTrue(config["externals"], "the external tree list must not be empty")
        for entry in config["externals"]:
            self.assertTrue(entry["prefix"])
            self.assertGreater(len(entry["reason"]), 20, entry)
        for entry in config["allow"]:
            self.assertTrue(entry["doc"] and entry["citation"])
            self.assertGreater(len(entry["reason"]), 20, entry)

    def test_pre_review_runs_the_gate(self):
        text = (ROOT / "tools" / "pre_review.py").read_text(encoding="utf-8")
        self.assertIn("check_doc_citations.py", text)

    def test_the_tracked_tree_passes_the_full_sweep(self):
        """A clean tree is what lets pre_review register the full sweep."""
        errors, totals = citations.check(ROOT)
        self.assertEqual(errors, [])
        self.assertGreater(totals["checked"], 1000)


if __name__ == "__main__":
    unittest.main()
