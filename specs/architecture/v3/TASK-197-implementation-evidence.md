# TASK-197 implementation evidence

Implementation remains on `task/TASK-197`, based on `v3`, in its registered isolated
worktree. No staging, commit, merge or worktree lifecycle action is performed by the
executor. Task status remains In Progress pending the runner's validation/finalization.

The behavioral journal is `.debug/task197/journal.md`. Retained raw RED/GREEN receipts
are under `.debug/task197/`; resumed probes are `/tmp/task197-*.log`. Final package and
conformance receipts are under `build/task197/`. `TASK-197-receipt.json` records actual
completed gates and artifact hashes; failed earlier attempts remain distinguishable.

Behavioral regressions cover native upgrade handoff and immediate DATA, cancellation
of pending receive/writable, copied opening fields/charge, reliable stream retirement,
late retransmission/credit, one stream seat, migrated-budget accounting, admission across
drain, failed GOAWAY deadline and admission-hook deduplication. A final wire cancellation exposed receive RESET
arriving before STOP_SENDING: semantic cancellation had incorrectly suppressed the
independent send RESET. A deterministic two-order regression fails the missing action
and passes after transport RESET preparation precedes the semantic idempotence guard.
The public pending-writable witness now forces acknowledged receive RESET and handler
retirement before sending STOP, then requires the decoded remote RESET. Resumed H2 regressions
first failed missing informational HEADERS and DATA emitted before final HEADERS;
`http2_native_owner` passes with bounded continuation publication and informational
head scheduling. The public one-shot source corpus first failed with 404, then proved
file/pipe exact bytes, second-use 409 and exactly one handle close on H1/H2/H3.

Public witnesses retain raw ordered fields/trailers, normalized routing, authentication,
fragmented forms and byte limits, multipart cleanup, concurrent immutable cursors,
borrowed buffers/reopenable and one-shot files/pipes, committed exception reset and
100-continue before upload. H3 partial borrowed/file cancellation actually exhausts
4096 bytes of stream credit, retains the lease while blocked and proves release/close
and coroutine retirement after decoded peer cancellation. Partial multipart creates an
actual temporary file and proves cleanup after cancellation. Held credential rotation
withholds a real client Finished flight after selecting the old certificate.

The installed matrix includes both pinned clients (aioquic1.3.0/quic-go0.55.0), the
public native example RFC9220 corpus, same-port TCP/UDP, four-listener aggregate tickets,
blocked QPACK, loss/reordering counts, three download/upload reset/refill cycles and six
one-second CPU/RSS observations. Normal allocator RSS limits apply only to normal lanes;
sanitizer lanes still require measured idle CPU thresholds and exact ownership witnesses.
No timeout or process-exit-only result substitutes for a decoded remote terminal.

Setup transfer bounds distinguish normal throughput from sanitizer instrumentation:
warm download uses normal3s/instrumented8s, and impaired recovery uses normal6s/
instrumented12s. Receipts require measured positive elapsed times within those bounds.
Exact TSan probes showed continuous bytes and FIN at 4–5s warm and 6.19s impaired;
earlier short-deadline failures remain retained. The six fixed one-second stalled CPU
windows, normal allocator RSS limit, impairment counts, ordered bytes/FIN and decoded
terminal predicates retain their original thresholds.

The unchanged standalone `http3_websocket_handshake` allocation-counter test cannot
link with TSan's strong new/delete interceptors. It passes fully under normal/ASan;
native negotiation, immediate DATA, upgrade/reset and destruction remain covered by
`http3_native_stream` within the thirteen affected TSan owner suites. No new sanitizer
suppression or linker override is introduced. The GOAWAY deadline unit test now mocks
only its own clock/decoder, preserving subprocess's real clock; actual decoder/network
gates remain unchanged.

Compile/setup mistakes, wrong fixture options, client timeouts, stale aggregate libraries
and source mutation during an earlier matrix are retained as failed attempts, not accepted
production RED or final evidence. Source libraries are rebuilt before test relink/install.
The historical missing upstream runner/simulator receipt was restored by shared tooling;
the task uses that genuine pinned infrastructure without replacing the required proof.

Nonlocal platform receipts are deferred to CI / v3 PR under repository policy. Provider
and independent clients are uninstrumented; task owner/consumer sanitizer flags and any
existing explicit sanitizer exclusions are reported by their gates.

Final local closure passed against the final runtime sources:

| Gate | Verified result | Receipt/log |
|---|---|---|
| Installed public H3 | 20 cells: normal/ASan/TSan TLS-on and normal/ASan TLS-off; both pinned clients in every TLS-on cell | `build/task197/public-h3-*-closure1/receipt.json` |
| Public H2 preservation | 12 cells, 51 cases per cell, genuine hyper-h2 4.4.1 and Node24.15.0 | `build/task197/public-h2-*-closure1/receipt.json` |
| Public TCP TLS preservation | 12 cells, 82 cases per cell, authentic TCP443 ACME witness in an isolated network namespace | `build/task197/public-tls-*-closure1/receipt.json` |
| Affected native owners | normal14/ASan14/TSan13 suites; 674 tests and11,553 checks, with the explicit standalone TSan allocation-counter exclusion above | `/tmp/task197-owner-*-closure.log` |
| Retained full gate | all nine stages; native48/55 and sanitizer48/55 suites, genuine upstream/ns3 QUIC, H3 and RFC9220 interop, six TLS-off and one TLS-on fuzz target at2000 runs each | `build/task197/full-final5/manifest.json` |
| Driver contracts and examples | 87 Python contract tests; Go race checks; six examples in each TLS lane with one TLS-only skip in TLS-off; syntax for12 changed Python files | `/tmp/task197-*-closure.log`, example and Go logs in the versioned receipt |

The full gate froze every tracked/untracked nonignored source throughout all nine
stages. Each final installed source/library identity was also verified after its matrix.
Subsequent versioned changes are receipt/evidence/task bookkeeping only. The machine
receipt deduplicates exact source/library maps into a hash-addressed input catalog; each
gate retains its own raw receipt and command-log hashes, client/provider identities,
scenario cases and measured lifecycle observations. Prior failed/interrupted attempts
and earlier passes from different source/driver states remain recorded. No source test
or closure predicate is replaced by an earlier receipt.

The six checked implementation items map to the same-route semantic/policy/metadata
families; blocked-QPACK, loss, window/reset/refill and lease-cleanup families; aggregate
four-listener drain/handler-stop and retained transport predicates; both-client native
and example RFC9220 corpora plus pending receive/writable cancellation; SNI/mTLS, held
Finished/rotation, independent stores, resumption/rejected early data and TCP/QUIC
challenge/profile isolation; and the44 installed cells with current examples/docs.
Task status remains In Progress for the runner's validation/finalization. All prepared
changes are unstaged and uncommitted.
