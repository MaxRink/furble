"""Coverage inventory gate for firmware sources.

A measured coverage audit found 10 firmware files (4831 of 24794 SLOC, 19.5
percent) that neither the host test suite (tests/host) nor the simulator (sim/)
compiles. Nothing in CI stopped a new firmware source from landing outside both
build lists. This gate closes that hole. Every src/ and lib/ .cpp must be built
by the host suite, built by the simulator, or carry an explicit one-line reason
in tests/build_inventory_exemptions.json.

The CMake forms this gate understands, which are the forms the two build files
actually use:

  - a source path built from a variable reference, such as
    ${FURBLE_ROOT}/src/FurbleGPS.cpp or ${FURBLE_PROTOCOL_DIR}/ProvisionTLV.cpp,
    optionally double or single quoted. A bare relative path is not recognised,
    because neither build file names a firmware source that way.
  - set(VAR value) for the variables those paths reference, taking the first
    whitespace separated token as the value. Nested references are expanded.
  - a # comment to end of line, which is stripped before any path is read, so a
    commented out source counts as not built. A quoted # is not treated as a
    comment, which matches CMake for the plain assignments these files use.

sim/build.sh is read with its own $ROOT and ${ROOT} form, with the same comment
stripping.

Anything outside these forms is invisible to the gate. That fails closed for a
source (it reports as uncovered) and open for an exemption (it reports as
redundant), so a parse gap is loud rather than silent.
"""
from pathlib import Path
import json
import posixpath
import re
import shutil
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
EXEMPTIONS = "tests/build_inventory_exemptions.json"
HOST_CMAKE = "tests/host/CMakeLists.txt"
SIM_CMAKE = "sim/CMakeLists.txt"
SIM_SHELL = "sim/build.sh"
# Canary source for the drop fixtures below. Any vendor source works.
CANARY = "lib/furble/CanonEOS.cpp"

# Directories that hold firmware sources. Each is scanned non-recursively.
FIRMWARE_DIRS = (
  "src",
  "lib/furble",
  "lib/furble/protocol",
  "lib/preferences",
  "lib/blowfish",
)

SET_RE = re.compile(r"set\(\s*([A-Za-z0-9_]+)\s+([^)\n]*)", re.IGNORECASE)
CMAKE_PATH_RE = re.compile(r"\$\{[A-Za-z0-9_]+\}[^\s\"')]*\.cpp")
SHELL_PATH_RE = re.compile(r"\$\{?ROOT\}?/[^\s\"']*\.cpp")


def strip_comments(text: str) -> str:
  """Remove every # comment to end of line, keeping a quoted # intact.

  A commented out source has to read as not built, and a comment that merely
  mentions a path must not make that path look built. Both build files quote
  paths rather than embedding a bare #, so a simple quote aware scan is enough.
  """
  out = []
  for line in text.splitlines():
    quote = None
    cut = len(line)
    for index, character in enumerate(line):
      if quote is not None:
        if character == quote:
          quote = None
      elif character in "\"'":
        quote = character
      elif character == "#":
        cut = index
        break
    out.append(line[:cut])
  return "\n".join(out)


def normalise(path: str) -> str:
  """Collapse a repo path to a plain relative posix path."""
  cleaned = posixpath.normpath(path.strip().strip('"').strip("'"))
  return cleaned[2:] if cleaned.startswith("./") else cleaned


def is_firmware(path: str) -> bool:
  return posixpath.dirname(path) in FIRMWARE_DIRS and path.endswith(".cpp")


def firmware_sources(root: Path) -> set:
  """Every firmware source on disk, as repo relative paths."""
  found = set()
  for directory in FIRMWARE_DIRS:
    for source in sorted((root / directory).glob("*.cpp")):
      found.add(source.relative_to(root).as_posix())
  return found


def cmake_variables(text: str, own_dir: str) -> dict:
  """Read every set(VAR value) in a CMake file, plus the builtin dir names."""
  variables = {
    "CMAKE_CURRENT_SOURCE_DIR": own_dir,
    "CMAKE_CURRENT_LIST_DIR": own_dir,
    "PROJECT_SOURCE_DIR": own_dir,
  }
  for name, value in SET_RE.findall(text):
    first = value.strip().split()
    if first:
      variables[name] = first[0].strip('"').strip("'")
  return variables


def expand(value: str, variables: dict) -> str:
  """Substitute ${VAR} references until the value stops changing."""
  for _ in range(10):
    expanded = re.sub(r"\$\{([A-Za-z0-9_]+)\}",
                      lambda match: variables.get(match.group(1), match.group(0)),
                      value)
    if expanded == value:
      break
    value = expanded
  return value


def cmake_paths(text: str, own_dir: str, scope: str = None) -> set:
  """Firmware sources named by a CMake file, with variables resolved.

  Variables come from the whole file. Paths come from scope when given, so a
  caller can read one source list block while still resolving file wide names.
  """
  variables = cmake_variables(text, own_dir)
  paths = set()
  for token in CMAKE_PATH_RE.findall(text if scope is None else scope):
    resolved = expand(token, variables)
    if "${" in resolved:
      continue
    candidate = normalise(resolved)
    if is_firmware(candidate):
      paths.add(candidate)
  return paths


