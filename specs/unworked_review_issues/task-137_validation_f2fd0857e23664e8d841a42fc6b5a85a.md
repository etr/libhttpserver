# Unworked Review Issues

**Run:** 2026-10-07 02:38:16
**Task:** TASK-137
**Total:** 2 (0 critical, 1 major, 1 minor)

## Major

1. [ ] **test-quality-reviewer** | `test/unit/hpack_primitives_test.cpp:124` | missing-test
   The all-octet Huffman coverage derives its wire oracle from the production encoder, while the decoder trie uses the same production code table. Appendix C literals are ASCII and the six independent code fixtures do not cover most binary octets. A task-local header overlay swapping complete code/length entries for symbols 128 and 129 still passed hpack_primitives (1409 checks), hpack_static_table (321 checks), and hpack_corpus (312 checks, including deterministic mutations). This incorrect HPACK mapping would encode/decode those octets incompatibly with RFC 7541 peers despite every current focused check passing. Evidence: /private/tmp/hpack-test-quality-q4y84pyp/*.log; repository source was unchanged.
   *Recommendation:* Add an independently fixed Appendix B wire fixture covering all 256 octets through hpack_encode_huffman and hpack_decode_huffman, or equivalent independent per-octet encoded-byte vectors. Do not derive expected bytes from the production table. Confirm the 128/129 swap is detected while retaining round trips for algorithmic coverage.

## Minor

2. [ ] **test-quality-reviewer** | `test/unit/hpack_primitives_test.cpp:230` | implementation-coupling
   The successful 4 KiB decode asserts exactly one allocation and at most 16 bytes of capacity rounding, based on libc++ storage behavior. Rejection before proportional allocation is the task acceptance contract; these narrower successful-output details can reject a behavior-preserving change to string construction or allocator capacity policy. The required local baseline passes, so this is a maintainability concern rather than a missing local gate or nonlocal-platform blocker.
   *Recommendation:* Keep the zero-allocation rejection checks and successful decoded-byte assertion. Express any successful allocation check as a documented, implementation-independent bounded-resource requirement; otherwise drop the exact call count and libc++ rounding threshold.
