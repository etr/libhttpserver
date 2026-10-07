# HPACK primitive fuzzing

TASK-137 covers integers, raw/Huffman literals, Huffman payloads, and static
lookups. Connection tables and complete field sections belong to TASK-138.
RFC 7541 Appendix C.2-C.6 bytes and explicit primitive extraction offsets are
in `test/data/hpack/rfc7541.hpp`; the ordinary `hpack_corpus` check replays them
and 8,192 deterministic mutations with seed `0x7541137`.

`hpack_fuzz_input` accepts at most 512 bytes. The first eight bytes control
prefix width, integer value/octet limits, payload/decoded limits, output limit,
and integer high bits. Outputs are capped at 64 bytes; invalid controls are
intentional. The remaining bytes are borrowed wire input. The entry checks
bounds, absence of committed failed output, determinism, input preservation,
and encode/decode round trips, including trailing string input.

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

Keep any minimized finding as a deterministic regression before resuming fuzzing.
BSD, Windows, and other nonlocal platform evidence belongs to CI and the v3 PR.
