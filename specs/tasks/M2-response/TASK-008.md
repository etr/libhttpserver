### TASK-008: Internal `detail::body` hierarchy

**Milestone:** M2 - Response Refactor
**Component:** `detail::body`
**Estimate:** L

**Goal:**
Build the polymorphic body hierarchy that `http_response`'s SBO buffer hosts, so factories have something concrete to placement-new into.

**Action Items:**
- [x] Create `src/httpserver/details/body.hpp` (gated `HTTPSERVER_COMPILATION` only).
- [x] Define `enum class body_kind { empty, string, file, iovec, pipe, deferred };` in a public header (consumers may inspect via `http_response::kind()`). *(Implemented in `src/httpserver/body_kind.hpp`, exposed via `httpserver.hpp`.)*
- [x] Define abstract `class detail::body` with `virtual ~body()`, `virtual body_kind kind() const noexcept = 0`, `virtual std::size_t size() const noexcept = 0`, `virtual MHD_Response* materialize(...) = 0`.
- [x] Implement subclasses: `string_body` (holds `std::string`), `file_body` (path + cached size), `iovec_body` (`std::vector<struct iovec>` — `<sys/uio.h>` allowed in this private header), `pipe_body` (fd + size hint), `deferred_body` (`std::function<ssize_t(uint64_t, char*, std::size_t)>`), `empty_body`. *(All six implemented in `src/httpserver/details/body.hpp` + `src/details/body.cpp`.)*
- [x] At end of the header: `static_assert(sizeof(string_body) <= 64); static_assert(sizeof(file_body) <= 64); ...` for each subclass; `static_assert(alignof(deferred_body) <= 16);`. *(All static_asserts present at end of `body.hpp`; mirrored in `test/unit/body_test.cpp`.)*
- [x] If a subclass doesn't fit in 64 B: the SBO contract from DR-005 says we heap-allocate it; document this fallback path and add a runtime branch in `http_response`'s factories. *(All current subclasses fit; static_asserts confirm it. The runtime heap-fallback branch is delegated to TASK-010's factories per a comment in `body.hpp` referencing DR-005. `iovec_body` intentionally accepts one heap allocation for its `std::vector` backing store — documented in the class comment.)*

**Dependencies:**
- Blocked by: TASK-002
- Blocks: TASK-009, TASK-010

**Acceptance Criteria:**
- All `static_assert`s on body subclass sizes pass.
- `materialize()` for each kind produces a valid `MHD_Response*` matching v1's behavior for the equivalent v1 subclass (`string_response` etc.).
- Typecheck passes.
- Tests pass.

**Related Requirements:** PRD-RSP-REQ-006 (subclasses removed from public API), PRD-HDR-REQ-005
**Related Decisions:** DR-005, §4.8

**Status:** Done
