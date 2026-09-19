#!/usr/bin/env python3
"""Reject a malformed inherited fuzz continuation before simulator boot."""

import os
import subprocess
import sys
import tempfile


if len(sys.argv) != 2:
    raise SystemExit("usage: check-fuzz-checkpoint.py SIMULATOR")

with tempfile.TemporaryFile() as checkpoint:
    checkpoint.write(b"not a fuzz checkpoint\n")
    checkpoint.flush()
    checkpoint.seek(0)
    env = os.environ.copy()
    env["FURBLE_SIM_FUZZ_CHECKPOINT_FD"] = str(checkpoint.fileno())
    result = subprocess.run(
        [sys.argv[1], "--fuzz", "--seed", "2", "--fuzz-steps", "600"],
        env=env,
        pass_fds=(checkpoint.fileno(),),
        capture_output=True,
        text=True,
        check=False,
    )

if result.returncode != 1 or "invalid or unreadable continuation" not in result.stderr:
    sys.stderr.write(result.stdout)
    sys.stderr.write(result.stderr)
    raise SystemExit(
        f"malformed fuzz checkpoint returned {result.returncode}, expected fail-closed status 1"
    )

print("PASS malformed fuzz checkpoint rejected")
