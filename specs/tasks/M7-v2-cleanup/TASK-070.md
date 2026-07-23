### TASK-070: Migrate `hook_table_` to `std::atomic<std::shared_ptr<T>>` (C++20 cleanup)

**Milestone:** M7 - v2 Cleanup
**Component:** `src/http_resource.cpp`
**Estimate:** M

**Goal:**
`src/http_resource.cpp:81-115` carries `TODO(C++20 cleanup): migrate hook_table_ to std::atomic<std::shared_ptr<T>>` paired with a `-Wdeprecated-declarations` push/pop suppression. C++20 is the minimum standard (TASK-001), so the migration is now in-scope; landing it lets us drop the suppression and shed a `std::atomic_load`/`std::atomic_store` legacy pair.

**Action Items:**
- [ ] Replace the `std::shared_ptr<hook_table>` field + free-function `std::atomic_load/store` access with `std::atomic<std::shared_ptr<hook_table>>` per [P0718R2].
- [ ] Update all readers and writers (registration path, dispatch path) to call `.load()` / `.store()` / `.exchange()` on the atomic directly.
- [ ] Remove the paired `#pragma GCC diagnostic push` / `ignored "-Wdeprecated-declarations"` / `pop` around the legacy free-function calls.
- [ ] Verify TSan-clean under existing CI matrix.
- [ ] Remove the TODO comment.

**Dependencies:**
- Blocked by: TASK-001 (C++20 floor, Done), TASK-051 (per-route hooks, Done), libc++ P0718R2 (Apple Clang/libc++ still lacks `std::atomic<std::shared_ptr<T>>` — recheck each LLVM release; see project memory)
- Blocks: None
- Spun off: TASK-094 (Done) — delivers the TSan stress harness for the per-resource CAS path ahead of the migration

**Acceptance Criteria:**
- `grep -nE 'atomic_load|atomic_store' src/http_resource.cpp` returns no matches.
- `grep -nE 'Wdeprecated-declarations' src/http_resource.cpp` returns no matches.
- Stress test `threadsafety_stress` extended with hook table swap remains TSan-clean.
  *(Satisfied out-of-band by TASK-094 — Done. TASK-094 added Sub-test D that stress-tests the legacy CAS path under TSan. When TASK-070 eventually lands, Sub-test D is the required regression net for the migrated atomic path.)*
- Typecheck passes.
- Tests pass.

**Related Requirements:** PRD §2 modern C++ NFR
**Related Decisions:** DR-001 (C++20)

**Status:** Backlog
