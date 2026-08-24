#!/bin/sh

# Keep docs/sim.md tied to the simulator vocabulary. This is deliberately a
# small source-text gate. It does not need a build or network access.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
exec python3 - "$ROOT" "$@" <<'PY'
import pathlib
import re
import sys

root = pathlib.Path(sys.argv[1])
check_links = "--links" in sys.argv[2:]
docs_path = root / "docs" / "sim.md"
docs = docs_path.read_text(encoding="utf-8")
sources = "\n".join(
    (root / path).read_text(encoding="utf-8", errors="replace")
    for path in (
        "sim/driver.cpp",
        "src/FurbleUI.cpp",
        "sim/main.cpp",
        "sim/FurbleControlSim.cpp",
        "tests/host",
    )
    if (root / path).is_file()
)
if root.joinpath("tests/host").is_dir():
    sources += "\n" + "\n".join(
        p.read_text(encoding="utf-8", errors="replace")
        for p in root.joinpath("tests/host").rglob("*")
        if p.is_file()
    )

missing = []

# Fixed action lines are the command vocabulary. Parameter placeholders such as
# `drop N` validate against the command prefix.
action_start = docs.index("### `action` commands")
action_end = docs.index("The `toggle` action", action_start)
for command in re.findall(r"^action ([^`\n]+)$", docs[action_start:action_end], re.MULTILINE):
    token = command.split()[0]
    if token not in sources:
        missing.append(f"action {command}")

for namespace, key in re.findall(r"`(ui|control|camera|setting)\.([a-z0-9_]+)`", docs):
    # Production query code switches on the suffix. Control/camera/setting
    # names are likewise registered without the scenario namespace prefix.
    if f'"{key}"' not in sources:
        missing.append(f"{namespace}.{key}")

if check_links:
    for target in re.findall(r"\[[^]]+\]\(([^)]+)\)", docs):
        if target.startswith(("http://", "https://", "#")):
            continue
        target_path = (docs_path.parent / target).resolve()
        if not target_path.is_file():
            missing.append(f"link {target}")

if missing:
    print("missing documented simulator tokens:", file=sys.stderr)
    for item in missing:
        print(f"  {item}", file=sys.stderr)
    raise SystemExit(1)

print("sim documentation tokens are present")
if check_links:
    print("sim documentation links resolve")
PY
