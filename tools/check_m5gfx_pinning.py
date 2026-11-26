#!/usr/bin/env python3
"""Fail when a single-board image's M5GFX pin stops taking effect.

`platformio.ini` sets `-DM5GFX_BOARD=board_t::board_X` on an image that runs on
exactly one M5 board, and `post:patches/m5gfx_board.py` applies
`patches/m5gfx_board.patch` to the installed M5GFX before it is compiled.
`M5GFX::autodetect` then starts with `board = M5GFX_BOARD`, so every
`board == 0 || board == board_t::board_Y` guard for another board Y is false
and the compiler drops that arm, its panel and its touch driver.

The pin only suppresses anything while the pinned board matches an arm. The
Waveshare ESP32-S3-ETH pinned `board_M5StampS3`, which M5GFX 0.2.27 has no arm
for: no guard matched, the function fell through to `board_unknown`, and the
retry loop in `M5GFX::init_impl` ran the whole ESP32-S3 probe set five times.
The patch now returns the display-less StampS3 family before the first probe.

Three things can silently regress, and each one is checked here:

  1. an env stops pinning a board it must pin (`REQUIRED_PINS`);
  2. an env stops running the post script that applies the patch;
  3. the patched `autodetect` stops naming the pinned board in the branch the
     env compiles, which brings the fall-through and its probes back.

Check 3 reads the installed library source, so it needs a build of that env.
Without one it is skipped unless `--require-source` is given; the firmware CI
job passes both after its build. It is a source check on purpose: the runtime
suppression comes from the guard that wraps each probe, not from the optimizer,
so the source is where a regression shows up. Codegen is measured separately,
by the size budget and by the plan 352 evidence.

Exit status is 0 when the pin is intact, 1 when a check fails, and 2 for an
invalid invocation.
"""

from __future__ import annotations

import argparse
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
PATCH_SCRIPT = "post:patches/m5gfx_board.py"

# The release envs that must keep pinning. A single-board image pins its board.
# The Waveshare is headless and shares its W5500 pins GPIO 9 to 14 with the
# ESP32-S3 probes, so it pins the display-less StampS3. Every debug env
# inherits these flags through ${env:....build_flags}.
REQUIRED_PINS = {
  "m5stick-c": "board_M5StickC",
  "m5stack-core": "board_M5Stack",
  "m5stick-s3": "board_M5StickS3",
  "waveshare-s3-eth": "board_M5StampS3",
}

# Boards M5GFX cannot configure, so autodetect must return them before the
# first probe: there is no panel, touch or PMIC to identify and every probe
# belongs to a different board. Keep in step with patches/m5gfx_board.patch.
NO_PROBE_BOARDS = ("board_M5StampS3", "board_M5StampS3Bat", "board_M5StampS3Mini")

PIN = re.compile(r"-DM5GFX_BOARD=board_t::(board_\w+)")
AUTODETECT = "board_t M5GFX::autodetect(bool use_reset, board_t board)"
MARKER = "furble: a single-board image pins"

# Calls that drive a pin, a bus or a device. Inside the branch an env compiles,
# the pinned board's own arm may call these; nothing before that arm may.
PROBE_CALLS = (
  "_detect_i2c_device(",
  "_read_panel_id(",
  "_check_m5pm1(",
  "_check_m5ioe1(",
  "_pin_reset(",
  "_set_sd_spimode(",
  "bus_spi->init(",
  "spi::beginTransaction(",
  "lgfx::i2c::init(",
  "lgfx::i2c::beginTransaction(",
  "lgfx::pinMode(",
  "pinMode(",
  "gpio_hi(",
  "gpio_lo(",
)

TARGET_GUARDS = {
  "esp32": "#if !defined (CONFIG_IDF_TARGET) || defined (CONFIG_IDF_TARGET_ESP32)",
  "esp32s3": "#elif defined (CONFIG_IDF_TARGET_ESP32S3)",
}


class Section:
  """One `[name]` of platformio.ini, option values kept verbatim."""

  def __init__(self, name: str):
    self.name = name
    self.options: dict[str, str] = {}

  def get(self, option: str) -> str | None:
    return self.options.get(option)


