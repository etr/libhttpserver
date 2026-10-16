# TASK-185 packaged release gates

The explicit release runner creates a fresh distribution, verifies its source
snapshot, builds installed TLS-off/on packages and preserves independent gate
results. Local execution and full tag readiness are separate results. FreeBSD,
macOS and Windows are CI/v3-PR owned. TASK-186–TASK-200 supply the repaired product capabilities and native benchmark
adapters. Public TLS/H2/H3, credential and throughput/latency stages now execute
their installed-package adapters in both TLS modes with independent receipt
validation. Private fixtures remain supporting evidence.

## CI selected-archive execution

The CI integration in [v3-ci.md](v3-ci.md) selects one immutable archive,
produces portable FreeBSD/Darwin/Windows bundles, then executes the existing
18-gate Linux inventory against that archive. Original platform receipts and
referenced bytes are retained; a scoped catalog resolver validates them without
foreign execution. Full readiness authorizes publication only after the actual
validator accepts every required gate and platform. Existing fresh-archive
callers and the separate release-owner pre-tag prerequisite remain supported.
Validation-only PR/dispatch runs exercise the same acceptance path without
creating tags or releases. Historical evidence below remains historical.

## Complete local implementation inventory (2026-10-14)

The fresh `build/task185-final-19` inventory passes all **18 mandatory local
gates** on one selected distribution archive. The launcher exits zero. The
runner independently rehashes every retained gate after the final benchmark;
`status=passed`, `local_passed=true`, and `tag_ready=false`. Only required
FreeBSD, macOS and Windows evidence remains pending, owned by CI / the v3 PR.
This satisfies local implementation acceptance under the repository policy;
validation, commits and finalization remain runner-owned.

| Selected evidence | SHA-256 |
| --- | --- |
| Distribution archive | `207a65567769c4045d310556caed7c2998b69e9d1c90ebb7d7418e13fa911c75` |
| Source snapshot | `244b24823bab20d2da31f6fd82ad0277faac95bbdd845264a8d3323e7f42cc08` |
| Completed manifest | `a8420509115bd4f82f6f557b0f00bc4e4edf42fe423b58d63d68e4dd9747afa6` |

| Mandatory local gate | Successful observed units |
| --- | --- |
| package | 848 tests / 14,923 checks; both TLS modes, shared/static and managed/external. |
| cxx20-off / cxx20-on | 229 / 260 registered local suites. |
| http1-websocket / http2 | 539 / 155 scenario/check units. |
| tls | 167 tests / 4,275 checks, including installed rotation witnesses. |
| parity | 168 witnesses. |
| resource-plateaus | 44 tests / 871 checks with the normal allocator. |
| resource-slow-peers | 12 measured slow-peer/control windows. |
| asan-ubsan-off / asan-ubsan-on | 229 / 260 local suites; mandatory leak, stack-return, scope and nonrecovering UB checks enabled. |
| tsan-off / tsan-on | 142 tests / 7,772 checks and 167 tests / 8,254 checks; the two named existing allocation exclusions remain explicit. |
| http3-full | All nine native, transport/capture, independent-client, WebSocket and instrumented-fuzz subgates. |
| fuzz-off / fuzz-on | 20,000 / 2,000 completed bounded runs. |
| public-capabilities | All 32 installed-package capability cells. |
| native-throughput-latency | All 20 optimized full-workload benchmark cells, including all four H3 linkage/loop combinations. |

Each benchmark cell completes 4,096 measured requests with the full configured
warmup and protocol-specific epochs. Adapter units are intentionally separate;
these suite, assertion, scenario and fuzz counts must not be summed. IOCP and
kqueue remain named nonlocal suites rather than local exclusions. All historical
attempts, including failed attempt-17 and superseded attempt-18, remain unchanged.

Exact invocation and environment are in
`build/task185-final-19-launch/next-inventory-invocation.json`; the launcher,
exit status, final evidence summary and pre-tag rejection log are retained beside
it. `--check-tag-ready` still rejects missing platform evidence after successfully
verifying local artifacts. Later reporting edits are outside the frozen source
snapshot. Preserve the manifest, retained stage copies, referenced installed
consumer directories and pinned tool artifacts before worktree cleanup: receipt
paths are absolute.

Run the fixed inventory; gate selection cannot reduce required coverage:

