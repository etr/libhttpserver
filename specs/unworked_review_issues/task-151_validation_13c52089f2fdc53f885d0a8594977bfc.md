# Unworked Review Issues

**Run:** 2026-10-08 01:42:59
**Task:** TASK-151
**Total:** 1 (0 critical, 0 major, 1 minor)

## Minor

1. [ ] **code-simplifier** | `src/detail/quic_frame.cpp:24` | readability
   The 31-entry packet-placement mask table is one unlabelled numeric row. The preceding comment explains mask bits but does not identify entries, so checking a frame family against its wire type requires manually counting indexes; this is a maintenance risk when adding or correcting frame placement.
   *Recommendation:* Optionally format the table into commented frame-family groups or use named packet-mask constants while keeping the direct lookup and existing behavior.
