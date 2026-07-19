# Test-Suite Portability Matrix

Tracks integration tests in `test/integ/` that are skipped on Windows or
Darwin, the reason for the skip, and the plan (if any) to restore coverage.

The CI lint `scripts/check-skip-rationales.sh` enforces that every
`#ifndef _WINDOWS`, `#ifndef DARWIN`, or `#if !defined(_WINDOWS)` block in
`test/integ/` carries a `// reason: ...` comment naming the underlying
limitation and pointing at a section of this file (or a follow-up task) where
the gap is tracked.

The opposite polarity — `#ifdef _WINDOWS` / `#ifdef DARWIN` blocks — adds
platform-specific coverage rather than removing it, so those directives do
NOT require a rationale comment.

## How to read this document

Each section names a specific skip site (`<file>:<line>`) and records:
- **Symptom**: what the test would observe if the skip were removed.
- **Root cause**: which platform / MHD / curl / test interaction prevents
  the test from running.
- **Restoration plan**: what would be required to re-enable, and the task
  tracking that work (or "not currently planned" if the gap is
  intentional).

---

## Skipped on Windows

### `threaded.cpp` — entire suite body (lines 61, 66, 73, 80)

- **Symptom**: under the MinGW64 / MSYS lanes the suite either deadlocks on
  `accept()` or fails the single curl round-trip with a connection-refused
  error.
- **Root cause**: the suite exercises
  `start_method(INTERNAL_SELECT).max_threads(5)`. libmicrohttpd is built
  from source on the Windows CI lanes with `./configure --enable-poll=no`
  (see the "Build and install libmicrohttpd (Windows)" step in
  `.github/workflows/verify-build.yml`), and MHD's `INTERNAL_SELECT`
  thread-pool path under that build is known to
  interact poorly with the MSYS curl shim — the same scenario is already
  documented as flaky in MHD's release notes from the libmicrohttpd 0.9.x
  series. libhttpserver's `start_method()` enum does not surface an IOCP-
  shaped alternative, so a Windows-shaped variant of the suite would
  require either flipping the MHD build to `--enable-poll=yes` (out of
  the current porting scope) or adding a new `start_method` to libhttpserver.
- **Restoration plan**: not currently planned. Coverage of the basic
  daemon-starts-accepts-one-request flow is already restored on Windows
  through the `ws_start_stop_suite::windows_smoke` variant; the
  specific `INTERNAL_SELECT` + thread-pool combo is not
  observably different on the Windows lanes from what `windows_smoke`
  already verifies. Re-open if a future Windows-specific bug is suspected
  in the thread-pool dispatch path.

### `ws_start_stop.cpp` — wide skip (rationale comment 130–137, directive 138, closes at line 1300)

- **Symptom**: TLS / IPv6 / SNI / PSK / custom-socket / bind-address tests
  fail to compile or fail at runtime on the MinGW64 / MSYS lanes.
- **Root cause**: a mix of failure modes — TLS round-trips through MinGW64
  curl + gnutls + MHD have been observed as flaky; IPv6 tests are gated on
  `IPV6_TESTS_ENABLED` which the Windows lanes do not set; the
  `custom_socket` test relies on POSIX `<sys/socket.h>` semantics not
  cleanly available on the MSYS build; and the SNI/PSK variants depend on
  GnuTLS callback shapes whose Windows behaviour has not been verified.
- **Restoration plan**: the simplest case — a non-TLS HTTP
  GET round-trip — is ported as the new `ws_start_stop_suite::windows_smoke` test
  that runs under `#ifdef _WINDOWS`. The remaining cases (TLS / IPv6 /
  SNI / PSK / custom-socket / bind-address) are tracked as a future
  follow-up task and are NOT in the current porting scope. Reopen if a specific
  Windows TLS bug is suspected.

### `authentication.cpp` — digest-auth block (line 265)

