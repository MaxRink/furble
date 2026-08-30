#!/usr/bin/env python3
"""Check the complete, stdlib-readable simulator scenario inventory."""
from __future__ import annotations
import argparse
import json
from pathlib import Path, PurePosixPath

BOARDS = {"m5stick-s3", "m5stick-c", "m5stack-core"}
CAPABILITIES = {"fauxny", "gps", "imu", "feedback", "ir", "sd"}
OWNERS = {"power-gate": "sim-power", "bughunt": "sim-bughunt", "e2e": "sim-e2e", "invalid": "sim-parser"}
SUITE_COMMANDS = {
  "power-gate": "check_sim_scenarios.py --list-certified --suite power-gate",
  "bughunt": "check_sim_scenarios.py --list-certified --suite bughunt",
  "e2e": "check_sim_scenarios.py --list-certified --suite e2e",
  "invalid": "check_sim_scenarios.py --list-certified --suite invalid",
}
SUITE_BOARDS = {"power-gate": {"m5stick-s3"}, "bughunt": {"m5stick-s3", "m5stick-c", "m5stack-core"}, "e2e": {"m5stick-s3", "m5stick-c", "m5stack-core"}, "invalid": {"m5stick-s3"}}
REQUIRED_SELECTIONS = {
  "power-gate": {"m5stick-s3"},
  "bughunt": {"m5stick-s3", "m5stick-c", "m5stack-core"},
  "e2e": {"m5stick-s3", "m5stick-c", "m5stack-core"},
  "invalid": {"m5stick-s3"},
}
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
OPTIONAL = {"reason"}

def expected_suite(path: str) -> str | None:
  candidate = PurePosixPath(path)
  prefix = PurePosixPath("sim/scenarios")
  if candidate.is_absolute() or ".." in candidate.parts or candidate.as_posix() != path:
    return None
  try:
    relative = candidate.relative_to(prefix)
  except ValueError:
    return None
  if not relative.parts or any(char.isspace() for char in path):
    return None
  if len(relative.parts) == 1:
    return "power-gate"
  return relative.parts[0] if relative.parts[0] in SUITES else None

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
    extra = set(entry) - REQUIRED - OPTIONAL
    if extra:
      errors.append(f"{label} has unknown fields: {', '.join(sorted(extra))}")
    path, suite, owner = entry["path"], entry["suite"], entry["owner"]
    if not isinstance(path, str):
      errors.append(f"{label} path is not a string")
      continue
    paths.append(path)
    if not (root / path).is_file():
      errors.append(f"stale manifest entry: {path}")
    path_suite = expected_suite(path)
    if path_suite is None:
      errors.append(f"{path}: path is outside a supported suite")
    elif suite != path_suite:
      errors.append(f"{path}: suite must be {path_suite}")
    if suite not in SUITES:
      errors.append(f"{path}: unknown suite")
      continue
    if owner != OWNERS.get(suite):
      errors.append(f"{path}: owner does not match suite mapping")
    if (not isinstance(entry["boards"], list) or not entry["boards"]
        or not set(entry["boards"]) <= BOARDS
        or not set(entry["boards"]) <= SUITE_BOARDS[suite]):
      errors.append(f"{path}: invalid board matrix")
    elif len(entry["boards"]) != len(set(entry["boards"])):
      errors.append(f"{path}: duplicate boards")
    exact_boards = EXACT_BOARDS.get(Path(path).name)
    if exact_boards is not None and set(entry["boards"]) != exact_boards:
      errors.append(f"{path}: board matrix does not match workflow")
    if not isinstance(entry["capabilities"], list) or not set(entry["capabilities"]) <= CAPABILITIES:
      errors.append(f"{path}: invalid capabilities")
    elif len(entry["capabilities"]) != len(set(entry["capabilities"])):
      errors.append(f"{path}: duplicate capabilities")
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
  for suite, boards in REQUIRED_SELECTIONS.items():
    for board in sorted(boards):
      if not any(isinstance(entry, dict) and entry.get("certified") is True
                 and entry.get("suite") == suite and board in entry.get("boards", [])
                 for entry in entries):
        errors.append(f"{suite}: no certified scenarios for required board {board}")
  return errors

def certified_paths(root: Path, manifest_path: Path, suite: str, board: str) -> list[str]:
  errors = check_manifest(root, manifest_path)
  if errors:
    raise ValueError("\n".join(errors))
  document = json.loads(manifest_path.read_text(encoding="utf-8"))
  return sorted(entry["path"] for entry in document["scenarios"]
                if entry["certified"] and entry["suite"] == suite and board in entry["boards"])

def main() -> int:
  parser = argparse.ArgumentParser()
  parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
  parser.add_argument("--manifest", type=Path)
  parser.add_argument("--list-certified", action="store_true")
  parser.add_argument("--suite", choices=sorted(SUITES))
  parser.add_argument("--board", choices=sorted(BOARDS), default="m5stick-s3")
  args = parser.parse_args()
  manifest = args.manifest or args.root / "sim/scenarios/manifest.json"
  errors = check_manifest(args.root, manifest)
  if errors:
    print("\n".join(errors))
    return 1
  if args.list_certified:
    if args.suite is None:
      parser.error("--list-certified requires --suite")
    print("\n".join(certified_paths(args.root, manifest, args.suite, args.board)))
    return 0
  print("sim scenario manifest is complete")
  return 0

if __name__ == "__main__":
  raise SystemExit(main())
