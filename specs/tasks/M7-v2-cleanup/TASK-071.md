### TASK-071: Wire `install_not_found_alias_` stub and remove dead `lambda_handler` arm

**Milestone:** M7 - v2 Cleanup
**Component:** `src/detail/webserver_aliases.cpp`, `src/detail/webserver_dispatch.cpp`
**Estimate:** S

**Goal:**
Two related forward-debt items in the alias/dispatch plumbing:
1. `src/detail/webserver_aliases.cpp:298-309` — `install_not_found_alias_` registers an empty stub callback labelled "structurally deferred (TASK-048)". TASK-048 has shipped; finish the wiring.
2. `src/detail/webserver_dispatch.cpp:260, 313-318` — dead `lambda_handler` variant arm kept "for a future task" that may store lambdas directly. Either land the lambda-storage path or remove the dead arm.

**Action Items:**
- [x] In `install_not_found_alias_`, replace the empty stub callback with the real `route_resolved`-phase hook that builds the 404 response matching v1 behaviour, mirroring the structure of `install_method_not_allowed_alias_` and `install_internal_error_alias_`.
- [x] Add an integ test covering the alias path end-to-end (request a missing route, expect 404 with the alias-built body).
- [x] In `webserver_dispatch.cpp:260, 313-318`: decide whether the `lambda_handler` variant arm is needed. If yes, land the lambda-storage path so the arm is exercised. If no, remove the arm and update the variant's `std::variant` parameter list.
- [x] Update Doxygen on `webserver::route(...)` if the variant shape changed (no public Doxygen referenced the variant; internal code comments updated in route_entry.hpp).

**Dependencies:**
- Blocked by: TASK-048 (route_resolved/before_handler firing, Done)
- Blocks: None

**Acceptance Criteria:**
- `install_not_found_alias_` is wired and produces the same 404 response shape as the v1 `not_found_handler` setter on a fresh `webserver` with no explicit not-found handler.
- The dead `lambda_handler` variant arm either exercises a real code path or no longer exists.
- Typecheck passes.
- Tests pass.

**Related Requirements:** PRD-HOOK-REQ-009 (v1 setters as aliases)
**Related Decisions:** DR-012

**Status:** Done
