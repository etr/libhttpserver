### TASK-006: Replace `#define` constants with `httpserver::constants`

**Milestone:** M1 - Foundation
**Component:** Public constants
**Estimate:** M

**Goal:**
Eliminate macro pollution from public headers by moving every `#define` constant into `constexpr` declarations under the `httpserver::constants` namespace.

**Action Items:**
- [x] Inventory every `#define` in `src/httpserver/*.hpp` (`DEFAULT_WS_PORT`, `DEFAULT_WS_TIMEOUT`, `NOT_FOUND_ERROR`, `METHOD_NOT_ALLOWED_ERROR`, etc.).
- [x] Create `src/httpserver/constants.hpp` defining each as `inline constexpr` of the appropriate type (`std::uint16_t` for ports, `std::string_view` for messages, etc.).
- [x] Update internal callers (in `src/*.cpp`) to use `httpserver::constants::name` instead of the macro.
- [x] Remove the `#define`s from public headers.
- [x] Re-export `constants.hpp` from `<httpserver.hpp>`.

**Dependencies:**
- Blocked by: TASK-002
- Blocks: TASK-033 (builder validation may reference port constants)

**Acceptance Criteria:**
- `grep -E '^\s*#define\s' src/httpserver/*.hpp` returns 0 lines (PRD §3.3 acceptance).
- Existing tests that referenced the macros via `<httpserver.hpp>` still resolve through `httpserver::constants::*`.
- Typecheck passes.
- Tests pass.

**Related Requirements:** PRD-CFG-REQ-002
**Related Decisions:** §4.9

**Status:** Complete
