### TASK-021: `http_resource` allow-mask via `method_set`

**Milestone:** M4 - Handler & Resource Model
**Component:** `http_resource`
**Estimate:** M

**Goal:**
Replace `http_resource`'s `std::map<std::string, bool> method_state` with a `method_set` bitmask, shrink `sizeof(http_resource)`, and make `is_allowed`/`get_allowed_methods` const.

**Action Items:**
- [ ] Replace `std::map<std::string, bool> method_state` with `method_set methods_allowed_;` member.
- [ ] `bool is_allowed(http_method m) const noexcept` returns `methods_allowed_.contains(m)`.
- [ ] `method_set get_allowed_methods() const noexcept` returns `methods_allowed_` by value.
- [ ] `void set_allowing(http_method m, bool allow) noexcept` (mutator stays non-const).
- [ ] `void allow_all() noexcept;` `void disallow_all() noexcept;`
- [ ] Convert internal v1 callers that passed method names as strings to use `http_method` enum values; provide a string→enum helper if existing user-facing setters need to keep their string form.

**Dependencies:**
- Blocked by: TASK-005
- Blocks: TASK-022, TASK-027, TASK-039

**Acceptance Criteria:**
- `sizeof(http_resource)` decreases by at least the cost of an empty `std::map<std::string, bool>` (PRD §3.6 acceptance — measured in TASK-039).
- `is_allowed(http_method)` is const and noexcept.
- All v1 tests that exercised method-allow toggling still pass after migration to the enum.
- Typecheck passes.
- Tests pass.

**Related Requirements:** PRD-REQ-REQ-002, PRD-REQ-REQ-003
**Related Decisions:** DR-006, §4.4

**Status:** Not Started
