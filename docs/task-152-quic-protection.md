# TASK-152: QUIC v1 protection and key ownership

Implementation baseline: `46df39e1` on `task/TASK-152`, based on `v3`.
Implementation is ready for coordinator-owned validation; the task remains
In Progress until that separate workflow finishes.

## Protection boundary

The private native TLS archive contains EVP-only QUIC cryptography. HKDF uses
OpenSSL 3.5 EVP_KDF extract/expand modes and TLS 1.3 labels. Initial protection
uses the fixed v1 salt, SHA-256 and AES-128-GCM. Traffic protection supports
AES-128-GCM/SHA-256, AES-256-GCM/SHA-384 and ChaCha20-Poly1305/SHA-256.
No installed header contains OpenSSL types or these interfaces. TLS-off builds
omit the new implementation and test executables.

`quic_crypto.hpp` exposes move-only owned material, transactional derivation,
nonce and header-mask helpers, packet protection/opening, and Retry integrity.
Packet wrappers reject incompatible levels, 0-RTT, caller-supplied tags, overlap,
and malformed or insufficient buffers. Retry and Version Negotiation are outside
packet AEAD protection; Retry has its own integrity functions.

Packets use the existing length-delimited envelope. Header samples stay inside
that packet, even in a coalesced datagram. Packet numbers XOR the full 62-bit
number into a 12-byte IV; truncated on-wire numbers serve only header encoding.
AES header protection uses ECB without padding. ChaCha header protection passes
the complete 16-byte sample into EVP's raw IV without changing its state words.
Retry authenticates separate length/CID/header AAD segments without a packet-sized
allocation. Verification authenticates the received tag with EVP.

All packet input views, output, and scratch must be disjoint. The caller supplies
padding and the short-header CID length. Scratch holds at most 65,535 packet bytes;
protection clears its bounded scratch region and opening clears the packet region
used. Output stays unchanged on errors, including authentication, reserved-bit,
and authenticated key-transition failures. Tentative plaintext exists only in
scratch until all checks pass. EVP objects use RAII; owned secrets use existing
`secure_bytes`; temporary secret, nonce and mask arrays use `secure_zero`.

## Connection-owned lifecycle

`quic_key_state` is serialized on its owner thread. It owns directional Initial,
Handshake and application keys, one next write generation, and bounded
current/next/previous read generations. Initial replacement and Handshake
replacement are transactional. Application installation is one-time per direction;
discarded levels cannot be reinstalled. A cleared owner cannot resurrect keys.

The caller supplies handshake-completion/confirmation and generation-ACK facts.
The owner checks monotonic successful write packet numbers but does not allocate
them. Initial replacement retains this boundary. Write updates retire old material
immediately, toggle the phase, derive `quic ku`, and retain the directional HP key.
Read promotion requires successful authentication. Selection uses authenticated
current-generation packet-number boundaries; key-transition error reporting also
waits for authentication, before publishing plaintext.

After a peer read update, sending is blocked until `respond_to_peer_update`
advances write keys to that generation. The transport must call this before
acknowledging the triggering packet. It explicitly retires previous read keys
and prepares the next read generation according to its PTO/recovery policy.
This module has no sockets, timers, SSL sessions, recovery engine, CRYPTO streams,
admission-token policy, or transport scheduling.

Per-generation encryption limits are capped at 2^23 for AES-GCM and at the QUIC
packet-number ceiling for ChaCha20-Poly1305. Authentication failures accumulate
across all keys and generations, with caps of 2^52 for AES-GCM and 2^36 for
ChaCha20-Poly1305. Policy options can reduce these caps. Limits stop processing
before counters can overflow; application confidentiality exhaustion returns
`update_required`. Generation counters also reject arithmetic wraparound.

## Fixed fixtures and development evidence

