# Roadmap and planned technology

This page separates intended direction from implemented API. Items here are not
available until they appear under `include/Bazzalt` and are covered by tests.

## Editor and deployment model

The intended editor is Python 3 with PySide6. It will host the same runtime used
by shipped games, render into an embedded/offscreen target, edit project data,
and drive import/compile tasks. A standalone host will provide native windowing
and presentation without changing scene or asset formats.

The runtime may eventually ship as `.dll`, `.so`, or `.dylib` behind a stable
host interface. Today the repository builds a static C++ library and does not
define a stable C ABI.

## Script-module direction

The proposed model is a generated native game module with stable registration
entry points and, when used, embedded Lua bytecode. The runtime would load this
module rather than recompiling the engine for every project. ABI design, reload
safety, Lua sandboxing, bytecode compatibility, and errors must be specified
before this is supported.
