# TASK-148 installed-consumer evidence

TASK-148 audits the **transitional installed aggregate**, not the final v3
release package. The checked policy declares MHD/GnuTLS and specific, inspected
provider support edges. For static consumers, the explicitly declared pkg-config private provider
closure may become direct link edges; shared consumers retain the narrower
legacy edge set. Unknown edges, curl, other providers, missing resolution,
and malformed inspection fail. Native-only executable/archive checks retain the
platform/C++ runtime plus selected OpenSSL policy. Strict mode rejects every
legacy edge and legacy archive symbol even if it is declared for pre-cutover use.
TASK-184 retains legacy removal and the ABI cutover; TASK-185 retains release gates.

## Reproduction and evidence locations

From a bootstrapped checkout:

```sh
./bootstrap
python3 scripts/test-v3-installed-package.py
python3 scripts/check-v3-installed-consumer.py \
  --output /private/tmp/TASK-148-matrix-pass \
  --provider-prefix /private/tmp/task129-provider/install \
  --prerequisite-prefix /opt/homebrew \
  --prerequisite-prefix /opt/homebrew/opt/gettext \
  --pkg-config-dir /opt/homebrew/Library/Homebrew/os/mac/pkgconfig/26 \
  --jobs 6
```

The output path must be absent. Each mode gets independent configure/build,
installation, and separate consumer directories. Prerequisite discovery is
explicit; the extra macOS metadata directory supplies the platform zlib .pc file.
Homebrew GnuTLS names `-lintl` without its keg directory, so the selected gettext
prerequisite prefix supplies that link search path. Consumer flags are cleared
of ambient include/link configuration, pkg-config lookup is isolated to the
installed package and selected prerequisites, and internal configuration macros
and source/build include paths are rejected.

The genuine selected provider reports **OpenSSL 3.5.9, 29 Sep 2026**. Its headers
and runtime passed the existing stable-3.5.9+ configure gate; no altered provider
fixture was used for positive proof. Local receipts are retained at:

- `/private/tmp/TASK-148-matrix-pass/summary.json`
- `tls-no/commands.json`, `tls-yes/commands.json` below that directory
- `tls-no/audit.json`, `tls-yes/audit.json`
- `tls-no/strict-rejection.json`, `tls-yes/strict-rejection.json`
- `/private/tmp/TASK-148-matrix-pass.log`

The audit receipts retain resolved canonical identities and SHA-256 values for inspected binaries,
parent/child dependency edges including cycles and aliases, current configure/
Makefile/pkg-config provenance, all-member static archive symbols, installed
header include traces, command return codes, and effective declaration hashes.
Preprocessor output is hashed rather than storing every standard library
expansion repeatedly. The matrix additionally retains actual compile/link
commands, pkg-config resolution, wire responses and clean stop exits.

## Findings and repairs

The consumer-style umbrella preprocessing initially exposed six effective MHD
type declarations from `websocket_handler.hpp`. The legacy session now stores
backend handles as opaque pointers and restores their types only in the private
implementation. The socket width and pointer/bool member layout are preserved;
public methods and semantics are unchanged. The private constructor signature
changes, while no public callable method changes.

Enumerating every installed header also found `create_test_request.hpp` had an
umbrella-only gate but was unreachable through the umbrella. The umbrella now
includes that existing public builder. The audit checks gated fragments only
through their intended umbrella context and requires each installed fragment to
be reached; it does not define internal/configuration macros to bypass gates.

A real TLS-on installed static consumer failed with unresolved OpenSSL context,
session and version symbols. `Libs.private` now adds the selected `V3_TLS_LIBS`,
so `pkg-config --libs --static libhttpserver` supplies the actual selected
provider. TLS-off substitutes empty provider flags. The real static consumer also exposed prerequisite search paths overriding the
selected provider: pkg-config paths now precede explicitly selected prerequisite
paths. The repaired consumer loaded the intended 3.5.9 provider and passed its
feature and traffic checks. No provider include flags
are added to the consumer-visible Cflags.

