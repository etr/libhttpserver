### TASK-137: Implement bounded HPACK primitives and static tables

**Milestone:** M10 - TLS and HTTP/2
**Component:** HPACK
**Estimate:** M
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Provide bounded HPACK primitives and static tables for libhttpserver v3.0.

**Action Items:**
- [x] Implement bounded HPACK integers, Huffman strings and static table.
- [x] Reject padding, EOS and integer overflow.
- [x] Replay RFC 7541 vectors and fuzz malformed blocks.

**Dependencies:**
- Blocked by: TASK-097
- Blocks: TASK-138, TASK-139

**Acceptance Criteria:**
- RFC vectors pass and malformed integers, Huffman padding or expanded sizes fail before proportional allocation.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-005, PRD-V3N-REQ-017
**Related Decisions:** DR-V3-001, DR-V3-006

**Status:** In Progress

**Implementation notes:**
- Private header-only primitives accept borrowed octet spans and explicit finite
  integer/payload/decoded/output limits. Static entries and a 513-node Huffman
  trie are immutable compile-time data. No public resource or compression
  context was added; connection tables and field sections remain TASK-138.
- New focused checks cover all 61 static entries, all 256 octets, integer
  boundaries/truncations/overflow, valid padding lengths, EOS/malformed tails,
  exact string limits, and actual allocation admission. Large rejected inputs
  request zero output allocations; valid 4 KiB Huffman output allocates once.
- Appendix C.2-C.6 fixtures replay 29 literals (all 12 Huffman literals) and 60
  integer constituents at explicit checked offsets, plus an ordered duplicate
  append seam. These checks do not claim full dynamic-block replay.
- Implementation checks: local C++20 Autotools build; seven focused/adjacent
  check programs; strict warning builds; changed-file cpplint and CCN <= 10;
  repository file-size check; Homebrew LLVM libFuzzer with ASan/UBSan and seeded
  deterministic mutations. The final seeded 30-second fuzz smoke executed
  2,961,002 inputs without findings; all 162 corpus files replayed successfully.
  See `test/fuzz/README-hpack.md` for reproduction.
- Repository-wide complexity still reports three inherited violations in
  `dispatch_request`, `valid_peer_pattern`, and `pollsys::accept_one`; no HPACK
  function exceeds the threshold. Nonlocal platform checks belong to CI and
  the v3 PR. Formal validation/finalization remain caller-owned.