def host_sources(root: Path) -> set:
  """Firmware sources compiled by the host test suite."""
  path = root / HOST_CMAKE
  if not path.exists():
    return set()
  return cmake_paths(strip_comments(path.read_text(encoding="utf-8")), "tests/host")


def sim_block(text: str) -> str:
  """The set(FURBLE_SOURCES ...) block of sim/CMakeLists.txt."""
  start = text.find("set(FURBLE_SOURCES")
  if start < 0:
    return ""
  depth = 0
  for index in range(start, len(text)):
    if text[index] == "(":
      depth += 1
    elif text[index] == ")":
      depth -= 1
      if depth == 0:
        return text[start:index + 1]
  return text[start:]


def sim_cmake_sources(root: Path) -> set:
  """Firmware sources listed by the simulator CMake build."""
  path = root / SIM_CMAKE
  if not path.exists():
    return set()
  text = strip_comments(path.read_text(encoding="utf-8"))
  return cmake_paths(text, "sim", sim_block(text))


def sim_shell_sources(root: Path) -> set:
  """Firmware sources listed by sim/build.sh."""
  path = root / SIM_SHELL
  if not path.exists():
    return set()
  paths = set()
  for token in SHELL_PATH_RE.findall(strip_comments(path.read_text(encoding="utf-8"))):
    candidate = normalise(re.sub(r"^\$\{?ROOT\}?/", "", token))
    if is_firmware(candidate):
      paths.add(candidate)
  return paths


def load_exemptions(root: Path) -> dict:
  path = root / EXEMPTIONS
  if not path.exists():
    return {}
  return json.loads(path.read_text(encoding="utf-8"))


def check_inventory(root: Path) -> list:
  """Return every inventory error found under root. Empty means clean."""
  errors = []
  exemptions = load_exemptions(root)
  sources = firmware_sources(root)
  cmake_sim = sim_cmake_sources(root)
  shell_sim = sim_shell_sources(root)

  for path in sorted(cmake_sim - shell_sim):
    errors.append("sim lists disagree: %s is in %s but not %s" % (path, SIM_CMAKE, SIM_SHELL))
  for path in sorted(shell_sim - cmake_sim):
    errors.append("sim lists disagree: %s is in %s but not %s" % (path, SIM_SHELL, SIM_CMAKE))

  built = host_sources(root) | cmake_sim | shell_sim

  for path in sorted(sources - built - set(exemptions)):
    errors.append("uncovered firmware source: %s is in neither build list nor %s"
                  % (path, EXEMPTIONS))

  for path in sorted(exemptions):
    reason = exemptions[path]
    if not isinstance(reason, str) or not reason.strip():
      errors.append("exemption needs a one-line reason: %s" % path)
    if not (root / path).exists():
      errors.append("stale exemption: %s does not exist" % path)
    elif path in built:
      errors.append("redundant exemption: %s is already built" % path)

  return errors


