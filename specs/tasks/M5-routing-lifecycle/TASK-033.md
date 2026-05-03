### TASK-033: `create_webserver` builder cleanup

**Milestone:** M5 - Routing, Lifecycle, Builder & Features
**Component:** `create_webserver`
**Estimate:** L

**Goal:**
Halve the builder's surface by collapsing each paired `foo()/no_foo()` to `foo(bool = true)`, and validate inputs at the build step.

**Action Items:**
- [ ] Inventory every `no_*` setter in `create_webserver.hpp` (`no_ssl`, `no_debug`, `no_pedantic`, `no_basic_auth`, `no_digest_auth`, `no_deferred`, `no_regex_checking`, `no_ban_system`, `no_post_process`, `no_single_resource`, `no_ipv6`, `no_dual_stack`, etc.).
- [ ] Replace each with a single `foo(bool enable = true)` setter; remove the corresponding `no_foo()`.
- [ ] Validate at the setter (or at `webserver` construction) and throw `std::invalid_argument` with a descriptive message:
  - port > 65535
  - threads < 0
  - any setter receiving an obviously bogus value (negative timeouts, zero buffer sizes, etc.)
- [ ] Update internal callers, tests, and examples to use the new boolean-arg form.
- [ ] Confirm `create_webserver.hpp` line count drops by ≥30% (PRD §3.3 acceptance).

**Dependencies:**
- Blocked by: TASK-006, TASK-014
- Blocks: TASK-034

**Acceptance Criteria:**
- `grep -E '^\s*create_webserver& no_' src/httpserver/create_webserver.hpp` returns 0 (PRD §3.3 acceptance).
- `create_webserver.hpp` line count ≥30% lower than v1 baseline.
- A test passing port 70000 to a setter throws `std::invalid_argument` whose message names the offending parameter.
- Typecheck passes.
- Tests pass.

**Related Requirements:** PRD-CFG-REQ-001, PRD-CFG-REQ-002, PRD-CFG-REQ-003, PRD-CFG-REQ-004
**Related Decisions:** §4.9

**Status:** Not Started
