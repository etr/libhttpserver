### TASK-138: Implement connection-owned HPACK dynamic tables

**Milestone:** M10 - TLS and HTTP/2
**Component:** HPACK
**Estimate:** L
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Provide connection-owned HPACK dynamic tables for libhttpserver v3.0.

**Action Items:**
- [x] Own encoder and decoder dynamic tables per HTTP/2 connection.
- [x] Handle capacity updates, eviction and never-indexed fields.
- [x] Enforce compressed and expanded field-section budgets.

**Dependencies:**
- Blocked by: TASK-137
- Blocks: TASK-140, TASK-145

**Acceptance Criteria:**
- Encoder/decoder tables evolve in wire order with correct eviction, limits and ordered duplicate fields.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-005, PRD-V3N-REQ-017, PRD-V3N-REQ-018
**Related Decisions:** DR-V3-001, DR-V3-006

**Status:** In Progress

**Implementation notes:**
- Private, noncopyable `hpack_connection` owns separate encoder and decoder
  tables charged against the same hierarchical connection budget. Tables own
  octets, index newest entries at 62, evict oldest entries, and retain each
  `hpack_table_bytes` reservation until eviction, failure, or destruction.
  Referenced names are owned before insertion can evict their source.
- Complete sections preserve ordered duplicate occurrences and never-indexed
  policy. Semantic materialization uses `append`; encoding semantic fields
  requires an explicit policy and traverses `entries()`. Relays can encode the
  decoded occurrence list without dropping its never-indexed metadata.
- Capacity defaults are 4096. Explicit settings-boundary methods distinguish
  the peer encoder maximum, chosen encoder capacity, and acknowledged decoder
  maximum. Pending encoder changes emit the smallest intervening capacity and
  final capacity, including empty blocks; decoder reductions require a leading
  shrink. Local memory limits are independent of these wire settings.
- Compressed bytes, expanded bytes (name + value + 32 for every occurrence), and
  field count have independent ceilings. Aggregate raw/Huffman lengths are
  inspected without allocation before strings are decoded; table reservation
  and semantic field-count refusal precede proportional copies/cache growth.
  Invalid policies/settings leave state unchanged. Complete-block truncation,
  malformed wire, or processing refusal publishes no fields/wire output and
  permanently disables that direction, releasing its table reservations.
- Calls require serialized connection execution in received/transmitted wire
  order. Successfully encoded blocks must be sent in that order or the caller
  must close the connection. Attaching the owner to the future HTTP/2 transport
  remains outside this task.
- Local verification: C++20 Autotools build with examples, docs, and native TLS
  disabled; eight focused/adjacent targets pass. Full serial `make check` passes
  239 programs/scripts, with the Linux epoll check skipped on macOS (240 total).
  Full header, native-linkage, documentation, staged-install, and hygiene gates
  pass. The first sandboxed full run could not bind/connect loopback sockets;
  it was stopped and the full suite passed with approved local network access.
- Four focused binaries pass ASan/UBSan. Whole-block Appendix C.2-C.6 replay
  checks independent ordered-field and table-snapshot oracles. The final seeded
  30-second bounded stateful libFuzzer smoke ran 1,697,050 inputs without findings;
  all 456 saved corpus inputs replayed. See `test/fuzz/README-hpack.md`.
- Changed-file strict warnings, cpplint, CCN <= 10, and repository file-size
  checks pass. Repository-wide complexity still reports the three unchanged
  baseline violations in `dispatch_request`, `valid_peer_pattern`, and
  `pollsys::accept_one`. BSD, Windows, and other nonlocal checks remain CI/v3 PR
  work. Independent validation, runner commits, integration, and cleanup are
  caller-owned; task status remains In Progress until that workflow finishes.
