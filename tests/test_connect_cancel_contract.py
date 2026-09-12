#!/usr/bin/env python3
"""Hold the plan 148 connect-cancel contract across every camera class.

Camera::connect() holds Camera::m_Mutex for the whole attempt, so any wait
inside a vendor connect blocks the target task's Camera::disconnect() behind it.
Control::disconnect() sets the per-camera cancel token and then waits a bounded
time for the teardown to settle; a wait that never polls connectCancelled()
cannot be aborted, so the teardown burns its cap and drains the target with the
attempt still running. That is issue 271, and it was reachable because
CanonEOSSmart waited 60 s for a pairing confirmation without polling, DJIOsmo
waited the whole 30 s on its protocol handshake, and three Nikon waits did the
same.

The rule is scoped to the function that waits: the poll must appear in the same
braced body as the wait. Every earlier scope was wider and every one of them was
escapable, as review demonstrated:

1. Bare-word matching over the file. Deleting a poll and leaving
   `// TODO: should poll connectCancelled()` kept the gate green. Comments and
   string literals are stripped and the poll must be a call.
2. Only scanning files that define `_connect()`. A shared base carrying the wait
   for its subclasses, such as Fujifilm.cpp, was never checked.
3. Only recognising vTaskDelay and sleep_for. A timed xQueueReceive,
   xSemaphoreTake, task notification or event group wait blocks identically, and
   widening this is what found the three Nikon waits.
4. File scope. One poll anywhere immunised every wait in the file, so a second
   unpolled wait was invisible; worse, NikonBase.cpp defines an accessor named
   connectCancelled(), and that definition alone satisfied a file-scope search
   for both of its sliced waits.
5. Sources only. A wait in an inline helper in a header was never seen.

Known residual hole, written down rather than claimed closed: a NimBLE call with
its own internal timeout is a blocking wait that cannot be recognised by name.
That is the shape behind the Fujifilm Secure stale-bond window, and it is what
the hardware gate and review cover.

Code inside `#if 0` is not code, but the live `#else` branch of one is.
"""

import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parent.parent
VENDOR_DIR = ROOT / "lib" / "furble"

# Blocking primitives a vendor connect can park in.
WAIT = re.compile(
    r"\b(?:vTaskDelay|vTaskDelayUntil|ulTaskNotifyTake|xTaskNotifyWait"
    r"|xSemaphoreTake|xQueueReceive|xQueueSemaphoreTake|xEventGroupWaitBits"
    r"|usleep|nanosleep)\s*\(|\bstd::this_thread::sleep_for\s*\("
)
POLL = re.compile(r"\bconnectCancelled\s*\(")

# A block opened by one of these is control flow or a type, not a function body.
NOT_A_FUNCTION = re.compile(
    r"\b(?:if|else|for|while|switch|do|try|catch|class|struct|union|enum"
    r"|namespace|extern)\s*$"
)


def strip_comments_and_strings(text):
    """Remove comments and string or character literals, preserving newlines.

    A comment naming connectCancelled() must not satisfy the gate, and a wait
    named inside a log string must not trip it. Newlines are preserved so the
    reported line numbers stay usable.
    """
    out = []
    i = 0
    n = len(text)
    while i < n:
        two = text[i:i + 2]
        if two == "//":
            j = text.find("\n", i)
            i = n if j < 0 else j
            continue
        if two == "/*":
            j = text.find("*/", i + 2)
            chunk = text[i:n if j < 0 else j + 2]
            out.append("\n" * chunk.count("\n"))
            i = n if j < 0 else j + 2
            continue
        ch = text[i]
        if ch in "\"'":
            quote = ch
            i += 1
            while i < n:
                if text[i] == "\\":
                    i += 2
                    continue
                if text[i] == quote:
                    i += 1
                    break
                i += 1
            out.append(" ")
            continue
        out.append(ch)
        i += 1
    return "".join(out)


def strip_disabled(text):
    """Drop `#if 0` blocks, keeping the live `#else` branch of one.

    Dropping straight through to the matching `#endif` also deleted the branch
    that actually compiles, which hid any wait in it.
    """
    out = []
    # Each entry is True while the current conditional is being dropped.
    stack = []
    for line in text.splitlines():
        stripped = line.strip()
        if re.match(r"#\s*if", stripped):
            dropping = bool(stack and stack[-1]) or bool(
                re.match(r"#\s*if\s+0\b", stripped)
            )
            stack.append(dropping)
            out.append("")
            continue
        if re.match(r"#\s*(elif|else)\b", stripped) and stack:
            outer_drops = any(stack[:-1])
            if not outer_drops:
                # Flip only the innermost `#if 0`, whose else branch is live.
                stack[-1] = not stack[-1]
            out.append("")
            continue
        if re.match(r"#\s*endif\b", stripped) and stack:
            stack.pop()
            out.append("")
            continue
        out.append("" if any(stack) else line)
    return "\n".join(out)


def scannable(text):
    return strip_comments_and_strings(strip_disabled(text))