Adversarial fixtures cover declared versus undeclared direct/transitive legacy
edges, strict rejection, selected provider identity, TLS-off provider rejection,
unknown libraries even under system paths, runtime-name spoofing, missing graph
nodes, unresolved libraries, malformed/failed inspection, cycles, symlink
identities, all-member archive symbol leakage, effective backend declarations,
backend/private/OS includes, source/build contamination, internal consumer
macros, injected headers, external installed-header symlinks, umbrella-only
fragments, and ELF loader resolution. Fixtures are policy/parser tests and are
separate from the real installed-package evidence.

## Local verification

The 21 adversarial fixture tests pass. Local `make check-local` and the adversarial fixture target passed in both TLS
modes. Logs: `/private/tmp/TASK-148-check-local-no.log` and
`/private/tmp/TASK-148-check-local-yes.log`. This includes the existing strict
native linkage gate, header gates, installed layout/hygiene and repository local
lint gates.

Focused `feature_unavailable`, `webserver_ws_unavailable`, `ws_registry`,
`ws_registry_concurrency`, `header_hygiene`, `v3_header_hygiene`, and
`native_server` regressions passed in both modes. Logs:
`/private/tmp/TASK-148-regressions-no.log` and
`/private/tmp/TASK-148-regressions-yes.log`. Changed C++ files passed cpplint;
Python syntax compilation and `git diff --check` passed.

RED evidence includes the effective MHD header leak, inaccessible public test
builder, TLS-on static OpenSSL link failure, malformed/missing inspection,
symlink alias and fragment handling failures. Logs are the task-owned
`/private/tmp/TASK-148-*-red.log` files and
`/private/tmp/TASK-148-on-red/static-link-red.log`.

The first sandboxed consumer could not bind its ephemeral loopback port; the
same authorized traffic check passed outside the sandbox. Later build/staging
attempts exhausted disk space. Only obsolete copies created by this TASK-148
execution were reclaimed, preserving failure logs, and affected gates were
rerun. These failures did not change source behavior.

## Evidence boundary

macOS has no on-disk copies of several sealed dyld-cache libraries. The audit
records exact named platform cache leaves, including C++/system runtimes and
legacy framework/support leaves reached through declared provider edges. It
does not inspect dependencies inside Apple's sealed OS cache or treat arbitrary
system-directory names as allowed. Third-party dependency closure is recursively
inspected with `otool` and canonical path resolution. ELF parsing has adversarial
fixture coverage; a real ELF installed matrix was not executed locally.

The local legacy MHD WebSocket prerequisite is absent (`microhttpd_ws.h` and its
library), so the legacy WebSocket-disabled branch and relevant available
regressions were executed. The WebSocket-enabled legacy branch was not executed
locally. Native WebSocket types remain separate from this legacy session.

The matrix verifies real **plaintext HTTP/1 traffic in both provider build
modes**, compiled provider availability, and typed `not_supported` TLS-listener
failure before binding. Public native listeners still have no operational TLS
credential path; this audit does not claim TLS traffic, QUIC interoperability,
or dependency-free aggregate packaging. BSD, Windows, and other nonlocal checks
remain assigned to CI and the v3 PR and do not block local completion.

## Installed matrix outcomes and identities

| Native TLS | Installed headers | Aggregate nodes / edges | Native nodes / edges | Strict graph violations |
|---|---:|---:|---:|---:|
| no | 65 | 21 / 59 | 3 / 2 | 42 |
| yes | 65 | 23 / 66 | 5 / 7 | 42 |

All four shared/static consumer runs served `HTTP/1.1 200` with the exact
16-byte body `installed-v3-ok\n` and exited 0 after stop. Provider availability
matched each mode; both rejected TLS listeners with typed `not_supported` before
binding. The installed file sets and effective public declaration hashes match.

