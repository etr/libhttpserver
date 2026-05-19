### 4.7 Route table

**Responsibility:** Map (method, path) → handler entry. Support exact paths, parameterized paths (`/users/{id}`), prefix matches (`register_prefix`), and regex routes.

**Implementation:** Three structures, queried in order:

1. **Hash map** `std::unordered_map<std::string, route_entry>` for **exact paths**. O(1) amortized lookup.
2. **Radix tree** for **parameterized paths and prefix matches**. Single tree handles both cases (a prefix entry is a tree node marked as prefix-terminating; a parameterized segment is a wildcard child). O(L) lookup where L is path length.
3. **Regex chain** `std::vector<std::pair<std::regex, route_entry>>` for **regex routes**. Linear fallback when neither hash nor radix matches.

A `route_entry` carries:
- `method_set methods` — which methods this entry serves
- `std::variant<lambda_handler, std::shared_ptr<http_resource>>` — the actual handler (lambda or class)
- `bool is_prefix` — radix node bookkeeping

**Cache:** an LRU cache (256 entries) sits in front of all three structures, keyed by full path (and method, for per-method-handler entries). After warm-up, hot paths bypass even the hash lookup.

**Concurrency:** all three structures are protected by a `std::shared_mutex` (`route_table_mutex_`). Registration grabs the writer lock; lookup grabs the reader lock. The LRU cache uses a separate `std::mutex` (`route_cache_mutex_`) for its list/map pair (insertion/promotion mutate; reads under a shared_mutex would deadlock with the writer-on-full path — keep it simple with a plain mutex).

**Lock order:** `route_table_mutex_` is acquired BEFORE `route_cache_mutex_` whenever both are held. The lookup pipeline never holds both at once: it walks the tier chain under a shared lock on the table, releases that lock, then takes the cache mutex briefly to install/promote the hit. Registration takes the table writer lock, releases it, and only then clears the cache.

**Future evolution:** if the radix tree starts to dominate lookup cost (measured), it can be replaced with a different data structure (compressed trie, perfect hash on a frozen route set) without touching the public API. v2.0 commits only to the *outer shape* (three-tier with cache), not the radix-tree implementation choice.

**Related requirements:** PRD-HDL-REQ-002, PRD-HDL-REQ-004, PRD-HDL-REQ-006.

---
