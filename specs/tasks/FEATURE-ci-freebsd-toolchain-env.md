# FEATURE-ci-freebsd-toolchain-env: retain selected SDK identity through archive native checks

Status: In Progress — implementation ready for caller validation and finalization.
Base: `v3` at `831aad155b9c4f0350b6376e2f589379039022d2`.
Plan: `.groundwork-plans/FEATURE-ci-freebsd-toolchain-env-plan.md` in the primary checkout.
Journal: `.debug/ci-freebsd-toolchain-env-b_nxofo9/journal.md` in the primary checkout.
Requirements: the existing complete selected SDK contract and native packaging
checks in `specs/architecture/v3/architecture.md`, with strict runtime ownership
under `PRD-V3N-REQ-002/003`.

Action items:

- [x] Read the accepted plan, literal journal, primary CLAUDE.md, applicable AGENTS
  and relevant selected SDK/workflow/runner/native audit sources.
- [x] Create the assigned isolated worktree from the exact local v3 base and
  preserve unrelated work.
- [x] Prove the missing handoff with the production archive orchestrator,
  production command runner, real child process, GNU make and native audit.
- [x] Select the explicit CLI SDK prefix in the archive runner's local subprocess
  environment so the matrix and both later TLS-mode native checks agree.
- [x] Verify explicit CLI precedence, inherited selection without a CLI override,
  quoted prefixes, missing identity rejection and rejection of another runtime.
- [x] Retain compiler/C compiler CLI selection, strict audits, complete archive
  checks and process-local SDK selection without Node/provider C flag changes.
- [x] Run affected existing tests, Python syntax and whitespace checks.
- [ ] Caller: full Groundwork validation, finalization, merge and cleanup.
- [ ] Native FreeBSD post-repair execution: CI/v3-PR owned, not executed locally.

The sourced FreeBSD `toolchain.env` supplies shell assignments, while the workflow
passes their values explicitly to the archive runner. That runner previously
forwarded `--toolchain-prefix` only to its child package matrix. Runtime selection
inside that child cannot update the parent environment, so later `gmake check`
lost `V3_PACKAGE_TOOLCHAIN`; the existing strict native audit then rejected the
private libc++ runtime. A conflicting inherited prefix could likewise disagree
with the explicit CLI selection. The repair belongs in `platform_proof`, which
owns the environment shared by its matrix and subsequent make commands.

The test-first execution reproduced `selected FreeBSD C++ runtime prefix missing`
at the actual later `gmake check -j2` boundary. With a stale inherited prefix it
reproduced `invalid C++ runtime prefix`. After the two-line repair both TLS-off
and TLS-on native audit recipes pass through real GNU make with controlled
FreeBSD `uname`/`ldd`/`nm` fixtures. Separate cases preserve inherited selection
when no CLI prefix is given and reject missing or foreign selected runtimes.
The regression also checks that the runner leaves `os.environ` unchanged.

Verification:

- Focused baseline: 5 existing platform environment/FreeBSD selector tests pass.
- `python3 scripts/test-v3-platform-ci.py`: 15 pass.
- `python3 scripts/test-v3-native-linkage.py`: 21 pass.
- `python3 scripts/test-v3-ci-workflows.py FreeBSDNodePrerequisites`: 6 pass,
  including the unchanged Node/provider environment contract in both workflows.
- Changed Python files pass `python3 -m py_compile`; `git diff --check` passes.
  CLAUDE's C++ cpplint does not apply to these Python/Markdown changes. No
  workflow changed, so actionlint is not an affected check.

RED/GREEN output and a durable real command/native audit trace are retained in
the primary checkout under
`.debug/ci-freebsd-toolchain-env-b_nxofo9/implementation/`; the durable trace is
`native-boundary/output/commands.json`. The package-producing fixture replaces
the expensive native matrix, and archive identity/count aggregation are controlled
test inputs; these runs prove environment propagation and strict audit behavior,
not native FreeBSD compilation, execution or libcxxrt ABI compatibility. Native
post-repair CI and overall CI-green status remain incomplete.

The predecessor SDK validation remains sealed and unchanged. Existing local
LLVM20 cancellation/compiler/runtime evidence remains valid without rebuilding
the SDK. No full baseline, old validation restart, unrelated failure investigation,
new native CI run, publication, push or merge was performed. Previously closed
Darwin/Linux/Windows uncertainty batches remain closed.
