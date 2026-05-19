### TASK-040: Rewrite `examples/`

**Milestone:** M6 - Release Readiness
**Component:** Documentation
**Estimate:** L

**Goal:**
Provide the lambda-first hello world (≤10 LOC) and a class-based shared-state example, plus the rest of the example suite refreshed to v2.0 idioms.

**Action Items:**
- [ ] Write `examples/hello_world.cpp` using `on_get` + lambda — count lines including `main()`; target ≤10.
- [ ] Write `examples/shared_state.cpp` using a `http_resource` subclass that holds a counter mutated under `std::mutex` from both `render_get` and `render_post` — explicitly demonstrates the case where the class form is the right shape.
- [ ] Audit existing examples; port each to v2.0 (`with_*` chains, smart-ptr resources, snake_case `render_*`, `http_response::factory(...)` returns).
- [ ] Remove examples that demonstrated v1-only patterns (raw-pointer ownership, paired `no_*` setters, *_response subclasses).
- [ ] Each example should compile against the installed v2.0 headers as a minimal Makefile or CMake snippet.

**Dependencies:**
- Blocked by: TASK-025, TASK-036
- Blocks: TASK-041 (README references the examples)

**Acceptance Criteria:**
- `hello_world.cpp` is ≤10 LOC including `main()`, no subclass, no raw pointer (PRD §3.4 acceptance).
- `shared_state.cpp` exercises GET + POST on the same resource sharing a counter; demonstrates the locking pattern.
- All examples build clean with `make examples` (or equivalent).
- Typecheck passes.
- Tests pass.

**Related Requirements:** PRD-HDL-REQ-001..006, PRD §3.4 acceptance
**Related Decisions:** §13 documentation deliverable, AR-006

**Status:** Not Started
