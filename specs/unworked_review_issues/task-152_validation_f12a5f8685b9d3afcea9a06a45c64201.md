# Unworked Review Issues

**Run:** 2026-10-08 02:52:12
**Task:** TASK-152
**Total:** 4 (0 critical, 2 major, 2 minor)

## Major

1. [ ] **performance-reviewer** | `src/detail/quic_packet_crypto.cpp:167` | algorithmic-complexity
   protect_quic_packet installs its cleansing guard over the entire scratch span capped at 65,535 bytes before encoding, then never narrows it to encoded.consumed. A caller reusing a maximum-sized buffer pays a 65,535-byte secure wipe for every small packet, including failures that wrote no scratch. On macOS the portable secure_zero performs volatile byte stores. A local 10,000-iteration optimized probe of the same 22-byte AES-GCM packet measured steady-state approximately 1.33 microseconds with 128-byte scratch, 1.60 microseconds with 1,200-byte scratch, and 16.2-17.0 microseconds with 65,535-byte scratch. This adds roughly an order of magnitude of packet-path CPU solely from unused capacity.
   *Recommendation:* Track and cleanse only scratch bytes actually written. The existing encode_quic_packet uses a measuring pass before any writes, so unsuccessful encoding has no touched region; after successful encoding, guard scratch.first(encoded.consumed) through every crypto error and successful return. Preserve output transactionality and cleansing of all staged plaintext. Update assertions/documentation that currently require wiping untouched scratch, then repeat a small-packet probe with maximum-sized scratch.

2. [ ] **test-quality-reviewer** | `test/unit/quic_key_state_test.cpp:42` | implementation-coupling
   The failed Handshake installation test asserts only that key.bytes().data() retains its address. The provider-failure derivation test repeats this pattern at test/unit/quic_crypto_test.cpp:226. Address identity is not the rollback contract: corruption of the retained key/IV/secret can pass, while preserving exactly the same material through different storage can fail. No packet protection/opening after these failures establishes the plan's requirement that the retained generation remains usable.
   *Recommendation:* Snapshot the retained suite/level and secret/key/IV/HP bytes before the failure, compare values afterward, and protect/open a Handshake packet using the retained directional keys. For provider failure, restore fetch properties and replay an independent Initial fixture with the retained keys. Remove storage-address assertions unless address stability is explicitly part of the contract.

## Minor

3. [ ] **code-simplifier** | `test/Makefile.am:2127` | code-structure
   The two adjacent NATIVE_V3_TLS blocks register the new crypto and key-state test executables separately, repeating the same conditional boundary.
   *Recommendation:* Place both executable registrations inside one NATIVE_V3_TLS block and keep the shared EXTRA_DIST declaration outside it; this removes repeated build logic without changing which targets are enabled.

4. [ ] **performance-reviewer** | `src/detail/quic_key_state.cpp:211` | missing-caching
   Every application open computes and removes header protection once in inspect_header for generation selection, then quic_open_checked recomputes the same mask in authenticate_staged. Header-protection keys remain unchanged across generations, so each application packet repeats an EVP fetch, context allocation, initialization and HP cipher operation for the identical sample.
   *Recommendation:* When optimizing this seam, pass the inspected mask or unprotected fields through a private staged-opening helper so generation selection and authenticated opening share one HP computation. Keep the fields untrusted until AEAD and the existing authenticated transition callback succeed. Benchmark application opens before prioritizing this non-blocking optimization.
