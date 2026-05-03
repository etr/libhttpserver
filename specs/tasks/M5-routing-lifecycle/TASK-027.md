### TASK-027: 3-tier route table (hash + radix + regex) with LRU cache

**Milestone:** M5 - Routing, Lifecycle, Builder & Features
**Component:** Route table
**Estimate:** XL

**Goal:**
Replace v1's three maps with the architecture-mandated 3-tier structure: `unordered_map` for exact paths, radix tree for parameterized + prefix, regex chain for fallback, all behind a 256-entry LRU cache.

**Action Items:**
- [ ] In `webserver_impl`, define:
  - `std::unordered_map<std::string, route_entry> exact_routes_;`
  - `radix_tree<route_entry> param_and_prefix_routes_;` (implement or vendor a small radix tree; the architecture commits to outer shape, not implementation)
  - `std::vector<std::pair<std::regex, route_entry>> regex_routes_;`
- [ ] `route_entry` carries: `method_set methods`, `std::variant<lambda_handler, std::shared_ptr<http_resource>> handler`, `bool is_prefix`.
- [ ] `std::shared_mutex route_table_mutex_` protects all three structures (writer lock for register, reader for lookup).
- [ ] LRU cache: `std::list<cache_entry>` + `std::unordered_map<key, list_iterator>` under a separate `std::mutex route_cache_mutex_`. 256 entries.
- [ ] Lookup order: cache → exact → radix → regex. Hits at any tier promote into the cache.
- [ ] Implement parameterized-path extraction (`/users/{id}` populates `req.get_path_pieces()` accordingly).
- [ ] Implement prefix matching for `register_prefix`.

**Dependencies:**
- Blocked by: TASK-005, TASK-014, TASK-021, TASK-024, TASK-025, TASK-026
- Blocks: TASK-028, TASK-031, TASK-032, TASK-036

**Acceptance Criteria:**
- Microbenchmark: exact-path lookup on a warm cache faster than v1's equivalent (no regression).
- Concurrent registration + lookup stress test (per DR-007 / DR-008) shows no deadlock or data race under TSan.
- Path-piece extraction populates `http_request` correctly for parameterized routes.
- Typecheck passes.
- Tests pass.

**Related Requirements:** PRD-HDL-REQ-002, PRD-HDL-REQ-004
**Related Decisions:** DR-007, §4.7, §5.1

**Status:** Not Started
