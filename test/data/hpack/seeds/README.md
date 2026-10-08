# HPACK replay seeds

Each `.seed` starts with eight control bytes for `hpack_fuzz_input`, followed by
independent RFC 7541 example octets or a malformed representation. The replay
also drives the existing two-block decoder and table-state checks. Seeds cover
invalid indices, truncated/overflow integers, truncated literals, EOS, excessive
or zero Huffman padding, misplaced/oversized updates and minimum/final updates.

The selected libFuzzer lane copies these immutable seeds into a build-local
writable corpus. The HPACK target caps generated inputs at 512 bytes.