```sh
PATH=/usr/bin:/bin python3 scripts/run-v3-release-gates.py \
  --output "$PWD/build/task185-release-01" \
  --provider-prefix /tmp/task182-provider \
  --compiler /usr/bin/clang++ --cc /usr/bin/clang --make /usr/bin/make \
  --pkg-config-dir /usr/lib/x86_64-linux-gnu/pkgconfig --jobs 4 \
  --python /selected/pinned-python --node /selected/node24 \
  --h2spec /selected/h2spec --go /selected/go --tshark /selected/tshark \
  --runner-python /selected/runner-python --runner-dir /selected/quic-runner \
  --server-image libhttpserver-task185-endpoint --isolated-public-network
```

Each attempt requires a new output directory. Missing tools and failed commands
produce unsuccessful gates, preserving logs, argv, environments, tool versions,
artifact hashes and earlier results. Sanitizers use fresh extracted-source
builds; providers and clients remain uninstrumented. Required ASan/UBSan lanes
enable LeakSanitizer, stack-use-after-return, stack-use-after-scope and
nonrecovering UBSan. Execute outside the ptrace sandbox, as in TASK-186/200.
Disabling required leak or stack checks fails the stage. TSan reuses
`test/tsan.supp`; only the existing WebSocket allocation fault injection and
timer-allocation reclaim hook are omitted because TSan owns operator new.
Public package/adapter execution can use `--isolated-public-network` to create
a loopback-only Linux user/network namespace for authentic ACME TCP443.
Bootstrap/distribution preparation stays outside that namespace: Automake
preserves root-owned system macro metadata when making the distribution.
The two excluded case names and reasons are checked and normal/ASan coverage
remains mandatory. Nonlocal backend suites are recorded
by name, separate from executed local cases.

The existing source-only README/release-note/Doxygen checks require a Git
checkout with specifications. A verified specifications bundle supplied to an
archive for parity does not turn it into a checkout. Packaged `make check`
retains all native, header, example, lint, installation and hygiene checks;
the documentation checks retain their documented source-checkout scope.

The release inventory requests `--optimized` from the installed-package builder,
selecting `CXXFLAGS=-O3 -g` for the production libraries used by protocol and
performance checks. The ordinary installed-consumer invocation retains its
existing default. Benchmark acceptance still requires optimized normal-allocator
artifacts and the full measured workload.

The external-readiness test uses target-local linker wrapping on Linux/FreeBSD
to inject allocation failures before delegating successful allocations to the
real allocator. This preserves sanitizer allocator ownership. All five
allocation-failure cases remain active under TSan, with no additional exclusions.
The supporting normal, ASan/UBSan and TSan probes each pass 28 tests/497 checks;
full configured release proof remains separate from these probes.

Simulator-clock verification binds the selected retained receipt path after
checking its artifacts and real immutable image. The producer worktree may
already be removed. Reopening the selected path must reproduce the same
validated configuration; the historical receipt bytes remain unchanged.

The `artifact-identity.json` distinguishes packaged files from the same-snapshot
specification/documentation bundle needed by historical runners. Archive runners
verify both identities instead of manufacturing Git metadata. Normal private
fixtures remain supporting evidence. Shared libraries and every native archive
member must match installed production artifacts; installed consumers reject
source headers and undeclared dependencies.

Before creating a release tag, verify the complete receipt set:

```sh
python3 scripts/run-v3-release-gates.py \
  --check-tag-ready "$PWD/build/task185-release-01/manifest.json"
# Or: make check-v3-tag-ready RELEASE_GATE_MANIFEST=/absolute/manifest.json
```

This command fails closed on missing/failed required local gates, altered
artifacts, unapproved exclusions or pending platform receipts. The release
workflow starts after a tag exists; it does not itself enforce this pre-tag
prerequisite. Tag creation remains a separate release-owner action.

The final continuation selects explicit pinned clients for nested package
checks, carries the verified simulator-clock compatibility receipt into the H3
aggregate, and uses the full benchmark defaults: 4096 measured requests,
256 requested warmup requests, protocol-specific epochs, 1024-byte payloads,
four workers, H1/H2 concurrency eight, and H3 concurrency four. Reduced runs
cannot satisfy the independent benchmark validator.

The public and benchmark envelopes also freeze the adapter catalogs, including
consumer executables, link maps and selected client modules retained outside
the stage directory. Nested JSON hashes alone do not replace those bytes.

After the final stage, local completion independently rehashes every retained
gate envelope, source/archive/provider and selected executable even while
platform evidence is pending. Expanded package audits have a bounded 64 MiB
JSON limit; the TLS-on audit now exceeds the former 32 MiB bound.

