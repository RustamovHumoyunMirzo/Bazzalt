"""Project Git operations. Call from a worker, never from a paint/tick callback."""
from dataclasses import dataclass
import os
from pathlib import Path, PurePosixPath, PureWindowsPath
import re
import signal
import shutil
import subprocess
import tempfile


class GitError(RuntimeError):
    def __init__(self, key, detail=""):
        super().__init__(detail)
        self.Key = key


@dataclass(frozen=True)
class Change:
    Path: str
    Index: str
    Worktree: str
    Original: str = ""
    Conflict: bool = False


@dataclass(frozen=True)
class Snapshot:
    Root: str
    Branch: str
    Upstream: str
    Ahead: int
    Behind: int
    Changes: tuple
    Branches: tuple
    History: tuple
    Merge: bool
    Rebase: bool


def ParseStatus(data):
    """Porcelain v2 -z keeps spaces, Unicode and rename source paths lossless."""
    records = iter(data.decode("utf-8", "surrogateescape").split("\0"))
    changes = []
    headers = {}
    for record in records:
        if not record:
            continue
        kind = record[0]
        if kind == "#":
            key, _, value = record[2:].partition(" ")
            headers[key] = value
        elif kind == "?":
            changes.append(Change(record[2:], "?", "?"))
        elif kind in ("1", "2", "u"):
            fields = record.split(" ", {"1": 8, "2": 9, "u": 10}[kind])
            original = next(records) if kind == "2" else ""
            changes.append(Change(fields[-1], fields[1][0], fields[1][1], original, kind == "u"))
    return headers, tuple(changes)


