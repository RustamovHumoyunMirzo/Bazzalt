# macOS runner allocation

The 2026-11-04 run 41861250371 was cancelled before any steps executed.
Both jobs had runner_id 0, no runner name and no steps. GitHub annotations
reported that a hosted runner did not acquire the job, plus ARM capacity warnings.
This was infrastructure allocation, not an engine/compiler failure.

The first mitigation moved CI and packaging to macos-26 and macos-26-intel.
The ARM pool also failed allocation; Intel ran but hung in a Cocoa menu check.
CI and release packaging now use the supported macos-15 (ARM64) and
macos-15-intel (x86_64) pools, with the same architecture coverage. The macOS deployment target remains 12.0;
architecture coverage, native tests and packaging checks are unchanged.
See https://github.com/actions/runner-images#available-images for current labels.
A pool change cannot guarantee capacity. If another job has no steps and the
same annotation, rerun the failed jobs after GitHub recovers; inspect annotations
before treating a cancellation as a source failure. Do not use continue-on-error,
remove architecture coverage or increase build timeouts to mask allocation failures.

The ARM macOS 26 pool also rejected a job before acquiring a runner. A pool
change is a mitigation, not a guarantee. retry-macos-allocation.yml adds bounded
recovery on completion: only the latest master push/manual run, at most three
attempts, and only when every unsuccessful job has no steps, runner_id 0 and
GitHub's specific hosted-runner acquisition annotation. Real failures, manual
cancellations and superseded commits are not retried. Successful jobs are retained.
The privileged recovery job checks out only trusted master code, never a PR ref or
an uploaded artifact, and has only contents:read and actions:write permissions.

The Intel timeout was not an allocation failure: it completed compilation, native
and editor checks before hanging in test_native_menu_without_duplicate_client_strip.
Client-only menu corner widgets are no longer installed into the Cocoa native menu.
Cocoa/Metal cases now run in separate processes, each limited to four minutes,
with live logs and a native sample on a hang. Python traces and explicit menu-stage
breadcrumbs identify which call stalls. Timeout remains a test failure; an unbounded
LLDB rerun no longer consumes the entire job. A hosted device that cannot create
Metal argument buffers still runs Cocoa menu and package verification; actual GPU
presentation is reported as unvalidated, not silently counted as tested.

Allocation-only retry attempt two uses the alternate macos-26 / macos-26-intel
pool; attempt three returns to macOS 15. CI and release packaging use the same
architecture-preserving selection. Neither pool guarantees capacity. A source
failure or a test timeout is still ineligible for automatic retry.
