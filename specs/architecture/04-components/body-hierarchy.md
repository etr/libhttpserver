### 4.8 `detail::body` hierarchy

**Responsibility:** Polymorphic body representation backing `http_response`'s SBO buffer. Each subclass carries the data needed for one body kind and knows how to stream itself into an MHD response.

**Implementation:** Abstract base in `src/httpserver/detail/body.hpp` (not installed):

```cpp
namespace httpserver::detail {
class body {
public:
    virtual ~body() = default;
    virtual body_kind kind() const noexcept = 0;
    virtual std::size_t size() const noexcept = 0;
    virtual MHD_Response* materialize(/* dispatch context */) = 0;  // builds the MHD response on demand
};

class string_body  : public body { /* std::string content; */ };
class file_body    : public body { /* std::string path; std::size_t size_cached; */ };
class iovec_body   : public body { /* std::vector<iovec> iov; (iovec from <sys/uio.h>, included only in this private header) */ };
class pipe_body    : public body { /* int fd; std::size_t hint; */ };
class deferred_body: public body { /* std::function<ssize_t(uint64_t pos, char* buf, std::size_t max)> producer; */ };
class empty_body   : public body { /* nothing */ };
}
```

**SBO storage:** factories use placement-new into the response's `body_storage_` buffer when the subclass fits (always true for v2.0's set). New body kinds added in v2.x check at compile time (`static_assert`) whether they fit; if they don't, the factory falls back to `new`-allocating and storing the heap pointer.

**Materialization timing:** `materialize()` is called from `webserver`'s dispatch, not from the handler. The body holds whatever data it needs (strings, paths, callables) until that point; resources owned by the body (file handles, pipe FDs) are opened lazily during materialize where appropriate.

**Related requirements:** PRD-RSP-REQ-006, PRD-HDR-REQ-005.

---
