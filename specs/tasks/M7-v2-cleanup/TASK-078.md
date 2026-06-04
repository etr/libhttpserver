### TASK-078: Resolve commented-out CONNECT-method test bodies

**Milestone:** M7 - v2 Cleanup
**Component:** `test/integ/basic.cpp`
**Estimate:** S

**Goal:**
`test/integ/basic.cpp:821-830, 885-896` carry `/* … */`-commented-out CONNECT-method test bodies with no tracking issue. `basic.cpp:2895` (`validator_builder` test) only asserts the server boots; the validator hook is "stored but not currently called in webserver". Either restore the bodies or remove them with a tracking link.

**Action Items:**
- [ ] Investigate why the CONNECT-method blocks at `basic.cpp:821-830` and `885-896` were commented out (git log of the lines). If the commented behaviour is no longer achievable under v2 dispatch, delete the blocks and add a one-line note to `test/REGRESSION.md` recording the v1-only feature.
- [ ] If the blocks should still pass under v2, uncomment them and fix any breakage.
- [ ] For the `validator_builder` test at `basic.cpp:2895`: either wire the validator hook into the dispatch path so the test exercises real behaviour, or delete the test and note in `RELEASE_NOTES.md` that `validator_builder` is a v1 surface with no v2 semantics.

**Dependencies:**
- Blocked by: None
- Blocks: None

**Acceptance Criteria:**
- No `/* … */`-commented test bodies remain in `basic.cpp` without an inline tracking link.
- The `validator_builder` test either exercises real validation or is removed with a documented rationale.
- Typecheck passes.
- Tests pass.

**Related Requirements:** PRD §2 test reliability NFR
**Related Decisions:** None new

**Status:** Backlog
