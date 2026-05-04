### TASK-016: Per-connection arena for `http_request_impl`

**Milestone:** M3 - Webserver internal & Request Refactor
**Component:** `http_request` / `http_request_impl`
**Estimate:** L

**Goal:**
Eliminate per-request `malloc` on the hot path by allocating `http_request_impl` (and its owned strings/containers where practical) from a `std::pmr::monotonic_buffer_resource` that lives on the connection state.

**Action Items:**
- [ ] Add a `std::pmr::monotonic_buffer_resource arena_;` member (with appropriate initial buffer) to `connection_state` inside `webserver_impl`.
- [ ] Allocate `http_request_impl` from `arena_` via `std::pmr::polymorphic_allocator<>` instead of `new`. Plumb the allocator through the dispatch path so `http_request`'s constructor receives it.
- [ ] Reset the arena when MHD invokes `MHD_RequestTerminationCode` (request-completion callback) so a keep-alive connection reuses the same buffer.
- [ ] Convert internal request-impl containers (`std::pmr::vector`, `std::pmr::string`, `std::pmr::unordered_map`) to use the arena where the type is internal-only.
- [ ] Document the arena-lifetime contract in `webserver_impl`: views returned by `http_request` getters live until the connection's request-completion callback fires.

**Dependencies:**
- Blocked by: TASK-014, TASK-015
- Blocks: TASK-018

**Acceptance Criteria:**
- A microbenchmark shows `http_request_impl` construction allocates 0 bytes from the global heap on a warm connection (after the first request grew the arena).
- Existing request-side tests still pass; AddressSanitizer reports no use-after-free across keep-alive request boundaries.
- `MHD_RequestTerminationCode` callback resets the arena (verified by a test that observes arena memory reuse).
- Typecheck passes.
- Tests pass.

**Related Requirements:** PRD §2 hot-path NFR
**Related Decisions:** DR-003b, §4.2, §5.3, AR-005

**Status:** Not Started
