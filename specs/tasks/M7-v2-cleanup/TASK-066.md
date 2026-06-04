### TASK-066: Runtime setter for hook alias slots

**Milestone:** M7 - v2 Cleanup
**Component:** `hook_handle`, `webserver_impl`
**Estimate:** M

**Goal:**
Five separate "if a future task adds a runtime setter for the alias slots, this MUST take the lock shared" comments cluster across `src/hook_handle.cpp:142, 449, 497` and `src/httpserver/detail/webserver_impl.hpp:336, 360`. All point at the same missing capability: runtime re-registration of internal alias slots (`log_access`, `not_found_handler`, `method_not_allowed_handler`, `internal_error_handler`, `auth_handler`). Land that capability or strike the speculation.

**Action Items:**
- [ ] Decide: (a) add a `webserver::set_alias(hook_phase, alias_id, hook_action_fn)` runtime setter that re-points the relevant alias slot under the existing `shared_mutex`, with replacement semantics matching the construction-time alias (last-position, single-slot), or (b) remove the forward-debt comments and pin "aliases are immutable after `webserver::start()`" in the docs.
- [ ] If (a): implement the setter, document it in the architecture doc (§4.10), add a hook-bus integration test covering concurrent alias replacement under load, and remove all five forward-debt comments.
- [ ] If (b): remove the five comments and add a documentation note (in `hook_handle.hpp` and the §4.10 architecture page) that alias slots are construction-time-only.
- [ ] Update `test/unit/hooks_log_access_alias_slot_test.cpp` to reflect the chosen semantics — coordinated with TASK-085.

**Dependencies:**
- Blocked by: TASK-045 (hook bus skeleton, Done)
- Blocks: TASK-085 (residual test smell on alias replacement)

**Acceptance Criteria:**
- `grep -nE 'if a future task adds a runtime setter for the alias slots' src/` returns no matches.
- The hook bus architecture doc (§4.10) explicitly states whether alias slots are mutable after start.
- A test pins the chosen semantics (either runtime replacement, or rejection of replacement after start).
- Typecheck passes.
- Tests pass.

**Related Requirements:** PRD-HOOK-REQ-009 (v1 setters documented as aliases)
**Related Decisions:** DR-012

**Status:** Backlog
