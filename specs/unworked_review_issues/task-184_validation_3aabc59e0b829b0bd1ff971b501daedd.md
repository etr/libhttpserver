# Unworked Review Issues

**Run:** 2026-10-10 02:16:40
**Task:** TASK-184
**Total:** 4 (0 critical, 0 major, 4 minor)

## Minor

1. [ ] **code-quality-reviewer** | `examples/README.md:77` | documentation
   The retained legacy section still says v2 examples build with --enable-examples and that check-examples.sh enforces the v2 introductory examples. The changed examples/Makefile.am and both example check scripts now build or verify only the four native examples; the legacy headers required by those v2 sources were removed.
   *Recommendation:* Label the legacy examples as historical source references that are not built by v3, and describe the example check scripts as native-only.

2. [ ] **code-simplifier** | `scripts/audit-v3-installed-package.py:326` | code-structure
   `provenance_violations` combines record iteration, comment filtering, an exception for the header-hygiene assignment, and regex matching in one nested comprehension, which makes its allow/filter rules harder to scan.
   *Recommendation:* Extract the line-selection rule into a descriptively named helper or use a short explicit loop before collecting matches; keep the current filtering behavior unchanged.

3. [ ] **code-simplifier** | `scripts/audit-v3-installed-package.py:381` | code-structure
   The audit still exposes `pre-cutover` as a mode even though dependency and archive checks now reject legacy paths identically in both modes; the mode also flows through `symbol_violations` without affecting its logic.
   *Recommendation:* Make strict auditing unconditional, remove the obsolete mode parameter and CLI choice, and update the test that iterates over both equivalent modes.

4. [ ] **housekeeper** | `examples/README.md:76` | documentation-stale
   The Legacy v2 examples section says the remaining MHD-backed examples build with `--enable-examples` and that `verify-installed-examples.sh` compiles examples after installation. TASK-184 removes those legacy example build targets: `examples/Makefile.am` now registers only four native v3 programs, and the installed-example script compiles only those four. This leaves the usage guidance inaccurate for readers following the examples page.
   *Recommendation:* Clarify that the remaining v2 example sources are historical/reference material and are no longer built or installed by the native `--enable-examples` configuration; describe the native-only scope of the installed-example check.
