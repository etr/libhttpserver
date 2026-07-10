### TASK-095: Zero arena *overflow* blocks on connection reuse (CWE-226 residue from TASK-068)

**Milestone:** M7 - v2 Cleanup
**Component:** `src/httpserver/detail/connection_state.hpp`
**Estimate:** M

**Goal:**
TASK-068 closed the CWE-226 residual-credential window for the arena's
`initial_buffer_` (8 KiB, zeroed via `secure_zero` in `reset_arena()`),
but explicitly accepted one residue: when a request's arena usage
overflows `ARENA_INITIAL_BYTES`, `std::pmr::monotonic_buffer_resource`
silently allocates additional blocks from the upstream (heap) resource.
`arena_.release()` frees those overflow blocks back to the process heap
WITHOUT zeroing them, so credential bytes decoded into an overflow block
(large Authorization header, many headers, large body) remain observable
to any future allocation from the same heap. The accepted-residue
comment in `connection_state.hpp` (reset_arena scope-limitation block)
references this task.

**Action Items:**
- [ ] Implement one of the two closure options from the TASK-068 review:
  (a) a zero-on-deallocate upstream `std::pmr::memory_resource` adapter
  that wraps `new_delete_resource` and calls `secure_zero` on every
  `do_deallocate` before forwarding; or (b) a hand-rolled arena that
  tracks all overflow blocks and zeroes + frees them on release.
  Option (a) is expected to be the smaller diff: plug the adapter in as
  the upstream of `arena_` at construction.
- [ ] Extend `test/unit/connection_state_sentinel_test.cpp` with an
  overflow case: allocate past `ARENA_INITIAL_BYTES`, write a sentinel
  into the overflow region, `reset_arena()`, and assert the overflow
  bytes were scrubbed before the block returned to the heap (the
  adapter makes this observable by intercepting `do_deallocate`).
- [ ] Update the scope-limitation comment in `connection_state.hpp` to
  drop the accepted-residue language once the gap is closed.

**Dependencies:**
- Blocked by: None (TASK-068 is Done)
- Blocks: None

**Acceptance Criteria:**
- Credential bytes written into arena overflow blocks are zeroed before
  the blocks are returned to the upstream allocator, verified by a unit
  test that intercepts deallocation.
- No measurable regression in `bench_warm_path` for requests that do
  NOT overflow the initial buffer (the adapter must only add cost on
  the overflow path).
- ASan + valgrind continue to report clean.
- Typecheck passes.
- Tests pass.

**Related Requirements:** PRD §2 security NFR (CWE-226)
**Related Decisions:** None new

**Status:** Backlog
