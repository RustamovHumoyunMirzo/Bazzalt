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

## Planned subsystems

| Area | Intended technology | Current status |
|---|---|---|
| Editor UI | Python 3, PySide6 | Initial editor files only |
| Window/input | GLFW | Not implemented |
| Physics | Jolt Physics | Not implemented |
| Audio | miniaudio | Not implemented |
| Native scripts | Dynamically loaded game module | Not implemented |
| Lua scripts | LuaJIT/precompiled bytecode | Not implemented |
| Editor viewport | Filament offscreen target | Foundation only |
| Game presentation | Native swap chain/window host | Not implemented |
| Material instances | Renderer-neutral authored assets | Planned |
| Dependency graph | General tracking/hot reload | glTF sidecars only |

## Script-module direction

The proposed model is a generated native game module with stable registration
entry points and, when used, embedded Lua bytecode. The runtime would load this
module rather than recompiling the engine for every project. ABI design, reload
safety, Lua sandboxing, bytecode compatibility, and errors must be specified
before this is supported.

## Constraints for future work

- Preserve UUID identity and forward-compatible YAML.
- Keep third-party implementation types out of public headers.
- Keep saving, importing, and renderer lifetime host-owned.
- Add public APIs only for stable game-facing capabilities.
- Version persistent formats and importer output independently.
- Provide migrations before serialized layouts change.