class GitRepository:
    Limit = 16 * 1024 * 1024
    DiffLimit = 2 * 1024 * 1024

    def __init__(self, project):
        self.Root = Path(project).resolve()
        self.Git = shutil.which("git")
        if not self.Git:
            raise GitError("git_missing")
        result = self._Run("rev-parse", "--show-toplevel", accepted=(0, 128))
        if result.returncode:
            if "not a git repository" not in result.stderr:
                raise GitError("failed", result.stderr.strip())
            raise GitError("no_repository")
        self.Root = Path(os.fsdecode(result.stdout).strip()).resolve()

    def _Run(self, *arguments, accepted=(0,), input=None, preview=False):
        environment = {key: value for key, value in os.environ.items() if not key.startswith("GIT_")}
        environment.update(GIT_TERMINAL_PROMPT="0", GCM_INTERACTIVE="Never", GIT_LITERAL_PATHSPECS="1",
                           GIT_OPTIONAL_LOCKS="0", LC_ALL="C", GIT_SSH_COMMAND="ssh -oBatchMode=yes")
        command = [self.Git, "--no-pager", "-c", "color.ui=false", "-c", "core.quotepath=false",
                   "-c", "i18n.logOutputEncoding=UTF-8", *arguments]
        # File-backed pipes bound Python memory even for an enormous diff. All
        # commands have a deadline and previews expose an explicit truncation bit.
        try:
            with tempfile.TemporaryFile() as output, tempfile.TemporaryFile() as errors:
                process = subprocess.Popen(command, cwd=self.Root, env=environment,
                                           stdin=subprocess.PIPE if input is not None else subprocess.DEVNULL,
                                           stdout=output, stderr=errors, start_new_session=os.name != "nt",
                                           creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
                try:
                    process.communicate(input, timeout=60)
                except subprocess.TimeoutExpired:
                    try:
                        if os.name == "nt":
                            subprocess.run([str(Path(os.environ["SystemRoot"]) / "System32/taskkill.exe"),
                                            "/PID", str(process.pid), "/T", "/F"], capture_output=True, timeout=10,
                                           creationflags=subprocess.CREATE_NO_WINDOW)
                        else:
                            try:
                                os.killpg(process.pid, signal.SIGKILL)
                            except ProcessLookupError:
                                pass
                    finally:
                        process.kill()
                        process.wait()
                    raise
                result = subprocess.CompletedProcess(command, process.returncode)
                limit = self.DiffLimit if preview else self.Limit
                output.seek(0)
                data = output.read(limit + 1)
                errors.seek(0)
                detail = errors.read(65536).decode("utf-8", "replace")
        except subprocess.TimeoutExpired as error:
            raise GitError("timeout") from error
        except OSError as error:
            raise GitError("failed", str(error)) from error
        if result.returncode not in accepted:
            detail = detail or data[:65536].decode("utf-8", "replace")
            detail = re.sub(r"(https?://)[^/\s@]+@", r"\1***@", detail)
            raise GitError("failed", detail.strip())
        if len(data) > limit and not preview:
            raise GitError("output_limit")
        result.stdout = data[:limit]
        result.stderr = detail
        result.Truncated = len(data) > limit
        return result

    @classmethod
    def Initialize(cls, project):
        project = Path(project).resolve()
        try:
            cls(project)
        except GitError as error:
            if error.Key != "no_repository":
                raise
        else:
            raise GitError("already_repository")
        instance = cls.__new__(cls)
        instance.Root = project
        instance.Git = shutil.which("git")
        instance._Run("init")
        ignore = project / ".gitignore"
        existing = ignore.read_bytes() if ignore.exists() else b""
        patterns = ["/.bazzalt/Cache/", "/.bazzalt/Editor/", "/bin/"]
        missing = [value.encode("ascii") for value in patterns if value.encode("ascii") not in existing.splitlines()]
        if missing:
            with ignore.open("ab") as stream:
                stream.write((b"\n" if existing and not existing.endswith(b"\n") else b"") + b"\n".join(missing) + b"\n")
        return cls(project)

    def _Path(self, value):
        if not isinstance(value, str) or not value or "\0" in value:
            raise GitError("invalid_path")
        posix, windows = PurePosixPath(value), PureWindowsPath(value)
        if posix.is_absolute() or windows.drive or windows.root or ".." in posix.parts or ".." in windows.parts or ".git" in posix.parts:
            raise GitError("invalid_path")
        return value

    def _Head(self):
        return self._Run("rev-parse", "--verify", "HEAD", accepted=(0, 128)).returncode == 0

    def _StatePath(self, name):
        path = os.fsdecode(self._Run("rev-parse", "--git-path", name).stdout).strip()
        return (self.Root / path).exists()

    def Status(self):
        return ParseStatus(self._Run("status", "--porcelain=v2", "-z", "--branch", "--ahead-behind", "--untracked-files=all").stdout)

    def Snapshot(self):
        headers, changes = self.Status()
        branches = self._Run("for-each-ref", "--format=%(refname:short)", "refs/heads").stdout.decode("utf-8", "replace").splitlines()
        history = []
        if self._Head():
            fields = self._Run("log", "--no-color", "-n", "100", "-z", "--format=%H%x00%an%x00%aI%x00%s").stdout.decode("utf-8", "replace").rstrip("\0").split("\0")
            history = [tuple(fields[index:index + 4]) for index in range(0, len(fields), 4)]
        distance = headers.get("branch.ab", "+0 -0").split()
        return Snapshot(str(self.Root), headers.get("branch.head", ""), headers.get("branch.upstream", ""),
                        int(distance[0][1:]), int(distance[1][1:]), changes, tuple(branches), tuple(history),
                        self._StatePath("MERGE_HEAD"), self._StatePath("rebase-merge") or self._StatePath("rebase-apply"))

    def _Pairs(self, paths):
        selected = set(self._Path(path) for path in paths)
        if not selected or len(selected) > 1000:
            raise GitError("selection_limit")
        _, changes = self.Status()
        for change in changes:
            if change.Path in selected and change.Original:
                selected.add(change.Original)
        candidates = set(selected)
        for path in selected:
            candidates.add(path[:-5] if path.endswith(".meta") else path + ".meta")
        tracked = set(os.fsdecode(self._Run("ls-files", "-z", "--", *sorted(candidates)).stdout).split("\0"))
        tracked.update(change.Path for change in changes)
        tracked.update(change.Original for change in changes if change.Original)
        return sorted(path for path in candidates if path in selected or path in tracked or os.path.lexists(self.Root / path))

    def Stage(self, paths):
        paired = self._Pairs(paths)
        conflicts = {change.Path for change in self.Status()[1] if change.Conflict}
        for path in conflicts.intersection(paired):
            text = self._WorkingText(path)
            self._CheckMarkers(text)
        self._Run("add", "-A", "--", *paired)

    def Unstage(self, paths):
        paired = self._Pairs(paths)
        if any(change.Conflict and change.Path in paired for change in self.Status()[1]):
            raise GitError("unresolved")
        if self._Head():
            self._Run("reset", "-q", "HEAD", "--", *paired)
        else:
            self._Run("rm", "--cached", "-f", "--ignore-unmatch", "--", *paired)

    def Diff(self, path, staged=False):
        path = self._Path(path)
        if not staged and any(change.Path == path and change.Index == "?" for change in self.Status()[1]):
            try:
                text = self._WorkingText(path)
            except GitError as error:
                if error.Key == "binary_conflict":
                    raise GitError("binary_preview") from error
                raise
            return {"text": text, "truncated": False}
        result = self._Run("diff", "--no-color", "--no-ext-diff", "--no-textconv", *( ("--cached",) if staged else ()), "--", path, preview=True)
        return {"text": result.stdout.decode("utf-8", "replace"), "truncated": result.Truncated}

    def ShowCommit(self, identity):
        if not re.fullmatch(r"[0-9a-f]{40}|[0-9a-f]{64}", identity):
            raise GitError("invalid_revision")
        result = self._Run("show", "--no-color", "--format=fuller", "--no-ext-diff", "--no-textconv", identity, "--", preview=True)
        return {"text": result.stdout.decode("utf-8", "replace"), "truncated": result.Truncated}

    def Commit(self, message):
        if not message.strip() or "\0" in message or len(message.encode("utf-8")) > 65536:
            raise GitError("message_required")
        if any(change.Conflict for change in self.Status()[1]):
            raise GitError("unresolved")
        if self._StatePath("rebase-merge") or self._StatePath("rebase-apply"):
            raise GitError("operation_in_progress")
        # -F - preserves Unicode and multiline messages without shell quoting.
        return self._Run("-c", "i18n.commitEncoding=UTF-8", "commit", "-F", "-", input=message.encode("utf-8")).stdout.decode("utf-8", "replace")

    def _Branch(self, name):
        if not name or name.startswith("-") or "\0" in name:
            raise GitError("invalid_branch")
        return self._Run("check-ref-format", "--branch", name).stdout.decode("utf-8", "strict").strip()

    def VerifyProject(self, branch, project):
        branch = self._Branch(branch)
        path = Path(project).resolve().relative_to(self.Root).as_posix()
        if self._Run("cat-file", "-e", branch + ":" + path, accepted=(0, 128)).returncode:
            raise GitError("project_missing")

    def _Clean(self):
        if self.Status()[1]:
            raise GitError("clean_required")
        if self._StatePath("MERGE_HEAD") or self._StatePath("rebase-merge") or self._StatePath("rebase-apply"):
            raise GitError("operation_in_progress")

    def Switch(self, name, create=False):
        name = self._Branch(name)
        self._Clean()
        return self._Run("switch", "-c", name) if create else self._Run("switch", "--no-guess", name)

    def Merge(self, name):
        name = self._Branch(name)
        self._Clean()
        return self._Run("merge", "--no-edit", name)

    def Fetch(self):
        return self._Run("fetch", "--all", "--prune")

    def Pull(self):
        self._Clean()
        return self._Run("pull", "--ff-only")

    def Push(self):
        return self._Run("push")

    def Restore(self, paths):
        paired = self._Pairs(paths)
        tracked = set(os.fsdecode(self._Run("ls-files", "-z", "--", *paired).stdout).split("\0"))
        if not set(paths).issubset(tracked) or any(change.Conflict for change in self.Status()[1]):
            raise GitError("tracked_required")
        self._Run("restore", "--worktree", "--", *[path for path in paired if path in tracked])

    def AbortMerge(self):
        if not self._StatePath("MERGE_HEAD"):
            raise GitError("no_merge")
        return self._Run("merge", "--abort")

    def Stash(self):
        if any(change.Conflict for change in self.Status()[1]):
            raise GitError("unresolved")
        return self._Run("stash", "push", "--include-untracked", "-m", "Bazzalt editor")

    def PopStash(self):
        self._Clean()
        return self._Run("stash", "pop")

    def Conflict(self, path):
        path = self._Path(path)
        if not any(change.Path == path and change.Conflict for change in self.Status()[1]):
            raise GitError("not_conflicted")
        stages = {}
        for line in self._Run("ls-files", "--unmerged", "-z", "--", path).stdout.split(b"\0"):
            if line:
                metadata = line.split(b"\t", 1)[0].split()
                stages[int(metadata[2])] = metadata[1].decode("ascii")
        result = {}
        for stage in (1, 2, 3):
            result[stage] = None
            if stage in stages:
                content = self._Run("cat-file", "blob", stages[stage], preview=True)
                if content.Truncated or b"\0" in content.stdout:
                    raise GitError("binary_conflict")
                try:
                    content.stdout.decode("utf-8", "strict")
                except UnicodeError as error:
                    raise GitError("binary_conflict") from error
                result[stage] = content.stdout
        result["working"] = self._WorkingText(path)
        return result

    def _WorkingText(self, path):
        file = self.Root / self._Path(path)
        if file.is_symlink() or not file.resolve().is_relative_to(self.Root):
            raise GitError("binary_conflict")
        if not file.exists():
            return ""
        if file.stat().st_size > self.DiffLimit:
            raise GitError("binary_conflict")
        try:
            text = file.read_bytes().decode("utf-8")
        except UnicodeError as error:
            raise GitError("binary_conflict") from error
        if "\0" in text:
            raise GitError("binary_conflict")
        return text

    @staticmethod
    def _CheckMarkers(text):
        if re.search(r"(?m)^(<<<<<<< |=======\r?$|>>>>>>> )", text):
            raise GitError("markers_remain")

    def ResolveText(self, path, text):
        self.Conflict(path)
        self._CheckMarkers(text)
        if "\0" in text or len(text.encode("utf-8")) > self.DiffLimit:
            raise GitError("binary_conflict")
        target = self.Root / self._Path(path)
        descriptor, temporary = tempfile.mkstemp(prefix=".bazzalt-merge-", dir=target.parent)
        try:
            with os.fdopen(descriptor, "wb") as stream:
                stream.write(text.encode("utf-8"))
            if target.exists():
                shutil.copymode(target, temporary)
            os.replace(temporary, target)
        finally:
            if os.path.exists(temporary):
                os.unlink(temporary)
        self.Stage([path])

    def ResolveSide(self, path, stage):
        path = self._Path(path)
        if stage not in (2, 3) or not any(change.Path == path and change.Conflict for change in self.Status()[1]):
            raise GitError("not_conflicted")
        entries = self._Run("ls-files", "--unmerged", "-z", "--", path).stdout.split(b"\0")
        exists = any(entry and entry.split(b"\t", 1)[0].split()[2] == str(stage).encode() for entry in entries)
        if exists:
            self._Run("checkout", "--ours" if stage == 2 else "--theirs", "--", path)
            self._Run("add", "--", path)
        else:
            self._Run("rm", "-f", "--", path)
