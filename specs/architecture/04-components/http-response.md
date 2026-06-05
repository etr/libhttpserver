### 4.3 `http_response`

**Responsibility:** Describe the response a handler wants to send: status, headers, footers, cookies, body. Constructed by user code via factories; consumed by library dispatch which materializes an `MHD_Response*` from it.

**Implementation:** **Non-PIMPL value type, declared `final` (sealed per PRD §3.5).** Inheritance is prevented at compile time; `static_assert(std::is_final_v<httpserver::http_response>)` is exercised in the SBO unit test. Public header carries the data members directly:
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
  - Factories: `http_response::string(...)`, `::file(...)`, `::iovec(std::span<const httpserver::iovec_entry>)`, `::pipe(...)`, `::empty(...)`, `::deferred(...)`, `::unauthorized(scheme, realm, ...)`, `::unauthorized(digest_challenge)` — all return `http_response` by value. The `unauthorized(digest_challenge)` overload (TASK-062, declared behind `HAVE_DAUTH`) produces a fully RFC 7616 §3.3-compliant `WWW-Authenticate: Digest …` challenge with `nonce`, `opaque`, `algorithm`, and `qop` parameters; the dispatch path detects `body_kind::digest_challenge` and routes through libmicrohttpd's `MHD_queue_auth_required_response3`, which drives the per-connection nonce state machine. The legacy `unauthorized("Digest", realm, body)` string overload remains for source compatibility and emits only `WWW-Authenticate: Digest realm="<realm>"`; for new code prefer the `digest_challenge` overload. See DR-013 (Superseded by TASK-062) for the supersession note.
  - **`httpserver::iovec_entry`** is a library-defined POD declared in `<httpserver/iovec_entry.hpp>` (a dedicated public header installed alongside `http_response.hpp` and included by it): `struct iovec_entry { const void* base; std::size_t len; };`. It mirrors POSIX `struct iovec` exactly in layout but does not require `<sys/uio.h>` in any installed header. The internal dispatch path uses the user-supplied span to build a `struct iovec` array inside `iovec_body`. The implementation file `src/detail/body.cpp` carries `static_assert`s pinning the layout assumption: `static_assert(sizeof(iovec_entry) == sizeof(struct iovec))`, `static_assert(offsetof(iovec_entry, base) == offsetof(struct iovec, iov_base))`, `static_assert(offsetof(iovec_entry, len) == offsetof(struct iovec, iov_len))`. When the asserts hold, conversion is a `reinterpret_cast`; when they fail (a hypothetical platform with divergent layout), the build fails loudly at compile time and we fall back to memcpy. This keeps the public header free of system headers and makes the API uniformly available on platforms where `<sys/uio.h>` is not standard (e.g., MSVC builds).
  - Fluent setters: `with_header`, `with_footer`, `with_cookie`, `with_status` — each has two ref-qualified overloads: `& → http_response&` (mutate-in-place on an lvalue) and `&& → http_response&&` (return the object by rvalue-reference for zero-copy rvalue factory chains, e.g. `http_response::string("body").with_header("X-Foo", "bar").with_status(201)`).
  - `const` accessors: `get_header`, `get_footer`, `get_cookie` returning `string_view` (empty on miss; do not insert).
  - `get_headers`, `get_footers`, `get_cookies` returning `const map&`.

**Cookie surface (TASK-064):** the v2.0 cookie API is structured. A new public header `<httpserver/cookie.hpp>` declares `httpserver::cookie` (a copyable + movable value type) with fluent `with_name`, `with_value`, `with_domain`, `with_path`, `with_expires`, `with_max_age`, `with_secure`, `with_http_only`, `with_same_site` setters, plus an `enum class same_site_mode { unset, strict, lax, none }`. `http_response::with_cookie(cookie)` appends to a `std::vector<cookie>` carried directly on the response (separate field from the legacy `cookies_` map). The dispatch path (`detail/webserver_request.cpp::decorate_mhd_response`) renders one `Set-Cookie` header per entry via `cookie::to_set_cookie_header()`, which produces an RFC 6265 §4.1 well-formed serialization with fixed attribute ordering (`name=value; Expires=...; Max-Age=...; Domain=...; Path=...; Secure; HttpOnly; SameSite=...`) and auto-coerces `Secure` when `SameSite=None` is set. `cookie::parse_cookie_header(string_view)` is the matching RFC 6265 §5.4 request-side parser (byte-transparent, skips entries without `=`, strips outer DQUOTE pairs). The legacy `with_cookie(string, string)`, `get_cookie(...)`, and `get_cookies()` accessors are `[[deprecated]]` and will be removed in v2.1; they keep working through a thin shim that forwards through the structured path and mirrors name/value into the legacy `cookies_` map for source-compatibility with v1 callers.
  - `kind()` returning `body_kind`.
- The virtuals `get_raw_response`, `decorate_response`, `enqueue_response` are removed from the public API (PRD-HDR-REQ-005). The MHD response object is constructed inside the library's dispatch path from the `http_response` value's `body_->materialize()` (or equivalent internal API on `detail::body`).

**Move semantics:** hand-written to handle the inline-vs-heap cross-product (4 cases on assignment, 2 on construction). Move construct: if source body is inline, placement-new into destination's buffer + destruct source's; if heap, swap pointer. Move assign covers inline↔inline, inline↔heap, heap↔inline, heap↔heap. Tested under sanitizers.

**Related requirements:** PRD-HDR-REQ-004 (exempt), PRD-RSP-REQ-001..007.

---
