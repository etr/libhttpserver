# TASK-154: QUIC stream state and bounded reassembly

The private stream foundation follows [RFC 9000 stream IDs and states](https://www.rfc-editor.org/rfc/rfc9000.html#section-2.1),
[final-size rules](https://www.rfc-editor.org/rfc/rfc9000.html#section-4.5), and
[STOP_SENDING transitions](https://www.rfc-editor.org/rfc/rfc9000.html#section-3.5).
It links into `libhttpserver_v3core.la`; no public headers or budget keys change.

## Ownership and state

`quic_stream_ids` belongs to the serialized connection owner. Four high-water
counters represent the four stream classes. Local IDs advance monotonically;
a peer opening a higher ID implicitly opens lower IDs of that class without
allocating objects for them. Locally initiated IDs must already be open.
`quic_stream_state` construction checks the ID and connection role. The caller
owns stream materialization, retirement, and the rule against recreating retired
IDs; the counters are not a closed-stream registry or a stream-count limit.

Bidirectional send and receive halves evolve independently; a unidirectional
stream makes its other half unavailable. Receive states distinguish receiving,
size known, all data received, data consumed, reset received, and reset consumed.
FIN is committed with accepted storage, so gaps and unread bytes delay EOF.
RESET_STREAM fixes final size, interrupts unread data, and releases all receive
storage without allocating missing data. Late STREAM frames still undergo
final-size validation and update the highest accepted end, but never refill a
reset receive half. Final-size checks remain active in terminal states.

`take_terminal()` delivers EOF or the first reset error once. A reset may
replace a pending EOF, but cannot create another application terminal after
one was delivered. Repeated matching controls are idempotent. STOP_SENDING
creates one reset request with the first error and the highest sent end, even
after FIN was sent; it is ignored after all stream data was acknowledged.
Taking the request is distinct from `record_reset_sent()`. Reset retransmission
and packet/ACK tracking belong to the caller. ACK methods accept explicit facts
and do not implement recovery. Matching retransmissions preserve terminal send
states. No packet scheduling, CONNECTION_CLOSE, callbacks, flow control, CRYPTO,
TLS, or HTTP/3 engine integration is added here.

## Bounds and error policy

Accepted STREAM payload is copied before returning. Sorted disjoint ranges
coalesce overlaps and adjacency. Reads copy and consume contiguous bytes;
retained overlap must match, while consumed prefixes are discarded without
retaining history. Identical retransmissions do not allocate or increase byte
usage. Empty input creates no range.

- `max_buffered_bytes` caps unique unread bytes. Actual retained payload capacity
  is capped at the same value. A partial read retains the original allocation
  and charge until replacement or complete consumption; consequently a sparse
  insertion can hit the capacity bound before the unread-byte bound.
- `max_ranges` caps normalized retained intervals and descriptor capacity.
  The fixed descriptor pool is allocated lazily on the first nonempty admission.
  It remains charged after complete reads and is released on reset, `clear()` or
  destruction. Allocation uses `allocator<range>` to request exactly
  `max_ranges * sizeof(range)` bytes, avoiding a hidden `new[]` destructor cookie.
- `max_offset_span` caps each accepted STREAM end relative to the consumed
  offset. Large absolute offsets never size an allocation. RESET final size
  does not allocate data or need to fit this receive-storage span.

Both descriptor and payload allocation requests reserve
`server::resource::quic_reassembly_bytes` through the supplied budget and every
ancestor before allocation. Merges stage only affected ranges. Retained payload
never exceeds the payload limit, and old-plus-staged payload never exceeds twice
that limit; metadata has one explicitly bounded pool. Allocator bookkeeping
outside requested object storage is not part of the resource counter. Checked
configuration arithmetic ensures the maximum storage sum fits `size_t`;
invalid construction limits or unopened IDs throw `invalid_argument`.

Every rejected insertion preserves payload, offsets, final size, states, and
budget usage. Allocation failure or hierarchical budget refusal returns
`no_memory`. Payload capacity returns `byte_limit_exceeded`; range/span limits
return `gap_limit_exceeded`. Protocol-facing results distinguish invalid
62-bit arithmetic (`frame_encoding_error`), invalid ID/direction
(`stream_state_error`), inconsistent final size (`final_size_error`), and
conflicting retained overlap (`protocol_violation`). The caller decides how
these results affect the connection; local storage limits do not masquerade
as peer protocol errors.

## Executed local verification

Task-local receipts are in ignored `build/task154/` and
`build/task154-autotools/test/`. No shared environment was modified.

The existing STREAM/frame codec baseline passed 5 cases / 690 checks before
implementation. Initial reassembly failure stubs produced four failed checks;
stream identity/state stubs produced three failed assertions. New control tests
then produced 15 failures for missing RESET/STOP and validation behavior.
The allocation-request oracle caught an uncharged descriptor-array cookie
(one failure), fixed by exact allocator storage. The late STREAM-after-reset
oracle caught a stale highest-end counter (one failure). These RED logs are
`reassembly-red.log`, `stream-red.log`, `controls-red.log`, `storage-red.log`,
and `late-stream-red.log`. Additional boundary and permutation tests exercised
already implemented behavior without further production changes.

Final standalone tests passed with C++20 and
`-Wall -Wextra -Werror -pedantic`: reassembly 7 cases / 165 checks and stream
state 9 cases / 186 checks. Both passed again under ASan/UBSan with no reports.
Tests cover independent ordered bytes, copied input lifetime, partial reads,
bridging and nested overlap, duplicate storage usage, conflicting bytes, exact
and excessive byte/range/span limits, 62-bit arithmetic, eight merge orders,
ancestor refusal, staging rollback, allocation failure, reset/destructor release,
all ID classes/roles, unopened IDs, unavailable halves, FIN gaps/consumption,
FIN/RESET conflicts, zero-length FIN, terminal/control idempotence, and caller ACK
facts. Exact standalone commands (run from the worktree root):

```sh
mkdir -p build/task154
clang++ -std=c++20 -Wall -Wextra -Werror -pedantic \
  -DHTTPSERVER_COMPILATION -Isrc -Itest \
  test/unit/quic_reassembly_test.cpp src/detail/quic_reassembly.cpp \
  -o build/task154/quic-reassembly
build/task154/quic-reassembly
clang++ -std=c++20 -Wall -Wextra -Werror -pedantic \
  -DHTTPSERVER_COMPILATION -Isrc -Itest \
  test/unit/quic_stream_state_test.cpp src/detail/quic_stream_state.cpp \
  src/detail/quic_reassembly.cpp src/detail/quic_frame.cpp \
  src/detail/quic_varint.cpp -o build/task154/quic-stream-state
build/task154/quic-stream-state
```

Sanitizer builds use those same sources with
`-g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer` and separate
`*-sanitize` executable names. Final logs are `reassembly-final.log`,
`stream-final.log`, `reassembly-sanitize.log`, and `stream-sanitize.log`.

Autotools TLS-off v3core build and all four focused test executables passed
(23 cases / 1044 checks; no skips). The native TLS-off linkage audit also passed.
Commands:

```sh
bash bootstrap
mkdir -p build/task154-autotools
(cd build/task154-autotools && ../../configure \
  --disable-v3-tls --disable-examples CPPFLAGS=-I/opt/homebrew/include \
  LDFLAGS=-L/opt/homebrew/lib CXXFLAGS='-O0 -g')
make -C build/task154-autotools/src -j2 libhttpserver_v3core.la
make -C build/task154-autotools/test -j2 \
  quic_stream_state quic_reassembly quic_frame quic_codec_allocation
make -C build/task154-autotools/test check \
  check_PROGRAMS='quic_stream_state quic_reassembly quic_frame quic_codec_allocation' \
  TESTS='quic_stream_state quic_reassembly quic_frame quic_codec_allocation'
make -C build/task154-autotools check-v3-native-linkage
```

Bootstrap and native linking emitted existing Autotools and macOS linker
warnings. Standalone strict builds emitted no warnings. Changed-file cpplint,
CCN <= 10, and whitespace checks passed. Full repository complexity,
duplication, and file-size scripts still fail identically to a fresh HEAD source
export after normalizing paths: 17 existing complexity findings, three existing
clone groups, and the 509-SLOC `io_poll_backend.cpp` finding. No thresholds or
unrelated files were changed. Logs are `{complexity,duplication,file-size}-
{current,baseline}.log`; the baseline export is `build/task154-baseline/`.

BSD, Windows and other nonlocal checks remain unexecuted and assigned to CI/the
v3 PR by AGENTS.md. The task stays In Progress for caller-owned validation and
finalization. This phase does not claim transport interoperability or production
behavior.
