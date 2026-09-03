#!/usr/bin/env python3
"""Hold the plan 148 connect-cancel contract across every camera class.

Camera::connect() holds Camera::m_Mutex for the whole attempt, so any wait
inside a vendor connect blocks the target task's Camera::disconnect() behind it.
Control::disconnect() sets the per-camera cancel token and then waits a bounded
time for the teardown to settle; a wait that never polls connectCancelled()
cannot be aborted, so the teardown burns its cap and drains the target with the
attempt still running. That is issue 271, and it was reachable because
CanonEOSSmart waited 60 s for a pairing confirmation without polling and
DJIOsmo waited the whole 30 s on its protocol handshake the same way.

Three things this gate learned the hard way, from review of PR 272:

1. Comments and string literals are stripped before the scan, and the poll must
   appear as a call. An earlier version matched the bare word anywhere in the
   file, so deleting a poll and leaving `// TODO: should poll connectCancelled()`
   kept the gate green.
2. Every camera source is scanned, not only those defining `_connect()`. A
   shared base such as Fujifilm.cpp carries the wait for its subclasses and
   defines no `_connect()` of its own, so it was never checked.
3. A wait is any blocking primitive, not just vTaskDelay. A timed semaphore,
   queue receive, task notification or event group wait blocks exactly the same
   way, and those are the shapes most likely to appear in a new vendor.

Code inside `#if 0` is not code.
"""

import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parent.parent
VENDOR_DIR = ROOT / "lib" / "furble"

# Blocking primitives a vendor connect can park in. A NimBLE call with its own
# internal timeout cannot be spotted by name and is not in scope here; that is
# what the hardware gate and review are for.
WAIT = re.compile(
    r"\b(?:vTaskDelay|vTaskDelayUntil|ulTaskNotifyTake|xTaskNotifyWait"
    r"|xSemaphoreTake|xQueueReceive|xQueueSemaphoreTake|xEventGroupWaitBits"
    r"|usleep|nanosleep)\s*\(|\bstd::this_thread::sleep_for\s*\("
)
POLL = re.compile(r"\bconnectCancelled\s*\(")


def strip_comments_and_strings(text):
    """Remove comments and string or character literals.

    A comment naming connectCancelled() must not satisfy the gate, and a wait
    named inside a log string must not trip it.
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
            i = n if j < 0 else j + 2
            out.append(" ")
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
    """Drop `#if 0` blocks so dead code is not held to the contract."""
    out = []
    depth = 0
    for line in text.splitlines():
        stripped = line.strip()
        if depth:
            if stripped.startswith("#if"):
                depth += 1
            elif stripped.startswith("#endif"):
                depth -= 1
            continue
        if re.match(r"#if\s+0\b", stripped):
            depth = 1
            continue
        out.append(line)
    return "\n".join(out)


def scannable(path):
    return strip_comments_and_strings(strip_disabled(path.read_text()))


def waiting_camera_sources():
    """Every camera source that blocks somewhere, with its scannable body."""
    sources = sorted(VENDOR_DIR.glob("*.cpp"))
    return sources, [(p, body) for p in sources if WAIT.search(body := scannable(p))]


class ConnectCancelContractTest(unittest.TestCase):
    def test_camera_sources_are_present(self):
        sources, _ = waiting_camera_sources()
        self.assertTrue(sources, "no camera sources found under lib/furble")
        self.assertTrue(
            any("::_connect(" in p.read_text() for p in sources),
            "no camera class defines _connect()",
        )

    def test_every_waiting_source_polls_the_cancel_token(self):
        _, waiting = waiting_camera_sources()
        self.assertTrue(waiting, "expected at least one camera source to block")
        offenders = [
            str(path.relative_to(ROOT)) for path, body in waiting if not POLL.search(body)
        ]
        self.assertEqual(
            offenders,
            [],
            "these camera sources block inside a connect attempt without calling "
            "connectCancelled(), so Control::disconnect() cannot abort them: "
            f"{offenders}. See plans/167-control-zombie-connect-cancel.md.",
        )

    def test_a_comment_does_not_satisfy_the_gate(self):
        """The gate's own teeth, since a comment satisfying it is how it failed."""
        body = scannable_text('void f(void) { vTaskDelay(1); }\n'
                              '// connectCancelled() should be polled here\n')
        self.assertTrue(WAIT.search(body))
        self.assertFalse(POLL.search(body))

    def test_a_log_string_does_not_trip_the_gate(self):
        body = scannable_text('void f(void) { log("vTaskDelay(1) was skipped"); }\n')
        self.assertFalse(WAIT.search(body))


def scannable_text(text):
    return strip_comments_and_strings(strip_disabled(text))


if __name__ == "__main__":
    unittest.main()
