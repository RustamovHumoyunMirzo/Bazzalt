# Maintaining the API reference

The editor manual explains editor workflows. Programming guides belong under
`native` (C++) or `lua`; callable contracts belong under their `reference` folders.
Keep the manual's workflow explanation and link to the relevant language reference
instead of leaving a method documented only in the manual.

Editable pages remain static HTML at `docs/public/<version>/<section>/<topic>/<language>.html`.
Language and version discovery still uses folders and filenames, not another registry.

After changing a public header or Lua binding:

1. Update the explanatory prose, examples, units, error and lifetime notes in the
   corresponding C++ and Lua reference pages. Never imply a C++ method exists in
   Lua unless it is actually bound. Document tuple results and component copies.
2. Run `python scripts/sync_api_reference.py`. It refreshes marked declaration and
   binding sections of the newest version only and creates pages for missing types.
   Prose outside `API-SOURCE` markers is preserved. Do not edit inside those markers.
3. Run `python scripts/build_public_docs.py` to refresh navigation, search and links.
4. Run both scripts with `--check`, then `python -m unittest discover -s tests -p test_public_docs*.py`.

The source inventory intentionally omits `Detail` namespaces and implementation-only
headers. SDK ABI declarations remain distinguishable from ordinary gameplay APIs.
It does not replace explanatory prose or certify behavior: declarations alone cannot
describe ownership, valid phases or error handling. Add tests when extending binding
macros or the declaration grammar. Historical versions stay unchanged.

Lua references list registered members, not every C++ method on the same type. Binding
adapters are included for precise overload/tuple/optional behavior and labeled as C++
notation, not Lua examples. The existing examples remain the beginner-facing entry.
