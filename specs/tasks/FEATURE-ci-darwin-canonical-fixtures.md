# FEATURE-ci-darwin-canonical-fixtures

Status: In Progress
Base: local v3 `1fa030dd528e67218d7c0a6957371ba246679e96`

Repair the exact known Darwin Python fixture root aliases, executable discovery,
selected compiler and native linker-map setup failures. Keep original child names,
symlink/tampering controls and production provenance policies intact.

- [x] Reproduce known identity failures with real symlinked TMPDIR and actual validators.
- [x] Resolve affected fixture roots before constructing identities/catalogs.
- [x] Discover the success executable and use the selected compiler/native map format.
- [x] Verify affected suites, syntax and diff; preserve negative controls.
- [ ] Caller validation and finalization.

Evidence: primary `.debug/ci-darwin-canonical-fixtures-2shorq1l/journal.md`.
Native post-fix Darwin, Windows and FreeBSD execution remains CI/v3 PR owned.
No C++ investigations, runtime changes, full suite, SDK/Bash rebuilds or CI controls.

Caller-approved owner expansion: `scripts/lib/release_package_provenance.py`
`validate_compile` now recognizes the native Darwin linker option and contributing
object-file table entries. GNU handling and exact installed archive/security bindings
are preserved; actual real compilation uses unchanged `package_integration.compile_probe`.

Verification: all six affected suites pass locally (155 tests including six real
symlinked TMPDIR regressions), plus final focused reruns after test refactoring.
Python byte-compilation and `git diff --check` pass. No standalone Python linter is
documented; the repository cpplint command applies to C++, which is unchanged.
RED/GREEN logs cover each known alias symptom, selected executable/compiler and native
map grammar. No authentic retained Darwin map exists in the selected evidence tree;
small ld64 format tests prove parser behavior only. Real local GNU map/archive controls
prove foreign-first rejection and selected-only acceptance. Native post-fix platform
checks remain incomplete and CI/v3 PR owned. Older C++ uncertainties remain closed.
