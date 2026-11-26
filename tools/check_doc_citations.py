#!/usr/bin/env python3
"""Verify that `file:line` citations in the Markdown docs still point at code.

Plans and docs cite code as `path:line` or `path:start-end`. Nothing moves those
numbers when the cited file changes, so an edit above a cited line rotates every
citation below it. PR #473 rewrote a three line comment in `src/FurbleUI.cpp`
into seven lines, which moved every citation below it by four. Stale numbers
survived in `plans/338-e2e-page-coverage.md` and
`plans/implemented/303-upstream-sync.md` with no gate to notice. The first sweep,
against `bebf7caf`, found 80: 74 citations whose named symbol was not where the
text said, 5 paths that resolved to nothing in this checkout and one range past
the end of its file. Against the merge base `c644f39e` the same command reports
86: 78 symbol, 5 unresolved path and 3 out of range.

Only mechanical facts are checked, and nothing else is claimed:

1. The path is a file in this checkout. A path that matches no file and no
   declared external prefix fails. A bare filename (`Camera.h:203`) does not
   identify a file, so it is counted as unresolved and is not checked.
2. The cited line exists: `1 <= start <= end <= lines in the file`.
3. A citation that annotates a symbol is checked against the code. It annotates
   one when its own clause ends with exactly one inline code span that looks like
   an identifier, or when a parenthesised citation on its own line follows a
   clause that opens with one. That is the shape which claims "this symbol is
   here"; a symbol mentioned elsewhere in the sentence is not attributed, so a
   phrase with no symbol or with two is checked for its path and line only. The
   symbol then has to appear on the cited start line, or the start line has to
   fall inside that symbol's definition in the same file, because citing a line
   inside a function or the comment above a constant is legitimate and common. A
   range may also cover a one line declaration, and a range in a file that never
   defines the symbol may hold any use of it. A range that starts above a
   multi line definition is the drift shape and fails, except when the lines
   between the citation and the definition are a comment block attached to it:
   the span covers that block, so comment lines inserted directly above a cited
   definition move it without a finding.

What this gate cannot do, and does not claim:

* It cannot read intent. A citation that names no symbol is checked for path and
  range only, so a wrong-but-in-range line passes. `plans/implemented/303-upstream-sync.md`
  cited `src/FurbleUI.cpp:10214` as "the settings roller" with no symbol, drifted
  four lines, and this gate cannot see it. Those two were found by hand.
* It cannot tell whether the cited code supports the prose. A line that holds the
  named symbol and says the opposite is a pass.
* It cannot tell a mention from a use on the cited line. The symbol check reads
  the raw line, so a comment or a log string that names the symbol satisfies it.
* It cannot see drift that stays inside one definition. A four line edit inside a
  forty line function moves every citation in that function and none of them
  fail.
* It cannot see comment lines inserted directly above a cited definition, because
  the definition's span covers the comment block attached above it.
* It cannot see a symbol that the sentence names before another citation, after
  the citation, in bold rather than backticks, or in a table cell that holds
  several citations.
* It does not resolve a bare filename. The name may match a stub, a shim or an
  upstream copy rather than the file the author meant: `esp_console.h` resolves
  to `tests/host/console/esp_console.h` here, which is not ESP-IDF.
* It does not check a citation into an external tree (ESP-IDF, M5GFX, LVGL,
  esptool, the toolchain). Those are declared in `tools/doc_citations.json` with
  a reason and counted in the summary, not verified. It does not read `#L123`
  URL fragments, `file:line:column`, or a range written as two citations.

Deliberately approximate citations are listed in the `allow` section of
`tools/doc_citations.json` with a reason. An entry that suppresses nothing is an
error, so the allowlist cannot rot into a blanket.

Usage:
  python3 tools/check_doc_citations.py
  python3 tools/check_doc_citations.py --base origin/master
  python3 tools/check_doc_citations.py --verbose

`--all` (the default) sweeps every non-ignored Markdown file. `--base REV`
checks only the citations that range could have broken: every citation in a
changed Markdown file, plus every citation anywhere that targets a source file
the range changed. The second half is why `--base` is not just "changed docs",
and it is also the weaker mode: a citation into an untouched file is trusted, so
it cannot see drift that predates the range. Its summary line says so.

Exit status is 0 when every checked citation is sound, 1 on a violation, and 2
for an invalid invocation.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path
import json
import re
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
CONFIG = Path("tools/doc_citations.json")
MARKDOWN = ".md"

# Suffixes a citation may name, kept to what the docs cite so prose such as
# `python3 tools/x.py` is read as a citation only when it carries a line number.
SOURCE_SUFFIXES = (
    "c", "cc", "cmake", "cpp", "cxx", "h", "hh", "hpp", "hxx", "ino", "json",
    "py", "sh", "txt", "yaml", "yml",
)
SOURCE_ALTERNATION = "|".join(sorted(SOURCE_SUFFIXES, key=len, reverse=True))

# `path:12`, `path:12-34` and the en dash spelling. The lookbehind rejects a
# match inside a longer path or a URL, and a match with no directory is a bare
# filename, which is not resolved.
CITATION = re.compile(
    r"(?<![\w./-])(?P<path>[A-Za-z0-9_.-]+(?:/[A-Za-z0-9_.-]+)*"
    rf"\.(?:{SOURCE_ALTERNATION}))"
    r"[ \t]*:[ \t]*(?P<start>\d+)(?:[ \t]*[-\u2013][ \t]*(?P<end>\d+))?"
)

CODE_SPAN = re.compile(r"`([^`\n]+)`")
# A code span that is a path, a citation or shell rather than a symbol.
PATHISH = re.compile(rf"[/\\]|:(?:\d|//)|\.(?:{SOURCE_ALTERNATION})(?:$|[\s:])")
# A symbol: an optionally qualified identifier, optionally with call parens.
SYMBOL = re.compile(r"[A-Za-z_]\w*(?:::[A-Za-z_]\w*)*(?:\(\))?")
# Clauses inside a line, so two citations on one line do not share a symbol.
CLAUSE_BREAK = re.compile(r"[|;]|,(?![^`]*`)|\.(?=[ \t]+\S)")
# Text that may sit between a symbol and the citation that annotates it. A
# backtick is not stripped here, because the one that closes the span belongs to
# the span.
ANNOTATION = " \t(:"
# A phrase may open with a list bullet, a quote marker or an emphasis marker.
LIST_MARKER = re.compile(r"^[ \t]*(?:[-*+>]|\d+\.)[ \t]*")
# A line that closes a call rather than opening a definition.
CALL_TAIL = re.compile(r"(?:\.|->|\(|,|=|\breturn|\|\||&&)\s*$")
DEFINITION_TAIL = re.compile(r"^(?:const|noexcept|override|final)\b\s*")
COMMENT_PREFIX = ("//", "/*", "*", "*/")


class InvocationError(Exception):
    """An unusable invocation or configuration; maps to exit status 2."""


@dataclass(frozen=True)
class Citation:
    """One `path:line` or `path:start-end` occurrence in a Markdown file."""

    doc: str
    line: int
    column: int
    path: str
    start: int
    end: int | None

    @property
    def text(self) -> str:
        span = f"{self.start}-{self.end}" if self.end is not None else str(self.start)
        return f"{self.path}:{span}"


@dataclass(frozen=True)
class Finding:
    """A citation the gate rejects, with the reason it rejects it."""

    citation: Citation
    kind: str
    detail: str

    def __str__(self) -> str:
        return (f"{self.citation.doc}:{self.citation.line}: {self.citation.text}: "
                f"{self.kind}: {self.detail}")


def run_git(root: Path, arguments: list[str]) -> str:
    result = subprocess.run(["git", "-C", str(root), *arguments],
                            capture_output=True, text=True)
    if result.returncode != 0:
        raise InvocationError(f"git {' '.join(arguments)} failed: {result.stderr.strip()}")
    return result.stdout


def repository_files(root: Path) -> list[str]:
    """Every non-ignored file, so an uncommitted doc is scanned and resolvable."""
    listed = run_git(root, ["ls-files", "--cached", "--others", "--exclude-standard"])
    return sorted({line for line in listed.splitlines() if line})


def load_config(root: Path) -> tuple[list[dict[str, str]], list[dict[str, str]]]:
    """Return the declared external prefixes and the allowlist entries."""
    path = root / CONFIG
    if not path.is_file():
        return [], []
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except ValueError as error:
        raise InvocationError(f"{CONFIG.as_posix()} is not valid JSON: {error}") from error
    externals: list[dict[str, str]] = []
    allowed: list[dict[str, str]] = []
    for entry in data.get("externals", []):
        if not entry.get("prefix") or not entry.get("reason"):
            raise InvocationError(
                f"{CONFIG.as_posix()}: an externals entry needs a prefix and a reason: {entry}")
        externals.append(entry)
    for entry in data.get("allow", []):
        missing = [key for key in ("doc", "citation", "reason") if not entry.get(key)]
        if missing:
            raise InvocationError(f"{CONFIG.as_posix()}: an allow entry has no "
                                  f"{' or '.join(missing)}: {entry}")
        allowed.append(entry)
    return externals, allowed


def citations_in(doc: str, text: str) -> list[Citation]:
    found: list[Citation] = []
    for number, line in enumerate(text.splitlines(), start=1):
        for match in CITATION.finditer(line):
            end = match.group("end")
            found.append(Citation(doc, number, match.start(), match.group("path"),
                                  int(match.group("start")),
                                  int(end) if end is not None else None))
    return found


def symbol_like(span: str) -> bool:
    """Whether a code span reads as an identifier taken from the code.

    A plain lowercase English word such as `code` or `state` is not one, which
    keeps a template sentence from naming a symbol by accident.
    """
    candidate = span.strip()
    if not candidate or PATHISH.search(candidate):
        return False
    if SYMBOL.fullmatch(candidate) is None:
        return False
    name = candidate[:-2] if candidate.endswith("()") else candidate
    if "::" in name or "_" in name or candidate.endswith("()"):
        return True
    return not name.islower()


def annotation_free(text: str) -> str:
    """*text* without the annotation punctuation a citation may sit behind.

    A trailing lone backtick is dropped too, because it opens the span that the
    citation is parenthesised in. A balanced pair is kept, since it is the span.
    """
    trimmed = text.rstrip(ANNOTATION)
    if trimmed.count("`") % 2:
        trimmed = trimmed.rstrip("`").rstrip(ANNOTATION)
    return trimmed


def annotating_symbol(lines: list[str], citation: Citation) -> str | None:
    """The symbol this citation annotates, when the text claims one.

    The claim has to be syntactic, because a symbol mentioned somewhere in a
    sentence is not a claim that the symbol sits at the cited line. Two shapes
    count: the citation annotates the span right above it (`foo()` followed by
    `(path:12)`), and a parenthesised citation on its own line annotates the span
    that opens the line above. Anything else is not symbol-checked rather than
    checked against a symbol the author did not point at.
    """
    line = lines[citation.line - 1]
    before = line[:citation.column]
    clause = CLAUSE_BREAK.split(before)[-1]
    trailing = annotation_free(clause)
    spans = [span for span in CODE_SPAN.findall(trailing) if symbol_like(span)]
    if len(spans) == 1 and trailing.rstrip("`").endswith(spans[0].strip()):
        return bare_symbol(spans[0])
    if before.strip().strip(ANNOTATION + "`"):
        return None
    for index in range(citation.line - 2, -1, -1):
        previous = lines[index]
        if not previous.strip():
            continue
        clause = CLAUSE_BREAK.split(previous)[-1]
        opening = LIST_MARKER.sub("", clause.lstrip()).lstrip("`")
        spans = [span for span in CODE_SPAN.findall(clause) if symbol_like(span)]
        if len(spans) == 1 and opening.startswith(spans[0].strip()):
            return bare_symbol(spans[0])
        return None
    return None


def bare_symbol(span: str) -> str:
    candidate = span.strip()
    return candidate[:-2] if candidate.endswith("()") else candidate


def code_only(line: str) -> str:
    """A line with its comments and literal bodies blanked, for brace walking."""
    out: list[str] = []
    quote = ""
    index = 0
    while index < len(line):
        char = line[index]
        if quote:
            if char == "\\":
                out.extend("  ")
                index += 2
                continue
            if char == quote:
                quote = ""
            out.append(" ")
            index += 1
            continue
        if char in "\"'":
            quote = char
            out.append(" ")
            index += 1
            continue
        if line.startswith("//", index):
            break
        out.append(char)
        index += 1
    return "".join(out)


def matching_paren(code: str, opened: int) -> int:
    depth = 0
    for index in range(opened, len(code)):
        if code[index] == "(":
            depth += 1
        elif code[index] == ")":
            depth -= 1
            if depth == 0:
                return index
    return -1


def body_end(lines: list[str], line_number: int, column: int) -> int:
    """One-based line of the brace that closes the body opening at *column*."""
    depth = 0
    for number in range(line_number, len(lines) + 1):
        code = code_only(lines[number - 1])
        for index, char in enumerate(code):
            if number == line_number and index < column:
                continue
            if char == "{":
                depth += 1
            elif char == "}":
                depth -= 1
                if depth == 0:
                    return number
    return len(lines)


def comment_head(lines: list[str], line_number: int) -> int:
    """Walk a definition up over the comment block attached above it."""
    number = line_number
    while number > 1:
        previous = lines[number - 2].strip()
        if previous.startswith(COMMENT_PREFIX) or previous.endswith("*/"):
            number -= 1
        else:
            break
    return number


def definition_span(lines: list[str], name: str) -> tuple[int, int] | None:
    """The lines a definition of *name* occupies in *lines*, comments included.

    A citation may legitimately point at a line inside a function, or at the
    comment above a constant, so the span bounds the claim the symbol makes. The
    span is best effort: when the walk fails to find a body, the opening line is
    the whole span, and a symbol this file only calls has no span at all.
    """
    pattern = re.compile(r"(?<![\w])" + re.escape(name) + r"(?![\w])")
    for number, raw in enumerate(lines, start=1):
        code = code_only(raw)
        match = pattern.search(code)
        if match is None:
            continue
        before = code[:match.start()].rstrip()
        if CALL_TAIL.search(before):
            continue
        after = code[match.end():].lstrip()
        if after.startswith("("):
            opened = code.index("(", match.end())
            closed = matching_paren(code, opened)
            if closed < 0:
                continue
            tail = DEFINITION_TAIL.sub("", code[closed + 1:].strip()).strip()
            if tail.startswith("->"):
                continue
            if tail.startswith("{"):
                return (comment_head(lines, number),
                        body_end(lines, number, code.index("{", closed)))
            if tail.startswith(";") and before:
                # A declaration carries a return type before the name. A bare
                # `call(...);` at the start of a statement carries none, so it
                # is a use and not a definition.
                return (comment_head(lines, number), number)
            for forward in range(number, min(number + 4, len(lines) + 1)):
                following = code_only(lines[forward]).strip()
                if not following:
                    continue
                if following.startswith("{"):
                    column = code_only(lines[forward]).index("{")
                    return (comment_head(lines, number),
                            body_end(lines, forward + 1, column))
                break
            continue
        if after.startswith("="):
            return (comment_head(lines, number), number)
    return None


def symbol_on(lines: list[str], number: int, name: str) -> bool:
    """Whether *name* appears on the one-based line *number*.

    The qualified name is tried first and its last component second, so a doc
    that writes `UI::wakeDisplay()` matches `void UI::wakeDisplay(void)`.
    """
    if not 1 <= number <= len(lines):
        return False
    candidates = [name]
    if "::" in name:
        candidates.append(name.rsplit("::", 1)[1])
    for candidate in candidates:
        pattern = re.compile(r"(?<![\w])" + re.escape(candidate) + r"(?![\w])")
        if pattern.search(lines[number - 1]):
            return True
    return False


def normalized(path: str) -> str:
    """A citation path without its leading `./`, which a doc may write."""
    while path.startswith("./"):
        path = path[2:]
    return path


def is_external(path: str, externals: list[dict[str, str]]) -> bool:
    for entry in externals:
        prefix = entry["prefix"]
        if path == prefix or path.startswith(prefix.rstrip("/") + "/"):
            return True
    return False


class Corpus:
    """The checkout the gate reads, with its files and line counts cached.

    The file list and the declared externals and allowlist can be injected, which
    is how the gate is tested without a git repository to enumerate.
    """

    def __init__(self, root: Path, files: list[str] | None = None,
                 externals: list[dict[str, str]] | None = None,
                 allowed: list[dict[str, str]] | None = None) -> None:
        self.root = root
        self.files = set(files) if files is not None else set(repository_files(root))
        self.docs = [path for path in sorted(self.files) if path.endswith(MARKDOWN)]
        if externals is None and allowed is None:
            self.externals, self.allowed = load_config(root)
        else:
            self.externals = externals or []
            self.allowed = allowed or []
        self._lines: dict[str, list[str]] = {}

    def lines(self, path: str) -> list[str]:
        if path not in self._lines:
            text = (self.root / path).read_text(encoding="utf-8", errors="replace")
            self._lines[path] = text.splitlines()
        return self._lines[path]

    def document(self, doc: str) -> str:
        return (self.root / doc).read_text(encoding="utf-8", errors="replace")


def check_citation(corpus: Corpus, citation: Citation,
                   lines: list[str]) -> Finding | None:
    """The finding for *citation*, or None when it holds up."""
    path = normalized(citation.path)
    if path not in corpus.files:
        if is_external(path, corpus.externals):
            return None
        return Finding(citation, "unresolved path",
                       f"no file in this checkout and no declared external prefix "
                       f"in {CONFIG.as_posix()}")
    total = len(corpus.lines(path))
    if citation.start < 1 or citation.start > total:
        return Finding(citation, "line out of range", f"{path} has {total} lines")
    if citation.end is not None and (citation.end < citation.start
                                    or citation.end > total):
        return Finding(citation, "line out of range",
                       f"range {citation.start}-{citation.end} does not fit in "
                       f"{total} lines")
    name = annotating_symbol(lines, citation)
    if name is None:
        return None
    target = corpus.lines(path)
    if symbol_on(target, citation.start, name):
        return None
    span = definition_span(target, name)
    if span is not None and span[0] <= citation.start <= span[1]:
        return None
    # A file that never defines the name can only be citing a use of it, so a
    # range that holds the name anywhere is a fair citation.
    if citation.end is not None:
        # A range may cover a one line declaration. A range that starts above a
        # multi line definition is the drift shape, so it may not.
        if span is not None and span[0] == span[1] and (
                citation.start <= span[0] <= citation.end):
            return None
        if span is None and any(symbol_on(target, number, name)
                                for number in range(citation.start,
                                                    citation.end + 1)):
            return None
    where = (f"; it defines `{name}` at {span[0]}-{span[1]}" if span is not None
             else f"; it does not define `{name}`")
    return Finding(citation, "symbol missing",
                   f"`{name}` is not on line {citation.start}{where}")


def classify(corpus: Corpus, citation: Citation) -> str:
    """`checked`, `external` or `unresolved` for the summary counters."""
    path = normalized(citation.path)
    if "/" not in path:
        return "unresolved"
    if path in corpus.files:
        return "checked"
    return "external" if is_external(path, corpus.externals) else "checked"


def changed_paths(root: Path, base: str, head: str) -> set[str]:
    shown = run_git(root, ["diff", "--name-only", f"{base}...{head}"])
    return {line for line in shown.splitlines() if line}


def check(root: Path, base: str | None = None, head: str = "HEAD",
          verbose: bool = False) -> tuple[list[str], dict[str, int]]:
    """Check a checkout. `--base` narrows the sweep to one revision range."""
    return check_corpus(Corpus(root), base, head, verbose)


def check_corpus(corpus: Corpus, base: str | None = None, head: str = "HEAD",
                 verbose: bool = False) -> tuple[list[str], dict[str, int]]:
    """Check an already loaded corpus."""
    root = corpus.root
    touched = changed_paths(root, base, head) if base is not None else set()
    totals = {"checked": 0, "symbols": 0, "external": 0, "unresolved": 0,
              "citations": 0, "docs": 0, "scanned": 0}
    findings: list[Finding] = []
    for doc in corpus.docs:
        text = corpus.document(doc)
        lines = text.splitlines()
        totals["scanned"] += 1
        counted = False
        for citation in citations_in(doc, text):
            # In base mode a citation is this range's business when its own
            # document changed or when the file it points at changed. The second
            # half is why an untouched doc is still read: the drift this gate
            # exists for comes from a source edit, not a doc edit.
            if base is not None and (doc not in touched
                                     and normalized(citation.path) not in touched):
                continue
            counted = True
            totals["citations"] += 1
            kind = classify(corpus, citation)
            totals[kind] += 1
            if kind != "checked":
                continue
            if annotating_symbol(lines, citation) is not None:
                totals["symbols"] += 1
            finding = check_citation(corpus, citation, lines)
            if finding is not None:
                findings.append(finding)
        if counted:
            totals["docs"] += 1

    allowed = {(entry["doc"], entry["citation"]) for entry in corpus.allowed}
    used: set[tuple[str, str]] = set()
    errors: list[str] = []
    for finding in findings:
        key = (finding.citation.doc, finding.citation.text)
        if key in allowed:
            used.add(key)
            if verbose:
                print(f"allowed  {finding}")
            continue
        errors.append(str(finding))

    if base is None:
        for entry in corpus.allowed:
            if (entry["doc"], entry["citation"]) not in used:
                errors.append(f"{entry['doc']}: stale allow entry for "
                              f"{entry['citation']}: it suppresses nothing now")
    if verbose:
        print(f"scanned {totals['scanned']} Markdown files with "
              f"{totals['citations']} citations")
    return errors, totals


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--root", type=Path, default=ROOT,
                        help="checkout root (default: this repository)")
    parser.add_argument("--all", action="store_true",
                        help="sweep every Markdown file (the default)")
    parser.add_argument("--base", metavar="REV",
                        help="only check citations this range could have broken")
    parser.add_argument("--head", default="HEAD", help="head revision used with --base")
    parser.add_argument("--verbose", action="store_true",
                        help="print the scan size and any allowed finding")
    args = parser.parse_args(argv)

    if args.base and args.all:
        print("FAIL: --base and --all are mutually exclusive", file=sys.stderr)
        return 2
    root = args.root.resolve()
    if not (root / "tools").is_dir():
        print(f"FAIL: {root} does not look like the furble repository", file=sys.stderr)
        return 2
    try:
        run_git(root, ["rev-parse", "--verify", "--quiet", f"{args.head}^{{commit}}"])
        if args.base:
            run_git(root, ["rev-parse", "--verify", "--quiet",
                           f"{args.base}^{{commit}}"])
        errors, totals = check(root, args.base, args.head, args.verbose)
    except (InvocationError, OSError) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        return 2

    for error in errors:
        print(f"FAIL: {error}", file=sys.stderr)
    scope = f"since {args.base} (incomplete by design)" if args.base else "in the tree"
    print(f"doc citations {scope}: {totals['checked']} line-checked "
          f"({totals['symbols']} symbol-checked), {totals['external']} external, "
          f"{totals['unresolved']} unresolved basename, {len(errors)} errors "
          f"of {totals['citations']} citations in {totals['docs']} files")
    return 1 if errors else 0


if __name__ == "__main__":
  raise SystemExit(main())
