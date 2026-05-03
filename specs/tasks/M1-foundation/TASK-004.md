### TASK-004: Library-defined `iovec_entry` POD with layout-pinning asserts

**Milestone:** M1 - Foundation
**Component:** Public types
**Estimate:** S

**Goal:**
Replace `struct iovec` (`<sys/uio.h>`) at the public API surface with a library-defined POD, while guaranteeing zero-copy interop on platforms whose `struct iovec` matches.

**Action Items:**
- [x] Declare `struct iovec_entry { const void* base; std::size_t len; };` in `<httpserver/http_response.hpp>` (or a small dedicated header it pulls in). — Done: `src/httpserver/iovec_entry.hpp`
- [x] In an implementation file (`http_response.cpp` or `details/body.hpp`), add:
  - `static_assert(sizeof(iovec_entry) == sizeof(struct iovec))`
  - `static_assert(offsetof(iovec_entry, base) == offsetof(struct iovec, iov_base))`
  - `static_assert(offsetof(iovec_entry, len) == offsetof(struct iovec, iov_len))`
  — Done: `src/iovec_response.cpp` (also covers MHD_IoVec, alignof, and standard-layout asserts)
- [x] In the dispatch path, when the asserts hold, use `reinterpret_cast` to feed MHD; otherwise document a memcpy fallback (currently a compile-time fail until a divergent-layout platform appears). — Done: `src/iovec_response.cpp`
- [x] Public header must not include `<sys/uio.h>`. — Confirmed; hygiene enforced by `test/unit/header_hygiene_iovec_test.cpp`

**Dependencies:**
- Blocked by: TASK-002
- Blocks: TASK-010 (factory uses `std::span<const iovec_entry>`)

**Acceptance Criteria:**
- `grep -E '#include\s+<sys/uio\.h>' src/httpserver/*.hpp` returns no results.
- Library compiles on Linux (where `struct iovec` exists) with the static_asserts active.
- A consumer TU including only `<httpserver.hpp>` does not transitively pull in `<sys/uio.h>`.
- Typecheck passes.

**Related Requirements:** PRD-HDR-REQ-001..003 (public-header decoupling)
**Related Decisions:** §2.2 (header hygiene), §4.3 (`http_response`)

**Status:** Done
