# HTTP/3 engine fuzz corpus

These authored binary `.seed` files replay against fresh HTTP/3 connections.
The fuzz runner copies them into a writable build directory. Keep source seeds
immutable; add minimized regressions as new named seeds.

The first byte selects a limit profile modulo 16. Remaining records contain
three bytes (`operation`, `stream slot`, `payload length`), then that many raw
payload bytes. A truncated payload uses the available suffix. Operations are
reduced modulo 13 and slots modulo 16. At most 4096 bytes and 64 records run.
Slots 0–7 map to request streams `4 * slot`; slots 8–15 map to peer
unidirectional streams `4 * (slot - 8) + 2`.

| Operation | Behavior |
|---|---|
| 0 | Append and feed raw stream bytes |
| 1 / 2 | RESET / EOF terminal notification |
| 3 | Begin a scheduler turn |
| 4 | Advance local critical output by the first payload byte |
| 5 | Encode repeated response fields; odd first payload byte publishes |
| 6 | Cancel the latest outgoing section token |
| 7 | Decode and queue a local encoder instruction |
| 8 | Publish the latest outgoing section token |
| 9 | Cancel unpublished sections for the stream |
| 10 | Shut down the connection |
| 11 | Feed at an invalid stream offset |
| 12 | Release an observed event |

For local output/instructions, slots select control, encoder, or decoder roles
by `slot % 3`. Profile 7 holds observed events until operation 12; all other
profiles copy the event and release it immediately.

| Profile | Difference from the default ceilings |
|---|---|
| 1 | Expanded field bytes 41 (one below static GET's 42) |
| 2 | Zero fields |
| 3 | Compressed HEADERS 2 bytes |
| 4 | Zero blocked streams |
| 5 / 12 | Retained HEADERS 3 / 2 bytes |
| 6 | QPACK output 10 bytes |
| 7 | Hold events |
| 8 | Child QPACK table budget 127 bytes |
| 9 | Encoder instruction 3 bytes |
| 10 | Zero outgoing sections |
| 11 | Zero table capacity |
| 13 | Expanded field bytes 42 (exact static GET boundary) |
| 14 | Zero SETTINGS payload bytes |
| 15 | One field |

Default ceilings are compressed HEADERS 128 bytes, expanded fields 256 bytes,
eight fields, QPACK tables 128 bytes per direction, two blocked streams,
retained HEADERS 256 bytes, instructions and QPACK output 128 bytes, eight
outgoing sections, SETTINGS 128 bytes/eight identifiers, DATA chunks 16 bytes,
two frames/instructions per turn, and 16 stream records. Data and critical
storage envelopes are 16 KiB and 64 KiB. Feed calls are capped at 9536:
`2 * 4096 + 64 + (64 + 16) * 16` allows consuming calls, scheduler yields,
explicit feeds, event pumps and final drain visits. Copied observations/output
are capped at 16 KiB and 2048 observation entries.

Seeds cover varint widths, malformed/truncated frames, SETTINGS ordering and
identifiers, critical stream duplication/closure, encoder/request ordering,
blocked siblings, complete/partial cancellation, late insertion, decoded and
retained limits, table eviction/wrapping, decoder feedback, held events,
outgoing publication/cancellation, output refusal, and shutdown. Held DATA with
one release and subsequent EOF/RESET, plus a 3800-byte DATA frame, preserve the
regressions for fragmentation-sensitive backpressure and scheduler work.

`http3_fuzz_replay` runs sorted seeds, 32 fixed-seed mutations and 32 prefixes
per seed. Every input compares two fresh whole-slice executions and runs a
one-byte-fragment execution. Auto-release profiles compare normalized valid
results or invalid wire errors across fragmentation. Profile 7 compares two
fresh fragmented executions independently: a finite number of event releases
advances different byte extents, so observations and EOF errors can differ
between whole-slice and fragmented delivery. Both schedules retain all typed
error, resource, consumption, work and teardown assertions. Explicit replay
tests also require successful blocked admission, unblocking, cancellation and decoded-limit
rejection. Printed coverage indices 16–20 respectively mean decoded HEADERS,
blocked admission, cancellation, unblocking and limit rejection.
