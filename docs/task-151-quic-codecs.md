# TASK-151 QUIC v1 codecs

Implementation is prepared on `task/TASK-151`, based on `v3` at
`1d045ae7f69d119160fa0670968b07ab0f2f1e24`. Task and index status remain
**In Progress** for runner-owned validation and finalization.

## Private interfaces and lifetime

The five `src/httpserver/detail/quic_*.hpp` codec headers are guarded by
`HTTPSERVER_COMPILATION` and registered as `noinst_HEADERS`. The native core
contains the implementations; these codecs need no TLS provider.

- `quic_codec.hpp` defines typed verdicts, checked cursors/writers and limits.
- `quic_varint.hpp` decodes general varints, supports explicitly wider writes,
  writes truncated packet numbers, and reconstructs them using caller-supplied
  largest-received context. It owns no packet-number-space state.
- `quic_packet.hpp` splits one envelope from a datagram. Initial, Handshake and
  0-RTT use their encoded lengths; short headers consume the remainder. Retry
  and Version Negotiation have their own envelope shapes. Unsupported versions
  have a separate verdict. 0-RTT recognition introduces no runtime acceptance.
- `quic_frame.hpp` yields one typed frame, with all core families and STREAM
  flags. Placement follows the RFC frame table, including sender restrictions
  and unidirectional stream direction. ACK ranges use a validated encoded view
  and an allocation-free inclusive-range iterator. Outbound ACKs also accept
  caller-provided typed ranges.
- `quic_transport_parameters.hpp` returns fixed typed values and defaults,
  rejects duplicate IDs (including unknown IDs and alternate integer widths),
  skips well-formed unknown values and checks known shapes/sender restrictions.

Packet, token, CID, range, data and reason views borrow the original input.
Keep input alive and stable while using a result or iterator. No decoder copies
payloads or creates attacker-sized collections. Decoder errors consume zero
bytes and preserve external cursors. Results are committed after complete
syntax validation; failed parameter parses return default values, without
partially populated fields.

Writers first validate and measure, then write to caller-provided storage.
Insufficient output or invalid values leave output unchanged. Input views must
remain stable across both passes and must not alias output. Successful writes
report their exact size through `consumed` and `value`.

Default limits are 65,535 input bytes, 4,096 frame iterations, 256 parameters
and 256 additional ACK ranges. Parameter duplicate tracking has an absolute
256-entry stack bound even when a caller requests a larger limit. Frame and
ACK work bounds are caller-configurable and also constrained by input length.
Padding runs are returned together; scanning remains bounded by the byte limit.
A count/input policy failure is distinct from malformed syntax or truncation.

## Protection and handshake boundary

Envelope parsing identifies immutable fields and opaque protected bytes. It
never infers packet-number length, reserved bits or key phase from ciphertext.
`decode_quic_unprotected_header` takes the externally unmasked first byte and
packet-number bytes and reconstructs the number. It checks that unmasking did
not change immutable header bits. `validate_quic_authenticated_header` supplies
its reserved-bit verdict only for use **after successful authentication**.

Packet writers take opaque payload/tag bytes; Retry integrity tags come from
the caller. They perform no cryptography. Datagram admission, Initial datagram
size enforcement, handshake/recovery/flow state and HTTP/3 journeys remain
outside these codecs. Frame decoding expects an authenticated clear payload.

Transport-parameter shape parsing preserves CID presence, including an empty
CID. `validate_quic_transport_parameters` separately checks required initial
and original CIDs, Retry presence and supplied handshake CID values. Callers
must supply the actual authenticated handshake context before accepting the
parameters. Shape parsing alone does not authenticate CIDs.

