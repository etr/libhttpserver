### TASK-009: `http_response` value type with SBO buffer

**Milestone:** M2 - Response Refactor
**Component:** `http_response`
**Estimate:** L

**Goal:**
Convert `http_response` to a non-PIMPL value type carrying a 64-byte SBO buffer for the polymorphic body, with hand-written move semantics covering the inline/heap cross-product.

**Action Items:**
- [ ] In `src/httpserver/http_response.hpp`, declare:
  - `int status_code_;`
  - `header_map headers_; footers_; cookies_;`
  - `body_kind kind_;`
  - `alignas(16) std::byte body_storage_[64];`
  - `detail::body* body_ = nullptr;`
  - `bool body_inline_ = false;`
  - public constant `static constexpr std::size_t body_buf_size = 64;`
- [ ] Forward-declare `namespace httpserver::detail { class body; }` in the public header (no `body.hpp` include).
- [ ] Implement move ctor: if source is inline, placement-new the destination's body, call source's destructor, point `body_` at destination's buffer; if heap, swap pointer, set `body_inline_ = false`.
- [ ] Implement move-assign covering all 4 cross-product cases (inline↔inline, inline↔heap, heap↔inline, heap↔heap).
- [ ] Destructor calls `body_->~body()` always; calls `delete body_` only if `!body_inline_`.
- [ ] Copy ctor / copy assign: deleted (responses are move-only — value type but not copyable).

**Dependencies:**
- Blocked by: TASK-008
- Blocks: TASK-010, TASK-011, TASK-012, TASK-013, TASK-025, TASK-038

**Acceptance Criteria:**
- `static_assert(std::is_nothrow_move_constructible_v<http_response>)`.
- `static_assert(!std::is_copy_constructible_v<http_response>)`.
- AddressSanitizer + UndefinedBehaviorSanitizer report clean across all 4 move cases (test added in TASK-038 — placeholder green-light expected here).
- `http_response` is `final` — PRD §3.5 calls it "a sealed value type"; the `final` keyword realizes that.
- `http_response` is NOT wrapped in PIMPL — it is the explicit exemption named in PRD-HDR-REQ-004 because it carries no backend state. Static check: `static_assert(!std::is_same_v<decltype(http_response::body_), std::unique_ptr<httpserver::detail::body>>);` (or equivalent — there is no `impl_` member).
- Typecheck passes.
- Tests pass.

**Related Requirements:** PRD-HDR-REQ-004 (exemption clause), PRD-RSP-REQ-001, PRD-RSP-REQ-007
**Related Decisions:** DR-003a, DR-005

**Status:** Not Started
