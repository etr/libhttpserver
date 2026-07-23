### TASK-001: Bump C++ standard floor to C++20

**Milestone:** M1 - Foundation
**Component:** Build system
**Estimate:** M

**Goal:**
Compile the entire library and test suite under C++20 so all subsequent v2.0 work can rely on concepts, `std::span`, `<bit>`, designated initializers, and `std::pmr` without per-feature gates.

**Action Items:**
- [x] Set `AX_CXX_COMPILE_STDCXX([20], [noext], [mandatory])` (or equivalent) in `configure.ac`.
- [x] Update `Makefile.am`'s `AM_CXXFLAGS` to require `-std=c++20`; remove any `-std=c++11`/`-std=c++17` overrides in subdirectories.
- [x] Verify the test suite still compiles and links on the maintainer's primary toolchain (Apple Clang and a recent GCC).
- [x] Document the C++20 floor and the RHEL 9 `gcc-toolset-14` workaround in `INSTALL` / `README` build prerequisites (full doc rewrite happens in M6; this task only needs a one-line note).
- [x] Confirm CI (`.travis.yml` / GitHub Actions / whatever the repo runs) selects a compiler new enough to compile C++20.

**Dependencies:**
- Blocked by: None
- Blocks: TASK-002, every subsequent task

**Acceptance Criteria:**
- `./configure && make` succeeds with the new standard floor on at least one supported toolchain.
- `make check` passes (existing v1 test suite still green).
- `grep -RE '\-std=(c\+\+11|c\+\+14|c\+\+17|gnu\+\+(11|14|17))' configure.ac Makefile.am src test` returns no results.
- Typecheck passes.

**Related Requirements:** PRD §2 NFR (modern C++ idioms)
**Related Decisions:** DR-001

**Status:** Done
