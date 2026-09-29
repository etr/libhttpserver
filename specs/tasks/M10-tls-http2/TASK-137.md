### TASK-137: Implement bounded HPACK primitives and static tables

**Milestone:** M10 - TLS and HTTP/2
**Component:** HPACK
**Estimate:** M
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Provide bounded HPACK primitives and static tables for libhttpserver v3.0.

**Action Items:**
- [ ] Implement bounded HPACK integers, Huffman strings and static table.
- [ ] Reject padding, EOS and integer overflow.
- [ ] Replay RFC 7541 vectors and fuzz malformed blocks.

**Dependencies:**
- Blocked by: TASK-097
- Blocks: TASK-138, TASK-139

**Acceptance Criteria:**
- RFC vectors pass and malformed integers, Huffman padding or expanded sizes fail before proportional allocation.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-005, PRD-V3N-REQ-017
**Related Decisions:** DR-V3-001, DR-V3-006

**Status:** Not Started
