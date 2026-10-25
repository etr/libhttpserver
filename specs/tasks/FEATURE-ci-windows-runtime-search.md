# FEATURE-ci-windows-runtime-search

Status: In Progress (implementation; caller owns validation/finalization).
Base: local v3 `7792d6252f44bc04b0018128819bd0f760b33c66`.
Plan: primary `.groundwork-plans/FEATURE-ci-windows-runtime-search-plan.md`.
Journal: primary `.debug/ci-284-windows-pe-resolution-xntbf7u3/journal.md`.

Pass the declared Windows toolchain's `bin` directory to the existing strict PE
resolver. Preserve installed prefix/provider/prerequisite priority, explicit
selection over inherited declaration, and existing runtime/graph policy.

- [x] Read accepted plan, actual native records and owners; isolate exact local v3.
- [x] Verify five focused existing PE controls and record behavioral RED through real resolution/graph policy.
- [x] Append selected Windows runtime bin to DLL resolver roots only.
- [x] Verify focused runtime/PE and affected existing consumer contracts, Python syntax and diff.
- [ ] Caller Groundwork validation/finalization.
- [ ] Native Windows post-fix package/archive availability and loader checks: CI/v3 PR owned, incomplete.

Native RED: package run 40084556223 / MinGW job 120844695264 and archive
run 40084572210 / Windows job 120844856149. Actual PE root imports
`libgcc_s_seh-1.dll`; declared toolchain is `D:/a/_temp/msys64/mingw64`,
while `V3_PACKAGE_DLL_DIRS` omits its bin directory. Existing policy already
permits only GNU compiler runtime families under the selected declaration.
The retained artifact does not include toolchain DLL bytes: their actual native
presence is UNVERIFIED. Controlled fixture bytes and objdump outputs establish
local resolver/policy behavior only.

All prior unrelated uncertainties remain CLOSED: configure timestamp checks
FAILED/INCOMPLETE and unretried; epoll diagnostic budget EXHAUSTED/all-pass
UNKNOWN; adapter UNKNOWN/CLOSED. No broader repair or investigation is authorized.

Implementation: runtime_environment appends the effective declaration's `bin`
to Windows `V3_PACKAGE_DLL_DIRS` after all existing roots. Explicit toolchain
selection still overwrites the inherited declaration before this handoff. No
PATH, LD_LIBRARY_PATH or DYLD_LIBRARY_PATH behavior changes; Unix output and
input mapping immutability are covered. No resolver, allowlist, graph policy,
production library, workflow, provenance or native compiler change.

RED: the new real resolver/graph regression fails three selection subcases
with the actual unresolved GNU runtime import before the three-line correction.
GREEN: seven new tests pass; final focused selection runs 23 package PE/policy
tests and 26 existing consumer matrix/inspection tests, all PASS. The seven
behavioral tests verify exact selected file identities/SHA256, explicit and
inherited declaration/override, existing namespace priority, absent files,
undeclared or empty selection, PATH-only/unselected siblings, foreign runtime
families, wrong-prefix runtime lookalikes, loader environment and Unix behavior.
Controlled external objdump output exercises the actual inspector/parser,
resolver, graph hashing and strict runtime classification policy.

Python byte-compilation and diff whitespace checks PASS. No Python linter is
documented; cpplint applies to unchanged C++. Durable command logs and base/
current hashes are in the primary journal's `implementation/` directory.
The full auditor, dependency policy, native linkage checker and consumer runner/
fixtures retain identical bytes. No broad suite, build, native probe, diagnostic
reopening, publication or CI control occurred. Native file availability and
Windows loader behavior remain unverified and assigned to the next actual CI.

Reusable executor notes: run focused classes/methods with Python unittest script
arguments; no installation is needed. Keep temporary artifact paths canonical,
replace only package_commands' local OS binding and auditor's local platform
binding, and control external objdump subprocess results rather than owned
resolution or graph methods. Generated syntax output belongs under ignored
`build/`. Caller owns review, validation, finalization and publication.