A packaged H1 runner contract copied a miniature source fixture while inheriting
the enclosing archive identity. Its isolated fixture now removes that unrelated
identity; real release invocations still require and verify the explicit archive
identity. The observed 1-versus-7 exit regression and corrected execution are
retained separately.

The fresh source incorporates the completed TASK-200 dependency (`v3` at
`940b301dbdad2adf924e8b6981eefa983414365f`). Historical attempts remain unchanged.
The complete continuation inventory is retained in `build/task185-final-17`.
Seventeen mandatory local gates pass; the full native throughput/latency gate
fails on an actual H3 wire error. Local acceptance remains unsuccessful.
FreeBSD/macOS/Windows receipts remain pending and CI/v3-PR owned.

## H3 response recovery (2026-10-14)

The implement-step recovery reproduced the response truncation deterministically.
The wire owner checked the body queue while empty, then a native worker published
both payload and completion, and the owner observed completion and retained FIN
without the payload. The same schedule could emit trailers ahead of the body.
`stream_output` now observes completion before queue size: after completion is
published no producer can append, so FIN/trailers require a subsequently empty
queue. A false completion snapshot defers terminal output to the next owner turn.

The native H3 regression enumerates mutex-release seams while a suspended handler
publishes a 1024-byte response, with and without trailers. Target-local linker
wrapping on Linux/FreeBSD forces the schedule without a production hook or sleeps.
The unchanged sender fails twice with an empty body; the repaired sender passes
all 14 tests/302 checks. The 11 affected native H3/QUIC suites pass 161 tests/2,601
checks. Focused ASan/UBSan (including leak, stack-after-return and scope checks)
and TSan runs also pass the 14-test regression suite with the existing sanitizer
policies. These focused sanitizer binaries combine the repaired response object
with the retained instrumented core archives; they are supporting repair evidence,
not a replacement for fresh complete release lanes. Benchmark contracts pass 18
tests. Evidence is retained separately under `build/task185-h3-repair`.

The historical `task185-final-17` manifest remains unsuccessful and unchanged.
The full mandatory inventory must run again on a fresh archive through the
retried implement step. The task remains In Progress; validation, commits and
finalization remain runner-owned. Nonlocal checks remain CI/v3-PR owned.

## Entropy gate recovery (2026-10-14)

The fresh `task185-final-18` package matrix passed, then its TLS-off broad
check caught a statistical test defect: a one-byte OS draw legitimately equals
zero with probability 1/256. The entropy seam deliberately rejects all-zero
draws, while the old length test unconditionally expected success. A controlled
`getrandom` wrapper reproduces that failure deterministically without changing
the production entropy implementation. The repaired test checks the one-byte
outcome against the actual byte and verifies both adjacent guard bytes. It
retains the larger successful-fill, distinct-draw and batch checks.

The controlled regression changes from four tests/18 checks with one failure to
five tests/21 checks passing; the real OS draw also passes. Evidence is retained
in `build/task185-h3-repair/entropy-repair-summary.json`. Attempt-18 and its failed
native transcript remain unchanged. Its later execution was interrupted before
starting the new complete `build/task185-final-19` inventory; no attempt-18
receipt is promoted to acceptance for that new source archive.

## Historical final continuation evidence and blocker

The fresh optimized archive SHA-256 is
`d3eced16925dd3dba316b20f1773d2c0bb8a16658a8b1f5de48a75c18f09e96e`;
source snapshot SHA-256 is
`c70cdcf335f9aa6d37ac736a80961bfc94464ed0628d21d5101b5a8bec014cbc`.
The completed unsuccessful manifest SHA-256 is
`cfd53b4ad07099b3ff18578b6242cedcc52ad5b27c5743002a6577836c60912e`.
`build/task185-resume-evidence/final-17-summary.json` summarizes its exact results.
Reporting edits made after archive creation are not executable acceptance inputs.

| Mandatory local gate | Observed result |
| --- | --- |
| package | Passed: 848 tests, 14,913 checks; TLS-off/on installed shared/static and managed/external matrix. |
| cxx20-off / cxx20-on | Passed: 229 / 260 registered Automake suites. |
| http1-websocket | Passed: 539 recorded scenario/check units. |
| http2 | Passed: 155 recorded scenario/check units. |
| tls | Passed: 167 tests, 4,274 checks. |
| parity | Passed: 168 parity witnesses. |
| resource-plateaus | Passed: 44 tests, 871 checks. |
| resource-slow-peers | Passed: 12 real slow-peer/control windows. |
| asan-ubsan-off / asan-ubsan-on | Passed: 229 / 260 registered suites, leak and stack checks enabled. |
| tsan-off / tsan-on | Passed: 142 tests/7,775 checks and 167 tests/8,247 checks; no new exclusions. |
| http3-full | Passed: all nine required native, transport/capture, independent-client, WebSocket and instrumented-fuzz subgates. |
| fuzz-off / fuzz-on | Passed: 20,000 / 2,000 completed bounded fuzz runs. |
| public-capabilities | Passed: all 32 installed-package capability cells. |
| native-throughput-latency | Failed: four TLS-off and eleven TLS-on cells completed; TLS-on static-managed H3 encountered malformed response framing. |

