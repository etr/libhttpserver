### TASK-052: Hook bus documentation, examples, benchmark, stress-test extension

**Milestone:** M5 - Routing, Lifecycle, Builder & Features
**Component:** Hook bus
**Estimate:** L

**Goal:**
Close the hook bus by extending the docs that already shipped in M6 (TASK-040 examples, TASK-041 README, TASK-042 RELEASE_NOTES, TASK-043 Doxygen), adding the zero-cost-when-unused benchmark, and extending the TASK-032 stress test to cover concurrent `add_hook` / `remove`. This is the M6 touch-back task the v2.0 PR plan flagged as "tasks from M6 we'll need to redo."

**Action Items:**

*Examples (extends TASK-040)*
- [ ] `examples/banned_ip_log.cpp` — single `accept_decision` hook writing to a configurable sink. Resolves #332 in user-visible form.
- [ ] `examples/early_413.cpp` — single `request_received` hook returning `hook_action::respond_with(http_response::empty().with_status(413))` when `Content-Length > N`. Resolves #273 in user-visible form.
- [ ] `examples/clf_access_log.cpp` — single `response_sent` hook formatting Common Log Format with `time-taken`. Resolves #281 and #69 in user-visible form.
- [ ] `examples/per_route_auth.cpp` — `http_resource::add_hook(hook_phase::before_handler, ...)` demonstrating a per-route auth scheme that does NOT touch other routes.
- [ ] All four examples added to `examples/Makefile.am` and listed in `examples/README.md` next to the canonical "hello world" / class-form examples.

*README (extends TASK-041)*
- [ ] New top-level section "Lifecycle hooks" between "Handlers" and "Threading". Phase table identical to §4.10. One-paragraph short-circuit explainer. Pointer to the four new examples. Explicit callout: "The v1 setters `log_access`, `not_found_handler`, `method_not_allowed_handler`, `internal_error_handler`, and `auth_handler` are aliases for `add_hook`. They remain for ergonomic call sites; new code can use either form." with a side-by-side equivalence table.

*RELEASE_NOTES.md (extends TASK-042)*
- [ ] New bullet under "What's new in v2.0": "Lifecycle hook bus (`webserver::add_hook` / `http_resource::add_hook`) — 11 phases spanning connection, request, routing, handler, response. Multi-subscriber, server-wide and per-route. The v1 single-slot setters survive as documented aliases. See specs/architecture/04-components/hooks.md."
- [ ] Closes-list addition: #332, #281, #69, #273 (and #272 partial).

*Doxygen (extends TASK-043)*
- [ ] Doxygen blocks on the four new public headers (`hook_phase.hpp`, `hook_action.hpp`, `hook_handle.hpp`, `hook_context.hpp`).
- [ ] Doxygen on `webserver::add_hook` and `http_resource::add_hook` (one overload-group block per file, listing the eleven phases and the per-route restriction set).
- [ ] Update existing Doxygen for `log_access`, `not_found_handler`, `method_not_allowed_handler`, `internal_error_handler`, `auth_handler`: each gets one paragraph identifying it as an alias and naming the hook phase + equivalent `add_hook` call. (Per the user's "let's make sure we highlight in the documentation that they are aliases of hooks.")
- [ ] Update existing Doxygen block on `webserver` class-level threading contract (§5.1) to add a sentence on hook concurrency: "Registered hooks may run concurrently from multiple MHD worker threads; implementations must be thread-safe."

*Benchmark*
- [ ] `test/bench_hook_overhead.cpp`: serves a fixed warm path 1M times with (a) zero hooks registered, (b) one observation hook at `response_sent`. Records median + p99 per-request cost for each. CI gate: (a) within 2× microbench noise of the pre-hook-system baseline; (b) is informational. Mirrors the pattern of `test/bench_get_headers.cpp`.

*Stress test (extends TASK-032)*
- [ ] Extend `test/integ/threadsafety_stress.cpp` with a new fixture mode: in addition to randomly invoking `register_path`/`unregister_path`/`block_ip`/`unblock_ip` from inside handlers, also randomly `add_hook` / `hook_handle::remove` at random phases. TSan-clean under the existing tsan CI matrix entry. Document the rationale in the test file header.

**Dependencies:**
- Blocked by: TASK-045, TASK-046, TASK-047, TASK-048, TASK-049, TASK-050, TASK-051
- Blocks: None

**Acceptance Criteria:**
- The four new examples build (under `make check`) and the README / RELEASE_NOTES references resolve.
- `bench_hook_overhead` passes the (a) gate.
- The extended stress test runs for 60s TSan-clean.
- A documentation-spotcheck script (or manual grep) verifies each of the five alias setters carries the "this is an alias for `add_hook`" callout in its Doxygen block.
- Typecheck passes.
- Tests pass.

**Related Requirements:** PRD-HOOK-REQ-002, PRD-HOOK-REQ-007, PRD-HOOK-REQ-008, PRD-HOOK-REQ-009
**Related Decisions:** DR-012, §4.10, §5.6

**Status:** Not Started
