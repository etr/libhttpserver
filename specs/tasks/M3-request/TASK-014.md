### TASK-014: `webserver_impl` skeleton (PIMPL prep, structural only)

**Milestone:** M3 - Webserver internal & Request Refactor
**Component:** `webserver` / `webserver_impl`
**Estimate:** L

**Goal:**
Move `webserver`'s backend state (`MHD_Daemon*`, mutexes, ban set, connection table) into `details/webserver_impl.hpp` so the public header carries only `std::unique_ptr<webserver_impl>`. No API rename or behavioral change yet — pure structural move.

**Action Items:**
- [ ] Create `src/httpserver/details/webserver_impl.hpp` (gated `HTTPSERVER_COMPILATION` only).
- [ ] Move from public `webserver.hpp` into `webserver_impl`: `MHD_Daemon* daemon_`, all mutex/cond_var members, ban list, connection-state map, route-table data structures.
- [ ] Public `webserver.hpp` declares `class webserver { ... std::unique_ptr<webserver_impl> impl_; ... };` and forward-declares `class webserver_impl;` in `httpserver::detail` namespace.
- [ ] Implement public methods as one-liners forwarding to `impl_->method()`.
- [ ] Move `<microhttpd.h>` and `<pthread.h>` includes from public `webserver.hpp` into `webserver_impl.hpp` and `webserver.cpp`.
- [ ] Define a `connection_state` struct inside `webserver_impl` (will host the per-connection arena in TASK-016).

**Dependencies:**
- Blocked by: TASK-002
- Blocks: TASK-015, TASK-016, TASK-020, TASK-023, TASK-025, TASK-027, TASK-029, TASK-030, TASK-033, TASK-035

**Acceptance Criteria:**
- `grep -E '#include\s+<microhttpd\.h>' src/httpserver/webserver.hpp` returns nothing (matches the future state for full hygiene).
- All v1 tests pass without modification — the move is behavior-preserving.
- `sizeof(webserver)` is a single pointer plus any non-impl members (typically just `sizeof(void*)`).
- Typecheck passes.
- Tests pass.

**Related Requirements:** PRD-HDR-REQ-001..004
**Related Decisions:** DR-002, DR-003b, §4.1

**Status:** Not Started
