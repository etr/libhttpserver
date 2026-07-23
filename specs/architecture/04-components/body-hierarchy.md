### 4.8 `detail::response_body` hierarchy

**Responsibility:** Polymorphic body representation backing `http_response`'s SBO buffer. Each subclass carries the data needed for one body kind and knows how to stream itself into an MHD response.

**Implementation:** Abstract base in `src/httpserver/detail/body.hpp` (not installed). The vtable anchor (`~body() = default`) is emitted in `src/detail/body.cpp`. All subclasses are copy-deleted and non-assignable; relocation across SBO buffers uses the `move_into(void*)` virtual method (called by `http_response`'s move operations):

```cpp
namespace httpserver::detail {
class body {
public:
    virtual ~body();
    virtual body_kind kind() const noexcept = 0;
    virtual std::size_t size() const noexcept = 0;
    virtual MHD_Response* materialize() = 0;  // builds the MHD response on demand
    virtual void move_into(void* dst) noexcept = 0;  // placement-move into SBO buffer

protected:
    body(const body&) = delete;
    body& operator=(const body&) = delete;
    body(body&&) noexcept = default;       // enabled for subclass move ctors
    body& operator=(body&&) = delete;
};

class string_response_body  : public body { /* std::string content_; */ };
class file_response_body    : public body { /* std::string path_; std::size_t size_; int fd_; bool materialized_; */ };
class iovec_response_body   : public body { /* std::vector<iovec_entry> entries_; std::size_t total_size_; */ };
class pipe_response_body    : public body { /* int fd_; bool materialized_; */ };
class deferred_response_body: public body { /* std::function<ssize_t(uint64_t, char*, std::size_t)> producer_; */ };
class empty_response_body   : public body { /* int flags_; */ };
}
```

**SBO storage:** factories use placement-new into the response's `body_storage_` buffer when the subclass fits (always true for v2.0's set). New body kinds added in v2.x check at compile time (`static_assert`) whether they fit; if they don't, the factory falls back to `new`-allocating and storing the heap pointer.

**Materialization timing:** `materialize()` is called from `webserver`'s dispatch, not from the handler. The body holds whatever data it needs (strings, paths, callables) until that point.

**file_response_body open contract (implementation-close-out note):** Unlike the general guidance above, `file_response_body` opens the file descriptor and calls `fstat` at **construction** rather than in `materialize()`. This deliberate eager-open design achieves two goals: (1) `size()` is accurate immediately without calling `materialize()` first, and (2) `materialize()` can use `st_size` from the already-obtained `fstat` result, avoiding a TOCTOU race and an extra `lseek` syscall. The lazy-open guidance applies to the other body kinds (pipe, deferred); it does not apply to `file_response_body`.

**pipe_response_body ownership contract:** `pipe_response_body` takes ownership of a read-side file descriptor at construction. If `materialize()` succeeds, libmicrohttpd owns the fd (it is closed when `MHD_destroy_response` is called). If `materialize()` is never called, `~pipe_response_body()` closes the fd to prevent a leak. The `materialized_` field suppresses the destructor's close once MHD has taken ownership.

**iovec_response_body allocation note:** `iovec_response_body` intentionally incurs one heap allocation for its `std::vector<iovec_entry>` backing store. The SBO slot holds only the vector control block (~24 bytes); the iovec entry array lives on the heap. This is the only SBO-resident body kind that performs a secondary heap allocation, and it is accepted as an inherent cost of `std::vector` (not a DR-005 violation — DR-005 governs the body-pointer allocation, not internal allocations within a fitting body).

**Related requirements:** PRD-RSP-REQ-006, PRD-HDR-REQ-005.

---
