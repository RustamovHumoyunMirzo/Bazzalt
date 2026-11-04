# macOS runner allocation

The 2026-11-04 run 41861250371 was cancelled before any steps executed.
Both jobs had runner_id 0, no runner name and no steps. GitHub annotations
reported that a hosted runner did not acquire the job, plus ARM capacity warnings.
This was infrastructure allocation, not an engine/compiler failure.

CI and release packaging now use the supported macos-26 (ARM64) and
macos-26-intel (x86_64) pools. The macOS deployment target remains 12.0;
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
