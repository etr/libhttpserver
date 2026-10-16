# FEATURE-ci-v3-integration

Status: locally implemented and tested; local verification recorded in the task evidence;
actual GitHub PR verification and final validation remain root-owned.

Scope: native CI/replay, installed platform prerequisites, bounded fuzz campaigns,
selected-archive release acceptance and publication enforcement. No production
protocol behavior, default-branch setting, tag or release changes.

- [x] Add source-aware v3 push/PR native builds and CodeQL TLS coverage.
- [x] Repair actual PR-base ChangeLog comparison and pin touched external actions.
- [x] Keep package prerequisites/builds/evidence outside audited source roots;
      select pinned clients and executable native Git.
- [x] Shard all ten TLS-off fuzz targets and TLS-on callbacks; preserve core
      instrumentation, bounded smoke, varying campaign seeds and corpus trust.
- [x] Add default-branch schedule code inside v3, with deferred activation.
- [x] Prepare/select one archive; retain fixed portable platform evidence and
      invoke the existing complete readiness validator on live Linux evidence.
- [x] Bind immutable artifacts and authorization; isolate publication permissions
      and recheck the existing tag before the release side effect.
- [x] Exercise publication refusal and success through the actual validator,
      with only the final publication side effect mocked.
- [x] Root: verify manual native campaign dispatch invocation (accepted at
      08c5b7b5; campaign execution outcomes remain pending).
- [ ] Root: complete actual GitHub PR pipeline verification. First selected
      archive attempt failed at API PR-head versus merge-SHA origin binding;
      causal repair and negative controls passed locally; fresh CI is pending.
- [ ] CI / v3 PR: obtain fresh full local gate and FreeBSD/Darwin/Windows receipts
      for the selected commit/archive. Missing evidence is not a release pass.

Details and commands: [v3 CI](../../docs/v3-ci.md). Historical TASK-185 receipts
are unchanged and cannot replace fresh evidence.

Local verification: native TLS-off/on suites (239/270 tests, two documented
nonlocal backend skips in each), all eleven sanitizer fuzz targets, genuine
Doxygen/Graphviz, GCC QUIC replay and 100 initial CI/release/fuzz contracts passed.
The subsequent artifact-origin repair adds three focused contracts (103 total).
The fresh Linux installed attempt passed TLS-off but failed TLS-on public TLS at
unprivileged TCP443 binding. Its unsuccessful receipt remains unchanged. A fresh
focused public TLS run using the existing isolated network path passed all four
shared/static and managed/external cells (82 checks each). This proves the
affected prerequisite correction; it does not turn the failed whole matrix into
a passing receipt. The fresh complete 18-gate release run remains pending CI.
