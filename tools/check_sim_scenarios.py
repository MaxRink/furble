#!/usr/bin/env python3
"""Check the complete, stdlib-readable simulator scenario inventory."""
from __future__ import annotations
import argparse
import json
from pathlib import Path

BOARDS = {"m5stick-s3", "m5stick-c", "m5stack-core"}
CAPABILITIES = {"fauxny", "gps", "imu", "feedback", "ir", "sd"}
OWNERS = {"power-gate": "sim-power", "bughunt": "sim-bughunt", "e2e": "sim-e2e", "invalid": "sim-parser"}
SUITE_COMMANDS = {"power-gate": "power-gate", "bughunt": "bughunt", "e2e": "run-e2e.sh", "invalid": "run-invalid.sh"}
SUITE_BOARDS = {"power-gate": {"m5stick-s3"}, "bughunt": {"m5stick-s3", "m5stick-c", "m5stack-core"}, "e2e": {"m5stick-s3", "m5stick-c", "m5stack-core"}, "invalid": {"m5stick-s3"}}
EXACT_BOARDS = {
  "page-matrix.txt": {"m5stick-s3", "m5stick-c", "m5stack-core"},
  "overflow-sweep.txt": {"m5stick-s3", "m5stick-c", "m5stack-core"},
  "level-overflow.txt": {"m5stick-s3", "m5stick-c", "m5stack-core"},
  "imu-diagnostics.txt": {"m5stick-s3", "m5stick-c", "m5stack-core"},
  "redraw-steady.txt": {"m5stick-s3", "m5stick-c", "m5stack-core"},
  "connstate-page-sweep.txt": {"m5stick-s3", "m5stick-c", "m5stack-core"},
  "statusbar-stability.txt": {"m5stick-s3", "m5stick-c", "m5stack-core"},
  "text-size-overflow-large.txt": {"m5stick-s3", "m5stick-c"},
  "text-size-overflow-small.txt": {"m5stick-s3", "m5stick-c"},
  "text-size-gate-stickc.txt": {"m5stick-c"},
  "text-size-clamp-stickc.txt": {"m5stick-c"},
}
WORKFLOW_CAPABILITIES = {
  "capability-submenus.txt": {"ir", "feedback", "sd"},
}
SUITES = set(OWNERS)
REQUIRED = {"path", "suite", "owner", "boards", "capabilities", "expected_exit", "certified"}

def check_manifest(root: Path, manifest_path: Path) -> list[str]:
  try:
    document = json.loads(manifest_path.read_text(encoding="utf-8"))
  except (OSError, json.JSONDecodeError) as error:
    return [f"manifest cannot be read: {error}"]
  entries = document.get("scenarios") if isinstance(document, dict) else None
  if not isinstance(entries, list):
    return ["manifest.scenarios must be a list"]
  errors: list[str] = []
  if document.get("owners") != OWNERS:
    errors.append("owners mapping does not match suite policy")
  if document.get("suites") != SUITE_COMMANDS:
    errors.append("suites mapping does not match CI owner policy")
  paths: list[str] = []
  for index, entry in enumerate(entries):
    label = f"entry {index + 1}"
    if not isinstance(entry, dict):
      errors.append(f"{label} is not a mapping")
      continue
    missing = REQUIRED - set(entry)
    if missing:
      errors.append(f"{label} missing: {', '.join(sorted(missing))}")
      continue
    path, suite, owner = entry["path"], entry["suite"], entry["owner"]
    if not isinstance(path, str):
      errors.append(f"{label} path is not a string")
      continue
    paths.append(path)
    if not (root / path).is_file():
      errors.append(f"stale manifest entry: {path}")
    parts = path.split("/")
    expected_suite = parts[2] if len(parts) > 2 and parts[2] in SUITES else "power-gate"
    if suite != expected_suite:
      errors.append(f"{path}: suite must be {expected_suite}")
    if owner != OWNERS.get(suite):
      errors.append(f"{path}: owner does not match suite mapping")
    if (not isinstance(entry["boards"], list) or not entry["boards"]
        or not set(entry["boards"]) <= BOARDS
        or not set(entry["boards"]) <= SUITE_BOARDS[suite]):
      errors.append(f"{path}: invalid board matrix")
    exact_boards = EXACT_BOARDS.get(Path(path).name)
    if exact_boards is not None and set(entry["boards"]) != exact_boards:
      errors.append(f"{path}: board matrix does not match workflow")
    if not isinstance(entry["capabilities"], list) or not set(entry["capabilities"]) <= CAPABILITIES:
      errors.append(f"{path}: invalid capabilities")
    if type(entry["expected_exit"]) is not int:
      errors.append(f"{path}: expected_exit must be an integer")
    elif entry["expected_exit"] != (2 if suite == "invalid" else 0):
      errors.append(f"{path}: unexpected exit for suite")
    scenario_text = (root / path).read_text(errors="ignore") if (root / path).is_file() else ""
    required_caps = ({cap for cap in ("fauxny", "gps", "imu") if f"seed {cap} true" in scenario_text}
                     if suite != "invalid" else set())
    if not required_caps <= set(entry["capabilities"]):
      errors.append(f"{path}: capabilities omit scripted requirements")
    workflow_caps = WORKFLOW_CAPABILITIES.get(Path(path).name, set())
    if not workflow_caps <= set(entry["capabilities"]):
      errors.append(f"{path}: capabilities omit workflow requirements")
    if suite == "invalid" and entry["capabilities"]:
      errors.append(f"{path}: invalid fixtures must have no capabilities")
    if not isinstance(entry["certified"], bool):
      errors.append(f"{path}: certified must be boolean")
    if not entry["certified"] and not entry.get("reason"):
      errors.append(f"{path}: non-certified entry requires a reason")
  for path in sorted({path for path in paths if paths.count(path) > 1}):
    errors.append(f"duplicate manifest entry: {path}")
  actual = {str(path.relative_to(root)) for path in (root / "sim/scenarios").rglob("*.txt")}
  for path in sorted(actual - set(paths)):
    errors.append(f"missing manifest entry: {path}")
  for path in sorted(set(paths) - actual):
    errors.append(f"stale manifest entry: {path}")
  return errors

def main() -> int:
  parser = argparse.ArgumentParser()
  parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
  parser.add_argument("--manifest", type=Path)
  args = parser.parse_args()
  errors = check_manifest(args.root, args.manifest or args.root / "sim/scenarios/manifest.json")
  if errors:
    print("\n".join(errors))
    return 1
  print("sim scenario manifest is complete")
  return 0

if __name__ == "__main__":
  raise SystemExit(main())
