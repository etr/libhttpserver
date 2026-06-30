### TASK-086: Execute file-size split roadmap (FILE_LOC_MAX 750 → 500)

**Milestone:** M7 - v2 Cleanup
**Component:** `src/httpserver/create_webserver.hpp`, `src/hook_handle.cpp`, `src/detail/webserver_routes.cpp`, `src/httpserver/http_request.hpp`
**Estimate:** L

**Goal:**
`scripts/check-file-size.sh:56-60` carries `FILE_LOC_MAX="${FILE_LOC_MAX:-750}"`
with a long-term target of 500. The roadmap is documented in the comment
block at lines 26-46 and is the single residual TEMP-BUMP from commit
`87185ea`. Execute the four splits and drop the ceiling.

**Action Items:**
- [x] `src/httpserver/create_webserver.hpp` (712 LOC) → ~400 + ~300. Likely split: builder-state struct + setter definitions into a new private `create_webserver_setters.hpp` with public surface unchanged.
- [x] `src/hook_handle.cpp` (572 LOC) → ~430. Likely split: factor the per-phase dispatch helpers into `detail/hook_handle_dispatch.cpp`.
- [x] `src/detail/webserver_routes.cpp` (547 LOC) → ~480. Likely split: extract the regex tier or the LRU helpers into a sibling TU.
- [x] `src/httpserver/http_request.hpp` (583 LOC) → ~400 + ~250. Likely split: container-getter declarations vs. single-key getters, or move the inline `iovec_entry`-related members into a paired `http_request_iovec.hpp` already exists from TASK-004.
- [x] Drop the `FILE_LOC_MAX` default from 750 to 500. Remove the TEMP-BUMP comment block at lines 26-46.
- [x] Verify `make check` is green and `verify-build.yml` lint lanes pass.

**Dependencies:**
- Blocked by: None
- Blocks: None

**Acceptance Criteria:**
- Each of the four files (`create_webserver.hpp`, `hook_handle.cpp`, `webserver_routes.cpp`, `http_request.hpp`) is under 500 LOC.
- `scripts/check-file-size.sh` default ceiling is 500.
- The TEMP-BUMP block in the script is removed.
- ABI/SO version unchanged (verified by `make check-soversion`).
- Typecheck passes.
- Tests pass.

**Related Requirements:** PRD §2 code quality NFR
**Related Decisions:** None new

**Status:** Backlog
