# FEATURE-ci-windows-date: portable HTTP date conversion

Status: Complete (local implementation, Groundwork validation and finalization). Native execution remains CI / v3 PR owned. Base: `v3` at `1c7e490721802c74fe023f8c0ffd637f85a47c21`.

Native Windows job120372343449 in run39941070929 failed to compile the actual framer because `gmtime_r` is unavailable. The compiler's MinGW header diagnostic declares `gmtime_s(struct tm*, const time_t*)`. The affected source is unchanged between failed CI commit56da7ec9 and this task's base. This actual native compiler failure is the accepted RED for the compile-only platform selection repair.

The framer now selects `gmtime_s(&tm_value, &t)` on `_WIN32` and preserves `gmtime_r(&t, &tm_value)` elsewhere. UTC conversion, thread safety, IMF-fixdate formatting, return semantics and existing error policy remain unchanged.

Action items:

- [x] Create the isolated task worktree from local v3 using Groundwork's identity helper; preserve unrelated bauth work.
- [x] Retain original native compiler RED and bind its source to the task base.
- [x] Add only the Windows/POSIX conversion selection with the correct Windows argument order.
- [x] Run the existing focused framer/date tests against actual production translation units.
- [x] Run changed-file cpplint and `git diff --check`.
- [x] Preserve exact commands, raw outputs and verification limits in `.debug/FEATURE-ci-windows-date/` at the repository root.
- [x] Caller: Groundwork validation and local finalization into v3. Three focused reviewers approved with zero findings.
- [ ] CI / v3 PR: fresh native Windows compilation/execution.

Local verification: GNU C++20 compilation with `-Wall -Wextra` passes without warnings. The unchanged complete `http1_response_framer` executable passes64 tests/174 checks with `LC_ALL=C TZ=Pacific/Honolulu`, including exact RFC timestamp formatting, Date field order/omission/precedence and framing/corpus checks. cpplint and diff checks pass. No synthetic or source-text test was added. The first task-local link omitted actual parser dependencies; its failure is retained, and the corrected command links the real resource-charge, budget and target translation units. This was execution setup, not a production defect or native Windows GREEN.

Native Windows compilation/execution is unavailable locally: no installed Windows cross compiler/SDK was found. This remains CI / v3 PR owned under AGENTS.md and does not block local completion. No dependency installation, broad baseline/full suite, merge or publication was performed.

The separate Linux parity failure remains **UNCERTAIN**, as settled by the prior bounded check. Evidence and the LSan/ptrace process failure remain in `.debug/FEATURE-ci-native-runtime/journal.md`; a passing replay does not establish independence or a passed process gate. This task does not reopen, retry or repair that issue.

Project notes: Autotools/C++20, out-of-tree builds (`./bootstrap`, `configure --disable-v3-tls`, `make`); custom Littletest under `test/unit`. This task directly compiled the existing focused test with its actual production dependencies and parity helpers. `cpplint` uses root `CPPLINT.cfg`. Worktree creation required the existing Git sandbox escalation; branch/path came verbatim from Groundwork's helper.
