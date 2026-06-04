### TASK-072: Arena-allocated unescape on the warm path

**Milestone:** M7 - v2 Cleanup
**Component:** `src/detail/http_request_impl.cpp`
**Estimate:** M

**Goal:**
`src/detail/http_request_impl.cpp:202-206` is an acknowledged TASK-018 deferral: the warm-path "0-alloc" criterion explicitly does not apply when a user unescaper is registered, because the unescape call still allocates a `std::string`. Route the unescape output into the per-connection arena (TASK-016) so the criterion holds unconditionally.

**Action Items:**
- [ ] Extend the per-connection arena (from TASK-016) with an `unescape_into(string_view in, void* sink) -> string_view` helper, or have the arena expose a scratch-buffer API the unescape can write into.
- [ ] Refactor the unescape call site at `http_request_impl.cpp:202-206` to allocate into the arena. The returned `string_view` is valid for the lifetime of the request, matching the TASK-018 lifetime documentation.
- [ ] Update the inline TASK-018 deferral comment to reflect that arena unescape is now wired.
- [ ] Extend `test/bench_warm_path.cpp` (TASK-058) with an unescape variant: warm GET with `%2F` in the path. Pin "no allocations under heap profiler" as in TASK-058.

**Dependencies:**
- Blocked by: TASK-016 (arena, Done), TASK-018 (string_view getters, Done), TASK-058 (warm-path bench infra, Done)
- Blocks: None

**Acceptance Criteria:**
- The warm-path GET with an unescaper-registered route shows zero heap allocations under a heap profiler.
- `bench_warm_path` `%2F` variant matches the no-unescape baseline within noise.
- All existing unescape tests still pass.
- Typecheck passes.
- Tests pass.

**Related Requirements:** PRD-REQ-REQ-001 (const-correctness + zero-alloc)
**Related Decisions:** DR-003b (arena)

**Status:** Backlog
