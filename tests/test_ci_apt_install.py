"""Behaviour tests for tools/ci-apt-install.sh.

The script runs against a fake root (CI_APT_ROOT) and fake apt-get, dpkg-query
and sudo on PATH, so every branch is exercised without touching the machine it
runs on. Each test names the CI failure mode it pins: a broken third-party
source must not fail the step, and a broken archive, a failed install or a
package that dpkg does not report installed must.
"""

from __future__ import annotations

from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "tools" / "ci-apt-install.sh"

UBUNTU_SOURCES = """\
Types: deb
URIs: http://azure.archive.ubuntu.com/ubuntu/
Suites: noble noble-updates
Components: main universe
Signed-By: /usr/share/keyrings/ubuntu-archive-keyring.gpg
"""

MICROSOFT_LIST = (
    "deb [arch=amd64,arm64,armhf signed-by=/usr/share/keyrings/microsoft-prod.gpg]"
    " https://packages.microsoft.com/ubuntu/26.04/prod resolute main\n"
)

DEFAULT_APT_GET = """\
printf '%s\\n' "$*" >>"$FAKE_LOG"
exit 0
"""

DEFAULT_DPKG_QUERY = 'printf "install ok installed"\nexit 0\n'


class CiaptInstallTest(unittest.TestCase):
  def setUp(self):
    self.tmp = Path(tempfile.mkdtemp(prefix="ci-apt-install-test."))
    self.addCleanup(shutil.rmtree, self.tmp, True)
    self.bin = self.tmp / "bin"
    self.bin.mkdir()
    self.root = self.tmp / "root"
    self.parts = self.root / "etc" / "apt" / "sources.list.d"
    self.parts.mkdir(parents=True)
    self.log = self.tmp / "apt-get.log"
    self.fake("sudo", 'exec "$@"')
    self.fake("apt-get", DEFAULT_APT_GET)
    self.fake("dpkg-query", DEFAULT_DPKG_QUERY)

  def fake(self, name: str, body: str) -> None:
    path = self.bin / name
    path.write_text("#!/bin/sh\n" + body, encoding="utf-8")
    path.chmod(0o755)

  def add_source(self, name: str, text: str) -> Path:
    path = self.parts / name
    path.write_text(text, encoding="utf-8")
    return path

  def run_install(self, *packages: str, **env_overrides: str):
    environment = dict(os.environ)
    environment["PATH"] = f"{self.bin}{os.pathsep}{environment['PATH']}"
    environment["CI_APT_ROOT"] = str(self.root)
    environment["FAKE_LOG"] = str(self.log)
    environment.update(env_overrides)
    return subprocess.run(
        ["sh", str(SCRIPT), *packages],
        capture_output=True, text=True, env=environment, cwd=ROOT, check=False,
    )

  def apt_calls(self) -> list[str]:
    if not self.log.exists():
      return []
    return [line for line in self.log.read_text(encoding="utf-8").splitlines()
            if line]

  def test_broken_third_party_source_does_not_fail_the_step(self):
    self.add_source("ubuntu.sources", UBUNTU_SOURCES)
    third_party = self.add_source("microsoft-prod.list", MICROSOFT_LIST)

    result = self.run_install("clang", "libsdl2-dev")

    self.assertEqual(result.returncode, 0, result.stderr)
    rewritten = third_party.read_text(encoding="utf-8")
    self.assertFalse(
        [line for line in rewritten.splitlines()
         if line.strip() and not line.lstrip().startswith("#")],
        "no enabled entry may still point at the third-party repository",
    )
    calls = self.apt_calls()
    self.assertEqual(len(calls), 2, calls)
    self.assertIn("update", calls[0])
    self.assertIn("install", calls[1])
    self.assertIn("--no-install-recommends", calls[1])
    self.assertIn("clang", calls[1])
    self.assertIn("libsdl2-dev", calls[1])
    self.assertIn("packages.microsoft.com", result.stderr)

  def test_archive_only_runner_is_left_alone(self):
    kept = self.add_source("ubuntu.sources", UBUNTU_SOURCES)

    result = self.run_install("clang")

    self.assertEqual(result.returncode, 0, result.stderr)
    self.assertTrue(kept.exists())
    self.assertEqual(kept.read_text(encoding="utf-8"), UBUNTU_SOURCES)
    self.assertIn("disabled 0 unrelated source(s)", result.stderr)

  def test_legacy_sources_list_keeps_the_archive_and_drops_the_ppa(self):
    sources_list = self.root / "etc" / "apt" / "sources.list"
    sources_list.write_text(
        "# See http://help.ubuntu.com/community/UpgradeNotes\n"
        "deb http://archive.ubuntu.com/ubuntu noble main universe\n"
        "deb https://ppa.launchpadcontent.net/git-core/ppa/ubuntu noble main\n",
        encoding="utf-8",
    )

    result = self.run_install("clang")

    self.assertEqual(result.returncode, 0, result.stderr)
    rewritten = sources_list.read_text(encoding="utf-8")
    self.assertIn("deb http://archive.ubuntu.com/ubuntu noble main universe",
                  rewritten)
    self.assertIn(
        "#deb https://ppa.launchpadcontent.net/git-core/ppa/ubuntu noble main",
        rewritten,
    )
    self.assertIn("# See http://help.ubuntu.com/community/UpgradeNotes", rewritten)

  def test_mirror_file_indirection_is_read(self):
    # This is the exact shape on the Ubuntu 26.04 runner image, and the reason
    # the first two versions of this helper failed their own archive check
    # there: ubuntu.sources names `mirror+file:/etc/apt/apt-mirrors.txt`, and
    # each line of that file is a preference followed by the URI.
    (self.root / "etc" / "apt" / "apt-mirrors.txt").write_text(
        "# mirrors to try\n"
        "priority:1 http://azure.archive.ubuntu.com/ubuntu/\n"
        "priority:2 http://archive.ubuntu.com/ubuntu/\n",
        encoding="utf-8",
    )
    kept = self.add_source(
        "ubuntu.sources",
        "Types: deb\n"
        "URIs: mirror+file:/etc/apt/apt-mirrors.txt\n"
        "Suites: resolute resolute-updates\n"
        "Components: main universe\n",
    )

    result = self.run_install("clang", "libsdl2-dev")

    self.assertEqual(result.returncode, 0, result.stderr)
    self.assertTrue(kept.exists())
    self.assertIn("mirror list", result.stderr)
    self.assertIn("azure.archive.ubuntu.com", result.stderr)
    self.assertNotIn("mirror+file", result.stderr,
                     "the scheme must never be read as a host")
    self.assertNotIn("priority", result.stderr,
                     "the preference must never be read as a host")
    self.assertIn("disabled 0 unrelated source(s)", result.stderr)

  def test_unreadable_mirror_indirection_is_left_enabled(self):
    kept = self.add_source(
        "ubuntu.sources",
        "Types: deb\nURIs: mirror+file:/etc/apt/apt-mirrors-absent.txt\n"
        "Suites: resolute\nComponents: main\n",
    )

    result = self.run_install("clang")

    self.assertEqual(result.returncode, 0, result.stderr)
    self.assertTrue(kept.exists(),
                    "a source the helper cannot read must not be disabled")
    self.assertIn("does not exist", result.stderr)
    self.assertIn("cannot resolve the mirror list", result.stderr)

  def test_remote_mirror_list_is_left_enabled(self):
    kept = self.add_source(
        "mirrors.sources",
        "Types: deb\nURIs: mirror+https://example.test/mirrors.txt\n"
        "Suites: resolute\nComponents: main\n",
    )

    result = self.run_install("clang")

    self.assertEqual(result.returncode, 0, result.stderr)
    self.assertTrue(kept.exists())
    self.assertIn("cannot resolve the mirror list", result.stderr)

  def test_mirror_list_without_a_readable_uri_is_left_enabled(self):
    (self.root / "etc" / "apt" / "apt-mirrors.txt").write_text(
        "# every line unusable\npriority:1 not-a-uri\n", encoding="utf-8")
    kept = self.add_source(
        "ubuntu.sources",
        "Types: deb\nURIs: mirror+file:/etc/apt/apt-mirrors.txt\n"
        "Suites: resolute\nComponents: main\n",
    )

    result = self.run_install("clang")

    self.assertEqual(result.returncode, 0, result.stderr)
    self.assertTrue(kept.exists())
    self.assertIn("names no mirror this script can read", result.stderr)

  def test_runner_with_no_archive_source_fails_before_any_update(self):
    self.add_source("microsoft-prod.list", MICROSOFT_LIST)

    result = self.run_install("clang")

    self.assertNotEqual(result.returncode, 0)
    self.assertIn("no Ubuntu archive source is enabled", result.stderr)
    self.assertEqual(self.apt_calls(), [],
                     "a package index with no archive must not be updated")

  def test_broken_archive_fails_the_step(self):
    self.add_source("ubuntu.sources", UBUNTU_SOURCES)
    self.fake("apt-get", 'exit 100\n')

    result = self.run_install("clang")

    self.assertNotEqual(result.returncode, 0)
    self.assertIn("apt-get update failed", result.stderr)

  def test_missing_package_fails_the_step(self):
    self.add_source("ubuntu.sources", UBUNTU_SOURCES)
    self.fake("apt-get", """\
case "$1" in
  update) exit 0 ;;
  *) printf 'E: Unable to locate package clang\\n' >&2; exit 100 ;;
esac
""")

    result = self.run_install("clang")

    self.assertNotEqual(result.returncode, 0)
    self.assertIn("apt-get install failed", result.stderr)

  def test_package_dpkg_does_not_report_installed_fails_the_step(self):
    self.add_source("ubuntu.sources", UBUNTU_SOURCES)
    self.fake("dpkg-query", "exit 1\n")

    result = self.run_install("clang")

    self.assertNotEqual(result.returncode, 0)
    self.assertIn("clang is not installed after apt-get", result.stderr)
    self.assertIn("dpkg status: absent", result.stderr)

  def test_package_reported_half_configured_fails_the_step(self):
    self.add_source("ubuntu.sources", UBUNTU_SOURCES)
    self.fake("dpkg-query", 'printf "install ok half-configured"\n')

    result = self.run_install("clang")

    self.assertNotEqual(result.returncode, 0)
    self.assertIn("dpkg status: install ok half-configured", result.stderr)

  def test_empty_package_list_is_an_error(self):
    self.add_source("ubuntu.sources", UBUNTU_SOURCES)

    result = self.run_install()

    self.assertNotEqual(result.returncode, 0)
    self.assertIn("no packages named", result.stderr)

  def test_versioned_package_argument_is_rejected(self):
    self.add_source("ubuntu.sources", UBUNTU_SOURCES)

    result = self.run_install("clang=18")

    self.assertNotEqual(result.returncode, 0)
    self.assertIn("not a plain package name", result.stderr)

  def test_archive_allowlist_can_be_widened(self):
    self.add_source("ubuntu.sources", UBUNTU_SOURCES)
    internal = self.add_source(
        "internal.list",
        "deb http://mirrors.example.test/ubuntu noble main\n",
    )

    dropped = self.run_install("clang")
    self.assertEqual(dropped.returncode, 0, dropped.stderr)
    self.assertTrue(internal.read_text(encoding="utf-8").startswith("#"),
                    "an unlisted host must be disabled by default")

    internal.write_text("deb http://mirrors.example.test/ubuntu noble main\n",
                        encoding="utf-8")
    widened = self.run_install("clang", CI_APT_ARCHIVE_HOSTS="mirrors.example.test")
    self.assertEqual(widened.returncode, 0, widened.stderr)
    self.assertEqual(
        internal.read_text(encoding="utf-8"),
        "deb http://mirrors.example.test/ubuntu noble main\n",
        "an explicitly allowed host must be kept, not dropped",
    )

  def test_widening_the_allowlist_does_not_bypass_a_broken_archive(self):
    self.add_source("ubuntu.sources", UBUNTU_SOURCES)
    self.fake("apt-get", 'exit 100\n')

    result = self.run_install("clang", CI_APT_ARCHIVE_HOSTS="mirrors.example.test")

    self.assertNotEqual(result.returncode, 0)


class ScriptInvariantsTest(unittest.TestCase):
  """Source-level invariants that keep the failure suppression out."""

  def setUp(self):
    self.text = SCRIPT.read_text(encoding="utf-8")

  def test_no_apt_line_suppresses_its_exit_status(self):
    for line in self.text.splitlines():
      if "apt-get" not in line or line.lstrip().startswith("#"):
        continue
      self.assertNotIn("|| true", line, line)
      self.assertNotIn("--fix-missing", line, line)
      self.assertNotIn("AllowInsecureRepositories", line, line)

  def test_script_is_posix_sh(self):
    self.assertTrue(self.text.startswith("#!/bin/sh\n"))


if __name__ == "__main__":
  unittest.main()