[RFC 9001 Appendix A](https://www.rfc-editor.org/rfc/rfc9001.html#appendix-A)
provides exact client/server secrets, keys, IVs, HP keys, complete Initial packets,
Retry bytes, and the ChaCha packet/mask/next-secret assertions. The complete client
payload is padded to the published 1,162 bytes; wire comparison covers all 1,200
protected packet bytes. Both directional Initial packets open and protect exactly.

Additional pinned AES-256/SHA-384 fixtures use the secret bytes 00 through 2f,
packet number `0102030405060708`, CID `abcd`, and plaintext `01020304`.
They were generated independently with Python `hmac`/SHA-384 and cryptography
50.0.2 AESGCM/ECB, including a correctly authenticated reserved-bit violation.
Production uses OpenSSL 3.5.9, not that independent fixture generator.

Development followed assertion-level RED/GREEN slices:

- Derivation failure stubs: 2 failing assertions, then 21 passing checks.
- Packet/nonce/mask/Retry failure stubs: 11 failures, then 62 passing checks.
- Lifecycle failure stubs: 3 failing assertions, then 57 passing checks.
- Missing peer-response send gate: 2 failures, then the gate passed.
- Unauthenticated packet-number transition error: 1 failure, then authenticated
  validation passed while preserving output.
- Incorrect Initial cipher acceptance: 1 failure, then fixed-v1-suite rejection
  passed.

The final crypto executable has 9 tests / 180 checks; lifecycle has 6 tests /
96 checks. Additional assertions cover every PN width, full PN boundaries,
coalescing/sample truncation, overlap, short buffers, oversized input, wrong
keys/directions, header/ciphertext/tag corruption, provider fetch failures,
transactional installation, cleansing observed before allocation release,
reordering, retirement, phase wrap, authorization, and bounded usage limits.
Provider failure tests restrict EVP fetch properties in their serialized process
and restore them before assertions. No allocation-failure injection was performed.

## Reproducible local checks

Prerequisites: macOS C++20 toolchain, autotools, Homebrew legacy prerequisites,
and the existing OpenSSL 3.5.9 provider at `/private/tmp/task129-provider/install`.
Run from this worktree; build directories are task-local VPATH directories.

```sh
./bootstrap
mkdir -p build/task152-on build/task152-off build/task152-sanitize
cd build/task152-on
../../configure --enable-v3-tls --disable-examples \
  V3_TLS_CFLAGS=-I/private/tmp/task129-provider/install/include \
  V3_TLS_LIBS='-L/private/tmp/task129-provider/install/lib -lssl -lcrypto' \
  CPPFLAGS=-I/opt/homebrew/include LDFLAGS=-L/opt/homebrew/lib \
  CXXFLAGS='-std=c++20 -O0 -g'
make -C src -j2 libhttpserver_v3core.la
make -C test -j2 quic_crypto quic_key_state quic_packet quic_varint \
  quic_codec_allocation tls_psk_contract v3_native_linkage
focused='quic_crypto quic_key_state quic_packet quic_varint quic_codec_allocation tls_psk_contract'
make -C test check check_PROGRAMS="$focused" TESTS="$focused"
make check-v3-native-linkage
```

TLS-on: 6/6 focused executables passed, no skips; native linkage audit passed.
The unchanged baseline codec/allocation/PSK tests also passed before implementation.

TLS-off uses `--disable-v3-tls`, the same prerequisite/C++20 flags, and no provider
flags. The native archive built; `quic_packet`, `quic_varint`, and
`quic_codec_allocation` passed (3/3, no skips); native linkage audit passed with
no OpenSSL dependency. The crypto executables are absent from `check_PROGRAMS`.

The sanitizer configuration adds `-fsanitize=address,undefined
-fno-omit-frame-pointer` to CXXFLAGS and `-fsanitize=address,undefined` to LDFLAGS.
It uses explicit provider `libssl.a`/`libcrypto.a` paths in V3_TLS_LIBS. Both new
executables passed under ASan/UBSan (2/2, no skips). The reused OpenSSL static
archives are not sanitizer-instrumented. This proves local wrapper behavior,
not sanitizer coverage inside the provider.

Changed-file cpplint passed with the repository's extensions/headers settings;
changed production functions passed lizard CCN <=10; `git diff --check` passed.
Full repository complexity, duplication, and file-size gates still fail on
pre-existing code. Exact output matches a temporary export of baseline `46df39e1`
after path normalization: 17 complexity warnings; three CPD clone groups in
unchanged I/O interfaces, body/response interfaces and task awaiters; one
509-SLOC `io_poll_backend.cpp` finding. No new source participates in these
findings, and no thresholds or unrelated source files were changed.

BSD, Windows and other nonlocal checks remain assigned to CI/the v3 PR by
AGENTS.md. This implementation makes no HTTP/3 request, interoperability,
deployment, or production claim. Coordinator validation, commits and integration
remain outside this implementation phase.
