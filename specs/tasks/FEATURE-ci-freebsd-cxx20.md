# FEATURE-ci-freebsd-cxx20: select a complete supported C++20 SDK on FreeBSD

Status: Complete — local SDK provisioning validated; archive check environment follow-up is required before publication.
Base: `v3` at `b2f7b33f7ec4857920bf5bd2fe28ad0f2f2e1e25`.
Plan: `.groundwork-plans/FEATURE-ci-freebsd-cxx20-plan.md` in the primary checkout.
Journal: `.debug/ci-freebsd-cxx20-mj9f7lkt/journal.md` in the primary checkout.

Both FreeBSD 14.3 installed-package jobs now select LLVM19's C/C++ compilers
with a private, pinned stable libc++20.1.8 SDK. The selector verifies the official
complete source archive SHA256, builds libc++ using the documented system
libcxxrt ABI mode, records compiler/source/ABI provenance and build logs, and
requires a real cancellation compile/link/run plus selected-runtime resolution
before publishing its compiler and toolchain paths. The SDK remains private;
Node's compiler subshell and the provider C build receive no SDK flags.

Action items:

- [x] Read the saved plan, literal journal, primary CLAUDE.md, applicable AGENTS,
  relevant workflows and native failure evidence.
- [x] Create the isolated task worktree from the exact local v3 base; preserve
  unrelated work.
- [x] Corroborate the selected standard-library feature gap from native RED,
  official target sources and a real local LLVM19/libc++19 guard reproduction.
- [x] Obtain the caller-reviewed private stable libc++20.1.8 provisioning decision.
- [x] Implement verified source provisioning, stable ABI1/libcxxrt vendor settings,
  complete compiler/header/library/rpath selection and matching C compiler paths.
- [x] Pass selected compiler/C compiler/toolchain prefix through both existing
  package/archive lanes, retaining all original lane/consumer assertions.
- [x] Preserve strict runtime closure with FreeBSD-only exact selected libc++ and
  system libcxxrt ownership; retain recursive inspection and file hashing.
- [x] Demonstrate actual local stable20 source-build/compile/link/run semantics,
  compile-only selection, and expected RED when the old libc++19 is restored.
- [x] Run affected tests, bootstrap, actionlint and syntax/whitespace checks.
- [x] Caller: perform full Groundwork validation, finalization and merge.
- [ ] Native FreeBSD post-repair execution: CI/v3-PR owned, not executed locally.

Causal evidence and checkpoint decision (2026-10-23):

- Native package job `120582232049` / run `40002586128` and archive job
  `120582524209` / run `40002590991`, source `9b93ebe`, fail TLS-off compilation
  with `clang++ -std=c++20`: no `std::stop_token` in `io_operation.hpp` and
  `io_operation.cpp`, no `std::stop_callback` in `io_operation.cpp`. Fresh package
  job `120607477244`, source `6fc67232`, confirms the same failure. Terminal logs:
  `/tmp/v3-ci-9b93-job120582232049.log`,
  `/tmp/v3-ci-9b93-job120582524209.log`, and
  `/tmp/v3-ci-6fc-job120607477244.log`.
