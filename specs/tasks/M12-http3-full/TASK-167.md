### TASK-167: Implement QPACK dynamic table and instruction codecs

**Milestone:** M12 - Full HTTP/3
**Component:** QPACK
**Estimate:** L
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Provide QPACK dynamic table and instruction codecs for libhttpserver v3.0.

**Action Items:**
- [x] Implement QPACK insert, duplicate, capacity and eviction instructions.
- [x] Track absolute/relative/post-base references.
- [x] Replay RFC dynamic table vectors and malformed-index errors.

**Dependencies:**
- Blocked by: TASK-159
- Blocks: TASK-168

**Acceptance Criteria:**
- RFC dynamic vectors pass and referenced entries cannot be evicted prematurely.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-007, PRD-V3N-REQ-017
**Related Decisions:** DR-V3-001, DR-V3-006

**Status:** In Progress

**Implementation evidence:**
- Private table storage admits the configured maximum against hierarchical
  `qpack_table_bytes`; FIFO mutation prepares owned strings before eviction.
- Encoder entries require both insertion acknowledgment and released section
  holds before eviction. Decoder replicas replay instructions without that gate.
- Instruction codecs and explicit dynamic section APIs cover relative/post-base
  references, Required Insert Count wrapping, ordered fields, and sensitivity.
- RFC 9204 Appendix B.2–B.5 is replayed continuously with separate encoder and
  decoder tables, delayed duplicate delivery, feedback, and eviction checkpoints.
- Local C++20 Autotools build with `CXX=clang++` and TLS disabled passed. The
  eleven focused QPACK/HPACK/semantic targets passed; four changed standalone
  tests also passed strict GCC compilation (`-Wall -Wextra -Werror -pedantic`).
  Changed-file cpplint and `git diff --check` passed.
- The default GCC full build encountered the pre-existing `native_server::peer_policy`
  declaration diagnostic in `server.hpp:206`; Clang completed the local build.
  BSD, Windows, and other nonlocal checks remain assigned to CI and the v3 PR.
- Connection dispatch, blocked-section storage, and acknowledgment queues remain
  TASK-168 responsibilities. Task status stays In Progress for caller validation.
