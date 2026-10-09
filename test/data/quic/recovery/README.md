# QUIC recovery event corpus

These are authored coverage inputs, not discovered crash artifacts. Each event
is four unsigned bytes `(kind, a, b, c)`, decoded by
`test/fuzz/quic_recovery_fuzz.cpp`. Trailing incomplete events are ignored.
Input is capped at 4096 bytes and 64 events; payloads at 16 bytes. Time advances
monotonically by `1 + b` logical milliseconds, without sockets or workers.
The replay runs every input twice and compares normalized state and accounting.

| Kind | Operation |
| --- | --- |
| 0 | Receive packet number b in packet space a modulo 3 |
| 1 / 15 | Insert / consume owned CRYPTO reassembly bytes |
| 2 / 16 | Retain CRYPTO / STREAM information |
| 3 / 4 / 5 | Prepare or reserve / commit / abandon packet |
| 6 / 7 / 8 | Receive ACK / expire timers / discard packet space |
| 9 | Change key availability and send permission |
| 10 / 11 / 12 / 13 | Receive candidate path / prepare control / complete control / receive response |
| 14 / 23 | Local close, peer close or reset / stop and reset |
| 17 / 18 / 19 / 20 | Begin / prepare / commit / abandon datagram |
| 21 / 22 | Prepare and optionally publish ACK / increase flow credit |

| Seed | Coverage |
| --- | --- |
| ack-loss-reorder.seed | Higher ACK first, loss expiry, then late ACK of lost packet |
| reordered-crypto.seed | Gap blocks reads; filled gap delivers owned ordered bytes |
| path-pending-close.seed | Path response while send is pending, activation after abandonment, queued work after close |
| pto-drain-reset.seed | Handshake PTO, peer close, stateless reset and terminal rejection |
| spaces-batch-flow.seed | All packet spaces, duplicate/reordered receive, datagram batch, flow credit and key discard |

The driver limits receive ranges to 4, sent packets to 8 per space, information
records to 16, retained recovery bytes to 128, reassembly to 64 bytes per space,
and each storage partition to 128 KiB. Teardown checks complete budget release.
`quic_recovery_fuzz_replay` replays the sorted corpus, 32 deterministic mutations
and 32 truncated prefixes per seed. Copy seeds into the runner's build-local
corpus before fuzzing; do not modify these tracked inputs with libFuzzer.
