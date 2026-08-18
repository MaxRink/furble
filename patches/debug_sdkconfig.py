import os
import re

from platformio.project.helpers import get_project_dir

Import("env")

if env.IsIntegrationDump():
  Return()


CONFIG_LINE = re.compile(r"^(# )?(CONFIG_[A-Za-z0-9_]+)(?:=.*| is not set)$")


def config_key(line):
  match = CONFIG_LINE.match(line.rstrip("\n"))
  return match.group(2) if match else None


def config_map(lines, path):
  values = {}
  for line in lines:
    key = config_key(line)
    if key is None:
      continue
    if key in values:
      raise RuntimeError("duplicate %s in %s" % (key, path))
    values[key] = line.rstrip("\n")
  return values


def read_fragment(path):
  with open(path, encoding="utf-8") as fragment_file:
    lines = fragment_file.readlines()

  values = config_map(lines, path)
  if not values:
    raise RuntimeError("debug sdkconfig fragment is empty: %s" % path)
  for line in lines:
    stripped = line.strip()
    if stripped and not stripped.startswith("#") and config_key(line) is None:
      raise RuntimeError("invalid sdkconfig fragment line in %s: %s" % (path, stripped))
  return values


def merge_config(source_path, fragment_path, overlay_path):
  with open(source_path, encoding="utf-8") as source_file:
    source_lines = source_file.readlines()

  source_values = config_map(source_lines, source_path)
  fragment_values = read_fragment(fragment_path)
  merged_lines = list(source_lines)
  replaced = set()

  for index, line in enumerate(merged_lines):
    key = config_key(line)
    if key in fragment_values:
      merged_lines[index] = fragment_values[key] + "\n"
      replaced.add(key)

  if merged_lines and not merged_lines[-1].endswith("\n"):
    merged_lines[-1] += "\n"
  for key, value in fragment_values.items():
    if key not in replaced:
      merged_lines.append(value + "\n")

  merged_values = config_map(merged_lines, overlay_path)
  if set(merged_values) != set(source_values) | set(fragment_values):
    raise RuntimeError("debug sdkconfig merge changed the symbol set")

  for key, value in source_values.items():
    if key not in fragment_values and merged_values[key] != value:
      raise RuntimeError("debug sdkconfig merge changed %s" % key)
  for key, value in fragment_values.items():
    if merged_values[key] != value:
      raise RuntimeError("debug sdkconfig fragment did not apply %s" % key)

  with open(overlay_path, "w", encoding="utf-8", newline="") as overlay_file:
    overlay_file.writelines(merged_lines)


project_dir = get_project_dir()
board_config = env.BoardConfig()
source_path = env.subst(board_config.get("build.esp-idf.sdkconfig_path", ""))
if not source_path:
  raise RuntimeError("debug environment has no release sdkconfig path")
if not os.path.isabs(source_path):
  source_path = os.path.join(project_dir, source_path)
source_path = os.path.abspath(source_path)

fragment_path = os.path.join(project_dir, "sdkconfig.debug")
overlay_path = os.path.join(env.subst("$BUILD_DIR"), "sdkconfig.debug")
os.makedirs(os.path.dirname(overlay_path), exist_ok=True)
merge_config(source_path, fragment_path, overlay_path)

board_config.update("build.esp-idf.sdkconfig_path", overlay_path)
