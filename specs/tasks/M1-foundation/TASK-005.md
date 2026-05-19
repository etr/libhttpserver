### TASK-005: Add `http_method` enum and `method_set` bitmask

**Milestone:** M1 - Foundation
**Component:** `http_method` / `method_set`
**Estimate:** M

**Goal:**
Introduce the type-safe HTTP-method primitives that `http_resource`, route table, and lambda registration all consume.

**Action Items:**
- [x] Create `src/httpserver/http_method.hpp` (gated per TASK-002).
- [x] Define `enum class http_method : std::uint8_t { get, head, post, put, del, connect, options, trace, patch, count_ };` (note: `del`, not `delete`).
- [x] Define `struct method_set { std::uint32_t bits = 0; ... };` with constexpr `contains`, `set`, `clear`, `set_all`, `clear_all`.
- [x] Add free constexpr noexcept bitwise operators (`|`, `&`, `^`, `~`, `|=`, `&=`, `^=`) on `http_method` and `method_set`, all consteval-friendly.
- [x] Add `to_string(http_method)` returning a `string_view` (for logging / 405 Allow header construction).
- [x] Re-export from `<httpserver.hpp>`.

**Dependencies:**
- Blocked by: TASK-002
- Blocks: TASK-021, TASK-025, TASK-026, TASK-027

**Acceptance Criteria:**
- `static_assert(method_set{}.set(http_method::get).contains(http_method::get));` passes at compile time.
- `static_assert(static_cast<std::uint8_t>(http_method::count_) <= 32);` passes (room in the bitmask).
- Unit tests cover bitwise composition, `to_string`, and round-trip through `set`/`contains`.
- Typecheck passes.
- Tests pass.

**Related Requirements:** PRD-REQ-REQ-003, PRD-HDL-REQ-006
**Related Decisions:** DR-006

**Status:** Done