These counts use each adapter's own units; suite, native assertion, scenario,
subgate and fuzz-run counts must not be summed as one native assertion total.
The kqueue and IOCP suites are explicitly nonlocal rather than local skips.

The benchmark failure is retained in
`native-throughput-latency/tls-yes/benchmark/static-managed-h3/samples.json`
and `server.json`, with exact commands and tracebacks in the parent logs.
Pinned aioquic 1.3.0 reported `H3_MESSAGE_ERROR` 270:
`content-length does not match data size`. It occurred during warmup after
407 completed connection epochs, 1,628 measured responses and 1,628 warmup
responses, before the required 4,096 measured requests completed. The runner
correctly retains `local_passed=false` and `tag_ready=false`. The child failure
then terminates the consumer; its recorded exit -9 is teardown after the wire
failure, not an independent server-crash diagnosis. No workload reduction,
retry or successful aggregate is substituted for this observation.

The blocked dependency is native H3 response/QUIC lifecycle correctness under
the full installed benchmark. The caller must assign causal repair ownership
across the TASK-195/196/198 component lineage. A repaired source must pass the
complete mandatory inventory before local acceptance can close. This task
remains In Progress; later validation, commits and finalization are caller-owned.

Exact reproduction inputs are retained in
`build/task185-tool-recovery-15/final-17-invocation.json` and the task-local
`run-final-17.py` launcher. Tool receipts identify Clang 18.1.3, GNU Make 4.3,
Python 3.12.3, Node 24.15.0, h2spec 2.6.0, Go 1.25.1, Docker 29.1.3,
TShark 4.6.4, Autoconf 2.71, Automake 1.16.5, Libtool 2.4.7, OpenSSL 3.5.9
for the selected provider, h2 4.4.1, aioquic 1.3.0 and websockets 15.0.1.
All selected executables, provider bytes, libraries, headers, logs, commands,
image identities and adapter catalogs are retained with hashes. Preserve the
attempt, retained stage copies, external consumer directories and selected
tool artifacts before any worktree cleanup; their receipt paths are absolute.

The four setup repairs have observed RED/GREEN evidence in
`build/task185-tool-recovery-15`: documentation-bundle scope, optimized package
selection, simulator receipt retention and sanitizer allocator ownership.
Focused contracts pass: release runner 44, installed consumer 29, QUIC runner
36 (four existing optional retained-capture fixture skips), full H3 runner 27.
The external-readiness normal, ASan/UBSan and TSan probes each pass all 28 tests
and 497 checks, including all five allocation-failure cases. The fresh full
sanitizer inventory independently confirms their configured integration.

## Historical implementation and unsuccessful attempts

`build/task185-final-16` is a complete unsuccessful attempt with all 18 local
stage outcomes retained (nine passed, nine failed). Its manifest SHA-256 is
`d17f8912d8c6721bd0d34433bf60cc7473619d1f04cb62f60d50b5038f9e454d`.
It proves the installed public capability adapters and bounded fuzz executions
ran successfully. Remaining setup failures were the tooling-bundle documentation
guard, TSan allocator replacement, historical simulator receipt location, and
debug production-library selection for benchmarks. Each repair has focused
failing-then-passing evidence. It remains unsuccessful and supplies no complete
release acceptance claim for the repaired source.

The following evidence describes earlier snapshots and their actual failures;
it does not establish acceptance for the repaired source.

Validation recovery identified an integrity failure in the historical
`task185-release-05` package envelope: subsequent native builds changed configured
dependency files that the envelope had already hashed. Its original manifest
and hashes remain untouched. The reported package pass is historical execution
evidence, but that envelope no longer passes artifact verification and cannot
satisfy release readiness.

