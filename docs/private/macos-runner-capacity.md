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
