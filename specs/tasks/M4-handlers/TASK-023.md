### TASK-023: Smart-pointer `register_resource` overloads

**Milestone:** M4 - Handler & Resource Model
**Component:** `webserver` registration API
**Estimate:** M

**Goal:**
Replace the raw-pointer `register_resource` overload with `unique_ptr` and `shared_ptr` overloads so ownership is explicit at the call site.

**Action Items:**
- [ ] Add `void register_resource(const std::string& path, std::unique_ptr<http_resource> resource);` (move-in ownership; library internally upgrades to `shared_ptr` for thread-safe lookup).
- [ ] Add `void register_resource(const std::string& path, std::shared_ptr<http_resource> resource);` (caller retains a reference).
- [ ] Remove the raw-pointer overload `register_resource(string, http_resource*, bool)`.
- [ ] Update internal route-table entries to hold `std::shared_ptr<http_resource>` (`route_entry`'s variant per §4.7).
- [ ] Update examples and tests to use the new ownership model.

**Dependencies:**
- Blocked by: TASK-014
- Blocks: TASK-024

**Acceptance Criteria:**
- `auto r = std::make_unique<my_resource>(); ws.register_resource("/foo", std::move(r));` compiles and serves.
- The raw-pointer overload no longer exists in the public header.
- A test verifies the resource destructor runs when the webserver is destroyed.
- Typecheck passes.
- Tests pass.

**Related Requirements:** PRD-HDL-REQ-003, PRD-HDL-REQ-005
**Related Decisions:** §4.4, §4.7

**Status:** Not Started