def read_ini(path: Path) -> dict[str, Section]:
  """Parse platformio.ini without expanding ${...} references."""
  sections: dict[str, Section] = {}
  current: Section | None = None
  key: str | None = None
  for raw in path.read_text(encoding="utf-8").splitlines():
    stripped = raw.strip()
    if not stripped or stripped.startswith(";"):
      continue
    header = re.match(r"^\[(.+)\]$", stripped)
    if header is not None:
      current = sections.setdefault(header.group(1), Section(header.group(1)))
      key = None
      continue
    if current is None:
      continue
    if raw[0].isspace():
      if key is not None:
        current.options[key] = f"{current.options[key]}\n{stripped}"
      continue
    match = re.match(r"^([\w.+-]+)\s*=\s*(.*)$", stripped)
    if match is None:
      continue
    key = match.group(1)
    current.options[key] = match.group(2).strip()
  return sections


def expand(value: str, sections: dict[str, Section], option: str,
           seen: set[str] | None = None) -> str:
  """Expand `${env:x.build_flags}`, `${furble.build_flags}` and `${env.build_flags}`."""
  seen = set() if seen is None else seen

  def replace(match: re.Match[str]) -> str:
    section_name, _, referenced = match.group(1).rpartition(".")
    if not section_name:
      section_name, referenced = "env", match.group(1)
    if referenced != option:
      return match.group(0)
    if not (section_name == "env" or section_name == "furble"
            or section_name.startswith("env:")):
      return match.group(0)
    token = f"{section_name}.{option}"
    if token in seen:
      return match.group(0)
    seen.add(token)
    section = sections.get(section_name)
    raw = section.get(option) if section is not None else None
    if raw is None:
      return match.group(0)
    return expand(raw, sections, option, seen)

  return re.sub(r"\$\{([^}]+)\}", replace, value)


def section_for(sections: dict[str, Section], name: str) -> Section | None:
  """Look up an env by its bare name (`m5stick-c`) or its section key."""
  for key in (name, f"env:{name}"):
    section = sections.get(key)
    if section is not None:
      return section
  return None


def chain(sections: dict[str, Section], name: str) -> list[Section]:
  """The section followed by every `extends` ancestor."""
  result: list[Section] = []
  current = name
  while current is not None:
    section = section_for(sections, current)
    if section is None or section in result:
      break
    result.append(section)
    parent = section.get("extends") or ""
    current = None
    for candidate in parent.split(","):
      candidate = candidate.strip()
      if candidate.startswith("env:"):
        current = candidate
        break
  return result


def inherited(sections: dict[str, Section], name: str, option: str) -> str | None:
  """The option as PlatformIO resolves it: own, then ancestor, then [env]."""
  for section in chain(sections, name):
    value = section.get(option)
    if value is not None:
      return value
  default = sections.get("env")
  if default is not None and default.name != name:
    return default.get(option)
  return None


def pinned_board(sections: dict[str, Section], name: str) -> str | None:
  flags = inherited(sections, name, "build_flags") or ""
  pins = PIN.findall(expand(flags, sections, "build_flags"))
  if not pins:
    return None
  return pins[-1]


def board_option(sections: dict[str, Section], name: str) -> str:
  return (inherited(sections, name, "board") or "").strip()


def target_of(board: str) -> str:
  """The target M5GFX::autodetect selects on, for the boards furble builds."""
  return "esp32s3" if "s3" in board else "esp32"


def autodetect_body(text: str) -> str | None:
  start = text.find(AUTODETECT)
  if start < 0:
    return None
  brace = text.find("{", start)
  if brace < 0:
    return None
  depth = 0
  for index in range(brace, len(text)):
    if text[index] == "{":
      depth += 1
    elif text[index] == "}":
      depth -= 1
      if depth == 0:
        return text[brace : index + 1]
  return None


def target_branch(body: str, target: str) -> str:
  """The `#if`/`#elif` region of autodetect that this target compiles."""
  guard = TARGET_GUARDS[target]
  lines = body.splitlines()
  start = None
  for index, line in enumerate(lines):
    if line.startswith(guard):
      start = index
      break
  if start is None:
    return body
  depth = 1
  end = len(lines)
  for index in range(start + 1, len(lines)):
    line = lines[index]
    if line.startswith("#if"):
      depth += 1
    elif line.startswith("#endif"):
      depth -= 1
      if depth == 0:
        end = index
        break
    elif depth == 1 and line.startswith(("#elif", "#else")):
      end = index
      break
  return "\n".join(lines[start:end])


def first_probe(branch: str) -> tuple[int, str] | None:
  for index, line in enumerate(branch.splitlines()):
    code = line.split("//", 1)[0]
    for call in PROBE_CALLS:
      if call in code:
        return index, call
  return None


