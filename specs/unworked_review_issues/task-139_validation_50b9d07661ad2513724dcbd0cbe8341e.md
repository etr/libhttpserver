# Unworked Review Issues

**Run:** 2026-10-07 04:01:07
**Task:** TASK-139
**Total:** 2 (0 critical, 1 major, 1 minor)

## Major

1. [ ] **test-quality-reviewer** | `test/unit/http2_settings_test.cpp:35` | missing-test
   Received SETTINGS state is not fully protected: this test sends ENABLE_PUSH=0 and MAX_HEADER_LIST_SIZE=0 but never asserts either resulting peer value; the endpoint loop at lines 109-115 likewise asserts only acceptance/ACK size. Removing the corresponding state assignments from apply_setting would leave all HTTP/2 tests passing. In addition, the only truncated control-payload transcript is PING, so no test covers the planned SETTINGS transaction guarantee that complete earlier tuples followed by a truncated tuple leave peer/HPACK state unchanged and emit no SETTINGS ACK.
   *Recommendation:* Add explicit received enable_push and max_header_list_size assertions, including zero and UINT32_MAX where legal. Add a segmented SETTINGS frame containing a table-size change and another setting, truncate its final tuple, then assert unchanged peer/encoder state and no SETTINGS ACK before and after EOF (only the defined terminal error output).

## Minor

2. [ ] **performance-reviewer** | `src/detail/http2_frame.cpp:173` | algorithmic-complexity
   Unknown frame payloads are scanned byte by byte although no payload content is retained or interpreted. This makes discard work proportional to the declared payload instead of the number of input chunks. A task-local Apple clang -O2 probe processing 64 coalesced unknown frames took 2.734 ms at the default 16,384-byte cap and 1,202.564 ms at the permitted 16,777,215-byte cap. No allocation or unbounded growth occurs, and the machine is currently private, so this is non-blocking.
   *Recommendation:* For unknown frame types, advance used and payload_used_ by count directly and return before the byte loop. Preserve segmented consumption and frame boundaries; use the existing unknown-frame conformance checks to verify behavior.
