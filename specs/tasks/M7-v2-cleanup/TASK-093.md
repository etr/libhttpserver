### TASK-093: Tighten example caveats (auth, pipe, access-log)

**Milestone:** M7 - v2 Cleanup
**Component:** `examples/`
**Estimate:** S

**Goal:**
The audit's §4 says examples are "clean" but flags six "in production…" caveats
as "defensible but could be tightened". The two most-visible (constant-time
password compare in `per_route_auth`, write-loop hardening in
`pipe_response_example`) are worth landing now because copy-paste from
examples to production code is a known anti-pattern users actually do.

**Action Items:**
- [x] `examples/per_route_auth.cpp:45-49`: replace the non-constant-time `memcmp` / `==` with a constant-time helper (write a 4-line `constant_time_equal(string_view, string_view)` directly in the example, since pulling a dep would defeat the purpose). Keep the production-fix note in the comment but flip it from "fix in production" to "this is the production-ready form".
- [x] `examples/pipe_response_example.cpp:54-57`: wrap the write in a partial-write loop. Keep the comment but mark it "production-ready".
- [x] `examples/clf_access_log.cpp:102`: replace the hard-coded `"HTTP/1.1"` with the protocol the request actually advertised (`get_version()` from TASK-018). If the version is not exposed in the hook context yet, raise a paired note in TASK-052's docs sweep.
- [x] `examples/client_cert_auth.cpp:65`, `examples/centralized_authentication.cpp:51`, `examples/minimal_https_psk.cpp:35`: these are genuinely illustrative; leave as-is but reword the inline comments to say "for illustration; production must …" rather than "in production we would …" so the framing is consistent.

**Dependencies:**
- Blocked by: TASK-018 (Done)
- Blocks: None

**Acceptance Criteria:**
- `per_route_auth.cpp` uses a constant-time compare.
- `pipe_response_example.cpp` handles partial writes correctly.
- `clf_access_log.cpp` emits the real request protocol version.
- All examples build under `make check`.
- Typecheck passes.
- Tests pass.

**Related Requirements:** PRD §2 documentation NFR
**Related Decisions:** None new

**Status:** Done
