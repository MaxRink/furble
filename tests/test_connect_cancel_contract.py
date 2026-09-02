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

The rule is deliberately coarse: a camera source that waits must also poll. A
source with no wait is exempt, and code inside `#if 0` is not code.
"""

import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parent.parent
VENDOR_DIR = ROOT / "lib" / "furble"

WAIT = re.compile(r"\bvTaskDelay\b|\bstd::this_thread::sleep_for\b")
POLL = "connectCancelled"


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


def waiting_camera_sources():
    """Camera sources that define _connect() and wait inside the attempt."""
    sources = sorted(VENDOR_DIR.glob("*.cpp"))
    waiting = []
    for path in sources:
        text = path.read_text()
        if "::_connect(" not in text:
            continue
        body = strip_disabled(text)
        if WAIT.search(body):
            waiting.append((path, body))
    return sources, waiting


class ConnectCancelContractTest(unittest.TestCase):
    def test_camera_sources_are_present(self):
        sources, _ = waiting_camera_sources()
        self.assertTrue(sources, "no camera sources found under lib/furble")
        self.assertTrue(
            any("::_connect(" in p.read_text() for p in sources),
            "no camera class defines _connect()",
        )

    def test_every_waiting_connect_polls_the_cancel_token(self):
        _, waiting = waiting_camera_sources()
        self.assertTrue(waiting, "expected at least one camera connect to wait")
        offenders = [
            str(path.relative_to(ROOT)) for path, body in waiting if POLL not in body
        ]
        self.assertEqual(
            offenders,
            [],
            "these camera sources wait inside a connect attempt without polling "
            "connectCancelled(), so Control::disconnect() cannot abort them: "
            f"{offenders}. See plans/167-control-zombie-connect-cancel.md.",
        )


if __name__ == "__main__":
    unittest.main()
