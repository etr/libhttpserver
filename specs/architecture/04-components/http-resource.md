### 4.4 `http_resource` (class-form handler)

**Responsibility:** Stateful handler base for cases where state is shared across HTTP methods of one resource (counter, cache, DB handle, auth context).

**Implementation:** Public abstract base. Subclasses override one of `render_get / render_post / render_put / render_delete / render_patch / render_options / render_head` (renamed from v1's `render_GET` etc., to comply with PRD-NAM-REQ-001 snake_case). The default `render(...)` falls back when the method-specific override is not provided.

The allow-mask (formerly `std::map<std::string, bool> method_state`) becomes `method_set methods_allowed_;` — a `uint32_t` bitmask wrapper (DR-6). `is_allowed(http_method)` and `get_allowed_methods()` are `const` and return without allocation.

**Lifetime:** owned by the `webserver` via `unique_ptr` or `shared_ptr` (PRD-HDL-REQ-003). Raw-pointer registration is gone (PRD-HDL-REQ-005).

**Related requirements:** PRD-HDL-REQ-003, PRD-HDL-REQ-005, PRD-REQ-REQ-002, PRD-REQ-REQ-003.

---
