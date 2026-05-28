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
- All seven value-constant macros (`DEFAULT_WS_PORT`, `DEFAULT_WS_TIMEOUT`, `DEFAULT_MASK_VALUE`,
  `NOT_FOUND_ERROR`, `METHOD_ERROR`, `NOT_METHOD_ERROR`, `GENERIC_ERROR`) are absent from
  `src/httpserver/*.hpp`. Note: include guards, the `COMPARATOR` function-like macro, and the
  Windows platform shims (`_WINDOWS`, `_WIN32_WINNT`) are out of scope — they predate this task
  and are not value constants. The PRD §3.3 grep pattern (`grep -E '^\s*#define\s'`) matches
  these excluded forms; they are expected to remain.
- Existing tests that referenced the macros via `<httpserver.hpp>` still resolve through `httpserver::constants::*`.
- Typecheck passes.
- Tests pass.

**Related Requirements:** PRD-CFG-REQ-002
**Related Decisions:** §4.9

**Status:** Done
