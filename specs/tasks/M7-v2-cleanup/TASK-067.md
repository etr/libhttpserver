### TASK-067: Remove v1 `registered_resources*` maps and `namespace compat` shim

**Milestone:** M7 - v2 Cleanup
**Component:** `webserver_impl`, `create_webserver.hpp`
**Estimate:** L

**Goal:**
Three transitional surfaces survive TASK-053's v1→v2 dispatch cutover:
1. The v1 `registered_resources*` maps + mutex at `src/httpserver/detail/webserver_impl.hpp:152-164` (registration-time bookkeeping only, but still a divergence point).
2. The `namespace compat` shim + `[[deprecated]]` overload + paired `-Wdeprecated-declarations` suppression at `src/httpserver/create_webserver.hpp:108-157, 549, 558-571` ("will be removed in the next release").
3. The `prepare_or_create_lambda_shim` / WebSocket-path consumers that still read the v1 maps.

Cut them in a single, well-tested PR so the v2 dispatch path is the only one.

**Action Items:**
- [ ] Identify every reader of `registered_resources*` (grep `src/`). For each: rewrite to consume the v2 route table or registration journal directly.
- [ ] Delete `registered_resources*` maps + mutex from `webserver_impl`.
- [ ] Delete `namespace compat` from `create_webserver.hpp`, the `[[deprecated]]` overload, and the paired `-Wdeprecated-declarations` push/pop.
- [ ] Drop the same `-Wdeprecated-declarations` suppression in `src/http_resource.cpp:81-115` once the migration to `std::atomic<std::shared_ptr<T>>` (TASK-070) makes it unnecessary, or hand it off to TASK-070 explicitly if scope expands.
- [ ] RELEASE_NOTES.md: document the binary/source incompatibility under "Removals".

**Dependencies:**
- Blocked by: TASK-053 (Done)
- Blocks: None

**Acceptance Criteria:**
- `grep -nE 'registered_resources' src/httpserver/detail/webserver_impl.hpp` returns no matches.
- `grep -nE 'namespace compat' src/httpserver/create_webserver.hpp` returns no matches.
- `grep -nE 'Wdeprecated-declarations' src/httpserver/create_webserver.hpp` returns no matches.
- All existing tests pass.
- Typecheck passes.
- Tests pass.

**Related Requirements:** PRD §2 API minimalism, PRD §1 release strategy
**Related Decisions:** DR-007, DR-011

**Status:** Done
