# QUIC parser seeds

Each `.seed` is a raw byte input, including an empty input. The invariant-header
parser still tries absent, zero, two, twenty and invalid twenty-one byte short
CID lengths. Its original routing-success return value is preserved.

The same input is independently offered to packet-envelope, frame and client/
server transport-parameter decoders. Scalar and packet-number primitives are
also exercised. Inputs are capped at 4 KiB; frame, ACK and parameter work limits
remain fixed in the fuzz harness. There is no cryptography or connection state.

The existing seeds cover long/short invariant headers, version zero, maximum
CIDs, missing fixed bits, truncation and oversized CID lengths. The `codec-*`
seeds cover a valid Initial envelope, ACK/ECN, STREAM, transport parameters,
huge advertised lengths, ACK underflow, alternate-width duplicate parameter
IDs, nonminimal frame types and truncated scalars. These are separate decoder
invocations, so raw invariant-header seeds retain their original meaning.

The replay test uses a fixed mutation schedule. The instrumented runner copies
the corpus into its build directory before allowing libFuzzer to mutate it.
