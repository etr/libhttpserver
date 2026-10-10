# RFC 9113 request-engine corpus

The five-column shared corpus format is unchanged. `verdict` contains
`scope code stream`, with RFC wire error codes. The payload column lists routed
stream IDs; `/` identifies stream 1 and `/sibling` identifies stream 3.
The stage `eof` requires EOF during a field block. `unadvertised` withholds
server SETTINGS exposure to exercise RFC 8441 capability gating.

Executor schedules are semantic boundaries, independent of input fragmentation:

- `feed` defers executor work until the entire transcript has been parsed.
- `pump` runs pending work and retires output after each completed frame.
- `dispatch` runs pending work after each completed frame, suspends the stream 1
  handler on an application signal, and defers output retirement until input is
  parsed. The later stream error cancels this suspended handler. This schedule
  applies to `content-length-data`, `malformed-trailer`, `closed-data`,
  `stream-window-overflow` and `half-closed-head`.

Binary HPACK literals are authored independently of the library encoder. The
harness checks resets, GOAWAY, response order, sticky failure, consumption,
routes and complete budget release. Coalesced, bytewise, every split and partial
output retirement must give identical wire output within a fixed schedule.
The five later-error fixtures retain their original route 1 observation and
exact stream error verdicts. Paired deferred-start regressions require zero
stream 1 calls: a reset invalidates queued lazy work before execution. Started
application waits must be destroyed once without running after a late signal;
started body reads may complete once with typed `connection_closed`. A separate
token observer must complete once with typed `cancelled`. All reservations must
return after teardown. These rules follow PRD-V3N-REQ-023/025, DR-V3-003/006 and
the queued-handler cancellation contract in `docs/task-142-http2-evidence.md`.

Dispatching an immediately completed response is a different schedule: an
END_STREAM opening becomes fully closed before later input. The harness checks
`closed-data` and `half-closed-head` separately in that schedule for connection
STREAM_CLOSED, rather than treating its result as interchangeable with the
original half-closed stream verdict. Changing only input chunk boundaries never
selects a different schedule.

The paired tests append an independently authored dynamic-table insertion in a
discarded HEADERS block on reset stream 1, then stream 3 references index 62.
They assert the sibling's actual `x-route: after` field, exact route order,
reset-before-response ordering, full suffix consumption, no duplicate reset or
GOAWAY, admission/cancellation counts and all fragmentation variants. Per-case
and aggregate corpus/schedule execution counts are printed in the test log.

RFC 9113 sections 5.3.2, 6.2, 6.3 and Appendix B deprecate priority dependency
semantics. Self-dependency therefore has no error verdict. PRIORITY length,
nonzero stream placement and continuation isolation remain enforced.

Required minimum/final HPACK table updates after acknowledged SETTINGS changes
are covered by `hpack_corpus` using independent octets; peer SETTINGS affects the
response encoder, so feeding those settings to this engine cannot emulate local
receive-table changes. The existing 28 connection-level fixtures remain separate.

The TLS fixture exercises the real private request engine. The public listener
still dispatches HTTP/1. No transcript substitutes for independent clients.
