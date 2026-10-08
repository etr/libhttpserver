# TASK-160 HTTP/3 framing and control ownership

The private, provider-free server connection core classifies admitted peer
QUIC IDs and incrementally classifies unidirectional types. It owns one peer
control stream and each QPACK critical role. The QUIC owner still opens IDs,
reassembles ordered bytes, drives serialized turns and delivers terminals.
There is no UDP listener or semantic exchange bridge in this change.

The control stream requires SETTINGS first and publishes staged settings only
after complete validation. Duplicate IDs (including ignored IDs), HTTP/2
reserved IDs, repeated SETTINGS, wrong frame roles and malformed scalar
payloads have separate connection error mappings. Requests accept defaults
before peer SETTINGS and enforce HEADERS, DATA*, optional trailing HEADERS.
Unknown stream types and frames are discarded without payload copies.
Client GOAWAY carries a push ID, so it has no request-stream-ID bit constraint.
Scalar push/drain frames receive role and exact payload-layout validation;
this slice does not implement push ownership or graceful drain semantics.
The transcript oracle follows [RFC 9114](https://www.rfc-editor.org/rfc/rfc9114.html).

Static-only HEADERS use the connection-owned QPACK codec. Invalid compressed
sections map to 0x200, prohibited dynamic encoder instructions to 0x201, and
acknowledgments/increments without dynamic references to 0x202. Bounded Stream
Cancellation instructions and zero encoder capacity are accepted. Local owned
prefixes contain the control type plus exactly one SETTINGS advertising zero
QPACK capacity/blocked streams, and the two QPACK types. The owner binds opened
server unidirectional IDs; retries cannot bind a second ID to one role or rewind
partial output. No peer SETTINGS prerequisite delays these prefixes.
These stream rules follow [RFC 9204](https://www.rfc-editor.org/rfc/rfc9204.html).

## Bounded ownership

Defaults are 64 KiB compressed HEADERS, 64 KiB expanded field-section size,
256 fields, 4 KiB SETTINGS, 64 setting identifiers, 16 KiB DATA chunks,
64 completed frame/chunk events per serialized turn, and 128 retained stream
records. Exact HEADERS/SETTINGS bounds are admitted; excessive byte, field or
record loads fail with excessive load (0x107). DATA is chunked and exhausted turn
budgets yield until `begin_turn()`. Stream facts cannot be recreated after FIN/RESET.
Settings values retain all 62 wire bits and never increase local admission caps.

HEADERS assembly and decoded descriptors/strings reserve data storage before
allocation. Control payloads, connection/settings scratch/output and stream
metadata reserve protected critical storage. Scalars use eight fixed bytes;
partial QUIC integers use at most eight bytes, and QPACK instructions at most
ten. Unknown payloads and borrowed DATA allocate no declared-length storage.
Configuration arithmetic, ordered offsets and narrowing are checked before use.
The QPACK decoder's additional private `decode_allocated` entry point preserves
its existing checked API while letting precharged owners map allocation failure
to internal error (0x102), independently from configured load refusal.
Failed admissions roll back reservations; leases retain the ancestor envelope
when the original pool handle is destroyed.

Each request has one outstanding immutable event; its bytes/fields stay valid
until release. A stalled request does not prevent another stream or critical
control processing. DATA borrows caller input; the caller retains that span and
unconsumed input. Events expose absolute frame/payload boundaries. Releasing an
event releases storage/lifetime only. The composition fixture sends framing
receipts through `consume_protocol`, advances partial body receipts through
`consume_body`, and defers later framing credit until preceding DATA is handled.
RESET cancellation abandons parser storage and uses `settle_reset` for credit.
Known critical FIN/RESET takes error 0x104; ordinary clean truncation takes
0x106; request RESET mid-frame is cancellation.

## Local verification

Registered worktree: `.worktrees/TASK-160`, branch `task/TASK-160`, base `v3`,
initial HEAD `1f8c6a33`. The supplied worktree was clean with no prior task
implementation. No worktree, branch, stage, commit, merge or runner phase was
created or changed by the implementation agent.

The three new suites were written before the HTTP/3 production APIs. Initial
compilation failed on the missing private HTTP/3 header (`task160-red.log`).
Subsequent red regressions exposed allocator failure misclassification and
retained payload charge, scalar allocation, overflowing configured descriptor
arithmetic, local critical-terminal classification, allocation failure in a
quota-refusal diagnostic, and the erroneous client GOAWAY bit restriction.
Each reproduced failure is retained under the ignored task-local build receipts;
all are green in the final suites.

The local build used `./bootstrap`, then an out-of-tree configuration with
`--disable-examples --disable-v3-tls`, Homebrew include/library paths and
`CXXFLAGS='-O1 -g'`. The generated build adds `-std=c++20`. Both the full library
build and the provider-free v3 core build pass.

| Suite | Tests | Checks |
|---|---:|---:|
| http3_frame | 5 | 476 |
| http3_settings | 3 | 32 |
| http3_connection | 15 | 141 |
| quic_varint | 4 | 184 |
| quic_stream_state | 9 | 186 |
| quic_reassembly | 7 | 165 |
| quic_flow_control | 14 | 278 |
| quic_flow_storage | 5 | 55 |
| qpack_static_table | 1 | 504 |
| qpack_primitives | 2 | 2137 |
| qpack_field_section | 7 | 354 |
| Total | 72 | 4512 |

All 11 executables pass without skips. The three HTTP/3 suites also pass
ASan/UBSan (23 tests, 649 checks), rebuilding their actual native/QUIC composition
sources with matching instrumentation. Changed-file cpplint and whitespace
checks pass. New HTTP/3 functions meet CCN <= 10. `make check-local` passes,
including native linkage, independent header consumers, documentation checks,
staged private-header exclusion and umbrella-header hygiene.

Repository-wide complexity still reports 19 existing violations (including the
unchanged QPACK `read_name` function); normalized function/metric output matches
the untouched `v3` checkout. The file-size gate still reports the existing
509-SLOC `io_poll_backend.cpp`, with byte-identical baseline output. No threshold
or unrelated code was changed. The warning-suppression gate passes.

Normal receipts live under `build/task160-off/`; instrumented receipts under
`build/task160-sanitized/`. BSD, Windows and other nonlocal platform checks are
unexecuted and assigned to CI/the v3 PR by AGENTS.md. These local receipts prove
private framing/control ownership, not network HTTP/3 interoperability or
complete request serving. The task remains In Progress until caller-owned
validation/finalization.
