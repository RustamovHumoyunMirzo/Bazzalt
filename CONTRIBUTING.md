# Contributing to BAZZALT

Report bugs with the engine version, operating system, reproduction steps and logs.
For rendering bugs, include the GPU/backend and a small scene that reproduces the
problem. Do not attach private projects, credentials or encryption keys.

## Changes and pull requests

Keep pull requests focused. Explain the behavior before and after the change and
the checks you ran. Discuss large API/file-format changes in an issue first.
Preserve users' uncommitted work and backwards compatibility for UUIDs, YAML
assets, project settings and serialized components. Version descriptors and add
migrations when changing persistent fields; do not rewrite authored data during
layout or rendering.

Public APIs live in `include/Bazzalt`, use PascalCase, and keep third-party and
engine-loop details private. Every public gameplay API change must update its
Lua binding, C++/Lua reference pages, examples and API coverage checks together.
See [Lua integration](docs/private/lua-scripting.md) and the relevant architecture
guide in `docs/private`. Editor-only services must remain safe no-ops in games.
Extension changes must also update the separate Extensions authoring reference,
public C++/Lua SDK usage, package version/ABI checks and editor/game deployment
tests. See [extension architecture](docs/private/extensions.md).

Every user-visible API or editor change must update the current release's
`docs/public/<version>/whats-new/en.html` in the same change. Include additions,
changed behavior, fixes and migration requirements, and link to the relevant
manual or C++/Lua reference. Update existing entries when refining the same
feature; do not rewrite older release notes. Refresh the generated docs afterward.

Use shared editor themes, resources, localized strings and widgets. Content tools
must run without Qt; final games must not acquire an editor/Python dependency.
Keep large dependency builds in the existing CI workflows. Build locally against
an already supplied SDK rather than rebuilding Filament or Qt for routine work.

## Verification

- Run relevant native tests with `ctest --test-dir build -C Release --output-on-failure`.
- Run editor tests with `python -m unittest discover -s Editor/tests -t .`.
- Run compiler tests with `python -m unittest discover -s Compiler/tests`.
- Refresh API references using `python scripts/sync_api_reference.py`, then
  documentation using `python scripts/build_public_docs.py`. Both support `--check`.
- Run `python -m unittest discover -s tests -p 'test_public_docs*.py'` after docs changes.

Add tests that reproduce failures or verify meaningful behavior; a parser-only
test does not establish that a rendering feature works. GPU changes should include
an actual render/readback check where possible. Explain any unavailable checks.

## Dependencies and licensing

Update [THIRD_PARTY.md](THIRD_PARTY.md), pinned fetch scripts, release staging and
license notices when adding/updating a dependency. Preserve upstream copyright
headers. Do not assume the engine's license covers dependencies or project assets.
Project module licenses are listed in [README](README.md#license).
