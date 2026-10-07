# TASK-127 implementation evidence

The registered worktree is `/Users/etr/progs/libhttpserver/.worktrees/TASK-127`, branch `task/TASK-127`, based on `v3` at `975034debaba3c015f7f40cf85450856cb168d47`. The implementation phase leaves changes unstaged for the Groundwork runner and keeps task status In Progress.

## Implementation

`io_kqueue_backend` owns a close-on-exec kqueue and a coalesced wake socket. Persistent read/write filters use `EV_ADD | EV_DISPATCH` with subsequent `EV_ENABLE`, without `EV_CLEAR` on sockets. Only directions with pending work are enabled; bounded reads/accepts leave excess availability in the kernel. Read EOF and socket error hints do not discard unread bytes. Positive reads complete before a subsequent recv reports terminal EOF; write failure closes the write direction while reads remain available.

A separate zero-timeout `EV_RECEIPT` control phase allocates one receipt per change and checks identity, filter, flags and data before updating driver state. Successful `EV_ERROR/data=0` is accepted. Fatal control/wait failures resolve all pending work once. Wait EINTR recomputes monotonic remaining time; an interrupted control batch fails closed rather than blindly replaying ambiguous changes. The private syscall seam uses actual syscalls unless a focused fault case explicitly substitutes it.

`io_managed_socket_backend` extracts epoll's managed operation registry and socket steps, avoiding duplicate lifecycle code. The registry mutex leases caller buffers and sockets throughout nonblocking syscalls. Selected operations remain in the registry, completion handlers run outside the lock, and monotonic incarnation tokens reject stale work. Tokens cannot wrap; closing/releasing invalidates an incarnation before fd reuse. `close()` joins the driver before owner/executor teardown, with driver-local shutdown handling retained. Independent poll remains the shared oracle. The managed factory preserves Linux epoll selection and external poll selection, and chooses kqueue only on macOS/FreeBSD.

## Local proof

Host: Darwin 25.3.0 arm64; Apple clang 21.0.0; C++20. Autotools bootstrap/configure/library build passed in `/private/tmp/task127-autotools-build`, with Doxygen enabled. Configuration used `CXX='clang++ -std=c++20'`, `CPPFLAGS=-I/opt/homebrew/include`, and transitional `LDFLAGS='-L/opt/homebrew/lib -lgnutls'`.

The focused test gate passed 11/11 executables, zero skips/failures/errors: `io_backend_contract`, `io_kqueue_backend`, `io_operation`, `io_connection_owner`, `fake_io_backend`, `connection_engine`, `drain_scope`, `native_server`, `native_http1_e2e`, `v3_header_hygiene`, and `v3_native_linkage`. These contain 245 littletest cases; the linkage smoke target has its own exit-code oracle. The kqueue suite executes 48 cases, including all applicable shared scenarios, queued/new reads after buffered EOF, independent read/write progress and cancellation, latent accepts/reads, backpressure, stale tokens/fds/filters, gated in-flight buffer/socket leases, concurrent close, fatal/mixed/error receipts, control/wait EINTR, wakes, timers, and disabled EOF iteration bounds.

Real receipt observations from the normal kqueue log: buffered EOF with pending bytes occurred 6 times across 7 socket events, with 8 successful controls; invalid registration produced one nonzero EV_ERROR receipt. A public native-server HTTP request completed byte-exactly and generated 18 kqueue waits and 3 socket events. Fresh poll/kqueue fixtures produced identical 17-entry normalized semantic traces and identical 262144-byte backpressure streams, including actual initial partial writes. Scheduler-dependent transfer boundaries are compared by complete byte stream and ordering, not chunk size.

The complete native-core source list was compiled into separate ASan/UBSan and TSan archives. Four consumers passed each instrumented lane: kqueue (48 cases), shared fake/poll contract (42), native server (17), and connection engine (36). ASan leak detection is unsupported by this Apple runtime; ASan/UBSan memory/undefined-behavior checks passed without requesting that unsupported capability. TSan reported no races.

Changed-file cpplint, file-size, public-header gates, warning-suppression, assertion, fence checks, and native source/binary linkage audit passed. The transitional environment lists GnuTLS in the native linkage binary but resolves no symbols from it; the audit verifies only platform/C++ runtime symbols are used. Repository complexity and duplication retain exactly their unchanged HEAD-baseline findings (three complexity offenders and two duplicate blocks); new/modified managed backend code introduces no findings.

## TDD and retained receipts

Receipts and task-local build scripts live under `/private/tmp/task127-receipts`, with auxiliary scripts/builds under `/private/tmp/task127-*`. `source-sha256.txt` binds all changed/new source, fixture, build and task evidence files; `receipt-sha256.txt` inventories logs. `source-state.txt` records worktree identity, HEAD, branch and platform. Nothing was staged or committed by the executor.

- `red-missing-backend.log`: the initial shared/socket kqueue tests failed because the backend did not exist; `kqueue-first-green.log` passed 37 cases after implementation.
- `red-managed-observation.log` and `red-inflight-lease.log`: required private observation/syscall lease seams were absent before their implementation; succeeding real-socket suites retained their green receipts.
- `red-stale-writability.log`: three expected runtime failures showed that a selected would-block writer incorrectly completed zero bytes; the repair leaves that operation pending, with the final suite green.
- `baseline-unsandboxed.log`: unchanged native fake/poll baseline passed 42 cases. The first sandboxed baseline could not bind loopback sockets and is retained separately, not classified as a production regression.
- `final-focused-tests.log`, normal per-target logs, `asan-*.log`, `tsan-*.log`, static gate logs and native linkage logs contain final results. Earlier configure/build harness problems (overlapping library relink, missing response-parser link input, old Bash empty-array handling) remain in their initial logs; corrected final receipts are the acceptance proof.

## Nonlocal coverage

Actual FreeBSD, Linux epoll after the shared registry extraction, Windows and other nonlocal platform execution was not performed. User AGENTS.md policy dated 2026-10-06 assigns these checks to CI and the v3 PR and explicitly makes missing nonlocal receipts nonblocking for local completion. No VM, service, remote endpoint or emulated kernel was provisioned. This evidence claims actual local macOS runtime proof only; it does not claim FreeBSD runtime acceptance or a full-repository validation run. Formal validation and finalization belong to the caller/runner.
