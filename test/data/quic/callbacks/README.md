# QUIC TLS callback event corpus

These are authored coverage inputs, not discovered crash artifacts. Each event
is four unsigned bytes `(kind, a, b, c)`, decoded by
`test/fuzz/quic_tls_callbacks_fuzz.cpp`. Trailing incomplete events are ignored.
Input is capped at 4096 bytes and 64 events; payloads at 16 bytes. Callback
receive buffers hold at most 32 bytes per level, output 8 bytes per level, and
receive leases 4 bytes. The root resource budget is 128 KiB.

| Kind | Operation |
| --- | --- |
| 0 / 15 | Insert CRYPTO bytes / insert empty Initial data |
| 1 / 2 / 3 | Dispatch send / receive lease / release lease |
| 4 / 5 | Copy retained output / retire output prefix |
| 6 / 14 | Install secret directly with chosen suite / dispatch secret with SSL cipher lookup |
| 7 / 8 / 9 | Dispatch transport parameters / alert / retain callback error |
| 10 / 11 | Local or peer close / advance logical lifecycle timer |
| 12 / 13 | Exhaust / release the remaining resource budget |
| 16 / 17 / 18 | Reserve send / commit or abandon send / retire owner state |

| Seed | Coverage |
| --- | --- |
| all-levels-suites.seed | Reordered Initial data, handshake/application AES-256 secrets and parameters |
| chacha-transitions.seed | Handshake/application ChaCha secrets, output retirement and teardown |
| lease-transition.seed | Held Initial lease across AES-128 handshake secret transition |
| backpressure.seed | Partial send, full output buffer, prefix retirement and owned output copy |
| budget-dispatch-secret.seed | Recoverable budget refusal, then dispatched secret without negotiated cipher |
| close-held-lease.seed | Close with a live receive lease and pending send, then completion and late events |
| failure-pending-close.seed | Dispatched alert while send is pending, sticky failure, close and queued rejection |
| invalid-release.seed | Wrong lease release length, sticky failure, repeated errors and retirement |
| params-repetition.seed | Valid parameters followed by malformed repeated parameters |

The dispatch table is exercised with a valid SSL object. Direct secret events
exercise key transitions separately from provider cipher selection. The replay
checks lease stability, owned input/output, sticky errors, one close notification,
key disposal and budget release, comparing stable observations twice per input.
`quic_tls_callbacks_fuzz_replay` runs the sorted corpus with 32 deterministic
mutations and 32 truncated prefixes per seed. Its additional real-handshake owner
test delivers malformed post-handshake CRYPTO before and after a pinned send,
checks request cancellation, and verifies key and storage retirement.

Close races are serialized event orderings, not concurrent callback access.
No callback is invoked through a retired state pointer: queued event data is
rejected after callback retirement, and SSL is destroyed before its callback
storage. Fuzzing uses a writable build-local copy of this corpus.
