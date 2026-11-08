# Roadmap and compatibility planning

[TODO.md](../../TODO.md) is the current source of planned feature work. Use issues
and pull requests for detailed proposals. This handbook describes implemented
maintenance contracts.

## Current foundation

The repository has shared native core/provider libraries, editor and Hub,
standalone platform hosts, native/Lua scripting, cooking and release workflows.
Use [architecture](architecture.md) and [the code map](maintenance-map.md) for the
current ownership model. Supported targets/backends are listed in the current
public platform guide, reached from [the documentation index](../public/index.html).

## Proposing an integration

Record the intended user behavior, owning layer, persisted data and compatibility
needs. Identify the public C++/Lua contract, editor workflow, runtime cost and
verification path. For provider changes, explain whether the existing dispatch
contract suffices or needs a coordinated ABI version change.

Prefer incremental changes with working entry points. Preserve existing callers,
assets and tests while moving implementation details. Use the
[module guide](module-development.md) for backend replacement and the
[change recipes](contributor-workflows.md) for new features.
