### TASK-046: Fire `connection_opened` / `connection_closed` / `accept_decision`

**Milestone:** M5 - Routing, Lifecycle, Builder & Features
**Component:** Hook bus
**Estimate:** S

**Goal:**
Wire the three connection-level phases into the existing MHD callback sites. Closes long-standing feature request #332 (banned-IP log entry).

**Action Items:**
- [x] In `webserver_impl::connection_notify`, fire `connection_opened` on `MHD_CONNECTION_NOTIFY_STARTED` and `connection_closed` on `MHD_CONNECTION_NOTIFY_CLOSED`. Lives in the new `src/detail/webserver_callbacks_lifecycle.cpp` (split out of `webserver_callbacks.cpp` to stay under FILE_LOC_MAX). Closure pointer for `MHD_OPTION_NOTIFY_CONNECTION` switched from `nullptr` to `parent` (the owning `webserver*`) so the callback can reach `impl_->any_hooks_`.
- [x] In `webserver_impl::policy_callback` (also in `webserver_callbacks_lifecycle.cpp`), at the bottom (after the YES/NO decision is fixed), fire `accept_decision` with `{peer_address, bool accepted, optional<std::string_view> reason}`. `reason` is `"banned"` / `"not-allowed"` / `std::nullopt` for ACCEPT. Decision derivation extracted into anon-ns helper `classify_decision` to stay under CCN.
- [x] Use the per-phase `any_hooks_` short-circuit: a relaxed atomic load, branch out if zero. Inline pattern: `if (impl->any_hooks_[hook_phase::X].load(std::memory_order_relaxed)) impl->fire_X(ctx);` — context is constructed only inside the if-body so the zero-hook path costs one atomic load.
- [x] `fire_*` helpers take a `shared_lock` on `hook_table_mutex_`, snapshot the phase vector into a stack-local `std::vector<phase_entry<Sig>>` with `reserve(8)`, release the lock, iterate with try/catch. Implemented as three `noexcept` members of `webserver_impl` in `src/hook_handle.cpp`. Mirrors the TASK-027 route-cache promotion pattern.
- [x] Catch any exception thrown by a hook callable; route it through `log_dispatch_error` with a `"hook[<phase>] threw: ..."` prefix. Non-`std::exception` caught via `catch (...)`. For `accept_decision` specifically, the structural guarantee holds: `fire_accept_decision` returns void and `decision` is captured in a local before the fire call, so a throwing hook cannot reach the `return decision;` branch.

**Public-header change:** `accept_ctx` extended to `{peer_address peer, bool accepted, std::optional<std::string_view> reason}` (TASK-045 had only `peer`). `reason` always references a string literal with static storage duration.

**New tests (4):**
- `test/unit/hooks_accept_ctx_shape_test.cpp` — compile-time pin for the extended shape.
- `test/integ/hooks_connection_lifecycle.cpp` — drives a curl round-trip and asserts the three lifecycle hooks fire (accepted=true, valid peer).
- `test/integ/hooks_accept_decision_banned.cpp` — blocks `127.0.0.1`, asserts hook observes accepted=false reason="banned" (closes #332).
- `test/integ/hooks_accept_decision_throwing.cpp` — two sub-tests pinning that a throwing hook does not flip the decision.

**New example:** `examples/banned_ip_log.cpp` — minimal program demonstrating the solution to issue #332.

**Updated tests:** `test/integ/hooks_no_firing.cpp` narrowed: still asserts zero invocations on the eight phases TASK-047..051 will wire, but lets the three lifecycle phases fire (they must, per the new tests).

**Dependencies:**
- Blocked by: TASK-045
- Blocks: TASK-052

**Acceptance Criteria:**
- New integ test `hooks_connection_lifecycle`: opens a connection, registers one hook on each of the three phases, observes (`connection_opened`, `accept_decision{accepted=true}`, `connection_closed`) firing in order.
- New integ test `hooks_accept_decision_banned`: with the IP ban list populated and the default policy ACCEPT, an `accept_decision` hook observes `accepted=false, reason="banned"`. Demonstrated as the solution to #332 in `examples/banned_ip_log.cpp`.
- A throwing `accept_decision` hook does not flip the accept/reject decision (the connection is still rejected per `policy_callback`'s return value).
- TSan-clean under the existing `build-type: tsan` CI matrix entry.
- Typecheck passes.
- Tests pass.

**Related Requirements:** PRD-HOOK-REQ-002, PRD-HOOK-REQ-005, PRD-HOOK-REQ-008
**Related Decisions:** DR-012, §4.10
**Resolves:** Issue #332

**Status:** Done
