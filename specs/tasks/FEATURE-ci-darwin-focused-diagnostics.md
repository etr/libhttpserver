# FEATURE-ci-darwin-focused-diagnostics

Status: In Progress — implementation prepared; caller validation and the one native batch remain pending.

Base: local `v3` at `a5a72356741300e5da16b94090f0f26a67a2d1f7`.
Branch: `task/FEATURE-ci-darwin-focused-diagnostics`.
Plan: `.groundwork-plans/FEATURE-ci-darwin-focused-diagnostics-plan.md` in the primary workspace.
Journal: `.debug/ci-darwin-focused-diagnostics-cn_lss1c/journal.md` in the primary workspace.

This is temporary diagnostic instrumentation. This branch, including its temporary replacement of `.github/workflows/release.yml`, must never merge into `v3`. The source assertions, budgets, timeout values, allocation fault countdowns and runtime decisions remain authoritative. A passing native observation establishes intermittency only. No root cause or production repair is claimed.

- [x] Read literal journal, selected historical assertion logs and owning tests/boundaries. Prior batches stay closed.
- [x] Instrument actual continuation/local noop values; outbox capacities and abort frames; QPACK allocation attempts and original error result; resource push kind/capacity/budget; peer bind/connect operation/address/errno; header watchdog, timer submission/claim/application, published readiness, actual poll and close timestamps.
- [x] Keep private scalar tracing compile-time disabled in ordinary builds. The helper uses one fixed 512-byte stack buffer and one write, preserves errno, creates no event store/lock and performs no C++ allocation. Runtime flags narrow library traces to the header or storage processes.
- [x] Prepare manual-only, read-only native Darwin batch under existing default-branch dispatch-capable `release.yml` filename. The required `tag` is a fixed diagnostic sentinel; it never selects a ref, creates a tag or publishes anything.
- [x] Compile syntax, diagnostic logger smoke checks, workflow/embedded Python/shell syntax, actionlint, changed-file cpplint and diff checks locally; no native tests or full suites in this phase.
- [ ] Caller independently validates the diagnostic diff and reviews the workflow before publishing this branch only.
- [ ] Caller dispatches the command below ONCE and retains source-bound receipt, raw logs and LLDB abort frames.
- [ ] Caller records actual native results, context differences and causal assessment without reopening closed probes.

Proposed single dispatch after caller review/publication:

```bash
gh workflow run release.yml --ref task/FEATURE-ci-darwin-focused-diagnostics -f tag=darwin-focused-once-a5a72356
```

The workflow rejects other event/ref/sentinel values, dirty checkout and rerun attempts, binds checkout and Git source archive to `github.sha`, and uses the existing macOS 15/compiler-wrapper/provider prerequisite owners and GNU make. Root independently verified the current default-branch `release.yml` exists and supports dispatch. There are no push, tag, schedule, release or deployment triggers/jobs in this temporary replacement.

The five historical TLS-no programs (`task_resume_signal_lifetime`, `http1_response_outbox`, `http3_qpack`, `resource_limits`, `consumer_v3_peer_policy_runtime`) build through their declared native make targets, retaining the target's real shared/convenience-static linkage. `external_http1_e2e` builds once against an installed TLS-yes shared library using the same real test/parity translation units and private integration include context as the historical package suite. All execute at EUID 0, matching the recurring header failure. There is no extra static-only header execution. All six execute at most once, each with a 180-second outer LLDB bound; later programs still build/run after an earlier failure or abort. Existing test case assertions and their own 300 ms/10 s deadlines remain unchanged.

The artifact contains exact archive bytes/hash, commit/run identity, selected tools/environment, configure/compiler/link commands and original command logs, binaries/link map, provider preparation record and LLDB exit/stop/backtrace/local-variable evidence. A launch/setup failure remains incomplete. This is a bootstrapped Git source archive, not the historical generated release tarball. Current selected tools/provider and temporary tracing/debugging can change timing. This evidence is diagnostic only and cannot substitute for release acceptance. The existing independent CI monitor is untouched.

Narrow source-storage tracing was approved by the caller because returned sink state cannot reveal a destroyed rejected allocation. It observes only existing `bound`, `actual`, `excess`, provisional charge and successful result locals in `reserve_buffer_locked`; it adds no capacity/reserve probes or accounting changes. The real host poll helper is instrumented at its syscall boundary to capture the actual submitted timeout and returned revents, without an alternate poll implementation.

Local evidence: enabled C++20 syntax/format checking succeeded for the six test TUs and changed source TUs using the existing primary Linux `build/config.h` only to satisfy `connection_engine.cpp`'s generated include. The first syntax attempt lacked that header; corrected execution setup passed. Linux excludes the native kqueue body; its Darwin compilation and all native execution remain pending. Logger smoke checks confirmed enabled output, preserved errno and disabled operand elision (no test suites executed). Shell/embedded Python/LLDB-script parsing and actionlint passed. Final complete changed-file lint/diff results are recorded in the primary journal.

Implementation notes for future task executors: Autotools requires out-of-tree builds; `./bootstrap`, configure with selected CC/CXX/MAKE, then GNU make. These six tests use the existing littletest harness (`-Itest` for `./littletest.hpp`), except the peer consumer's `require` checks. `src/libhttpserver_v3core.la` supplies private implementation tests; normal consumers link `src/libhttpserver.la` or installed shared library. Public headers must not include private/platform/provider headers. Worktree creation required sandbox escalation for Git metadata even though the task files are within the writable repository. No preexisting task-executor agent memory was available at the usual agent-memory locations; this record retains the setup facts.
