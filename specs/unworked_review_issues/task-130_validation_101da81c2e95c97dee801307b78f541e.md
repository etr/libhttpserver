# Unworked Review Issues

**Run:** 2026-10-06 22:02:28
**Task:** TASK-130
**Total:** 4 (0 critical, 1 major, 3 minor)

## Major

1. [ ] **code-quality-reviewer** | `src/detail/tls_io_backend.cpp:326` | error-handling
   pump() unconditionally steps reader immediately after writer. A sufficiently large SSL_write_ex call returns WANT_WRITE because the bounded BIO is full; apply_step() discards that output-wait state, and the next step can invoke SSL_read_ex on the same SSL object before the write retry completes. OpenSSL explicitly requires that no other operation capable of provider I/O run between a write WANT_WRITE and retry of that write. Thus an admitted concurrent read reaches an unsupported provider state sequence. The existing full-duplex test uses a small message, while the 100,000-byte backpressure test has no concurrent read on the writing endpoint, so neither verifies this combination. This is source/API-contract evidence; no reproduced runtime failure is claimed. See [OpenSSL 3.5 SSL_get_error documentation](https://docs.openssl.org/3.5/man3/SSL_get_error/). This is not a QUIC-only rule: the identical prohibition exists in the [OpenSSL 3.0 documentation](https://docs.openssl.org/3.0/man3/SSL_get_error/), before native QUIC support, and remains a standalone paragraph at doc/man3/SSL_get_error.pod:98-103 in the selected OpenSSL 3.5.9 source.
   *Recommendation:* Retain the writer retry state and suppress other SSL I/O while its last result is WANT_WRITE; continue draining/sending ciphertext and retry the same write arguments before resuming the reader. Add a focused regression that combines an admitted local read with a large backpressured write and verifies provider-call ordering and exactly-once results.

## Minor

2. [ ] **architecture-alignment-checker** | `src/httpserver/detail/io_operation.hpp:233` | interface-contract
   The shared io_backend contract says request_cancel returning ok means the target was delivered cancelled. The new TLS override instead enqueues cancellation and returns ok before terminal arbitration (src/detail/tls_io_backend.cpp:366-374); success can still win. The TLS facade and evidence correctly document accepted deferred cancellation, but generic callers see a contradictory contract. This is a documentation inconsistency in the private operation boundary described by architecture section 3.4 and DR-V3-004; current stop-token consumers rely on the operation result rather than this return value.
   *Recommendation:* Update the shared request_cancel contract to allow accepted deferred cancellation and state that the target operation result is authoritative for races, preserving the serialized TLS cancellation implementation.

3. [ ] **security-reviewer** | `src/detail/tls_io_backend.cpp:326` | tls-provider-state
   pump() invokes step(reader) immediately after step(writer), including when SSL_write_ex returns WANT_WRITE. The provider requires retrying that write before any other SSL operation capable of I/O. This rule also applies to TCP memory BIOs: it appears in the pre-QUIC OpenSSL 3.0 documentation, and the selected 3.5.9 TCP record implementation allows SSL_read to enter handshake_func on a post-handshake message (ssl/record/rec_layer_s3.c around lines 1030-1040). Concurrent admitted reads under output backpressure can therefore enter an unsupported provider call sequence. This is a provider-state hardening issue; no cross-session security exploit was demonstrated. [OpenSSL 3.5 SSL_get_error](https://docs.openssl.org/3.5/man3/SSL_get_error/), [OpenSSL 3.0 SSL_get_error](https://docs.openssl.org/3.0/man3/SSL_get_error/).
   *Recommendation:* Track a writer parked on WANT_WRITE and suppress reader SSL calls until that same write has been retried after output capacity becomes available; retain its plaintext arguments. Add a focused regression covering concurrent reads with a large backpressured write and peer post-handshake control input.

4. [ ] **spec-alignment-checker** | `src/detail/tls_session.cpp:98` | task-goal
   The prepared design calls for plaintext read/write io_backend semantics, but a valid zero-length read waits for peer input instead of completing ok with zero bytes as io_poll_backend::read_step does at lines 598-602. A bounded probe using the built native TLS archive completed the handshake, submitted an empty read, and drove all events: applied=false and one raw read remained pending; a subsequent nonempty write still succeeded. This is a minor private-adapter semantic variance; the required normal read, EOF, timeout, cancellation, and shutdown paths are implemented and tested.
   *Recommendation:* Complete an admitted empty plaintext read with ok and zero transferred bytes without invoking SSL_read_ex; add a focused assertion that it neither submits raw input nor waits for a deadline.