Full strict CLI audits also reject the real transitional aggregate and its
all-member legacy archive symbols in both modes (96 violations each). Those
receipts are `/private/tmp/TASK-148-matrix-green/tls-{no,yes}/strict-audit.json`.

The source distribution passed with the registered worktree Git context supplied
to the existing dist hook, and includes every new audit script, policy, consumer
and evidence document. Log: `/private/tmp/TASK-148-dist.log`.

| TLS | Artifact (relative to each lane) | SHA-256 |
|---|---|---|
| no | `prefix/lib/libhttpserver.2.dylib` | `e3e71d4a986ed29cb4d5b29da1b20bc2ce91ad83bea7768441574771d467fe67` |
| no | `prefix/lib/libhttpserver.a` | `b3f98c633e9cf71e5cb984488ac177d3d2f7fcf669e8b73d0d317e274fed174c` |
| no | `build/src/.libs/libhttpserver_v3core.a` | `1a65d141047a571a7f1bd4503b4c4a002a3e0907e04154ee0f069fb5ec3b3823` |
| no | `build/test/v3_native_linkage` | `ea2671d35115709f2d2969528fb6d960581d40958aa73d8a586d91320e5c7f07` |
| no | `consumer/v3_package_consumer` | `ce453c67066d596347b044bc15bf444920b7090fae93d71ce6b3b0808896c15e` |
| no | `consumer/static_consumer` | `60a22910a68cf0d68b865c8bd1bbb37f0310a6023c0c8fa7faef969acb8456bb` |
| yes | `prefix/lib/libhttpserver.2.dylib` | `5673c518facc8e3bb7365ac996bf38b382aa60d446d1de519c4ba16350fa6860` |
| yes | `prefix/lib/libhttpserver.a` | `5402569de01ea3481de29aa5d8fd47f9d8cccacf125f79754345c7aecfda95de` |
| yes | `build/src/.libs/libhttpserver_v3core.a` | `4fd90bb60a67a9cee388c2caac1cb5e35d26673d1fa6b3acfe57e48828990061` |
| yes | `build/test/v3_native_linkage` | `6ee17f88110a135a5e4392ad09433593c97dc613bcf4fc91ba559fd86a4afcfa` |
| yes | `consumer/v3_package_consumer` | `a0758b4bfc747dbe44d6d3df15a638e30545da64d75f2d877b5b83b722972f44` |
| yes | `consumer/static_consumer` | `b94fbabf635b76a07b78fb9a300ccfa62d2a5b74d7000445d0c0134b12f02503` |

Inspected dependency adjacency below uses library families for readability.
Resolved paths, requested loader identities and binary hashes remain in the
JSON receipts. OS cache leaves have no third-party recursive inspection.