def mentions(branch: str, board: str) -> int | None:
  """Line of the first guard or case that names *board*."""
  pattern = re.compile(
      r"(?:board\s*==\s*board_t::" + board + r"\b"
      r"|case\s+board_t::" + board + r"\b)")
  for index, line in enumerate(branch.splitlines()):
    if pattern.search(line.split("//", 1)[0]):
      return index
  return None


def check_source(source: Path, env: str, board: str, target: str) -> list[str]:
  """Problems with the patched M5GFX::autodetect for one pinned env."""
  text = source.read_text(encoding="utf-8", errors="replace")
  rebuild = f" (build {env} to refresh .pio/libdeps if it is stale)"
  if MARKER not in text:
    return [f"{env}: {source} carries no pin, patches/m5gfx_board.patch did "
            f"not apply{rebuild}"]
  body = autodetect_body(text)
  if body is None:
    return [f"{env}: {source} has no M5GFX::autodetect"]
  failures: list[str] = []
  if "board = M5GFX_BOARD;" not in body:
    failures.append(f"{env}: M5GFX::autodetect no longer starts from M5GFX_BOARD")
  branch = target_branch(body, target)
  where = mentions(branch, board)
  if where is None:
    failures.append(
        f"{env}: {board} matches no arm in the {target} branch of "
        f"M5GFX::autodetect, so it falls through to board_unknown and every "
        f"probe runs, five times over the retry loop{rebuild}")
    return failures
  if board in NO_PROBE_BOARDS:
    probe = first_probe(branch)
    if probe is not None and probe[0] < where:
      failures.append(
          f"{env}: {board} is returned after {probe[1]} runs in the {target} "
          f"branch of M5GFX::autodetect")
  return failures


def find_source(root: Path, env: str) -> Path | None:
  candidate = root / ".pio" / "libdeps" / env / "M5GFX" / "src" / "M5GFX.cpp"
  return candidate if candidate.is_file() else None


def check(root: Path, envs: list[str] | None, require_source: bool) -> list[str]:
  sections = read_ini(root / "platformio.ini")
  names = sorted(name[4:] for name in sections if name.startswith("env:"))
  if envs is not None:
    missing = sorted(set(envs) - set(names))
    if missing:
      return [f"platformio.ini has no env {name}" for name in missing]
  selected = [name for name in names if envs is None or name in envs]

  failures: list[str] = []
  for env in selected:
    board = pinned_board(sections, env)
    required = REQUIRED_PINS.get(env)
    if required is None and board is None:
      continue
    if required is not None and board is None:
      failures.append(f"{env}: no -DM5GFX_BOARD in its build flags")
      continue
    if required is not None and board != required:
      failures.append(f"{env}: pins {board}, expected {required}")
    scripts = expand(inherited(sections, env, "extra_scripts") or "", sections,
                     "extra_scripts")
    if PATCH_SCRIPT not in scripts:
      failures.append(
          f"{env}: pins {board} without {PATCH_SCRIPT}, so nothing applies "
          f"patches/m5gfx_board.patch to its M5GFX")
      continue
    source = find_source(root, env)
    if source is None:
      if require_source:
        failures.append(
            f"{env}: M5GFX is not installed under .pio/libdeps, build the env "
            f"before checking the patched source")
      continue
    target = target_of(board_option(sections, env))
    failures.extend(check_source(source, env, board, target))
  return failures


def main(argv: list[str] | None = None) -> int:
  parser = argparse.ArgumentParser(description=__doc__,
                                   formatter_class=argparse.RawDescriptionHelpFormatter)
  parser.add_argument("--root", type=Path, default=ROOT,
                      help="checkout root (default: this repository)")
  parser.add_argument("--env", action="append", dest="envs",
                      help="limit the check to one env (repeatable)")
  parser.add_argument("--require-source", action="store_true",
                      help="fail instead of skipping when a pinned env has no installed M5GFX")
  args = parser.parse_args(argv)

  root = args.root.resolve()
  if not (root / "platformio.ini").is_file():
    print(f"{root} has no platformio.ini", file=sys.stderr)
    return 2
  failures = check(root, args.envs, args.require_source)
  for failure in failures:
    print(f"FAIL: {failure}", file=sys.stderr)
  if failures:
    return 1
  print("PASS: M5GFX board pinning")
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
