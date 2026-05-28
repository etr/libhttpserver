### 4.9 `create_webserver` (builder)

**Responsibility:** Configuration builder for `webserver`.

**Implementation:** Single-class builder, ~half the v1 line count. Each paired `foo()/no_foo()` collapses to `foo(bool = true)` (PRD-CFG-REQ-001). All `#define` constants (`DEFAULT_WS_PORT`, `DEFAULT_WS_TIMEOUT`, `NOT_FOUND_ERROR`) move to `constexpr` in `httpserver::constants` (PRD-CFG-REQ-002). Out-of-range setters throw `std::invalid_argument` (PRD-CFG-REQ-003).

The builder remains non-PIMPL (it's a pure value carrier; PIMPL would buy nothing).

**Related requirements:** PRD-CFG-REQ-001..004.

---
