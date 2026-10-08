# Unworked Review Issues

**Run:** 2026-10-08 00:29:50
**Task:** TASK-150
**Total:** 1 (0 critical, 1 major, 0 minor)

## Major

1. [ ] **test-quality-reviewer** | `test/unit/quic_fuzz_replay_test.cpp:27` | missing-test
   The sole scripted state-decoder regression test checks only trace length, its Q prefix and equality with a second replay; the corpus assertion at line 46 also checks only trace length. Neither verifies any decoded action outcome. A decoder that ignores clock advance (opcode 5), or returns the 69-byte empty-rig teardown trace for every input, satisfies all these assertions and the fresh-rig accounting bounds. The direct network harness tests do not call this decoder, so they cannot catch this loss of state-fuzz coverage.
   *Recommendation:* Add independently specified assertions for the existing decoded script: emitted and delivered packet events, duplication/retirement dispatch outcomes, timer completion and logical time, and final zero-resource event. A small explicit expected trace or assertions over decoded trace fields are sufficient; keep self-replay and mutation coverage as complementary checks.
