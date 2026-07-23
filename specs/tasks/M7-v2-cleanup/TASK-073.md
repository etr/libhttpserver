### TASK-073: Revisit libmicrohttpd v0.99 unescape workaround

**Milestone:** M7 - v2 Cleanup
**Component:** `src/detail/webserver_callbacks.cpp`
**Estimate:** S

**Goal:**
The workaround at `src/detail/webserver_callbacks.cpp:339-342` patches over a libmicrohttpd v0.99 unescape bug, never revisited. Verify whether current supported MHD versions still need it.

**Action Items:**
- [x] Identify the minimum supported MHD version in the build system (`configure.ac` `PKG_CHECK_MODULES(MHD, libmicrohttpd >= X)`).
- [x] Cross-reference the upstream MHD changelog: confirm the v0.99 unescape bug is fixed in our minimum supported version. Link the upstream commit/release in the commit message.
- [x] If the bug is fixed in the min-supported version: remove the workaround and add a static `#if MHD_VERSION < 0x00091300` guard (or equivalent) only if we want belt-and-braces for older distros.
- [x] If the bug is still present in the min-supported version: leave the workaround but add a comment naming the upstream issue tracker URL and the version cutoff at which it can go.
- [x] Update the inline comment with the verification outcome.

**Dependencies:**
- Blocked by: None
- Blocks: None

**Acceptance Criteria:**
- The decision is documented in the comment with an MHD version reference.
- If removed: all unescape tests still pass against the supported MHD matrix.
- Typecheck passes.
- Tests pass.

**Related Requirements:** PRD §2 dependency hygiene NFR
**Related Decisions:** None new

**Status:** Done
