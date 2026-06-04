### TASK-084: Re-measure libstdc++/Linux v1 baseline for `get_headers` ns/call

**Milestone:** M7 - v2 Cleanup
**Component:** `test/v1_baseline/v1_constants.hpp`, `test/PERFORMANCE.md`
**Estimate:** S

**Goal:**
`test/v1_baseline/v1_constants.hpp:65-70, 94-98` + `test/PERFORMANCE.md:34` —
the libstdc++/Linux v1 `get_headers()` ns/call constant was never re-measured;
the libc++ value (760 ns) is reused on both stdlibs. Sizeof constants are
per-stdlib; only the ns/call constant is mono-platform. Capture the missing
measurement so the TASK-039 ≥10× speedup gate has a real Linux baseline.

**Action Items:**
- [ ] On a representative Linux x86-64 host with libstdc++ (Ubuntu 22.04, the verify-build.yml default), run `bench_get_headers` against a clean v1 checkout (or via the existing v1-baseline harness). Capture median, p99, n.
- [ ] Update `v1_constants.hpp:65-70` and `94-98` with the new libstdc++ value, separating it from the libc++ value.
- [ ] Update `test/PERFORMANCE.md:34` with the per-stdlib table.
- [ ] Re-run TASK-039's `≥10× speedup` assertion against the new baseline on the Linux lane and confirm it still passes.

**Dependencies:**
- Blocked by: TASK-039 (Done)
- Blocks: None

**Acceptance Criteria:**
- `v1_constants.hpp` carries separate ns/call values for libc++ and libstdc++.
- The `≥10× speedup` assertion in TASK-039 selects the right baseline per stdlib at compile time.
- `test/PERFORMANCE.md:34` documents the measurement methodology and the per-stdlib values.
- Typecheck passes.
- Tests pass.

**Related Requirements:** PRD §3.6 performance acceptance, PRD-REQ-REQ-001
**Related Decisions:** None new

**Status:** Backlog
