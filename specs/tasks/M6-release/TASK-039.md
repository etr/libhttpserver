### TASK-039: Performance acceptance — `get_headers()` and `sizeof(http_resource)`

**Milestone:** M6 - Release Readiness
**Component:** Microbenchmarks
**Estimate:** M

**Goal:**
Verify the two PRD §3.6 numeric acceptance criteria with reproducible microbenchmarks.

**Action Items:**
- [ ] Write `test/bench_get_headers.cpp`: tight loop calling `req.get_headers()` on a request with 16 headers, measured under v1 (separate branch / vendored snapshot) and v2.0; report ratio.
- [ ] Verify v2.0 is ≥10× faster (PRD §3.6 acceptance).
- [ ] Add `static_assert(sizeof(http_resource) <= sizeof_v1_http_resource - sizeof(std::map<std::string, bool>));` (with a literal numeric upper bound matching the v1 baseline) — or a runtime assertion in a test. This is the verification step for the `sizeof(http_resource)` shrink criterion in TASK-021.
- [ ] Document the methodology and v1 baseline values in `test/PERFORMANCE.md` so future regressions are caught.
- [ ] Wire benchmarks into a `make bench` target (not part of `make check` so they don't slow normal CI).

**Dependencies:**
- Blocked by: TASK-017, TASK-018, TASK-021
- Blocks: None

**Acceptance Criteria:**
- `bench_get_headers` reports ≥10× speedup vs v1 (PRD §3.6 acceptance).
- `sizeof(http_resource)` decreased by at least the cost of an empty `std::map<std::string, bool>` (PRD §3.6 acceptance).
- Both numbers documented in `test/PERFORMANCE.md` with the v1 baseline they were measured against.
- Typecheck passes.
- Tests pass.

**Related Requirements:** PRD-REQ-REQ-001, PRD-REQ-REQ-003 (numeric §3.6 acceptance criteria for these two requirements)
**Related Decisions:** DR-006, §4.4

**Status:** Not Started