The repaired runner retains independent byte copies under `retained/<stage>`
before proceeding to another stage. Receipt artifact paths and command log paths
refer to these copies; `retained_paths` and each command's `execution_log` preserve
the mapping to the executed paths. Copying checks the original size and SHA-256,
and any retention failure makes the stage unsuccessful. Configured build trees
remain available for later gates without invalidating earlier evidence. A
regression completes a package stage, runs a subsequent native build/check that
changes its dependency file, and re-verifies the original package receipt; it
also confirms that altering a retained copy fails verification.

The pending-platform regression now begins with a complete verified manifest,
checks its successful positive control, and then requires the platform-specific
failure for missing and pending platform rows. Neither this repair nor focused
validation changes the mandatory inventory or supplies missing public integration
and benchmark capabilities. The all-local-gates criterion remains unsuccessful.

Recovery execution is retained in `build/task185-recovery-summary.json`:

The fresh repair-session recheck is retained separately in
`build/task185-recovery-recheck-03/summary.json`. Its 32 runner and seven archive
contracts pass, and the immutable package envelope re-verifies. The installed
TLS-on static consumer using OpenSSL 3.5.9 still emits `not_supported unbound`
for public TLS, HTTP/2 and HTTP/3 listener configurations. The relevant server
implementation, public server header and native test inventory match the verified
package source byte-for-byte. Repeating the release readiness check still fails
on unsuccessful mandatory local gates. No historical receipt was rewritten.
This confirms the missing product/benchmark dependencies; setup repair alone
cannot supply the capabilities explicitly excluded by the selected task plan.

| Recovery evidence | Verified result |
| --- | --- |
| `build/task185-recovery-01` | Fresh packaged TLS-off/on matrix: 830 tests, 14,677 checks; normal native resource stage: 44 tests, 871 checks. |
| Retained package envelope after native builds/checks | Verification passes despite 19 changed files in the working package build trees. |
| `build/task185-recovery-focused-02` | Final packaged runner/archive contracts: 32 + 7 tests; two registered Automake script suites pass. |

The first package probe predates the final conflicting-record regression; the
focused distribution includes that final guard. They are separate snapshots,
neither a complete passing release inventory. Initial registration probes
mistakenly triggered native-program prerequisites; their failure logs remain
retained. The corrected focused registration command is
`make -C <build>/test check-TESTS check_PROGRAMS= 'TESTS=integ/release_gate_runner_test.py integ/release_artifact_test.py'`.
Working-tree release/archive contracts also pass all 39 tests; affected H3,
installed-consumer and package-workflow contracts pass 26, 24 and four tests.
Warning-suppression lint, fence contracts, Python compilation and diff whitespace
checks pass. The existing 509-SLOC backend file-size failure is unchanged.

The selected task plan explicitly excludes implementation of missing public
protocol capabilities and native benchmarks. The remaining mandatory acceptance
failure requires a scope decision or delivery by those dependency owners. The
repair leaves TASK-185 In Progress and preserves all required gates, failed
historical receipts, and CI ownership of nonlocal checks. No commit, merge, tag,
push or deployment is performed in the recovery session.

The task-local Linux execution selects the static OpenSSL 3.5.9 provider at
`/home/etr/progs/libhttpserver/build/tooling/provider`, allowing the existing
QUIC endpoint image to carry the same provider code in its executable. The
installed package matrix still tests both shared and static libhttpserver
linkage. The separate shared-provider package attempt is retained independently.

Each native Automake test has a 180-second deadline through the selected GNU
`timeout` tool, with a five-second kill grace. Build commands retain their own
longer deadlines. A timed-out native test fails its gate; other native tests and
independent release stages continue. An existing Linux `peer_policy` test stalled
in `concurrent_mutation_and_reads`. Its six unbounded readers and two mutators
suggest reader starvation; the retained log alone does not establish the cause.
Its first eight cases passed in that log.
That product/test dependency is owned by peer-policy maintainers through
ownership triage; this task retains the failure rather than changing the test
or library policy.

`--tool-artifact PATH` binds wrapper runtime dependencies and capture libraries
to the receipt in addition to the selected tool executable hashes. The local
capture runtime comes from the existing `task163-runner-tools` Docker image;
its loader/libraries and Compose executable are declared artifacts. Go 1.25.1
and Node 24.15.0 downloads were checked against official checksum metadata.
Python packages, h2spec's exact build identity and tool versions are retained in
the command logs. Tool provisioning is a separate task-local setup step; the
release runner itself never downloads tools or modifies host package policy.

When CI platform evidence returns, retain its referenced artifact files and
supply matching platform envelopes to the verification command:

```sh
python3 scripts/run-v3-release-gates.py --check-tag-ready /absolute/manifest.json \
  --platform-receipt /evidence/freebsd.json \
  --platform-receipt /evidence/macos.json \
  --platform-receipt /evidence/windows.json
```

