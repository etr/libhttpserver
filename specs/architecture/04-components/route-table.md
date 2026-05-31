### 4.7 Route table

**Responsibility:** Map (method, path) → handler entry. Support exact paths, parameterized paths (`/users/{id}`), prefix matches (`register_prefix`), and regex routes.

**Implementation:** Three structures, queried in order:

1. **Hash map** `std::unordered_map<std::string, route_entry>` for **exact paths**. O(1) amortized lookup.
2. **Radix tree** for **parameterized paths and prefix matches**. Single tree handles both cases (a prefix entry is a tree node marked as prefix-terminating; a parameterized segment is a wildcard child). O(L) lookup where L is path length. Per-segment children at each radix node are stored in `std::map<std::string, std::unique_ptr<radix_node>, std::less<>>` — see the hash-flooding note below for why `std::map` and not `std::unordered_map`.
3. **Regex chain** `std::vector<std::pair<std::regex, route_entry>>` for **regex routes**. Linear fallback when neither hash nor radix matches.

A `route_entry` carries:
- `method_set methods` — which methods this entry serves
- `std::variant<lambda_handler, std::shared_ptr<http_resource>>` — the actual handler (lambda or class)
- `bool is_prefix` — radix node bookkeeping

**Cache:** an LRU cache (256 entries) sits in front of all three structures, keyed by full path (and method, for per-method-handler entries). After warm-up, hot paths bypass even the hash lookup.

**Concurrency:** all three structures are protected by a `std::shared_mutex` (`route_table_mutex_`). Registration grabs the writer lock; lookup grabs the reader lock. The LRU cache uses a separate `std::mutex` (`route_cache_mutex_`) for its list/map pair (insertion/promotion mutate; reads under a shared_mutex would deadlock with the writer-on-full path — keep it simple with a plain mutex).

**Lock order:** `route_table_mutex_` is acquired BEFORE `route_cache_mutex_` whenever both are held. The lookup pipeline never holds both at once: it walks the tier chain under a shared lock on the table, releases that lock, then takes the cache mutex briefly to install/promote the hit. Registration takes the table writer lock, releases it, and only then clears the cache.

**Prefix-vs-exact collision detection:** registering an exact route at a path that already has a prefix terminus (or vice versa) throws `std::invalid_argument` at registration time rather than silently double-registering. The cache key `(method, path)` cannot distinguish the two kinds at lookup time, so the conflict is rejected at the source. The guard probes both storage locations (`exact_routes_` and the radix tree's `exact_terminus_` / `prefix_terminus_`) before any mutation, so the atomicity contract — "a failed registration leaves the table exactly as it was" — still holds.

**Hash-flooding immunity (CWE-407):** the radix tree's per-segment children are kept in `std::map` rather than `std::unordered_map`. URL path segments are attacker-controlled, and neither libc++ nor libstdc++ seed `std::hash<std::string>` by default — a crafted sibling-key corpus can degrade `std::unordered_map::find` from O(1) amortized to O(n) per probe, opening an algorithmic-complexity DoS vector. The `std::map` (red-black tree) gives O(log n) worst case with no hashing in the loop. The transparent comparator `std::less<>` lets the hot-path lookup pass `std::string_view` keys directly without constructing a temporary `std::string` per probe. Typical URL trees branch shallowly (< 10 children per node), so the constant-factor difference from hashing is dominated by the per-segment string compare either way; the cache-miss radix path stays well under the 5 µs ceiling (to be enforced by `test/bench_route_lookup.cpp` once TASK-053 lands and wires `lookup_v2` into dispatch).

**Future evolution:** if `std::map` probe cost dominates measured lookup time at high fanout, switching to an in-tree small-vector flat_map remains an internal-only optimization. v2.0 commits only to the *outer shape* (three-tier with cache), not the per-node container choice.

**Related requirements:** PRD-HDL-REQ-002, PRD-HDL-REQ-004, PRD-HDL-REQ-006.

**Implementation status:** TASK-025 introduced `detail::route_entry` and the `lambda_resource` shim into the existing v1 three-map storage shape. TASK-027 wired `route_entry` into the full 3-tier table described above (hash map for exact paths, radix tree for parameterized/prefix paths, regex chain for regex routes). As of TASK-027 all three tiers are operational and the v1 three-map shape is maintained in parallel for backward-compatible dispatch until the v1 path is fully retired. TASK-056 swapped the radix-node child container from `std::unordered_map` to `std::map<…, std::less<>>` for CWE-407 immunity and added registration-time detection of prefix-vs-exact terminus collisions (`reject_terminus_collision`).

---
