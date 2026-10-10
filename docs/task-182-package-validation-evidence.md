# TASK-182 installed package validation

The package gate independently configures, builds and installs native TLS-off
and TLS-on packages, then compiles fresh C++20 shared/static consumers against
installed headers and pkg-config metadata. Every consumer executes the same
configuration cases and HTTP/1.0, HTTP/1.1, POST, persistent, pipelined and
shutdown journeys in managed and external modes.

| Host | Managed driver | External host | Execution owner |
| --- | --- | --- | --- |
| Linux | epoll | poll | Local checks and CI |
| FreeBSD 14.3 | kqueue | poll | CI / v3 PR |
| macOS | kqueue | poll | CI / v3 PR |
| Native MinGW64 | IOCP | WSAPoll | CI / v3 PR |

Nonlocal execution is deferred to CI / v3 PR. A workflow definition or a policy
fixture is not a runtime receipt. The dedicated SHA-pinned workflow uploads
its prerequisite and package evidence even when a required command fails.

## Public transport boundary

Provider availability differs from public transport availability. Current
public TLS, HTTP/2 and HTTP/3 listener requests return `not_supported` before
binding any endpoint, including mixed plaintext/TLS configurations. This gate
checks that boundary in both loop modes and does not implement new transports.
The same provider/profile mismatch case has the existing validation precedence:
managed mode reports `invalid_argument`; external mode reports `not_supported`
because its TLS restriction is checked first. Both outcomes are explicitly
asserted and neither binds an endpoint.

The required installed integration tools compile only existing test sources.
Installed public includes precede source private includes, and compiler dependency
files reject source public headers and build headers. `HTTPSERVER_COMPILATION`
and private headers are allowed only for these explicitly named tools; they are
not installed and are forbidden for public consumers. The inventory lives in
`test/installed/v3-package-matrix.json`. Required suites reject empty execution,
any failure, and any skip. TLS tools retain the independent poll oracle and
prove encrypted managed factory and external readiness paths with witnesses.

Readiness suites exercise stale generations, worker wakeups, timers and stop.
Native backend suites provide the platform oracle; Windows additionally checks
completion storage and IOCP lifetime scenarios. Header-only suites are labeled
separately from installed-library execution. POSIX HTTP/2 network fixtures and
the TASK-178 HTTP/3 interoperability pipeline remain separate release evidence.

## Reproduction

Bootstrap the checkout, select genuine dependency prefixes, and run:

```sh
python3 scripts/check-v3-installed-consumer.py \
  --output /fresh/evidence \
  --provider-prefix /selected/shared-openssl \
  --prerequisite-prefix /selected/mhd \
  --prerequisite-prefix /selected/platform-dependencies \
  --compiler clang++ --cc clang --jobs 2
```

Add explicit `--pkg-config-dir` arguments for system multiarch metadata.
Use `--make gmake` on FreeBSD and `--toolchain-prefix` on native Windows.
Windows uses native Python, native compiler executables, `.exe` consumers,
explicit DLL directories and PE imports; MSYS runtime dependencies are rejected.