Platform identities must match the same archive, source snapshot and extracted
file set. Importing platform evidence preserves the original local receipt.
Missing, duplicate, altered, failed or differently sourced evidence fails.

| Attempt | Retained outcome |
| --- | --- |
| `build/task185-release-01` | Preparation rejected the existing `AC_CONFIG_FILES` fixture's terminal newline; the precise transformation now has a RED/GREEN contract and retained source/package hashes. |
| `build/task185-release-02` | Preparation passed; stage verification rejected the missing process-global identity variable. The corrected adapter accepts an explicit verified identity path, covered by RED/GREEN contracts. |
| `build/task185-release-03` | Shared-provider installed matrix passed. The full native check stalled in `peer_policy`; the superseded attempt was interrupted, preserving its snapshots, test logs and command records. |
| `build/task185-release-04` | Superseded during package execution after adding the tested native-test deadline. Logs and the exact source/archive identities remain retained. |
| `build/task185-release-05` | Complete inventory execution with pinned clients, static provider, capture runtime and per-test deadlines; final results are recorded below. |

The frozen full attempt was retained when real stdout formats exposed receipt
adapter defects: normal allocator windows and the private rotation example do
not emit Littletest summaries, Automake aligns its counts, and H2/H3 resume
probes use different payload sizes. Each correction has a failing-then-passing
contract. The current runner also copies native `.log`/`.trs` details into stage
evidence on failure. H1 archive logging now verifies the supplied archive
identity rather than discovering Git metadata from an enclosing checkout.

These corrections do not promote the original failed aggregate. Supplementary
receipts verify the unchanged attempt-05 archive, source, executables and
installed archive members; their exact adapter copies/hashes are retained.
`build/task185-adapter-audit-05-v2/receipt.json` rechecks the successful TLS and
slow-peer executions. `build/task185-supplement-05/manifest.json` contains fresh
normal allocator and installed-example probes. Independent WebSocket execution
still fails in that supplement.

The fresh `build/task185-current-08` distribution includes the normalizer's
previously omitted `data/normalize_body_fixture`. The existing packaged test
first failed its body-file expectation; with the fixture distributed it passes
11 tests/30 checks. Its packaged release contracts pass 28 runner tests and seven
archive tests. Final working-tree contracts also pass all 35 tests. Cosmetic
refactoring and subsequent reporting edits are separate from each recorded
archive snapshot; historical receipts cannot satisfy a different archive.

| Supporting evidence | Exact successful counts |
| --- | --- |
| TLS-off/on installed shared/static package matrix | 830 tests, 14,677 checks |
| Independent H2/h2spec receipt | 155 successful cases; pinned exclusion inventory retained separately |
| Private TLS/policy/ACME/PSK/QUIC/rotation executions, corrected adapter | 155 tests, 4,085 checks |
| Normal allocator H1/WS CPU/RSS windows and resource reservations | 44 tests, 871 checks |
| Normal allocator H2/H3 slow peers, uploads, cancellation and resume | 12 measured idle windows; three download and three upload cycles per protocol |
| Installed examples outside the failed full native check | Four TLS-off and four TLS-on compilations |
| Packaged normalizer fixture regression | 11 tests, 30 checks |
| H3 TLS-off native and ASan/UBSan derivatives | 48 suites per derivative: 431 Littletest tests/12,270 checks plus 110 named corpus cases |
| H3 TLS-on native and ASan/UBSan derivatives | 55 suites per derivative: 497 Littletest tests/13,501 checks plus 110 named corpus cases |
| Independent QUIC transport matrix | 12 successful peer/case cells, 36 validated captures and two authenticated closure witnesses |
| Independent RFC 9220 WebSocket supplement | 11 successful client cases |

The normal TLS-off broad check reports 215 suites: 209 pass, four fail and two
nonlocal backend skips. The failures are the missing packaged normalizer fixture
(fixed in the later distribution), the `peer_policy` deadline, six failing H2
engine checks, and the raw WebSocket handshake client. The independent
WebSocket lane also times out waiting for the `/flood` handshake. The parity
inventory requests `cookie_render`, which has no target in the current v3
Makefile; its missing per-case evidence remains unsuccessful.

The fresh ASan/UBSan TLS-off check reports 215 suites: 207 pass, six fail and two
nonlocal backend skips. Besides normalization, H2 and WebSocket failures, ASan
reports stack-use-after-scope in `consumer_v3_concurrency`
(`resume_signal.hpp:393`), `response_sources` (`body_sink_fake.hpp:121`) and
`forms_multipart_route` (`forms_multipart.cpp:276`). These retained reports
require owner triage; this release task does not change their library/test code.

