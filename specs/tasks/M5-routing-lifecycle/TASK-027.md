### TASK-027: 3-tier route table (hash + radix + regex) with LRU cache

**Milestone:** M5 - Routing, Lifecycle, Builder & Features
**Component:** Route table
**Estimate:** XL

**Goal:**
Replace v1's three maps with the architecture-mandated 3-tier structure: `unordered_map` for exact paths, radix tree for parameterized + prefix, regex chain for fallback, all behind a 256-entry LRU cache.

**Action Items:**
- [x] In `webserver_impl`, define:
  - `std::unordered_map<std::string, route_entry> exact_routes_;`
  - `radix_tree<route_entry> param_and_prefix_routes_;` (bespoke segment-trie in `src/httpserver/detail/radix_tree.hpp`; per §4.7 the spec commits only to outer shape)
  - `std::vector<std::pair<std::regex, route_entry>> regex_routes_;`
- [x] `route_entry` carries: `method_set methods`, `std::variant<lambda_handler, std::shared_ptr<http_resource>> handler`, `bool is_prefix`. (Already shipped by TASK-025.)
- [x] `std::shared_mutex route_table_mutex_` protects all three structures (writer lock for register, reader for lookup).
- [x] LRU cache: `std::list<cache_entry>` + `std::unordered_map<key, list_iterator>` under a separate `std::mutex` (encapsulated in `detail::route_cache`). 256 entries.
- [x] Lookup order: cache → exact → radix → regex. Hits at any tier promote into the cache. (Implemented in `webserver_impl::lookup_v2`; pinned by `lookup_pipeline` test.)
- [x] Implement parameterized-path extraction (`/users/{id}` populates `req.get_path_pieces()` accordingly). (Radix tree captures parameters; pinned by `lookup_pipeline::parameterized_path_hits_radix_tier_and_captures`.)
- [x] Implement prefix matching for `register_prefix`. (Radix tree `prefix_terminus_`; pinned by `route_table::radix_tree_prefix_match_serves_subpaths_and_bare_path` and `lookup_pipeline::prefix_path_hits_radix_tier_and_serves_subpaths`.)

**Implementation notes (Sebastiano, 2026-05-10):**
- The v2 3-tier table is populated alongside (and atomically with) the
  v1 maps. The dispatch site in `finalize_answer` continues to use the
  v1 path so this PR is a purely additive change with full back-compat.
  Cycle K (cutting v1 over and demolishing it) is left to a follow-up
  to keep this diff reviewable; the plan §7.5 already anticipated this
  split.
- The microbenchmark (plan §3.6) and the TSan CI matrix variant (§3.7)
  are documented manual gates; both are listed in the plan's risks
  section and tracked as follow-ups outside TASK-027 scope. The
  `route_table_concurrency` test is the on-`make-check` gate for the
  lock-order discipline (table BEFORE cache); a TSan rebuild of that
  same TU is the manual gate documented in its file header.

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

**Status:** In Progress
