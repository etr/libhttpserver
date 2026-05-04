### TASK-035: Smart-pointer `register_ws_resource` overloads

**Milestone:** M5 - Routing, Lifecycle, Builder & Features
**Component:** WebSocket registration
**Estimate:** M

**Goal:**
Mirror the `register_resource` ownership pattern for WebSocket handlers; remove the raw-pointer overload.

**Action Items:**
- [ ] Add `register_ws_resource(const std::string& path, std::unique_ptr<websocket_handler>);` and `(..., std::shared_ptr<websocket_handler>);`.
- [ ] Add `unregister_ws_resource(const std::string& path);` (registration drops; handler destructor runs when last reference goes away).
- [ ] Remove the raw-pointer overload `register_ws_resource(string, websocket_handler*)`.
- [ ] On a `--disable-websocket` build, both overloads throw `feature_unavailable` (consistent with TASK-034).
- [ ] Update any v1 examples or tests using the raw-pointer form.

**Dependencies:**
- Blocked by: TASK-014, TASK-034
- Blocks: None

**Acceptance Criteria:**
- `auto h = std::make_unique<my_ws_handler>(); ws.register_ws_resource("/ws", std::move(h));` compiles and serves WebSocket frames.
- The raw-pointer overload no longer exists.
- A test on a websocket-disabled build verifies both overloads throw `feature_unavailable`.
- Typecheck passes.
- Tests pass.

**Related Requirements:** PRD-HDL-REQ-003, PRD-HDL-REQ-005, PRD-FLG-REQ-002
**Related Decisions:** §4.5, DR-010

**Status:** Not Started