class BuildInventoryTest(unittest.TestCase):
  def copy_fixture(self, root: Path) -> Path:
    """Copy the parsed inputs and empty stand ins for every firmware source."""
    for relative in (HOST_CMAKE, SIM_CMAKE, SIM_SHELL, EXEMPTIONS):
      target = root / relative
      target.parent.mkdir(parents=True, exist_ok=True)
      shutil.copyfile(ROOT / relative, target)
    for path in firmware_sources(ROOT):
      target = root / path
      target.parent.mkdir(parents=True, exist_ok=True)
      target.touch()
    return root

  def test_every_firmware_source_is_built_or_exempt(self):
    self.assertEqual(check_inventory(ROOT), [])

  def test_sim_cmake_and_shell_lists_agree(self):
    self.assertEqual(sim_cmake_sources(ROOT), sim_shell_sources(ROOT))
    self.assertNotEqual(sim_cmake_sources(ROOT), set())

  def test_no_exemption_is_stale(self):
    for path in load_exemptions(ROOT):
      self.assertTrue((ROOT / path).exists(), "stale exemption: %s" % path)

  def test_no_exemption_is_also_built(self):
    built = host_sources(ROOT) | sim_cmake_sources(ROOT) | sim_shell_sources(ROOT)
    self.assertEqual(sorted(set(load_exemptions(ROOT)) & built), [])

  def test_every_exemption_has_a_reason(self):
    for path, reason in load_exemptions(ROOT).items():
      self.assertIsInstance(reason, str)
      self.assertTrue(reason.strip(), "empty reason: %s" % path)

  def test_host_and_sim_lists_are_parsed(self):
    # Guards the parsers themselves. A silent parse failure would make the gate
    # pass by exempting nothing and building nothing.
    self.assertIn("lib/furble/Camera.cpp", host_sources(ROOT))
    self.assertIn("lib/furble/protocol/FujifilmProtocol.cpp", host_sources(ROOT))
    self.assertIn("src/FurbleGPS.cpp", sim_cmake_sources(ROOT))
    self.assertIn("src/FurbleGPS.cpp", sim_shell_sources(ROOT))

  def test_new_unlisted_source_is_rejected(self):
    with tempfile.TemporaryDirectory() as directory:
      root = self.copy_fixture(Path(directory))
      (root / "src/Foo.cpp").touch()
      self.assertIn("uncovered firmware source: src/Foo.cpp is in neither build list nor %s"
                    % EXEMPTIONS, check_inventory(root))

  def drop_canary_from_sim_lists(self, root):
    # The simulator builds the whole vendor set since plan 161, so a canary the
    # host build drops is still covered there. Take it out of both simulator
    # lists too, so these fixtures test the host-side drop they are about.
    cmake = root / SIM_CMAKE
    text = cmake.read_text(encoding="utf-8")
    cmake.write_text(text.replace('    "${FURBLE_ROOT}/%s"\n' % CANARY, ""), encoding="utf-8")
    shell = root / SIM_SHELL
    text = shell.read_text(encoding="utf-8")
    shell.write_text(text.replace('  "$ROOT/%s" \\\n' % CANARY, ""), encoding="utf-8")
    self.assertNotIn(CANARY, sim_cmake_sources(root))
    self.assertNotIn(CANARY, sim_shell_sources(root))

  def test_source_dropped_from_host_cmake_is_rejected(self):
    with tempfile.TemporaryDirectory() as directory:
      root = self.copy_fixture(Path(directory))
      self.drop_canary_from_sim_lists(root)
      cmake = root / HOST_CMAKE
      text = cmake.read_text(encoding="utf-8")
      dropped = "${FURBLE_ROOT}/%s" % CANARY
      self.assertIn(dropped, text)
      cmake.write_text(text.replace(dropped, ""), encoding="utf-8")
      self.assertIn("uncovered firmware source: %s is in neither build "
                    "list nor %s" % (CANARY, EXEMPTIONS), check_inventory(root))

  def test_commented_out_source_is_reported_uncovered(self):
    # A source commented out of the host build is not compiled, so the gate has
    # to see it as uncovered rather than reading the path out of the comment.
    with tempfile.TemporaryDirectory() as directory:
      root = self.copy_fixture(Path(directory))
      self.drop_canary_from_sim_lists(root)
      cmake = root / HOST_CMAKE
      text = cmake.read_text(encoding="utf-8")
      live = "               ${FURBLE_ROOT}/%s\n" % CANARY
      self.assertIn(live, text)
      commented = "               # ${FURBLE_ROOT}/%s\n" % CANARY
      cmake.write_text(text.replace(live, commented), encoding="utf-8")
      self.assertNotIn(CANARY, host_sources(root))
      self.assertIn("uncovered firmware source: %s is in neither build "
                    "list nor %s" % (CANARY, EXEMPTIONS), check_inventory(root))

  def test_comment_mentioning_an_exempted_path_is_not_a_build(self):
    # A comment that names an exempted file must not make it look built, which
    # would fire the redundant-exemption check on a file nothing compiles.
    with tempfile.TemporaryDirectory() as directory:
      root = self.copy_fixture(Path(directory))
      cmake = root / HOST_CMAKE
      cmake.write_text(cmake.read_text(encoding="utf-8")
                       + "\n# See ${FURBLE_ROOT}/src/FurbleSD.cpp for the card mount.\n",
                       encoding="utf-8")
      self.assertNotIn("src/FurbleSD.cpp", host_sources(root))
      self.assertEqual(check_inventory(root), [])

  def test_stale_exemption_is_rejected(self):
    with tempfile.TemporaryDirectory() as directory:
      root = self.copy_fixture(Path(directory))
      (root / "src/main.cpp").unlink()
      self.assertIn("stale exemption: src/main.cpp does not exist", check_inventory(root))

  def test_sim_lists_that_disagree_are_rejected(self):
    with tempfile.TemporaryDirectory() as directory:
      root = self.copy_fixture(Path(directory))
      shell = root / SIM_SHELL
      text = shell.read_text(encoding="utf-8")
      dropped = '"$ROOT/src/FurbleGPS.cpp" \\\n'
      self.assertIn(dropped, text)
      shell.write_text(text.replace(dropped, ""), encoding="utf-8")
      errors = check_inventory(root)
      self.assertIn("sim lists disagree: src/FurbleGPS.cpp is in %s but not %s"
                    % (SIM_CMAKE, SIM_SHELL), errors)

  def test_fixture_tree_matches_the_real_repo(self):
    # Control for the mutation tests. The fixture must reproduce the real tree
    # exactly, so every error a mutation raises belongs to that mutation.
    with tempfile.TemporaryDirectory() as directory:
      root = self.copy_fixture(Path(directory))
      self.assertEqual(check_inventory(root), check_inventory(ROOT))


if __name__ == "__main__":
  unittest.main()