- The [FreeBSD 14.3 release notes](https://www.freebsd.org/releases/14.3R/relnotes/)
  identify LLVM/Clang/libc++ 19.1.7. The release's
  [libc++ configuration](https://github.com/freebsd/freebsd-src/blob/releng/14.3/contrib/llvm-project/libcxx/include/__config)
  hides stop APIs behind experimental activation. Its
  [Clang driver](https://github.com/freebsd/freebsd-src/blob/releng/14.3/contrib/llvm-project/clang/lib/Driver/ToolChains/FreeBSD.cpp)
  selects `/usr/include/c++/v1`; the installed llvm19 compiler alone does not
  provide a newer standard library. Official llvm19/llvm20 port definitions
  provision compiler-rt/openmp runtimes, without libc++.
- [LLVM19's usage documentation](https://github.com/llvm/llvm-project/blob/llvmorg-19.1.7/libcxx/docs/UsingLibcxx.rst)
  explicitly disclaims API/ABI stability for its experimental stop types and
  jthread. Real local Linux LLVM19/libc++19.1.1 reproduces the absent types;
  experimental activation compiles/links/runs cancellation but is rejected as
  the repair. This is guard characterization, not native FreeBSD evidence.
- Exact target SDK acquisition failed: the active official FreeBSD14.3 base.txz
  URL returned HTTP404 and both official archive hostnames were network
  unreachable. The caller reviewed this limitation and selected stable private
  libc++20.1.8 provisioning, with native execution assigned to CI under AGENTS.
  No target SDK acquisition or native target compile/link success is claimed.

SDK and ABI contract:

- [LLVM's C++20 status](https://libcxx.llvm.org/Status/Cxx20.html) identifies LLVM20
  as the stable stop-token/jthread release. The official
  [LLVM20.1.8 release](https://github.com/llvm/llvm-project/releases/tag/llvmorg-20.1.8)
  provides the complete source archive, SHA256
  `6898f963c8e938981e6c4a302e83ec5beb4630147c7311183cf61069af16333d`.
  The selector verifies that digest before extraction and uses matching cmake,
  llvm/cmake, runtimes, libcxx and libc sources. The full source is necessary:
  component libc++ archives omit the libc shared utility headers used by charconv.
- [LLVM20 vendor documentation](https://github.com/llvm/llvm-project/blob/llvmorg-20.1.8/libcxx/docs/VendorDocumentation.rst)
  supports private-prefix builds and `LIBCXX_CXX_ABI=libcxxrt`. The helper selects
  stable ABI version1, unstable ABI off, extensive hardening matching FreeBSD's
  base setting, system unwinding and the vendor's libcxxrt new/delete setting.
  `/usr/include/c++/v1/cxxabi.h` and `/lib/libcxxrt.so.1` are hashed before build.
- FreeBSD's actual
  [libc++ Makefile](https://github.com/freebsd/freebsd-src/blob/releng/14.3/lib/libc++/Makefile)
  installs the libcxxrt `cxxabi.h` in that directory and defines `LIBCXXRT`.
  Its [config](https://github.com/freebsd/freebsd-src/blob/releng/14.3/lib/libc++/__config_site)
  uses ABI1/`__1`; its linker script groups libc++ with libcxxrt. The
  [libcxxrt Makefile](https://github.com/freebsd/freebsd-src/blob/releng/14.3/lib/libcxxrt/Makefile)
  assigns shared ABI version1 and `/lib` ownership. LLVM20's actual
  `HandleLibCXXABI.cmake` copies only these ABI headers into a private build
  directory and links libcxxrt, preserving the system ABI owner.
- The wrapper explicitly selects private headers and final-link libraries/rpath
  through cleaned environments, with no global CXXFLAGS/LDFLAGS changes.
  Compile/preprocess/relocatable modes omit final-link flags; static mode retains
  private library selection without rpath. The cancellation probe checks libc++20
  headers, callback-once semantics and real jthread stop/join behavior.
- Python and shell audits accept only the selected prefix's direct `lib` directory
  with canonical `libc++.so.1`/`libc++.so.1.0`, plus system `/lib/libcxxrt.so.1`,
  on FreeBSD. The shell retains resolved ldd paths. Near-miss names, other
  prefixes and providers remain rejected. The Python graph still recursively
  inspects/hashes selected runtimes and rejects transitive unselected providers.

Verification:

- Real LLVM20.1.8 libc++ source build passes on local Linux with the real Linux
  libc++abi19.1.1/libunwind19.1.1 ABI owner, stable ABI1 and system unwinding.
  Production wrapper and production cancellation probe compile, link and run
  without experimental activation; compile-only mode also passes. Actual ldd
  resolves libc++20 from the selected private SDK. Restoring old libc++19 headers
  reproduces the missing types. This verifies the stable library and selection
  behavior locally; it does not verify native FreeBSD execution or libcxxrt ABI.
- Task-local build setup corrections are retained: the initial component-only
  source lacked required libc headers and the initial Linux build needed explicit
  system unwinding. The complete pinned source and documented system-unwinder
  setting resolve these relevant setup failures; no unrelated repair occurred.
- `test-v3-installed-package.py`: 43 pass; `test-v3-native-linkage.py`: 21 pass;
  `test-v3-ci-workflows.py`: 21 pass; `test-v3-platform-ci.py`: 11 pass.
  The final four selector tests also pass after wrapper/probe refinements.
  RED receipts precede selector/workflow/audit changes, including actual old SDK
  compilation and accepted/rejected runtime ownership through existing audits.
- Bootstrap passes, retaining the existing .PHONY/AC_OUTPUT warnings. Actionlint
  1.7.7 passes for both affected workflows with optional shellcheck disabled
  because shellcheck is unavailable. Changed Python syntax, FreeBSD guest sh
  syntax, native audit/generated wrapper Bash syntax and `git diff --check` pass.
  CLAUDE's C++ cpplint does not apply to the Python/YAML/Makefile/shell changes.
- Parsed workflow comparison confirms all other jobs/top-level behavior unchanged;
  the entire Node version/build/compiler-selection block is byte-identical.
  The execution regression observes no CC/CXX/CPPFLAGS/LDFLAGS leakage into other
  build commands and verifies all three selected toolchain CLI paths in both lanes.

Evidence, primary-source copies, real compiler/build logs, source metadata and
executable task-local probes are retained in the primary checkout at
`.debug/ci-freebsd-cxx20-mj9f7lkt/implementation/`; the actual local SDK/build remains
under `/tmp/freebsd-cxx20-proof/`. The selector is distributed through Makefile.am.
The repository ignores new `*.py` files, so the new owned helper must be added
explicitly. Tests use the existing Python unittest scripts; no new framework was
introduced. No full baseline, new native CI dispatch, publication, validation loop
or merge was run. Previously closed Darwin/Linux/Windows uncertainties remain
closed. Overall CI-green status and caller validation remain incomplete.

Final local validation and serial follow-up (2026-10-23):

Groundwork sealed exact implementation `f91303f2850560a8d79de1f1cffd55cfda97f823` / tree `6df6ab711604b5de3c7b06a6f5a3795823e65ba0`: 1 iteration, 0 fixed, 3 unworked; all nine applicable profiles approved. Durable run: `.git/groundwork/validation/cdb4ee2aea69cc6a/8efc9fac2c9ecb40/groundwork-validation-0037d169109d3f37fd2e85bc9b88c3b6`. The helper-produced unworked findings report is preserved unchanged. No finding verdict was overridden or repaired by this validation. The earlier implementation-ready text above is superseded only for local validation/finalization. Native FreeBSD/libcxxrt execution remains unexecuted and CI/v3-PR owned.

One concrete approved architecture finding identifies lost selected-toolchain environment between the archive workflow and platform proof later make checks. Caller will complete narrowly scoped serial `FEATURE-ci-freebsd-toolchain-env` before publishing this batch; this known CI defect is not waived or claimed repaired here. Earlier closed Darwin/Linux/Tpo uncertainty batches remain closed. Overall CI is not green.
