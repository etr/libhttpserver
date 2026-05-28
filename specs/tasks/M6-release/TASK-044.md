### TASK-044: SOVERSION bump and packaging

**Milestone:** M6 - Release Readiness
**Component:** Build / packaging
**Estimate:** S

**Goal:**
Bump the shared-object version 1→2 in autoconf and verify `libhttpserver2` is parallel-installable with `libhttpserver1`.

**Action Items:**
- [x] In `configure.ac` (or wherever SOVERSION is set), bump `LT_VERSION` / `-version-info` from the v1 value to the v2.0 value (current:revision:age conventions; the result must produce `libhttpserver.so.2`). (Bumped the three `m4_define` lines that compose `libhttpserver_PKG_VERSION` and `libhttpserver_LDF_VERSION`; the project uses libtool's `-version-number A:B:C` flag, so `2:0:0` now drives both the SONAME and the on-disk filename to `libhttpserver.so.2.0.0` / `libhttpserver.so.2` / `libhttpserver.so` on Linux.)
- [x] Update `libhttpserver.pc.in` (pkg-config metadata) — `Version: 2.0.0`, library name remains `libhttpserver`. (No edit needed: the template already uses `@VERSION@`, which now interpolates to `2.0.0` from `AC_INIT`.)
- [x] Update `Makefile.am` install rules if the `.so` symlink chain needs adjusting. (No install-rule edits required; libtool emits the correct chain. Added two new check targets — `check-soversion` and `check-parallel-install` — to assert it.)
- [x] Verify with a clean install in a temp prefix. (Implemented as `scripts/check-soversion.sh` + `make check-soversion`. Linux asserts the three-file chain; Darwin asserts the two-file chain libtool emits on Mach-O.)
- [x] Document parallel-installability with v1 in the release notes (TASK-042 covers prose; this task verifies it works at the file-system level). (Implemented as `scripts/check-parallel-install.sh` + `make check-parallel-install`; verified locally that `libhttpserver.0.dylib` from `master` and `libhttpserver.2.dylib` from this branch coexist in a shared DESTDIR.)
- [x] Update the version in `configure.ac`'s `AC_INIT` to `2.0.0`. (Driven transitively by the `MAJOR/MINOR/REVISION` macro bump.)

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

**Status:** Done
