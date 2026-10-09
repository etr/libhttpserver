# Unworked Review Issues

**Run:** 2026-10-08 19:23:09
**Task:** TASK-167
**Total:** 1 (0 critical, 0 major, 1 minor)

## Minor

1. [ ] **performance-reviewer** | `src/httpserver/detail/qpack_dynamic_table.hpp:113` | algorithmic-complexity
   Absolute-index lookup scans every retained list node from oldest to newest. Dynamic decoding calls this for each referenced field in both admission and materialization passes, giving O(fields * retained_entries) work even for repeated references to the latest entry. A local optimized probe with 256 such fields took approximately 2.3 ms at 1,900 entries and 41.7 ms at 30,000 entries. Both inputs remain within explicit configured bounds; current private/static-only integration and lack of a TASK-167 throughput target make this a nonblocking optimization opportunity.
   *Recommendation:* Consider a bounded absolute-index side index that preserves stable node addresses, or cache resolved references across the two decode passes. Retain eviction/hold lifetime and allocation-failure atomicity tests, and benchmark large configured tables before enabling dynamic QPACK scheduling.