| Unsuccessful or pending dependency | Owner / triage action |
| --- | --- |
| Native H2 route-effect conformance failures | H2 engine/corpus maintainers; investigate the six recorded checks |
| Raw and independent H1/WebSocket handshake failures | Native listener/WebSocket maintainers; investigate retained EOF/timeout traces |
| Normal peer-policy deadline | Peer-policy maintainers; investigate the stalled concurrency case |
| ASan stack lifetime failures | Concurrency, response-source and multipart maintainers; investigate exact ASan reports |
| TSan owner-suite build conflicts | Concurrency test maintainers; make `task_resume_signal_lifetime` respect the sanitizer-owned allocation convention |
| Independent H3 semantic/drain deadline | H3 lifecycle/interop maintainers; investigate quic-go fixture `drain deadline expired` and retained shutdown traffic event |
| Missing `cookie_render` parity target | Parity inventory/v3 cookie maintainers; supply the required current-v3 witness |
| Public TLS/H2/H3 listeners and credential installation | v3 public integration owner; provide installed public witnesses |
| Native throughput/latency benchmark | v3 performance owner; provide the native benchmark dependency |
| FreeBSD, macOS, Windows and nonlocal backends | CI / v3 PR, per repository policy |
| LeakSanitizer execution | Applicable sanitizer/CI owner; local leak detection remains unexecuted |

The acceptance criterion requiring every v3.0 gate to pass remains unmet. The
C++20 package builds and focused implementation checks pass. Full release/tag
readiness remains false; no tag or publication is performed by this runner.

The normal TLS-on broad check reports 243 suites: 237 pass, four fail and two
nonlocal backend skips. The ASan/UBSan TLS-on check reports 243 suites: 235 pass,
six fail and two nonlocal backend skips. Their failed suites match the respective
TLS-off inventories. Both TSan builds fail at `task_resume_signal_lifetime`:
its global `operator new/delete` definitions conflict with the TSan runtime.
No executed TSan owner-suite pass or allocation-exception coverage is claimed.

The C++20 configuration adapter also needed to recognize Autoconf's actual
`CXX = clang++ -std=c++20` value when `CXXFLAGS` contains optimization/debug
flags. Its RED/GREEN regression retains compiler, Automake and user flags,
still rejects missing standards and uninstrumented sanitizer configurations,
and passes 26 existing/new full-H3 contracts. The fresh
`build/task185-h3-supplement-05` execution uses exact copied adapter files from
`build/task185-h3-supplement-adapter` over the verified attempt-05 source/builds;
it does not replace the failed frozen aggregate.
All four native derivatives and the QUIC transport matrix pass in that
supplement. The aioquic semantic client completes, but the quic-go fixture
reports `drain deadline expired`; the unchanged runner then rejects its shutdown
`traffic` event while waiting for required semantic completion. Its H3 aggregate
fails before the later WebSocket/fuzz stages. The independent
`build/task185-h3-ws-supplement-05` lane executes the same required RFC 9220
predicate successfully instead of treating the skipped aggregate stage as a
pass. The outer release inventory executes its separate instrumented fuzz lanes.

The complete attempt-05 archive SHA-256 is
`da82a11660bc5c2444df89d9023e4eb9c99948ca33b6280213d7b033d65a6668`;
its source snapshot SHA-256 is
`6a0bcd677c13ec2b799c9124f4a79e17bf625a36c0c53d9134caa080389dfeee`.
The current-08 focused archive SHA-256 is
`4d804a577ac377796fa54e82693c8d544363b265d00c4a826e5673703ac0ee3c`;
its source snapshot SHA-256 is
`c61e94913bb1347471840a730625a5b4eeb98e8e5967df8149b41febbbf5c7e7`.
Later documentation/tracking edits are outside those frozen snapshots.

| Recorded local tool | Version |
| --- | --- |
| Clang C/C++ | 18.1.3 |
| GNU Make / Autoconf / Automake / Libtool | 4.3 / 2.71 / 1.16.5 / 2.4.7 |
| Python / h2 / aioquic / websockets | 3.12.3 / 4.4.1 / 1.3.0 / 15.0.1 |
| Node / Go / quic-go | 24.15.0 / 1.25.1 / v0.55.0 |
| h2spec | 2.6.0, build `70ac2294010887f48b18e2d64f5cccd48421fad1` |
| Docker / Compose | 29.1.3 / 2.40.3 |
| tshark / editcap | 4.6.4 / 4.6.4 |
| Static OpenSSL provider | 3.5.9 |
| pkg-config / GNU timeout / Binutils / glibc | 1.8.1 / 9.4 / 2.42 / 2.39 |
| Upstream QUIC runner | `740c05a10b61d65e8abd3ad38d60898004d335d9` |

