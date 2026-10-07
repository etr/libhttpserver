# TASK-133 initial-handshake mTLS and peer identity

`tls_options::client_auth` and per-host credentials carry provider-neutral
`none`, `request`, and `require` modes. An unset mode resolves to `require` for
`mutual_tls` and `none` for `certificates`. Explicit `request` permits an
anonymous client, but verifies every presented chain. Explicit `none` conflicts
with `mutual_tls`; enabled client authentication conflicts with TLS-off and PSK
profiles. Invalid policy enumerators return `invalid_argument`. Post-handshake
authentication returns `not_supported` for every transport configuration,
including one enabling HTTP/3. V3.0 implements initial-handshake authentication
only; no QUIC or TCP post-handshake authentication path is provided.

Both authenticated modes require explicit, nonempty trust roots. Roots retain
strict PEM parsing. An invalid candidate preserves the exact active snapshot
and context and consumes no generation. The context factory's defaulted mode
retains anonymous behavior for existing snapshot-less callers. The legacy
credential registry previously retained the mutual profile without enforcing
client verification; the default mutual profile now enforces its documented
required-client-chain contract.

Contexts install the provider's default chain verifier with `SSL_VERIFY_NONE`,
`SSL_VERIFY_PEER`, or `SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT`.
No permissive verification callback is installed. ClientHello selection
explicitly copies verification mode, depth, verification parameters and the
selected trust store onto the SSL before authentication and resumption lookup.
The selected-host session namespace and pinned immutable generation remain in
force. Initial contexts and snapshot-less server sessions use the same policy.
Post-handshake authentication and early application data remain disabled.

`server::tls_peer_metadata` is independently includable without provider headers
or build macros. It owns the leaf's RFC2253 subject/issuer, explicit-length UTF-8
CN, lowercase 64-character SHA-256 fingerprint, and UTC Unix-second validity
bounds. Anonymous TLS uses false flags, empty strings and -1 timestamps. A leaf
is verified only when authentication was enabled and provider verification
succeeded. Successful resumption copies the cached authenticated peer identity.
Extraction builds the entire value before publishing; extraction exceptions
fail the handshake without exposing a partial identity. ASN.1 time conversion
uses checked parsing and C++20 calendar arithmetic, without `timegm`.

The serialized session accessor returns null before successful authentication.
The adapter atomically publishes an immutable shared pointer immediately before
successful handshake completion, after flushing handshake output. Readers call
no provider operations. Retained values survive registry replacement and
session/adapter teardown. Rejected authentication returns the adapter's existing
`protocol_error`, leaves metadata null, completes the control once and retires
raw operations on executor drain.

## Wire and ownership checks

The independent peer verifies server credentials against the existing explicit
server root and uses TLS 1.2/1.3 version bounds. Fragmented memory-BIO transfers
are bounded at 73/41 bytes. The adapter fixture uses the existing fake raw I/O
completion transport with a real provider handshake. No runtime fixture
certificate generation is required.

Dedicated test-only client roots, an intermediate and leaf fixtures cover a
trusted chain, no CN, wrong root, missing intermediate, expired validity and
server-only EKU. Roots and normal leaves have fixed 2020–2040 validity; the
expired leaf is valid only during 2020–2021. Only public CA/intermediate
certificates and test leaf keys are checked in; CA signing keys are excluded.
These keys are test data, not deployable credentials.

The tests observe initial-handshake CertificateRequest and outgoing nonempty
Certificate messages. Negative peers actually transmit their selected chain.
The full none/request/require matrix runs for both versions: none requests no
certificate, request permits omission but rejects invalid presentation, and
require rejects omission and invalid presentation. SNI tests switch none to
require and require to request/none, using disjoint roots. Unknown/omitted SNI
uses the default host. Tests compare Alice's exact fingerprint and validity,
empty CN, metadata before/after authentication, immutable retained identity,
atomic reads during adapter pumping, delayed selections, same-generation
resumption, cross-host tickets and replacement-generation rejection. Default
mutual policy and snapshot-less authenticated contexts also run on wire.

Behavioral TDD receipts before the corresponding implementation changes:

- Options policy: 9 failing checks; GREEN 19 tests / 181 checks.
- Registry policy/publication: 18 failing checks; GREEN 5 tests / 170 checks.
- Initial mTLS, metadata and SNI trust selection: 84 failing checks;
  first GREEN 2 tests / 258 checks.
- Adapter publication: 2 failing checks with a null accessor stub; implementation
  published the immutable value. A subsequent fixture drive failed to park on a
  peer alert after server rejection; its bounded failure retirement condition
  was corrected. GREEN was 5 tests / 360 checks before additional assertions.

