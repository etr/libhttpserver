### TASK-074: Gate DEBUG raw-body printing behind explicit opt-in

**Milestone:** M7 - v2 Cleanup
**Component:** `src/detail/webserver_body_pipeline.cpp`
**Estimate:** S

**Goal:**
`webserver_body_pipeline.cpp:199-208` contains a `#ifdef DEBUG` block that prints the raw request body (including credentials and PII) to stdout. The inline comment explicitly warns it must never be widened to release builds. The risk is contained but the suppression mechanism is fragile (a developer could ship a debug build to production). Make the print opt-in via an explicit env var so the default behaviour even in `DEBUG` builds is safe.

**Action Items:**
- [x] Replace the `#ifdef DEBUG` guard with a runtime check on `getenv("LIBHTTPSERVER_DEBUG_DUMP_REQUEST_BODY")`. The block compiles always but only fires when the env var is set; release builds never set the var, so the gate is a one-line runtime check independent of any compile-time `DEBUG` define. Documented in the new `docs/debug-env-vars.md` (a fresh, dedicated home for `LIBHTTPSERVER_*` debug knobs) with a cross-reference from `specs/architecture/10-observability.md`.
- [x] Add a warning log at startup if the env var is set, naming the security risk. Implemented as `httpserver::detail::maybe_warn_debug_dump_request_body()` called from `webserver::start()`. One-shot stderr write (process-wide `std::atomic<bool>` flag) plus best-effort forwarding to `webserver::get_error_logger()` when wired. Message names the env var, the SECURITY WARNING marker, credentials/cookies/PII at risk, and points to `docs/debug-env-vars.md`.
- [x] Update the inline comment to reference the env var rather than `#ifdef DEBUG`. The comment in `webserver_body_pipeline.cpp` now names `LIBHTTPSERVER_DEBUG_DUMP_REQUEST_BODY`, points to `docs/debug-env-vars.md`, and notes that the TASK-057 `operator<<` redaction does not cover this code path.
- [x] Spot-check that release builds with `-DDEBUG` accidentally set still do not print without the env var. Pinned by `test/integ/debug_dump_request_body_unset_test.cpp`, which builds and runs identically on both `--enable-debug` and stock release configurations; the runtime check is the only gate, so the spot-check resolves by construction.

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

**Status:** Done
