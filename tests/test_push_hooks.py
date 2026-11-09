"""The pre-push git hook and the Claude Code settings that enable it (plan 314 PR 14).

Each test pushes from a scratch clone to a local bare remote through real git,
with tools/pre_review.py replaced by a stub that records its arguments and
fails for a commit whose message contains "bad". The hook itself is the
tracked .githooks/pre-push, enabled through the env in .claude/settings.json.
"""

import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
HOOK = ROOT / ".githooks" / "pre-push"
SETTINGS = ROOT / ".claude" / "settings.json"
STUB = """import os, subprocess, sys
if "GIT_DIR" in os.environ:
    sys.exit("GIT_DIR leaked into the reviewer")
head = sys.argv[sys.argv.index("--head") + 1]
open("review.log", "a").write(head + "\\n")
message = subprocess.run(["git", "log", "-1", "--format=%s", head],
                         capture_output=True, text=True).stdout
sys.exit(1 if "bad" in message else 0)
"""


class PushHookTest(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.tmp)
        self.remote = self.tmp / "remote.git"
        self.work = self.tmp / "work"
        # A hook exports GIT_DIR to the gate, which runs this suite; keep the
        # scratch repositories from inheriting it.
        env = {k: v for k, v in os.environ.items() if not k.startswith("GIT_")}
        env.update(json.loads(SETTINGS.read_text(encoding="utf-8"))["env"])
        env.update(GIT_AUTHOR_NAME="t", GIT_AUTHOR_EMAIL="t@t", GIT_COMMITTER_NAME="t",
                   GIT_COMMITTER_EMAIL="t@t", GIT_CONFIG_GLOBAL=os.devnull,
                   GIT_CONFIG_NOSYSTEM="1")
        self.env = env
        self.git("init", "-q", "--bare", "-b", "master", str(self.remote), cwd=self.tmp)
        self.git("init", "-q", "-b", "master", str(self.work), cwd=self.tmp)
        (self.work / ".githooks").mkdir()
        shutil.copy2(HOOK, self.work / ".githooks" / "pre-push")
        (self.work / "tools").mkdir()
        (self.work / "tools" / "pre_review.py").write_text(STUB, encoding="utf-8")
        (self.work / ".gitignore").write_text("review.log\n", encoding="utf-8")
        self.git("add", ".")
        self.git("commit", "-q", "-m", "base")
        self.git("remote", "add", "origin", str(self.remote))
        self.git("push", "-q", "--no-verify", "origin", "master")
        self.git("fetch", "-q", "origin")

    def git(self, *args, cwd=None, check=True):
        return subprocess.run(["git", *args], cwd=cwd or self.work, env=self.env,
                              capture_output=True, text=True, timeout=60, check=check)

    def commit(self, message):
        (self.work / "f.txt").write_text(message, encoding="utf-8")
        self.git("add", "f.txt")
        self.git("commit", "-q", "-m", message)
        return self.git("rev-parse", "HEAD").stdout.strip()

    def reviewed(self):
        log = self.work / "review.log"
        return log.read_text(encoding="utf-8").split() if log.exists() else []

    def test_the_settings_env_enables_the_tracked_hook(self):
        self.assertEqual(self.git("config", "core.hooksPath").stdout.strip(), ".githooks")
        self.assertTrue(HOOK.stat().st_mode & 0o111)

    def test_a_clean_push_reviews_the_pushed_commit(self):
        sha = self.commit("good")
        self.assertEqual(self.git("push", "-q", "origin", "master", check=False).returncode, 0)
        self.assertEqual(self.reviewed(), [sha])

    def test_a_failing_review_blocks_the_push(self):
        self.commit("bad")
        result = self.git("push", "-q", "origin", "master", check=False)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("pre-review failed", result.stderr)

    def test_the_reviewed_ref_is_the_one_pushed_not_head(self):
        sha = self.commit("bad")
        self.git("switch", "-q", "-c", "other", "HEAD~1")
        self.assertNotEqual(self.git("push", "-q", "origin", "master", check=False).returncode, 0)
        self.assertEqual(self.reviewed(), [sha])

    def test_no_verify_skips_only_its_own_push(self):
        self.commit("bad")
        result = self.git("push", "-q", "--no-verify", "origin", "master", check=False)
        self.assertEqual((result.returncode, self.reviewed()), (0, []))
        self.git("push", "-q", "origin", "master:refs/heads/copy", check=False)
        self.assertEqual(len(self.reviewed()), 1)

    def test_a_push_from_a_linked_worktree_hides_git_dir_from_the_gate(self):
        # Git exports GIT_DIR to a hook run from a linked worktree. The gate's
        # own tests build scratch repositories, so a leaked GIT_DIR once
        # committed into, and reconfigured, the real repository.
        tree = self.tmp / "linked"
        self.git("worktree", "add", "-q", "-b", "side", str(tree))
        (tree / "g.txt").write_text("x", encoding="utf-8")
        self.git("add", "g.txt", cwd=tree)
        self.git("commit", "-q", "-m", "good", cwd=tree)
        result = self.git("push", "-q", "origin", "side", cwd=tree, check=False)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_a_branch_deletion_skips_the_review(self):
        self.git("push", "-q", "--no-verify", "origin", "master:refs/heads/old")
        self.assertEqual(self.git("push", "-q", "origin", ":old", check=False).returncode, 0)
        self.assertEqual(self.reviewed(), [])


if __name__ == "__main__":
    unittest.main()
