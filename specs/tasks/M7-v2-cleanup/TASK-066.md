### TASK-066: Runtime setter for hook alias slots

**Milestone:** M7 - v2 Cleanup
**Component:** `hook_handle`, `webserver_impl`
**Estimate:** M

**Goal:**
Five separate "if a future task adds a runtime setter for the alias slots, this MUST take the lock shared" comments cluster across `src/hook_handle.cpp:142, 449, 497` and `src/httpserver/detail/webserver_impl.hpp:336, 360`. All point at the same missing capability: runtime re-registration of internal alias slots (`log_access`, `not_found_handler`, `method_not_allowed_handler`, `internal_error_handler`, `auth_handler`). Land that capability or strike the speculation.

**Action Items:**
- [x] Decide: (a) add a `webserver::set_alias(hook_phase, alias_id, hook_action_fn)` runtime setter that re-points the relevant alias slot under the existing `shared_mutex`, with replacement semantics matching the construction-time alias (last-position, single-slot), or (b) remove the forward-debt comments and pin "aliases are immutable after `webserver::start()`" in the docs. **→ Option (b) selected. Rationale: zero demand signal (no examples, no PRD, no test reaches for runtime replacement); option (a) would hide two different mechanisms behind one façade (`log_access`/`internal_error_handler` are single-slot members; `auth_handler`/`not_found_handler`/`method_not_allowed_handler` are detached vector entries); DR-012 already framed aliases as "documented sugar that internally registers a hook" with no "open follow-ups" entry for runtime replacement; the public runtime extension surface IS `add_hook()` + `hook_handle`. See `.groundwork-plans/TASK-066-plan.md` for the full plan.**
- [x] If (a): ~~implement the setter, document it in the architecture doc (§4.10), add a hook-bus integration test covering concurrent alias replacement under load, and remove all five forward-debt comments.~~ Not selected.
- [x] If (b): remove the five comments and add a documentation note (in `hook_handle.hpp` and the §4.10 architecture page) that alias slots are construction-time-only. **Done.** Removed seven comment blocks (the spec's five plus two more found in `webserver_aliases.cpp` and the duplicated `fire_handler_exception` tail). Added the docstring paragraph to `hook_handle.hpp` and the "Alias mutability" paragraph to §4.10.
- [x] Update `test/unit/hooks_log_access_alias_slot_test.cpp` to reflect the chosen semantics — coordinated with TASK-085. **Done.** Replaced the misleading `log_access_second_registration_replaces_first` test with two real contract pins: `log_access_alias_is_immutable_after_construction` and `handler_exception_alias_is_immutable_after_construction`. TASK-085's action item 3 ("update this test once TASK-066 ships") is satisfied here.

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

**Status:** Done
