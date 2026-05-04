### TASK-010: `http_response` factory functions

**Milestone:** M2 - Response Refactor
**Component:** `http_response` factories
**Estimate:** M

**Goal:**
Provide one canonical way to construct each body kind via static factories that return `http_response` by value.

**Action Items:**
- [x] Add static factories on `http_response`:
  - `static http_response string(std::string body, std::string content_type = "text/plain");`
  - `static http_response file(std::string path);`
  - `static http_response iovec(std::span<const httpserver::iovec_entry> entries);`
  - `static http_response pipe(int fd, std::size_t size_hint = 0);`
  - `static http_response empty();`
  - `static http_response deferred(std::function<ssize_t(std::uint64_t, char*, std::size_t)> producer);`
  - `static http_response unauthorized(std::string_view scheme, std::string_view realm, std::string body = {});`
- [x] Each factory placement-news the appropriate `detail::body` subclass into `body_storage_` (and sets `body_inline_ = true`); for the (currently empty) heap-fallback path, the factory MUST use `::operator new(sizeof(concrete_body))` followed by placement-new (NOT plain `new concrete_body(...)`) so that `http_response`'s destructor — which always calls `body_->~body()` and then `::operator delete(body_)` for the heap path — does not double-destroy. This contract is set by TASK-009 (plan OQ-4) for symmetry between inline and heap teardown.
- [x] `unauthorized()` covers both basic and digest auth (scheme parameter); replaces v1's `basic_auth_fail_response` and `digest_auth_fail_response`.
- [x] Document lifetime: `pipe(fd, ...)` takes ownership of `fd` and closes it after the response is materialized.

**Dependencies:**
- Blocked by: TASK-008, TASK-009, TASK-004
- Blocks: TASK-013

**Acceptance Criteria:**
- `auto r = http_response::string("hi");` compiles, `r.kind() == body_kind::string`.
- `auto r = http_response::iovec(std::array<iovec_entry, 2>{...});` compiles without including `<sys/uio.h>` from user code.
- `http_response::unauthorized("Basic", "myrealm")` produces a 401 with `WWW-Authenticate: Basic realm="myrealm"` header.
- Typecheck passes.
- Tests pass.

**Related Requirements:** PRD-RSP-REQ-001, PRD-RSP-REQ-005, PRD-RSP-REQ-007
**Related Decisions:** §4.3, DR-005

**Status:** Done
