# TASK-184 native package cutover evidence

Local implementation evidence collected on Linux x86-64, using Clang C++20,
from task branch `task/TASK-184` based on
`100c34b88cf7e18efcfe90ed84477cd1f45110ef` (`v3`). No commit, validation
phase, merge or worktree cleanup was performed by the implementer.

## Package boundary

The production aggregate contains the native v3 core, with OpenSSL private
objects only in TLS-on builds. Legacy implementation/header manifests,
configure probes, GnuTLS/MHD flags, legacy example/test build targets and CI
provider installation paths are removed. Installed metadata reports version
3.0.0 and ELF SONAME `libhttpserver.so.3`. The object-only aggregate explicitly
selects the C++ linker, including platforms without an explicit `-lstdc++` flag.

Runtime packages can retain ABI-2 and ABI-3 versioned files simultaneously.
Headers, `.pc`, `.a`, `.la` and the unversioned linker name remain shared paths;
parallel development packages and loading both ABIs into one process are not
promised. Existing public TLS/H2/H3 listeners and public credential installation
remain capability gaps; private TLS/provider tests do not establish them.

## Installed and distribution verification

The fresh installed matrix passed:

```sh
python3 scripts/check-v3-installed-consumer.py \
  --output /tmp/task184-installed-final \
  --provider-prefix /tmp/task182-provider \
  --compiler clang++ --cc clang --jobs 2
```

`/tmp/task184-installed-final/summary.json` records both TLS modes, four public
managed/external and shared/static cells per mode, eight native integration
suites per linkage TLS-off and fifteen per linkage TLS-on (46 suite executions).
Real OpenSSL 3.5.9 was verified from the selected provider prefix. Both modes
install the same 39 public include files with identical effective declaration
hashes. Strict audits retain recursive shared dependencies, every defined and
undefined archive-member symbol, public include traces, metadata and build
provenance; no legacy violations were found.

The matrix's startup `src/Makefile.am` hash precedes the final C++ linker-driver
fix. TLS-off was explicitly relinked and reinstalled with that fix
(`/tmp/task184-final-cxx-link.log`, `/tmp/task184-final-cxx-install.log`);
TLS-on built with it. The startup receipt is preserved, and final-source strict
audits are retained separately in each lane's `audit-final-source.json`.
The linkage gate checks the generated C++ link command with `AM_LDFLAGS=`.

A `make dist` archive was unpacked at
`/tmp/task184-dist-source/libhttpserver-3.0.0` and independently configured,
built and installed with native examples enabled in both modes:
`/tmp/task184-dist-build` and `/tmp/task184-dist-tls-build`.
Both supplied `/tmp/task184-rejecting-headers` containing rejecting
`microhttpd.h` and `microhttpd_ws.h` fixtures. Both builds, header/layout/linkage
checks and `check-local` passed. Their `audit.json` receipts have no violations
and identical public declarations. Native private/header and retained test
helper manifests were corrected after real archive-build failures; a source
regression checks those distribution closures.

## Genuine runtime coexistence

Before removing sources, the original ABI-2 production aggregate was built in
`/tmp/task184-abi2-build`. Its retained library and an executable constructing
and destroying the original `http_response` are in `/tmp/task184-abi2`.
The old library SHA-256 is
`2393741c62a4360fa75206662212cdc9ea725abf22434e7cc52526e5d5b2198d`;
the old consumer SHA-256 is
`e55ea450bc842a2a44d92071268e78ff02da40f9c5abf5e069d136abc754f28a`.
These are genuine original binaries, rather than substitute fixture libraries.

```sh
python3 scripts/check-v3-runtime-coexistence.py \
  --baseline /tmp/task184-abi2 \
  --build /tmp/task184-installed-final/tls-no/build \
  --output /tmp/task184-runtime-final --compiler clang++
```

`/tmp/task184-runtime-final/receipt.json` passed: a real v3 staged installation
preserved the old file hash and ABI-2 symlink; `readelf` verified SONAME/NEEDED 2
and 3; both executables ran against the same library directory.
`check-soversion.sh` passed all eight installation assertions
(`/tmp/task184-soversion-final.log`). All four installed native examples compiled
in each TLS mode (`/tmp/task184-installed-examples.log`,
`/tmp/task184-examples-tls-final.log`).

## Regression gates and limits

Policy fixtures passed: installed audit 35, installed consumers 24, native
linkage 12, cutover 9, package workflow 4. Focused native configuration, HTTP/1
parity/parser, WebSocket codec, hooks and URL-encoded/multipart route binaries
passed (`/tmp/task184-focused-native-run.log`). These were built and run directly;
a broad `make check` dependency build was canceled rather than claimed as a
complete release-wide test run. The real TLS configure matrix passed,
including rejected unsupported/mismatched providers and the genuine provider.
CodeQL, workflow pinning, MSan and Valgrind structure gates, version validation,
Python compilation, shell syntax, warning-suppression lint and diff whitespace
checks passed. Final source `check-local` logs are
`/tmp/task184-check-local-no-final.log` and
`/tmp/task184-check-local-tls-final.log`.

The broader file-size gate has an inherited failure: unchanged
`src/detail/io_poll_backend.cpp` is 509 SLOC against a 500 limit. Its contents
match the base commit; this task does not alter the backend.
Doxygen execution was skipped by the existing gate because Doxygen is absent.
BSD, macOS, Windows, ARM and other nonlocal build/runtime checks were not run;
CI and the v3 PR own that evidence under the project workflow policy.
Release-wide conformance, sanitizer and performance gates remain TASK-185 work.
