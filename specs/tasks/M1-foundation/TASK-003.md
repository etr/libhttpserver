### TASK-003: Add `httpserver::feature_unavailable` exception type

**Milestone:** M1 - Foundation
**Component:** Public exception types
**Estimate:** S

**Goal:**
Provide the documented error type users catch when a build-time-disabled feature is invoked, so later tasks can throw it without circular header coupling.

**Action Items:**
- [x] Add a new public header `src/httpserver/feature_unavailable.hpp`.
- [x] Define `class feature_unavailable : public std::runtime_error` with a constructor taking `(std::string_view feature, std::string_view build_flag)` that composes a `what()` message naming both (e.g., `"feature 'tls' unavailable: built without HAVE_GNUTLS"`).
- [x] Re-export from `<httpserver.hpp>`.
- [x] Apply the gate from TASK-002.

**Dependencies:**
- Blocked by: TASK-002
- Blocks: TASK-034

**Acceptance Criteria:**
- `static_assert(std::is_base_of_v<std::runtime_error, httpserver::feature_unavailable>)` passes.
- A unit test catches the exception as `std::runtime_error` and asserts `what()` contains both the feature name and the build flag.
- Typecheck passes.
- Tests pass.

**Related Requirements:** PRD-FLG-REQ-004, PRD-FLG-REQ-005
**Related Decisions:** §7 (feature availability)

**Status:** Done
