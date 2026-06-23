### TASK-083: Wire real CI gates into benchmarks (hook overhead, warm path, route lookup)

**Milestone:** M7 - v2 Cleanup
**Component:** `test/bench_*.cpp`
**Estimate:** M

**Goal:**
Three benches have soft or absent acceptance gates:
- `test/bench_hook_overhead.cpp:121-128` — the relative `2×-of-HOOK_BASELINE_NS` gate the task spec asked for was punted; an absolute 50 ns ceiling is used instead.
- `test/bench_warm_path.cpp:45-46, 278-280` — no pass/fail ceilings, manual before/after comparison.
- `test/bench_route_lookup.cpp:222-227` — measures a "cache-warm + radix" mix rather than pure radix-tier latency.
- `test/bench_harness.hpp:58-72` — MSVC sink known to be elidable; robust `_ReadWriteBarrier()` alternative punted.

Land the gates that were asked for in TASK-052 and TASK-053, separate the bench_route_lookup measurement, and harden the MSVC sink.

**Action Items:**
- [x] `bench_hook_overhead`: implement the relative `2× HOOK_BASELINE_NS` gate per TASK-052 acceptance. Compute `HOOK_BASELINE_NS` in the no-hooks variant of the same bench run (not a hardcoded constant) so the gate auto-tracks runner speed. Keep the absolute 50 ns ceiling as a sanity bound.
- [x] `bench_warm_path`: add `>= 5% improvement vs baseline` pass/fail per TASK-058 acceptance. Use a versioned `BASELINE_NS` per-platform constant header, refreshed deliberately (see TASK-084).
- [x] `bench_route_lookup`: split into two measurements — `cache_warm_ns` (cache hit) and `radix_pure_ns` (cache cold, radix only). Each carries its own gate (≤ 200 ns and ≤ 5 µs from TASK-053).
- [x] `bench_harness.hpp`: replace the MSVC sink with `_ReadWriteBarrier()` + a `volatile` pointer write, mirroring the gcc/clang `asm volatile("" :: "g"(x) : "memory")` pattern. Document why the previous sink was elidable.
- [x] Wire the new gates into `bench_targets` in `test/Makefile.am`. Bench runs stay opt-in from `make check`.

**Dependencies:**
- Blocked by: TASK-052 (Done), TASK-053 (Done), TASK-058 (Done)
- Blocks: None

**Acceptance Criteria:**
- `bench_hook_overhead` fails when overhead exceeds `2× HOOK_BASELINE_NS`.
- `bench_warm_path` fails when warm-path latency regresses ≥ 5% vs baseline.
- `bench_route_lookup` emits two named measurements with separate gates.
- MSVC sink survives `/O2` (verified by disassembly snippet in the commit message).
- Typecheck passes.
- Tests pass.

**Related Requirements:** PRD §3.6 performance acceptance, PRD-HOOK-REQ-008 (zero-cost when unused)
**Related Decisions:** §4.5 routing, DR-012

**Status:** Done