- **Symptom**: the curl client on MinGW64 returns parsing errors when
  presented with the digest challenge that MHD's
  `MHD_queue_auth_required_response3` produces, so the test never reaches
  the second (authenticated) round-trip.
- **Root cause**: MinGW64's curl `--digest` parser has historically been
  unable to handle the challenge produced by MHD on Windows. The
  pre-existing comment ("Will fix this separately", in place
  beforehand) was never followed up because the RFC-7616 work
  focused on the algorithm-handling code in libhttpserver, not on the
  test-infrastructure issue that prevents the round-trip from completing
  on MinGW64.
- **Restoration plan**: a future follow-up task. The actual port requires
  isolating whether the failure is in MinGW64 curl's challenge parser,
  in MHD's challenge format, or in the way libhttpserver wires the two
  together — out of the current "restore coverage or document" framing.

### `connection_state_body_residue_test.cpp` — single test body (line 137)

- **Symptom**: the test depends on the `hook_phase::connection_opened`
  hook firing, which in turn depends on MHD reaching the lifecycle path
  that the test exercises. On Windows under the MinGW64 build that path
  has not been verified.
- **Root cause**: predates the current portability sweep — the test was added under the same
  `#ifndef _WINDOWS` convention used by `ws_start_stop.cpp` and was not
  separately verified on Windows. The connection-lifecycle hook firing
  has not been validated against the MSYS curl + MHD combo.
- **Restoration plan**: not currently planned. The hook-firing pipeline
  is covered on Linux and Darwin by the rest of the `hooks_*` suite;
  porting this single residue test to Windows would require duplicating
  the curl-driven body-replay setup under a `#ifdef _WINDOWS` guard with
  hand-validated expectations, which is disproportionate to the coverage
  it would add.

## Skipped on Darwin

### `ws_start_stop.cpp` — `custom_socket` test (line 337)

- **Symptom**: when the `#ifndef DARWIN` gate is removed, the test
  appears to start the daemon but the curl client cannot reach it on
  macOS.
- **Root cause**: the test creates an `AF_INET` socket with only
  `SO_REUSEADDR` set, then hands it to libhttpserver via `bind_socket(fd)`.
  On macOS, MHD's `MHD_USE_INTERNAL_POLLING_THREAD` path drives the
  accept loop through `kqueue`, and registering a user-bound listening
  socket on macOS under `SO_REUSEADDR` alone has historically been
  fragile when an earlier daemon-bind on the same port has TIME_WAIT
  state. The Linux fall-through happens to work because of how Linux
  reuses the listen socket binding semantics.
- **Restoration plan**: the hypothesis recorded above (Darwin requires
  `SO_REUSEPORT` alongside `SO_REUSEADDR`) is plausible but not
  confirmed end-to-end on the CI lane. The skip is retained with
  this `// reason:` comment so the gap is documented; promoting the
  port to "fixed" requires a CI run on `macos-latest` that actually
  exercises the modified socket setup. Tracked as a future follow-up
  task.

---

## Skipped in test/unit/

The skip sites in `test/unit/` predate the `check-skip-rationales.sh` lint
(which scopes itself to `test/integ/` only). They are tracked here for
parity, but the lint does NOT enforce a `// reason:` comment on these
blocks.

### `response_body_test.cpp` — pipe_response_body POSIX-pipe tests (lines 263, 277)

- **Symptom**: the two pipe_response_body tests (`pipe_body_kind_and_materialize`,
  `pipe_body_destructor_closes_fd_when_not_materialized`) call POSIX
  `::pipe()` directly. MSYS2/MinGW64 does not ship POSIX `::pipe()`;
  the Windows equivalent is `_pipe()` from `<io.h>` or
  `CreatePipe()` from `<windows.h>`, with different fd semantics.
- **Root cause**: the `pipe_response_body` class itself is portable (it owns and
  closes an fd; `MHD_create_response_from_pipe` is wired the same way
  on every MHD build), but the tests need to *create* a pipe to feed
  the constructor, and pipe creation is platform-specific.
