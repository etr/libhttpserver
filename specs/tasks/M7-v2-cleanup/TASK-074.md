### TASK-074: Gate DEBUG raw-body printing behind explicit opt-in

**Milestone:** M7 - v2 Cleanup
**Component:** `src/detail/webserver_body_pipeline.cpp`
**Estimate:** S

**Goal:**
`webserver_body_pipeline.cpp:199-208` contains a `#ifdef DEBUG` block that prints the raw request body (including credentials and PII) to stdout. The inline comment explicitly warns it must never be widened to release builds. The risk is contained but the suppression mechanism is fragile (a developer could ship a debug build to production). Make the print opt-in via an explicit env var so the default behaviour even in `DEBUG` builds is safe.

**Action Items:**
- [ ] Replace the `#ifdef DEBUG` guard with a runtime check on `getenv("LIBHTTPSERVER_DEBUG_DUMP_REQUEST_BODY")`. The block compiles always but only fires when the env var is set and the build is `DEBUG` (or unconditionally, since prod builds never set the var). Document the env var in `src/httpserver/detail/README.md` (or wherever debug knobs live).
- [ ] Add a warning log at startup if the env var is set, naming the security risk.
- [ ] Update the inline comment to reference the env var rather than `#ifdef DEBUG`.
- [ ] Spot-check that release builds with `-DDEBUG` accidentally set still do not print without the env var.

**Dependencies:**
- Blocked by: None
- Blocks: None

**Acceptance Criteria:**
- Setting `LIBHTTPSERVER_DEBUG_DUMP_REQUEST_BODY=1` produces the same output as today's `#ifdef DEBUG` build.
- Default behaviour (env var unset) is silent on both `RELEASE` and `DEBUG` builds.
- Startup log clearly names the security risk when the env var is set.
- Typecheck passes.
- Tests pass.

**Related Requirements:** PRD §2 security NFR (defense-in-depth)
**Related Decisions:** None new

**Status:** Backlog