| TLS | Parent | Inspected direct dependencies |
|---|---|---|
| no | `libgmp.10.dylib` | `libSystem.B.dylib` |
| no | `libgnutls.30.dylib` | `CoreFoundation`, `CoreServices`, `Security`, `libSystem.B.dylib`, `libgmp.10.dylib`, `libhogweed.7.0.dylib`, `libidn2.0.dylib`, `libintl.8.dylib`, `libnettle.9.0.dylib`, `libp11-kit.0.dylib`, `libtasn1.6.dylib`, `libunistring.5.dylib`, `libz.1.dylib` |
| no | `libhogweed.7.0.dylib` | `libSystem.B.dylib`, `libgmp.10.dylib`, `libnettle.9.0.dylib` |
| no | `libhttpserver.2.dylib` | `libSystem.B.dylib`, `libc++.1.dylib`, `libgnutls.30.dylib`, `libmicrohttpd.12.dylib` |
| no | `libidn2.0.dylib` | `CoreFoundation`, `libSystem.B.dylib`, `libintl.8.dylib`, `libunistring.5.dylib` |
| no | `libintl.8.dylib` | `CoreFoundation`, `CoreServices`, `libSystem.B.dylib`, `libiconv.2.dylib` |
| no | `libmicrohttpd.12.dylib` | `libSystem.B.dylib`, `libgnutls.30.dylib` |
| no | `libnettle.9.0.dylib` | `libSystem.B.dylib` |
| no | `libp11-kit.0.dylib` | `libSystem.B.dylib`, `libffi.dylib` |
| no | `libtasn1.6.dylib` | `libSystem.B.dylib` |
| no | `libunistring.5.dylib` | `CoreFoundation`, `CoreServices`, `libSystem.B.dylib`, `libiconv.2.dylib` |
| no | `static_consumer` | `CoreFoundation`, `CoreServices`, `Security`, `libSystem.B.dylib`, `libc++.1.dylib`, `libgmp.10.dylib`, `libgnutls.30.dylib`, `libhogweed.7.0.dylib`, `libidn2.0.dylib`, `libintl.8.dylib`, `libmicrohttpd.12.dylib`, `libnettle.9.0.dylib`, `libp11-kit.0.dylib`, `libtasn1.6.dylib`, `libunistring.5.dylib`, `libz.1.dylib` |
| no | `v3_package_consumer` | `libSystem.B.dylib`, `libc++.1.dylib`, `libhttpserver.2.dylib`, `libmicrohttpd.12.dylib` |
| yes | `libcrypto.3.dylib` | `libSystem.B.dylib` |
| yes | `libgmp.10.dylib` | `libSystem.B.dylib` |
| yes | `libgnutls.30.dylib` | `CoreFoundation`, `CoreServices`, `Security`, `libSystem.B.dylib`, `libgmp.10.dylib`, `libhogweed.7.0.dylib`, `libidn2.0.dylib`, `libintl.8.dylib`, `libnettle.9.0.dylib`, `libp11-kit.0.dylib`, `libtasn1.6.dylib`, `libunistring.5.dylib`, `libz.1.dylib` |
| yes | `libhogweed.7.0.dylib` | `libSystem.B.dylib`, `libgmp.10.dylib`, `libnettle.9.0.dylib` |
| yes | `libhttpserver.2.dylib` | `libSystem.B.dylib`, `libc++.1.dylib`, `libcrypto.3.dylib`, `libgnutls.30.dylib`, `libmicrohttpd.12.dylib`, `libssl.3.dylib` |
| yes | `libidn2.0.dylib` | `CoreFoundation`, `libSystem.B.dylib`, `libintl.8.dylib`, `libunistring.5.dylib` |
| yes | `libintl.8.dylib` | `CoreFoundation`, `CoreServices`, `libSystem.B.dylib`, `libiconv.2.dylib` |
| yes | `libmicrohttpd.12.dylib` | `libSystem.B.dylib`, `libgnutls.30.dylib` |
| yes | `libnettle.9.0.dylib` | `libSystem.B.dylib` |
| yes | `libp11-kit.0.dylib` | `libSystem.B.dylib`, `libffi.dylib` |
| yes | `libssl.3.dylib` | `libSystem.B.dylib`, `libcrypto.3.dylib` |
| yes | `libtasn1.6.dylib` | `libSystem.B.dylib` |
| yes | `libunistring.5.dylib` | `CoreFoundation`, `CoreServices`, `libSystem.B.dylib`, `libiconv.2.dylib` |
| yes | `static_consumer` | `CoreFoundation`, `CoreServices`, `Security`, `libSystem.B.dylib`, `libc++.1.dylib`, `libcrypto.3.dylib`, `libgmp.10.dylib`, `libgnutls.30.dylib`, `libhogweed.7.0.dylib`, `libidn2.0.dylib`, `libintl.8.dylib`, `libmicrohttpd.12.dylib`, `libnettle.9.0.dylib`, `libp11-kit.0.dylib`, `libssl.3.dylib`, `libtasn1.6.dylib`, `libunistring.5.dylib`, `libz.1.dylib` |
| yes | `v3_package_consumer` | `libSystem.B.dylib`, `libc++.1.dylib`, `libhttpserver.2.dylib`, `libmicrohttpd.12.dylib` |
