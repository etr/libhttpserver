# FEATURE-ci-freebsd-readelf-format

Status: Complete (local validation/finalization; native CI pending).
Base: local v3 `e56a14f0f6748209ac39c1319bc1c894529ab2a4`.
Plan: primary `.groundwork-plans/FEATURE-ci-freebsd-readelf-format-plan.md`.
Journal: primary `.debug/ci-7880-freebsd-readelf-6x0vi2k0/journal.md`.

Accept the captured FreeBSD bare NEEDED dynamic tag column in the installed
package auditor, preserving GNU parenthesized tags and fail-closed inspection,
resolution, recursive graph evidence, runtime classification and package policy.
The native receipt supplies new evidence for this repair; prior closed unknown
assessments remain closed. Scope is the ELF needed-format parser and its tests.

- [x] Read accepted plan, primary instructions, native stdout and existing owner/tests; isolate from exact local v3.
- [x] Record meaningful RED through the real inspector with exact native stdout and all seven expected names.
- [x] Make the minimal parser repair with GNU compatibility and strict malformed/noise controls.
- [x] Verify affected suite, direct consumer/failure evidence contracts, real local GNU ELF, Python compilation and diff checks.
- [x] Caller Groundwork validation/finalization.
- [ ] Native FreeBSD post-fix package/archive checks: CI/v3 PR owned, incomplete; not a local acceptance gate.

No runtime leaves, whitelists, loader/policy relaxation, inspector retries or new
native format assumptions are authorized. No broad baseline/SDK/native probe,
CI control, merge, publication or cleanup belongs to this implementation.

Local implementation evidence (2026-10-23):

The native stdout is retained literally in the existing distributed test module,
with its command/job/artifact origin and SHA256. The real platform inspector and
command recorder, with only subprocess output controlled, resolve all seven
expected names to exact paths. The old parser raised `empty ELF dependency
inspection` on this regression. The replacement recognizes full bare/parenthesized
NEEDED rows; malformed tags, wrong labels, empty names and surrounding diagnostic
noise cannot supply dependency entries. Existing GNU fixtures remain unchanged.
Dynamic-section, nonruntime empty-result, unresolved/missing ldd resolution,
recursive provenance/hash and runtime/TLS/legacy policy requirements remain.

Verification: all 53 installed-package tests pass (48 existing, five new); all
four installed-consumer inspection-failure tests and the direct package/archive
receipt-recovery test pass. A small real local GNU C++ ELF goes through real
readelf/ldd and recursive graph inspection: six hashed nodes, 11 edges, strict
TLS-off policy passes. This proves local GNU compatibility, not native FreeBSD
loader behavior. Python compilation and diff whitespace checks pass; there is
no documented Python lint command. Original 54 existing helper/test method ASTs
are unchanged, and the embedded native stdout matches all 1968 captured bytes.

Logs, source hashes, local GNU graph/commands and limits are retained in primary
`.debug/ci-7880-freebsd-readelf-6x0vi2k0/implementation/`. One mistyped recovery
test selector failed at discovery; its log is retained separately, the exact
existing selector was then used and passed. No subsystem failure was observed.
Caller Groundwork validation passed and local finalization is recorded below. Native FreeBSD
post-fix package/archive checks remain CI/v3 PR owned and incomplete.

Groundwork validation `ea1324dc403a04447c1cce7f522e9740` sealed PASS for
`1172ebdcdc41fbec480f6fe3805b75d238b09eb1`: one iteration, nine independent
approvals, zero fixes and one approved unworked finding persisted by the helper.
The helper report is retained unread for separately owned work. Native FreeBSD
post-fix package/archive validation remains incomplete and CI/v3 PR owned.
Prior failed receipts and closed uncertainty remain preserved.
