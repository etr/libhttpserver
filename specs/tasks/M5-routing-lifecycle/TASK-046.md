### TASK-046: Fire `connection_opened` / `connection_closed` / `accept_decision`

**Milestone:** M5 - Routing, Lifecycle, Builder & Features
**Component:** Hook bus
**Estimate:** S

**Goal:**
Wire the three connection-level phases into the existing MHD callback sites. Closes long-standing feature request #332 (banned-IP log entry).

**Action Items:**
- [ ] In `webserver_impl::connection_notify` (`webserver.cpp:1295`), fire `connection_opened` on `MHD_CONNECTION_NOTIFY_STARTED` and `connection_closed` on `MHD_CONNECTION_NOTIFY_CLOSED`. Context: `peer_address`, the `connection_state*` already allocated on STARTED.
- [ ] In `webserver_impl::policy_callback` (`webserver.cpp:1450`), at the bottom (after the YES/NO decision is fixed), fire `accept_decision` with `{peer_address, bool accepted, optional<std::string_view> reason}`. `reason` is `"banned"` / `"not-allowed"` / `std::nullopt` for ACCEPT.
- [ ] Use the per-phase `any_hooks_` short-circuit: a relaxed atomic load, branch out if zero. The body of each firing site is a single `if (impl_->any_hooks_[hook_phase::X].load(std::memory_order_relaxed)) impl_->fire_X(...);` to keep the inline code path tiny.
- [ ] `fire_*` helpers take a `shared_lock` on `hook_table_mutex_`, copy the phase vector into a small `std::vector` on the stack (typical N is small; reserve(8) is fine), release the lock, iterate. Mirror the TASK-027 pattern for route-cache promotion.
- [ ] Catch any exception thrown by a hook callable; route it through the existing `log_dispatch_error` helper (DR-009 §5.2). For `accept_decision` specifically, a throwing hook does NOT change the accept/reject decision — the decision was made before the hook fired.

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

**Status:** Not Started
