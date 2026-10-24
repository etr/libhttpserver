# FEATURE-ci-audit-fixtures-and-evidence

Status: Complete (local implementation/validation/finalization; native CI pending).
Base: local v3 `6918a230c549d0322116760e141283cd71ecf744`

Repair the four supported native Windows audit fixture assumptions and retain
early installed-package inspection failure evidence for the existing package
and selected-archive recovery paths. Preserve strict policy and original failure.

- [x] Use portable PE fixture artifacts and actual native fixture roots; scope FreeBSD loader assertions.
- [x] Retain failed artifact, actual inspection command/status/output and resolved tool where available.
- [x] Persist early package-lane failure evidence without masking the original failure.
- [x] Verify actual subprocess/orchestration and both existing failure recovery paths locally.
- [x] Caller validation and finalization.

Evidence: primary `.debug/ci-audit-fixtures-evidence-pgijityi/journal.md`.
Windows native RED: 43 fixtures, six failures and two errors, retained at
`.debug/ci-a5-audit-windows-4nypx02d/fixture-command.log`. FreeBSD assessment
is CLOSED: the root consumer has seven DT_NEEDED entries, but actual readelf
output/tool identity was lost. Exact format and later loader health remain
unknown. This task captures evidence; it does not repair the FreeBSD parser.
Native post-fix Windows, FreeBSD and Darwin checks remain incomplete and CI/v3
PR owned. No CI green or publication claim. Other uncertainties remain CLOSED.

Local verification: all 43 original audit fixtures plus four new command/context
regressions pass (47 total). Twelve affected package-lane tests, seven existing
FreeBSD workflow/recovery tests (including new byte-for-byte inspection receipt
recovery through both EXIT copyback and real SCP peers), one successful selected
tool retention test, and two catalog provenance/alias retention tests pass.
Python compilation of all five changed scripts and diff whitespace checks pass.
No tests skipped in these local runs; no Python linter is documented.

TDD observed missing failed-node context, missing lane failure receipts, missing
launch/timeout command records, and optional tool metadata masking command
failure before their repairs. The subprocess boundary intentionally emits
unrecognized text; it establishes evidence retention and fail-closed behavior,
not native FreeBSD parser compatibility. Actual native format and later loader
health remain unknown. No parser, dependency policy, runtime allowance, recovery
workflow, public API or library source changed. Caller validation/finalization
and native post-fix execution remain pending.

Groundwork validation `76fe38b0ec454b69576216cbb1224b18` sealed PASS for
implementation `e6d1da57b0de0cefc9029669c2fa28a209319229`: one iteration,
nine fresh domain approvals, zero fixes and one unworked finding persisted
by the helper. Its report is retained without caller reading it. Native
post-fix gates remain incomplete; FreeBSD parser format remains unknown
and existing audit failure remains failed. Overall CI is unsuccessful.
