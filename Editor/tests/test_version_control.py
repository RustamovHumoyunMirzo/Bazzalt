import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from Editor.version_control import GitError, GitRepository, ParseStatus


@unittest.skipUnless(shutil.which("git"), "Git executable required")
class RepositoryTests(unittest.TestCase):
    def setUp(self):
        self.Folder = tempfile.TemporaryDirectory()
        self.Root = Path(self.Folder.name) / "Тест Game"
        self.Root.mkdir()
        self.Repo = GitRepository.Initialize(self.Root)
        self.Git("config", "user.name", "Editor Tests")
        self.Git("config", "user.email", "editor@example.invalid")
        self.Git("config", "core.autocrlf", "false")
        self.Git("config", "commit.gpgSign", "false")
        self.Git("config", "core.hooksPath", str(self.Root / ".git/hooks"))
        excludes = self.Root / ".git/test-excludes"
        excludes.write_text("", encoding="utf-8")
        self.Git("config", "core.excludesFile", str(excludes))

    def tearDown(self):
        self.Folder.cleanup()

    def Git(self, *args):
        result = subprocess.run(["git", *args], cwd=self.Root, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr.decode("utf-8", "replace"))
        return result.stdout

    def Write(self, name, text):
        target = self.Root / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(text, encoding="utf-8", newline="\n")

    def Initial(self):
        self.Write("cube.bscene", "base\n")
        self.Write("cube.bscene.meta", "UUID: 1\n")
        self.Repo.Stage([".gitignore", "cube.bscene"])
        self.Repo.Commit("Initial")
        return self.Repo.Snapshot().Branch

    def test_unborn_unicode_literal_paths_metadata_and_history(self):
        name = "Assets/модель [1].bscene"
        self.Write(name, "initial\n")
        self.Write(name + ".meta", "UUID: 1\n")
        self.Write("Assets/модель 1.bscene", "must stay untracked")
        self.Repo.Stage([name])
        self.assertEqual({change.Path for change in self.Repo.Snapshot().Changes if change.Index == "A"}, {name, name + ".meta"})
        self.Repo.Unstage([name])
        self.assertTrue((self.Root / name).exists())
        self.assertEqual(self.Git("diff", "--cached", "--name-only"), b"")
        self.Repo.Stage([name])
        self.Repo.Commit("Unicode commit ✓\n\nMultiline message")
        snapshot = self.Repo.Snapshot()
        self.assertEqual(snapshot.History[0][3], "Unicode commit ✓")
        self.assertIn("Multiline message", self.Repo.ShowCommit(snapshot.History[0][0])["text"])
        self.Write(name, "changed\n")
        self.assertIn("+changed", self.Repo.Diff(name)["text"])
        self.Repo.Stage([name])
        self.assertIn("+changed", self.Repo.Diff(name, True)["text"])

    def test_rename_stages_old_and_new_metadata_deletions(self):
        self.Initial()
        (self.Root / "cube.bscene").rename(self.Root / "new.bscene")
        (self.Root / "cube.bscene.meta").rename(self.Root / "new.bscene.meta")
        self.Repo.Stage(["cube.bscene", "new.bscene"])
        paths = self.Git("diff", "--cached", "--name-only", "--no-renames").decode().splitlines()
        self.assertEqual(set(paths), {"cube.bscene", "new.bscene", "cube.bscene.meta", "new.bscene.meta"})
        self.assertTrue(any(change.Original for change in self.Repo.Snapshot().Changes))
        self.Repo.Unstage(["new.bscene"])
        self.assertEqual(self.Git("diff", "--cached", "--name-only"), b"")

    def test_branch_dirty_guard_conflict_manual_resolution_and_abort(self):
        main = self.Initial()
        self.Repo.Switch("feature", True)
        self.Write("cube.bscene", "feature\n")
        with self.assertRaises(GitError):
            self.Repo.Switch(main)
        self.Repo.Stage(["cube.bscene"])
        self.Repo.Commit("Feature")
        self.Repo.Switch(main)
        self.Write("cube.bscene", "main\n")
        self.Repo.Stage(["cube.bscene"])
        self.Repo.Commit("Main")
        with self.assertRaises(GitError):
            self.Repo.Merge("feature")
        self.assertTrue(self.Repo.Snapshot().Merge)
        conflict = self.Repo.Conflict("cube.bscene")
        self.assertEqual(conflict[2], b"main\n")
        self.assertEqual(conflict[3], b"feature\n")
        with self.assertRaises(GitError):
            self.Repo.Stage(["cube.bscene"])
        self.Repo.ResolveText("cube.bscene", "combined\n")
        self.Repo.Commit("Resolve")
        self.assertFalse(self.Repo.Snapshot().Merge)
        self.assertEqual((self.Root / "cube.bscene").read_text(), "combined\n")

    def test_delete_modify_side_resolution_and_stash_restore(self):
        main = self.Initial()
        self.Repo.Switch("delete", True)
        self.Git("rm", "cube.bscene")
        self.Repo.Commit("Delete")
        self.Repo.Switch(main)
        self.Write("cube.bscene", "modified\n")
        self.Repo.Stage(["cube.bscene"])
        self.Repo.Commit("Modify")
        with self.assertRaises(GitError):
            self.Repo.Merge("delete")
        self.Repo.ResolveSide("cube.bscene", 3)
        self.assertFalse((self.Root / "cube.bscene").exists())
        self.Repo.AbortMerge()
        self.assertTrue((self.Root / "cube.bscene").exists())
        self.Write("cube.bscene", "stash\n")
        self.Repo.Stash()
        self.assertFalse(self.Repo.Snapshot().Changes)
        self.Repo.PopStash()
        self.assertEqual((self.Root / "cube.bscene").read_text(), "stash\n")
        self.Repo.Restore(["cube.bscene"])
        self.assertEqual((self.Root / "cube.bscene").read_text(), "modified\n")

    def test_paths_and_revisions_cannot_escape_or_inject_options(self):
        self.Initial()
        for path in ("../outside", "/outside", "C:\\outside", "x/../outside", ".git/config", "x\\..\\outside"):
            with self.assertRaises(GitError):
                self.Repo.Stage([path])
        with self.assertRaises(GitError):
            self.Repo.ShowCommit("--output=outside")
        with self.assertRaises(GitError):
            self.Repo.Switch("--orphan")
        with self.assertRaises(GitError):
            self.Repo.Commit(" ")
        self.Write("-file[1].txt", "literal")
        self.Repo.Stage(["-file[1].txt"])
        self.assertIn("-file[1].txt", self.Git("diff", "--cached", "--name-only").decode())

    def test_porcelain_paths_with_spaces_and_rename_source(self):
        fields = b"# branch.head main\0# branch.ab +2 -1\0"
        fields += b"2 R. N... 100644 100644 100644 a b R100 new file\0old file\0"
        fields += b"u UU N... 100644 100644 100644 100644 a b c conflict file\0? untracked file\0"
        headers, changes = ParseStatus(fields)
        self.assertEqual(headers["branch.ab"], "+2 -1")
        self.assertEqual(changes[0].Original, "old file")
        self.assertEqual(changes[1].Path, "conflict file")
        self.assertTrue(changes[1].Conflict)
        self.assertEqual(changes[2].Path, "untracked file")

    def test_linked_worktree_git_file_and_preview_limits(self):
        self.Initial()
        linked = self.Root.parent / "linked"
        self.Git("worktree", "add", "-b", "linked", str(linked))
        repository = GitRepository(linked)
        self.assertTrue((linked / ".git").is_file())
        self.assertEqual(repository.Snapshot().Branch, "linked")
        self.assertFalse(repository.Snapshot().Changes)
        self.Write("large.txt", "x" * 1024)
        self.Repo.Stage(["large.txt"])
        self.Repo.DiffLimit = 64
        preview = self.Repo.Diff("large.txt", True)
        self.assertTrue(preview["truncated"])
        self.assertEqual(len(preview["text"].encode()), 64)
