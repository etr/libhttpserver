### TASK-082: Tighten static-size bounds in `http_resource_test` and `webserver_pimpl_test`

**Milestone:** M7 - v2 Cleanup
**Component:** `test/unit/http_resource_test.cpp`, `test/unit/webserver_pimpl_test.cpp`
**Estimate:** S

**Goal:**
Two loose `sizeof` gates lose their bite as bystanders for accidental field bloat:
- `test/unit/http_resource_test.cpp:51-60` — sizeof gate loose at 256 bytes; authoritative check delegated to a bench.
- `test/unit/webserver_pimpl_test.cpp:42-52` — pimpl size bound at 1152 bytes, "TASK-019/020 will tighten".

Tighten each to the current observed size + a small slack (e.g., +16 bytes) so growth requires explicit threshold updates, not silent drift.

**Action Items:**
- [x] Run `sizeof(http_resource)` on every CI lane (libc++/libstdc++, 32-bit/64-bit, Apple/Linux/Windows). Capture in a comment table.
- [x] Set the gate to `max(observed) + 16` (one cache-line of slack) and assert via `static_assert` so failures are compile-time.
- [x] Same for `sizeof(webserver)`. Confirm TASK-019/020 have shipped, then set the gate to the tightened observed size.
- [x] Update the gate comment to reference the table and the rationale for the slack.

**Dependencies:**
- Blocked by: TASK-019 (Done), TASK-020 (Done), TASK-021 (Done)
- Blocks: None

**Acceptance Criteria:**
- Both gates are `static_assert`s at `observed + 16` (or tighter) on every CI lane.
- The gates' comment carries the per-lane size table.
- Typecheck passes.
- Tests pass.

**Related Requirements:** PRD-REQ-REQ-003 (bitmask method state — `sizeof(http_resource)` shrink)
**Related Decisions:** §2.2 const-correctness, §4.2

**Status:** In Progress
