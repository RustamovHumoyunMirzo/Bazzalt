# Upstream notice snapshots

`third_party/` contains unmodified license and NOTICE files from the pinned
Filament and Qt source releases, not dependency implementations. `sources.json`
records each upstream URL and its SHA-256 digest. The snapshots intentionally
include notices for optional SDK/tool dependencies as well as linked libraries.

Refresh them with `python scripts/update_third_party_notices.py` when changing
those dependency versions. This downloads only notice files. Review the diff and
run `python -m unittest discover -s tests -p test_third_party.py` before committing.
Do not edit upstream license texts to match BAZZALT's own license.

`scripts/collect_third_party.py` combines these snapshots with fetched direct
dependency notices and, for Editor/Hub, installed Python package notices.
Release staging copies the result into each product's `licenses/` directory.
See [THIRD_PARTY.md](../THIRD_PARTY.md) for dependency attribution and scope.
