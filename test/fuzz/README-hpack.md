# HPACK primitive and connection fuzzing

The entry preserves TASK-137 integer, raw/Huffman string, Huffman payload,
and static lookup checks. TASK-138 adds complete sections and connection-owned
tables. `hpack_corpus` checks all RFC 7541 Appendix C.2-C.6 blocks against fixed
ordered fields and table snapshots, with independent request/response sequence
contexts and the response examples' negotiated 256-byte capacity. It also runs
8,192 seeded primitive/section mutations (`0x7541137`) and per-octet section tag
mutations and truncations.

`hpack_fuzz_input` accepts at most 512 bytes. The first eight bytes control
prefix width, integer value/octet limits, payload/decoded limits, output limit,
and integer high bits. Outputs are capped at 64 bytes; invalid controls are
intentional. The remaining bytes are borrowed wire input. The entry checks
bounds, absence of committed failed output, determinism, input preservation,
and encode/decode round trips, including trailing string input.

The additional section exercise borrows at most 64 wire bytes, processes two
consecutive blocks with one connection owner, and seeds a prior dynamic entry.
Each directional table is capped at 128 bytes and shares a 256-byte hierarchical
budget. Compressed output is capped at 64 bytes, expanded occurrences at 128
bytes, and field count at four. The second block uses input-derived smaller
limits. Assertions check sticky terminal failure, unpublished failed outputs,
expanded accounting, table bounds, and retained reservation bounds. An
independent decoder (at most 128 additional retained table bytes) replays each
successful encoder output in order and checks fields and never-indexed policy.

With a Clang installation containing libFuzzer, build from the source root:

```sh
clang++ -std=c++20 -O1 -g -fsanitize=fuzzer,address,undefined \
  -fno-omit-frame-pointer -DHTTPSERVER_COMPILATION -DHPACK_LIBFUZZER \
  -Isrc -Itest test/fuzz/hpack_fuzz.cpp -o build/hpack_fuzzer
mkdir -p build/hpack-fuzz-corpus
build/hpack_fuzzer build/hpack-fuzz-corpus -max_total_time=30 \
  -timeout=2 -max_len=512 -rss_limit_mb=512 -seed=7541137
build/hpack_fuzzer build/hpack-fuzz-corpus -runs=0 -max_len=512
```

Apple Clang may lack `libclang_rt.fuzzer_osx.a`; Homebrew LLVM's `clang++`
provides the local libFuzzer runtime. ASan/UBSan are executed in this smoke
run. An empty corpus is supported. To seed published examples, prepend eight
control bytes (for example `05 ff ff 0b 40 40 40 00`) to the hex-decoded
fixture blocks. Add integer zero chains, overflowing continuations, huge
lengths with missing data, truncated literals, EOS/padding errors, high-bit
bytes, and expanded-budget edges. The deterministic check includes these
families; production decoders receive them directly in the unit tests.

`hpack_primitives` also observes real `operator new` calls: large malformed,
expanded-over-limit, encoded-over-limit, truncated, and encoder-over-limit
inputs request zero output allocations. Valid 4 KiB decoded output requests
one string allocation after admission (standard string capacity may round up).
`hpack_connection` observes aggregate raw/Huffman section refusal and table
reservation refusal before input-sized octet copies, without assuming successful
allocation counts or a standard-library capacity growth policy.

Keep any minimized finding as a deterministic regression before resuming fuzzing.
BSD, Windows, and other nonlocal platform evidence belongs to CI and the v3 PR.
