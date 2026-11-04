# CPU jobs and profiling

Jobs.cpp owns one lazy, process-wide pool in the core DLL. Exported non-template functions ensure gameplay modules use the same queue. Workers are capped at eight to avoid blindly competing with Jolt/Filament/audio. Jolt remains independent. Jobs must operate on detached data; neither ECS nor Lua becomes thread-safe. Packaged tasks retain exceptions in shared-future handles. Worker Wait helps queued tasks; cyclic dependencies remain a programmer error. ParallelFor bounds child tasks to the worker count.

Native jobs capture the current module lease and restore module context around callback execution, preserving nested job lifetime. NativeScriptRuntime::Stop uses the private per-module JobAccess barrier before script instance destruction and after OnDestroy, leaving unrelated streaming IO running. Producers must be stopped before general WaitAll barriers. Do not block a job on the main thread. No arbitrary asynchronous scripts, cancellation, or parallel mutation of engine state is introduced.

Profiler.cpp owns bounded process-wide CPU capture storage. Generation checks exclude scopes crossing capture sessions. Disabled scopes have no copied names or sample writes. Completed samples serialize under one mutex; opt-in profiling itself has overhead. Export is snapshot-based trace-event JSON, locale-independent and escaped. GetSamples copies potentially large data: never poll it on editor ticks. CPU Render.Present includes driver waits but cannot describe GPU stage duration.

Window > CPU Profiler is modeless and updates its table only on Stop. Lua reads copies on its VM thread. Private pybind functions are editor tooling, not public engine-loop controls. Closing the dialog stops a capture. New engine hotspots should receive stable named scopes, with per-system scopes retained for comparing user systems. Do not automatically parallelize systems without defining their data dependencies.

Validation: JobsProfilerTests covers nested fork/join, exceptions, exact parallel coverage, range overflow, host barriers, bounded samples, capture generation and trace escaping. Editor tests cover capture UI aggregation and unavailable modules. All changes are dependency-free beyond standard C++ threads; no dependency build is needed.


## Visual capture diagnostics

The editor bridge includes start_us in copied CPU samples. Frame selection filters all threads by interval overlap. Timeline nesting is calculated once; tiny bars coalesce only for painting. Ctrl-wheel zoom is bounded. Samples are copied only on Stop.

Runtime.Statistics is sampled at 4 Hz while recording, retaining 240 observations. RenderBackend reads eight asynchronous FrameInfo records and selects the newest completed GPU frame; invalid/pending values remain -1. No synchronization or public renderer handles are introduced. GPU observations are not correlated with CPU frame selection across swapchains. Counters describe resources, not draw execution.

Process residency uses GetProcessMemoryInfo on Windows and /proc/self/statm on Linux; unsupported platforms return unavailable. This includes editor memory. Geometry bytes come from the existing tracked cache, without allocator interception or VRAM estimates. CPU export stays CPU-only. Palette colors follow the shared theme. WindowMinimizeButtonHint is disabled. Closing stops capture and polling. Tests cover cross-thread selection, unavailable GPU timings, bounded history, construction/painting and cleanup.