`/tmp/task185-final-run.py` retains the exact full-inventory launcher and
`/tmp/task185-h3-supplement.py` retains the bounded full-H3 supplement launcher.
Tool versions, binary hashes, environments and runtime closures are additionally
retained in the manifests and command logs. The installed-client and H3 receipt
contracts pass 24 and 26 tests respectively; the release contracts pass 35.
Python compilation, shell syntax, warning-suppression lint, fence contracts and
`git diff --check` pass. File-size lint reports the unchanged
`src/detail/io_poll_backend.cpp` baseline at 509 SLOC against its 500-SLOC limit;
this task does not change that file.

The completed frozen inventory retains all 18 stage outcomes:

| Required stage | Frozen outcome | Duration (seconds) | Receipt detail |
| --- | --- | ---: | --- |
| `package` | passed | 849.243 | 830 tests/14677 checks |
| `cxx20-off` | failed | 432.951 | command exited 2 |
| `cxx20-on` | failed | 477.711 | command exited 2 |
| `http1-websocket` | failed | 23.208 | command exited 1 |
| `http2` | passed | 28.945 | 155 tests/155 checks |
| `tls` | failed | 12.226 | missing/duplicate native tests |
| `parity` | failed | 1.207 | command exited 1 |
| `resource-plateaus` | failed | 13.068 | missing/duplicate native tests |
| `resource-slow-peers` | failed | 30.821 | incomplete resource saturation/cancellation scenarios |
| `asan-ubsan-off` | failed | 974.129 | command exited 2 |
| `asan-ubsan-on` | failed | 1242.555 | command exited 2 |
| `tsan-off` | failed | 185.785 | command exited 2 |
| `tsan-on` | failed | 230.377 | command exited 2 |
| `http3-full` | failed | 0.323 | command exited 1 |
| `fuzz-off` | passed | 1543.504 | 20000 tests/20000 checks |
| `fuzz-on` | passed | 329.997 | 2000 tests/2000 checks |
| `public-capabilities` | failed | 0.215 | Public installed TLS/H2/H3 listeners return not_supported; public credentials lack installation witness. Separate product integration dependency. |
| `native-throughput-latency` | failed | 0.209 | No native v3 throughput/latency runner exists; test/bench_*.cpp uses historical v2 APIs. Performance owner must provide the required native benchmark dependency. |

The frozen aggregate has `local_passed=false`, `tag_ready=false` and status `failed`.
Its manifest SHA-256 is `fc77a86fcca57b4ae4672e27f5bcc2861e090c64b50de96cf610c477d797e2ba`.
A real `--check-tag-ready` invocation rejects it with `required local release gate missing or unsuccessful`.
Corrected supporting adapters and later packaged fixture checks are listed above; this original aggregate remains unsuccessful.

Raw normal/ASan and TSan build diagnostics, including nested test logs, are
copied and hashed in `build/task185-final-native-audit-05/receipt.json` (1,834 files).
The fresh current-08 Automake `check-TESTS` registration probe executes both new
dependency-free suites successfully (two suite entries, 35 unit tests). It is
a focused registration check, separate from the unsuccessful broad inventories.

| Instrumented target | Actual completed runs |
| --- | ---: |
| `fuzz-off/hpack` | 2000 |
| `fuzz-off/http1_body` | 2000 |
| `fuzz-off/http1_head` | 2000 |
| `fuzz-off/http2_engine` | 2000 |
| `fuzz-off/http3_engine` | 2000 |
| `fuzz-off/qpack` | 2000 |
| `fuzz-off/quic_parser` | 2000 |
| `fuzz-off/quic_recovery` | 2000 |
| `fuzz-off/quic_state` | 2000 |
| `fuzz-off/websocket_codec` | 2000 |
| `fuzz-on/quic_tls_callbacks` | 2000 |

These derivatives compile protocol sources with libFuzzer, ASan and
nonrecovering UBSan; each uses 2,000 requested runs, 30 seconds, five-second
per-input timeout, 512 MiB RSS and seed 128. Providers/clients remain outside
the instrumentation boundary and LeakSanitizer remains unexecuted.
