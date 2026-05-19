### TASK-041: Rewrite `README.md`

**Milestone:** M6 - Release Readiness
**Component:** Documentation
**Estimate:** L

**Goal:**
Replace v1's README with a v2.0-only document that introduces the new API surface, threading contract, error-propagation contract, and feature-availability behavior.

**Action Items:**
- [ ] Top-of-README: 10-line "Hello, world" snippet (the same one as `examples/hello_world.cpp`).
- [ ] Sections:
  - Build / install (C++20 floor; RHEL 9 `gcc-toolset-14` note)
  - Hello world — lambda form
  - Class-form handlers (when to reach for `http_resource`)
  - Request: `string_view` getters, lifetime contract, TLS accessors
  - Response: factories + fluent `with_*`
  - Routing: `register_path` / `register_prefix`, parameterized paths, `route()` for runtime methods
  - Threading contract (DR-008 distilled — concurrent invocation, `stop()` deadlock from handler)
  - Error propagation (DR-009 distilled — exceptions land at `internal_error_handler`)
  - Feature availability — `features()`, `feature_unavailable`, build-flag mapping table
  - WebSocket
  - Migrating from v1 (one-paragraph pointer to RELEASE_NOTES.md)
- [ ] Cross-link to `examples/` and `RELEASE_NOTES.md`.
- [ ] Remove every v1-era reference (raw pointers, `no_*` setters, `sweet_kill`, `*_response` subclasses).

**Dependencies:**
- Blocked by: TASK-031, TASK-032, TASK-040
- Blocks: TASK-042, TASK-043

**Acceptance Criteria:**
- README renders cleanly on GitHub.
- Hello-world snippet matches `examples/hello_world.cpp` byte-for-byte.
- Threading and error-propagation sections accurately reflect §5.1, §5.2, DR-008, DR-009.
- Typecheck passes.

**Related Requirements:** PRD §2 documentation NFR
**Related Decisions:** §13 documentation deliverable, AR-006, AR-007

**Status:** Not Started
