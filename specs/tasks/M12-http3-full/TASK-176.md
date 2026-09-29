### TASK-176: Fuzz QUIC recovery and TLS callback event sequences

**Milestone:** M12 - Full HTTP/3
**Component:** Build/packaging/validation
**Estimate:** L
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Provide QUIC recovery and TLS callback event sequences for libhttpserver v3.0.

**Action Items:**
- [ ] Generate reordered CRYPTO, ACK, timer, path and callback events.
- [ ] Run stateful ASan/UBSan fuzz targets under byte/time ceilings.
- [ ] Preserve minimized crashes as deterministic regressions.

**Dependencies:**
- Blocked by: TASK-166
- Blocks: TASK-178

**Acceptance Criteria:**
- Stateful sanitizer traces cover reordered CRYPTO/ACK, path changes, callback failure and close races.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-007, PRD-V3N-REQ-008, PRD-V3N-REQ-025
**Related Decisions:** DR-V3-001

**Status:** Not Started
