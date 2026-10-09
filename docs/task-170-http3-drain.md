# HTTP/3 graceful drain

TASK-170 implements private, owner-serialized HTTP/3 drain initiation and a
completion ticket using the existing `drain_scope`. Public listener integration
remains a later task. The protected network fixture forwards the drain deadline
into its timer selection and consumes the engine's one-shot close request using
the existing QUIC lifecycle.

Acceptance means successful initial semantic HEADERS admission before initiation.
The request is counted before its handler starts, including a handler that starts
the drain itself. Admission freezes immediately on successful initiation. New
streams, lower-ID holes, attached incomplete HEADERS and QPACK-blocked initial
HEADERS are refused with STOP_SENDING/RESET_STREAM `H3_REQUEST_REJECTED` (`0x10b`).
Accepted DATA and trailers, including blocked trailers, continue normally.
Refusal uses existing bounded stream and action storage.

The local control stream sends SETTINGS first, then GOAWAY frame type `0x7` with
ID `2^62 - 4`. Only acknowledged retirement of the complete initial GOAWAY enables
the final GOAWAY. That cutoff is the highest accepted request ID plus four, or
zero if no request was accepted. Both IDs are client bidirectional stream IDs,
and never increase. Accepting the maximum request ID leaves no representable
excluding cutoff; initiation then returns `limit_exceeded` without starting a
drain. The two GOAWAY frames use fixed storage with a critical budget reservation
committed before the admission transition. Invalid, repeated and reservation
refused initiation leave the caller's ticket unchanged.

The adapter attributes ACK completion to its own reliable information IDs and
application stream records. A local critical stream has at most one outstanding
information record, so its acknowledged control offset is contiguous. Partial
retention, packet preparation, abandoned preparation, actual emission and loss
do not advance the GOAWAY stage. Retransmission preserves stream offsets and
frame bytes. Duplicate and unrelated completions have no accounting effect.

One unit counts the connection, and another counts each accepted request through
handler retirement and reliable response DATA/FIN retirement. Semantic exchange
reaping still happens when FIN is retained; it does not by itself complete the
drain. Completion also waits for final GOAWAY and pending local QPACK output to
retire. This prevents connection closure from truncating encoder instructions
needed to decode an already acknowledged response. Control and both QPACK
streams continue their ordinary processing throughout the application drain.

A successful drain publishes an application CONNECTION_CLOSE request with
`H3_NO_ERROR` (`0x100`). It completes without requiring another empty output poll
when the final owned information completion arrives. Multiple external waiters
observe the same stable result. A handler waiting on its own ticket receives
`would_deadlock`; initiating drain remains nonblocking.

At the deadline, the scope fixes `deadline_expired` and its remaining count before
the owner cancels outstanding work. The ticket's cancellation hook only sets an
atomic notification; external waiters never touch coroutine frames, request maps,
flow control or recovery. Repeated owner checks cancel and publish close once.
Closure proceeds even if flow control or recovery prevents GOAWAY or response
output. Early connection failure, disconnect or destruction also fixes an
incomplete result before dropping counted work. With the existing two-status
ticket API, terminal interruption is represented by `deadline_expired`, even when
it occurs before the original deadline. Resetting an accepted request during
drain prevents successful completion.

Application completion and QUIC endpoint retention are separate. The owner
commits the close request through `quic_connection_lifecycle`; closing/draining
CID routing and terminal keys remain for the existing protocol retention period.
Late ACKs and timer calls cannot change an incomplete result into completed.
Owner entry points retain shared state and defer semantic reaping while callbacks
run; inline executor cancellation may safely destroy the engine itself.

## Local verification

The build uses Autotools and Clang C++20. Run `./bootstrap`, configure a separate
build directory with `--disable-examples --disable-v3-tls`, then `make -j6`.
Focused executables are registered in `test/Makefile.am`; Littletest suites live
in `test/unit`, and real flow/recovery fixtures use `http3_request_fixture.hpp`.
Run native TLS fixtures from the build's `test` directory so `TLS_TEST_DIR`
resolves their certificates correctly.

The TLS-on build uses the genuine OpenSSL 3.5.9 provider at
`build/tooling/provider` in the main repository through its pkg-config directory,
with a separate task build directory and `--enable-v3-tls`. The protected owner
suite covers accepted FIN ACK completion and deadline cancellation while QUIC
retains closing state.

Local C++20 TLS-off and TLS-on builds passed. The ten focused TLS-off suites
passed, including 16 drain cases / 218 checks and 20 QPACK scheduling cases /
405 checks. The TLS-on drain and QPACK scheduling regressions passed, and the
protected owner suite passed 32 cases / 330 checks. `make -j6 check-local`,
changed-file cpplint and `git diff --check` passed.

The additional repository-wide file-size probe reports the unchanged baseline
`src/detail/io_poll_backend.cpp` at 509 SLOC against a 500-SLOC limit; no changed
production file exceeds that limit. The optional complexity probe could not run
because `lizard` is absent. Ownership triage: CI lint lane maintainers must supply `lizard` before
`lint-complexity` can produce a receipt; no replacement checker was introduced. Sanitizers were not
run for this task.
BSD, Windows and other nonlocal platform execution belongs to CI and the v3 PR,
as required by the repository workflow; it was not run locally.