- **Restoration plan**: a future follow-up could swap `::pipe()` for
  `_pipe(fds, 4096, _O_BINARY)` from `<io.h>` under an `#ifdef _WIN32`
  branch (mingw-w64 ships `<io.h>` with `_pipe`/`_close`). The Linux
  and macOS CI lanes currently exercise the pipe_response_body code path; the
  Windows MSYS lanes do not. The class's fd lifecycle and
  destructor-closes-fd contract are still verified on Linux/macOS, and
  the production `MHD_create_response_from_pipe` call is the same on
  every platform, so the Windows gap is a defence-in-depth coverage
  loss rather than a contract gap.

### `response_body_test.cpp` — file_response_body destructor test (line 198)

- **Symptom**: `file_body_destructor_closes_fd_when_not_materialized`
  uses POSIX `::open()` / `::close()` and inspects `errno == EBADF`.
- **Root cause**: the file_response_body class itself is portable (the
  constructor uses `::open` via libmicrohttpd's headers on every
  platform), but the anchor-fd technique (open / close / re-open to
  obtain a known fd slot) is POSIX-shaped.
- **Restoration plan**: a future follow-up could use `_open` / `_close`
  from `<io.h>` on Windows for an equivalent anchor-fd test. The
  destructor-closes-fd contract for file_response_body is still verified on
  Linux/macOS lanes; the Windows gap is the same defence-in-depth
  loss as for pipe_response_body above.

### `http_response_factories_test.cpp` — pipe() factory tests (lines 256–273)

- **Symptom**: `pipe_factory_kind` calls POSIX `::pipe()` to feed
  `http_response::pipe(int)`.
- **Root cause**: same as response_body_test pipe_response_body — pipe creation is
  platform-specific, so `pipe_factory_kind` stays gated behind
  `#ifndef _WIN32`. The factory's signature pin (the `static_assert`
  that `http_response::pipe` takes exactly one `int` argument, plus
  the `pipe_factory_signature_is_single_arg` test that hosts it) has
  no platform dependency and has been hoisted out of the `#ifndef
  _WIN32` block to file scope, so it now compiles and runs on every
  CI lane, including Windows.
- **Restoration plan**: same follow-up as response_body_test pipe_response_body for the
  remaining runtime `pipe_factory_kind` test. Until then, the
  Linux/macOS CI lanes cover the runtime factory behaviour and the
  Windows lanes do not; the compile-time signature contract is now
  covered everywhere.

### `iovec_entry_test.cpp` — POSIX struct iovec bridge (line 86)

- **Symptom**: `reinterpret_cast_to_struct_iovec_preserves_data`
  reinterpret-casts an `iovec_entry` array to `const struct iovec*`
  (POSIX `<sys/uio.h>`). MSYS2/MinGW64 does not ship `<sys/uio.h>`.
- **Root cause**: POSIX `struct iovec` has no Windows equivalent; the
  Windows MHD build uses MHD's own `MHD_IoVec` shape, which the
  `reinterpret_cast_to_MHD_IoVec_preserves_data` test (unconditional,
  lines 105–118) already exercises on every platform.
- **Restoration plan**: **not currently planned**. The
  `MHD_IoVec` cast test already covers the actual production cast
  path that `iovec_response::get_raw_response()` performs at dispatch
  time. The POSIX `struct iovec` cast is a defence-in-depth assertion
  only — its purpose is to catch a future libc-shaped consumer that
  re-uses the same layout. The Windows gap here is structural (no
  POSIX type to cast to), not a coverage loss for any production
  call site.

## Adding a new entry

1. Add a `// reason: <one-liner>` comment within the 5 lines immediately
   preceding the `#ifndef _WINDOWS`, `#ifndef DARWIN`, or
   `#if !defined(_WINDOWS)` directive.
2. Add a section to this file with **Symptom**, **Root cause**, and
   **Restoration plan**.
3. Run `./scripts/check-skip-rationales.sh` locally — it must exit 0
   before the change is committed.