def function_bodies(text):
    """Yield (start, end) spans of every function body in the scannable text.

    A body is a braced block whose opening brace is preceded by a `)`, allowing
    trailing specifiers, and is not opened by a control-flow or type keyword.
    Nested blocks belong to the enclosing function, so only the outermost such
    block is reported and an inner lambda is covered by it.
    """
    spans = []
    depth = 0
    open_stack = []
    for i, ch in enumerate(text):
        if ch == "{":
            open_stack.append((i, depth))
            depth += 1
            continue
        if ch != "}":
            continue
        depth -= 1
        if not open_stack:
            continue
        start, start_depth = open_stack.pop()
        if start_depth != 0 and not any(s <= start < e for s, e in spans):
            # Nested inside something that is not a function body, such as a
            # class in a header. Still eligible.
            pass
        prefix = text[:start].rstrip()
        # Cut back to the previous statement or block boundary.
        cut = max(prefix.rfind(";"), prefix.rfind("}"), prefix.rfind("{"))
        signature = prefix[cut + 1:].strip()
        signature = re.sub(r"\)\s*(?:const|noexcept|override|final|\s)*$", ")", signature)
        if not signature.endswith(")"):
            continue
        head = re.sub(r"\(.*", "", signature, flags=re.S).strip()
        if NOT_A_FUNCTION.search(head + " "):
            continue
        if any(s <= start and i <= e for s, e in spans):
            continue
        spans.append((start, i))
    return spans


def camera_sources():
    return sorted(list(VENDOR_DIR.glob("*.cpp")) + list(VENDOR_DIR.glob("*.h")))


def offending_waits():
    """Every (file, line) whose enclosing function waits without polling."""
    offenders = []
    checked = 0
    for path in camera_sources():
        text = scannable(path.read_text())
        spans = function_bodies(text)
        covered = []
        for start, end in spans:
            body = text[start:end + 1]
            if not WAIT.search(body):
                continue
            checked += 1
            covered.append((start, end))
            if not POLL.search(body):
                line = text[:start].count("\n") + 1
                offenders.append(f"{path.relative_to(ROOT)}:{line}")
        # A wait outside every function body cannot be cancel-polled at all.
        for match in WAIT.finditer(text):
            if any(s <= match.start() <= e for s, e in spans):
                continue
            checked += 1
            line = text[:match.start()].count("\n") + 1
            offenders.append(f"{path.relative_to(ROOT)}:{line} (outside any function)")
    return offenders, checked


class ConnectCancelContractTest(unittest.TestCase):
    def test_camera_sources_are_present(self):
        sources = camera_sources()
        self.assertTrue(sources, "no camera sources found under lib/furble")
        self.assertTrue(
            any("::_connect(" in p.read_text() for p in sources),
            "no camera class defines _connect()",
        )

    def test_every_waiting_function_polls_the_cancel_token(self):
        offenders, checked = offending_waits()
        self.assertTrue(checked, "expected at least one camera function to block")
        self.assertEqual(
            offenders,
            [],
            "these functions block inside a connect attempt without calling "
            "connectCancelled() in the same body, so Control::disconnect() cannot "
            f"abort them: {offenders}. See "
            "plans/170-control-zombie-connect-cancel.md.",
        )

    def test_a_comment_does_not_satisfy_the_gate(self):
        body = scannable(
            "void f(void) {\n  vTaskDelay(1);\n}\n"
            "// connectCancelled() should be polled in f\n"
        )
        spans = function_bodies(body)
        self.assertEqual(len(spans), 1)
        self.assertFalse(POLL.search(body[spans[0][0]:spans[0][1]]))

    def test_a_log_string_does_not_trip_the_gate(self):
        self.assertFalse(WAIT.search(scannable('void f(void) { g("vTaskDelay(1)"); }\n')))

    def test_a_poll_in_another_function_does_not_cover_a_wait(self):
        """R2: an accessor named connectCancelled must not immunise the file."""
        text = scannable(
            "bool C::connectCancelled(void) const { return m_C->connectCancelled(); }\n"
            "bool C::wait(void) { vTaskDelay(1); return true; }\n"
        )
        spans = function_bodies(text)
        waiting = [s for s in spans if WAIT.search(text[s[0]:s[1]])]
        self.assertEqual(len(waiting), 1)
        self.assertFalse(POLL.search(text[waiting[0][0]:waiting[0][1]]))

    def test_a_second_wait_in_a_polling_file_is_seen(self):
        """Mc: file scope let one poll cover every wait in the file."""
        text = scannable(
            "bool C::a(void) { if (connectCancelled()) return false; vTaskDelay(1); return true; }\n"
            "bool C::b(void) { vTaskDelay(1); return true; }\n"
        )
        spans = function_bodies(text)
        unpolled = [
            s for s in spans
            if WAIT.search(text[s[0]:s[1]]) and not POLL.search(text[s[0]:s[1]])
        ]
        self.assertEqual(len(unpolled), 1)

    def test_headers_are_scanned(self):
        """Mb: an inline helper in a header was invisible."""
        self.assertTrue(any(p.suffix == ".h" for p in camera_sources()))

    def test_the_live_else_branch_of_if_zero_survives(self):
        text = strip_disabled("#if 0\nvTaskDelay(1);\n#else\nvTaskDelay(2);\n#endif\n")
        self.assertNotIn("vTaskDelay(1)", text)
        self.assertIn("vTaskDelay(2)", text)


if __name__ == "__main__":
    unittest.main()
