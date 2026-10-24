# FEATURE-ci-darwin-focused-diagnostics

Status: In Progress — first native attempt failed in launcher setup before all six programs; local environment correction passed focused checks and awaits caller review.

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
- [x] Caller independently validates the diagnostic diff and reviews the workflow before publishing this branch only.
- [x] Caller dispatched the first attempt and retained its source-bound failed setup receipt and raw logs; all six selected programs remain not-executed.
- [x] Correct the workflow host/debugger versus inferior environment boundary and verify it with real local subprocess checks, syntax and actionlint.
- [ ] Caller reviews the narrow setup delta before publishing the correction and continuing the same authorized six-program batch.
- [ ] Caller records actual native results, context differences and causal assessment without reopening closed probes.

Corrected continuation after caller delta review/publication (no selected program execution budget was consumed):

```bash
gh workflow run release.yml --ref task/FEATURE-ci-darwin-focused-diagnostics -f tag=darwin-focused-once-a5a72356
```

The workflow rejects other event/ref/sentinel values, dirty checkout and rerun attempts, binds checkout and Git source archive to `github.sha`, and uses the existing macOS 15/compiler-wrapper/provider prerequisite owners and GNU make. Root independently verified the current default-branch `release.yml` exists and supports dispatch. There are no push, tag, schedule, release or deployment triggers/jobs in this temporary replacement.

The five historical TLS-no programs (`task_resume_signal_lifetime`, `http1_response_outbox`, `http3_qpack`, `resource_limits`, `consumer_v3_peer_policy_runtime`) build through their declared native make targets, retaining the target's real shared/convenience-static linkage. `external_http1_e2e` builds once against an installed TLS-yes shared library using the same real test/parity translation units and private integration include context as the historical package suite. All execute at EUID 0, matching the recurring header failure. There is no extra static-only header execution. All six execute at most once, each with a 180-second outer LLDB bound; later programs still build/run after an earlier failure or abort. Existing test case assertions and their own 300 ms/10 s deadlines remain unchanged.

The artifact contains exact archive bytes/hash, commit/run identity, selected tools/environment, configure/compiler/link commands and original command logs, binaries/link map, provider preparation record and LLDB exit/stop/backtrace/local-variable evidence. A launch/setup failure remains incomplete. This is a bootstrapped Git source archive, not the historical generated release tarball. Current selected tools/provider and temporary tracing/debugging can change timing. This evidence is diagnostic only and cannot substitute for release acceptance. The existing independent CI monitor is untouched.

Narrow source-storage tracing was approved by the caller because returned sink state cannot reveal a destroyed rejected allocation. It observes only existing `bound`, `actual`, `excess`, provisional charge and successful result locals in `reserve_buffer_locked`; it adds no capacity/reserve probes or accounting changes. The real host poll helper is instrumented at its syscall boundary to capture the actual submitted timeout and returned revents, without an alternate poll implementation.

Local evidence: enabled C++20 syntax/format checking succeeded for the six test TUs and changed source TUs using the existing primary Linux `build/config.h` only to satisfy `connection_engine.cpp`'s generated include. The first syntax attempt lacked that header; corrected execution setup passed. Linux excludes the native kqueue body; its Darwin compilation and all native execution remain pending. Logger smoke checks confirmed enabled output, preserved errno and disabled operand elision (no test suites executed). Shell/embedded Python/LLDB-script parsing and actionlint passed. Final complete changed-file lint/diff results are recorded in the primary journal.

Implementation notes for future task executors: Autotools requires out-of-tree builds; `./bootstrap`, configure with selected CC/CXX/MAKE, then GNU make. These six tests use the existing littletest harness (`-Itest` for `./littletest.hpp`), except the peer consumer's `require` checks. `src/libhttpserver_v3core.la` supplies private implementation tests; normal consumers link `src/libhttpserver.la` or installed shared library. Public headers must not include private/platform/provider headers. Worktree creation required sandbox escalation for Git metadata even though the task files are within the writable repository. No preexisting task-executor agent memory was available at the usual agent-memory locations; this record retains the setup facts.

Groundwork validation `2c3b204286112d4867f6fe7d78b1920e` sealed PASS for
implementation HEAD `6d69f85ab44595a6a25626cd31707327e7e8d2de`: one
iteration, nine independent domain approvals, zero fixes, one unworked finding
persisted by the helper. The helper report is retained without caller reading it.
This sealed validation is historical diagnostic readiness; it does not establish successful native setup or execution.

First native attempt `40052229400`, job `120747190075`, source `8f9722656397961dc9b5d1af360552541a6ff398`, failed before bootstrap/build: `xcrun lldb --version` exited `-6` under the workflow's global provider/Homebrew LLVM `DYLD_LIBRARY_PATH`. The retained log reports missing Apple libc++abi symbols, a pure virtual call and mutex fatal error. Artifact `12544022476` SHA-256 `269c9db7bb48f58be881c5aa700cfc74c73605b48969d1334730f3fae82ea31d` was verified; its original receipt remains unchanged at the journal's `first-native-evidence/receipt.json`. All six cases are `not-executed`; no C++ result or cause was established.

The correction removes inferior-only loader/tracing variables from host compiler/build/inspection/debugger environments. It retains the existing selected compiler wrapper and provider linker rpath, and passes the complete build-or-installed-prefix, provider and selected toolchain library path explicitly through LLDB `target.env-vars`. Header/storage tracing stays in the relevant inferior environment. Each case records that target environment separately from the debugger command environment. No C++ instrumentation, test assertion, timeout or runtime decision changed.

Focused proof lives in the primary journal's `setup-repair/test_environment_boundary.py`: the original workflow failed the boundary assertions; all six tests pass against the correction. Tests execute the actual embedded workflow environment construction, `run_case`, generated LLDB settings, inspection call sites and command logging/monitoring with real local Python child processes. Only unavailable `xcrun`/inspection executable argv are adapted to the local environment reporter; there is no simulated LLDB result, Darwin execution, selected program launch or native success claim. They verify clean host/debugger environments, both complete inferior library paths, trace isolation, selected compiler/provider flags and no mutation back into build/metadata environments. All five shell steps and both embedded Python scripts parse; actionlint 1.7.7 and diff whitespace checks pass. Native setup/compile/execution remain unverified locally and require the caller's corrected native continuation after delta review. No full suites, native execution, publication, dispatch, merge or cleanup were performed during this repair.
