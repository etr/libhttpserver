# QUIC dispatch and timer lifecycle seeds

This is a deterministic test-support state machine, not a QUIC transport
implementation. Every input replays twice through real CID registration,
dispatch, serialized owner queues and the fake timer backend. Each action
checks bounds; teardown checks zero outstanding resources and packet release.

Input is capped at 4096 bytes and 64 actions. Integer fields are unsigned;
truncated or unknown actions end decoding safely. Action bytes:

| Opcode | Arguments | Action |
|---|---|---|
| 0 | endpoint (1..4) | register/re-register CID `[endpoint, 0]` |
| 1 | length u16 little endian, raw datagram | emit whole datagram |
| 2 | pending slot u8 | drop |
| 3 | pending slot u8 | deliver selected datagram (reordering) |
| 4 | pending slot u8 | duplicate with independent storage |
| 5 | nanoseconds u8 | advance logical clock, expire timers |
| 6 | none | drain owner executors |
| 7 | delay nanoseconds u8 | submit timer |
| 8 | submitted timer slot u8 | cancel |
| 9 | endpoint | retire CID |
| 10 | endpoint | destroy owner |
| 11 | none | teardown/end |

Slots select modulo the current list size; empty lists are no-ops. Limits
are 16 pending packets, 16 KiB pending packet bytes, 16 live timers, four
routes, two packets/8 KiB per owner, 128 harness actions and 2 MiB trace.
Queue charges remain with real owners until drain/destruction; harness byte
snapshots measure only pending network datagrams. Observation copies are
bounded by the action/packet/trace limits and do not count as pending storage.

Trace version 1 begins with `QN` and byte `1`. Every event has an opcode,
u64 ID and u64 logical nanosecond time; integers use little endian encoding.
Opcodes 1..15 are emission, loss, duplication, dispatch, delivery, timer
submission, completion, cancellation, advance, registration, retirement,
destruction, drain, teardown and resource snapshot. Duplication adds the
source ID; dispatch/completion/cancellation add the result enum value;
timer/advance add the delay. Resource snapshots add network packet count,
network bytes, live timers and owner pending count. Packet-bearing events
(emission, duplication, delivery) encode length, bytes, socket ID, peer,
optional local endpoint, optional interface and receive time. Endpoints
encode family, 16 raw address bytes, port and scope. Optional fields encode
presence as u64 before the value. No pointers, padding or wall time appear.
