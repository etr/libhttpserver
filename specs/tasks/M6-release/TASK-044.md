### TASK-044: SOVERSION bump and packaging

**Milestone:** M6 - Release Readiness
**Component:** Build / packaging
**Estimate:** S

**Goal:**
Bump the shared-object version 1→2 in autoconf and verify `libhttpserver2` is parallel-installable with `libhttpserver1`.

**Action Items:**
- [ ] In `configure.ac` (or wherever SOVERSION is set), bump `LT_VERSION` / `-version-info` from the v1 value to the v2.0 value (current:revision:age conventions; the result must produce `libhttpserver.so.2`).
- [ ] Update `libhttpserver.pc.in` (pkg-config metadata) — `Version: 2.0.0`, library name remains `libhttpserver`.
- [ ] Update `Makefile.am` install rules if the `.so` symlink chain needs adjusting.
- [ ] Verify with a clean install in a temp prefix: `libhttpserver.so.2.X.X` ships, `libhttpserver.so.2 → libhttpserver.so.2.X.X` symlink correct, `libhttpserver.so` dev symlink correct.
- [ ] Document parallel-installability with v1 in the release notes (TASK-042 covers prose; this task verifies it works at the file-system level).
- [ ] Update the version in `configure.ac`'s `AC_INIT` to `2.0.0`.

**Dependencies:**
- Blocked by: TASK-042, TASK-043
- Blocks: None (this is the last gate before tagging)

**Acceptance Criteria:**
- `./configure && make && make install DESTDIR=$tmp` produces `libhttpserver.so.2.0.0` and the expected symlinks.
- `pkg-config --modversion libhttpserver` reports `2.0.0`.
- A test installs both `libhttpserver1` (separate build) and `libhttpserver2` into the same prefix and confirms both `.so.1` and `.so.2` coexist (or document the test as manual if CI can't reasonably do this).
- Typecheck passes.

**Related Requirements:** PRD §1 release strategy
**Related Decisions:** DR-011, §5.4, §8

**Status:** Not Started