The codec contracts follow [RFC 9000 sections 16–19 and Appendix A](https://www.rfc-editor.org/rfc/rfc9000.html#section-16),
with the protection boundary from [RFC 9001 section 5.4](https://www.rfc-editor.org/rfc/rfc9001.html#section-5.4).

## Behavioral RED/GREEN evidence

Each codec slice first compiled against declarations and failure stubs, then
ran fixed-vector tests before its implementation was added. The initial runtime
RED receipts were 103 failed varint checks, two failed packet assertions,
34 failed frame checks, 13 failed parameter checks and three failed codec-fuzz
integration checks. The preferred-address fixture was corrected to the RFC's
24 address/port bytes before final GREEN verification.

Direct slice commands used Apple Clang and real codec translation units:

```sh
clang++ -std=c++20 -DHTTPSERVER_COMPILATION -Isrc -Itest \
  test/unit/quic_varint_test.cpp src/detail/quic_varint.cpp \
  -o build/quic-varint-red
build/quic-varint-red
```

The same pattern was used for packet, frame and parameter suites. GREEN
compiles added `-Wall -Wextra -Werror -pedantic` and each slice's real sources.
Frame serialization is in a separate translation unit to keep the reader and
writer small. An additional ACK-iterator regression first failed its third pair-consumption
assertion (four bytes instead of two), then passed after reporting each call's
consumption separately. The first range comes from the prefix, so its call
advances the iterator index while consuming zero encoded-range bytes.

The fuzz integration RED/GREEN used the registered
`quic_fuzz_replay` executable linked against the native core.

Fixed expected bytes cover RFC integer examples and transition octets,
all four packet-number widths, every supported packet envelope, every core
frame type and all eight STREAM flag combinations. Assertions also verify
expected typed fields, exact consumption, truncated prefixes, checked ACK
arithmetic, offset/length bounds, nonminimal frame-type rejection, CID/token/path
shapes, packet placement, parameter defaults/restrictions, alternate-width
known/unknown duplicates, the 256-parameter boundary and handshake CID checks.
Round trips supplement these independent expected-byte vectors.

## Final local verification

The task-local Autotools build uses Apple Clang 21.0.0, C++20, TLS off,
`CPPFLAGS=-I/opt/homebrew/include`, `LDFLAGS=-L/opt/homebrew/lib` and
`CXXFLAGS='-O0 -g'`. Existing legacy dependencies are discovered from Homebrew;
the new tests link only the native archive.

The planned `--enable-debug` configuration encountered a pre-existing
`DEBUG` macro redefinition under `-Werror`. A whole-core strict-warning build
also found existing `io_udp_backend.cpp` aggregate-initializer warnings.
The final ordinary configured build passes. Separately, every new production
translation unit compiled successfully with `-Wall -Wextra -Werror -pedantic`
in the complete standalone allocation-test build, without warning suppression.

```sh
(cd build/task151-codecs && ../../configure --disable-v3-tls --disable-examples \
  CPPFLAGS=-I/opt/homebrew/include LDFLAGS=-L/opt/homebrew/lib \
  CXXFLAGS='-O0 -g')

make -C build/task151-codecs/src -j2 libhttpserver_v3core.la
make -C build/task151-codecs/test -j2 \
  quic_varint quic_packet quic_frame quic_transport_parameters \
  quic_codec_allocation quic_invariant_header quic_datagram_dispatch \
  quic_network_harness quic_fuzz_replay

focused='quic_varint quic_packet quic_frame quic_transport_parameters quic_codec_allocation quic_invariant_header quic_datagram_dispatch quic_network_harness quic_fuzz_replay'
make -C build/task151-codecs/test check check_PROGRAMS="$focused" TESTS="$focused"
```

Both Automake selectors are needed: setting `TESTS` alone still attempts to
build unrelated legacy programs. The final focused run passed all nine
executables (44 cases, 2,416 checks), with no failures or skips. The existing datagram dispatch loopback test first
failed under the sandbox and passed when the same suite was rerun with socket
access. Final logs are under `build/task151-codecs/`.

The dedicated allocation executable prepares fixtures and test state before
its scope, intercepts ordinary/array/aligned/nothrow allocation entry points,
and aborts if any codec allocates within the scope. A separate self-test proves
the counter observes ordinary and aligned calls. Valid packet/header/frame/
parameter reads and writes, borrowed 65,500-byte STREAM/CRYPTO data, huge packet
and frame lengths, huge ACK counts and duplicate/huge transport tuples all
complete with zero scoped allocations.

The bounded fuzz target independently invokes scalar, envelope, frame and
parameter parsers on each raw input. It preserves the invariant-header target's
original return meaning and 4-KiB input cap. It checks deterministic verdicts,
consumption, cursor progress, borrowed-view bounds, ACK iteration and stable
canonical re-encoding. Parser work is capped at 256 frames, 64 additional ACK
ranges and 64 parameters for each fuzz call. Nine new committed seeds join the
existing fixed-seed replay/mutation corpus.

```sh
bash scripts/run-v3-protocol-fuzz.sh \
  --compiler /opt/homebrew/opt/llvm/bin/clang++ \
  --build-dir build/task151-codec-fuzz-final \
  --targets quic_parser --runs 2000 --seconds 30
```

Homebrew LLVM 22.1.8 completed 2,000 instrumented runs with ASan, UBSan and
libFuzzer, a 30-second cap, 4-KiB inputs, seed 128 and a 512-MiB RSS limit.
All newly exercised sources are compiled with instrumentation. The source
corpus remains unchanged; libFuzzer mutates a task-local copy.

Changed-file cpplint and new-code CCN <= 10 checks pass. Every new production
file is below the 500-SLOC ceiling. Repository-wide file-size, complexity and
copy/paste gates have pre-existing failures: a 509-SLOC `io_poll_backend.cpp`,
existing native I/O/invariant-parser complexity findings, and three existing
copy/paste groups. Running those same scripts against an extracted HEAD
snapshot produced identical findings; none involve new codec code. Receipt
logs are `build/baseline-*.log`, `build/{file-size,complexity,duplication}.log`,
`build/cpplint.log` and `build/new-code-complexity.log`.

`git diff --check` passes. BSD, Windows and other nonlocal platform checks are
unexecuted and CI/v3-PR-owned. These results establish local codec behavior;
there is no interoperability, HTTP/3 request, deployment or production claim.
