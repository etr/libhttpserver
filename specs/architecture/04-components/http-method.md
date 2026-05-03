### 4.6 `http_method` and `method_set`

**Responsibility:** Type-safe representation of HTTP methods and method-allow masks.

**Implementation:**

```cpp
enum class http_method : std::uint8_t {
    get, head, post, put, del, connect, options, trace, patch, count_
};
// `del` rather than `delete` (C++ keyword); `count_` sentinel for compile-time iteration.

struct method_set {
    std::uint32_t bits = 0;
    constexpr bool contains(http_method m) const noexcept;
    constexpr method_set& set(http_method m) noexcept;
    constexpr method_set& clear(http_method m) noexcept;
    constexpr method_set& set_all() noexcept;
    constexpr method_set& clear_all() noexcept;
    // bitwise free operators on http_method and method_set, all constexpr noexcept
};
```

`uint32_t` carries 32 method slots — 23 bits of growth headroom beyond the 9 standard methods (room for WebDAV verbs if ever added).

**Related requirements:** PRD-REQ-REQ-003, PRD-HDL-REQ-006.

---
