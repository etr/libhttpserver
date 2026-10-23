# FEATURE-ci-iocp-close

Status: Complete (local implementation, Groundwork validation and finalization).
Native Windows results remain CI / v3 PR owned; no release readiness claimed.

Scope: repair the IOCP socket retirement call rejected by the Windows compiler.
Base: v3 at `b5cd6cf06e39a1b0727c2614e5236ff00a59dbf1`.

- [x] Create an isolated task worktree using the Groundwork identity helper.
- [x] Read the actual native Windows compile failure and verify its source is
      unchanged at the task base.
- [x] Pass the registration socket lvalue to the shared close helper, preserving
      handle invalidation, map erasure, cancellation and completion lease lifetime.
- [x] Retain the existing native UDP cancellation/reuse test without adding
      mirrored source-text or synthetic implementation tests.
- [x] Build and run the focused existing local socket helper suite; run cpplint
      on the changed production file and check the diff.
- [x] Retain commands, raw results, original failure and causal assessment on disk.
- [x] Caller: complete Groundwork validation and finalization.
      Three focused reviewers approved with no findings; matching evidence reused.
- [ ] CI / v3 PR: obtain fresh native Windows compile and IOCP runtime results.

## Failure evidence and causal assessment

The original native Windows package job `120361073253` on `808f7fa` failed at
`src/detail/io_iocp_backend.cpp:260`: the temporary returned by `std::exchange`
cannot bind to `close_socket(native_socket_t&)`. The full original log is
retained at `.debug/FEATURE-ci-iocp-close/windows-job-120361073253.log` in the
main repository; lines 751–763 contain the compiler diagnostic. A recorded
`git diff 808f7fa b5cd6cf -- src/detail/io_iocp_backend.cpp` is empty, tying that
source-bound RED to this task base. The accepted plan uses the real compile
failure as RED for this compile-only repair.

`close_socket` already copies the closing handle, sets the caller's reference
to `k_invalid_socket`, then closes the copied handle. Passing `record->socket`
directly honors that contract for both stream and datagram registrations. A
retained completion packet's eventual registration destructor sees the invalid
handle, so it cannot close a socket that reused the old value. The now redundant
datagram branch is removed; the lifetime comment remains. Connection map erasure
and `CancelIoEx` still precede the helper call, with the same mutex and packet
ownership. No other source or public interface changed.

## Local verification and limits

From the isolated task worktree, with `EVIDENCE` denoting the absolute main
repository directory `.debug/FEATURE-ci-iocp-close`:

```sh
g++ -std=c++20 -O0 -g -Wall -Wextra -pthread -DHTTPSERVER_COMPILATION \
  -Isrc -Itest test/unit/io_poll_sys_listener_test.cpp \
  src/detail/io_poll_sys.cpp src/net/address.cpp src/detail/net_address.cpp \
  -o "$EVIDENCE/io_poll_sys_listener"
timeout 30s "$EVIDENCE/io_poll_sys_listener"
cpplint src/detail/io_iocp_backend.cpp
git diff --check
```

All commands exited 0. The existing listener/helper suite executed 12 tests,
45 successful checks, no failures or skips. It compiles the actual shared socket
helper and uses real local sockets; it supplies local helper coverage, not
Windows IOCP proof. cpplint reported no errors. Exact expanded commands, working
directories, output and exit codes are retained in `build-listener.log`,
`run-listener.log`, `cpplint.log` and `diff-check.log` alongside `journal.md`.

The bounded local capability check found no Windows crosscompiler or SDK.
Native Windows compilation and
`udp_cancelled_packet_keeps_native_storage_and_reuse_generation` execution remain
incomplete until CI runs them. No substitute native pass is claimed. No broad
baseline or unrelated repairs were run; previous task evidence and the unrelated
bauth worktree remain unchanged.

## Implementation notes

General setup uses `./bootstrap`, an out-of-tree configure/build, and
`make check`; this task needed only the dependency-free focused compile above.
Tests use `test/littletest.hpp`, `LT_BEGIN_AUTO_TEST` and `LT_CHECK` assertions.
Private implementations live in `src/detail`, owning headers in
`src/httpserver/detail`; the socket helper takes a mutable lvalue reference and
invalidates before closing. Git metadata writes required sandbox escalation.
Task-local setup and test memory is retained in the evidence directory.
