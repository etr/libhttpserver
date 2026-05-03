### 4.3 `http_response`

**Responsibility:** Describe the response a handler wants to send: status, headers, footers, cookies, body. Constructed by user code via factories; consumed by library dispatch which materializes an `MHD_Response*` from it.

**Implementation:** **Non-PIMPL value type.** Public header carries the data members directly:
- `int status_code`
- `http::header_map headers`, `footers`, `cookies` (separate maps; cookies kept distinct from headers for v2.0 API compatibility)
- `body_kind kind_` enum (`empty`, `string`, `file`, `iovec`, `pipe`, `deferred`)
- `alignas(16) std::byte body_storage_[64]` — SBO buffer for the body subclass
- `detail::body* body_` — points into `body_storage_` (inline) or to a heap object
- `bool body_inline_` — bookkeeping for destructor / move

The body subclasses (`detail::string_body`, `file_body`, `iovec_body`, `pipe_body`, `deferred_body`, `empty_body`) live in `src/httpserver/detail/body.hpp` and are not installed.

**SBO contract:**
- All current body subclasses are sized to fit in 64 bytes. The largest, `deferred_body` (~56 bytes including vptr + `std::function` on libstdc++), has 8 bytes of headroom.
- A body subclass added in v2.x that exceeds 64 bytes heap-allocates instead — graceful fallback. Bumping the buffer is an ABI break.
- Buffer alignment is 16 bytes (covers `std::function` and any `alignas(16)` member we might add).

**Interfaces:**
- Exposes (from PRD §3.5):
  - Factories: `http_response::string(...)`, `::file(...)`, `::iovec(std::span<const httpserver::iovec_entry>)`, `::pipe(...)`, `::empty(...)`, `::deferred(...)`, `::unauthorized(scheme, realm, ...)` — all return `http_response` by value.
  - **`httpserver::iovec_entry`** is a library-defined POD declared in `<httpserver/http_response.hpp>`: `struct iovec_entry { const void* base; std::size_t len; };`. It mirrors POSIX `struct iovec` exactly in layout but does not require `<sys/uio.h>` in any installed header. The internal dispatch path uses the user-supplied span to build a `struct iovec` array inside `iovec_body`. The implementation file (`detail/body.hpp` / `http_response.cpp`) carries `static_assert`s pinning the layout assumption: `static_assert(sizeof(iovec_entry) == sizeof(struct iovec))`, `static_assert(offsetof(iovec_entry, base) == offsetof(struct iovec, iov_base))`, `static_assert(offsetof(iovec_entry, len) == offsetof(struct iovec, iov_len))`. When the asserts hold, conversion is a `reinterpret_cast`; when they fail (a hypothetical platform with divergent layout), the build fails loudly at compile time and we fall back to memcpy. This keeps the public header free of system headers and makes the API uniformly available on platforms where `<sys/uio.h>` is not standard (e.g., MSVC builds).
  - Fluent setters: `with_header`, `with_footer`, `with_cookie`, `with_status` — return `http_response&`.
  - `const` accessors: `get_header`, `get_footer`, `get_cookie` returning `string_view` (empty on miss; do not insert).
  - `get_headers`, `get_footers`, `get_cookies` returning `const map&`.
  - `kind()` returning `body_kind`.
- The virtuals `get_raw_response`, `decorate_response`, `enqueue_response` are removed from the public API (PRD-HDR-REQ-005). The MHD response object is constructed inside the library's dispatch path from the `http_response` value's `body_->materialize()` (or equivalent internal API on `detail::body`).

**Move semantics:** hand-written to handle the inline-vs-heap cross-product (4 cases on assignment, 2 on construction). Move construct: if source body is inline, placement-new into destination's buffer + destruct source's; if heap, swap pointer. Move assign covers inline↔inline, inline↔heap, heap↔inline, heap↔heap. Tested under sanitizers.

**Related requirements:** PRD-HDR-REQ-004 (exempt), PRD-RSP-REQ-001..007.

---
