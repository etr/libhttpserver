# FEATURE-ci-audit-fixture-paths: canonical audit fixture roots

Status: Implemented; caller Groundwork validation and finalization pending.
Base: `v3` at `54fa6aae23ee91db861a9802f27d2dac9c28ed2c`.

Mac package job120420002071 and archiveDarwin job120420935245 both failed
`test_selected_unwind_still_inspects_hashes_and_rejects_transitive_provider`
with a `/private/tmp/.../consumer` dictionary lookup error. The audit correctly
canonicalizes graph identities, while the fixture built its keys from an
unresolved `/tmp` directory. `AuditTest.setUp` now resolves its temporary root
before constructing paths, matching the existing `CompileProbeTest` fixture.

Action items:

- [x] Create the isolated Groundwork worktree from the recorded v3 head;
  preserve the bauth worktree and all prior closed repairs.
- [x] Preserve both original native CI logs and reproduce the actual original
  test entrypoint locally through a real symlinked temporary root before edits.
- [x] Add a durable regression that runs the original unwind contract through
  a real temporary directory alias; observe its canonical lookup failure first.
- [x] Canonicalize only the fixture root. Preserve production audit code and
  all 40 original test bodies, including inspection, hashes, transitive provider
  rejection, missing dependency rejection, prefix negatives and symlink escapes.
- [x] Keep tempfile selection and environment restoration scoped; run nested
  fixture cleanup before deleting its outer alias directory. Check restoration
  even when the tempfile cache started uninitialized.
- [x] Run the regression, original entrypoint, complete affected module and
  focused alias cases; run changed-file Python compilation and diff checks.
- [x] Retain raw RED/GREEN commands, logs and journal under root
  `.debug/FEATURE-ci-audit-fixture-paths/`.
- [ ] Caller: Groundwork validation and local finalization into v3.
- [ ] CI / v3 PR: native Mac package and selected-archive execution.

Local verification: all 41 affected module tests pass (all original 40 plus
the alias regression). Six additional focused cases pass under a real symlinked
temporary root: the original unwind contract, canonical selected-directory and
numeric-name negatives, graph cycle/alias edges, installed header escape, and
both compiler probe contracts. The original entrypoint also passes with the
same reproduction driver that failed before the fix. AST comparison confirms
all original test methods are unchanged; production audit bytes are unchanged.
Changed-file `py_compile` and `git diff --check` pass. Python scripts have no
documented standalone linter; no C++ files changed.

The regression uses real directory symlinks and the original test case through
unittest, replacing only temporary-root selection. It does not mock path
resolution, skip original contracts, or alter runtime policy. The Linux host
supports these symlinks. Local fixture execution is not native Mac package or
archive proof; those gates remain incomplete and CI / v3 PR owned under AGENTS.

No broad native baseline, CI rerun, validation loop, merge or publication was
performed. Older Linux parity causality remains UNCERTAIN after its previously
closed bounded diagnosis; no additional investigation was run. Closed entropy,
selected LLVM unwind policy, and prior consumer alias findings remain unchanged.

Project notes: Autotools/C++20 builds require bootstrap and out-of-tree configure
and make; this fixture-only change needs no dependency installation. Tests use
Python unittest and the available C++ compiler. Worktree identity came verbatim
from the Groundwork helper; Git metadata writes required sandbox escalation.
