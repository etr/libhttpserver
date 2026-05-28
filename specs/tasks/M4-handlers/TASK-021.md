### TASK-021: `http_resource` allow-mask via `method_set`

**Milestone:** M4 - Handler & Resource Model
**Component:** `http_resource`
**Estimate:** M

**Goal:**
Replace `http_resource`'s `std::map<std::string, bool> method_state` with a `method_set` bitmask, shrink `sizeof(http_resource)`, and make `is_allowed`/`get_allowed_methods` const.

**Action Items:**
- [x] Replace `std::map<std::string, bool> method_state` with `method_set methods_allowed_;` member.
- [x] `bool is_allowed(http_method m) const noexcept` returns `methods_allowed_.contains(m)`.
- [x] `method_set get_allowed_methods() const noexcept` returns `methods_allowed_` by value.
- [x] `void set_allowing(http_method m, bool allow) noexcept` (mutator stays non-const).
- [x] `void allow_all() noexcept;` `void disallow_all() noexcept;`
- [x] Convert internal v1 callers that passed method names as strings to use `http_method` enum values. The string-based public API (`set_allowing(const std::string&, bool)` / `is_allowed(const std::string&)` / vector-returning `get_allowed_methods()`) was removed outright on this branch (Decision 1 in plan). The wire-string-to-enum decode happens once at `webserver_impl::answer_to_connection` and the result is recorded on `modded_request::method_enum`; no public string-to-enum helper is shipped.

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

**Status:** Done
