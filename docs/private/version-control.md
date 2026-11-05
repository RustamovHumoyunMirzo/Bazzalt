# Editor Git integration

Editor/version_control.py owns the Git subprocess adapter and porcelain-v2 model.
Editor/gui/version_control.py owns the Window dialog and worktree reload session.
The module uses the installed Git executable. The editor invokes Git as an external process.

GitRepository methods are blocking worker operations. The dialog serializes them
through a single ThreadPoolExecutor and consumes results from a Qt timer. Worker
closures carry paths and plain Python data. All widget access and native editor
bridge calls occur on the Qt thread. Passive status/diff reads keep text editing
enabled. Queued preview selection is coalesced to the latest selected file.
The visible dialog polls every five seconds. Hidden dialogs retain their state.

Commands use argument lists, literal pathspecs, explicit path separators and
file-backed output capture. Reads have a 16 MiB cap and previews a 2 MiB cap.
Each command has a 60-second deadline. Timed-out Git process groups are terminated
on POSIX and descendant processes are terminated with taskkill on Windows.
Network helpers use existing credentials and SSH batch mode. Diagnostic URL
userinfo is redacted. Inherited GIT_* routing variables are removed so a launcher
environment cannot redirect the index or worktree. Git's index locking handles
external clients. Never remove index.lock automatically after a failure.

The repository root comes from Git, including linked worktrees. The dialog shows
that root because commits, stashes and branches affect the entire repository.
Status parses NUL-separated records, including rename originals. Stage/unstage
expand selected asset paths to paired .meta paths, including deleted/renamed paths
that are absent from the current index. Conflicts reject marker-bearing text.
Manual resolution writes a sibling temporary and replaces the destination before
staging. Index stages 2/3 support binary and delete/modify choices. Default diffs
disable external diff programs and text-conversion helpers. Git executes configured hooks,
signing and asset clean/smudge filters during explicit mutation commands.

WorktreeUpdate refuses Play and dirty scenes, flushes document editors, stops
editor ticks, suspends native scene-file locks and clears old document buffers.
The bridge's SetSceneFileLocksSuspended is editor-private. AcquireSceneLock also
checks the suspension flag so incidental refresh cannot reacquire a file during
a Git write. A successful update reloads the project through RuntimeService's
existing project lifecycle signals, restores surviving additive scenes and their
active path, and resumes ticking. Undo/selection state is rebuilt by that lifecycle.

A merge or stash conflict retains the suspended session until all index conflicts
are resolved, or the merge is aborted. The runtime must never parse a conflicted
scene just to display Git status. Git writes can succeed even when their process
returns failure, so mutation results always include a fresh status snapshot.
Reload errors retain the paused editor and expose an explicit retry action.
Main-window close is blocked while a Git task is active. Editor shutdown stops
the dialog timers and executor before releasing the native host.

RepositoryTests uses disposable repositories for metadata renames, Unicode and
literal paths, unborn branches, commit messages, stashes and real merge conflicts.
VersionControlUITests exercises the Window action, staging/commit buttons and
actual native scene locks across branch switching, conflict suspension and reload.
Run these with the built editor bridge and the offscreen Qt platform.