The inherited options and credential tests passed during their behavioral RED
runs. No full pristine repository baseline is claimed. An early build invocation
ran a test binary from the wrong directory for its relative fixture path; that
run is not positive evidence.

## Local verification

macOS ARM64, Apple Clang, C++20, Autotools VPATH builds. Provider:
`/private/tmp/task129-provider/install` (OpenSSL 3.5.9). Configure commands match
`docs/task-132-tls-selection-evidence.md`, using `--enable-v3-tls` or
`--disable-v3-tls`, Homebrew prerequisite include/library directories and
`CXXFLAGS='-std=c++20 -O0 -g'`.

Final frozen-source receipts:

- Complete TLS-on and TLS-off builds passed.
- TLS-on: 10/10 executables, 61 tests / 1,474 checks; no failures or skips.
  The new mTLS suite passed 6 tests / 390 checks.
- TLS-off: 5/5 executables, two public consumers plus 63 tests / 937 checks;
  no failures or skips.
- Native linkage audits and repository `check-local` passed in both modes,
  including staged install/hygiene checks.
- The metadata header compiled and ran in an independent C++20 consumer with
  no provider headers, `HTTPSERVER_COMPILATION`, or TLS build macros.
- ASan/UBSan and TSan each passed 19 tests / 851 checks across the four targets,
  with no sanitizer diagnostics.
- Changed-file cpplint passed. Changed-source functions have CCN <= 10 except
  the unchanged `valid_peer_pattern` baseline; repository file-size and
  `git diff --check` passed. The complete complexity gate retains the three
  unchanged baseline warnings described below.

The complete TLS-on focused command is:

```sh
make -C build-on -j2
make -C build-on/test -j2 tls_mtls tls_credentials tls_selection \
  tls_credentials_rotation tls_io tls_io_race tls_io_loopback \
  io_tls_control server_options_validate consumer_v3_server
make -C build-on/test -j1 check-TESTS \
  check_PROGRAMS='tls_mtls tls_credentials tls_selection tls_credentials_rotation tls_io tls_io_race tls_io_loopback io_tls_control server_options_validate consumer_v3_server' \
  TESTS='tls_mtls tls_credentials tls_selection tls_credentials_rotation tls_io tls_io_race tls_io_loopback io_tls_control server_options_validate consumer_v3_server'
make -C build-on check-v3-native-linkage check-local
```

TLS-off uses complete build plus explicit `check_PROGRAMS`/`TESTS` containing
`consumer_v3_features consumer_v3_server io_tls_control io_backend_contract
server_options_validate`, then native linkage and `check-local`.

Sanitizers compile all affected native sources (`io_operation`,
`io_connection_owner`, `fake_io_backend`, `worker_pool`, `tls_credentials`,
`tls_session`, `tls_io_backend`) directly with C++20, `-pthread -O1 -g
-fno-omit-frame-pointer` and either `-fsanitize=address,undefined` or
`-fsanitize=thread`. The four executables are `tls_mtls`, `tls_credentials`,
`tls_selection` and `tls_credentials_rotation`; no unsanitized native archive is
used. A final frozen-source sanitizer run rebuilds these objects and all four
executables, including the default/snapshot-less wire assertions. The selected provider static
libraries are not instrumented. Reproduction follows the TASK-132 sanitizer
commands with the added `tls_mtls` target. Task-local logs/scripts are under
`/private/tmp/task133-*`.

Restricted-sandbox TLS-on loopback and TLS-off backend runs failed while opening listeners. The focused
set was rerun with authorization to bind ephemeral local sockets; the restricted
run is not positive evidence. An intermediate rebuild overlapped the TLS-on `check-local` staged install,
causing libtool archive/link errors. The task-local TLS-on build output was
cleaned and rebuilt; subsequent build, tests and install checks run sequentially.
That interrupted run is not positive evidence. Existing macOS libtool
deprecation and duplicate library warnings remain.

Changed-file cpplint and source CCN checks, repository file-size checks, and
`git diff --check` are recorded with the final receipts. The full repository
complexity gate still reports three unchanged baseline violations:
`dispatch_request` (11), `valid_peer_pattern` (15), and `pollsys::accept_one`
(11). Fresh files extracted from `v3` reproduce those exact warnings; no new
function exceeds CCN 10.

Linux, BSD, Windows and other unavailable platform checks are unexecuted and
belong to CI/the v3 PR under AGENTS.md. Native listener availability remains
false, and this task does not wire HTTP request dispatch. HTTP/2 framing, QUIC
transport interoperability, PSK and ACME are outside TASK-133. Groundwork
validation, runner commits, integration into `v3` and cleanup remain
caller-owned.
