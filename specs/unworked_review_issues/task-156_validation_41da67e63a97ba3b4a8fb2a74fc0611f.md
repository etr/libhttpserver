# Unworked Review Issues

**Run:** 2026-10-08 06:02:16
**Task:** TASK-156
**Total:** 2 (0 critical, 2 major, 0 minor)

## Major

1. [ ] **performance-reviewer** | `src/detail/quic_repacketize.cpp:134` | algorithmic-complexity
   Each prepare_packet scans from status.begin() to the first eligible byte and then from there to the end of the entire eligible suffix before encode_information truncates it to the output buffer. For an N-byte retained segment split into B-byte packets this performs approximately N bytes of status inspection for each of N/B packets, or O(N^2/B) work even without loss or reordering. The accepted max_retained_bytes reaches 16 MiB. A task-local clang++ -O2 public-API probe using 1200-byte buffers, immediate successful ACKs and a sufficiently admitted resource budget measured 1 MiB/879 packets in 161.498 ms, 2 MiB/1758 packets in 550.282 ms, 4 MiB/3516 packets in 2213.105 ms, and 8 MiB/7032 packets in 8913.323 ms. The default 64 KiB case measured 1.553 ms. This is a substantial connection-owner CPU/throughput penalty at the supported larger capacities, although capacities remain finite.
   *Recommendation:* Maintain pending/probe cursors or bounded eligible interval metadata that update when ACK/loss changes delivery coverage. Bound discovery of the selected run by the maximum payload that can fit the supplied output before scanning its distant end; determine terminal eligibility from whether that bounded slice actually reaches the segment end. Preserve shared-transmission delivery semantics. Reuse the task-local probe to verify approximately linear total send cost as segment sizes double, plus existing loss/reorder/FIN tests.

2. [ ] **test-quality-reviewer** | `test/unit/quic_recovery_test.cpp:26` | missing-test
   The new commit_sent key_generation input and receive_ack application_generation output have no behavioral coverage: every new test commits the default generation 0, and none asserts application_generation. The documented caller key-owner seam could return no generation or the wrong generation while all current suites stay green. The scoped plan explicitly retains application key generation with sent packets, and docs/task-156-quic-recovery.md promises generation acknowledgement facts to the key owner.
   *Recommendation:* Add a focused API-level scenario committing Application packets with distinct nonzero generations, acknowledge individual packets and a range spanning generations, and assert the newly acknowledged maximum generation. Check that duplicate ACKs and ACKs in Initial/Handshake produce no Application generation event and that key generations retain one Application packet-number sequence.
