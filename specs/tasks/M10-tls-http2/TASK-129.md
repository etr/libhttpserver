### TASK-129: Establish OpenSSL 3.5 LTS build boundary and feature gates

**Milestone:** M10 - TLS and HTTP/2
**Component:** TLS adapter and credential registry
**Estimate:** M
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Provide OpenSSL 3.5 LTS build boundary and feature gates for libhttpserver v3.0.

**Action Items:**
- [x] Add explicit TLS-on and TLS-off build configurations.
- [x] Gate OpenSSL 3.5 QUIC callback API and release patch floor.
- [x] Keep OpenSSL includes and types private while public declarations stay identical.

**Dependencies:**
- Blocked by: TASK-108
- Blocks: TASK-130, TASK-131, TASK-134, TASK-148, TASK-152

**Acceptance Criteria:**
- TLS-on builds require the selected patched 3.5 line; TLS-off has no OpenSSL symbols or headers in public API.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-002, PRD-V3N-REQ-003, PRD-V3N-REQ-005, PRD-V3N-REQ-007, PRD-V3N-REQ-034, PRD-V3N-REQ-037
**Related Decisions:** DR-V3-002, DR-V3-007

**Status:** Complete


**Implementation evidence (2026-10-06, macOS arm64):**
- Added native `--enable-v3-tls` / `--disable-v3-tls` modes, default off, with private provider flags. Stable OpenSSL 3.5.x patch >= 9 is required; other lines, LibreSSL, prereleases, missing callback APIs, and mismatched runtimes are rejected. Legacy GnuTLS discovery remains a separate transitional lane.
- Provisioned genuine OpenSSL 3.5.9 from the official release archive into `/tmp/task129-provider/install`. Verified the published SHA256: `603f5602e2eef00d77fbd429d34dcd5822bb301757a1bc9cdb24c670f1eb859a`. The isolated fixture was built with shared libraries and `no-tests`; this is build/provider proof, not upstream-suite or transport interoperability proof.
- TDD observed ignored native configure options, unavailable compiled feature declarations, incorrect pre-bind behavior, and audit fixtures passing forbidden dependencies before implementing their fixes. Provider selection without a TLS endpoint continues to serve plaintext HTTP/1.
- Fresh TLS-off and TLS-on C++20 builds pass. Each final `make check -j1` passes **232/233** programs, with only the macOS-inapplicable `io_epoll_backend` skipped; no failures or errors. This includes existing native HTTP/1, installed-consumer fixtures, header hygiene, and the transitional legacy tests. Native server tests pass 19 tests / 97 checks in each mode.
- `make check-v3-tls-build` passes **17 configure cases** and **9 adversarial linkage fixtures**. Positive cases use unmodified genuine provider headers/libraries; synthetic rejection fixtures are not positive provider evidence. Both actual `make check-v3-native-linkage` gates pass, inspecting the real native executable and archive independently.
- Real libssl/libcrypto smoke reports `OpenSSL 3.5.9 29 Sep 2026`. The same consumer source compiles against staged public headers in both modes with no backend include paths or configuration macros. All **63 installed header files** have identical sets and contents. Forcing the local 3.6 runtime causes the TLS-on consumer smoke to fail as expected.
- Both modes pass `check-headers`, `check-hygiene`, and `check-install-layout` (also included by full `make check`). Changed C++ files pass the repository cpplint command; shell syntax, Python compilation, and `git diff --check` pass.
- Build note: `docs/native-tls-build.md`. To rerun the configure matrix with this fixture: `V3_TLS_TEST_PREFIX=/tmp/task129-provider/install CPPFLAGS=-I/opt/homebrew/include LDFLAGS=-L/opt/homebrew/lib make -C build-off check-v3-tls-build`.
- BSD, Windows, Linux, and other nonlocal checks remain unexecuted here and belong to CI and the v3 PR under AGENTS policy. TCP TLS, HTTP/2, and HTTP/3 transport remain unavailable; native listen rejects them before any endpoint binds. TASK-184 still owns removal of legacy aggregate/package dependencies.
- Implementation is ready for the runner's validation phase. Status remains In Progress until validation/finalization; no Git staging or commits were performed by the implementer.