`prepare-v3-package-ci.py` builds shared OpenSSL 3.5.9 and MHD 1.0.3 from
checksummed sources. The OpenSSL checksum was verified against the official
[OpenSSL source checksum](https://www.openssl.org/source/openssl-3.5.9.tar.gz.sha256).
The FreeBSD action is pinned to the verified `v1.4.2` commit
`c9f815bc7aa0d34c9fdd0619b034a32d6ca7b57e`; its
[action definition](https://github.com/vmactions/freebsd-vm/blob/c9f815bc7aa0d34c9fdd0619b034a32d6ca7b57e/action.yml)
defines VM release and copyback controls.

## Audit and receipts

Recursive ELF, Mach-O and PE graphs retain resolved paths, hashes, inspection
commands and platform leaf provenance. Archives are inspected across all
members. Import archives cannot substitute for the actual static library.
Static libhttpserver linkage includes its pkg-config declared dependency closure;
provider/platform dependencies can remain shared. Shared tools must resolve the
installed libhttpserver; static tools must not load it dynamically.

The transitional aggregate still intentionally includes MHD/GnuTLS. Its explicit
pre-cutover graph policy is checked, and strict aggregate rejection is retained
as cutover evidence. Native linkage keeps the strict provider boundary. Selected
OpenSSL paths must stay under the selected provider prefix. Runtime allowances
require trusted OS paths or the selected MinGW toolchain root.

Evidence contains revision/branch, source and artifact hashes, compiler identity,
configure/build/install/link commands, selected environment, pkg-config metadata,
header declaration hashes, archive symbols, recursive dependency graphs, suite
counts, backend witnesses, traffic assertions and command/process failure logs.
Each output directory is fresh; failed attempts are preserved.
Shared and static public cells have distinct process receipts so later cells
cannot overwrite an earlier cell's stdout, stderr or shutdown evidence.

The installed HTTP suite now waits for the Digest nonce timestamp to expire
under a five-second monotonic budget. A fixed sleep did not establish expiry
when this VM's wall clock stepped backwards. The response assertions still
require a fresh `401` challenge with `stale=TRUE`; separate tests cover clock
rollback, a stuck wall clock and malformed nonce timestamps.

## Local results

The final Linux matrix passed at `/tmp/task182-matrix-final-3/summary.json`,
using branch `task/TASK-182` at base revision
`3df64484c21c6367105a42178a44ba90af694430` with the recorded implementation
source hashes. Both package audits have zero violations and identical installed
header sets/effective declarations. FreeBSD, macOS and MinGW64 remain deferred
to CI / v3 PR.

| Native TLS | Public cells | Suites per linkage | Test cases per linkage |
| --- | --- | --- | --- |
| Off | 4 | 8 | 182 |
| On | 4 | 15 | 233 |

Shared and static execution together cover 46 suite runs and 830 test cases,
with zero failures or skips. The eight public cells retain 80 configuration
results and 48 traffic journeys. Each TLS parity suite retains 56 matching
encrypted transcript observations per managed, external and poll-oracle driver.

The local command selects Clang 18.1.3 and the shared OpenSSL 3.5.9 provider
at `/tmp/task182-provider`, with system MHD/GnuTLS metadata and genuine
libunistring development files extracted under
`/tmp/task182-system-prerequisite`. System package downloads and hashes are
retained in `/tmp/task182-system-packages`. The inherited user `pkg-config`
shim is bypassed with `PATH=/usr/bin:/bin`.

```sh
PATH=/usr/bin:/bin python3 scripts/check-v3-installed-consumer.py \
  --output /tmp/task182-matrix-final-3 \
  --provider-prefix /tmp/task182-provider \
  --prerequisite-prefix /usr \
  --prerequisite-prefix /tmp/task182-system-prerequisite \
  --pkg-config-dir /usr/lib/x86_64-linux-gnu/pkgconfig \
  --pkg-config-dir /usr/share/pkgconfig \
  --compiler clang++ --cc clang --jobs 2
```

The command output is `/tmp/task182-matrix-final-3.log`; detailed command,
public-process, suite and dependency receipts live below its output directory.
The failed fixed-sleep nonce attempt remains at `/tmp/task182-matrix-final`.
The superseded `/tmp/task182-matrix-final-2` run predates distinct shared/static
process filenames and is not the final acceptance receipt.

The package, consumer, native-linkage and workflow fixtures passed 35, 18, 12
and 3 tests respectively. The three nonce-expiry tests and 29 existing behavior
parser tests passed. Focused Automake tests, native linkage, installed header
and hygiene gates, workflow pinning, warning-suppression checks, C++ lint,
Python/shell syntax and Markdown fence checks passed. `make dist` contains all
14 new validation artifacts.

The repository-wide file-size check retains its unchanged baseline failure:
`src/detail/io_poll_backend.cpp` has 509 SLOC against a 500 limit. Its log is
`/tmp/task182-final-file-size.log`. Optional `markdownlint` is absent locally.
