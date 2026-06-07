### TASK-094: Extend `threadsafety_stress` with per-resource `add_hook` CAS race

**Milestone:** M7 - v2 Cleanup
**Component:** `test/integ/threadsafety_stress.cpp`, `src/http_resource.cpp`
**Estimate:** S

**Goal:**
Spun off from TASK-070, which is blocked on libc++ implementing P0718R2 (see TASK-070 "Blocker" section). One acceptance-criterion piece of TASK-070 — *"Stress test `threadsafety_stress` extended with hook table swap remains TSan-clean"* — does **not** depend on the migration and can land today. The existing stress test (`test/integ/threadsafety_stress.cpp`, comment at lines 25–30) claims to cover the `route_table_mutex_ → resource hook_table → server-wide hook_table` lock order under TSan, but in practice it only hammers `webserver::add_hook` + `hook_handle::remove` (the M5/TASK-052 server-side surface). The per-resource lazy CAS path in `src/http_resource.cpp:93-110` (`ensure_table()` — `std::atomic_load_explicit` + `std::atomic_compare_exchange_strong_explicit` on a `std::shared_ptr<detail::resource_hook_table>` field) is never driven by the test. This task closes that gap so we (a) gain regression coverage on the current legacy implementation **today** and (b) seed a ready-made stress harness for the day TASK-070 unblocks.

**Action Items:**
- [x] Add a new stress phase to `test/integ/threadsafety_stress.cpp` that targets the lazy CAS path in `http_resource::ensure_table()` (`src/http_resource.cpp:93-110`). The phase must repeatedly: (a) construct a fresh `http_resource` subclass whose `hook_table_` is null, (b) launch N concurrent threads (≥8) that each call `http_resource::add_hook` so they race the `atomic_compare_exchange_strong_explicit` on a null slot, (c) follow with mixed add/remove load against the now-installed table to exercise concurrent registration + dispatch.
- [x] Sequence the new phase alongside the existing webserver-side hook ops so a single run exercises both lock-order tiers (`webserver::add_hook` ↔ `http_resource::add_hook`).
- [x] Verify the phase actually drives `ensure_table()` (e.g., assert that at least one CAS loser occurs across the run — via either a debug counter behind `HTTPSERVER_TEST_INSTRUMENTATION` or by inspecting `hook_table_raw_()` from a witness thread). Acceptable if the assertion only runs in TSan/instrumented lanes.
- [x] Run the extended `threadsafety_stress` under the existing TSan lane and confirm zero warnings.
- [x] Update the test header comment (lines 23–34) to accurately describe the resource-side coverage that was previously claimed but not delivered.
- [x] Do **not** modify `src/http_resource.cpp` semantics. The migration stays parked in TASK-070; this task is test-only (plus the optional test-only instrumentation counter, if used).

**Dependencies:**
- Blocked by: None (the legacy CAS path is what this task stresses; it exists today).
- Blocks: None. Unblocks one acceptance criterion of TASK-070 ahead of time — when TASK-070 eventually lands, this stress phase is the regression net it requires.

**Acceptance Criteria:**
- Extended `threadsafety_stress` runs TSan-clean under the existing CI matrix (Linux libstdc++ TSan lane is authoritative; macOS lane informational).
- New phase observably drives the CAS in `ensure_table()`: either an instrumented counter shows ≥1 CAS-loser across a representative run, or a documented design rationale explains why the schedule provably hits the contended-null window.
- `grep -n "resource hook_table" test/integ/threadsafety_stress.cpp` returns at least one comment line that accurately describes the new coverage (replacing the previous overclaim).
- `src/http_resource.cpp` is unchanged except for at most one optional `#ifdef HTTPSERVER_TEST_INSTRUMENTATION` counter increment in `ensure_table()` if the visibility approach chosen needs it.
- Typecheck passes.
- All tests pass.

**Related Requirements:** PRD §2 modern C++ NFR; PRD §5 concurrency NFR (lock-order documentation).
**Related Decisions:** DR-001 (C++20); the `route_table_mutex_ → resource hook_table → server-wide hook_table` lock order documented in `specs/architecture/05-cross-cutting.md` §5.6 (recorded by TASK-051).

**Status:** Done
