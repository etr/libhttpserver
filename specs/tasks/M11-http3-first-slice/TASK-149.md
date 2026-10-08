### TASK-149: Add owned UDP send/receive operations and CID dispatch seam

**Milestone:** M11 - First HTTP/3 slice
**Component:** Private operation I/O
**Estimate:** M
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Provide owned UDP send/receive operations and CID dispatch seam for libhttpserver v3.0.

**Action Items:**
- [x] Add bounded UDP receive/send operations on supported backends.
- [x] Extract invariant header and route by destination CID.
- [x] Preserve packet and address metadata through connection-owner dispatch.

**Dependencies:**
- Blocked by: TASK-099, TASK-100
- Blocks: TASK-150, TASK-153

**Acceptance Criteria:**
- Datagrams route by destination connection ID through bounded per-connection queues on supported I/O backends.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-007, PRD-V3N-REQ-008
**Related Decisions:** DR-V3-004

**Status:** Complete


**Implementation verification (macOS arm64, 2026-10-07):**
- C++20 static native and transitional library builds pass with native TLS disabled; the UDP/CID seam remains private and independent of OpenSSL.
- Focused `make check`: 11 targets pass (`io_datagram`, `io_udp_backend`, `quic_invariant_header`, `quic_datagram_dispatch`, `io_operation`, `io_connection_owner`, `fake_io_backend`, `io_backend_contract`, `external_readiness_adapter`, `io_completion_storage`, `io_kqueue_backend`). Poll and native kqueue UDP contracts exercise IPv4/IPv6, whole-message ordering, empty datagrams, zero-capacity truncation, metadata, cancellation, release and external registration generations.
- AddressSanitizer/UndefinedBehaviorSanitizer runs pass for UDP backend and CID/owner dispatch targets, including the real UDP → CID → two-owner loopback path. The native linkage audit and changed-file cpplint pass.
- Linux epoll and Windows IOCP targets return platform skips locally. Windows overlapped UDP storage, cancellation/late packets, registration reuse and wildcard destination/interface metadata have guarded tests; execution belongs to CI and the v3 PR. FreeBSD and other nonlocal platforms are likewise delegated to CI under AGENTS.md.
- Implementation is ready for the caller's validation/finalization. Task status remains In Progress until those phases complete. QUIC handshake/transport state is outside this task.
